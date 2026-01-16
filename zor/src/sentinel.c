/*
 * sentinel.c — EntroMorphic OS Sentinel Daemon
 *
 * "The Read-Only Nervous System"
 *
 * Each Sentinel is a perception agent that:
 *   1. Reads a sensor value
 *   2. Normalizes it via Auto-Gain Control (AGC)
 *   3. Runs the V3 CfC Liquid Chip (396 bytes of physics)
 *   4. Detects anomalies using the Second Star Constant
 *   5. Writes state to shared memory for the Director to visualize
 *
 * Compile:
 *   gcc -O3 -I../include sentinel.c -o sentinel -lrt -lm
 *
 * Usage:
 *   ./sentinel <ID 0-8> <LABEL> [--mock]
 *
 * Example (launch the grid):
 *   ./sentinel 0 "CPU_0" --mock &
 *   ./sentinel 1 "CPU_1" --mock &
 *   ./sentinel 4 "NET_TX" --mock &
 *   ./sentinel 8 "MEM_FREE" --mock &
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include "shm_protocol.h"
#include "../foundry/seeds/sine_seed.h"  /* The V3 CfC Chip */

/* ═══════════════════════════════════════════════════════════════════════════
 * GLOBALS
 * ═══════════════════════════════════════════════════════════════════════════ */

static GridMemory *grid = NULL;
static int my_id = -1;
static volatile int running = 1;

/* ═══════════════════════════════════════════════════════════════════════════
 * SIGNAL HANDLER — Graceful Shutdown
 * ═══════════════════════════════════════════════════════════════════════════ */

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * AUTO GAIN CONTROL (AGC)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Normalizes sensor readings to approximately [-1, 1] range using
 * exponential moving average of mean and variance.
 *
 * This allows the V3 chip to work with any sensor scale.
 */

typedef struct {
    float mean;
    float variance;
    float alpha;    /* Adaptation rate (smaller = slower adaptation) */
    int warmup;     /* Samples before normalization kicks in */
    int count;
} AGC;

static void agc_init(AGC *ctx) {
    ctx->mean = 0.0f;
    ctx->variance = 1.0f;
    ctx->alpha = 0.01f;  /* Slow adaptation */
    ctx->warmup = 50;    /* 50 samples before normalizing */
    ctx->count = 0;
}

static float agc_process(AGC *ctx, float raw) {
    ctx->count++;

    /* Update running mean */
    float diff = raw - ctx->mean;
    ctx->mean += ctx->alpha * diff;

    /* Update running variance */
    ctx->variance = (1.0f - ctx->alpha) * ctx->variance +
                    (ctx->alpha * diff * diff);

    /* Compute standard deviation with floor */
    float std = sqrtf(ctx->variance);
    if (std < 0.001f) std = 0.001f;

    /* Return normalized value (z-score) */
    if (ctx->count < ctx->warmup) {
        /* During warmup, just center around 0 */
        return (raw - ctx->mean) / (std + 1.0f);
    }

    return (raw - ctx->mean) / std;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SHARED MEMORY LINK
 * ═══════════════════════════════════════════════════════════════════════════ */

static int init_shm(int id, const char *label) {
    /* Open or create shared memory segment */
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open");
        return -1;
    }

    /* Set size */
    if (ftruncate(fd, sizeof(GridMemory)) == -1) {
        perror("ftruncate");
        close(fd);
        return -1;
    }

    /* Map into address space */
    grid = (GridMemory *)mmap(NULL, sizeof(GridMemory),
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED, fd, 0);
    if (grid == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return -1;
    }

    close(fd);  /* fd no longer needed after mmap */

    /* Initialize our square */
    my_id = id;
    SquareState *s = &grid->squares[my_id];

    memset(s, 0, sizeof(SquareState));
    strncpy(s->label, label, sizeof(s->label) - 1);
    s->label[sizeof(s->label) - 1] = '\0';
    s->status = STATUS_OK;

    /* Mark ourselves as active */
    grid->active_mask |= (1 << my_id);
    grid->version = SHM_PROTOCOL_VERSION;

    return 0;
}

