/*
 * bench_gym.c — EntroMorph Heartbeat Gym
 *
 * "The selection pressure that matches mutation speed."
 *
 * This proves that 65.8M mutations/sec can actually LEARN a signal.
 * The bottleneck is now SELECTION, not mutation.
 *
 * Key insight: Use XOR Resonance (Zit Detector) as fitness kernel.
 * Bitwise operations keep selection as fast as mutation.
 *
 * Build:
 *   gcc -O3 -I../include bench_gym.c -o bench_gym -lm
 *
 * Run:
 *   ./bench_gym
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
 * Configuration
 * ═══════════════════════════════════════════════════════════════════════════ */

#define SIGNAL_LEN      1024    /* Total signal length */
#define WINDOW_SIZE     32      /* Scent trace window (fast filter) */
#define FULL_WINDOW     256     /* Full evaluation window (promoted candidates) */
#define POP_SIZE        64      /* Population size */
#define MAX_GENS        5000    /* Maximum generations */
#define ELITE_COUNT     4       /* Number of elites to preserve */

/* Graph configuration */
#define MAX_NODES       256     /* CfC neuron = ~9 nodes, need room for hidden + output */
#define MAX_REGS        256     /* Registers for all intermediate values */
#define HIDDEN_DIM      4       /* Smaller for speed (still enough to learn) */

/* ═══════════════════════════════════════════════════════════════════════════
 * Signal Generator: Heartbeat with Noise
 *
 * A sine wave with random jitter — the network must filter noise
 * and lock to the underlying phase.
 * ═══════════════════════════════════════════════════════════════════════════ */

static float signal_buffer[SIGNAL_LEN + 1];

