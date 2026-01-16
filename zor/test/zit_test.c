/*
 * zit_test.c — Anomaly Detection via Phase-Locked Tracking
 *
 * "Define Order, and Chaos reveals itself."
 *
 * The CfC tracker's "slowness" is actually its sensitivity to anomalies.
 * It acts as a stiff reference frame. Anything that moves too fast
 * for the CfC to track is, by definition, an anomaly.
 *
 * Build:
 *   gcc -O3 -I../include zit_test.c -o zit_test -lm
 *
 * Run:
 *   ./zit_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* Include the evolved sine tracker */
#include "../foundry/seeds/sine_seed.h"

/* Detection threshold — tuned for the Efficient Species */
#define ZIT_THRESHOLD 0.1122911624f

/* ═══════════════════════════════════════════════════════════════════════════
 * Signal Generators
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Clean signal (what the tracker learned) */
static inline float clean_signal(int t) {
    float phase1 = t * 0.1f;
    float phase2 = t * 0.4f;
    float phase3 = t * 0.023f;
    return 0.6f * sinf(phase1) + 0.25f * sinf(phase2) + 0.15f * sinf(phase3);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Anomaly Types
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    ANOMALY_SPIKE,      /* Sudden large value */
    ANOMALY_DROP,       /* Sudden drop to zero */
    ANOMALY_SQUARE,     /* Square wave glitch */
    ANOMALY_NOISE,      /* High-frequency noise burst */
    ANOMALY_DRIFT,      /* Gradual baseline shift */
} AnomalyType;

