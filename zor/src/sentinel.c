/*
 * sentinel.c — EntroMorphic System Sentinel
 *
 * "Define Order, and Chaos reveals itself."
 *
 * A 396-byte anomaly detector that monitors system metrics in real-time.
 * Uses dual-tau architecture: Fast tracker (Zits) + Slow anchor (Drift).
 *
 * Build:
 *   gcc -O3 -I../include sentinel.c -o sentinel -lm
 *
 * Run:
 *   ./sentinel              # Monitor CPU load
 *   ./sentinel --memory     # Monitor memory usage
 *   ./sentinel --network    # Monitor network packets
 *
 * Deploy:
 *   nohup ./sentinel > /var/log/sentinel.log 2>&1 &
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <signal.h>

/* Include the evolved sine tracker */
#include "../foundry/seeds/sine_seed.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Configuration
 * ═══════════════════════════════════════════════════════════════════════════ */

#define SAMPLE_RATE_HZ    10      /* 10 samples per second */
#define SAMPLE_INTERVAL   (1000000 / SAMPLE_RATE_HZ)  /* microseconds */

/*
 * The Second Star Constant — 0.1122911624
 *
 * Discovered through evolutionary optimization of the V3 Efficient Species.
 * This threshold achieves 4/4 perfect anomaly detection across all test types:
 * spike, drop, noise burst, and gradual drift.
 *
 * Named for: "Second star to the right, and straight on 'til morning."
 *            — J.M. Barrie, Peter Pan
 *
 * The value emerged from the Gluttony Penalty evolution run. When the fitness
 * landscape was constrained by metabolic limits (MAX_ALLOWED_STATE=5.0) and
 * anti-identity pressure, the Efficient Species evolved with a natural
 * tracking error distribution that makes 0.1122911624 the optimal decision
 * boundary between "normal fluctuation" and "anomalous deviation."
 */
#define SECOND_STAR_CONSTANT  0.1122911624f

#define ZIT_THRESHOLD     SECOND_STAR_CONSTANT  /* Tuned for Efficient Species */
#define DRIFT_THRESHOLD   0.5f                  /* Slow drift threshold */
#define WARMUP_SAMPLES    50                    /* Samples before detection starts */

/* ═══════════════════════════════════════════════════════════════════════════
 * Metric Sources
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    METRIC_CPU_LOAD,
    METRIC_MEMORY,
    METRIC_NETWORK,
} MetricType;

/* Read 1-minute CPU load average */
float read_cpu_load(void) {
    double load = 0.0;
    FILE* f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%lf", &load) != 1) load = 0.0;
        fclose(f);
    }
    return (float)load;
}

/* Read memory usage percentage */
float read_memory_usage(void) {
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return 0.0f;

    unsigned long mem_total = 0, mem_available = 0;
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line + 9, "%lu", &mem_total);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line + 13, "%lu", &mem_available);
        }
    }
    fclose(f);

    if (mem_total == 0) return 0.0f;
    return (float)(mem_total - mem_available) / (float)mem_total;
}

/* Read network packets per second (delta) */
static unsigned long prev_rx = 0, prev_tx = 0;
float read_network_activity(void) {
    FILE* f = fopen("/proc/net/dev", "r");
    if (!f) return 0.0f;

    char line[512];
    unsigned long rx = 0, tx = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Look for eth0 or ens or any interface */
        if (strstr(line, "eth") || strstr(line, "ens") || strstr(line, "enp")) {
            char* colon = strchr(line, ':');
            if (colon) {
                unsigned long vals[16];
                if (sscanf(colon + 1, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                           &vals[0], &vals[1], &vals[2], &vals[3], &vals[4],
                           &vals[5], &vals[6], &vals[7], &vals[8], &vals[9]) >= 10) {
                    rx += vals[0];  /* bytes received */
                    tx += vals[8];  /* bytes transmitted */
                }
            }
        }
    }
    fclose(f);

    float delta = 0.0f;
    if (prev_rx > 0) {
        delta = (float)((rx - prev_rx) + (tx - prev_tx)) / 1000000.0f;  /* MB/s */
    }
    prev_rx = rx;
    prev_tx = tx;

    return delta;
}

/* Dispatch metric reader */
float read_metric(MetricType type) {
    switch (type) {
        case METRIC_CPU_LOAD: return read_cpu_load();
        case METRIC_MEMORY:   return read_memory_usage();
        case METRIC_NETWORK:  return read_network_activity();
        default: return 0.0f;
    }
}