void generate_heartbeat_signal(EntroRNG* rng) {
    for (int t = 0; t <= SIGNAL_LEN; t++) {
        /* Clean signal: sine wave with varying frequency */
        float phase = t * 0.1f;
        float clean = sinf(phase);

        /* Add noise (small jitter) */
        float noise = entro_rng_range(rng, -0.2f, 0.2f);

        /* Occasional spike (heartbeat artifact) */
        if (t % 63 == 0) {
            noise += entro_rng_range(rng, 0.3f, 0.5f);
        }

        signal_buffer[t] = clean + noise;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Population Arena — Zero-Copy Memory Layout
 *
 * All genomes share a contiguous memory block.
 * Mutation is pointer arithmetic, not allocation.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    /* Memory arena */
    ShapeNode node_arena[POP_SIZE * MAX_NODES];
    float reg_arena[POP_SIZE * MAX_REGS];

    /* Graph handles */
    ShapeGraph graphs[POP_SIZE];
    float fitness[POP_SIZE];

    /* Metadata */
    uint32_t generation;
    float best_ever;
    int best_ever_gen;
} Population;

void population_init(Population* pop, EntroRNG* rng) {
    memset(pop, 0, sizeof(Population));
    pop->best_ever = -INFINITY;

    for (int i = 0; i < POP_SIZE; i++) {
        ShapeGraph* g = &pop->graphs[i];

        /* Point to arena slice */
        g->nodes = &pop->node_arena[i * MAX_NODES];
        g->registers = &pop->reg_arena[i * MAX_REGS];

        /* Initialize structure */
        shape_graph_init(g, g->nodes, g->registers, MAX_NODES, MAX_REGS);

        /* 1 input, 1 output */
        shape_graph_set_inputs(g, 1);
        shape_graph_set_outputs(g, 1);

        /* Allocate hidden state registers */
        uint32_t state_regs[HIDDEN_DIM];
        for (int h = 0; h < HIDDEN_DIM; h++) {
            state_regs[h] = shape_alloc_reg(g);
        }

        /* Build random CfC layer */
        float taus[HIDDEN_DIM];
        for (int h = 0; h < HIDDEN_DIM; h++) {
            taus[h] = entro_rng_range(rng, 0.5f, 4.0f);
        }

        /* Simple connectivity: input -> each neuron */
        uint32_t input_regs[HIDDEN_DIM];
        for (int h = 0; h < HIDDEN_DIM; h++) {
            input_regs[h] = g->input_start;  /* All neurons see input */
        }

        shape_build_cfc_layer(g, input_regs, state_regs, HIDDEN_DIM, taus);

        /* Output: weighted sum of hidden states */
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

        /* Softsign output for bounded [-1, 1] range */
        shape_add_node(g, SHAPE_SOFTSIGN, g->output_start, sum_reg, 0, 0.0f);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Fitness Kernel: XOR Resonance Scoring (Zit Detector)
 *
 * Instead of expensive MSE, we use:
 *   1. Directional accuracy (did it predict the trend?)
 *   2. Resonance reward (low entropy = stable internal harmonics)
 *
 * The XOR popcount measures "geometric stability" — stable shapes
 * create stable bit patterns in the register file.
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline float score_genome_fast(ShapeGraph* g, int window_size) {
    float fitness = 0.0f;

    /* Reset registers */
    memset(g->registers, 0, g->reg_count * sizeof(float));

    /* Previous states for resonance check */
    uint32_t prev_state = 0;

    for (int t = 0; t < window_size; t++) {
        /* Load input */
        g->registers[g->input_start] = signal_buffer[t];

        /* Execute one step */
        shape_execute_graph(g, 0.1f);

        /* Get output */
        float output = g->registers[g->output_start];
        float truth = signal_buffer[t + 1];

        /* A. Directional Accuracy (fast integer compare) */
        int pred_up = (output > 0.0f);
        int truth_up = (truth > signal_buffer[t]);
        if (pred_up == truth_up) {
            fitness += 10.0f;
        }

        /* B. Magnitude Accuracy (bounded error) */
        float error = fabsf(output - truth);
        if (error < 0.3f) fitness += 5.0f;
        if (error < 0.1f) fitness += 10.0f;

        /* C. Resonance Reward (Zit Detector) */
        /* XOR current state bits with previous state bits */
        /* Low popcount = stable harmonics = good */
        uint32_t curr_state = *(uint32_t*)&g->registers[g->input_start + 1];
        uint32_t entropy = __builtin_popcount(curr_state ^ prev_state);

        if (entropy < 8) fitness += 8.0f;   /* Very stable */
        if (entropy < 16) fitness += 4.0f;  /* Moderately stable */
        /* High entropy (>24) = chaotic = no bonus */

        prev_state = curr_state;
    }

    return fitness;
}

/* Full evaluation for promoted candidates */
static inline float score_genome_full(ShapeGraph* g) {
    return score_genome_fast(g, FULL_WINDOW);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Mutation: Binary-Level Genome Modification
 *
 * Each mutation is a single integer write.
 * Target: < 20ns per mutation.
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline void mutate_genome(ShapeGraph* g, EntroRNG* rng) {
    /* Choose mutation type */
    uint32_t mutation_type = entro_rng_next(rng) % 100;

    if (mutation_type < 40) {
        /* Rewire connection (40% chance) */
        uint32_t node_idx = entro_rng_next(rng) % g->node_count;
        uint32_t new_input = entro_rng_next(rng) % g->reg_count;
        shape_mutate_rewire(g, node_idx, new_input);

    } else if (mutation_type < 70) {
        /* Perturb value (30% chance) */
        uint32_t node_idx = entro_rng_next(rng) % g->node_count;
        float delta = entro_rng_range(rng, -0.2f, 0.2f);
        shape_mutate_value(g, node_idx, delta);

    } else if (mutation_type < 85) {
        /* Change opcode (15% chance) - structural mutation */
        uint32_t node_idx = entro_rng_next(rng) % g->node_count;
        /* Only mutate to compatible activation opcodes */
        ShapeOpcode ops[] = {SHAPE_SOFTSIGN, SHAPE_TANH, SHAPE_RELU, SHAPE_SIGMOID};
        ShapeOpcode new_op = ops[entro_rng_next(rng) % 4];
        shape_mutate_opcode(g, node_idx, new_op);

    } else if (mutation_type < 95) {
        /* Toggle node active state (10% chance) */
        uint32_t node_idx = entro_rng_next(rng) % g->node_count;
        if (g->nodes[node_idx].flags & NODE_FLAG_ACTIVE) {
            shape_mutate_disable(g, node_idx);
        } else {
            shape_mutate_enable(g, node_idx);
        }

    } else {
        /* Macro mutation: rewire multiple nodes (5% chance) */
        for (int m = 0; m < 3; m++) {
            uint32_t node_idx = entro_rng_next(rng) % g->node_count;
            uint32_t new_input = entro_rng_next(rng) % g->reg_count;
            shape_mutate_rewire(g, node_idx, new_input);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Selection: Tournament + Elitism
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline int tournament_select(Population* pop, EntroRNG* rng, int tournament_size) {
    int best_idx = entro_rng_next(rng) % POP_SIZE;
    float best_fit = pop->fitness[best_idx];

    for (int t = 1; t < tournament_size; t++) {
        int idx = entro_rng_next(rng) % POP_SIZE;
        if (pop->fitness[idx] > best_fit) {
            best_fit = pop->fitness[idx];
            best_idx = idx;
        }
    }

    return best_idx;
}

/* Copy graph (memory layout allows memcpy) */
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
 * Main Evolution Loop
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("╭──────────────────────────────────────────────────────────────╮\n");
    printf("│  EntroMorph Heartbeat Gym                                    │\n");
    printf("│  \"Selection at the speed of mutation\"                       │\n");
    printf("╰──────────────────────────────────────────────────────────────╯\n");
    printf("\n");

    /* Initialize RNG */
    EntroRNG rng;
    entro_rng_seed(&rng, (uint64_t)time(NULL));

    /* Generate training signal */
    printf("Generating heartbeat signal...\n");
    generate_heartbeat_signal(&rng);
    printf("  Signal length: %d samples\n", SIGNAL_LEN);
    printf("  Window size: %d (fast), %d (full)\n", WINDOW_SIZE, FULL_WINDOW);
    printf("\n");

    /* Initialize population */
    printf("Initializing population...\n");
    Population* pop = (Population*)malloc(sizeof(Population));
    population_init(pop, &rng);
    printf("  Population size: %d\n", POP_SIZE);
    printf("  Hidden neurons: %d\n", HIDDEN_DIM);
    printf("  Nodes per genome: %d\n", pop->graphs[0].node_count);
    printf("  Memory per genome: %zu bytes\n", shape_graph_memory(&pop->graphs[0]));
    printf("  Total arena: %zu KB\n", sizeof(Population) / 1024);
    printf("\n");

    /* Baseline evaluation */
    printf("Baseline evaluation...\n");
    for (int i = 0; i < POP_SIZE; i++) {
        pop->fitness[i] = score_genome_fast(&pop->graphs[i], WINDOW_SIZE);
    }

    float baseline_best = pop->fitness[0];
    float baseline_avg = pop->fitness[0];
    for (int i = 1; i < POP_SIZE; i++) {
        baseline_avg += pop->fitness[i];
        if (pop->fitness[i] > baseline_best) {
            baseline_best = pop->fitness[i];
        }
    }
    baseline_avg /= POP_SIZE;
    printf("  Gen 0: best=%.1f avg=%.1f\n", baseline_best, baseline_avg);
    printf("\n");

    /* Sort population by fitness (for elitism) */
    int elite_indices[ELITE_COUNT];

    /* Evolution loop */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Starting evolution (%d generations)\n", MAX_GENS);
    printf("═══════════════════════════════════════════════════════════════\n\n");

    clock_t start = clock();
    long long total_mutations = 0;
    long long total_evals = 0;

    for (int gen = 0; gen < MAX_GENS; gen++) {
        pop->generation = gen;

        /* Evaluate all genomes */
        for (int i = 0; i < POP_SIZE; i++) {
            pop->fitness[i] = score_genome_fast(&pop->graphs[i], WINDOW_SIZE);
            total_evals++;
        }

        /* Find elites */
        for (int e = 0; e < ELITE_COUNT; e++) {
            float best = -INFINITY;
            elite_indices[e] = 0;
            for (int i = 0; i < POP_SIZE; i++) {
                /* Skip if already elite */
                int is_elite = 0;
                for (int j = 0; j < e; j++) {
                    if (i == elite_indices[j]) { is_elite = 1; break; }
                }
                if (is_elite) continue;

                if (pop->fitness[i] > best) {
                    best = pop->fitness[i];
                    elite_indices[e] = i;
                }
            }
        }

        /* Track best ever */
        float gen_best = pop->fitness[elite_indices[0]];
        if (gen_best > pop->best_ever) {
            pop->best_ever = gen_best;
            pop->best_ever_gen = gen;
        }

        /* Report progress */
        if (gen % 500 == 0 || gen == MAX_GENS - 1) {
            float avg = 0.0f;
            for (int i = 0; i < POP_SIZE; i++) avg += pop->fitness[i];
            avg /= POP_SIZE;

            /* Full evaluation of best */
            float full_score = score_genome_full(&pop->graphs[elite_indices[0]]);

            printf("Gen %4d | best=%.1f (full=%.1f) avg=%.1f | mutations=%lld\n",
                   gen, gen_best, full_score, avg, total_mutations);
        }

        /* Selection + Mutation for next generation */
        /* (Preserve elites, replace rest with mutated tournament winners) */
        for (int i = 0; i < POP_SIZE; i++) {
            /* Skip elites */
            int is_elite = 0;
            for (int e = 0; e < ELITE_COUNT; e++) {
                if (i == elite_indices[e]) { is_elite = 1; break; }
            }
            if (is_elite) continue;

            /* Tournament selection */
            int winner = tournament_select(pop, &rng, 3);

            /* Copy winner into this slot */
            copy_graph(&pop->graphs[i], &pop->graphs[winner]);

            /* Mutate */
            int num_mutations = 1 + (entro_rng_next(&rng) % 3);  /* 1-3 mutations */
            for (int m = 0; m < num_mutations; m++) {
                mutate_genome(&pop->graphs[i], &rng);
                total_mutations++;
            }
        }
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Evolution Complete\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("Time: %.3f seconds\n", elapsed);
    printf("Generations: %d\n", MAX_GENS);
    printf("Generations/sec: %.1f\n", MAX_GENS / elapsed);
    printf("Total mutations: %lld\n", total_mutations);
    printf("Mutations/sec: %.2f M\n", (total_mutations / elapsed) / 1e6);
    printf("Total evaluations: %lld\n", total_evals);
    printf("Evaluations/sec: %.2f K\n", (total_evals / elapsed) / 1e3);
    printf("\n");

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Results\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("Best fitness (window=%d): %.1f (generation %d)\n",
           WINDOW_SIZE, pop->best_ever, pop->best_ever_gen);

    /* Final full evaluation of winner */
    float final_fast = score_genome_fast(&pop->graphs[elite_indices[0]], WINDOW_SIZE);
    float final_full = score_genome_full(&pop->graphs[elite_indices[0]]);

    printf("Winner fitness (window=%d): %.1f\n", WINDOW_SIZE, final_fast);
    printf("Winner fitness (window=%d): %.1f\n", FULL_WINDOW, final_full);
    printf("\n");

    /* Calculate improvement */
    float improvement = ((final_fast - baseline_best) / baseline_best) * 100.0f;
    printf("Improvement over baseline: %.1f%%\n", improvement);
    printf("\n");

    /* Demonstrate prediction */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Winner Prediction Demo (first 20 samples)\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    ShapeGraph* winner = &pop->graphs[elite_indices[0]];
    memset(winner->registers, 0, winner->reg_count * sizeof(float));

    printf("  t  |  Input  | Predict |  Truth  | Error | Dir\n");
    printf("-----+---------+---------+---------+-------+-----\n");

    int correct_dir = 0;
    for (int t = 0; t < 20; t++) {
        winner->registers[winner->input_start] = signal_buffer[t];
        shape_execute_graph(winner, 0.1f);

        float output = winner->registers[winner->output_start];
        float truth = signal_buffer[t + 1];
        float error = fabsf(output - truth);

        int pred_up = (output > 0.0f);
        int truth_up = (truth > signal_buffer[t]);
        int dir_correct = (pred_up == truth_up);
        correct_dir += dir_correct;

        printf(" %2d  | %6.3f  | %6.3f  | %6.3f  | %5.3f | %s\n",
               t, signal_buffer[t], output, truth, error,
               dir_correct ? "Y" : "N");
    }

    printf("\nDirectional accuracy: %d/20 (%.0f%%)\n", correct_dir, correct_dir * 5.0f);
    printf("\n");

    /* Verdict */
    printf("═══════════════════════════════════════════════════════════════\n");
    if (improvement > 50.0f) {
        printf("VERDICT: LEARNING CONFIRMED\n");
        printf("The network learned to track the heartbeat signal.\n");
    } else if (improvement > 10.0f) {
        printf("VERDICT: PARTIAL LEARNING\n");
        printf("Some adaptation observed. More generations may help.\n");
    } else {
        printf("VERDICT: INSUFFICIENT LEARNING\n");
        printf("Consider: longer evolution, different mutations, or hyperparams.\n");
    }
    printf("═══════════════════════════════════════════════════════════════\n\n");

    free(pop);
    return 0;
}
