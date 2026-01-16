/*
 * bench_evolution.c — EntroMorph Performance Benchmark
 *
 * "How many generations per second can we evolve?"
 *
 * This benchmark measures:
 *   1. Graph execution speed (nanoseconds per step)
 *   2. Mutation speed (rewires per second)
 *   3. Full evolution throughput (generations per second)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "../include/trixc/shapefabric.h"
#include "../include/trixc/entromorph.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Timing Utilities
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Benchmark 1: ShapeGraph Execution Speed
 * ═══════════════════════════════════════════════════════════════════════════ */

static void bench_graph_execution(void) {
    printf("\n═══════════════════════════════════════════════════════════════\n");
    printf("Benchmark: ShapeGraph Execution\n");
    printf("═══════════════════════════════════════════════════════════════\n");

    /* Allocate graph */
    #define MAX_NODES 256
    #define MAX_REGS 128
    ShapeNode nodes[MAX_NODES];
    float regs[MAX_REGS];
    ShapeGraph graph;

    shape_graph_init(&graph, nodes, regs, MAX_NODES, MAX_REGS);

    /* Build a small CfC-like graph: 4 inputs, 8 hidden, 2 outputs */
    shape_graph_set_inputs(&graph, 4);

    /* Allocate state registers */
    uint32_t state_regs[8];
    for (int i = 0; i < 8; i++) {
        state_regs[i] = shape_alloc_reg(&graph);
    }

    /* Build 8 CfC neurons */
    float taus[8] = {0.5f, 1.0f, 1.5f, 2.0f, 0.5f, 1.0f, 1.5f, 2.0f};
    uint32_t input_regs[8] = {0, 1, 2, 3, 0, 1, 2, 3};
    shape_build_cfc_layer(&graph, input_regs, state_regs, 8, taus);

    /* Output layer: sum of states */
    shape_graph_set_outputs(&graph, 2);
    shape_add_node(&graph, SHAPE_ADD, graph.output_start,
                   state_regs[0], state_regs[1], 0.0f);
    shape_add_node(&graph, SHAPE_ADD, graph.output_start + 1,
                   state_regs[4], state_regs[5], 0.0f);

    printf("Graph: %u nodes, %u registers\n", graph.node_count, graph.reg_count);
    printf("Memory: %zu bytes\n", shape_graph_memory(&graph));

    /* Warm up */
    for (int i = 0; i < 1000; i++) {
        graph.registers[0] = sinf(i * 0.1f);
        graph.registers[1] = cosf(i * 0.1f);
        graph.registers[2] = sinf(i * 0.2f);
        graph.registers[3] = cosf(i * 0.2f);
        shape_execute_graph(&graph, 0.1f);
    }

    /* Benchmark */
    const int ITERATIONS = 1000000;
    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        graph.registers[0] = sinf(i * 0.01f);
        graph.registers[1] = cosf(i * 0.01f);
        graph.registers[2] = sinf(i * 0.02f);
        graph.registers[3] = cosf(i * 0.02f);
        shape_execute_graph(&graph, 0.1f);
    }

    double elapsed = get_time_sec() - start;
    double ns_per_step = (elapsed / ITERATIONS) * 1e9;
    double steps_per_sec = ITERATIONS / elapsed;

    printf("\nResults:\n");
    printf("  %d iterations in %.3f seconds\n", ITERATIONS, elapsed);
    printf("  %.1f ns per step\n", ns_per_step);
    printf("  %.0f steps per second\n", steps_per_sec);
    printf("  %.2f million steps/sec\n", steps_per_sec / 1e6);

    #undef MAX_NODES
    #undef MAX_REGS
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Benchmark 2: Mutation Speed
 * ═══════════════════════════════════════════════════════════════════════════ */

