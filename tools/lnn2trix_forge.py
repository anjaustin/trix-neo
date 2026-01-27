#!/usr/bin/env python3
"""
lnn2trix_forge.py — Liquid Neural Network to TriX Forge Compiler

Converts trained LNN/CfC weights into FORGED NEON soft-chips.
Unlike lnn2trix.py which outputs float weights, this compiler:
  1. Quantizes weights to ternary {-1, 0, +1}
  2. Packs weights into Block-8-K64 layout for NEON SDOT
  3. Generates complete, compilable C headers

"The forge stamps circuits, not loops."

Usage:
    # From PyTorch checkpoint
    python lnn2trix_forge.py model.pt --name=my_chip

    # From NumPy weights with explicit dimensions
    python lnn2trix_forge.py weights.npz --name=driver --dt=0.01

    # Generate synthetic example and forge it
    python lnn2trix_forge.py --example --name=test_chip --input-dim=64 --hidden-dim=32

Output:
    - {name}_forged.h    Complete forged CfC soft-chip with NEON kernels

Copyright 2026 Trix Research
"""

import argparse
import sys
from pathlib import Path
from datetime import datetime
import struct

# Optional imports
try:
    import torch

    HAS_TORCH = True
except ImportError:
    HAS_TORCH = False

try:
    import numpy as np

    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False
    print("WARNING: NumPy not available. Limited functionality.")


# ═══════════════════════════════════════════════════════════════════════════════
# TERNARY QUANTIZATION
#
# BitNet b1.58 style: scale by absmean, round to {-1, 0, +1}
# ═══════════════════════════════════════════════════════════════════════════════


def quantize_ternary(weights, method="absmean"):
    """
    Quantize float weights to ternary {-1, 0, +1}.

    Args:
        weights: numpy array of float weights
        method: 'absmean' (BitNet b1.58) or 'threshold'

    Returns:
        int8 array with values -1, 0, +1
    """
    if method == "absmean":
        # BitNet b1.58 method: scale by absmean
        gamma = np.abs(weights).mean()
        if gamma < 1e-8:
            return np.zeros_like(weights, dtype=np.int8)
        scaled = weights / gamma
        quantized = np.round(np.clip(scaled, -1, 1)).astype(np.int8)
    elif method == "threshold":
        # Simple threshold method
        threshold = np.abs(weights).mean() * 0.5
        quantized = np.zeros_like(weights, dtype=np.int8)
        quantized[weights > threshold] = 1
        quantized[weights < -threshold] = -1
    else:
        raise ValueError(f"Unknown quantization method: {method}")

    return quantized


def analyze_ternary_weights(weights, name=""):
    """Analyze ternary weight distribution."""
    total = weights.size
    pos = np.sum(weights > 0)
    neg = np.sum(weights < 0)
    zero = np.sum(weights == 0)

    print(
        f"  {name}: +1={pos} ({100 * pos / total:.1f}%), "
        f"0={zero} ({100 * zero / total:.1f}%), "
        f"-1={neg} ({100 * neg / total:.1f}%)"
    )

    return {"positive": pos, "zero": zero, "negative": neg, "total": total}


# ═══════════════════════════════════════════════════════════════════════════════
# BLOCK-8-K64 WEIGHT PACKING
#
# The forge NEON kernel expects weights in this layout for maximum performance.
# ═══════════════════════════════════════════════════════════════════════════════


