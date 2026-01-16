/*
 * genesis.c — The EntroMorphic Foundry
 *
 * "Don't train. Grow."
 *
 * This is not a training script. This is a SEED GENERATOR.
 * It uses evolutionary genesis to discover optimal topologies,
 * then exports them as frozen, deployable chips.
 *
 * Usage:
 *   ./genesis sine      # Generate sine predictor seed
 *   ./genesis anomaly   # Generate anomaly detector seed
 *   ./genesis control   # Generate PID controller seed
 *   ./genesis pattern   # Generate pattern classifier seed
 *
 * Output:
 *   seeds/<target>_seed.h — Ready-to-deploy frozen chip
 *
 * Build:
 *   gcc -O3 -I../include genesis.c -o genesis -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdint.h>

#include "trixc/shapefabric.h"
#include "trixc/entromorph.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Foundry Configuration
 * ═══════════════════════════════════════════════════════════════════════════ */

#define POP_SIZE        512     /* Balance: diversity vs speed */
#define MAX_GENS        10000   /* Maximum generations */
#define TARGET_TIME     60.0    /* Target completion time (seconds) */
#define ELITE_COUNT     8       /* Elites to preserve */
#define TOURNAMENT_K    4       /* Tournament size */

/* Graph configuration */
#define MAX_NODES       128
#define MAX_REGS        256
#define HIDDEN_DIM      8       /* "19-neuron micro-brain" target */

/* Scent window sizes */
#define FAST_WINDOW     256     /* Long enough for gluttony penalty to detect explosion */
#define FULL_WINDOW     512     /* Promoted candidates */

/* Signal buffer */
#define SIGNAL_LEN      2048

/* ═══════════════════════════════════════════════════════════════════════════
 * METABOLIC REGULARIZATION — The Gluttony Penalty
 *
 * Networks that solve problems via "Infinite Gain" (pumping internal states
 * to massive values) are punished. This forces evolution to discover
 * efficient topologies rather than brute-force energy solutions.
 * ═══════════════════════════════════════════════════════════════════════════ */
#define MAX_ALLOWED_STATE   5.0f      /* Sane register bound */
#define GLUTTONY_PENALTY    100.0f    /* Per-unit penalty for excess */

/* ═══════════════════════════════════════════════════════════════════════════
 * Seed Targets — What are we evolving for?
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    TARGET_SINE,        /* Time-series prediction (noisy sine) */
    TARGET_ANOMALY,     /* Anomaly detection (spike finding) */
    TARGET_CONTROL,     /* PID-like control (error minimization) */
    TARGET_PATTERN,     /* Pattern classification (Zit detection) */
    TARGET_COUNT
} SeedTarget;

const char* TARGET_NAMES[] = {
    "sine",
    "anomaly",
    "control",
    "pattern"
};

const char* TARGET_DESCRIPTIONS[] = {
    "Time-series prediction (noisy sine wave)",
    "Anomaly detection (spike identification)",
    "PID-like control (error minimization)",
    "Pattern classification (Zit resonance)"
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Memory Arena — Zero-Copy Population
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    ShapeNode* node_arena;
    float* reg_arena;
    ShapeGraph* graphs;
    float* fitness;

    /* Best ever tracking */
    ShapeNode* best_nodes;
    float* best_regs;
    ShapeGraph best_graph;
    float best_fitness;
    int best_gen;

    /* Metadata */
    SeedTarget target;
    uint32_t generation;
    uint64_t total_mutations;
    uint64_t total_evals;
} Foundry;

/* ═══════════════════════════════════════════════════════════════════════════
 * Signal Generators — The Curriculum
 * ═══════════════════════════════════════════════════════════════════════════ */

static float signal_buffer[SIGNAL_LEN + 1];
static int anomaly_labels[SIGNAL_LEN];  /* For anomaly detection */

void generate_sine_signal(EntroRNG* rng) {
    for (int t = 0; t <= SIGNAL_LEN; t++) {
        /* Complex signal: Primary sine + harmonics
         * This forces "Liquid" tracking — a DC approximation won't work.
         */
        float phase1 = t * 0.1f;           /* Primary frequency */
        float phase2 = t * 0.4f;           /* 4x harmonic */
        float phase3 = t * 0.023f;         /* Slow drift (incommensurate) */

        /* Composite wave — must OSCILLATE to track this */
        float clean = 0.6f * sinf(phase1)
                    + 0.25f * sinf(phase2)
                    + 0.15f * sinf(phase3);

        /* Light noise */
        float noise = entro_rng_range(rng, -0.1f, 0.1f);
        signal_buffer[t] = clean + noise;
    }
}