static void bench_mutation(void) {
    printf("\n═══════════════════════════════════════════════════════════════\n");
    printf("Benchmark: Mutation Speed\n");
    printf("═══════════════════════════════════════════════════════════════\n");

    /* Create a population of genomes */
    #define POP_SIZE 64
    LiquidGenome population[POP_SIZE];
    EntroRNG rng;
    entro_rng_seed(&rng, 12345);

    /* Initialize population */
    for (int i = 0; i < POP_SIZE; i++) {
        entro_genesis(&population[i], 4, 8, 3, &rng, i);
    }

    printf("Population: %d genomes\n", POP_SIZE);
    printf("Genome size: %zu bytes each\n", sizeof(LiquidGenome));
    printf("Parameters per genome: %d\n", entro_genome_param_count(&population[0]));

    /* Benchmark mutation */
    const int MUTATIONS = 10000000;
    MutationParams params = MUTATION_DEFAULT;

    double start = get_time_sec();

    for (int i = 0; i < MUTATIONS; i++) {
        int idx = i % POP_SIZE;
        entro_mutate(&population[idx], &params, &rng);
    }

    double elapsed = get_time_sec() - start;
    double mutations_per_sec = MUTATIONS / elapsed;

    printf("\nResults:\n");
    printf("  %d mutations in %.3f seconds\n", MUTATIONS, elapsed);
    printf("  %.0f mutations per second\n", mutations_per_sec);
    printf("  %.2f million mutations/sec\n", mutations_per_sec / 1e6);

    #undef POP_SIZE
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Benchmark 3: Full Evolution Loop
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Simple fitness function: minimize output variance */
static float simple_fitness(const LiquidGenome* genome, EntroRNG* rng) {
    const int SEQ_LEN = 10;
    float inputs[SEQ_LEN * ENTROMORPH_MAX_INPUT];

    /* Generate random inputs */
    for (int i = 0; i < SEQ_LEN * genome->input_dim; i++) {
        inputs[i] = entro_rng_range(rng, -1.0f, 1.0f);
    }

    /* Target: outputs should be near zero */
    float targets[ENTROMORPH_MAX_OUTPUT] = {0};

    float mse = entro_evaluate_mse(genome, inputs, targets, SEQ_LEN, 0.1f, 1);
    return -mse;  /* Higher fitness = lower error */
}

static void bench_evolution(void) {
    printf("\n═══════════════════════════════════════════════════════════════\n");
    printf("Benchmark: Full Evolution Loop\n");
    printf("═══════════════════════════════════════════════════════════════\n");

    #define POP_SIZE 64

    /* Double buffer for populations */
    LiquidGenome pop_a[POP_SIZE];
    LiquidGenome pop_b[POP_SIZE];
    LiquidGenome* current = pop_a;
    LiquidGenome* next = pop_b;

    EntroRNG rng;
    entro_rng_seed(&rng, 42);
    uint32_t next_id = POP_SIZE;

    /* Initialize population */
    for (int i = 0; i < POP_SIZE; i++) {
        entro_genesis(&current[i], 4, 8, 3, &rng, i);
    }

    EvolutionParams params = EVOLUTION_DEFAULT;
    params.population_size = POP_SIZE;

    printf("Population: %d\n", POP_SIZE);
    printf("Input: %d, Hidden: %d, Output: %d\n", 4, 8, 3);

    /* Run evolution */
    const int GENERATIONS = 1000;

    double start = get_time_sec();

    for (int gen = 0; gen < GENERATIONS; gen++) {
        /* Evaluate fitness */
        for (int i = 0; i < POP_SIZE; i++) {
            current[i].fitness = simple_fitness(&current[i], &rng);
        }

        /* Evolve */
        entro_evolve_generation(current, next, POP_SIZE, &params, &rng,
                                &next_id, gen + 1);

        /* Swap populations */
        LiquidGenome* tmp = current;
        current = next;
        next = tmp;
    }

    double elapsed = get_time_sec() - start;
    double gens_per_sec = GENERATIONS / elapsed;

    /* Find best */
    int best_idx = 0;
    float best_fitness = current[0].fitness;
    for (int i = 1; i < POP_SIZE; i++) {
        if (current[i].fitness > best_fitness) {
            best_fitness = current[i].fitness;
            best_idx = i;
        }
    }

    printf("\nResults:\n");
    printf("  %d generations in %.3f seconds\n", GENERATIONS, elapsed);
    printf("  %.1f generations per second\n", gens_per_sec);
    printf("  Best fitness: %.6f (genome %u)\n", best_fitness, current[best_idx].id);
    printf("  Total evaluations: %d\n", GENERATIONS * POP_SIZE);
    printf("  Evaluations per second: %.0f\n", (GENERATIONS * POP_SIZE) / elapsed);

    #undef POP_SIZE
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Benchmark 4: Zero-Copy Binary Mutation (ShapeGraph)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void bench_binary_mutation(void) {
    printf("\n═══════════════════════════════════════════════════════════════\n");
    printf("Benchmark: Zero-Copy Binary Mutation (ShapeGraph)\n");
    printf("═══════════════════════════════════════════════════════════════\n");

    /* Build a ShapeGraph */
    #define MAX_NODES 128
    #define MAX_REGS 64
    ShapeNode nodes[MAX_NODES];
    float regs[MAX_REGS];
    ShapeGraph graph;

    shape_graph_init(&graph, nodes, regs, MAX_NODES, MAX_REGS);
    shape_graph_set_inputs(&graph, 4);

    uint32_t state_regs[8];
    for (int i = 0; i < 8; i++) {
        state_regs[i] = shape_alloc_reg(&graph);
    }

    float taus[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t input_regs[8] = {0, 1, 2, 3, 0, 1, 2, 3};
    shape_build_cfc_layer(&graph, input_regs, state_regs, 8, taus);

    printf("Graph: %u nodes\n", graph.node_count);

    EntroRNG rng;
    entro_rng_seed(&rng, 9999);

    /* Benchmark: raw integer mutations */
    const int MUTATIONS = 100000000;  /* 100 million */

    double start = get_time_sec();

    for (int i = 0; i < MUTATIONS; i++) {
        /* Zero-copy mutation: just change integers */
        uint32_t node_idx = entro_rng_int(&rng, graph.node_count);
        uint32_t new_input = entro_rng_int(&rng, graph.reg_count);
        shape_mutate_rewire(&graph, node_idx, new_input);
    }

    double elapsed = get_time_sec() - start;
    double muts_per_sec = MUTATIONS / elapsed;

    printf("\nResults:\n");
    printf("  %d mutations in %.3f seconds\n", MUTATIONS, elapsed);
    printf("  %.0f mutations per second\n", muts_per_sec);
    printf("  %.1f million mutations/sec\n", muts_per_sec / 1e6);
    printf("  %.1f ns per mutation\n", (elapsed / MUTATIONS) * 1e9);

    #undef MAX_NODES
    #undef MAX_REGS
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("╭──────────────────────────────────────────────────────────────╮\n");
    printf("│  EntroMorph Performance Benchmark                            │\n");
    printf("│  \"Millions of generations. Zero allocations.\"               │\n");
    printf("╰──────────────────────────────────────────────────────────────╯\n");

    bench_graph_execution();
    bench_mutation();
    bench_evolution();
    bench_binary_mutation();

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Benchmark complete.\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    return 0;
}