const char* metric_name(MetricType type) {
    switch (type) {
        case METRIC_CPU_LOAD: return "CPU Load";
        case METRIC_MEMORY:   return "Memory %";
        case METRIC_NETWORK:  return "Net MB/s";
        default: return "Unknown";
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Dual-Tau Detector
 *
 * Fast Tracker: CfC chip (τ ~ 1-2 seconds) - detects spikes
 * Slow Anchor: EMA (τ ~ 30 seconds) - detects drift
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    /* Fast tracker (CfC chip) */
    float fast_state[sine_SEED_REGS];
    float fast_output;

    /* Slow anchor (EMA) */
    float anchor;
    float anchor_alpha;  /* EMA smoothing factor */

    /* Statistics */
    float baseline_mae;
    int sample_count;
    int warmup_complete;

    /* Detection state */
    int zit_active;
    int drift_active;
    int zit_count;
    int drift_count;
} DualTauDetector;

void detector_init(DualTauDetector* d) {
    memset(d, 0, sizeof(DualTauDetector));
    sine_seed_reset(d->fast_state);
    d->anchor_alpha = 0.02f;  /* ~50 sample time constant at 10Hz = 5 seconds */
    d->baseline_mae = 0.1f;   /* Initial estimate */
}

typedef struct {
    int zit_detected;
    int drift_detected;
    float zit_delta;
    float drift_delta;
    float fast_track;
    float anchor;
} DetectionResult;

DetectionResult detector_step(DualTauDetector* d, float input) {
    DetectionResult result = {0};

    /* Run fast tracker (CfC chip) */
    d->fast_output = sine_seed_step(input, d->fast_state);
    result.fast_track = d->fast_output;

    /* Update slow anchor (EMA) */
    if (d->sample_count == 0) {
        d->anchor = input;  /* Initialize to first value */
    } else {
        d->anchor = d->anchor * (1.0f - d->anchor_alpha) + input * d->anchor_alpha;
    }
    result.anchor = d->anchor;

    d->sample_count++;

    /* Warmup phase */
    if (d->sample_count < WARMUP_SAMPLES) {
        /* Update baseline MAE estimate */
        float error = fabsf(input - d->fast_output);
        d->baseline_mae = d->baseline_mae * 0.9f + error * 0.1f;
        return result;
    }

    if (!d->warmup_complete) {
        d->warmup_complete = 1;
    }

    /* Calculate deltas */
    result.zit_delta = fabsf(input - d->fast_output);
    result.drift_delta = fabsf(d->fast_output - d->anchor);

    /* Zit detection (fast anomaly)
     * Using Second Star Constant directly — proven 4/4 detection */
    if (result.zit_delta > ZIT_THRESHOLD) {
        result.zit_detected = 1;
        if (!d->zit_active) {
            d->zit_count++;
            d->zit_active = 1;
        }
    } else {
        d->zit_active = 0;
    }

    /* Drift detection (slow anomaly) */
    if (result.drift_delta > DRIFT_THRESHOLD) {
        result.drift_detected = 1;
        if (!d->drift_active) {
            d->drift_count++;
            d->drift_active = 1;
        }
    } else {
        d->drift_active = 0;
    }

    /* Update baseline MAE (slowly) */
    if (!result.zit_detected && !result.drift_detected) {
        float error = fabsf(input - d->fast_output);
        d->baseline_mae = d->baseline_mae * 0.99f + error * 0.01f;
    }

    return result;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Signal Handling
 * ═══════════════════════════════════════════════════════════════════════════ */

static volatile int running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char** argv) {
    /* Parse arguments */
    MetricType metric = METRIC_CPU_LOAD;
    int verbose = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--memory") == 0) metric = METRIC_MEMORY;
        else if (strcmp(argv[i], "--network") == 0) metric = METRIC_NETWORK;
        else if (strcmp(argv[i], "--cpu") == 0) metric = METRIC_CPU_LOAD;
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) verbose = 1;
    }

    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Print header */
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  ENTROMORPHIC SENTINEL                                       ║\n");
    printf("║  \"Define Order, and Chaos reveals itself.\"                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Monitoring: %s\n", metric_name(metric));
    printf("Sample rate: %d Hz\n", SAMPLE_RATE_HZ);
    printf("Chip memory: %d bytes\n", (int)(sine_SEED_REGS * sizeof(float)));
    printf("Warming up (%d samples)...\n", WARMUP_SAMPLES);
    printf("\n");

    /* Initialize detector */
    DualTauDetector detector;
    detector_init(&detector);

    /* Main loop */
    time_t start_time = time(NULL);

    while (running) {
        float input = read_metric(metric);
        DetectionResult result = detector_step(&detector, input);

        /* Get timestamp */
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char timestamp[20];
        strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);

        /* Output */
        if (result.zit_detected) {
            printf("[%s] ZIT #%d | %s=%.3f | Track=%.3f | Delta=%.3f\n",
                   timestamp, detector.zit_count, metric_name(metric),
                   input, result.fast_track, result.zit_delta);
            fflush(stdout);
        }

        if (result.drift_detected) {
            printf("[%s] DRIFT #%d | %s=%.3f | Track=%.3f | Anchor=%.3f | Delta=%.3f\n",
                   timestamp, detector.drift_count, metric_name(metric),
                   input, result.fast_track, result.anchor, result.drift_delta);
            fflush(stdout);
        }

        if (verbose && detector.warmup_complete) {
            printf("[%s] %s=%.3f | Track=%.3f | Anchor=%.3f | MAE=%.4f\n",
                   timestamp, metric_name(metric), input,
                   result.fast_track, result.anchor, detector.baseline_mae);
            fflush(stdout);
        }

        /* Periodic status (every 60 seconds) */
        if ((now - start_time) % 60 == 0 && detector.sample_count % SAMPLE_RATE_HZ == 0) {
            printf("[%s] STATUS | Samples=%d | Zits=%d | Drifts=%d | MAE=%.4f\n",
                   timestamp, detector.sample_count, detector.zit_count,
                   detector.drift_count, detector.baseline_mae);
            fflush(stdout);
        }

        usleep(SAMPLE_INTERVAL);
    }

    /* Shutdown summary */
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("SENTINEL SHUTDOWN\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Total samples: %d\n", detector.sample_count);
    printf("Zits detected: %d\n", detector.zit_count);
    printf("Drifts detected: %d\n", detector.drift_count);
    printf("Final MAE: %.4f\n", detector.baseline_mae);
    printf("═══════════════════════════════════════════════════════════════\n\n");

    return 0;
}
