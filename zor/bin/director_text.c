/*
 * director_text.c — EntroMorphic OS Director (Text Mode)
 *
 * "The Read-Only Nervous System"
 *
 * Simple text-based grid viewer using ANSI escape codes.
 * No ncurses dependency - works in any terminal.
 *
 * Compile:
 *   gcc -O3 -I../include director_text.c -o director_text -lrt
 *
 * Usage:
 *   ./director_text
 *
 * Controls:
 *   Ctrl+C - Quit
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include "../include/shm_protocol.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * ANSI COLOR CODES
 * ═══════════════════════════════════════════════════════════════════════════ */

#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_RED     "\033[31m"
#define ANSI_WHITE   "\033[37m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_DIM     "\033[2m"
#define ANSI_BLINK   "\033[5m"
#define ANSI_CLEAR   "\033[2J\033[H"

/* ═══════════════════════════════════════════════════════════════════════════
 * GLOBALS
 * ═══════════════════════════════════════════════════════════════════════════ */

static GridMemory *grid = NULL;
static uint64_t last_heartbeat[GRID_SIZE] = {0};
static int dead_count[GRID_SIZE] = {0};
static volatile int running = 1;

/* ═══════════════════════════════════════════════════════════════════════════
 * SIGNAL HANDLER
 * ═══════════════════════════════════════════════════════════════════════════ */

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SHARED MEMORY
 * ═══════════════════════════════════════════════════════════════════════════ */

static int init_shm_reader(void) {
    int fd = shm_open(SHM_NAME, O_RDONLY, 0666);
    if (fd == -1) {
        return -1;  /* SHM doesn't exist yet */
    }

    grid = (GridMemory *)mmap(NULL, sizeof(GridMemory),
                               PROT_READ, MAP_SHARED, fd, 0);
    close(fd);

    if (grid == MAP_FAILED) {
        grid = NULL;
        return -1;
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * HEARTBEAT CHECK
 * ═══════════════════════════════════════════════════════════════════════════ */

static int is_alive(int idx) {
    SquareState *s = &grid->squares[idx];

    /* Check if heartbeat has changed */
    if (s->heartbeat == last_heartbeat[idx]) {
        dead_count[idx]++;
    } else {
        dead_count[idx] = 0;
        last_heartbeat[idx] = s->heartbeat;
    }

    /* Dead if no heartbeat for HEARTBEAT_TIMEOUT intervals */
    return (dead_count[idx] < HEARTBEAT_TIMEOUT);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DRAWING
 * ═══════════════════════════════════════════════════════════════════════════ */

static const char* get_color(int status, int alive) {
    if (!alive) return ANSI_DIM ANSI_WHITE;
    switch (status) {
        case STATUS_OK:   return ANSI_GREEN;
        case STATUS_WARN: return ANSI_YELLOW;
        case STATUS_CRIT: return ANSI_RED;
        default:          return ANSI_DIM ANSI_WHITE;
    }
}

static const char* get_status_symbol(int status, int alive) {
    if (!alive) return "◌";  /* Empty circle */
    switch (status) {
        case STATUS_OK:   return "●";  /* Filled circle */
        case STATUS_WARN: return "◐";  /* Half circle */
        case STATUS_CRIT: return "◉";  /* Target */
        default:          return "◌";
    }
}

static void draw_grid(void) {
    printf(ANSI_CLEAR);

    /* Header */
    printf(ANSI_BOLD ANSI_CYAN);
    printf("╔════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║         EntroMorphic OS v1.0  —  The Read-Only Nervous System              ║\n");
    printf("╚════════════════════════════════════════════════════════════════════════════╝\n");
    printf(ANSI_RESET);
    printf("\n");

    /* Grid */
    for (int row = 0; row < GRID_ROWS; row++) {
        /* Top border */
        printf("  ");
        for (int col = 0; col < GRID_COLS; col++) {
            printf("┌────────────────────────┐");
            if (col < GRID_COLS - 1) printf("  ");
        }
        printf("\n");

        /* Content lines */
        for (int line = 0; line < 5; line++) {
            printf("  ");
            for (int col = 0; col < GRID_COLS; col++) {
                int idx = row * GRID_COLS + col;
                SquareState *s = &grid->squares[idx];
                int alive = is_alive(idx);
                const char *color = get_color(s->status, alive);

                printf("│ %s", color);

                if (!alive) {
                    /* Offline display */
                    switch (line) {
                        case 0: printf("                      "); break;
                        case 1: printf("      [OFFLINE]       "); break;
                        case 2: printf("       Square %d       ", idx); break;
                        case 3: printf("                      "); break;
                        case 4: printf("                      "); break;
                    }
                } else {
                    /* Active display */
                    switch (line) {
                        case 0:
                            printf("%s %-14s %s    ",
                                   get_status_symbol(s->status, alive),
                                   s->label,
                                   s->status == STATUS_CRIT ? "!!" : "  ");
                            break;
                        case 1:
                            printf("  RAW: %8.2f      ", s->raw_value);
                            break;
                        case 2:
                            printf("  NRM: %8.4f      ", s->norm_value);
                            break;
                        case 3:
                            printf("  TRK: %8.4f      ", s->tracker);
                            break;
                        case 4:
                            if (s->status == STATUS_CRIT) {
                                printf(ANSI_BLINK "  >> ZIT <<  " ANSI_RESET "%s", color);
                                printf("Δ=%.3f   ", s->delta);
                            } else if (s->status == STATUS_WARN) {
                                printf("  ~drift~   Δ=%.4f ", s->delta);
                            } else {
                                printf("  DLT: %8.4f      ", s->delta);
                            }
                            break;
                    }
                }
                printf(ANSI_RESET " │");
                if (col < GRID_COLS - 1) printf("  ");
            }
            printf("\n");
        }

        /* Bottom border */
        printf("  ");
        for (int col = 0; col < GRID_COLS; col++) {
            printf("└────────────────────────┘");
            if (col < GRID_COLS - 1) printf("  ");
        }
        printf("\n");
        if (row < GRID_ROWS - 1) printf("\n");
    }

    /* Footer */
    printf("\n");
    printf(ANSI_CYAN);
    printf("═══════════════════════════════════════════════════════════════════════════════\n");
    printf("  Active: %d/9  │  Zits: %lu  │  Uptime: %lu ticks  │  Ctrl+C to quit\n",
           __builtin_popcount(grid->active_mask),
           grid->total_zits,
           grid->uptime_ticks);
    printf("═══════════════════════════════════════════════════════════════════════════════\n");
    printf(ANSI_RESET);

    fflush(stdout);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

static void wait_for_shm(void) {
    printf("EntroMorphic OS Director v1.0 (Text Mode)\n\n");
    printf("Waiting for Sentinels to create shared memory...\n");
    printf("(Start at least one Sentinel first)\n\n");

    while (init_shm_reader() != 0 && running) {
        printf(".");
        fflush(stdout);
        sleep(1);
    }
    if (running) {
        printf("\nConnected!\n");
        sleep(1);
    }
}

int main(void) {
    /* Setup signal handlers */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Wait for shared memory to exist */
    if (init_shm_reader() != 0) {
        wait_for_shm();
    }

    if (!running) {
        printf("\nAborted.\n");
        return 0;
    }

    /* Main loop - 10 Hz refresh */
    while (running) {
        draw_grid();
        usleep(100000);  /* 100ms */
    }

    /* Cleanup */
    printf(ANSI_CLEAR);
    printf("Director shutdown.\n");

    if (grid != NULL) {
        munmap((void *)grid, sizeof(GridMemory));
    }

    return 0;
}