static void cleanup_shm(void) {
    if (grid != NULL && my_id >= 0) {
        /* Mark ourselves as inactive */
        grid->active_mask &= ~(1 << my_id);
        grid->squares[my_id].status = STATUS_OFFLINE;

        munmap(grid, sizeof(GridMemory));
        grid = NULL;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MOCK SENSORS
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * For demonstration, we generate synthetic sensor data.
 * In production, these would read from /proc, sysfs, etc.
 */

static float read_mock_sensor(const char *label, int t) {
    /* Base signal: smooth sinusoidal variation */
    float base = sinf(t * 0.05f) * 20.0f + 50.0f;

    /* Add noise */
    float noise = ((rand() % 100) / 50.0f) - 1.0f;

    /* Inject anomalies based on label */
    float anomaly = 0.0f;

    if (strcmp(label, "NET_TX") == 0) {
        /* Square 4: Network TX spikes every ~10 seconds */
        if ((t % 100) > 95) {
            anomaly = 80.0f;  /* Spike! */
        }
    } else if (strcmp(label, "MEM_FREE") == 0) {
        /* Square 8: Memory leak (gradual drift) */
        if (t > 200 && t < 300) {
            anomaly = -0.3f * (t - 200);  /* Slow drift down */
        }
    } else if (strcmp(label, "DISK_IO") == 0) {
        /* Square 7: Burst I/O */
        if ((t % 150) > 140) {
            anomaly = 60.0f;
        }
    }

    return base + noise + anomaly;
}

/* Real sensor reading (Linux-specific) */
static float read_cpu_load(void) {
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return 0.0f;
    float load;
    if (fscanf(f, "%f", &load) != 1) load = 0.0f;
    fclose(f);
    return load * 100.0f;  /* Scale to percentage-ish */
}

static float read_mem_free(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0.0f;
    char line[256];
    long mem_free = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemFree:", 8) == 0) {
            sscanf(line + 8, "%ld", &mem_free);
            break;
        }
    }
    fclose(f);
    return (float)mem_free / 1024.0f;  /* MB */
}

static float read_sensor(const char *label, int t, int use_mock) {
    if (use_mock) {
        return read_mock_sensor(label, t);
    }

    /* Real sensors */
    if (strncmp(label, "CPU", 3) == 0) {
        return read_cpu_load();
    } else if (strcmp(label, "MEM_FREE") == 0) {
        return read_mem_free();
    }

    /* Fallback to mock */
    return read_mock_sensor(label, t);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN LOOP
 * ═══════════════════════════════════════════════════════════════════════════ */

static void print_usage(const char *prog) {
    printf("EntroMorphic OS Sentinel v1.0\n\n");
    printf("Usage: %s <ID 0-8> <LABEL> [--mock]\n\n", prog);
    printf("Arguments:\n");
    printf("  ID      Square position (0-8) in the 3x3 grid\n");
    printf("  LABEL   Sensor name (e.g., CPU_0, NET_TX, MEM_FREE)\n");
    printf("  --mock  Use mock sensors instead of real system data\n\n");
    printf("Example:\n");
    printf("  %s 0 CPU_0 --mock &\n", prog);
    printf("  %s 4 NET_TX --mock &\n", prog);
    printf("  %s 8 MEM_FREE --mock &\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    int id = atoi(argv[1]);
    if (id < 0 || id >= GRID_SIZE) {
        fprintf(stderr, "Error: ID must be 0-8\n");
        return 1;
    }

    const char *label = argv[2];
    int use_mock = (argc > 3 && strcmp(argv[3], "--mock") == 0);

    /* Setup signal handlers */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Initialize shared memory */
    if (init_shm(id, label) != 0) {
        fprintf(stderr, "Failed to initialize shared memory\n");
        return 1;
    }

    /* Initialize AGC */
    AGC agc;
    agc_init(&agc);

    /* Initialize V3 CfC Chip state */
    float chip_state[sine_SEED_REGS];
    sine_seed_reset(chip_state);

    printf(">> Sentinel %d (%s) ONLINE%s\n",
           id, label, use_mock ? " [MOCK]" : "");
    printf("   Threshold: %.10f (Second Star Constant)\n",
           THRESHOLD_SECOND_STAR);
    printf("   Update rate: %d Hz\n", 1000000 / SENTINEL_PERIOD_US);

    int t = 0;
    uint64_t zit_count = 0;

    while (running) {
        /* 1. Read sensor */
        float raw = read_sensor(label, t, use_mock);

        /* 2. Normalize via AGC */
        float norm = agc_process(&agc, raw);

        /* 3. Execute V3 CfC Chip — THE PHYSICS ENGINE */
        float tracker = sine_seed_step(norm, chip_state);

        /* 4. Compute anomaly score */
        float delta = fabsf(norm - tracker);

        /* 5. Determine status using thresholds */
        int status = STATUS_OK;
        if (delta > THRESHOLD_SECOND_STAR) {
            status = STATUS_CRIT;
            zit_count++;
            grid->total_zits++;
        } else if (delta > THRESHOLD_DRIFT) {
            status = STATUS_WARN;
        }

        /* 6. Update shared memory */
        SquareState *s = &grid->squares[my_id];
        s->raw_value = raw;
        s->norm_value = norm;
        s->tracker = tracker;
        s->delta = delta;
        s->status = status;
        s->heartbeat++;

        /* Debug output on anomaly */
        if (status == STATUS_CRIT) {
            printf("[ZIT #%lu] delta=%.4f > %.4f (t=%d)\n",
                   zit_count, delta, THRESHOLD_SECOND_STAR, t);
        }

        t++;
        grid->uptime_ticks++;

        usleep(SENTINEL_PERIOD_US);
    }

    /* Cleanup */
    printf(">> Sentinel %d (%s) shutting down...\n", id, label);
    cleanup_shm();

    return 0;
}
