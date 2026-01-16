/*
 * fuse_box.c — Universal Chip Validation Harness
 *
 * "The Smart Fuse Tester"
 *
 * This harness tests any forged chip against raw data without writing
 * custom code for each chip. The chip header is included at compile time.
 *
 * Compile:
 *   gcc -O3 -DCHIP=MOTOR_CHIP -include motor_chip.h fuse_box.c -o fuse_test -lm
 *
 * Or use the Makefile target:
 *   make fuse CHIP=motor_chip
 *
 * Usage:
 *   ./fuse_test raw_data.csv [threshold]
 *
 * Output:
 *   - Per-sample predictions and anomaly scores
 *   - Summary statistics (correlation, detection rate)
 *   - Zit alerts when threshold exceeded
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * PREPROCESSOR MAGIC — Generic Chip Interface
 *
 * The CHIP macro is defined at compile time (e.g., -DCHIP=MOTOR_CHIP)
 * We use token pasting to call the correct functions.
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifndef CHIP
#error "Must define CHIP macro (e.g., -DCHIP=MOTOR_CHIP)"
#endif

/* Token pasting helpers */
#define PASTE(a, b) a##_##b
#define EXPAND_PASTE(a, b) PASTE(a, b)

/* Chip interface macros */
#define CHIP_STEP(raw, state)   EXPAND_PASTE(CHIP, step)(raw, state)
#define CHIP_RESET(state)       EXPAND_PASTE(CHIP, reset)(state)
#define CHIP_REGS               EXPAND_PASTE(CHIP, REGS)
#define CHIP_AGC_MEAN           EXPAND_PASTE(CHIP, AGC_MEAN)
#define CHIP_AGC_STD            EXPAND_PASTE(CHIP, AGC_STD)
#define CHIP_FITNESS            EXPAND_PASTE(CHIP, FITNESS)

/* String conversion for display */
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define CHIP_NAME_STR TOSTRING(CHIP)

/* ═══════════════════════════════════════════════════════════════════════════
 * ANOMALY DETECTION — The Second Star Constant
 * ═══════════════════════════════════════════════════════════════════════════ */

#define DEFAULT_THRESHOLD 0.1122911624f  /* The Second Star */

/* ═══════════════════════════════════════════════════════════════════════════
 * CSV LOADING
 * ═══════════════════════════════════════════════════════════════════════════ */

#define MAX_SAMPLES 65536

static float raw_data[MAX_SAMPLES];
static int sample_count = 0;

int load_csv(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s'\n", filename);
        return -1;
    }

    char line[1024];
    sample_count = 0;

    /* Skip header if non-numeric */
    if (fgets(line, sizeof(line), f)) {
        char c = line[0];
        if (c == '-' || c == '.' || (c >= '0' && c <= '9')) {
            float val;
            if (sscanf(line, "%*f,%f", &val) == 1 || sscanf(line, "%f", &val) == 1) {
                raw_data[sample_count++] = val;
            }
        }
    }

    while (fgets(line, sizeof(line), f) && sample_count < MAX_SAMPLES) {
        float val;
        if (sscanf(line, "%*f,%f", &val) == 1 || sscanf(line, "%f", &val) == 1) {
            raw_data[sample_count++] = val;
        }
    }

    fclose(f);
    return sample_count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * STATISTICS
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    float min_delta;
    float max_delta;
    float sum_delta;
    float sum_delta_sq;
    int zit_count;
    int sample_count;

    /* For correlation */
    float sum_x;
    float sum_y;
    float sum_xy;
    float sum_x2;
    float sum_y2;
} Stats;

void stats_init(Stats* s) {
    memset(s, 0, sizeof(Stats));
    s->min_delta = 1e30f;
    s->max_delta = -1e30f;
}

void stats_update(Stats* s, float input, float prediction, float delta, int is_zit) {
    s->sample_count++;

    if (delta < s->min_delta) s->min_delta = delta;
    if (delta > s->max_delta) s->max_delta = delta;
    s->sum_delta += delta;
    s->sum_delta_sq += delta * delta;

    if (is_zit) s->zit_count++;

    /* For correlation (input vs prediction) */
    s->sum_x += input;
    s->sum_y += prediction;
    s->sum_xy += input * prediction;
    s->sum_x2 += input * input;
    s->sum_y2 += prediction * prediction;
}

