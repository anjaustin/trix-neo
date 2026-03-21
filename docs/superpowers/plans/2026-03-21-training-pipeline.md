# Training Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an offline training pipeline that trains a binary metric encoder, quantizes weights to int8, and exports a deployable `.trix` chip file with weight binaries.

**Architecture:** A PyTorch BNN encoder maps labeled input windows to 512-bit binary codes using a sign-binarized output layer trained with classification loss + binary regularization. After training, float weights are clamped/rounded to int8, per-class prototype signatures are extracted via majority vote, thresholds are set from the 95th-percentile intra-class Hamming distance, and the result is written as a `.trix` spec file + raw int8 weight `.bin` files loadable by `trix_load()`.

**Tech Stack:** Python 3.9+, PyTorch 2.x, NumPy, SciPy (`.mat` loading), existing `tools/python/trix.py` ctypes bindings

---

## File Map

| File | Action | Purpose |
|---|---|---|
| `tools/python/requirements.txt` | Create | Pin deps: torch, numpy, scipy |
| `tools/python/model.py` | Create | BNN encoder (PyTorch) with STE binarization |
| `tools/python/dataset.py` | Create | Synthetic + CWRU dataset loaders |
| `tools/python/train.py` | Create | Training loop, checkpoint, eval |
| `tools/python/export.py` | Create | Int8 quantization → weights.bin + .trix writer |
| `tools/python/trix.py` | Modify | Add `trace_tick_start`/`trace_tick_end` to `_TrixResult` |

---

## Background: What the Runtime Expects

`trix_load()` reads a `.trix` YAML spec. For a chip with one linear layer, the spec looks like:

```yaml
softchip:
  name: bearing_fault
  version: 1.0.0
state:
  bits: 512
linear:
  encoder:
    input_dim: 64
    output_dim: 512
    weights: /path/to/encoder_weights.bin
signatures:
  normal:
    pattern: <128 hex chars = 512 bits>
    threshold: 45
  inner_race:
    pattern: <128 hex chars>
    threshold: 45
inference:
  mode: first_match
  default: unknown
```

The weight file is raw `int8_t[N * K]` (row-major, N=output_dim, K=input_dim).

The encoder output goes through `linear_sign_binarize()` in the runtime (z>0 → 1, MSB-first) before Hamming matching. Training must replicate this exactly.

---

## Task 1: Fix Python Bindings + requirements.txt

The `_TrixResult` ctypes struct is missing the two trace tick fields. Fix now before they cause silent misreads.

**Files:**
- Modify: `tools/python/trix.py:60-66`
- Create: `tools/python/requirements.txt`

- [ ] **Step 1: Read trix.py to confirm current _TrixResult fields**

```bash
grep -n "_TrixResult\|_fields_\|trace" tools/python/trix.py
```
Expected: 4 fields, no trace fields.

- [ ] **Step 2: Update `_TrixResult` to match the C struct**

In `tools/python/trix.py`, replace the `_TrixResult` class:

```python
class _TrixResult(ctypes.Structure):
    _fields_ = [
        ("match",            ctypes.c_int),
        ("distance",         ctypes.c_int),
        ("threshold",        ctypes.c_int),
        ("label",            ctypes.c_char_p),
        ("trace_tick_start", ctypes.c_uint32),
        ("trace_tick_end",   ctypes.c_uint32),
    ]
```

Also update `Result.__init__` to accept and store these fields:

```python
class Result:
    def __init__(self, match, distance, threshold, label,
                 trace_tick_start=0, trace_tick_end=0):
        self.match = match
        self.distance = distance
        self.threshold = threshold
        self.label = label
        self.label_bytes = label
        self.trace_tick_start = trace_tick_start
        self.trace_tick_end = trace_tick_end
```

And update the `Chip.infer()` return line:

```python
return Result(result.match, result.distance, result.threshold, label,
              result.trace_tick_start, result.trace_tick_end)
```

- [ ] **Step 3: Create `tools/python/requirements.txt`**

```
torch>=2.0.0
numpy>=1.24.0
scipy>=1.10.0
```

