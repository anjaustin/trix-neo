/*
 * forge.c — The EntroMorphic Forge
 *
 * "Data in, chip out."
 *
 * The Forge is the product layer on top of Genesis. It takes a CSV file
 * containing real-world time-series data and evolves a frozen chip that
 * can track/predict that signal.
 *
 * This is the bridge between "Scientific Curiosity" and "Killer App."
 *
 * Usage:
 *   ./forge train --input data.csv --epochs 5000 --out my_chip.h
 *   ./forge train --input data.csv  # Uses defaults
 *   ./forge test  --input data.csv --chip my_chip.h  # Evaluate chip
 *
 * CSV Format:
 *   Single column of floats (one value per line)
 *   Or two columns: timestamp,value (timestamp ignored)
 *
 * Build:
 *   gcc -O3 -I../include -I../foundry forge.c -o forge -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <getopt.h>

#include "trixc/shapefabric.h"
#include "trixc/entromorph.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * FORGE CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════ */

#define MAX_SIGNAL_LEN      65536   /* Max samples we can load */
#define DEFAULT_EPOCHS      5000
#define DEFAULT_POP_SIZE    256
#define DEFAULT_TARGET_TIME 60.0

/* Evolution parameters */
#define ELITE_COUNT     8
#define TOURNAMENT_K    4
#define MAX_NODES       128
#define MAX_REGS        256
#define HIDDEN_DIM      8

/* Metabolic regularization */
#define MAX_ALLOWED_STATE   5.0f
#define GLUTTONY_PENALTY    100.0f

/* Prediction horizon - forces actual learning, not pass-through */
#define PREDICT_HORIZON 3

/* ═══════════════════════════════════════════════════════════════════════════
 * AUTO-GAIN CONTROL (AGC) — Normalizes any data range to [-1, 1]
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    float mean;
    float std;
    int count;
} AGC_Stats;

void agc_compute_stats(float* data, int len, AGC_Stats* stats) {
    /* Compute mean */
    float sum = 0.0f;
    for (int i = 0; i < len; i++) {
        sum += data[i];
    }
    stats->mean = sum / len;

    /* Compute std */
    float var_sum = 0.0f;
    for (int i = 0; i < len; i++) {
        float diff = data[i] - stats->mean;
        var_sum += diff * diff;
    }
    float variance = var_sum / len;
    stats->std = sqrtf(variance);
    if (stats->std < 0.001f) stats->std = 0.001f;  /* Floor */

    stats->count = len;
}