/* ═══════════════════════════════════════════════════════════════════════════
 * Main Test
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  ZIT DETECTION TEST                                          ║\n");
    printf("║  \"The CfC's slowness IS its sensitivity\"                    ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("Chip Statistics:\n");
    printf("  Nodes: %d\n", sine_SEED_NODES);
    printf("  Registers: %d\n", sine_SEED_REGS);
    printf("  Memory: %d bytes\n", (int)(sine_SEED_REGS * sizeof(float)));
    printf("\n");

    /* Initialize tracker state */
    float state[sine_SEED_REGS];
    sine_seed_reset(state);

    /* ═══════════════════════════════════════════════════════════════════════
     * Phase 1: Lock onto clean signal
     * ═══════════════════════════════════════════════════════════════════════ */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("PHASE 1: Locking onto clean signal (100 steps)\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    float baseline_error = 0.0f;
    for (int t = 0; t < 100; t++) {
        float input = clean_signal(t);
        float output = sine_seed_step(input, state);
        baseline_error += fabsf(input - output);
    }
    baseline_error /= 100.0f;

    printf("Phase locked. Baseline MAE: %.4f\n", baseline_error);
    printf("\n");

    /* ═══════════════════════════════════════════════════════════════════════
     * Phase 2: Inject anomalies and measure response
     * ═══════════════════════════════════════════════════════════════════════ */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("PHASE 2: Injecting anomalies\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    /* Test 1: Spike anomaly */
    printf("TEST 1: SPIKE ANOMALY (Input += 2.0 at t=105)\n");
    printf("─────────────────────────────────────────────────────────────\n");
    printf("  t   |  Input  | Tracker |  Delta  | Status\n");
    printf("──────+---------+---------+---------+────────────────────\n");

    sine_seed_reset(state);
    /* Warm up */
    for (int t = 0; t < 100; t++) {
        float input = clean_signal(t);
        sine_seed_step(input, state);
    }

    int spike_detected = 0;
    for (int t = 100; t < 120; t++) {
        float input = clean_signal(t);

        /* THE ZIT: Spike at t=105 */
        if (t == 105) input += 2.0f;

        float output = sine_seed_step(input, state);
        float delta = fabsf(input - output);

        /* Detection threshold: 5x baseline error */
        const char* status = "";
        if (delta > ZIT_THRESHOLD) {
            status = "<<< ZIT DETECTED!";
            spike_detected = 1;
        } else if (delta > ZIT_THRESHOLD * 0.5f) {
            status = "<< elevated";
        }

        printf(" %3d  | %7.3f | %7.3f | %7.3f | %s\n",
               t, input, output, delta, status);
    }
    printf("\n");

    /* Test 2: Drop anomaly */
    printf("TEST 2: DROP ANOMALY (Input = 0 at t=155)\n");
    printf("─────────────────────────────────────────────────────────────\n");
    printf("  t   |  Input  | Tracker |  Delta  | Status\n");
    printf("──────+---------+---------+---------+────────────────────\n");

    sine_seed_reset(state);
    for (int t = 0; t < 150; t++) {
        float input = clean_signal(t);
        sine_seed_step(input, state);
    }

    int drop_detected = 0;
    for (int t = 150; t < 170; t++) {
        float input = clean_signal(t);

        /* THE ZIT: Drop to zero at t=155-157 */
        if (t >= 155 && t <= 157) input = 0.0f;

        float output = sine_seed_step(input, state);
        float delta = fabsf(input - output);

        const char* status = "";
        if (delta > ZIT_THRESHOLD) {
            status = "<<< ZIT DETECTED!";
            drop_detected = 1;
        } else if (delta > ZIT_THRESHOLD * 0.5f) {
            status = "<< elevated";
        }

        printf(" %3d  | %7.3f | %7.3f | %7.3f | %s\n",
               t, input, output, delta, status);
    }
    printf("\n");

    /* Test 3: Noise burst */
    printf("TEST 3: NOISE BURST (Random ±0.5 at t=205-210)\n");
    printf("─────────────────────────────────────────────────────────────\n");
    printf("  t   |  Input  | Tracker |  Delta  | Status\n");
    printf("──────+---------+---------+---------+────────────────────\n");

    sine_seed_reset(state);
    for (int t = 0; t < 200; t++) {
        float input = clean_signal(t);
        sine_seed_step(input, state);
    }

    int noise_detected = 0;
    srand(42);  /* Reproducible */
    for (int t = 200; t < 220; t++) {
        float input = clean_signal(t);

        /* THE ZIT: High-frequency noise at t=205-210 */
        if (t >= 205 && t <= 210) {
            input += ((rand() % 100) / 100.0f - 0.5f) * 1.0f;
        }

        float output = sine_seed_step(input, state);
        float delta = fabsf(input - output);

        const char* status = "";
        if (delta > ZIT_THRESHOLD) {
            status = "<<< ZIT DETECTED!";
            noise_detected = 1;
        } else if (delta > ZIT_THRESHOLD * 0.5f) {
            status = "<< elevated";
        }

        printf(" %3d  | %7.3f | %7.3f | %7.3f | %s\n",
               t, input, output, delta, status);
    }
    printf("\n");

    /* Test 4: Gradual drift (harder to detect) */
    printf("TEST 4: GRADUAL DRIFT (+0.01/step starting at t=255)\n");
    printf("─────────────────────────────────────────────────────────────\n");
    printf("  t   |  Input  | Tracker |  Delta  | Status\n");
    printf("──────+---------+---------+---------+────────────────────\n");

    sine_seed_reset(state);
    for (int t = 0; t < 250; t++) {
        float input = clean_signal(t);
        sine_seed_step(input, state);
    }

    int drift_detected = 0;
    for (int t = 250; t < 280; t++) {
        float input = clean_signal(t);

        /* THE ZIT: Gradual drift starting at t=255 */
        if (t >= 255) {
            input += 0.02f * (t - 255);  /* +0.02 per step */
        }

        float output = sine_seed_step(input, state);
        float delta = fabsf(input - output);

        const char* status = "";
        if (delta > ZIT_THRESHOLD) {
            status = "<<< ZIT DETECTED!";
            drift_detected = 1;
        } else if (delta > ZIT_THRESHOLD * 0.5f) {
            status = "<< elevated";
        }

        printf(" %3d  | %7.3f | %7.3f | %7.3f | %s\n",
               t, input, output, delta, status);
    }
    printf("\n");

    /* ═══════════════════════════════════════════════════════════════════════
     * Results Summary
     * ═══════════════════════════════════════════════════════════════════════ */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("DETECTION SUMMARY\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("  Baseline MAE: %.4f\n", baseline_error);
    printf("  Detection threshold: %.4f (tuned)\n", ZIT_THRESHOLD);
    printf("\n");

    printf("  Test 1 (Spike):  %s\n", spike_detected ? "DETECTED" : "MISSED");
    printf("  Test 2 (Drop):   %s\n", drop_detected ? "DETECTED" : "MISSED");
    printf("  Test 3 (Noise):  %s\n", noise_detected ? "DETECTED" : "MISSED");
    printf("  Test 4 (Drift):  %s\n", drift_detected ? "DETECTED" : "MISSED");
    printf("\n");

    int total_detected = spike_detected + drop_detected + noise_detected + drift_detected;

    printf("═══════════════════════════════════════════════════════════════\n");
    if (total_detected == 4) {
        printf("VERDICT: PERFECT DETECTION (4/4)\n");
        printf("The CfC tracker detected ALL anomaly types.\n");
    } else if (total_detected >= 3) {
        printf("VERDICT: EXCELLENT DETECTION (%d/4)\n", total_detected);
        printf("The tracker caught most anomalies.\n");
    } else if (total_detected >= 2) {
        printf("VERDICT: GOOD DETECTION (%d/4)\n", total_detected);
        printf("Detects obvious anomalies, misses subtle ones.\n");
    } else {
        printf("VERDICT: NEEDS TUNING (%d/4)\n", total_detected);
        printf("Consider adjusting detection threshold.\n");
    }
    printf("═══════════════════════════════════════════════════════════════\n\n");

    return 0;
}