float stats_correlation(Stats* s) {
    float n = (float)s->sample_count;
    float num = n * s->sum_xy - s->sum_x * s->sum_y;
    float den1 = n * s->sum_x2 - s->sum_x * s->sum_x;
    float den2 = n * s->sum_y2 - s->sum_y * s->sum_y;
    float den = sqrtf(fabsf(den1) * fabsf(den2));
    return (den > 0.001f) ? (num / den) : 0.0f;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

void print_usage(const char* prog) {
    printf("\n");
    printf("THE FUSE BOX — Universal Chip Tester\n");
    printf("\"Does your chip detect anomalies?\"\n\n");
    printf("Usage: %s <data.csv> [threshold] [--quiet]\n\n", prog);
    printf("Options:\n");
    printf("  threshold  Detection threshold (default: %.10f)\n", DEFAULT_THRESHOLD);
    printf("  --quiet    Only show summary, no per-sample output\n\n");
    printf("Chip loaded: %s\n", CHIP_NAME_STR);
    printf("  Registers: %d\n", CHIP_REGS);
    printf("  AGC Mean:  %.4f\n", CHIP_AGC_MEAN);
    printf("  AGC Std:   %.4f\n", CHIP_AGC_STD);
    printf("\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* csv_file = argv[1];
    float threshold = DEFAULT_THRESHOLD;
    int quiet = 0;

    /* Parse optional args */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        } else {
            threshold = atof(argv[i]);
        }
    }

    /* Load data */
    int samples = load_csv(csv_file);
    if (samples < 0) return 1;

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  THE FUSE BOX — Universal Chip Tester                        ║\n");
    printf("║  \"Does your chip detect anomalies?\"                          ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    printf("Chip:      %s\n", CHIP_NAME_STR);
    printf("Data:      %s (%d samples)\n", csv_file, samples);
    printf("Threshold: %.10f\n", threshold);
    printf("AGC:       mean=%.4f, std=%.4f\n", CHIP_AGC_MEAN, CHIP_AGC_STD);
    printf("\n");

    /* Initialize chip state */
    float state[CHIP_REGS];
    CHIP_RESET(state);

    /* Run simulation */
    Stats stats;
    stats_init(&stats);

    if (!quiet) {
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("   Time |      Raw |     Norm |   Output |    Delta | Status\n");
        printf("═══════════════════════════════════════════════════════════════\n");
    }

    /* Warmup period (let chip stabilize) */
    int warmup = samples > 100 ? 50 : 10;

    for (int t = 0; t < samples; t++) {
        float raw = raw_data[t];

        /* Normalize (same as chip does internally) */
        float norm = (raw - CHIP_AGC_MEAN) / CHIP_AGC_STD;

        /* Execute chip */
        float output = CHIP_STEP(raw, state);

        /* Anomaly score */
        float delta = fabsf(norm - output);

        /* Is this a Zit? (only after warmup) */
        int is_zit = (t >= warmup) && (delta > threshold);

        /* Update stats (only after warmup) */
        if (t >= warmup) {
            stats_update(&stats, norm, output, delta, is_zit);
        }

        /* Output */
        if (!quiet) {
            /* Always show first 10, last 10, and any Zits */
            int show = (t < 10) || (t >= samples - 10) || is_zit;

            /* Also show every 100th sample */
            if (t % 100 == 0) show = 1;

            if (show) {
                const char* status = "     ";
                if (t < warmup) {
                    status = "WARM ";
                } else if (is_zit) {
                    status = ">>ZIT";
                }

                printf("  %5d | %8.2f | %8.4f | %8.4f | %8.4f | %s\n",
                       t, raw, norm, output, delta, status);
            }
        }
    }

    if (!quiet) {
        printf("═══════════════════════════════════════════════════════════════\n");
    }

    /* Summary */
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("SUMMARY\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    int effective_samples = stats.sample_count;
    float mean_delta = stats.sum_delta / effective_samples;
    float var_delta = (stats.sum_delta_sq / effective_samples) - (mean_delta * mean_delta);
    float std_delta = sqrtf(var_delta);
    float correlation = stats_correlation(&stats);

    printf("Samples analyzed: %d (after %d warmup)\n", effective_samples, warmup);
    printf("\n");
    printf("Delta Statistics:\n");
    printf("  Min:  %.6f\n", stats.min_delta);
    printf("  Max:  %.6f\n", stats.max_delta);
    printf("  Mean: %.6f\n", mean_delta);
    printf("  Std:  %.6f\n", std_delta);
    printf("\n");
    printf("Detection:\n");
    printf("  Threshold: %.10f\n", threshold);
    printf("  Zits:      %d (%.2f%%)\n", stats.zit_count,
           100.0f * stats.zit_count / effective_samples);
    printf("\n");
    printf("Tracking Quality:\n");
    printf("  Correlation (input vs output): %.4f\n", correlation);

    /* Verdict */
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");

    if (stats.zit_count == 0) {
        printf("VERDICT: ALL CLEAR — No anomalies detected\n");
        printf("         The chip tracked the signal within threshold.\n");
    } else if (stats.zit_count < effective_samples * 0.05) {
        printf("VERDICT: MINOR ANOMALIES — %d Zits detected (%.1f%%)\n",
               stats.zit_count, 100.0f * stats.zit_count / effective_samples);
        printf("         Occasional deviations from learned pattern.\n");
    } else if (stats.zit_count < effective_samples * 0.20) {
        printf("VERDICT: SIGNIFICANT ANOMALIES — %d Zits detected (%.1f%%)\n",
               stats.zit_count, 100.0f * stats.zit_count / effective_samples);
        printf("         Pattern has changed from training data.\n");
    } else {
        printf("VERDICT: SYSTEM FAILURE — %d Zits detected (%.1f%%)\n",
               stats.zit_count, 100.0f * stats.zit_count / effective_samples);
        printf("         Data completely unlike training distribution.\n");
    }

    printf("═══════════════════════════════════════════════════════════════\n\n");

    return (stats.zit_count > 0) ? 1 : 0;
}
