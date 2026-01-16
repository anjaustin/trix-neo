/*
 * stability_test.c — Numerical Stability Proof
 *
 * "The Skeptic's Doubt: Recurrent networks accumulate floating-point errors."
 *
 * This test runs the sine_seed.h chip for 100,000,000 steps and measures
 * whether the feedback loop (S_prev) diverges or explodes.
 *
 * Success Metric: MAE at Step 10,000, 100,000, and 100,000,000 must be
 * within 0.1% of the MAE at Step 1,000.
 *
 * Build:
 *   gcc -O3 -I../include stability_test.c -o stability_test -lm
 *
 * Run:
 *   ./stability_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <float.h>

#include "../foundry/seeds/sine_seed.h"

/* Test parameters */
#define TOTAL_STEPS     100000000   /* 100 million steps */
#define CHECKPOINT_1    1000
#define CHECKPOINT_2    10000
#define CHECKPOINT_3    100000
#define CHECKPOINT_4    1000000
#define CHECKPOINT_5    10000000
#define CHECKPOINT_6    100000000

/* Window size for MAE calculation at each checkpoint */
#define MAE_WINDOW      1000

/* Clean sine signal */
static inline float clean_sine(long t) {
    float phase = t * 0.1f;
    return sinf(phase);
}

/* Calculate MAE over a window */
float calculate_mae(float* state, long start_step, int window) {
    float total_error = 0.0f;

    for (int i = 0; i < window; i++) {
        long t = start_step + i;
        float input = clean_sine(t);
        float output = sine_seed_step(input, state);
        total_error += fabsf(input - output);
    }

    return total_error / window;
}

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  NUMERICAL STABILITY TEST                                    ║\n");
    printf("║  \"The Skeptic's Doubt: RNNs accumulate floating-point error\" ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("Test Parameters:\n");
    printf("  Total steps: %d (100 million)\n", TOTAL_STEPS);
    printf("  Equivalent real-time at 10Hz: %.1f days\n", TOTAL_STEPS / 10.0 / 86400.0);
    printf("  MAE window: %d steps\n", MAE_WINDOW);
    printf("  Success criterion: MAE drift < 0.1%% from baseline\n");
    printf("\n");

    /* Initialize state */
    float state[sine_SEED_REGS];
    sine_seed_reset(state);

    /* Tracking variables */
    float mae_at_1k = 0, mae_at_10k = 0, mae_at_100k = 0;
    float mae_at_1m = 0, mae_at_10m = 0, mae_at_100m = 0;
    float max_output = -FLT_MAX, min_output = FLT_MAX;
    float max_state = -FLT_MAX, min_state = FLT_MAX;

    /* Timing */
    clock_t start_time = clock();
    clock_t last_report = start_time;

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Running 100,000,000 steps...\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    /* Main loop */
    long t;
    for (t = 0; t < TOTAL_STEPS; t++) {
        float input = clean_sine(t);
        float output = sine_seed_step(input, state);

        /* Track output bounds */
        if (output > max_output) max_output = output;
        if (output < min_output) min_output = output;

        /* Track state bounds (check all registers) */
        for (int r = 0; r < sine_SEED_REGS; r++) {
            if (state[r] > max_state) max_state = state[r];
            if (state[r] < min_state) min_state = state[r];
        }

        /* Check for NaN/Inf explosion */
        if (isnan(output) || isinf(output)) {
            printf("FAILURE: Output exploded at step %ld (NaN or Inf)\n", t);
            return 1;
        }

        /* Checkpoints - measure MAE over window ENDING at checkpoint */
        if (t == CHECKPOINT_1 - 1) {
            /* Reset and measure fresh MAE at this point */
            float temp_state[sine_SEED_REGS];
            for (int r = 0; r < sine_SEED_REGS; r++) temp_state[r] = state[r];

            float err_sum = 0;
            for (int i = 0; i < MAE_WINDOW; i++) {
                float inp = clean_sine(CHECKPOINT_1 + i);
                float out = sine_seed_step(inp, temp_state);
                err_sum += fabsf(inp - out);
            }
            mae_at_1k = err_sum / MAE_WINDOW;

            printf("  [%12ld] MAE = %.6f (baseline)\n", CHECKPOINT_1, mae_at_1k);
            fflush(stdout);
        }

        if (t == CHECKPOINT_2 - 1) {
            float temp_state[sine_SEED_REGS];
            for (int r = 0; r < sine_SEED_REGS; r++) temp_state[r] = state[r];

            float err_sum = 0;
            for (int i = 0; i < MAE_WINDOW; i++) {
                float inp = clean_sine(CHECKPOINT_2 + i);
                float out = sine_seed_step(inp, temp_state);
                err_sum += fabsf(inp - out);
            }
            mae_at_10k = err_sum / MAE_WINDOW;

            float drift = fabsf(mae_at_10k - mae_at_1k) / mae_at_1k * 100.0f;
            printf("  [%12ld] MAE = %.6f (drift: %.4f%%)\n", CHECKPOINT_2, mae_at_10k, drift);
            fflush(stdout);
        }

        if (t == CHECKPOINT_3 - 1) {
            float temp_state[sine_SEED_REGS];
            for (int r = 0; r < sine_SEED_REGS; r++) temp_state[r] = state[r];

            float err_sum = 0;
            for (int i = 0; i < MAE_WINDOW; i++) {
                float inp = clean_sine(CHECKPOINT_3 + i);
                float out = sine_seed_step(inp, temp_state);
                err_sum += fabsf(inp - out);
            }
            mae_at_100k = err_sum / MAE_WINDOW;

            float drift = fabsf(mae_at_100k - mae_at_1k) / mae_at_1k * 100.0f;
            printf("  [%12ld] MAE = %.6f (drift: %.4f%%)\n", CHECKPOINT_3, mae_at_100k, drift);
            fflush(stdout);
        }

        if (t == CHECKPOINT_4 - 1) {
            float temp_state[sine_SEED_REGS];
            for (int r = 0; r < sine_SEED_REGS; r++) temp_state[r] = state[r];

            float err_sum = 0;
            for (int i = 0; i < MAE_WINDOW; i++) {
                float inp = clean_sine(CHECKPOINT_4 + i);
                float out = sine_seed_step(inp, temp_state);
                err_sum += fabsf(inp - out);
            }
            mae_at_1m = err_sum / MAE_WINDOW;

            float drift = fabsf(mae_at_1m - mae_at_1k) / mae_at_1k * 100.0f;
            printf("  [%12ld] MAE = %.6f (drift: %.4f%%)\n", CHECKPOINT_4, mae_at_1m, drift);
            fflush(stdout);
        }

        if (t == CHECKPOINT_5 - 1) {
            float temp_state[sine_SEED_REGS];
            for (int r = 0; r < sine_SEED_REGS; r++) temp_state[r] = state[r];

            float err_sum = 0;
            for (int i = 0; i < MAE_WINDOW; i++) {
                float inp = clean_sine(CHECKPOINT_5 + i);
                float out = sine_seed_step(inp, temp_state);
                err_sum += fabsf(inp - out);
            }
            mae_at_10m = err_sum / MAE_WINDOW;

            float drift = fabsf(mae_at_10m - mae_at_1k) / mae_at_1k * 100.0f;
            printf("  [%12ld] MAE = %.6f (drift: %.4f%%)\n", CHECKPOINT_5, mae_at_10m, drift);
            fflush(stdout);
        }

        /* Progress report every 10M steps */
        if ((t + 1) % 10000000 == 0) {
            clock_t now = clock();
            double elapsed = (double)(now - start_time) / CLOCKS_PER_SEC;
            double rate = (t + 1) / elapsed / 1000000.0;
            printf("  ... %ldM steps, %.1f sec, %.2f M steps/sec\n",
                   (t + 1) / 1000000, elapsed, rate);
            fflush(stdout);
        }
    }

    /* Final checkpoint at 100M */
    {
        float temp_state[sine_SEED_REGS];
        for (int r = 0; r < sine_SEED_REGS; r++) temp_state[r] = state[r];

        float err_sum = 0;
        for (int i = 0; i < MAE_WINDOW; i++) {
            float inp = clean_sine(CHECKPOINT_6 + i);
            float out = sine_seed_step(inp, temp_state);
            err_sum += fabsf(inp - out);
        }
        mae_at_100m = err_sum / MAE_WINDOW;

        float drift = fabsf(mae_at_100m - mae_at_1k) / mae_at_1k * 100.0f;
        printf("  [%12ld] MAE = %.6f (drift: %.4f%%)\n", CHECKPOINT_6, mae_at_100m, drift);
    }

    clock_t end_time = clock();
    double total_seconds = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("RESULTS\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("Execution:\n");
    printf("  Total time: %.2f seconds\n", total_seconds);
    printf("  Throughput: %.2f M steps/sec\n", TOTAL_STEPS / total_seconds / 1000000.0);
    printf("\n");

    printf("Bounds (checking for explosion):\n");
    printf("  Output range: [%.6f, %.6f]\n", min_output, max_output);
    printf("  State range:  [%.6f, %.6f]\n", min_state, max_state);
    printf("\n");

    printf("MAE Stability:\n");
    printf("  ┌─────────────────┬────────────┬────────────┐\n");
    printf("  │ Checkpoint      │ MAE        │ Drift      │\n");
    printf("  ├─────────────────┼────────────┼────────────┤\n");
    printf("  │ %15ld │ %10.6f │ baseline   │\n", CHECKPOINT_1, mae_at_1k);
    printf("  │ %15ld │ %10.6f │ %8.4f%% │\n", CHECKPOINT_2, mae_at_10k,
           fabsf(mae_at_10k - mae_at_1k) / mae_at_1k * 100.0f);
    printf("  │ %15ld │ %10.6f │ %8.4f%% │\n", CHECKPOINT_3, mae_at_100k,
           fabsf(mae_at_100k - mae_at_1k) / mae_at_1k * 100.0f);
    printf("  │ %15ld │ %10.6f │ %8.4f%% │\n", CHECKPOINT_4, mae_at_1m,
           fabsf(mae_at_1m - mae_at_1k) / mae_at_1k * 100.0f);
    printf("  │ %15ld │ %10.6f │ %8.4f%% │\n", CHECKPOINT_5, mae_at_10m,
           fabsf(mae_at_10m - mae_at_1k) / mae_at_1k * 100.0f);
    printf("  │ %15ld │ %10.6f │ %8.4f%% │\n", CHECKPOINT_6, mae_at_100m,
           fabsf(mae_at_100m - mae_at_1k) / mae_at_1k * 100.0f);
    printf("  └─────────────────┴────────────┴────────────┘\n");
    printf("\n");

    /* Verdict */
    float max_drift = 0;
    float drifts[5] = {
        fabsf(mae_at_10k - mae_at_1k) / mae_at_1k * 100.0f,
        fabsf(mae_at_100k - mae_at_1k) / mae_at_1k * 100.0f,
        fabsf(mae_at_1m - mae_at_1k) / mae_at_1k * 100.0f,
        fabsf(mae_at_10m - mae_at_1k) / mae_at_1k * 100.0f,
        fabsf(mae_at_100m - mae_at_1k) / mae_at_1k * 100.0f
    };
    for (int i = 0; i < 5; i++) {
        if (drifts[i] > max_drift) max_drift = drifts[i];
    }

    printf("═══════════════════════════════════════════════════════════════\n");
    if (max_drift < 0.1f) {
        printf("VERDICT: PASSED — NUMERICALLY STABLE\n");
        printf("Maximum drift: %.4f%% (threshold: 0.1%%)\n", max_drift);
        printf("\n");
        printf("The Skeptic's Doubt has been answered.\n");
        printf("100 million steps. 115 days equivalent. No divergence.\n");
    } else if (max_drift < 1.0f) {
        printf("VERDICT: MARGINAL — SLIGHT DRIFT DETECTED\n");
        printf("Maximum drift: %.4f%% (threshold: 0.1%%)\n", max_drift);
    } else {
        printf("VERDICT: FAILED — SIGNIFICANT DRIFT\n");
        printf("Maximum drift: %.4f%% (threshold: 0.1%%)\n", max_drift);
    }
    printf("═══════════════════════════════════════════════════════════════\n\n");

    return (max_drift < 0.1f) ? 0 : 1;
}
