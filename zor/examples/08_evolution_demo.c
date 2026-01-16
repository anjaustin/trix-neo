/*
 * 08_evolution_demo.c — EntroMorph Evolution Demo
 *
 * Demonstrates:
 *   1. Creating a population of random genomes
 *   2. Evaluating fitness
 *   3. Running evolution for multiple generations
 *   4. Exporting the winner as a frozen chip
 *
 * Build:
 *   gcc -O3 -I../include 08_evolution_demo.c -o 08_evolution_demo -lm
 *
 * Run:
 *   ./08_evolution_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "trixc/entromorph.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Configuration
 * ═══════════════════════════════════════════════════════════════════════════ */

#define INPUT_DIM   4
#define HIDDEN_DIM  8
#define OUTPUT_DIM  3
#define POP_SIZE    32
#define GENERATIONS 100
#define SEQ_LEN     10

/* ═══════════════════════════════════════════════════════════════════════════
 * Training Data: Simple XOR-like pattern
 *
 * Class 0: Both inputs positive OR both negative
 * Class 1: Input 0 positive, Input 1 negative
 * Class 2: Input 0 negative, Input 1 positive
 * ═══════════════════════════════════════════════════════════════════════════ */

#define NUM_SAMPLES 12

/* Sequences: [NUM_SAMPLES][SEQ_LEN][INPUT_DIM] */
static float train_inputs[NUM_SAMPLES][SEQ_LEN][INPUT_DIM];
static int train_labels[NUM_SAMPLES];

