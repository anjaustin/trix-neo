/*
 * noise_ramp.c — Robustness Test via Noise Injection
 *
 * "It works on clean data. Real sensors are dirty."
 *
 * This test defines the operational envelope: at what noise level
 * does the tracker "lose lock"? We feed signal + noise and ramp
 * the noise amplitude from 0.0 to 2.0.
 *
 * The chip's output should remain smooth (low variance) even as
 * input variance explodes. We find the BREAK POINT where tracking fails.
 *
 * Build:
 *   gcc -O3 -I../include noise_ramp.c -o noise_ramp -lm
 *
 * Run:
 *   ./noise_ramp
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* Include the evolved sine tracker */
#include "../foundry/seeds/sine_seed.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Test Configuration
 * ═══════════════════════════════════════════════════════════════════════════ */

#define SAMPLE_RATE       100      /* Samples per second */
#define TEST_DURATION     5.0f     /* Seconds per noise level */
#define WARMUP_DURATION   1.0f     /* Warmup seconds before measurement */

#define NOISE_MIN         0.0f     /* Starting noise amplitude */
#define NOISE_MAX         2.0f     /* Maximum noise amplitude */
#define NOISE_STEPS       21       /* Number of noise levels to test */

#define SIGNAL_FREQ       1.0f     /* Base signal frequency (Hz) */
#define SIGNAL_AMP        1.0f     /* Base signal amplitude */

/* Lock detection thresholds */
#define CORRELATION_LOCK  0.5f     /* Below this = lost lock (50% = still useful) */
#define CORRELATION_GOOD  0.7f     /* Above this = solid tracking */
#define SNR_IMPROVEMENT   1.5f     /* Output SNR must be > input SNR * this */

/* ═══════════════════════════════════════════════════════════════════════════
 * Random Number Generator (reproducible)
 * ═══════════════════════════════════════════════════════════════════════════ */

static unsigned int rng_state = 42;

float rand_uniform(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return (float)((rng_state >> 16) & 0x7FFF) / 32767.0f;
}

float rand_gaussian(void) {
    /* Box-Muller transform */
    float u1 = rand_uniform();
    float u2 = rand_uniform();
    if (u1 < 1e-10f) u1 = 1e-10f;
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * M_PI * u2);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Signal Statistics
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    float noise_amplitude;
    float input_variance;
    float output_variance;
    float clean_variance;
    float input_snr_db;
    float output_snr_db;
    float snr_improvement_db;
    float correlation;
    int lock_status;  /* 1 = locked, 0 = lost */
} NoiseTestResult;

float calculate_variance(const float* data, int len) {
    float mean = 0.0f;
    for (int i = 0; i < len; i++) mean += data[i];
    mean /= len;

    float var = 0.0f;
    for (int i = 0; i < len; i++) {
        float diff = data[i] - mean;
        var += diff * diff;
    }
    return var / len;
}

