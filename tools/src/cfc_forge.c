/*
 * cfc_forge.c — TriX CfC Soft-Chip Forge Implementation
 *
 * "The CfC cell is just 5 Primes orchestrated in time."
 *
 * This file stamps complete CfC cells as frozen soft-chips.
 * The forge generates:
 *   1. Frozen weight arrays (Block-8-K64 layout)
 *   2. NEON MatVec kernels for gate and candidate
 *   3. Fused activation functions
 *   4. The complete cell update logic
 *
 * Copyright 2026 Trix Research
 */

#include "../include/cfc_forge.h"
#include "../../zor/include/trixc/memory.h"
#include "../../zor/include/trixc/errors.h"
#include "../../zor/include/trixc/logging.h"
#include <string.h>
#include <time.h>
#include <stdarg.h>

/* String buffer utilities */
typedef struct {
    char* buf;
    size_t size;
    size_t pos;
} StringBuffer;

static void sb_init(StringBuffer* sb, char* buf, size_t size) {
    sb->buf = buf;
    sb->size = size;
    sb->pos = 0;
}

static void sb_printf(StringBuffer* sb, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(sb->buf + sb->pos, sb->size - sb->pos, fmt, args);
    va_end(args);
    if (written > 0 && (size_t)written < sb->size - sb->pos) {
        sb->pos += written;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * HEADER EMISSION
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_cfc_header(StringBuffer* sb, const CfCCellSpec* spec) {
    time_t now = time(NULL);
    
    sb_printf(sb, "/*\n");
    sb_printf(sb, " * TriX Forged CfC Soft-Chip: %s\n", spec->name);
    sb_printf(sb, " * Generated: %s", ctime(&now));
    sb_printf(sb, " *\n");
    sb_printf(sb, " * Architecture:\n");
    sb_printf(sb, " *   Input:  %d\n", spec->input_dim);
    sb_printf(sb, " *   Hidden: %d\n", spec->hidden_dim);
    if (spec->has_output) {
        sb_printf(sb, " *   Output: %d\n", spec->output_dim);
    }
    sb_printf(sb, " *\n");
    sb_printf(sb, " * CfC Update Rule:\n");
    sb_printf(sb, " *   gate      = sigmoid(W_gate @ [x; h] + b_gate)\n");
    sb_printf(sb, " *   candidate = tanh(W_cand @ [x; h] + b_cand)\n");
    sb_printf(sb, " *   decay     = exp(-dt / tau)\n");
    sb_printf(sb, " *   h_new     = (1 - gate) * h * decay + gate * candidate\n");
    sb_printf(sb, " *\n");
    sb_printf(sb, " * \"The SDOT instruction IS the Prime.\"\n");
    sb_printf(sb, " */\n\n");
    
    sb_printf(sb, "#ifndef %s_CFC_H\n", spec->name);
    sb_printf(sb, "#define %s_CFC_H\n\n", spec->name);
    
    sb_printf(sb, "#include <stdint.h>\n");
    sb_printf(sb, "#include <string.h>\n");
    sb_printf(sb, "#include <math.h>\n");
    sb_printf(sb, "#include <arm_neon.h>\n\n");
    
    /* Constants */
    sb_printf(sb, "/* Dimensions */\n");
    sb_printf(sb, "#define %s_INPUT_DIM  %d\n", spec->name, spec->input_dim);
    sb_printf(sb, "#define %s_HIDDEN_DIM %d\n", spec->name, spec->hidden_dim);
    sb_printf(sb, "#define %s_CONCAT_DIM %d\n", spec->name, spec->input_dim + spec->hidden_dim);
    if (spec->has_output) {
        sb_printf(sb, "#define %s_OUTPUT_DIM %d\n", spec->name, spec->output_dim);
    }
    sb_printf(sb, "\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ACTIVATION FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_activations(StringBuffer* sb) {
    sb_printf(sb, "/* ═══════════════════════════════════════════════════════════════\n");
    sb_printf(sb, " * ACTIVATION FUNCTIONS (Fused EXP Prime)\n");
    sb_printf(sb, " * ═══════════════════════════════════════════════════════════════ */\n\n");
    
    sb_printf(sb, "static inline float _sigmoid(float x) {\n");
    sb_printf(sb, "    return 1.0f / (1.0f + expf(-x));\n");
    sb_printf(sb, "}\n\n");
    
    sb_printf(sb, "static inline float _tanh(float x) {\n");
    sb_printf(sb, "    return tanhf(x);\n");
    sb_printf(sb, "}\n\n");
    
    sb_printf(sb, "static inline void _softmax(const float* x, float* out, int n) {\n");
    sb_printf(sb, "    float max_val = x[0];\n");
    sb_printf(sb, "    for (int i = 1; i < n; i++) if (x[i] > max_val) max_val = x[i];\n");
    sb_printf(sb, "    float sum = 0.0f;\n");
    sb_printf(sb, "    for (int i = 0; i < n; i++) { out[i] = expf(x[i] - max_val); sum += out[i]; }\n");
    sb_printf(sb, "    for (int i = 0; i < n; i++) out[i] /= sum;\n");
    sb_printf(sb, "}\n\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * WEIGHT ARRAYS
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_weight_array(StringBuffer* sb, const char* name, const int8_t* data, 
                              int rows, int cols, const char* desc) {
    sb_printf(sb, "/* %s: [%d, %d] */\n", desc, rows, cols);
    sb_printf(sb, "static const int8_t %s[%d] __attribute__((aligned(64))) = {\n", 
             name, rows * cols);
    
    if (data) {
        for (int i = 0; i < rows * cols; i++) {
            if (i % 16 == 0) sb_printf(sb, "    ");
            sb_printf(sb, "%4d,", data[i]);
            if (i % 16 == 15 || i == rows * cols - 1) sb_printf(sb, "\n");
        }
    } else {
        sb_printf(sb, "    /* Weights loaded from file at runtime */\n");
        sb_printf(sb, "    0  /* Placeholder - replace with actual weights */\n");
    }
    
    sb_printf(sb, "};\n\n");
}

static void emit_bias_array(StringBuffer* sb, const char* name, const float* data,
                            int size, const char* desc) {
    sb_printf(sb, "/* %s: [%d] */\n", desc, size);
    sb_printf(sb, "static const float %s[%d] __attribute__((aligned(64))) = {\n", 
             name, size);
    
    if (data) {
        for (int i = 0; i < size; i++) {
            if (i % 8 == 0) sb_printf(sb, "    ");
            sb_printf(sb, "%12.6ff,", data[i]);
            if (i % 8 == 7 || i == size - 1) sb_printf(sb, "\n");
        }
    } else {
        sb_printf(sb, "    0.0f  /* Placeholder */\n");
    }
    
    sb_printf(sb, "};\n\n");
}

static void emit_weights(StringBuffer* sb, const CfCCellSpec* spec) {
    int concat_dim = spec->input_dim + spec->hidden_dim;
    
    sb_printf(sb, "/* ═══════════════════════════════════════════════════════════════\n");
    sb_printf(sb, " * FROZEN WEIGHTS (Block-8-K64 Layout)\n");
    sb_printf(sb, " * ═══════════════════════════════════════════════════════════════ */\n\n");
    
    /* W_gate */
    char w_gate_name[128];
    snprintf(w_gate_name, sizeof(w_gate_name), "%s_W_gate", spec->name);
    emit_weight_array(sb, w_gate_name, spec->W_gate, spec->hidden_dim, concat_dim, "Gate weights");
    
    /* b_gate */
    char b_gate_name[128];
    snprintf(b_gate_name, sizeof(b_gate_name), "%s_b_gate", spec->name);
    emit_bias_array(sb, b_gate_name, spec->b_gate, spec->hidden_dim, "Gate bias");
    
    /* W_cand */
    char w_cand_name[128];
    snprintf(w_cand_name, sizeof(w_cand_name), "%s_W_cand", spec->name);
    emit_weight_array(sb, w_cand_name, spec->W_cand, spec->hidden_dim, concat_dim, "Candidate weights");
    
    /* b_cand */
    char b_cand_name[128];
    snprintf(b_cand_name, sizeof(b_cand_name), "%s_b_cand", spec->name);
    emit_bias_array(sb, b_cand_name, spec->b_cand, spec->hidden_dim, "Candidate bias");
    
    /* tau */
    sb_printf(sb, "/* Time constant */\n");
    sb_printf(sb, "static const float %s_tau[%d] = {\n", spec->name,
             spec->tau_shared ? 1 : spec->hidden_dim);
    if (spec->tau) {
        int tau_count = spec->tau_shared ? 1 : spec->hidden_dim;
        sb_printf(sb, "    ");
        for (int i = 0; i < tau_count; i++) {
            sb_printf(sb, "%12.6ff%s", spec->tau[i], i < tau_count - 1 ? ", " : "");
        }
        sb_printf(sb, "\n");
    } else {
        sb_printf(sb, "    1.0f  /* Default tau */\n");
    }
    sb_printf(sb, "};\n\n");
    
    /* Output projection if present */
    if (spec->has_output) {
        char w_out_name[128];
        snprintf(w_out_name, sizeof(w_out_name), "%s_W_out", spec->name);
        emit_weight_array(sb, w_out_name, spec->W_out, spec->output_dim, spec->hidden_dim, "Output weights");
        
        char b_out_name[128];
        snprintf(b_out_name, sizeof(b_out_name), "%s_b_out", spec->name);
        emit_bias_array(sb, b_out_name, spec->b_out, spec->output_dim, "Output bias");
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * NEON MATVEC KERNEL (Block-8-K64)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_matvec_kernel(StringBuffer* sb, const char* cell_name, const char* layer_name,
                               int N, int K) {
    int K_blocks = K / 64;
    if (K % 64 != 0) K_blocks++;  /* Handle non-64-aligned K */
    
    sb_printf(sb, "/* %s MatVec: %d -> %d (NEON Block-8-K64) */\n", layer_name, K, N);
    sb_printf(sb, "static void %s_%s_matvec(\n", cell_name, layer_name);
    sb_printf(sb, "    const int8_t* __restrict__ x,\n");
    sb_printf(sb, "    int32_t* __restrict__ y\n");
    sb_printf(sb, ") {\n");
    
    sb_printf(sb, "    const int8_t* W = %s_W_%s;\n", cell_name, layer_name);
    sb_printf(sb, "    const int K_blocks = %d;\n", K / 64);
    sb_printf(sb, "    const int block_stride = 8 * 64;\n\n");
    
    sb_printf(sb, "    for (int n = 0; n < %d; n += 8) {\n", N);
    sb_printf(sb, "        int nb = n / 8;\n\n");
    
    /* Accumulators */
    sb_printf(sb, "        int32x4_t acc0 = vdupq_n_s32(0);\n");
    sb_printf(sb, "        int32x4_t acc1 = vdupq_n_s32(0);\n");
    sb_printf(sb, "        int32x4_t acc2 = vdupq_n_s32(0);\n");
    sb_printf(sb, "        int32x4_t acc3 = vdupq_n_s32(0);\n");
    sb_printf(sb, "        int32x4_t acc4 = vdupq_n_s32(0);\n");
    sb_printf(sb, "        int32x4_t acc5 = vdupq_n_s32(0);\n");
    sb_printf(sb, "        int32x4_t acc6 = vdupq_n_s32(0);\n");
    sb_printf(sb, "        int32x4_t acc7 = vdupq_n_s32(0);\n\n");
    
    sb_printf(sb, "        const int8_t* a_ptr = x;\n");
    sb_printf(sb, "        const int8_t* w_base = W + nb * K_blocks * block_stride;\n\n");
    
    /* K loop */
    sb_printf(sb, "        for (int kb = 0; kb < K_blocks; kb++) {\n");
    sb_printf(sb, "            int8x16_t a0 = vld1q_s8(a_ptr);\n");
    sb_printf(sb, "            int8x16_t a1 = vld1q_s8(a_ptr + 16);\n");
    sb_printf(sb, "            int8x16_t a2 = vld1q_s8(a_ptr + 32);\n");
    sb_printf(sb, "            int8x16_t a3 = vld1q_s8(a_ptr + 48);\n");
    sb_printf(sb, "            a_ptr += 64;\n\n");
    
    sb_printf(sb, "            const int8_t* w_block = w_base + kb * block_stride;\n\n");
    
    /* Unrolled SDOT for 8 rows */
    for (int row = 0; row < 8; row++) {
        sb_printf(sb, "            /* Row %d */\n", row);
        sb_printf(sb, "            acc%d = vdotq_s32(acc%d, vld1q_s8(w_block + %d * 64 + 0), a0);\n", row, row, row);
        sb_printf(sb, "            acc%d = vdotq_s32(acc%d, vld1q_s8(w_block + %d * 64 + 16), a1);\n", row, row, row);
        sb_printf(sb, "            acc%d = vdotq_s32(acc%d, vld1q_s8(w_block + %d * 64 + 32), a2);\n", row, row, row);
        sb_printf(sb, "            acc%d = vdotq_s32(acc%d, vld1q_s8(w_block + %d * 64 + 48), a3);\n", row, row, row);
    }
    
    sb_printf(sb, "        }\n\n");
    
    /* Store results */
    for (int i = 0; i < 8; i++) {
        sb_printf(sb, "        y[n + %d] = vaddvq_s32(acc%d);\n", i, i);
    }
    
    sb_printf(sb, "    }\n");
    sb_printf(sb, "}\n\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CfC CELL FUNCTION
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_cell_function(StringBuffer* sb, const CfCCellSpec* spec) {
    int concat_dim = spec->input_dim + spec->hidden_dim;
    
    sb_printf(sb, "/* ═══════════════════════════════════════════════════════════════\n");
    sb_printf(sb, " * CfC CELL - Single Timestep\n");
    sb_printf(sb, " * ═══════════════════════════════════════════════════════════════ */\n\n");
    
    sb_printf(sb, "void %s_cell(\n", spec->name);
    sb_printf(sb, "    const float* x,        /* Input [%d] */\n", spec->input_dim);
    sb_printf(sb, "    const float* h_prev,   /* Previous hidden [%d] */\n", spec->hidden_dim);
    sb_printf(sb, "    float dt,              /* Time delta */\n");
    sb_printf(sb, "    float* h_new           /* Output hidden [%d] */\n", spec->hidden_dim);
    sb_printf(sb, ") {\n");
    
    /* Concat buffer */
    sb_printf(sb, "    /* Stack allocation */\n");
    sb_printf(sb, "    int8_t concat_q[%d] __attribute__((aligned(64)));\n", concat_dim);
    sb_printf(sb, "    int32_t gate_pre[%d];\n", spec->hidden_dim);
    sb_printf(sb, "    int32_t cand_pre[%d];\n", spec->hidden_dim);
    sb_printf(sb, "    float gate[%d];\n", spec->hidden_dim);
    sb_printf(sb, "    float candidate[%d];\n\n", spec->hidden_dim);
    
    /* Quantize and concatenate */
    sb_printf(sb, "    /* Step 1: Quantize and concatenate [x; h_prev] */\n");
    sb_printf(sb, "    float scale_x = 0.0f, scale_h = 0.0f;\n");
    sb_printf(sb, "    for (int i = 0; i < %d; i++) {\n", spec->input_dim);
    sb_printf(sb, "        float abs_val = x[i] > 0 ? x[i] : -x[i];\n");
    sb_printf(sb, "        if (abs_val > scale_x) scale_x = abs_val;\n");
    sb_printf(sb, "    }\n");
    sb_printf(sb, "    for (int i = 0; i < %d; i++) {\n", spec->hidden_dim);
    sb_printf(sb, "        float abs_val = h_prev[i] > 0 ? h_prev[i] : -h_prev[i];\n");
    sb_printf(sb, "        if (abs_val > scale_h) scale_h = abs_val;\n");
    sb_printf(sb, "    }\n");
    sb_printf(sb, "    float scale = scale_x > scale_h ? scale_x : scale_h;\n");
    sb_printf(sb, "    if (scale < 1e-8f) scale = 1e-8f;\n");
    sb_printf(sb, "    float inv_scale = 127.0f / scale;\n\n");
    
    sb_printf(sb, "    for (int i = 0; i < %d; i++) {\n", spec->input_dim);
    sb_printf(sb, "        int v = (int)(x[i] * inv_scale);\n");
    sb_printf(sb, "        concat_q[i] = v > 127 ? 127 : (v < -127 ? -127 : v);\n");
    sb_printf(sb, "    }\n");
    sb_printf(sb, "    for (int i = 0; i < %d; i++) {\n", spec->hidden_dim);
    sb_printf(sb, "        int v = (int)(h_prev[i] * inv_scale);\n");
    sb_printf(sb, "        concat_q[%d + i] = v > 127 ? 127 : (v < -127 ? -127 : v);\n", spec->input_dim);
    sb_printf(sb, "    }\n\n");
    
    /* Gate and candidate MatVecs */
    sb_printf(sb, "    /* Step 2: Gate = sigmoid(W_gate @ concat + b_gate) */\n");
    sb_printf(sb, "    %s_gate_matvec(concat_q, gate_pre);\n", spec->name);
    sb_printf(sb, "    for (int i = 0; i < %d; i++) {\n", spec->hidden_dim);
    sb_printf(sb, "        float pre = (float)gate_pre[i] * scale / 127.0f + %s_b_gate[i];\n", spec->name);
    sb_printf(sb, "        gate[i] = _sigmoid(pre);\n");
    sb_printf(sb, "    }\n\n");
    
    sb_printf(sb, "    /* Step 3: Candidate = tanh(W_cand @ concat + b_cand) */\n");
    sb_printf(sb, "    %s_cand_matvec(concat_q, cand_pre);\n", spec->name);
    sb_printf(sb, "    for (int i = 0; i < %d; i++) {\n", spec->hidden_dim);
    sb_printf(sb, "        float pre = (float)cand_pre[i] * scale / 127.0f + %s_b_cand[i];\n", spec->name);
    sb_printf(sb, "        candidate[i] = _tanh(pre);\n");
    sb_printf(sb, "    }\n\n");
    
    /* Decay and update */
    sb_printf(sb, "    /* Step 4: h_new = (1 - gate) * h_prev * decay + gate * candidate */\n");
    if (spec->tau_shared) {
        sb_printf(sb, "    float decay = expf(-dt / %s_tau[0]);\n", spec->name);
        sb_printf(sb, "    for (int i = 0; i < %d; i++) {\n", spec->hidden_dim);
        sb_printf(sb, "        float retention = (1.0f - gate[i]) * h_prev[i] * decay;\n");
        sb_printf(sb, "        float update = gate[i] * candidate[i];\n");
        sb_printf(sb, "        h_new[i] = retention + update;\n");
        sb_printf(sb, "    }\n");
    } else {
        sb_printf(sb, "    for (int i = 0; i < %d; i++) {\n", spec->hidden_dim);
        sb_printf(sb, "        float decay = expf(-dt / %s_tau[i]);\n", spec->name);
        sb_printf(sb, "        float retention = (1.0f - gate[i]) * h_prev[i] * decay;\n");
        sb_printf(sb, "        float update = gate[i] * candidate[i];\n");
        sb_printf(sb, "        h_new[i] = retention + update;\n");
        sb_printf(sb, "    }\n");
    }
    
    sb_printf(sb, "}\n\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SEQUENCE FORWARD
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_forward_function(StringBuffer* sb, const CfCCellSpec* spec) {
    sb_printf(sb, "/* ═══════════════════════════════════════════════════════════════\n");
    sb_printf(sb, " * CfC FORWARD - Sequence Processing\n");
    sb_printf(sb, " * ═══════════════════════════════════════════════════════════════ */\n\n");
    
    sb_printf(sb, "void %s_forward(\n", spec->name);
    sb_printf(sb, "    const float* inputs,   /* [seq_len, %d] */\n", spec->input_dim);
    sb_printf(sb, "    int seq_len,\n");
    sb_printf(sb, "    float dt,\n");
    sb_printf(sb, "    const float* h_init,   /* [%d] or NULL */\n", spec->hidden_dim);
    sb_printf(sb, "    float* outputs,        /* [seq_len, %d] */\n", spec->hidden_dim);
    sb_printf(sb, "    float* h_final         /* [%d] or NULL */\n", spec->hidden_dim);
    sb_printf(sb, ") {\n");
    
    sb_printf(sb, "    float h_current[%d];\n\n", spec->hidden_dim);
    
    sb_printf(sb, "    /* Initialize hidden state */\n");
    sb_printf(sb, "    if (h_init) {\n");
    sb_printf(sb, "        memcpy(h_current, h_init, %d * sizeof(float));\n", spec->hidden_dim);
    sb_printf(sb, "    } else {\n");
    sb_printf(sb, "        memset(h_current, 0, %d * sizeof(float));\n", spec->hidden_dim);
    sb_printf(sb, "    }\n\n");
    
    sb_printf(sb, "    /* Process sequence */\n");
    sb_printf(sb, "    for (int t = 0; t < seq_len; t++) {\n");
    sb_printf(sb, "        const float* x_t = inputs + t * %d;\n", spec->input_dim);
    sb_printf(sb, "        float* out_t = outputs + t * %d;\n\n", spec->hidden_dim);
    sb_printf(sb, "        %s_cell(x_t, h_current, dt, out_t);\n", spec->name);
    sb_printf(sb, "        memcpy(h_current, out_t, %d * sizeof(float));\n", spec->hidden_dim);
    sb_printf(sb, "    }\n\n");
    
    sb_printf(sb, "    if (h_final) {\n");
    sb_printf(sb, "        memcpy(h_final, h_current, %d * sizeof(float));\n", spec->hidden_dim);
    sb_printf(sb, "    }\n");
    sb_printf(sb, "}\n\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * OUTPUT PROJECTION
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_output_function(StringBuffer* sb, const CfCCellSpec* spec) {
    if (!spec->has_output) return;
    
    sb_printf(sb, "/* ═══════════════════════════════════════════════════════════════\n");
    sb_printf(sb, " * OUTPUT PROJECTION\n");
    sb_printf(sb, " * ═══════════════════════════════════════════════════════════════ */\n\n");
    
    /* MatVec kernel for output */
    emit_matvec_kernel(sb, spec->name, "out", spec->output_dim, spec->hidden_dim);
    
    sb_printf(sb, "void %s_output(\n", spec->name);
    sb_printf(sb, "    const float* h,        /* [%d] */\n", spec->hidden_dim);
    sb_printf(sb, "    float* logits          /* [%d] */\n", spec->output_dim);
    sb_printf(sb, ") {\n");
    
    sb_printf(sb, "    int8_t h_q[%d] __attribute__((aligned(64)));\n", spec->hidden_dim);
    sb_printf(sb, "    int32_t out_pre[%d];\n\n", spec->output_dim);
    
    /* Quantize hidden state */
    sb_printf(sb, "    /* Quantize hidden state */\n");
    sb_printf(sb, "    float scale = 0.0f;\n");
    sb_printf(sb, "    for (int i = 0; i < %d; i++) {\n", spec->hidden_dim);
    sb_printf(sb, "        float abs_val = h[i] > 0 ? h[i] : -h[i];\n");
    sb_printf(sb, "        if (abs_val > scale) scale = abs_val;\n");
    sb_printf(sb, "    }\n");
    sb_printf(sb, "    if (scale < 1e-8f) scale = 1e-8f;\n");
    sb_printf(sb, "    float inv_scale = 127.0f / scale;\n\n");
    
    sb_printf(sb, "    for (int i = 0; i < %d; i++) {\n", spec->hidden_dim);
    sb_printf(sb, "        int v = (int)(h[i] * inv_scale);\n");
    sb_printf(sb, "        h_q[i] = v > 127 ? 127 : (v < -127 ? -127 : v);\n");
    sb_printf(sb, "    }\n\n");
    
    sb_printf(sb, "    /* Output projection */\n");
    sb_printf(sb, "    %s_out_matvec(h_q, out_pre);\n", spec->name);
    sb_printf(sb, "    for (int i = 0; i < %d; i++) {\n", spec->output_dim);
    sb_printf(sb, "        logits[i] = (float)out_pre[i] * scale / 127.0f + %s_b_out[i];\n", spec->name);
    sb_printf(sb, "    }\n");
    sb_printf(sb, "}\n\n");
    
    /* Softmax variant */
    sb_printf(sb, "void %s_output_softmax(\n", spec->name);
    sb_printf(sb, "    const float* h,        /* [%d] */\n", spec->hidden_dim);
    sb_printf(sb, "    float* probs           /* [%d] */\n", spec->output_dim);
    sb_printf(sb, ") {\n");
    sb_printf(sb, "    float logits[%d];\n", spec->output_dim);
    sb_printf(sb, "    %s_output(h, logits);\n", spec->name);
    sb_printf(sb, "    _softmax(logits, probs, %d);\n", spec->output_dim);
    sb_printf(sb, "}\n\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FOOTER
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_footer(StringBuffer* sb, const CfCCellSpec* spec) {
    sb_printf(sb, "#endif /* %s_CFC_H */\n", spec->name);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PUBLIC API
 * ═══════════════════════════════════════════════════════════════════════════ */

static int validate_cfc_spec(const CfCCellSpec* spec, trix_error_context_t* ctx) {
    if (!spec) {
        trix_error_set(ctx, TRIX_ERROR_NULL_POINTER, "CfCCellSpec is NULL");
        return TRIX_ERROR_NULL_POINTER;
    }
    if (spec->input_dim <= 0 || spec->hidden_dim <= 0) {
        trix_error_set(ctx, TRIX_ERROR_INVALID_DIMENSIONS,
            "Invalid dimensions: input_dim=%d, hidden_dim=%d",
            spec->input_dim, spec->hidden_dim);
        return TRIX_ERROR_INVALID_DIMENSIONS;
    }
    if (spec->has_output && spec->output_dim <= 0) {
        trix_error_set(ctx, TRIX_ERROR_INVALID_DIMENSIONS,
            "has_output=true but output_dim=%d", spec->output_dim);
        return TRIX_ERROR_INVALID_DIMENSIONS;
    }
    return TRIX_OK;
}

size_t forge_cfc_to_string(const CfCCellSpec* spec, char* buffer, size_t size) {
    trix_error_context_t ctx_body;
    trix_error_context_t* ctx = &ctx_body;
    trix_error_init(ctx);
    trix_log_init();
    
    if (!buffer || size == 0) {
        log_error("forge_cfc_to_string: Invalid buffer");
        return 0;
    }
    
    int validation = validate_cfc_spec(spec, ctx);
    if (validation != TRIX_OK) {
        log_error("forge_cfc_to_string: %s", trix_error_description(validation));
        return 0;
    }
    
    log_info("Generating CfC cell for '%s' (input=%d, hidden=%d)",
             spec->name, spec->input_dim, spec->hidden_dim);
    
    StringBuffer sb;
    sb_init(&sb, buffer, size);
    
    int concat_dim = spec->input_dim + spec->hidden_dim;
    
    emit_cfc_header(&sb, spec);
    emit_activations(&sb);
    emit_weights(&sb, spec);
    
    /* MatVec kernels for gate and candidate */
    sb_printf(&sb, "/* ═══════════════════════════════════════════════════════════════\n");
    sb_printf(&sb, " * NEON MATVEC KERNELS (Block-8-K64)\n");
    sb_printf(&sb, " * ═══════════════════════════════════════════════════════════════ */\n\n");
    
    emit_matvec_kernel(&sb, spec->name, "gate", spec->hidden_dim, concat_dim);
    emit_matvec_kernel(&sb, spec->name, "cand", spec->hidden_dim, concat_dim);
    
    emit_cell_function(&sb, spec);
    emit_forward_function(&sb, spec);
    emit_output_function(&sb, spec);
    emit_footer(&sb, spec);
    
    return sb.pos;
}

int forge_cfc_to_file(const CfCCellSpec* spec, FILE* out) {
    if (!spec || !out) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
    char buffer[256 * 1024];
    size_t len = forge_cfc_to_string(spec, buffer, sizeof(buffer));
    
    if (len == 0) {
        log_error("forge_cfc_to_file: Failed to generate CfC cell");
        return TRIX_ERROR_INTERNAL;
    }
    
    size_t written = fwrite(buffer, 1, len, out);
    if (written != len) {
        log_error("forge_cfc_to_file: Failed to write to file");
        return TRIX_ERROR_FILE_WRITE;
    }
    
    log_info("Successfully wrote CfC cell to file");
    return TRIX_OK;
}