void generate_anomaly_signal(EntroRNG* rng) {
    memset(anomaly_labels, 0, sizeof(anomaly_labels));

    for (int t = 0; t <= SIGNAL_LEN; t++) {
        /* Baseline: smooth random walk */
        float baseline = sinf(t * 0.05f) + entro_rng_range(rng, -0.1f, 0.1f);
        signal_buffer[t] = baseline;

        /* Inject anomalies (5% of samples) */
        if (entro_rng_next(rng) % 100 < 5) {
            float spike = entro_rng_range(rng, 1.5f, 3.0f);
            if (entro_rng_next(rng) % 2) spike = -spike;
            signal_buffer[t] += spike;
            anomaly_labels[t] = 1;
        }
    }
}

void generate_control_signal(EntroRNG* rng) {
    /* Target setpoint with disturbances */
    float setpoint = 0.0f;
    for (int t = 0; t <= SIGNAL_LEN; t++) {
        /* Setpoint changes occasionally */
        if (t % 200 == 0) {
            setpoint = entro_rng_range(rng, -1.0f, 1.0f);
        }
        /* Add measurement noise */
        signal_buffer[t] = setpoint + entro_rng_range(rng, -0.2f, 0.2f);
    }
}

void generate_pattern_signal(EntroRNG* rng) {
    /* Binary patterns embedded in noise */
    for (int t = 0; t <= SIGNAL_LEN; t++) {
        /* Pattern: repeating 8-bit sequence */
        int pattern_bit = (t / 10) % 8;
        uint8_t pattern = 0b10110010;  /* Fixed pattern to learn */
        float target = (pattern >> pattern_bit) & 1 ? 0.8f : -0.8f;
        float noise = entro_rng_range(rng, -0.3f, 0.3f);
        signal_buffer[t] = target + noise;
    }
}