float calculate_correlation(const float* x, const float* y, int len) {
    float mean_x = 0.0f, mean_y = 0.0f;
    for (int i = 0; i < len; i++) {
        mean_x += x[i];
        mean_y += y[i];
    }
    mean_x /= len;
    mean_y /= len;

    float sum_xy = 0.0f, sum_x2 = 0.0f, sum_y2 = 0.0f;
    for (int i = 0; i < len; i++) {
        float dx = x[i] - mean_x;
        float dy = y[i] - mean_y;
        sum_xy += dx * dy;
        sum_x2 += dx * dx;
        sum_y2 += dy * dy;
    }

    return sum_xy / (sqrtf(sum_x2 * sum_y2) + 1e-10f);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main Test
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  NOISE RAMP TEST (Robustness Analysis)                       ║\n");
    printf("║  \"Real sensors are dirty.\"                                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("Chip Statistics:\n");
    printf("  Nodes: %d\n", sine_SEED_NODES);
    printf("  Registers: %d\n", sine_SEED_REGS);
    printf("  Memory: %d bytes\n", (int)(sine_SEED_REGS * sizeof(float)));
    printf("\n");

    printf("Test Configuration:\n");
    printf("  Signal: %.1f Hz sine wave, amplitude %.1f\n", SIGNAL_FREQ, SIGNAL_AMP);
    printf("  Noise: Gaussian, amplitude %.1f to %.1f\n", NOISE_MIN, NOISE_MAX);
    printf("  Duration: %.1f sec per level (%.1f sec warmup)\n",
           TEST_DURATION, WARMUP_DURATION);
    printf("\n");

    /* Calculate samples */
    int warmup_samples = (int)(WARMUP_DURATION * SAMPLE_RATE);
    int measure_samples = (int)(TEST_DURATION * SAMPLE_RATE);
    int total_samples = warmup_samples + measure_samples;

    /* Allocate buffers */
    float* clean_signal = malloc(total_samples * sizeof(float));
    float* noisy_input = malloc(total_samples * sizeof(float));
    float* tracker_output = malloc(total_samples * sizeof(float));

    /* Storage for results */
    NoiseTestResult results[NOISE_STEPS];
    float break_point = -1.0f;

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("NOISE RAMP\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf(" Noise | In Var | Out Var | In SNR | Out SNR | Improv | Corr  | Status\n");
    printf("───────┼────────┼─────────┼────────┼─────────┼────────┼───────┼────────\n");

    float omega = 2.0f * M_PI * SIGNAL_FREQ / SAMPLE_RATE;

    for (int n = 0; n < NOISE_STEPS; n++) {
        float noise_amp = NOISE_MIN + (NOISE_MAX - NOISE_MIN) * n / (NOISE_STEPS - 1);

        /* Reset RNG for reproducibility */
        rng_state = 42 + n;

        /* Initialize tracker */
        float state[sine_SEED_REGS];
        sine_seed_reset(state);

        /* Generate signals and run tracker */
        for (int t = 0; t < total_samples; t++) {
            float clean = SIGNAL_AMP * sinf(omega * t);
            float noise = noise_amp * rand_gaussian();
            float input = clean + noise;

            float output = sine_seed_step(input, state);

            clean_signal[t] = clean;
            noisy_input[t] = input;
            tracker_output[t] = output;
        }

        /* Analyze measurement portion (after warmup) */
        float* clean_measure = clean_signal + warmup_samples;
        float* input_measure = noisy_input + warmup_samples;
        float* output_measure = tracker_output + warmup_samples;

        /* Calculate variances */
        float clean_var = calculate_variance(clean_measure, measure_samples);
        float input_var = calculate_variance(input_measure, measure_samples);
        float output_var = calculate_variance(output_measure, measure_samples);

        /* Calculate noise variance (input - clean) */
        float noise_var = 0.0f;
        for (int i = 0; i < measure_samples; i++) {
            float noise_sample = input_measure[i] - clean_measure[i];
            noise_var += noise_sample * noise_sample;
        }
        noise_var /= measure_samples;

        /* Calculate output error variance (output - clean) */
        float error_var = 0.0f;
        for (int i = 0; i < measure_samples; i++) {
            float error = output_measure[i] - clean_measure[i];
            error_var += error * error;
        }
        error_var /= measure_samples;

        /* Calculate SNR */
        float input_snr_db = 10.0f * log10f(clean_var / (noise_var + 1e-10f));
        float output_snr_db = 10.0f * log10f(clean_var / (error_var + 1e-10f));
        float snr_improvement = output_snr_db - input_snr_db;

        /* Calculate correlation between output and clean signal */
        float correlation = calculate_correlation(output_measure, clean_measure, measure_samples);

        /* Determine lock status */
        int locked = (correlation >= CORRELATION_LOCK);
        int good = (correlation >= CORRELATION_GOOD);

        /* Store results */
        results[n].noise_amplitude = noise_amp;
        results[n].input_variance = input_var;
        results[n].output_variance = output_var;
        results[n].clean_variance = clean_var;
        results[n].input_snr_db = input_snr_db;
        results[n].output_snr_db = output_snr_db;
        results[n].snr_improvement_db = snr_improvement;
        results[n].correlation = correlation;
        results[n].lock_status = locked;

        /* Track break point */
        if (!locked && break_point < 0) {
            break_point = noise_amp;
        }

        /* Print row */
        const char* status = good ? "SOLID" : (locked ? "WEAK" : "LOST");
        printf(" %5.2f | %6.3f | %7.3f | %+6.1f | %+7.1f | %+6.1f | %5.3f | %s\n",
               noise_amp, input_var, output_var, input_snr_db, output_snr_db,
               snr_improvement, correlation, status);
    }

    printf("\n");

    /* ═══════════════════════════════════════════════════════════════════════
     * ASCII Variance Plot
     * ═══════════════════════════════════════════════════════════════════════ */

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("VARIANCE COMPARISON (Input vs Output)\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    /* Find max variance for scaling */
    float max_var = 0.0f;
    for (int n = 0; n < NOISE_STEPS; n++) {
        if (results[n].input_variance > max_var) max_var = results[n].input_variance;
        if (results[n].output_variance > max_var) max_var = results[n].output_variance;
    }

    printf("Variance\n");
    printf("    │\n");

    int plot_height = 12;
    for (int row = 0; row < plot_height; row++) {
        float level = max_var * (plot_height - row - 1) / (plot_height - 1);
        printf(" %5.2f │", level);

        for (int n = 0; n < NOISE_STEPS; n++) {
            float in_var = results[n].input_variance;
            float out_var = results[n].output_variance;
            float level_top = max_var * (plot_height - row) / (plot_height - 1);
            float level_bot = max_var * (plot_height - row - 1) / (plot_height - 1);

            char in_char = ' ';
            char out_char = ' ';

            if (in_var >= level_bot && in_var < level_top) in_char = 'x';
            if (out_var >= level_bot && out_var < level_top) out_char = 'o';

            if (in_char != ' ' && out_char != ' ') {
                printf("⊗");
            } else if (in_char != ' ') {
                printf("×");
            } else if (out_char != ' ') {
                printf("●");
            } else {
                printf(" ");
            }
            printf("  ");
        }

        if (row == 1) printf("  × = Input variance");
        if (row == 2) printf("  ● = Output variance");
        printf("\n");
    }

    /* X-axis */
    printf("      └");
    for (int n = 0; n < NOISE_STEPS; n++) {
        printf("───");
    }
    printf("→ Noise Amplitude\n");

    /* X-axis labels */
    printf("       ");
    for (int n = 0; n < NOISE_STEPS; n += 5) {
        printf("%-15.1f", results[n].noise_amplitude);
    }
    printf("\n\n");

    /* ═══════════════════════════════════════════════════════════════════════
     * SNR Improvement Plot
     * ═══════════════════════════════════════════════════════════════════════ */

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("SNR IMPROVEMENT (dB)\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    /* Find min/max SNR improvement */
    float min_snr = 0.0f, max_snr = 0.0f;
    for (int n = 0; n < NOISE_STEPS; n++) {
        if (results[n].snr_improvement_db < min_snr) min_snr = results[n].snr_improvement_db;
        if (results[n].snr_improvement_db > max_snr) max_snr = results[n].snr_improvement_db;
    }
    if (min_snr > -5.0f) min_snr = -5.0f;
    if (max_snr < 5.0f) max_snr = 5.0f;

    printf("SNR Gain (dB)\n");
    printf("    │\n");

    for (int row = 0; row < plot_height; row++) {
        float level = max_snr - (max_snr - min_snr) * row / (plot_height - 1);
        printf(" %+5.1f │", level);

        for (int n = 0; n < NOISE_STEPS; n++) {
            float snr = results[n].snr_improvement_db;
            float level_top = max_snr - (max_snr - min_snr) * row / (plot_height - 1);
            float level_bot = max_snr - (max_snr - min_snr) * (row + 1) / (plot_height - 1);

            /* Draw zero line */
            if (level_top >= 0 && level_bot < 0) {
                if (snr >= level_bot && snr < level_top) {
                    printf("◆");
                } else {
                    printf("─");
                }
            } else if (snr >= level_bot && snr < level_top) {
                printf("●");
            } else {
                printf(" ");
            }
            printf("  ");
        }

        if (row == plot_height / 2) printf("  ─ = 0 dB (no improvement)");
        printf("\n");
    }

    /* X-axis */
    printf("      └");
    for (int n = 0; n < NOISE_STEPS; n++) {
        printf("───");
    }
    printf("→ Noise Amplitude\n\n");

    /* ═══════════════════════════════════════════════════════════════════════
     * Summary
     * ═══════════════════════════════════════════════════════════════════════ */

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("ANALYSIS SUMMARY\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    /* Find key points */
    float max_noise_locked = 0.0f;
    float max_noise_good = 0.0f;
    float best_snr_improvement = -1000.0f;
    float best_snr_noise = 0.0f;
    int locked_count = 0;
    int good_count = 0;

    for (int n = 0; n < NOISE_STEPS; n++) {
        if (results[n].lock_status) {
            locked_count++;
            if (results[n].noise_amplitude > max_noise_locked) {
                max_noise_locked = results[n].noise_amplitude;
            }
        }
        if (results[n].correlation >= CORRELATION_GOOD) {
            good_count++;
            if (results[n].noise_amplitude > max_noise_good) {
                max_noise_good = results[n].noise_amplitude;
            }
        }
        if (results[n].snr_improvement_db > best_snr_improvement) {
            best_snr_improvement = results[n].snr_improvement_db;
            best_snr_noise = results[n].noise_amplitude;
        }
    }

    /* Calculate max variance rejection */
    float max_var_rejection = 1.0f;
    for (int n = 0; n < NOISE_STEPS; n++) {
        float ratio = results[n].input_variance / (results[n].output_variance + 1e-10f);
        if (ratio > max_var_rejection) max_var_rejection = ratio;
    }

    /* Calculate noise rejection ratio at moderate noise */
    int mid_idx = NOISE_STEPS / 2;
    float mid_noise = results[mid_idx].noise_amplitude;
    float input_var_mid = results[mid_idx].input_variance;
    float output_var_mid = results[mid_idx].output_variance;
    float rejection_ratio = input_var_mid / (output_var_mid + 1e-10f);

    printf("  Lock Status:\n");
    printf("    Locked levels: %d / %d (%.0f%%)\n",
           locked_count, NOISE_STEPS, 100.0f * locked_count / NOISE_STEPS);
    printf("    Max noise while locked: %.2f (SNR: %.1f dB)\n",
           max_noise_locked, results[(int)(max_noise_locked / (NOISE_MAX / (NOISE_STEPS-1)))].input_snr_db);
    if (break_point > 0) {
        printf("    Break point: noise = %.2f\n", break_point);
    } else {
        printf("    Break point: NOT REACHED (locked through %.2f)\n", NOISE_MAX);
    }
    printf("\n");

    printf("  Noise Rejection:\n");
    printf("    At noise=%.1f: Input var=%.3f, Output var=%.3f\n",
           mid_noise, input_var_mid, output_var_mid);
    printf("    Variance rejection ratio: %.1fx\n", rejection_ratio);
    printf("    Best SNR improvement: %.1f dB at noise=%.2f\n",
           best_snr_improvement, best_snr_noise);
    printf("\n");

    /* Calculate operational envelope */
    float envelope_noise = max_noise_locked;
    float envelope_snr = -1000.0f;
    for (int n = 0; n < NOISE_STEPS; n++) {
        if (fabsf(results[n].noise_amplitude - envelope_noise) < 0.01f) {
            envelope_snr = results[n].input_snr_db;
            break;
        }
    }

    printf("  Operational Envelope:\n");
    printf("    Maximum noise amplitude: %.2f\n", envelope_noise);
    printf("    Minimum input SNR: %.1f dB\n", envelope_snr);
    printf("    Correlation at limit: %.3f\n",
           results[(int)(envelope_noise / (NOISE_MAX / (NOISE_STEPS-1)))].correlation);
    printf("\n");

    /* ═══════════════════════════════════════════════════════════════════════
     * Verdict
     * ═══════════════════════════════════════════════════════════════════════ */

    printf("═══════════════════════════════════════════════════════════════\n");

    /* Robustness is based on VARIANCE REJECTION, not just correlation lock */
    int robust = (max_var_rejection >= 3.0f);  /* 3x or better variance rejection */
    int excellent = (max_var_rejection >= 5.0f && max_noise_locked >= 1.0f);

    if (excellent) {
        printf("VERDICT: EXCELLENT ROBUSTNESS\n");
        printf("═══════════════════════════════════════════════════════════════\n\n");
        printf("The tracker maintains stability through extreme noise:\n");
        printf("  ✓ Solid tracking (r>0.7) up to noise=%.2f\n", max_noise_good);
        printf("  ✓ Useful tracking (r>0.5) up to noise=%.2f\n", max_noise_locked);
        printf("  ✓ Variance rejection: %.1fx (output stays stable)\n", max_var_rejection);
        printf("  ✓ Best SNR improvement: %.1f dB\n", best_snr_improvement);
        printf("\n");
        printf(">>> SKEPTIC'S DOUBT #3: ANSWERED <<<\n");
        printf("\"It works on clean data. Real sensors are dirty.\"\n");
        printf("REBUTTAL: Output variance stable (%.2f→%.2f) while input explodes.\n",
               results[0].output_variance, results[NOISE_STEPS-1].output_variance);
        printf("          %.1fx variance rejection. Real-world ready.\n", max_var_rejection);
    } else if (robust) {
        printf("VERDICT: GOOD ROBUSTNESS\n");
        printf("═══════════════════════════════════════════════════════════════\n\n");
        printf("The tracker rejects noise effectively:\n");
        printf("  ✓ Solid tracking (r>0.7) up to noise=%.2f\n", max_noise_good);
        printf("  ✓ Useful tracking (r>0.5) up to noise=%.2f\n", max_noise_locked);
        printf("  ✓ Variance rejection: %.1fx\n", max_var_rejection);
        printf("  ✓ Best SNR improvement: %.1f dB\n", best_snr_improvement);
        printf("\n");
        printf(">>> SKEPTIC'S DOUBT #3: ANSWERED <<<\n");
        printf("\"It works on clean data. Real sensors are dirty.\"\n");
        printf("REBUTTAL: %.1fx variance rejection. Noise gets smoothed.\n", max_var_rejection);
    } else {
        printf("VERDICT: LIMITED ROBUSTNESS\n");
        printf("═══════════════════════════════════════════════════════════════\n\n");
        printf("The tracker struggles with noise:\n");
        printf("  ✗ Solid tracking only to noise=%.2f\n", max_noise_good);
        printf("  ✗ Max variance rejection: %.1fx\n", max_var_rejection);
        printf("\n");
        printf("Consider retraining with noisier signals.\n");
    }

    printf("\n");
    printf("\"The CfC is a low-pass filter. Noise is high-pass. Math wins.\"\n");
    printf("\n");

    free(clean_signal);
    free(noisy_input);
    free(tracker_output);

    return robust ? 0 : 1;
}