def align_to(n, alignment):
    """Round up n to multiple of alignment."""
    return ((n + alignment - 1) // alignment) * alignment


def pack_block8_k64(weights):
    """
    Pack row-major weights to Block-8-K64 layout.

    Input: [N, K] row-major
    Output: [N/8, K/64, 8, 64] = [N_blocks, K_blocks, 8, 64]

    For efficient NEON SDOT processing:
    - 8 output channels per outer iteration
    - 64 K elements per inner iteration
    """
    N, K = weights.shape

    # Align dimensions
    N_aligned = align_to(N, 8)
    K_aligned = align_to(K, 64)

    # Pad if needed
    if N_aligned != N or K_aligned != K:
        padded = np.zeros((N_aligned, K_aligned), dtype=weights.dtype)
        padded[:N, :K] = weights
        weights = padded

    N_blocks = N_aligned // 8
    K_blocks = K_aligned // 64

    # Reshape to blocked layout
    packed = np.zeros((N_blocks, K_blocks, 8, 64), dtype=weights.dtype)

    for nb in range(N_blocks):
        for kb in range(K_blocks):
            for ch in range(8):
                n = nb * 8 + ch
                k_start = kb * 64
                packed[nb, kb, ch, :] = weights[n, k_start : k_start + 64]

    return packed, N_aligned, K_aligned


# ═══════════════════════════════════════════════════════════════════════════════
# WEIGHT EXTRACTION
# ═══════════════════════════════════════════════════════════════════════════════


def extract_cfc_weights_torch(state_dict):
    """Extract CfC weights from PyTorch state dict."""
    weights = {}

    # Common naming patterns
    patterns = {
        "W_gate": [
            "gate.weight",
            "gate_net.weight",
            "wiring.gate_weight",
            "ff1.weight",
        ],
        "b_gate": ["gate.bias", "gate_net.bias", "wiring.gate_bias", "ff1.bias"],
        "W_cand": [
            "candidate.weight",
            "candidate_net.weight",
            "wiring.cand_weight",
            "ff2.weight",
        ],
        "b_cand": [
            "candidate.bias",
            "candidate_net.bias",
            "wiring.cand_bias",
            "ff2.bias",
        ],
        "W_out": ["output.weight", "output_proj.weight", "readout.weight", "fc.weight"],
        "b_out": ["output.bias", "output_proj.bias", "readout.bias", "fc.bias"],
        "tau": ["tau", "time_constant", "timescale"],
    }

    for key, patterns_list in patterns.items():
        for pattern in patterns_list:
            for sd_key, tensor in state_dict.items():
                if pattern in sd_key.lower() or sd_key.endswith(pattern):
                    weights[key] = tensor.detach().cpu().numpy()
                    print(f"  Found {key}: {sd_key} -> shape {weights[key].shape}")
                    break
            if key in weights:
                break

    return weights


def extract_cfc_weights_numpy(npz_file):
    """Extract CfC weights from NumPy .npz file."""
    data = np.load(npz_file)
    weights = {}

    key_map = {
        "W_gate": ["W_gate", "gate_weight", "w_gate"],
        "b_gate": ["b_gate", "gate_bias", "bias_gate"],
        "W_cand": ["W_cand", "cand_weight", "w_cand", "W_candidate"],
        "b_cand": ["b_cand", "cand_bias", "bias_cand", "b_candidate"],
        "W_out": ["W_out", "output_weight", "w_out", "W_output"],
        "b_out": ["b_out", "output_bias", "b_output"],
        "tau": ["tau", "time_constant"],
    }

    for key, patterns in key_map.items():
        for pattern in patterns:
            if pattern in data:
                weights[key] = data[pattern]
                print(f"  Found {key}: {pattern} -> shape {weights[key].shape}")
                break

    return weights


def create_synthetic_weights(input_dim=64, hidden_dim=32, output_dim=10, seed=42):
    """Create synthetic CfC weights for testing."""
    np.random.seed(seed)

    concat_dim = input_dim + hidden_dim

    # Xavier initialization then ternarize
    gate_scale = np.sqrt(2.0 / (concat_dim + hidden_dim))
    cand_scale = np.sqrt(2.0 / (concat_dim + hidden_dim))
    out_scale = np.sqrt(2.0 / (hidden_dim + output_dim))

    return {
        "W_gate": np.random.randn(hidden_dim, concat_dim).astype(np.float32)
        * gate_scale,
        "b_gate": np.random.randn(hidden_dim).astype(np.float32) * 0.1,
        "W_cand": np.random.randn(hidden_dim, concat_dim).astype(np.float32)
        * cand_scale,
        "b_cand": np.random.randn(hidden_dim).astype(np.float32) * 0.1,
        "W_out": np.random.randn(output_dim, hidden_dim).astype(np.float32) * out_scale,
        "b_out": np.random.randn(output_dim).astype(np.float32) * 0.1,
        "tau": np.random.uniform(0.5, 2.0, hidden_dim).astype(np.float32),
    }


# ═══════════════════════════════════════════════════════════════════════════════
# FORGE CODE GENERATION
# ═══════════════════════════════════════════════════════════════════════════════


def emit_header(name, input_dim, hidden_dim, output_dim, concat_dim):
    """Emit file header."""
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M")

    return f"""/*
 * {name}_forged.h — Forged CfC Soft-Chip
 *
 * Auto-generated by lnn2trix_forge.py
 * Date: {timestamp}
 *
 * Architecture:
 *   Input:  {input_dim}
 *   Hidden: {hidden_dim}
 *   Output: {output_dim}
 *   Concat: {concat_dim}
 *
 * CfC Update Rule:
 *   gate      = sigmoid(W_gate @ [x; h] + b_gate)
 *   candidate = tanh(W_cand @ [x; h] + b_cand)
 *   decay     = exp(-dt / tau)
 *   h_new     = (1 - gate) * h * decay + gate * candidate
 *
 * "The SDOT instruction IS the Prime."
 */

#ifndef {name.upper()}_FORGED_H
#define {name.upper()}_FORGED_H

#include <stdint.h>
#include <string.h>
#include <math.h>
#include <arm_neon.h>

/* Dimensions */
#define {name.upper()}_INPUT_DIM  {input_dim}
#define {name.upper()}_HIDDEN_DIM {hidden_dim}
#define {name.upper()}_OUTPUT_DIM {output_dim}
#define {name.upper()}_CONCAT_DIM {concat_dim}

"""


def emit_activations():
    """Emit activation functions."""
    return """/* ═══════════════════════════════════════════════════════════════
 * ACTIVATION FUNCTIONS
 * ═══════════════════════════════════════════════════════════════ */

static inline float _sigmoid(float x) {
    return 1.0f / (1.0f + expf(-x));
}

static inline float _tanh(float x) {
    return tanhf(x);
}

static inline void _softmax(const float* x, float* out, int n) {
    float max_val = x[0];
    for (int i = 1; i < n; i++) if (x[i] > max_val) max_val = x[i];
    float sum = 0.0f;
    for (int i = 0; i < n; i++) { out[i] = expf(x[i] - max_val); sum += out[i]; }
    for (int i = 0; i < n; i++) out[i] /= sum;
}

"""


def emit_weight_array(name, data, shape_str):
    """Emit a single weight array."""
    lines = [f"/* {shape_str} */"]
    lines.append(
        f"static const int8_t {name}[{data.size}] __attribute__((aligned(64))) = {{"
    )

    flat = data.flatten()
    row = []
    for i, val in enumerate(flat):
        row.append(f"{int(val):4d}")
        if len(row) == 16 or i == len(flat) - 1:
            suffix = "," if i < len(flat) - 1 else ""
            lines.append("    " + ", ".join(row) + suffix)
            row = []

    lines.append("};")
    return "\n".join(lines)


def emit_bias_array(name, data):
    """Emit a float bias array."""
    lines = [f"/* [{len(data)}] */"]
    lines.append(
        f"static const float {name}[{len(data)}] __attribute__((aligned(64))) = {{"
    )

    row = []
    for i, val in enumerate(data):
        row.append(f"{val:12.6f}f")
        if len(row) == 8 or i == len(data) - 1:
            suffix = "," if i < len(data) - 1 else ""
            lines.append("    " + ", ".join(row) + suffix)
            row = []

    lines.append("};")
    return "\n".join(lines)


def emit_weights_section(name, weights_packed, dims):
    """Emit all weight arrays."""
    N_gate, K_gate = dims["hidden_dim"], dims["concat_dim_aligned"]
    N_out, K_out = dims["output_dim_aligned"], dims["hidden_dim_aligned"]

    lines = [
        """/* ═══════════════════════════════════════════════════════════════
 * FROZEN WEIGHTS (Block-8-K64 Layout)
 * ═══════════════════════════════════════════════════════════════ */
"""
    ]

    # Gate weights
    lines.append(
        emit_weight_array(
            f"{name}_W_gate",
            weights_packed["W_gate"],
            f"Gate weights [{dims['hidden_dim']}, {dims['concat_dim']}] packed",
        )
    )
    lines.append("")
    lines.append(emit_bias_array(f"{name}_b_gate", weights_packed["b_gate"]))
    lines.append("")

    # Candidate weights
    lines.append(
        emit_weight_array(
            f"{name}_W_cand",
            weights_packed["W_cand"],
            f"Candidate weights [{dims['hidden_dim']}, {dims['concat_dim']}] packed",
        )
    )
    lines.append("")
    lines.append(emit_bias_array(f"{name}_b_cand", weights_packed["b_cand"]))
    lines.append("")

    # Tau
    lines.append(emit_bias_array(f"{name}_tau", weights_packed["tau"]))
    lines.append("")

    # Output weights (if present)
    if "W_out" in weights_packed:
        lines.append(
            emit_weight_array(
                f"{name}_W_out",
                weights_packed["W_out"],
                f"Output weights [{dims['output_dim']}, {dims['hidden_dim']}] packed",
            )
        )
        lines.append("")
        lines.append(emit_bias_array(f"{name}_b_out", weights_packed["b_out"]))
        lines.append("")

    return "\n".join(lines)


def emit_matvec_kernel(name, layer_name, N, K):
    """Emit NEON Block-8-K64 MatVec kernel."""
    K_blocks = K // 64

    lines = [
        f"""/* {layer_name} MatVec: {K} -> {N} (NEON Block-8-K64) */
static void {name}_{layer_name}_matvec(
    const int8_t* __restrict__ x,
    int32_t* __restrict__ y
) {{
    const int8_t* W = {name}_W_{layer_name};
    const int K_blocks = {K_blocks};
    const int block_stride = 8 * 64;

    for (int n = 0; n < {N}; n += 8) {{
        int nb = n / 8;

        int32x4_t acc0 = vdupq_n_s32(0);
        int32x4_t acc1 = vdupq_n_s32(0);
        int32x4_t acc2 = vdupq_n_s32(0);
        int32x4_t acc3 = vdupq_n_s32(0);
        int32x4_t acc4 = vdupq_n_s32(0);
        int32x4_t acc5 = vdupq_n_s32(0);
        int32x4_t acc6 = vdupq_n_s32(0);
        int32x4_t acc7 = vdupq_n_s32(0);

        const int8_t* a_ptr = x;
        const int8_t* w_base = W + nb * K_blocks * block_stride;

        for (int kb = 0; kb < K_blocks; kb++) {{
            int8x16_t a0 = vld1q_s8(a_ptr);
            int8x16_t a1 = vld1q_s8(a_ptr + 16);
            int8x16_t a2 = vld1q_s8(a_ptr + 32);
            int8x16_t a3 = vld1q_s8(a_ptr + 48);
            a_ptr += 64;

            const int8_t* w_block = w_base + kb * block_stride;
"""
    ]

    # Unrolled SDOT for 8 rows
    for row in range(8):
        lines.append(f"""
            acc{row} = vdotq_s32(acc{row}, vld1q_s8(w_block + {row} * 64 + 0), a0);
            acc{row} = vdotq_s32(acc{row}, vld1q_s8(w_block + {row} * 64 + 16), a1);
            acc{row} = vdotq_s32(acc{row}, vld1q_s8(w_block + {row} * 64 + 32), a2);
            acc{row} = vdotq_s32(acc{row}, vld1q_s8(w_block + {row} * 64 + 48), a3);""")

    lines.append("""
        }
""")

    # Store results
    for i in range(8):
        lines.append(f"        y[n + {i}] = vaddvq_s32(acc{i});")

    lines.append("""    }
}
""")

    return "\n".join(lines)


def emit_cell_function(name, dims):
    """Emit CfC cell function."""
    return f"""/* ═══════════════════════════════════════════════════════════════
 * CfC CELL - Single Timestep
 * ═══════════════════════════════════════════════════════════════ */

void {name}_cell(
    const float* x,        /* Input [{dims["input_dim"]}] */
    const float* h_prev,   /* Previous hidden [{dims["hidden_dim"]}] */
    float dt,              /* Time delta */
    float* h_new           /* Output hidden [{dims["hidden_dim"]}] */
) {{
    /* Stack allocation */
    int8_t concat_q[{dims["concat_dim_aligned"]}] __attribute__((aligned(64)));
    int32_t gate_pre[{dims["hidden_dim"]}];
    int32_t cand_pre[{dims["hidden_dim"]}];
    float gate[{dims["hidden_dim"]}];
    float candidate[{dims["hidden_dim"]}];

    /* Step 1: Quantize and concatenate [x; h_prev] */
    float scale_x = 0.0f, scale_h = 0.0f;
    for (int i = 0; i < {dims["input_dim"]}; i++) {{
        float abs_val = x[i] > 0 ? x[i] : -x[i];
        if (abs_val > scale_x) scale_x = abs_val;
    }}
    for (int i = 0; i < {dims["hidden_dim"]}; i++) {{
        float abs_val = h_prev[i] > 0 ? h_prev[i] : -h_prev[i];
        if (abs_val > scale_h) scale_h = abs_val;
    }}
    float scale = scale_x > scale_h ? scale_x : scale_h;
    if (scale < 1e-8f) scale = 1e-8f;
    float inv_scale = 127.0f / scale;

    memset(concat_q, 0, {dims["concat_dim_aligned"]});
    for (int i = 0; i < {dims["input_dim"]}; i++) {{
        int v = (int)(x[i] * inv_scale);
        concat_q[i] = v > 127 ? 127 : (v < -127 ? -127 : v);
    }}
    for (int i = 0; i < {dims["hidden_dim"]}; i++) {{
        int v = (int)(h_prev[i] * inv_scale);
        concat_q[{dims["input_dim"]} + i] = v > 127 ? 127 : (v < -127 ? -127 : v);
    }}

    /* Step 2: Gate = sigmoid(W_gate @ concat + b_gate) */
    {name}_gate_matvec(concat_q, gate_pre);
    for (int i = 0; i < {dims["hidden_dim"]}; i++) {{
        float pre = (float)gate_pre[i] * scale / 127.0f + {name}_b_gate[i];
        gate[i] = _sigmoid(pre);
    }}

    /* Step 3: Candidate = tanh(W_cand @ concat + b_cand) */
    {name}_cand_matvec(concat_q, cand_pre);
    for (int i = 0; i < {dims["hidden_dim"]}; i++) {{
        float pre = (float)cand_pre[i] * scale / 127.0f + {name}_b_cand[i];
        candidate[i] = _tanh(pre);
    }}

    /* Step 4: h_new = (1 - gate) * h_prev * decay + gate * candidate */
    for (int i = 0; i < {dims["hidden_dim"]}; i++) {{
        float decay = expf(-dt / {name}_tau[i]);
        float retention = (1.0f - gate[i]) * h_prev[i] * decay;
        float update = gate[i] * candidate[i];
        h_new[i] = retention + update;
    }}
}}

"""


def emit_forward_function(name, dims):
    """Emit sequence forward function."""
    return f"""/* ═══════════════════════════════════════════════════════════════
 * CfC FORWARD - Sequence Processing
 * ═══════════════════════════════════════════════════════════════ */

void {name}_forward(
    const float* inputs,   /* [seq_len, {dims["input_dim"]}] */
    int seq_len,
    float dt,
    const float* h_init,   /* [{dims["hidden_dim"]}] or NULL */
    float* outputs,        /* [seq_len, {dims["hidden_dim"]}] */
    float* h_final         /* [{dims["hidden_dim"]}] or NULL */
) {{
    float h_current[{dims["hidden_dim"]}];

    if (h_init) {{
        memcpy(h_current, h_init, {dims["hidden_dim"]} * sizeof(float));
    }} else {{
        memset(h_current, 0, {dims["hidden_dim"]} * sizeof(float));
    }}

    for (int t = 0; t < seq_len; t++) {{
        const float* x_t = inputs + t * {dims["input_dim"]};
        float* out_t = outputs + t * {dims["hidden_dim"]};

        {name}_cell(x_t, h_current, dt, out_t);
        memcpy(h_current, out_t, {dims["hidden_dim"]} * sizeof(float));
    }}

    if (h_final) {{
        memcpy(h_final, h_current, {dims["hidden_dim"]} * sizeof(float));
    }}
}}

"""


def emit_output_function(name, dims):
    """Emit output projection function."""
    if dims["output_dim"] == 0:
        return ""

    return f"""/* ═══════════════════════════════════════════════════════════════
 * OUTPUT PROJECTION
 * ═══════════════════════════════════════════════════════════════ */

void {name}_output(
    const float* h,        /* [{dims["hidden_dim"]}] */
    float* logits          /* [{dims["output_dim"]}] */
) {{
    int8_t h_q[{dims["hidden_dim_aligned"]}] __attribute__((aligned(64)));
    int32_t out_pre[{dims["output_dim_aligned"]}];

    /* Quantize hidden state */
    float scale = 0.0f;
    for (int i = 0; i < {dims["hidden_dim"]}; i++) {{
        float abs_val = h[i] > 0 ? h[i] : -h[i];
        if (abs_val > scale) scale = abs_val;
    }}
    if (scale < 1e-8f) scale = 1e-8f;
    float inv_scale = 127.0f / scale;

    memset(h_q, 0, {dims["hidden_dim_aligned"]});
    for (int i = 0; i < {dims["hidden_dim"]}; i++) {{
        int v = (int)(h[i] * inv_scale);
        h_q[i] = v > 127 ? 127 : (v < -127 ? -127 : v);
    }}

    /* Output projection */
    {name}_out_matvec(h_q, out_pre);
    for (int i = 0; i < {dims["output_dim"]}; i++) {{
        logits[i] = (float)out_pre[i] * scale / 127.0f + {name}_b_out[i];
    }}
}}

void {name}_output_softmax(
    const float* h,
    float* probs
) {{
    float logits[{dims["output_dim"]}];
    {name}_output(h, logits);
    _softmax(logits, probs, {dims["output_dim"]});
}}

"""


def emit_footer(name):
    """Emit file footer."""
    return f"""#endif /* {name.upper()}_FORGED_H */
"""


# ═══════════════════════════════════════════════════════════════════════════════
# MAIN FORGE PIPELINE
# ═══════════════════════════════════════════════════════════════════════════════


def forge_cfc(weights, name, output_dir="."):
    """
    Main forge pipeline: float weights -> forged NEON soft-chip
    """
    print("\n" + "=" * 70)
    print("  FORGE: Converting CfC to NEON Soft-Chip")
    print("=" * 70)

    # Step 1: Infer dimensions
    if "W_gate" not in weights:
        raise ValueError("Missing W_gate weights")

    hidden_dim, concat_dim = weights["W_gate"].shape
    input_dim = concat_dim - hidden_dim

    has_output = "W_out" in weights
    if has_output:
        output_dim = weights["W_out"].shape[0]
    else:
        output_dim = 0

    print(f"\nDimensions:")
    print(f"  Input:  {input_dim}")
    print(f"  Hidden: {hidden_dim}")
    print(f"  Output: {output_dim}")
    print(f"  Concat: {concat_dim}")

    # Step 2: Quantize weights to ternary
    print(f"\nQuantizing weights to ternary...")

    W_gate_q = quantize_ternary(weights["W_gate"])
    W_cand_q = quantize_ternary(weights["W_cand"])

    analyze_ternary_weights(W_gate_q, "W_gate")
    analyze_ternary_weights(W_cand_q, "W_cand")

    if has_output:
        W_out_q = quantize_ternary(weights["W_out"])
        analyze_ternary_weights(W_out_q, "W_out")

    # Step 3: Pack to Block-8-K64 layout
    print(f"\nPacking to Block-8-K64 layout...")

    W_gate_packed, hidden_aligned, concat_aligned = pack_block8_k64(W_gate_q)
    W_cand_packed, _, _ = pack_block8_k64(W_cand_q)

    print(f"  W_gate: [{hidden_dim}, {concat_dim}] -> packed [{W_gate_packed.shape}]")
    print(f"  Aligned: hidden={hidden_aligned}, concat={concat_aligned}")

    if has_output:
        W_out_packed, output_aligned, hidden_aligned_out = pack_block8_k64(W_out_q)
        print(f"  W_out: [{output_dim}, {hidden_dim}] -> packed [{W_out_packed.shape}]")
    else:
        output_aligned = 0
        hidden_aligned_out = hidden_aligned

    # Prepare packed weights dict
    weights_packed = {
        "W_gate": W_gate_packed,
        "b_gate": weights["b_gate"],
        "W_cand": W_cand_packed,
        "b_cand": weights["b_cand"],
        "tau": weights.get("tau", np.ones(hidden_dim, dtype=np.float32)),
    }

    if has_output:
        weights_packed["W_out"] = W_out_packed
        weights_packed["b_out"] = weights["b_out"]

    dims = {
        "input_dim": input_dim,
        "hidden_dim": hidden_dim,
        "output_dim": output_dim,
        "concat_dim": concat_dim,
        "hidden_dim_aligned": hidden_aligned,
        "concat_dim_aligned": concat_aligned,
        "output_dim_aligned": output_aligned,
    }

    # Step 4: Generate forged C code
    print(f"\nGenerating forged soft-chip...")

    code_parts = []

    # Header
    code_parts.append(emit_header(name, input_dim, hidden_dim, output_dim, concat_dim))

    # Activations
    code_parts.append(emit_activations())

    # Weights
    code_parts.append(emit_weights_section(name, weights_packed, dims))

    # MatVec kernels
    code_parts.append(
        "/* ═══════════════════════════════════════════════════════════════"
    )
    code_parts.append(" * NEON MATVEC KERNELS (Block-8-K64)")
    code_parts.append(
        " * ═══════════════════════════════════════════════════════════════ */\n"
    )

    code_parts.append(emit_matvec_kernel(name, "gate", hidden_aligned, concat_aligned))
    code_parts.append(emit_matvec_kernel(name, "cand", hidden_aligned, concat_aligned))

    if has_output:
        code_parts.append(
            emit_matvec_kernel(name, "out", output_aligned, hidden_aligned_out)
        )

    # Cell function
    code_parts.append(emit_cell_function(name, dims))

    # Forward function
    code_parts.append(emit_forward_function(name, dims))

    # Output function
    if has_output:
        code_parts.append(emit_output_function(name, dims))

    # Footer
    code_parts.append(emit_footer(name))

    # Write output
    output_path = Path(output_dir) / f"{name}_forged.h"
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "w") as f:
        f.write("\n".join(code_parts))

    print(f"\n  -> {output_path}")

    # Summary
    weight_bytes = W_gate_packed.size + W_cand_packed.size
    if has_output:
        weight_bytes += W_out_packed.size

    float_bytes = (
        hidden_dim * concat_dim * 2 + (output_dim * hidden_dim if has_output else 0)
    ) * 4

    print(f"\n" + "=" * 70)
    print(f"  FORGE COMPLETE")
    print(f"=" * 70)
    print(f"  Chip name:    {name}")
    print(f"  Output:       {output_path}")
    print(f"  Weight bytes: {weight_bytes:,} (ternary)")
    print(f"  vs float32:   {float_bytes:,} ({float_bytes / weight_bytes:.1f}x larger)")
    print(f"  Compression:  {float_bytes / weight_bytes:.1f}x")
    print("=" * 70)

    return str(output_path), dims