void generate_signal(SeedTarget target, EntroRNG* rng) {
    switch (target) {
        case TARGET_SINE:    generate_sine_signal(rng); break;
        case TARGET_ANOMALY: generate_anomaly_signal(rng); break;
        case TARGET_CONTROL: generate_control_signal(rng); break;
        case TARGET_PATTERN: generate_pattern_signal(rng); break;
        default: generate_sine_signal(rng);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Fitness Kernels — Selection Pressure
 *
 * Each kernel uses XOR resonance + task-specific scoring.
 * All kernels must be FAST (bitwise operations preferred).
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Shared resonance scoring (Zit Detector) */
static inline float resonance_score(uint32_t curr, uint32_t prev) {
    uint32_t entropy = __builtin_popcount(curr ^ prev);
    if (entropy < 4) return 12.0f;   /* Locked */
    if (entropy < 8) return 8.0f;    /* Stable */
    if (entropy < 16) return 4.0f;   /* Moderate */
    return 0.0f;                      /* Chaotic */
}

/* SINE: Phase-shifted tracking with INERTIA requirement
 *
 * CfC neurons are SMOOTHERS, not differentiators.
 * They CAN track a signal with phase shift (lag or lead).
 *
 * CRITICAL: We reward correlation with FUTURE values, not current.
 * This forces the network to build predictive dynamics, not pass-through.
 *
 * Task: Output should correlate with signal[t+PREDICT_HORIZON]
 */
#define PREDICT_HORIZON 3  /* Predict 3 steps ahead */

static inline float fitness_sine(ShapeGraph* g, int window) {
    float fitness = 0.0f;
    memset(g->registers, 0, g->reg_count * sizeof(float));

    /* Store outputs for cross-correlation */
    float outputs[FULL_WINDOW];
    float sum_out = 0.0f, sum_out_sq = 0.0f;

    /* METABOLIC TRACKING: Monitor state magnitude */
    float max_state_magnitude = 0.0f;

    for (int t = 0; t < window; t++) {
        g->registers[g->input_start] = signal_buffer[t];
        shape_execute_graph(g, 0.1f);

        outputs[t] = g->registers[g->output_start];
        sum_out += outputs[t];
        sum_out_sq += outputs[t] * outputs[t];

        /* Check ALL registers for state explosion */
        for (uint32_t r = 0; r < g->reg_count; r++) {
            float val = fabsf(g->registers[r]);
            if (val > max_state_magnitude) {
                max_state_magnitude = val;
            }
        }
    }

    /* Variance check */
    float mean_out = sum_out / window;
    float var_out = (sum_out_sq / window) - (mean_out * mean_out);

    if (var_out < 0.02f) {
        return 0.0f;  /* Flat output = death */
    }

    /* ═══════════════════════════════════════════════════════════════════════
     * ANTI-IDENTITY CHECK
     *
     * If output ≈ input (identity function), the network is cheating.
     * Measure correlation between output and CURRENT input.
     * If r > 0.99, it's a pass-through — kill it.
     * ═══════════════════════════════════════════════════════════════════════ */
    float sum_xy = 0.0f, sum_x = 0.0f, sum_y = 0.0f;
    float sum_x2 = 0.0f, sum_y2 = 0.0f;
    for (int t = 0; t < window; t++) {
        float x = outputs[t];
        float y = signal_buffer[t];  /* Current input */
        sum_xy += x * y;
        sum_x += x;
        sum_y += y;
        sum_x2 += x * x;
        sum_y2 += y * y;
    }
    float n = (float)window;
    float id_num = n * sum_xy - sum_x * sum_y;
    float id_den1 = n * sum_x2 - sum_x * sum_x;
    float id_den2 = n * sum_y2 - sum_y * sum_y;
    float id_den = sqrtf(fabsf(id_den1) * fabsf(id_den2));
    float identity_corr = (id_den > 0.001f) ? (id_num / id_den) : 0.0f;

    if (identity_corr > 0.995f) {
        return 0.0f;  /* Pass-through identity = death */
    }

    /* ═══════════════════════════════════════════════════════════════════════
     * PREDICTION CORRELATION
     *
     * Correlate output[t] with signal[t + PREDICT_HORIZON].
     * This forces the network to PREDICT, not just pass through.
     * ═══════════════════════════════════════════════════════════════════════ */
    float pred_sum_xy = 0.0f, pred_sum_x = 0.0f, pred_sum_y = 0.0f;
    float pred_sum_x2 = 0.0f, pred_sum_y2 = 0.0f;
    int pred_count = 0;

    for (int t = 0; t < window - PREDICT_HORIZON; t++) {
        float x = outputs[t];
        float y = signal_buffer[t + PREDICT_HORIZON];  /* FUTURE value */

        pred_sum_xy += x * y;
        pred_sum_x += x;
        pred_sum_y += y;
        pred_sum_x2 += x * x;
        pred_sum_y2 += y * y;
        pred_count++;
    }

    float pred_n = (float)pred_count;
    float pred_num = pred_n * pred_sum_xy - pred_sum_x * pred_sum_y;
    float pred_den1 = pred_n * pred_sum_x2 - pred_sum_x * pred_sum_x;
    float pred_den2 = pred_n * pred_sum_y2 - pred_sum_y * pred_sum_y;
    float pred_den = sqrtf(fabsf(pred_den1) * fabsf(pred_den2));
    float pred_corr = (pred_den > 0.001f) ? (pred_num / pred_den) : 0.0f;

    /* ═══════════════════════════════════════════════════════════════════════
     * REWARD PREDICTION CORRELATION
     * ═══════════════════════════════════════════════════════════════════════ */
    if (pred_corr > 0.95f) fitness += window * 8.0f;       /* Near perfect */
    else if (pred_corr > 0.9f) fitness += window * 6.0f;   /* Excellent */
    else if (pred_corr > 0.8f) fitness += window * 4.0f;   /* Very good */
    else if (pred_corr > 0.7f) fitness += window * 2.5f;   /* Good */
    else if (pred_corr > 0.5f) fitness += window * 1.0f;   /* Moderate */
    /* Low correlation gets base fitness only */

    /* Base fitness from prediction error */
    for (int t = 0; t < window - PREDICT_HORIZON; t++) {
        float error = fabsf(outputs[t] - signal_buffer[t + PREDICT_HORIZON]);
        if (error < 0.1f) fitness += 10.0f;
        else if (error < 0.2f) fitness += 5.0f;
        else if (error < 0.3f) fitness += 2.0f;
    }

    /* ═══════════════════════════════════════════════════════════════════════
     * THE GLUTTONY PENALTY — Metabolic Regularization
     *
     * If the network shouts louder than +/- MAX_ALLOWED_STATE, crush its score.
     * This forces efficient topology over brute-force gain.
     * ═══════════════════════════════════════════════════════════════════════ */
    float gluttony_cost = 0.0f;
    if (max_state_magnitude > MAX_ALLOWED_STATE) {
        /* Exponential penalty for every unit over the limit */
        gluttony_cost = (max_state_magnitude - MAX_ALLOWED_STATE) * GLUTTONY_PENALTY;
    }

    /* Apply the governor */
    float final_fitness = fitness - gluttony_cost;
    return (final_fitness > 0.0f) ? final_fitness : 0.0f;
}

/* ANOMALY: Spike detection + low false positive */
static inline float fitness_anomaly(ShapeGraph* g, int window) {
    float fitness = 0.0f;
    memset(g->registers, 0, g->reg_count * sizeof(float));

    int true_pos = 0, false_pos = 0, true_neg = 0, false_neg = 0;

    for (int t = 0; t < window && t < SIGNAL_LEN; t++) {
        g->registers[g->input_start] = signal_buffer[t];
        shape_execute_graph(g, 0.1f);

        float output = g->registers[g->output_start];
        int predicted_anomaly = (fabsf(output) > 0.5f) ? 1 : 0;
        int actual_anomaly = anomaly_labels[t];

        if (predicted_anomaly && actual_anomaly) true_pos++;
        else if (predicted_anomaly && !actual_anomaly) false_pos++;
        else if (!predicted_anomaly && actual_anomaly) false_neg++;
        else true_neg++;
    }

    /* F1-like score: reward true positives, penalize false positives */
    fitness = true_pos * 50.0f - false_pos * 10.0f - false_neg * 20.0f + true_neg * 2.0f;

    return fitness;
}

/* CONTROL: Error minimization + stability */
static inline float fitness_control(ShapeGraph* g, int window) {
    float fitness = 0.0f;
    memset(g->registers, 0, g->reg_count * sizeof(float));

    float accumulated_error = 0.0f;
    float prev_output = 0.0f;
    uint32_t prev_state = 0;

    for (int t = 0; t < window; t++) {
        /* Input is setpoint, output should track it */
        float setpoint = signal_buffer[t];
        g->registers[g->input_start] = setpoint;
        shape_execute_graph(g, 0.1f);

        float output = g->registers[g->output_start];
        float error = fabsf(output - setpoint);
        accumulated_error += error;

        /* Tracking accuracy */
        if (error < 0.1f) fitness += 15.0f;
        else if (error < 0.2f) fitness += 10.0f;
        else if (error < 0.3f) fitness += 5.0f;

        /* Smooth control (penalize oscillation) */
        float delta = fabsf(output - prev_output);
        if (delta < 0.1f) fitness += 5.0f;
        prev_output = output;

        /* Resonance (stability) */
        uint32_t curr_state = *(uint32_t*)&g->registers[2];
        fitness += resonance_score(curr_state, prev_state);
        prev_state = curr_state;
    }

    /* Bonus for low accumulated error */
    if (accumulated_error < window * 0.1f) fitness += 100.0f;

    return fitness;
}

/* PATTERN: Classification accuracy */
static inline float fitness_pattern(ShapeGraph* g, int window) {
    float fitness = 0.0f;
    memset(g->registers, 0, g->reg_count * sizeof(float));
    uint32_t prev_state = 0;

    for (int t = 0; t < window; t++) {
        g->registers[g->input_start] = signal_buffer[t];
        shape_execute_graph(g, 0.1f);

        float output = g->registers[g->output_start];

        /* Pattern is positive/negative based on underlying bit */
        int pattern_bit = (t / 10) % 8;
        uint8_t pattern = 0b10110010;
        float expected = (pattern >> pattern_bit) & 1 ? 0.8f : -0.8f;

        /* Classification: did we get the sign right? */
        if ((output > 0) == (expected > 0)) {
            fitness += 15.0f;
            /* Bonus for confidence */
            if (fabsf(output) > 0.5f) fitness += 5.0f;
        }

        /* Resonance (locking to pattern) */
        uint32_t curr_state = *(uint32_t*)&g->registers[2];
        fitness += resonance_score(curr_state, prev_state) * 0.5f;
        prev_state = curr_state;
    }

    return fitness;
}

/* Dispatch fitness function */
static inline float evaluate_fitness(ShapeGraph* g, SeedTarget target, int window) {
    switch (target) {
        case TARGET_SINE:    return fitness_sine(g, window);
        case TARGET_ANOMALY: return fitness_anomaly(g, window);
        case TARGET_CONTROL: return fitness_control(g, window);
        case TARGET_PATTERN: return fitness_pattern(g, window);
        default: return fitness_sine(g, window);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Graph Construction — Random Genesis
 * ═══════════════════════════════════════════════════════════════════════════ */

void build_random_graph(ShapeGraph* g, EntroRNG* rng) {
    /* 1 input, 1 output */
    shape_graph_set_inputs(g, 1);
    shape_graph_set_outputs(g, 1);

    /* Allocate hidden state registers */
    uint32_t state_regs[HIDDEN_DIM];
    for (int h = 0; h < HIDDEN_DIM; h++) {
        state_regs[h] = shape_alloc_reg(g);
    }

    /* Build CfC layer with random time constants */
    float taus[HIDDEN_DIM];
    for (int h = 0; h < HIDDEN_DIM; h++) {
        taus[h] = entro_rng_range(rng, 0.5f, 4.0f);
    }

    uint32_t input_regs[HIDDEN_DIM];
    for (int h = 0; h < HIDDEN_DIM; h++) {
        input_regs[h] = g->input_start;
    }

    shape_build_cfc_layer(g, input_regs, state_regs, HIDDEN_DIM, taus);

    /* Output: weighted sum through softsign */
    uint32_t sum_reg = shape_alloc_reg(g);
    shape_add_node(g, SHAPE_CONST, sum_reg, 0, 0, 0.0f);

    for (int h = 0; h < HIDDEN_DIM; h++) {
        float w = entro_rng_range(rng, -0.5f, 0.5f);
        uint32_t w_reg = shape_alloc_reg(g);
        shape_add_node(g, SHAPE_CONST, w_reg, 0, 0, w);

        uint32_t prod_reg = shape_alloc_reg(g);
        shape_add_node(g, SHAPE_MUL, prod_reg, state_regs[h], w_reg, 0.0f);

        uint32_t new_sum = shape_alloc_reg(g);
        shape_add_node(g, SHAPE_ADD, new_sum, sum_reg, prod_reg, 0.0f);
        sum_reg = new_sum;
    }

    shape_add_node(g, SHAPE_SOFTSIGN, g->output_start, sum_reg, 0, 0.0f);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Mutation Operators
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline void mutate_genome(ShapeGraph* g, EntroRNG* rng) {
    uint32_t mutation_type = entro_rng_next(rng) % 100;

    if (mutation_type < 40) {
        /* Rewire */
        uint32_t node_idx = entro_rng_next(rng) % g->node_count;
        uint32_t new_input = entro_rng_next(rng) % g->reg_count;
        shape_mutate_rewire(g, node_idx, new_input);
    } else if (mutation_type < 70) {
        /* Perturb value */
        uint32_t node_idx = entro_rng_next(rng) % g->node_count;
        float delta = entro_rng_range(rng, -0.3f, 0.3f);
        shape_mutate_value(g, node_idx, delta);
    } else if (mutation_type < 85) {
        /* Change opcode */
        uint32_t node_idx = entro_rng_next(rng) % g->node_count;
        ShapeOpcode ops[] = {SHAPE_SOFTSIGN, SHAPE_TANH, SHAPE_RELU, SHAPE_SIGMOID, SHAPE_ADD, SHAPE_MUL};
        shape_mutate_opcode(g, node_idx, ops[entro_rng_next(rng) % 6]);
    } else if (mutation_type < 95) {
        /* Toggle active */
        uint32_t node_idx = entro_rng_next(rng) % g->node_count;
        if (g->nodes[node_idx].flags & NODE_FLAG_ACTIVE) {
            shape_mutate_disable(g, node_idx);
        } else {
            shape_mutate_enable(g, node_idx);
        }
    } else {
        /* Macro mutation */
        for (int m = 0; m < 4; m++) {
            uint32_t node_idx = entro_rng_next(rng) % g->node_count;
            uint32_t new_input = entro_rng_next(rng) % g->reg_count;
            shape_mutate_rewire(g, node_idx, new_input);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Selection
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline int tournament_select(Foundry* f, EntroRNG* rng) {
    int best_idx = entro_rng_next(rng) % POP_SIZE;
    float best_fit = f->fitness[best_idx];

    for (int t = 1; t < TOURNAMENT_K; t++) {
        int idx = entro_rng_next(rng) % POP_SIZE;
        if (f->fitness[idx] > best_fit) {
            best_fit = f->fitness[idx];
            best_idx = idx;
        }
    }
    return best_idx;
}

static inline void copy_graph(ShapeGraph* dst, ShapeGraph* src) {
    memcpy(dst->nodes, src->nodes, src->node_count * sizeof(ShapeNode));
    memcpy(dst->registers, src->registers, src->reg_count * sizeof(float));
    dst->node_count = src->node_count;
    dst->reg_count = src->reg_count;
    dst->input_start = src->input_start;
    dst->input_count = src->input_count;
    dst->output_start = src->output_start;
    dst->output_count = src->output_count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Seed Export — Generate Deployable Header
 * ═══════════════════════════════════════════════════════════════════════════ */

void export_seed(Foundry* f, const char* filename) {
    FILE* out = fopen(filename, "w");
    if (!out) {
        printf("Error: Cannot write to %s\n", filename);
        return;
    }

    ShapeGraph* g = &f->best_graph;
    const char* target_name = TARGET_NAMES[f->target];

    fprintf(out, "/*\n");
    fprintf(out, " * %s_seed.h — EntroMorphic Frozen Seed\n", target_name);
    fprintf(out, " *\n");
    fprintf(out, " * Target: %s\n", TARGET_DESCRIPTIONS[f->target]);
    fprintf(out, " * Generated: %s", ctime(&(time_t){time(NULL)}));
    fprintf(out, " * Fitness: %.2f\n", f->best_fitness);
    fprintf(out, " * Generation: %d\n", f->best_gen);
    fprintf(out, " * Mutations: %llu\n", (unsigned long long)f->total_mutations);
    fprintf(out, " *\n");
    fprintf(out, " * Usage:\n");
    fprintf(out, " *   #include \"%s_seed.h\"\n", target_name);
    fprintf(out, " *   float output = %s_seed_step(input, state);\n", target_name);
    fprintf(out, " */\n\n");

    fprintf(out, "#ifndef %s_SEED_H\n", target_name);
    fprintf(out, "#define %s_SEED_H\n\n", target_name);

    fprintf(out, "#include <stdint.h>\n");
    fprintf(out, "#include <string.h>\n");
    fprintf(out, "#include <math.h>\n\n");

    /* Seed metadata */
    fprintf(out, "/* Seed Metadata */\n");
    fprintf(out, "#define %s_SEED_NODES %u\n", target_name, g->node_count);
    fprintf(out, "#define %s_SEED_REGS %u\n", target_name, g->reg_count);
    fprintf(out, "#define %s_SEED_FITNESS %.2ff\n", target_name, f->best_fitness);
    fprintf(out, "#define %s_SEED_GEN %d\n\n", target_name, f->best_gen);

    /* Node data */
    fprintf(out, "/* Frozen Node Data */\n");
    fprintf(out, "static const uint8_t %s_seed_opcodes[%u] = {\n    ",
            target_name, g->node_count);
    for (uint32_t i = 0; i < g->node_count; i++) {
        fprintf(out, "%u", g->nodes[i].opcode);
        if (i < g->node_count - 1) fprintf(out, ", ");
        if ((i + 1) % 16 == 0 && i < g->node_count - 1) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "static const uint8_t %s_seed_flags[%u] = {\n    ",
            target_name, g->node_count);
    for (uint32_t i = 0; i < g->node_count; i++) {
        fprintf(out, "%u", g->nodes[i].flags);
        if (i < g->node_count - 1) fprintf(out, ", ");
        if ((i + 1) % 16 == 0 && i < g->node_count - 1) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "static const uint16_t %s_seed_out_idx[%u] = {\n    ",
            target_name, g->node_count);
    for (uint32_t i = 0; i < g->node_count; i++) {
        fprintf(out, "%u", g->nodes[i].out_idx);
        if (i < g->node_count - 1) fprintf(out, ", ");
        if ((i + 1) % 16 == 0 && i < g->node_count - 1) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "static const uint32_t %s_seed_in_a[%u] = {\n    ",
            target_name, g->node_count);
    for (uint32_t i = 0; i < g->node_count; i++) {
        fprintf(out, "%u", g->nodes[i].in_a_idx);
        if (i < g->node_count - 1) fprintf(out, ", ");
        if ((i + 1) % 8 == 0 && i < g->node_count - 1) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "static const uint32_t %s_seed_in_b[%u] = {\n    ",
            target_name, g->node_count);
    for (uint32_t i = 0; i < g->node_count; i++) {
        fprintf(out, "%u", g->nodes[i].in_b_idx);
        if (i < g->node_count - 1) fprintf(out, ", ");
        if ((i + 1) % 8 == 0 && i < g->node_count - 1) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "static const float %s_seed_values[%u] = {\n    ",
            target_name, g->node_count);
    for (uint32_t i = 0; i < g->node_count; i++) {
        fprintf(out, "%.6ff", g->nodes[i].value);
        if (i < g->node_count - 1) fprintf(out, ", ");
        if ((i + 1) % 6 == 0 && i < g->node_count - 1) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n\n");

    /* Execution function */
    fprintf(out, "/* Execute one step of the seed */\n");
    fprintf(out, "static inline float %s_seed_step(float input, float* state) {\n", target_name);
    fprintf(out, "    float regs[%u];\n", g->reg_count);
    fprintf(out, "    memcpy(regs, state, %u * sizeof(float));\n", g->reg_count);
    fprintf(out, "    regs[%u] = input;  /* Input register */\n\n", g->input_start);

    fprintf(out, "    /* Execute all active nodes */\n");
    fprintf(out, "    for (int i = 0; i < %u; i++) {\n", g->node_count);
    fprintf(out, "        if (!(%s_seed_flags[i] & 0x01)) continue;\n", target_name);
    fprintf(out, "        float a = regs[%s_seed_in_a[i]];\n", target_name);
    fprintf(out, "        float b = regs[%s_seed_in_b[i]];\n", target_name);
    fprintf(out, "        float v = %s_seed_values[i];\n", target_name);
    fprintf(out, "        float result;\n");
    fprintf(out, "        switch (%s_seed_opcodes[i]) {\n", target_name);
    fprintf(out, "            case 8:  result = a + b; break;  /* ADD */\n");
    fprintf(out, "            case 9:  result = a - b; break;  /* SUB */\n");
    fprintf(out, "            case 10: result = a * b; break;  /* MUL */\n");
    fprintf(out, "            case 11: result = b != 0 ? a/b : 0; break;  /* DIV */\n");
    fprintf(out, "            case 13: result = fabsf(a); break;  /* ABS */\n");
    fprintf(out, "            case 14: result = a > b ? a : b; break;  /* MAX */\n");
    fprintf(out, "            case 16: result = expf(a); break;  /* EXP */\n");
    fprintf(out, "            case 20: result = 1.0f/(1.0f+expf(-a)); break;  /* SIGMOID */\n");
    fprintf(out, "            case 21: result = tanhf(a); break;  /* TANH */\n");
    fprintf(out, "            case 22: result = a > 0 ? a : 0; break;  /* RELU */\n");
    fprintf(out, "            case 23: result = a/(1.0f+fabsf(a)); break;  /* SOFTSIGN */\n");
    fprintf(out, "            case 24: result = v; break;  /* CONST */\n");
    fprintf(out, "            case 27: result = a * expf(-0.1f/v); break;  /* DECAY */\n");
    fprintf(out, "            default: result = 0; break;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        regs[%s_seed_out_idx[i]] = result;\n", target_name);
    fprintf(out, "    }\n\n");

    fprintf(out, "    /* Save state */\n");
    fprintf(out, "    memcpy(state, regs, %u * sizeof(float));\n", g->reg_count);
    fprintf(out, "    return regs[%u];  /* Output register */\n", g->output_start);
    fprintf(out, "}\n\n");

    /* Reset function */
    fprintf(out, "/* Reset seed state */\n");
    fprintf(out, "static inline void %s_seed_reset(float* state) {\n", target_name);
    fprintf(out, "    memset(state, 0, %u * sizeof(float));\n", g->reg_count);
    fprintf(out, "}\n\n");

    fprintf(out, "#endif /* %s_SEED_H */\n", target_name);

    fclose(out);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main — The Genesis Process
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char** argv) {
    /* Parse target */
    SeedTarget target = TARGET_SINE;
    if (argc > 1) {
        for (int t = 0; t < TARGET_COUNT; t++) {
            if (strcmp(argv[1], TARGET_NAMES[t]) == 0) {
                target = t;
                break;
            }
        }
    }

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  THE FOUNDRY — EntroMorphic Genesis                          ║\n");
    printf("║  \"Don't train. Grow.\"                                        ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Target: %s\n", TARGET_NAMES[target]);
    printf("Description: %s\n", TARGET_DESCRIPTIONS[target]);
    printf("\n");

    /* Initialize RNG */
    EntroRNG rng;
    entro_rng_seed(&rng, (uint64_t)time(NULL));

    /* Generate signal */
    printf("Generating curriculum...\n");
    generate_signal(target, &rng);
    printf("  Signal length: %d samples\n", SIGNAL_LEN);
    printf("\n");

    /* Allocate foundry */
    printf("Initializing foundry...\n");
    Foundry* f = (Foundry*)malloc(sizeof(Foundry));
    memset(f, 0, sizeof(Foundry));
    f->target = target;
    f->best_fitness = -INFINITY;

    /* Allocate arenas */
    f->node_arena = (ShapeNode*)malloc(POP_SIZE * MAX_NODES * sizeof(ShapeNode));
    f->reg_arena = (float*)malloc(POP_SIZE * MAX_REGS * sizeof(float));
    f->graphs = (ShapeGraph*)malloc(POP_SIZE * sizeof(ShapeGraph));
    f->fitness = (float*)malloc(POP_SIZE * sizeof(float));

    /* Best tracking */
    f->best_nodes = (ShapeNode*)malloc(MAX_NODES * sizeof(ShapeNode));
    f->best_regs = (float*)malloc(MAX_REGS * sizeof(float));
    f->best_graph.nodes = f->best_nodes;
    f->best_graph.registers = f->best_regs;

    /* Initialize population */
    for (int i = 0; i < POP_SIZE; i++) {
        ShapeGraph* g = &f->graphs[i];
        g->nodes = &f->node_arena[i * MAX_NODES];
        g->registers = &f->reg_arena[i * MAX_REGS];
        shape_graph_init(g, g->nodes, g->registers, MAX_NODES, MAX_REGS);
        build_random_graph(g, &rng);
    }

    printf("  Population: %d\n", POP_SIZE);
    printf("  Hidden neurons: %d\n", HIDDEN_DIM);
    printf("  Nodes per genome: %d\n", f->graphs[0].node_count);
    printf("  Memory per genome: %zu bytes\n", shape_graph_memory(&f->graphs[0]));
    printf("\n");

    /* Evolution loop */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("GENESIS BEGINNING (target: %.0f seconds)\n", TARGET_TIME);
    printf("═══════════════════════════════════════════════════════════════\n\n");

    clock_t start = clock();
    double elapsed = 0.0;
    int elite_indices[ELITE_COUNT];

    for (int gen = 0; gen < MAX_GENS && elapsed < TARGET_TIME; gen++) {
        f->generation = gen;

        /* Evaluate all */
        for (int i = 0; i < POP_SIZE; i++) {
            f->fitness[i] = evaluate_fitness(&f->graphs[i], target, FAST_WINDOW);
            f->total_evals++;
        }

        /* Find elites */
        for (int e = 0; e < ELITE_COUNT; e++) {
            float best = -INFINITY;
            elite_indices[e] = 0;
            for (int i = 0; i < POP_SIZE; i++) {
                int is_elite = 0;
                for (int j = 0; j < e; j++) {
                    if (i == elite_indices[j]) { is_elite = 1; break; }
                }
                if (is_elite) continue;
                if (f->fitness[i] > best) {
                    best = f->fitness[i];
                    elite_indices[e] = i;
                }
            }
        }

        /* Track best ever */
        float gen_best = f->fitness[elite_indices[0]];
        if (gen_best > f->best_fitness) {
            f->best_fitness = gen_best;
            f->best_gen = gen;
            copy_graph(&f->best_graph, &f->graphs[elite_indices[0]]);
        }

        /* Report - detailed telemetry */
        elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
        if (gen % 200 == 0 || gen == MAX_GENS - 1) {
            /* Count how many got killed by variance penalty */
            int dead_count = 0;
            for (int i = 0; i < POP_SIZE; i++) {
                if (f->fitness[i] == 0.0f) dead_count++;
            }

            float full_score = evaluate_fitness(&f->graphs[elite_indices[0]], target, FULL_WINDOW);
            printf("Gen %5d | best=%6.0f (full=%6.0f) | dead=%3d/%d | %.1fs\n",
                   gen, gen_best, full_score, dead_count, POP_SIZE, elapsed);
        }

        /* Selection + Mutation */
        for (int i = 0; i < POP_SIZE; i++) {
            int is_elite = 0;
            for (int e = 0; e < ELITE_COUNT; e++) {
                if (i == elite_indices[e]) { is_elite = 1; break; }
            }
            if (is_elite) continue;

            int winner = tournament_select(f, &rng);
            copy_graph(&f->graphs[i], &f->graphs[winner]);

            int num_mut = 1 + (entro_rng_next(&rng) % 3);
            for (int m = 0; m < num_mut; m++) {
                mutate_genome(&f->graphs[i], &rng);
                f->total_mutations++;
            }
        }
    }

    double total_time = (double)(clock() - start) / CLOCKS_PER_SEC;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("GENESIS COMPLETE\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("Time: %.2f seconds\n", total_time);
    printf("Generations: %d\n", f->generation + 1);
    printf("Generations/sec: %.0f\n", (f->generation + 1) / total_time);
    printf("Total mutations: %llu\n", (unsigned long long)f->total_mutations);
    printf("Mutations/sec: %.2f M\n", (f->total_mutations / total_time) / 1e6);
    printf("\n");

    /* Final full evaluation */
    float final_full = evaluate_fitness(&f->best_graph, target, FULL_WINDOW);
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("SEED STATISTICS\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    printf("Best fitness (fast): %.0f\n", f->best_fitness);
    printf("Best fitness (full): %.0f\n", final_full);
    printf("Found at generation: %d\n", f->best_gen);
    printf("Nodes: %d\n", f->best_graph.node_count);
    printf("Registers: %d\n", f->best_graph.reg_count);
    printf("Memory: %zu bytes\n", shape_graph_memory(&f->best_graph));
    printf("\n");

    /* Export seed */
    char filename[256];
    snprintf(filename, sizeof(filename), "seeds/%s_seed.h", TARGET_NAMES[target]);

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("EXPORTING SEED\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    export_seed(f, filename);
    printf("Saved to: %s\n", filename);
    printf("\n");

    printf("To use:\n");
    printf("  #include \"%s\"\n", filename);
    printf("  float state[%d] = {0};\n", f->best_graph.reg_count);
    printf("  float output = %s_seed_step(input, state);\n", TARGET_NAMES[target]);
    printf("\n");

    /* Cleanup */
    free(f->node_arena);
    free(f->reg_arena);
    free(f->graphs);
    free(f->fitness);
    free(f->best_nodes);
    free(f->best_regs);
    free(f);

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("THE SEED IS READY.\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    return 0;
}
