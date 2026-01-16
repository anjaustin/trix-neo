/*
 * shm_protocol.h — EntroMorphic OS Shared Memory Protocol
 *
 * "The Read-Only Nervous System"
 *
 * This header defines the shared memory structure for the Hollywood Squares
 * grid. It is the single source of truth for both the C daemons (Sentinels)
 * and the UI (Director).
 *
 * Layout: 3x3 grid = 9 squares
 *
 *   ┌───────┬───────┬───────┐
 *   │   0   │   1   │   2   │
 *   ├───────┼───────┼───────┤
 *   │   3   │   4   │   5   │
 *   ├───────┼───────┼───────┤
 *   │   6   │   7   │   8   │
 *   └───────┴───────┴───────┘
 *
 * Each square is monitored by a Sentinel process that runs the V3 CfC chip.
 * The Director visualizes the grid state in real-time.
 */

#ifndef SHM_PROTOCOL_H
#define SHM_PROTOCOL_H

#include <stdint.h>

/* Shared Memory Name (POSIX) */
#define SHM_NAME "/entromorph_grid"

/* Grid Dimensions */
#define GRID_SIZE 9
#define GRID_ROWS 3
#define GRID_COLS 3

/* ═══════════════════════════════════════════════════════════════════════════
 * STATUS CODES — Traffic Light Model
 * ═══════════════════════════════════════════════════════════════════════════ */

#define STATUS_OFFLINE  0   /* Grey  — No heartbeat */
#define STATUS_OK       1   /* Green — Normal tracking */
#define STATUS_WARN     2   /* Yellow — Drift detected */
#define STATUS_CRIT     3   /* Red   — Zit! Anomaly detected */

/* ═══════════════════════════════════════════════════════════════════════════
 * SQUARE STATE — The State of a Single Hollywood Square
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    /* Identity */
    char label[16];         /* e.g., "CPU_LOAD", "NET_TX", "MEM_FREE" */

    /* Sensor Data */
    float raw_value;        /* The actual sensor reading */
    float norm_value;       /* Normalized input via AGC (-1 to 1 range) */

    /* V3 CfC Chip Output */
    float tracker;          /* What the Liquid Chip predicted */
    float delta;            /* |input - tracker| — The Anomaly Score */

    /* Status */
    int status;             /* STATUS_OK, STATUS_WARN, STATUS_CRIT */

    /* Proof of Life */
    uint64_t heartbeat;     /* Monotonic counter, incremented each step */
    uint64_t timestamp;     /* Last update time (optional) */
} SquareState;

/* ═══════════════════════════════════════════════════════════════════════════
 * GRID MEMORY — The Entire Nervous System
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    /* The 3x3 Grid */
    SquareState squares[GRID_SIZE];

    /* Bitmask of active sentinels (bit N = sentinel N is alive) */
    uint32_t active_mask;

    /* Global Statistics */
    uint64_t total_zits;    /* Total anomalies detected across all squares */
    uint64_t uptime_ticks;  /* System uptime in ticks */

    /* Version for compatibility checking */
    uint32_t version;
} GridMemory;

/* Protocol Version */
#define SHM_PROTOCOL_VERSION 1

/* ═══════════════════════════════════════════════════════════════════════════
 * THRESHOLDS — The Second Star Constant
 * ═══════════════════════════════════════════════════════════════════════════ */

/* The optimal detection threshold for the V3 Efficient Species.
 * Achieves 4/4 perfect detection (spike, drop, noise, drift).
 * "Second star to the right, and straight on 'til morning." */
#define THRESHOLD_SECOND_STAR  0.1122911624f

/* Drift threshold (lower than Zit, for early warning) */
#define THRESHOLD_DRIFT        0.08f

/* ═══════════════════════════════════════════════════════════════════════════
 * TIMING
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Sentinel update rate (microseconds) */
#define SENTINEL_PERIOD_US     100000  /* 100ms = 10 Hz */

/* Heartbeat timeout (number of missed beats before OFFLINE) */
#define HEARTBEAT_TIMEOUT      10      /* 10 beats = 1 second */

#endif /* SHM_PROTOCOL_H */