# ═══════════════════════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════════════════════


def main():
    parser = argparse.ArgumentParser(
        description="Forge LNN/CfC weights to NEON soft-chip",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # From PyTorch checkpoint
    python lnn2trix_forge.py model.pt --name=my_chip

    # From NumPy weights
    python lnn2trix_forge.py weights.npz --name=driver

    # Generate synthetic weights for testing
    python lnn2trix_forge.py --example --name=test_chip
        """,
    )

    parser.add_argument("input", nargs="?", help="Input file (.pt or .npz)")
    parser.add_argument("--name", required=True, help="Chip name")
    parser.add_argument("--output", "-o", default=".", help="Output directory")
    parser.add_argument(
        "--example", action="store_true", help="Generate synthetic weights"
    )
    parser.add_argument(
        "--input-dim", type=int, default=64, help="Input dim for example"
    )
    parser.add_argument(
        "--hidden-dim", type=int, default=32, help="Hidden dim for example"
    )
    parser.add_argument(
        "--output-dim", type=int, default=10, help="Output dim for example"
    )
    parser.add_argument("--seed", type=int, default=42, help="Random seed for example")

    args = parser.parse_args()

    # Validate
    if not args.example and not args.input:
        parser.error("Either provide an input file or use --example")

    if not HAS_NUMPY:
        print("Error: NumPy is required")
        sys.exit(1)

    # Load weights
    if args.example:
        print(f"Generating synthetic weights...")
        print(
            f"  Dimensions: {args.input_dim} -> {args.hidden_dim} -> {args.output_dim}"
        )
        weights = create_synthetic_weights(
            args.input_dim, args.hidden_dim, args.output_dim, args.seed
        )
    elif args.input.endswith(".pt") or args.input.endswith(".pth"):
        if not HAS_TORCH:
            print("Error: PyTorch required for .pt files")
            sys.exit(1)
        print(f"Loading PyTorch checkpoint: {args.input}")
        state_dict = torch.load(args.input, map_location="cpu")
        if "state_dict" in state_dict:
            state_dict = state_dict["state_dict"]
        if "model_state_dict" in state_dict:
            state_dict = state_dict["model_state_dict"]
        weights = extract_cfc_weights_torch(state_dict)
    elif args.input.endswith(".npz"):
        print(f"Loading NumPy weights: {args.input}")
        weights = extract_cfc_weights_numpy(args.input)
    else:
        print(f"Error: Unknown file format: {args.input}")
        sys.exit(1)

    # Forge
    output_path, dims = forge_cfc(weights, args.name, args.output)

    # Usage instructions
    print(f"\nUsage:")
    print(f'  #include "{args.name}_forged.h"')
    print(f"")
    print(f"  float x[{args.name.upper()}_INPUT_DIM];")
    print(f"  float h[{args.name.upper()}_HIDDEN_DIM] = {{0}};")
    print(f"  float h_new[{args.name.upper()}_HIDDEN_DIM];")
    print(f"")
    print(f"  {args.name}_cell(x, h, dt, h_new);")
    if dims["output_dim"] > 0:
        print(f"")
        print(f"  float probs[{args.name.upper()}_OUTPUT_DIM];")
        print(f"  {args.name}_output_softmax(h_new, probs);")
    print()


if __name__ == "__main__":
    main()
