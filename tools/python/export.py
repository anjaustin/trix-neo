"""
export.py — Export trained BNNEncoder to TriX chip format.

Produces:
  <out_dir>/<chip_name>_layer<N>.bin  — int8 weight file, [output_dim, input_dim] row-major
  <out_dir>/<chip_name>.trix          — YAML spec loadable by trix_load()

Usage:
    from export import export_chip
    export_chip(model, X_train, y_train, labels, out_dir="chips", chip_name="bearing")
"""

import os
import numpy as np
import torch


def _compute_binary_codes_int8(weight_arrays: list, X: np.ndarray,
                                batch_size: int = 256) -> np.ndarray:
    """
    Simulate the C runtime's int8 forward pass to compute binary codes.

    Uses int32 dot products with int8 weights (matching SDOT accumulation in C),
    ReLU between layers, and hard sign at the output. This ensures prototypes
    and thresholds are calibrated against what the C runtime will actually produce.

    Args:
        weight_arrays: list of int8 numpy arrays [N, K], one per encoder layer
        X:             int8 [B, K] input
        batch_size:    batch size for processing

    Returns:
        codes: uint8 [B, 512] binary codes
    """
    codes = []
    for i in range(0, len(X), batch_size):
        x = X[i:i+batch_size].astype(np.int32)          # [B, K]
        for layer_idx, W in enumerate(weight_arrays):
            x = x @ W.T.astype(np.int32)                # [B, N] via int32 dot
            if layer_idx < len(weight_arrays) - 1:
                # C runtime uses clamp_to_int8 (NOT ReLU) between layers.
                # This matches linear_runtime.c:clamp_to_int8() exactly.
                x = np.clip(x, -127, 127).astype(np.int8).astype(np.int32)
        codes.append((x > 0).astype(np.uint8))
    return np.concatenate(codes, axis=0)  # [B, 512] uint8


def _majority_vote_prototype(codes: np.ndarray) -> np.ndarray:
    """
    Per-bit majority vote over a set of binary codes.
    Returns {0,1}^512 prototype.
    """
    return (codes.mean(axis=0) >= 0.5).astype(np.uint8)


def _hamming_distance(a: np.ndarray, b: np.ndarray) -> int:
    """Hamming distance between two {0,1}^512 vectors."""
    return int(np.sum(a != b))


def _code_to_hex(code: np.ndarray) -> str:
    """
    Convert {0,1}^512 binary code to 128-char hex string (MSB-first per byte).
    Matches linear_sign_binarize() byte layout: bit 7 of byte 0 = code[0].
    """
    assert len(code) == 512
    result = []
    for byte_idx in range(64):
        byte_val = 0
        for bit_idx in range(8):
            if code[byte_idx * 8 + bit_idx]:
                byte_val |= (0x80 >> bit_idx)   # MSB-first
        result.append(f"{byte_val:02x}")
    return "".join(result)


def _percentile_threshold(codes: np.ndarray, prototype: np.ndarray,
                           percentile: float = 95.0) -> int:
    """
    Threshold = Nth percentile of intra-class Hamming distances to prototype.
    Conservative: catches 95% of training examples.
    Minimum threshold: 10 (avoid zero-threshold chips that reject everything).
    """
    distances = [_hamming_distance(c, prototype) for c in codes]
    threshold = int(np.percentile(distances, percentile))
    return max(threshold, 10)


def export_chip(model,
                X_train: np.ndarray,
                y_train: np.ndarray,
                labels: list[str],
                out_dir: str = "chips",
                chip_name: str = "trix_chip",
                chip_version: str = "1.0.0",
                threshold_percentile: float = 95.0) -> str:
    """
    Export trained model to .trix chip file + weight binaries.

    Args:
        model:       Trained BNNEncoder
        X_train:     int8 [N, K] training inputs (for prototype extraction)
        y_train:     int64 [N] class labels
        labels:      list of class name strings (len = num_classes)
        out_dir:     output directory (created if needed)
        chip_name:   chip identifier (used for filenames)
        chip_version: version string in chip metadata
        threshold_percentile: Hamming threshold coverage (default: 95th pct)

    Returns:
        Path to the .trix file.
    """
    os.makedirs(out_dir, exist_ok=True)
    num_classes = len(labels)
    weight_list = model.export_weights_int8()
    num_layers = len(weight_list)
    assert num_layers >= 1, "Model must have at least one encoder layer"

    # 1. Export weight binaries
    weight_paths = []
    for layer_idx, W_int8 in enumerate(weight_list):
        path = os.path.join(out_dir, f"{chip_name}_layer{layer_idx}.bin")
        W_int8.tofile(path)   # row-major int8, [N, K]
        weight_paths.append(os.path.abspath(path))
        print(f"  Wrote {path}: shape={W_int8.shape}, dtype={W_int8.dtype}")

    # 2. Compute binary codes using int8-quantized weights (matches C runtime exactly).
    # Using float weights here would compute codes inconsistent with what the C runtime
    # produces, causing prototype/threshold mismatch at inference time.
    print("  Computing binary codes for prototype extraction (int8 simulation)...")
    codes = _compute_binary_codes_int8(weight_list, X_train)  # [N, 512]

    # 3. Compute per-class prototypes and thresholds
    prototypes = {}
    thresholds = {}
    for cls_idx, label in enumerate(labels):
        mask = y_train == cls_idx
        if not np.any(mask):
            raise ValueError(f"No training examples for class '{label}'")
        cls_codes = codes[mask]
        proto = _majority_vote_prototype(cls_codes)
        thresh = _percentile_threshold(cls_codes, proto, threshold_percentile)
        prototypes[label] = proto
        thresholds[label] = thresh
        print(f"  {label}: {np.sum(mask)} examples, threshold={thresh}")

    # 4. Build linear layer spec list
    # Enumerate model.encoder Sequential; skip non-Linear modules (e.g. ReLU).
    # Use a separate weight_idx counter — layer_idx from enumerate() skips non-Linear
    # modules but weight_paths[] is indexed only over Linear modules.
    layer_specs = []
    weight_idx = 0
    for module in model.encoder:
        if hasattr(module, 'weight'):
            W = module.weight.detach()
            N, K = W.shape
            layer_specs.append((K, N, weight_paths[weight_idx]))
            weight_idx += 1

    # 5. Write .trix YAML spec
    # Section key is "linear:" (not "linear_layers:") — matches softchip_parse().
    # Layer format is name-keyed map (not YAML list) — parser reads bare key as layer name.
    # Property key is "weights:" (not "weights_file:") — matches parser strcmp branch.
    trix_path = os.path.join(out_dir, f"{chip_name}.trix")
    with open(trix_path, "w") as f:
        f.write(f"softchip:\n")
        f.write(f"  name: {chip_name}\n")
        f.write(f"  version: {chip_version}\n")
        f.write(f"state:\n  bits: 512\n")
        f.write(f"linear:\n")
        for layer_idx, (K, N, wpath) in enumerate(layer_specs):
            f.write(f"  encoder_layer{layer_idx}:\n")
            f.write(f"    input_dim: {K}\n")
            f.write(f"    output_dim: {N}\n")
            f.write(f"    weights: {wpath}\n")
        f.write(f"signatures:\n")
        for label, proto in prototypes.items():
            hex_str = _code_to_hex(proto)
            f.write(f"  {label}:\n")
            f.write(f"    pattern: {hex_str}\n")
            f.write(f"    threshold: {thresholds[label]}\n")
        f.write(f"inference:\n  mode: first_match\n  default: unknown\n")

    print(f"  Wrote {trix_path}")
    return trix_path