void generate_training_data(EntroRNG* rng) {
    for (int s = 0; s < NUM_SAMPLES; s++) {
        /* Assign class based on pattern */
        int pattern = s % 4;
        float sign0 = (pattern == 0 || pattern == 1) ? 1.0f : -1.0f;
        float sign1 = (pattern == 0 || pattern == 2) ? 1.0f : -1.0f;

        /* Determine label */
        if (sign0 == sign1) {
            train_labels[s] = 0;  /* Same sign */
        } else if (sign0 > 0) {
            train_labels[s] = 1;  /* +/- */
        } else {
            train_labels[s] = 2;  /* -/+ */
        }

        /* Generate sequence with noise */
        for (int t = 0; t < SEQ_LEN; t++) {
            float noise = entro_rng_range(rng, -0.2f, 0.2f);
            train_inputs[s][t][0] = sign0 * (0.5f + noise);
            train_inputs[s][t][1] = sign1 * (0.5f + noise);
            train_inputs[s][t][2] = entro_rng_range(rng, -0.1f, 0.1f);  /* Noise */
            train_inputs[s][t][3] = entro_rng_range(rng, -0.1f, 0.1f);  /* Noise */
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Fitness Function
 * ═══════════════════════════════════════════════════════════════════════════ */

float evaluate_genome(const LiquidGenome* genome, EntroRNG* rng) {
    (void)rng;  /* Not used in deterministic evaluation */

    /* Use classification accuracy as fitness */
    return entro_evaluate_classification(
        genome,
        (float*)train_inputs,
        train_labels,
        NUM_SAMPLES,
        SEQ_LEN,
        0.1f  /* dt */
    );
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("╭──────────────────────────────────────────────────────────────╮\n");
    printf("│  EntroMorph Evolution Demo                                   │\n");
    printf("│  \"Natural selection on silicon\"                             │\n");
    printf("╰──────────────────────────────────────────────────────────────╯\n");
    printf("\n");

    /* Initialize RNG */
    EntroRNG rng;
    entro_rng_seed(&rng, (uint64_t)time(NULL));

    /* Generate training data */
    printf("Generating training data...\n");
    generate_training_data(&rng);
    printf("  Samples: %d\n", NUM_SAMPLES);
    printf("  Sequence length: %d\n", SEQ_LEN);
    printf("  Classes: 3 (same-sign, +/-, -/+)\n");
    printf("\n");

    /* Show sample */
    printf("Sample labels: [");
    for (int i = 0; i < NUM_SAMPLES; i++) {
        printf("%d%s", train_labels[i], i < NUM_SAMPLES - 1 ? ", " : "]\n");
    }
    printf("\n");

    /* Initialize populations */
    LiquidGenome* pop_a = (LiquidGenome*)malloc(POP_SIZE * sizeof(LiquidGenome));
    LiquidGenome* pop_b = (LiquidGenome*)malloc(POP_SIZE * sizeof(LiquidGenome));
    LiquidGenome* current = pop_a;
    LiquidGenome* next = pop_b;

    uint32_t next_id = POP_SIZE;

    for (int i = 0; i < POP_SIZE; i++) {
        entro_genesis(&current[i], INPUT_DIM, HIDDEN_DIM, OUTPUT_DIM, &rng, i);
    }

    printf("Initializing population...\n");
    printf("  Population size: %d\n", POP_SIZE);
    printf("  Network: %d → %d → %d\n", INPUT_DIM, HIDDEN_DIM, OUTPUT_DIM);
    printf("  Genome size: %zu bytes\n", sizeof(LiquidGenome));
    printf("  Parameters: %d\n", entro_genome_param_count(&current[0]));
    printf("\n");

    /* Evolution parameters */
    EvolutionParams params = EVOLUTION_DEFAULT;
    params.population_size = POP_SIZE;
    params.elite_count = 2;
    params.tournament_size = 3;
    params.crossover_rate = 0.7f;

    /* Evolution loop */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Starting evolution (%d generations)\n", GENERATIONS);
    printf("═══════════════════════════════════════════════════════════════\n\n");

    clock_t start = clock();

    float best_ever = -INFINITY;
    int best_gen = 0;

    for (int gen = 0; gen < GENERATIONS; gen++) {
        /* Evaluate fitness */
        for (int i = 0; i < POP_SIZE; i++) {
            current[i].fitness = evaluate_genome(&current[i], &rng);
        }

        /* Find best */
        int best_idx = 0;
        float best_fitness = current[0].fitness;
        float sum_fitness = current[0].fitness;

        for (int i = 1; i < POP_SIZE; i++) {
            sum_fitness += current[i].fitness;
            if (current[i].fitness > best_fitness) {
                best_fitness = current[i].fitness;
                best_idx = i;
            }
        }

        float avg_fitness = sum_fitness / POP_SIZE;

        /* Track best ever */
        if (best_fitness > best_ever) {
            best_ever = best_fitness;
            best_gen = gen;
        }

        /* Print progress */
        if (gen % 10 == 0 || gen == GENERATIONS - 1) {
            printf("Gen %3d: best=%.1f%% avg=%.1f%% (genome %u)\n",
                   gen, best_fitness * 100, avg_fitness * 100, current[best_idx].id);
        }

        /* Evolve */
        entro_evolve_generation(current, next, POP_SIZE, &params, &rng,
                                &next_id, gen + 1);

        /* Swap buffers */
        LiquidGenome* tmp = current;
        current = next;
        next = tmp;
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Evolution complete\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("Time: %.3f seconds\n", elapsed);
    printf("Generations/sec: %.1f\n", GENERATIONS / elapsed);
    printf("Best accuracy: %.1f%% (generation %d)\n", best_ever * 100, best_gen);
    printf("\n");

    /* Final evaluation */
    for (int i = 0; i < POP_SIZE; i++) {
        current[i].fitness = evaluate_genome(&current[i], &rng);
    }

    /* Find winner */
    int winner_idx = 0;
    for (int i = 1; i < POP_SIZE; i++) {
        if (current[i].fitness > current[winner_idx].fitness) {
            winner_idx = i;
        }
    }

    LiquidGenome* winner = &current[winner_idx];

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Winner Analysis\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("Genome ID: %u\n", winner->id);
    printf("Generation: %u\n", winner->generation);
    printf("Parents: %u, %u\n", winner->parent_a, winner->parent_b);
    printf("Accuracy: %.1f%%\n", winner->fitness * 100);
    printf("\n");

    /* Test winner on each sample */
    printf("Predictions:\n");
    printf("  Sample  | Actual | Predicted | Correct\n");
    printf("  --------+--------+-----------+--------\n");

    CfCParams cell_params;
    CfCOutputParams out_params;
    entro_genome_to_params(winner, &cell_params, &out_params);

    int correct = 0;
    for (int s = 0; s < NUM_SAMPLES; s++) {
        /* Process sequence */
        float h[ENTROMORPH_MAX_HIDDEN] = {0};
        float h_new[ENTROMORPH_MAX_HIDDEN];

        for (int t = 0; t < SEQ_LEN; t++) {
            trix_cfc_cell(train_inputs[s][t], h, 0.1f, &cell_params, h_new);
            memcpy(h, h_new, HIDDEN_DIM * sizeof(float));
        }

        /* Get prediction */
        float probs[ENTROMORPH_MAX_OUTPUT];
        trix_cfc_output_softmax(h, &out_params, probs);

        int pred = 0;
        for (int i = 1; i < OUTPUT_DIM; i++) {
            if (probs[i] > probs[pred]) pred = i;
        }

        int is_correct = (pred == train_labels[s]);
        correct += is_correct;

        printf("  %6d  |   %d    |     %d     |   %s\n",
               s, train_labels[s], pred, is_correct ? "✓" : "✗");
    }

    printf("\nFinal accuracy: %d/%d (%.1f%%)\n", correct, NUM_SAMPLES,
           100.0f * correct / NUM_SAMPLES);
    printf("\n");

    /* Export winner */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Exporting winner to evolved_chip.h\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    FILE* f = fopen("evolved_chip.h", "w");
    if (f) {
        entro_export_header(winner, "EVOLVED", f);
        fclose(f);
        printf("Saved to: evolved_chip.h\n");
        printf("\n");
        printf("To use:\n");
        printf("  #include \"evolved_chip.h\"\n");
        printf("  #include \"trixc/cfc_shapes.h\"\n");
        printf("  // Then build CfCParams from the EVOLVED_* arrays\n");
    } else {
        printf("Error: Could not write file\n");
    }

    printf("\n");

    /* Cleanup */
    free(pop_a);
    free(pop_b);

    return 0;
}