void agc_normalize(float* data, int len, AGC_Stats* stats) {
    for (int i = 0; i < len; i++) {
        data[i] = (data[i] - stats->mean) / stats->std;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CSV PARSING
 * ═══════════════════════════════════════════════════════════════════════════ */

static float signal_buffer[MAX_SIGNAL_LEN];
static int signal_length = 0;

int load_csv(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s'\n", filename);
        return -1;
    }

    char line[1024];
    signal_length = 0;

    /* Skip header if it looks like text */
    if (fgets(line, sizeof(line), f)) {
        /* Check if first char is a digit, minus, or dot */
        char c = line[0];
        if (c == '-' || c == '.' || (c >= '0' && c <= '9')) {
            /* It's data, parse it */
            float val;
            /* Try two-column format first */
            if (sscanf(line, "%*f,%f", &val) == 1) {
                signal_buffer[signal_length++] = val;
            } else if (sscanf(line, "%f", &val) == 1) {
                signal_buffer[signal_length++] = val;
            }
        }
        /* Otherwise it's a header, skip */
    }

    /* Parse remaining lines */
    while (fgets(line, sizeof(line), f) && signal_length < MAX_SIGNAL_LEN) {
        float val;
        /* Try two-column format first (timestamp,value) */
        if (sscanf(line, "%*f,%f", &val) == 1) {
            signal_buffer[signal_length++] = val;
        } else if (sscanf(line, "%f", &val) == 1) {
            signal_buffer[signal_length++] = val;
        }
    }

    fclose(f);
    return signal_length;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FOUNDRY STRUCTURES
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
    uint32_t generation;
    uint64_t total_mutations;
    uint64_t total_evals;
    int pop_size;
} Forge;

/* ═══════════════════════════════════════════════════════════════════════════
 * FITNESS KERNEL — Phase-shifted tracking with PREDICTION requirement
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline float fitness_tracker(ShapeGraph* g, int window) {
    float fitness = 0.0f;
    memset(g->registers, 0, g->reg_count * sizeof(float));

    if (window > signal_length) window = signal_length;
    if (window < PREDICT_HORIZON + 10) return 0.0f;

    /* Store outputs for correlation analysis */
    float* outputs = (float*)malloc(window * sizeof(float));
    float sum_out = 0.0f, sum_out_sq = 0.0f;
    float max_state_magnitude = 0.0f;

    for (int t = 0; t < window; t++) {
        g->registers[g->input_start] = signal_buffer[t];
        shape_execute_graph(g, 0.1f);

        outputs[t] = g->registers[g->output_start];
        sum_out += outputs[t];
        sum_out_sq += outputs[t] * outputs[t];

        /* Monitor state explosion */
        for (uint32_t r = 0; r < g->reg_count; r++) {
            float val = fabsf(g->registers[r]);
            if (val > max_state_magnitude) {
                max_state_magnitude = val;
            }
        }
    }

    /* Variance check - flat output = death */
    float mean_out = sum_out / window;
    float var_out = (sum_out_sq / window) - (mean_out * mean_out);

    if (var_out < 0.02f) {
        free(outputs);
        return 0.0f;
    }

    /* Anti-identity check - pure pass-through = death */
    float sum_xy = 0.0f, sum_x = 0.0f, sum_y = 0.0f;
    float sum_x2 = 0.0f, sum_y2 = 0.0f;
    for (int t = 0; t < window; t++) {
        float x = outputs[t];
        float y = signal_buffer[t];
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
        free(outputs);
        return 0.0f;
    }

    /* PREDICTION CORRELATION - the real test */
    float pred_sum_xy = 0.0f, pred_sum_x = 0.0f, pred_sum_y = 0.0f;
    float pred_sum_x2 = 0.0f, pred_sum_y2 = 0.0f;
    int pred_count = 0;

    for (int t = 0; t < window - PREDICT_HORIZON; t++) {
        float x = outputs[t];
        float y = signal_buffer[t + PREDICT_HORIZON];

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

    /* Reward prediction correlation */
    if (pred_corr > 0.95f) fitness += window * 8.0f;
    else if (pred_corr > 0.9f) fitness += window * 6.0f;
    else if (pred_corr > 0.8f) fitness += window * 4.0f;
    else if (pred_corr > 0.7f) fitness += window * 2.5f;
    else if (pred_corr > 0.5f) fitness += window * 1.0f;

    /* Base fitness from prediction error */
    for (int t = 0; t < window - PREDICT_HORIZON; t++) {
        float error = fabsf(outputs[t] - signal_buffer[t + PREDICT_HORIZON]);
        if (error < 0.1f) fitness += 10.0f;
        else if (error < 0.2f) fitness += 5.0f;
        else if (error < 0.3f) fitness += 2.0f;
    }

    /* THE GLUTTONY PENALTY */
    float gluttony_cost = 0.0f;
    if (max_state_magnitude > MAX_ALLOWED_STATE) {
        gluttony_cost = (max_state_magnitude - MAX_ALLOWED_STATE) * GLUTTONY_PENALTY;
    }

    free(outputs);

    float final_fitness = fitness - gluttony_cost;
    return (final_fitness > 0.0f) ? final_fitness : 0.0f;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * GRAPH CONSTRUCTION
 * ═══════════════════════════════════════════════════════════════════════════ */

void build_random_graph(ShapeGraph* g, EntroRNG* rng) {
    shape_graph_set_inputs(g, 1);
    shape_graph_set_outputs(g, 1);

    uint32_t state_regs[HIDDEN_DIM];
    for (int h = 0; h < HIDDEN_DIM; h++) {
        state_regs[h] = shape_alloc_reg(g);
    }

    float taus[HIDDEN_DIM];
    for (int h = 0; h < HIDDEN_DIM; h++) {
        taus[h] = entro_rng_range(rng, 0.5f, 4.0f);
    }

    uint32_t input_regs[HIDDEN_DIM];
    for (int h = 0; h < HIDDEN_DIM; h++) {
        input_regs[h] = g->input_start;
    }

    shape_build_cfc_layer(g, input_regs, state_regs, HIDDEN_DIM, taus);

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
 * MUTATION & SELECTION
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline void mutate_genome(ShapeGraph* g, EntroRNG* rng) {
    uint32_t mutation_type = entro_rng_next(rng) % 100;

    if (mutation_type < 40) {
        uint32_t node_idx = entro_rng_next(rng) % g->node_count;
        uint32_t new_input = entro_rng_next(rng) % g->reg_count;
        shape_mutate_rewire(g, node_idx, new_input);
    } else if (mutation_type < 70) {
        uint32_t node_idx = entro_rng_next(rng) % g->node_count;
        float delta = entro_rng_range(rng, -0.3f, 0.3f);
        shape_mutate_value(g, node_idx, delta);
    } else if (mutation_type < 85) {
        uint32_t node_idx = entro_rng_next(rng) % g->node_count;
        ShapeOpcode ops[] = {SHAPE_SOFTSIGN, SHAPE_TANH, SHAPE_RELU, SHAPE_SIGMOID, SHAPE_ADD, SHAPE_MUL};
        shape_mutate_opcode(g, node_idx, ops[entro_rng_next(rng) % 6]);
    } else if (mutation_type < 95) {
        uint32_t node_idx = entro_rng_next(rng) % g->node_count;
        if (g->nodes[node_idx].flags & NODE_FLAG_ACTIVE) {
            shape_mutate_disable(g, node_idx);
        } else {
            shape_mutate_enable(g, node_idx);
        }
    } else {
        for (int m = 0; m < 4; m++) {
            uint32_t node_idx = entro_rng_next(rng) % g->node_count;
            uint32_t new_input = entro_rng_next(rng) % g->reg_count;
            shape_mutate_rewire(g, node_idx, new_input);
        }
    }
}

static inline int tournament_select(Forge* f, EntroRNG* rng) {
    int best_idx = entro_rng_next(rng) % f->pop_size;
    float best_fit = f->fitness[best_idx];

    for (int t = 1; t < TOURNAMENT_K; t++) {
        int idx = entro_rng_next(rng) % f->pop_size;
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
 * SEED EXPORT
 * ═══════════════════════════════════════════════════════════════════════════ */

void export_chip(Forge* f, const char* filename, const char* chip_name,
                 AGC_Stats* agc_stats) {
    FILE* out = fopen(filename, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot write to %s\n", filename);
        return;
    }

    ShapeGraph* g = &f->best_graph;

    fprintf(out, "/*\n");
    fprintf(out, " * %s — EntroMorphic Frozen Chip\n", chip_name);
    fprintf(out, " *\n");
    fprintf(out, " * Generated by The Forge\n");
    fprintf(out, " * Generated: %s", ctime(&(time_t){time(NULL)}));
    fprintf(out, " * Fitness: %.2f\n", f->best_fitness);
    fprintf(out, " * Generation: %d\n", f->best_gen);
    fprintf(out, " * Mutations: %llu\n", (unsigned long long)f->total_mutations);
    fprintf(out, " * Training samples: %d\n", signal_length);
    fprintf(out, " *\n");
    fprintf(out, " * AGC Normalization (baked in):\n");
    fprintf(out, " *   Mean: %.6f\n", agc_stats->mean);
    fprintf(out, " *   Std:  %.6f\n", agc_stats->std);
    fprintf(out, " *\n");
    fprintf(out, " * Usage:\n");
    fprintf(out, " *   #include \"%s\"\n", filename);
    fprintf(out, " *   float state[%s_REGS] = {0};\n", chip_name);
    fprintf(out, " *   float output = %s_step(raw_input, state);\n", chip_name);
    fprintf(out, " */\n\n");

    fprintf(out, "#ifndef %s_H\n", chip_name);
    fprintf(out, "#define %s_H\n\n", chip_name);

    fprintf(out, "#include <stdint.h>\n");
    fprintf(out, "#include <string.h>\n");
    fprintf(out, "#include <math.h>\n\n");

    /* Metadata */
    fprintf(out, "/* Chip Metadata */\n");
    fprintf(out, "#define %s_NODES %u\n", chip_name, g->node_count);
    fprintf(out, "#define %s_REGS %u\n", chip_name, g->reg_count);
    fprintf(out, "#define %s_FITNESS %.2ff\n", chip_name, f->best_fitness);
    fprintf(out, "#define %s_GEN %d\n\n", chip_name, f->best_gen);

    /* AGC constants */
    fprintf(out, "/* AGC Normalization Constants */\n");
    fprintf(out, "#define %s_AGC_MEAN %.10ff\n", chip_name, agc_stats->mean);
    fprintf(out, "#define %s_AGC_STD  %.10ff\n\n", chip_name, agc_stats->std);

    /* Node data */
    fprintf(out, "/* Frozen Node Data */\n");
    fprintf(out, "static const uint8_t %s_opcodes[%u] = {\n    ",
            chip_name, g->node_count);
    for (uint32_t i = 0; i < g->node_count; i++) {
        fprintf(out, "%u", g->nodes[i].opcode);
        if (i < g->node_count - 1) fprintf(out, ", ");
        if ((i + 1) % 16 == 0 && i < g->node_count - 1) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "static const uint8_t %s_flags[%u] = {\n    ",
            chip_name, g->node_count);
    for (uint32_t i = 0; i < g->node_count; i++) {
        fprintf(out, "%u", g->nodes[i].flags);
        if (i < g->node_count - 1) fprintf(out, ", ");
        if ((i + 1) % 16 == 0 && i < g->node_count - 1) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "static const uint16_t %s_out_idx[%u] = {\n    ",
            chip_name, g->node_count);
    for (uint32_t i = 0; i < g->node_count; i++) {
        fprintf(out, "%u", g->nodes[i].out_idx);
        if (i < g->node_count - 1) fprintf(out, ", ");
        if ((i + 1) % 16 == 0 && i < g->node_count - 1) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "static const uint32_t %s_in_a[%u] = {\n    ",
            chip_name, g->node_count);
    for (uint32_t i = 0; i < g->node_count; i++) {
        fprintf(out, "%u", g->nodes[i].in_a_idx);
        if (i < g->node_count - 1) fprintf(out, ", ");
        if ((i + 1) % 8 == 0 && i < g->node_count - 1) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "static const uint32_t %s_in_b[%u] = {\n    ",
            chip_name, g->node_count);
    for (uint32_t i = 0; i < g->node_count; i++) {
        fprintf(out, "%u", g->nodes[i].in_b_idx);
        if (i < g->node_count - 1) fprintf(out, ", ");
        if ((i + 1) % 8 == 0 && i < g->node_count - 1) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "static const float %s_values[%u] = {\n    ",
            chip_name, g->node_count);
    for (uint32_t i = 0; i < g->node_count; i++) {
        fprintf(out, "%.6ff", g->nodes[i].value);
        if (i < g->node_count - 1) fprintf(out, ", ");
        if ((i + 1) % 6 == 0 && i < g->node_count - 1) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n\n");

    /* Internal step function (normalized input) */
    fprintf(out, "/* Internal: Execute one step with normalized input */\n");
    fprintf(out, "static inline float %s_step_internal(float input, float* state) {\n", chip_name);
    fprintf(out, "    float regs[%u];\n", g->reg_count);
    fprintf(out, "    memcpy(regs, state, %u * sizeof(float));\n", g->reg_count);
    fprintf(out, "    regs[%u] = input;\n\n", g->input_start);

    fprintf(out, "    for (int i = 0; i < %u; i++) {\n", g->node_count);
    fprintf(out, "        if (!(%s_flags[i] & 0x01)) continue;\n", chip_name);
    fprintf(out, "        float a = regs[%s_in_a[i]];\n", chip_name);
    fprintf(out, "        float b = regs[%s_in_b[i]];\n", chip_name);
    fprintf(out, "        float v = %s_values[i];\n", chip_name);
    fprintf(out, "        float result;\n");
    fprintf(out, "        switch (%s_opcodes[i]) {\n", chip_name);
    fprintf(out, "            case 8:  result = a + b; break;\n");
    fprintf(out, "            case 9:  result = a - b; break;\n");
    fprintf(out, "            case 10: result = a * b; break;\n");
    fprintf(out, "            case 11: result = b != 0 ? a/b : 0; break;\n");
    fprintf(out, "            case 13: result = fabsf(a); break;\n");
    fprintf(out, "            case 14: result = a > b ? a : b; break;\n");
    fprintf(out, "            case 16: result = expf(a); break;\n");
    fprintf(out, "            case 20: result = 1.0f/(1.0f+expf(-a)); break;\n");
    fprintf(out, "            case 21: result = tanhf(a); break;\n");
    fprintf(out, "            case 22: result = a > 0 ? a : 0; break;\n");
    fprintf(out, "            case 23: result = a/(1.0f+fabsf(a)); break;\n");
    fprintf(out, "            case 24: result = v; break;\n");
    fprintf(out, "            case 27: result = a * expf(-0.1f/v); break;\n");
    fprintf(out, "            default: result = 0; break;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        regs[%s_out_idx[i]] = result;\n", chip_name);
    fprintf(out, "    }\n\n");

    fprintf(out, "    memcpy(state, regs, %u * sizeof(float));\n", g->reg_count);
    fprintf(out, "    return regs[%u];\n", g->output_start);
    fprintf(out, "}\n\n");

    /* Public step function (with AGC baked in) */
    fprintf(out, "/* Public: Execute one step with raw input (AGC applied automatically) */\n");
    fprintf(out, "static inline float %s_step(float raw_input, float* state) {\n", chip_name);
    fprintf(out, "    float normalized = (raw_input - %s_AGC_MEAN) / %s_AGC_STD;\n",
            chip_name, chip_name);
    fprintf(out, "    return %s_step_internal(normalized, state);\n", chip_name);
    fprintf(out, "}\n\n");

    /* Reset function */
    fprintf(out, "/* Reset chip state */\n");
    fprintf(out, "static inline void %s_reset(float* state) {\n", chip_name);
    fprintf(out, "    memset(state, 0, %u * sizeof(float));\n", g->reg_count);
    fprintf(out, "}\n\n");

    fprintf(out, "#endif /* %s_H */\n", chip_name);

    fclose(out);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN — THE FORGE
 * ═══════════════════════════════════════════════════════════════════════════ */

void print_usage(const char* prog) {
    printf("\n");
    printf("THE FORGE — EntroMorphic Chip Compiler\n");
    printf("\"Data in, chip out.\"\n\n");
    printf("Usage:\n");
    printf("  %s train --input <file.csv> [options]\n", prog);
    printf("  %s test  --input <file.csv> --chip <chip.h>\n\n", prog);
    printf("Options:\n");
    printf("  --input, -i   Input CSV file (required)\n");
    printf("  --out, -o     Output header file (default: chip.h)\n");
    printf("  --name, -n    Chip name (default: derived from output)\n");
    printf("  --epochs, -e  Max generations (default: %d)\n", DEFAULT_EPOCHS);
    printf("  --time, -t    Target time in seconds (default: %.0f)\n", DEFAULT_TARGET_TIME);
    printf("  --pop, -p     Population size (default: %d)\n", DEFAULT_POP_SIZE);
    printf("  --chip, -c    Chip header for testing\n");
    printf("  --help, -h    Show this help\n\n");
    printf("CSV Format:\n");
    printf("  Single column: one value per line\n");
    printf("  Two columns: timestamp,value (timestamp ignored)\n\n");
    printf("Examples:\n");
    printf("  %s train -i motor_vibration.csv -o motor_chip.h\n", prog);
    printf("  %s train -i cpu_load.csv -e 10000 -t 120\n", prog);
    printf("\n");
}

int main(int argc, char** argv) {
    /* Defaults */
    const char* input_file = NULL;
    const char* output_file = "chip.h";
    const char* chip_name = NULL;
    const char* test_chip = NULL;
    int max_epochs = DEFAULT_EPOCHS;
    double target_time = DEFAULT_TARGET_TIME;
    int pop_size = DEFAULT_POP_SIZE;
    int mode_train = 1;  /* 1 = train, 0 = test */

    /* Parse arguments */
    static struct option long_options[] = {
        {"input",  required_argument, 0, 'i'},
        {"out",    required_argument, 0, 'o'},
        {"name",   required_argument, 0, 'n'},
        {"epochs", required_argument, 0, 'e'},
        {"time",   required_argument, 0, 't'},
        {"pop",    required_argument, 0, 'p'},
        {"chip",   required_argument, 0, 'c'},
        {"help",   no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "i:o:n:e:t:p:c:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i': input_file = optarg; break;
            case 'o': output_file = optarg; break;
            case 'n': chip_name = optarg; break;
            case 'e': max_epochs = atoi(optarg); break;
            case 't': target_time = atof(optarg); break;
            case 'p': pop_size = atoi(optarg); break;
            case 'c': test_chip = optarg; mode_train = 0; break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    /* Check for command (train/test) */
    if (optind < argc) {
        if (strcmp(argv[optind], "train") == 0) {
            mode_train = 1;
        } else if (strcmp(argv[optind], "test") == 0) {
            mode_train = 0;
        }
    }

    if (!input_file) {
        fprintf(stderr, "Error: Input file required (--input)\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Derive chip name from output file if not specified */
    if (!chip_name) {
        static char name_buf[256];
        const char* base = strrchr(output_file, '/');
        base = base ? base + 1 : output_file;
        strncpy(name_buf, base, sizeof(name_buf) - 1);
        char* dot = strrchr(name_buf, '.');
        if (dot) *dot = '\0';
        /* Convert to uppercase and replace invalid chars */
        for (int i = 0; name_buf[i]; i++) {
            if (name_buf[i] >= 'a' && name_buf[i] <= 'z') {
                name_buf[i] -= 32;
            } else if (name_buf[i] == '-' || name_buf[i] == '.') {
                name_buf[i] = '_';
            }
        }
        chip_name = name_buf;
    }

    /* ═══════════════════════════════════════════════════════════════════════
     * LOAD DATA
     * ═══════════════════════════════════════════════════════════════════════ */

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  THE FORGE — EntroMorphic Chip Compiler                      ║\n");
    printf("║  \"Data in, chip out.\"                                        ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    printf("Loading data from: %s\n", input_file);
    int samples = load_csv(input_file);
    if (samples < 0) {
        return 1;
    }
    printf("  Loaded %d samples\n", samples);

    if (samples < 100) {
        fprintf(stderr, "Error: Need at least 100 samples (got %d)\n", samples);
        return 1;
    }

    /* Compute AGC stats on raw data */
    AGC_Stats agc_stats;
    agc_compute_stats(signal_buffer, signal_length, &agc_stats);
    printf("  Raw data range: mean=%.4f, std=%.4f\n", agc_stats.mean, agc_stats.std);

    /* Normalize for training */
    agc_normalize(signal_buffer, signal_length, &agc_stats);
    printf("  Normalized to: mean≈0, std≈1\n\n");

    if (!mode_train) {
        printf("Test mode not yet implemented.\n");
        printf("(Would evaluate chip against data)\n");
        return 0;
    }

    /* ═══════════════════════════════════════════════════════════════════════
     * EVOLUTION
     * ═══════════════════════════════════════════════════════════════════════ */

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("EVOLUTION PARAMETERS\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    printf("  Population: %d\n", pop_size);
    printf("  Max epochs: %d\n", max_epochs);
    printf("  Target time: %.0f seconds\n", target_time);
    printf("  Hidden neurons: %d\n", HIDDEN_DIM);
    printf("  Output: %s (%s)\n", output_file, chip_name);
    printf("\n");

    /* Initialize RNG */
    EntroRNG rng;
    entro_rng_seed(&rng, (uint64_t)time(NULL));

    /* Allocate forge */
    Forge* f = (Forge*)malloc(sizeof(Forge));
    memset(f, 0, sizeof(Forge));
    f->best_fitness = -INFINITY;
    f->pop_size = pop_size;

    /* Allocate arenas */
    f->node_arena = (ShapeNode*)malloc(pop_size * MAX_NODES * sizeof(ShapeNode));
    f->reg_arena = (float*)malloc(pop_size * MAX_REGS * sizeof(float));
    f->graphs = (ShapeGraph*)malloc(pop_size * sizeof(ShapeGraph));
    f->fitness = (float*)malloc(pop_size * sizeof(float));

    /* Best tracking */
    f->best_nodes = (ShapeNode*)malloc(MAX_NODES * sizeof(ShapeNode));
    f->best_regs = (float*)malloc(MAX_REGS * sizeof(float));
    f->best_graph.nodes = f->best_nodes;
    f->best_graph.registers = f->best_regs;

    /* Initialize population */
    for (int i = 0; i < pop_size; i++) {
        ShapeGraph* g = &f->graphs[i];
        g->nodes = &f->node_arena[i * MAX_NODES];
        g->registers = &f->reg_arena[i * MAX_REGS];
        shape_graph_init(g, g->nodes, g->registers, MAX_NODES, MAX_REGS);
        build_random_graph(g, &rng);
    }

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("FORGING CHIP (target: %.0f seconds)\n", target_time);
    printf("═══════════════════════════════════════════════════════════════\n\n");

    /* Evaluation window */
    int fast_window = signal_length / 4;  /* Use 25% for fast eval */
    if (fast_window < 256) fast_window = 256;
    if (fast_window > signal_length) fast_window = signal_length;

    int full_window = signal_length;

    clock_t start = clock();
    double elapsed = 0.0;
    int elite_indices[ELITE_COUNT];

    for (int gen = 0; gen < max_epochs && elapsed < target_time; gen++) {
        f->generation = gen;

        /* Evaluate all */
        for (int i = 0; i < pop_size; i++) {
            f->fitness[i] = fitness_tracker(&f->graphs[i], fast_window);
            f->total_evals++;
        }

        /* Find elites */
        for (int e = 0; e < ELITE_COUNT; e++) {
            float best = -INFINITY;
            elite_indices[e] = 0;
            for (int i = 0; i < pop_size; i++) {
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

        /* Report */
        elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
        if (gen % 200 == 0 || gen == max_epochs - 1) {
            int dead_count = 0;
            for (int i = 0; i < pop_size; i++) {
                if (f->fitness[i] == 0.0f) dead_count++;
            }

            float full_score = fitness_tracker(&f->graphs[elite_indices[0]], full_window);
            printf("Gen %5d | best=%6.0f (full=%6.0f) | dead=%3d/%d | %.1fs\n",
                   gen, gen_best, full_score, dead_count, pop_size, elapsed);
        }

        /* Selection + Mutation */
        for (int i = 0; i < pop_size; i++) {
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
    printf("FORGING COMPLETE\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("Time: %.2f seconds\n", total_time);
    printf("Generations: %d\n", f->generation + 1);
    printf("Generations/sec: %.0f\n", (f->generation + 1) / total_time);
    printf("Total mutations: %llu\n", (unsigned long long)f->total_mutations);
    printf("Mutations/sec: %.2f M\n", (f->total_mutations / total_time) / 1e6);
    printf("\n");

    /* Final full evaluation */
    float final_full = fitness_tracker(&f->best_graph, full_window);
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("CHIP STATISTICS\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    printf("Best fitness (fast): %.0f\n", f->best_fitness);
    printf("Best fitness (full): %.0f\n", final_full);
    printf("Found at generation: %d\n", f->best_gen);
    printf("Nodes: %d\n", f->best_graph.node_count);
    printf("Registers: %d\n", f->best_graph.reg_count);
    printf("Memory: %zu bytes\n", shape_graph_memory(&f->best_graph));
    printf("\n");

    /* Export chip */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("EXPORTING CHIP\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    export_chip(f, output_file, chip_name, &agc_stats);
    printf("Saved to: %s\n\n", output_file);

    printf("To use:\n");
    printf("  #include \"%s\"\n", output_file);
    printf("  float state[%s_REGS] = {0};\n", chip_name);
    printf("  float output = %s_step(raw_input, state);\n\n", chip_name);

    printf("Note: AGC normalization is baked into %s_step().\n", chip_name);
    printf("      Pass raw sensor values directly.\n\n");

    /* Cleanup */
    free(f->node_arena);
    free(f->reg_arena);
    free(f->graphs);
    free(f->fitness);
    free(f->best_nodes);
    free(f->best_regs);
    free(f);

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("THE CHIP IS FORGED.\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    return 0;
}