- [ ] **Step 4: Verify Python loads without error**

```bash
cd tools/python
python3 -c "import trix; print('trix available:', trix.is_available())"
```
Expected: prints without exception (available may be False if lib not built yet, that's OK).

- [ ] **Step 5: Commit**

```bash
git add tools/python/trix.py tools/python/requirements.txt
git commit -m "fix: add trace tick fields to Python _TrixResult, add requirements.txt"
```

---

## Task 2: BNN Encoder Model

**Files:**
- Create: `tools/python/model.py`

The encoder is a stack of linear layers with ReLU between hidden layers and a sign binarization at the output. Training uses a straight-through estimator (STE) so gradients flow through the sign function.

- [ ] **Step 1: Write `tools/python/model.py`**

```python
"""
model.py — Binary Neural Network encoder for TriX chip training.

Architecture: Linear(K, hidden) → ReLU → Linear(hidden, N) → SignSTE → {0,1}^N
Training: float weights, STE through sign, classification loss + balance reg.
Export: quantize float weights to int8 (clamp to [-127, 127]).
"""

import torch
import torch.nn as nn
import torch.nn.functional as F
import numpy as np


class SignSTE(torch.autograd.Function):
    """Sign function with straight-through estimator for the backward pass."""

    @staticmethod
    def forward(ctx, x):
        # 1 where x > 0, 0 elsewhere — matches linear_sign_binarize() in C
        return (x > 0).float()

    @staticmethod
    def backward(ctx, grad_output):
        # Pass gradient straight through (STE)
        return grad_output


def sign_ste(x):
    return SignSTE.apply(x)


class BNNEncoder(nn.Module):
    """
    Binary metric encoder.

    Takes a K-dimensional int8 input, produces an N-dimensional binary code.
    N must be 512 (= state_bits = 64 bytes × 8 bits) for TriX compatibility.

    Args:
        input_dim:  K — number of input features (≤ 64, divisible by 4)
        hidden_dim: intermediate dimension (use 256 for single-layer, 0 to skip)
        output_dim: N — must be 512
        num_classes: number of output classes (for classification head)
    """

    def __init__(self, input_dim: int, hidden_dim: int, output_dim: int,
                 num_classes: int):
        super().__init__()
        assert output_dim == 512, "output_dim must be 512 for TriX"
        assert input_dim <= 64 and input_dim % 4 == 0, \
            "input_dim must be ≤64 and divisible by 4"

        self.input_dim = input_dim
        self.output_dim = output_dim

        # Encoder layers
        layers = []
        in_dim = input_dim
        if hidden_dim > 0:
            layers += [nn.Linear(in_dim, hidden_dim, bias=False), nn.ReLU()]
            in_dim = hidden_dim
        layers += [nn.Linear(in_dim, output_dim, bias=False)]
        self.encoder = nn.Sequential(*layers)

        # Classification head (for supervised training signal)
        self.classifier = nn.Linear(output_dim, num_classes)

    def encode(self, x: torch.Tensor) -> torch.Tensor:
        """
        Forward through encoder, return float pre-binarization activations.
        Shape: (batch, output_dim)
        """
        return self.encoder(x.float())

    def binary_code(self, x: torch.Tensor) -> torch.Tensor:
        """
        Return binary code {0,1}^N via STE. Use during training.
        Shape: (batch, output_dim)
        """
        z = self.encode(x)
        return sign_ste(z)

    def forward(self, x: torch.Tensor):
        """
        Returns (binary_code, logits) for training.
        binary_code: (batch, 512) float {0.0, 1.0} via STE
        logits: (batch, num_classes) for cross-entropy
        """
        z = self.encode(x)
        code = sign_ste(z)
        logits = self.classifier(code)
        return code, logits

    def export_weights_int8(self) -> list[np.ndarray]:
        """
        Return list of int8 weight arrays, one per encoder linear layer.
        Shape: [N, K] row-major int8 — matches runtime expectation.
        Quantization: scale each layer to fill int8 range, clamp to [-127, 127].
        """
        weights = []
        for module in self.encoder:
            if isinstance(module, nn.Linear):
                W = module.weight.detach().float().numpy()  # [N, K]
                scale = 127.0 / (np.max(np.abs(W)) + 1e-8)
                W_q = np.clip(np.round(W * scale), -127, 127).astype(np.int8)
                weights.append(W_q)
        return weights
```

- [ ] **Step 2: Verify model instantiates and forward works**

```python
# Run this directly
import sys
sys.path.insert(0, 'tools/python')
from model import BNNEncoder
import torch

m = BNNEncoder(input_dim=64, hidden_dim=256, output_dim=512, num_classes=4)
x = torch.randint(-128, 127, (8, 64)).float()
code, logits = m(x)
assert code.shape == (8, 512), f"bad code shape: {code.shape}"
assert logits.shape == (8, 4), f"bad logits shape: {logits.shape}"
assert set(code.unique().tolist()).issubset({0.0, 1.0}), "code must be binary"
print("BNNEncoder OK:", code.shape, logits.shape)
```

```bash
cd /path/to/repo
python3 -c "$(cat << 'EOF'
import sys; sys.path.insert(0, 'tools/python')
from model import BNNEncoder
import torch
m = BNNEncoder(64, 256, 512, 4)
x = torch.randint(-128, 127, (8, 64)).float()
code, logits = m(x)
assert code.shape == (8, 512)
assert set(code.unique().tolist()).issubset({0.0, 1.0})
print('BNNEncoder OK')
EOF
)"
```
Expected: `BNNEncoder OK`

- [ ] **Step 3: Commit**

```bash
git add tools/python/model.py
git commit -m "feat: add BNNEncoder with STE binarization for TriX training"
```

---

## Task 3: Dataset Loader

**Files:**
- Create: `tools/python/dataset.py`

Two datasets: synthetic (always available, for CI) and CWRU (requires download, for real chips).

The synthetic dataset generates Gaussian clusters in `R^64`, quantized to int8, with 4 well-separated classes. CWRU loads `.mat` files from the Case Western Reserve University bearing dataset (freely downloadable).

- [ ] **Step 1: Write `tools/python/dataset.py`**

```python
"""
dataset.py — Dataset loaders for TriX chip training.

Synthetic: 4-class Gaussian clusters in int8 R^64. Always available.
CWRU:      Case Western Reserve bearing fault dataset (.mat files).
           Download from: https://engineering.case.edu/bearingdatacenter/download-data-file
           Expected files: Normal_0.mat, IR007_0.mat, OR007@6_0.mat, B007_0.mat

Both return (X: np.int8 [N, K], y: np.int64 [N]), labels: list[str]
"""

import numpy as np
from pathlib import Path


CWRU_CLASS_NAMES = ["normal", "inner_race", "outer_race", "ball"]


def load_synthetic(n_per_class: int = 500, input_dim: int = 64,
                   seed: int = 42) -> tuple[np.ndarray, np.ndarray, list[str]]:
    """
    Generate 4 synthetic classes as Gaussian clusters in int8 R^input_dim.
    Classes are separated by 64 units (ensuring Hamming separation post-encoding).

    Returns:
        X: int8 [n_per_class*4, input_dim]
        y: int64 [n_per_class*4]
        labels: ['class_0', 'class_1', 'class_2', 'class_3']
    """
    rng = np.random.default_rng(seed)
    num_classes = 4
    # Centers spread across int8 range [-128, 127]
    centers = np.array([
        [-64.0] * input_dim,
        [+64.0] * input_dim,
        [-64.0] * (input_dim // 2) + [+64.0] * (input_dim // 2),
        [+64.0] * (input_dim // 2) + [-64.0] * (input_dim // 2),
    ])

    Xs, ys = [], []
    for cls in range(num_classes):
        samples = rng.normal(centers[cls], scale=16.0, size=(n_per_class, input_dim))
        Xs.append(np.clip(samples, -128, 127).astype(np.int8))
        ys.append(np.full(n_per_class, cls, dtype=np.int64))

    X = np.concatenate(Xs, axis=0)
    y = np.concatenate(ys, axis=0)
    idx = rng.permutation(len(X))
    labels = [f"class_{i}" for i in range(num_classes)]
    return X[idx], y[idx], labels


def _load_mat_signal(path: str) -> np.ndarray:
    """Load a CWRU .mat file, return the drive-end accelerometer signal."""
    from scipy.io import loadmat
    data = loadmat(path)
    # CWRU mat files use keys like 'X097_DE_time', 'X098_DE_time', etc.
    # Find the drive-end key
    de_keys = [k for k in data.keys() if 'DE_time' in k]
    if not de_keys:
        raise ValueError(f"No DE_time key found in {path}. Keys: {list(data.keys())}")
    signal = data[de_keys[0]].flatten().astype(np.float32)
    return signal


def _signal_to_windows(signal: np.ndarray, window_size: int = 64,
                        stride: int = 32, scale: float = 64.0) -> np.ndarray:
    """
    Slice signal into overlapping windows, normalize to int8 range.

    Args:
        signal:      1D float32 accelerometer signal
        window_size: samples per window (= input_dim = K)
        stride:      hop between windows
        scale:       divisor to map raw signal to [-127, 127]

    Returns:
        windows: int8 [N_windows, window_size]
    """
    windows = []
    for start in range(0, len(signal) - window_size, stride):
        w = signal[start:start + window_size]
        # Normalize: zero-mean per window, scale to int8
        w = w - w.mean()
        max_abs = np.max(np.abs(w)) + 1e-8
        w = w / max_abs * scale
        windows.append(np.clip(w, -127, 127).astype(np.int8))
    return np.array(windows, dtype=np.int8)


def load_cwru(data_dir: str, window_size: int = 64,
              stride: int = 32, max_windows: int = 2000,
              seed: int = 42) -> tuple[np.ndarray, np.ndarray, list[str]]:
    """
    Load CWRU bearing dataset from directory.

    Expects these files (or similar — the loader searches for DE_time keys):
        Normal_0.mat      → class 0: normal
        IR007_0.mat       → class 1: inner race fault (7 mil)
        OR007@6_0.mat     → class 2: outer race fault (7 mil, 6 o'clock)
        B007_0.mat        → class 3: ball fault (7 mil)

    Args:
        data_dir:    directory containing .mat files
        window_size: samples per window (must be ≤64, divisible by 4)
        stride:      hop between windows
        max_windows: cap per class to balance dataset

    Returns:
        X: int8 [N, window_size], y: int64 [N], labels: list[str]
    """
    assert window_size <= 64 and window_size % 4 == 0, \
        "window_size must be ≤64 and divisible by 4"

    base = Path(data_dir)
    # Map class index to glob pattern (most CWRU file naming)
    file_patterns = [
        ("normal",      ["Normal_0.mat", "normal_0.mat", "Normal*.mat"]),
        ("inner_race",  ["IR007_0.mat",  "IR007*.mat"]),
        ("outer_race",  ["OR007@6_0.mat","OR007*.mat"]),
        ("ball",        ["B007_0.mat",   "B007*.mat"]),
    ]

    Xs, ys = [], []
    labels = []
    rng = np.random.default_rng(seed)

    for cls_idx, (label, patterns) in enumerate(file_patterns):
        mat_path = None
        for pat in patterns:
            found = list(base.glob(pat))
            if found:
                mat_path = str(found[0])
                break
        if mat_path is None:
            raise FileNotFoundError(
                f"Could not find {label} data in {data_dir}. "
                f"Tried patterns: {patterns}"
            )

        signal = _load_mat_signal(mat_path)
        windows = _signal_to_windows(signal, window_size, stride)

        # Cap and shuffle
        if len(windows) > max_windows:
            idx = rng.choice(len(windows), max_windows, replace=False)
            windows = windows[idx]

        Xs.append(windows)
        ys.append(np.full(len(windows), cls_idx, dtype=np.int64))
        labels.append(label)

    X = np.concatenate(Xs, axis=0)
    y = np.concatenate(ys, axis=0)
    idx = rng.permutation(len(X))
    return X[idx], y[idx], labels


def train_val_split(X: np.ndarray, y: np.ndarray, val_frac: float = 0.2,
                    seed: int = 42) -> tuple:
    """Split into train/val, stratified by class."""
    rng = np.random.default_rng(seed)
    classes = np.unique(y)
    train_idx, val_idx = [], []
    for cls in classes:
        idx = np.where(y == cls)[0]
        rng.shuffle(idx)
        n_val = max(1, int(len(idx) * val_frac))
        val_idx.extend(idx[:n_val])
        train_idx.extend(idx[n_val:])
    return (X[train_idx], y[train_idx],
            X[val_idx],   y[val_idx])
```

- [ ] **Step 2: Verify synthetic loader**

```bash
python3 -c "
import sys; sys.path.insert(0, 'tools/python')
from dataset import load_synthetic, train_val_split
import numpy as np
X, y, labels = load_synthetic(n_per_class=100)
assert X.shape == (400, 64), X.shape
assert X.dtype == np.int8
assert len(set(y.tolist())) == 4
Xtr, ytr, Xv, yv = train_val_split(X, y)
assert len(Xtr) + len(Xv) == 400
print('synthetic OK, labels:', labels)
"
```
Expected: `synthetic OK, labels: ['class_0', 'class_1', 'class_2', 'class_3']`

- [ ] **Step 3: Commit**

```bash
git add tools/python/dataset.py
git commit -m "feat: add synthetic + CWRU dataset loaders for TriX training"
```

---

## Task 4: Export Pipeline

**Files:**
- Create: `tools/python/export.py`

Takes a trained `BNNEncoder`, a dataset, and class labels → writes weight `.bin` files + `.trix` spec.

- [ ] **Step 1: Write `tools/python/export.py`**

```python
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
import struct
import numpy as np
import torch


def _compute_binary_codes(model, X: np.ndarray, batch_size: int = 256) -> np.ndarray:
    """
    Run X through encoder, return {0,1}^512 binary codes as bool array.
    Uses hard sign (no STE) for inference-time binarization.
    Shape: [N, 512]
    """
    model.eval()
    codes = []
    with torch.no_grad():
        for i in range(0, len(X), batch_size):
            x = torch.tensor(X[i:i+batch_size], dtype=torch.float32)
            z = model.encode(x)                         # [B, 512] float
            code = (z > 0).numpy().astype(np.uint8)     # {0,1}^512
            codes.append(code)
    return np.concatenate(codes, axis=0)  # [N, 512] uint8


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
    num_layers = len(model.export_weights_int8())
    assert num_layers >= 1, "Model must have at least one encoder layer"

    # 1. Export weight binaries
    weight_paths = []
    for layer_idx, W_int8 in enumerate(model.export_weights_int8()):
        path = os.path.join(out_dir, f"{chip_name}_layer{layer_idx}.bin")
        W_int8.tofile(path)   # row-major int8, [N, K]
        weight_paths.append(os.path.abspath(path))
        print(f"  Wrote {path}: shape={W_int8.shape}, dtype={W_int8.dtype}")

    # 2. Compute binary codes for all training examples
    print("  Computing binary codes for prototype extraction...")
    codes = _compute_binary_codes(model, X_train)  # [N, 512]

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
```

- [ ] **Step 2: Test export with a randomly-initialized model + synthetic data**

```bash
python3 -c "
import sys; sys.path.insert(0, 'tools/python')
from model import BNNEncoder
from dataset import load_synthetic
from export import export_chip
import numpy as np

m = BNNEncoder(64, 256, 512, 4)
X, y, labels = load_synthetic(n_per_class=50)
path = export_chip(m, X, y, labels, out_dir='/tmp/trix_export_test', chip_name='test')
print('exported:', path)

import os
assert os.path.exists(path), 'trix file missing'
assert os.path.exists(path.replace('.trix', '_layer0.bin')), 'weight file missing'
print('export OK')
"
```
Expected: prints exported path and `export OK`.

- [ ] **Step 3: Commit**

```bash
git add tools/python/export.py
git commit -m "feat: add export pipeline for int8 weights + .trix chip file"
```

---

## Task 5: Training Loop

**Files:**
- Create: `tools/python/train.py`

- [ ] **Step 1: Write `tools/python/train.py`**

```python
"""
train.py — Training loop for TriX BNN encoder.

Loss = CrossEntropy(logits, y) + lambda_bal * BalanceLoss(code)

BalanceLoss pushes each bit toward 50% activation across the batch:
    balance_loss = mean((mean(code, dim=0) - 0.5)^2)
This prevents code collapse (all-zeros or all-ones).

Usage:
    python3 train.py --dataset synthetic --out-dir chips/
    python3 train.py --dataset cwru --data-dir /path/to/cwru --out-dir chips/
"""

import argparse
import sys
import os
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, TensorDataset

sys.path.insert(0, os.path.dirname(__file__))
from model import BNNEncoder
from dataset import load_synthetic, load_cwru, train_val_split
from export import export_chip


def balance_loss(code: torch.Tensor) -> torch.Tensor:
    """Penalize bits that are always 0 or always 1 across the batch."""
    mean_activation = code.mean(dim=0)          # [N]
    return ((mean_activation - 0.5) ** 2).mean()


def train_epoch(model, loader, optimizer, lambda_bal: float, device: str):
    model.train()
    total_loss = 0.0
    correct = 0
    total = 0
    ce = nn.CrossEntropyLoss()

    for X_batch, y_batch in loader:
        X_batch = X_batch.to(device).float()
        y_batch = y_batch.to(device)

        optimizer.zero_grad()
        code, logits = model(X_batch)

        loss = ce(logits, y_batch) + lambda_bal * balance_loss(code)
        loss.backward()
        optimizer.step()

        total_loss += loss.item() * len(X_batch)
        correct += (logits.argmax(dim=1) == y_batch).sum().item()
        total += len(X_batch)

    return total_loss / total, correct / total


@torch.no_grad()
def eval_epoch(model, loader, device: str):
    model.eval()
    correct = 0
    total = 0
    for X_batch, y_batch in loader:
        X_batch = X_batch.to(device).float()
        y_batch = y_batch.to(device)
        _, logits = model(X_batch)
        correct += (logits.argmax(dim=1) == y_batch).sum().item()
        total += len(X_batch)
    return correct / total


def main():
    p = argparse.ArgumentParser(description="Train TriX BNN encoder")
    p.add_argument("--dataset",    choices=["synthetic", "cwru"], default="synthetic")
    p.add_argument("--data-dir",   default=".", help="CWRU .mat directory")
    p.add_argument("--input-dim",  type=int, default=64)
    p.add_argument("--hidden-dim", type=int, default=256)
    p.add_argument("--epochs",     type=int, default=50)
    p.add_argument("--batch-size", type=int, default=128)
    p.add_argument("--lr",         type=float, default=1e-3)
    p.add_argument("--lambda-bal", type=float, default=0.1,
                   help="Balance regularization weight")
    p.add_argument("--out-dir",    default="chips")
    p.add_argument("--chip-name",  default="trix_chip")
    args = p.parse_args()

    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"Device: {device}")

    # Load data
    if args.dataset == "synthetic":
        X, y, labels = load_synthetic(n_per_class=500, input_dim=args.input_dim)
    else:
        X, y, labels = load_cwru(args.data_dir, window_size=args.input_dim)
    print(f"Dataset: {len(X)} examples, {len(labels)} classes: {labels}")

    Xtr, ytr, Xv, yv = train_val_split(X, y, val_frac=0.2)
    print(f"Train: {len(Xtr)}, Val: {len(Xv)}")

    # Dataloaders
    train_ds = TensorDataset(
        torch.tensor(Xtr, dtype=torch.float32),
        torch.tensor(ytr, dtype=torch.long)
    )
    val_ds = TensorDataset(
        torch.tensor(Xv, dtype=torch.float32),
        torch.tensor(yv, dtype=torch.long)
    )
    train_loader = DataLoader(train_ds, batch_size=args.batch_size, shuffle=True)
    val_loader   = DataLoader(val_ds,   batch_size=args.batch_size, shuffle=False)

    # Model
    model = BNNEncoder(
        input_dim=args.input_dim,
        hidden_dim=args.hidden_dim,
        output_dim=512,
        num_classes=len(labels)
    ).to(device)
    optimizer = optim.Adam(model.parameters(), lr=args.lr)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)

    # Train
    best_val_acc = 0.0
    best_state = None
    for epoch in range(1, args.epochs + 1):
        train_loss, train_acc = train_epoch(
            model, train_loader, optimizer, args.lambda_bal, device)
        val_acc = eval_epoch(model, val_loader, device)
        scheduler.step()

        if val_acc > best_val_acc:
            best_val_acc = val_acc
            best_state = {k: v.clone() for k, v in model.state_dict().items()}

        if epoch % 10 == 0 or epoch == 1:
            print(f"Epoch {epoch:3d}: loss={train_loss:.4f} "
                  f"train_acc={train_acc:.3f} val_acc={val_acc:.3f}")

    # Restore best
    model.load_state_dict(best_state)
    print(f"\nBest val accuracy: {best_val_acc:.3f}")

    # Export
    print("\nExporting chip...")
    trix_path = export_chip(
        model, Xtr, ytr, labels,
        out_dir=args.out_dir,
        chip_name=args.chip_name
    )
    print(f"Chip exported to: {trix_path}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run training on synthetic data, verify it converges**

```bash
cd /path/to/repo
python3 tools/python/train.py \
    --dataset synthetic \
    --epochs 30 \
    --out-dir /tmp/trix_chips \
    --chip-name synthetic_test
```

Expected output:
```
Epoch   1: loss=1.xxxx train_acc=0.xxx val_acc=0.xxx
Epoch  10: loss=0.xxxx train_acc=0.9xx val_acc=0.9xx
Epoch  20: loss=0.xxxx train_acc=0.9xx val_acc=0.9xx
Epoch  30: loss=0.xxxx train_acc=0.9xx val_acc=0.9xx
Best val accuracy: >0.90
Chip exported to: /tmp/trix_chips/synthetic_test.trix
```

**If val_acc < 0.80 after 30 epochs:** The 4-class synthetic problem is easy — this indicates a bug in the loss or data loader. Check that `balance_loss` isn't overwhelming `ce` (try `--lambda-bal 0.01`).

- [ ] **Step 3: Verify the exported chip loads and infers in C**

Build the runtime first if not already built:
```bash
cmake -B build -DTRIX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
```

Then verify the chip file:
```bash
./build/trix /tmp/trix_chips/synthetic_test.trix info
```
Expected: prints chip name, version, signature count (4), linear layers (1).

- [ ] **Step 4: Commit**

```bash
git add tools/python/train.py
git commit -m "feat: add BNN training loop with classification loss + balance regularization"
```

---

## Task 6: End-to-End Validation Test

Verify that a chip trained on synthetic data and loaded by the C runtime produces correct match/no-match results.

**Files:**
- Create: `tools/python/test_pipeline.py`

- [ ] **Step 1: Write `tools/python/test_pipeline.py`**

```python
"""
test_pipeline.py — End-to-end training → export → inference validation.

Trains on synthetic data, exports chip, loads via C runtime, runs inference,
verifies that class centers match their expected signatures.

Usage: python3 tools/python/test_pipeline.py
Exit code 0 = pass, 1 = fail.
"""

import sys
import os
import subprocess
import numpy as np
import torch

sys.path.insert(0, os.path.dirname(__file__))
from model import BNNEncoder
from dataset import load_synthetic, train_val_split
from export import export_chip
from train import train_epoch, eval_epoch, balance_loss
import torch.optim as optim
from torch.utils.data import DataLoader, TensorDataset


def main():
    print("=== Training pipeline end-to-end test ===\n")

    # 1. Train for 20 epochs on synthetic data
    X, y, labels = load_synthetic(n_per_class=300, input_dim=64)
    Xtr, ytr, Xv, yv = train_val_split(X, y)

    model = BNNEncoder(64, 256, 512, 4)
    optimizer = torch.optim.Adam(model.parameters(), lr=1e-3)
    train_ds = DataLoader(
        TensorDataset(torch.tensor(Xtr, dtype=torch.float32),
                      torch.tensor(ytr, dtype=torch.long)),
        batch_size=64, shuffle=True
    )
    val_ds = DataLoader(
        TensorDataset(torch.tensor(Xv, dtype=torch.float32),
                      torch.tensor(yv, dtype=torch.long)),
        batch_size=64
    )

    best_acc = 0.0
    best_state = None
    for epoch in range(30):
        train_epoch(model, train_ds, optimizer, 0.1, "cpu")
        acc = eval_epoch(model, val_ds, "cpu")
        if acc > best_acc:
            best_acc = acc
            best_state = {k: v.clone() for k, v in model.state_dict().items()}

    model.load_state_dict(best_state)
    print(f"Training complete. Best val acc: {best_acc:.3f}")

    assert best_acc > 0.80, f"FAIL: val acc {best_acc:.3f} < 0.80"
    print("  ✓ Val accuracy > 0.80")

    # 2. Export chip
    out_dir = "/tmp/trix_pipeline_test"
    trix_path = export_chip(model, Xtr, ytr, labels,
                            out_dir=out_dir, chip_name="pipeline_test")
    assert os.path.exists(trix_path), f"FAIL: {trix_path} not created"
    assert os.path.exists(trix_path.replace(".trix", "_layer0.bin")), "FAIL: weight file missing"
    print(f"  ✓ Chip exported to {trix_path}")

    # 3. Load via Python ctypes and infer class centers
    import trix as trix_py
    if not trix_py.is_available():
        print("  SKIP: trix runtime not built (run cmake --build build first)")
        return 0

    chip = trix_py.load(trix_path)
    print(f"  ✓ Chip loaded: {chip.name}, {chip.num_signatures} signatures")

    # Create class center inputs (same as dataset.load_synthetic centers)
    centers_int8 = np.array([
        np.full(64, -64, dtype=np.int8),
        np.full(64, +64, dtype=np.int8),
        np.array([-64]*32 + [64]*32, dtype=np.int8),
        np.array([64]*32 + [-64]*32, dtype=np.int8),
    ])

    matches = 0
    for cls_idx, center in enumerate(centers_int8):
        result = chip.infer(bytes(center))
        expected_label = labels[cls_idx]
        if result.is_match and result.label == expected_label:
            matches += 1
            print(f"  ✓ class_center[{cls_idx}] → {result.label} dist={result.distance}")
        else:
            print(f"  ✗ class_center[{cls_idx}] → {result.label or 'no match'} "
                  f"(expected {expected_label}), dist={result.distance}")

    assert matches >= 3, f"FAIL: only {matches}/4 class centers matched correctly"
    print(f"\n  ✓ {matches}/4 class centers correctly classified")
    print("\nPASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run the end-to-end test**

```bash
# Build first
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4

# Run test
TRIX_LIB_PATH=$(pwd)/build python3 tools/python/test_pipeline.py
```

Expected:
```
=== Training pipeline end-to-end test ===
Training complete. Best val acc: 0.9xx
  ✓ Val accuracy > 0.80
  ✓ Chip exported to /tmp/trix_pipeline_test/pipeline_test.trix
  ✓ Chip loaded: pipeline_test, 4 signatures
  ✓ class_center[0] → class_0 dist=xx
  ✓ class_center[1] → class_1 dist=xx
  ✓ class_center[2] → class_2 dist=xx
  ✓ class_center[3] → class_3 dist=xx
  ✓ 4/4 class centers correctly classified
PASS
```

**If fewer than 3/4 centers match:** Check threshold values in the exported `.trix` file — they may be set too tight. The `--lambda-bal` flag controls bit diversity; too-low diversity causes Hamming matches to fail.

- [ ] **Step 3: Final commit**

```bash
git add tools/python/test_pipeline.py
git commit -m "feat: add end-to-end training pipeline validation test"
```
