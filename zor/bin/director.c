/*
 * director.c — EntroMorphic OS Director (Grid Viewer)
 *
 * "The Read-Only Nervous System"
 *
 * The Director is the visualization layer. It reads the shared memory
 * segment populated by the Sentinels and renders a real-time 3x3 grid
 * showing the state of the nervous system.
 *
 * Compile:
 *   gcc -O3 -I../include director.c -o director -lncurses -lrt
 *
 * Usage:
 *   ./director
 *
 * Controls:
 *   q - Quit
 *
 * Display:
 *   GREEN  - STATUS_OK: Normal tracking
 *   YELLOW - STATUS_WARN: Drift detected
 *   RED    - STATUS_CRIT: ZIT! Anomaly detected
 *   GREY   - STATUS_OFFLINE: No heartbeat
 */

#include <ncurses.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#include "../include/shm_protocol.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * DISPLAY CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════ */

#define CELL_HEIGHT 8
#define CELL_WIDTH  26
#define REFRESH_MS  100  /* 10 Hz refresh */

/* Color pairs */
#define COLOR_OK      1
#define COLOR_WARN    2
#define COLOR_CRIT    3
#define COLOR_OFFLINE 4
#define COLOR_HEADER  5

/* ═══════════════════════════════════════════════════════════════════════════
 * GLOBALS
 * ═══════════════════════════════════════════════════════════════════════════ */

static GridMemory *grid = NULL;
static uint64_t last_heartbeat[GRID_SIZE] = {0};
static int dead_count[GRID_SIZE] = {0};

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
 * DRAWING
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_box(int y, int x, int h, int w, int color) {
    attron(COLOR_PAIR(color));

    /* Corners */
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);

    /* Horizontal lines */
    for (int i = 1; i < w - 1; i++) {
        mvaddch(y, x + i, ACS_HLINE);
        mvaddch(y + h - 1, x + i, ACS_HLINE);
    }

    /* Vertical lines */
    for (int i = 1; i < h - 1; i++) {
        mvaddch(y + i, x, ACS_VLINE);
        mvaddch(y + i, x + w - 1, ACS_VLINE);
    }

    attroff(COLOR_PAIR(color));
}

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

static void draw_square(int row, int col) {
    int idx = row * GRID_COLS + col;
    int y = row * CELL_HEIGHT + 2;  /* +2 for header */
    int x = col * CELL_WIDTH;

    SquareState *s = &grid->squares[idx];
    int alive = is_alive(idx);

    /* Determine color */
    int color = COLOR_OFFLINE;
    if (alive) {
        switch (s->status) {
            case STATUS_OK:   color = COLOR_OK;   break;
            case STATUS_WARN: color = COLOR_WARN; break;
            case STATUS_CRIT: color = COLOR_CRIT; break;
            default:          color = COLOR_OFFLINE; break;
        }
    }

    /* Draw box */
    draw_box(y, x, CELL_HEIGHT, CELL_WIDTH, color);

    /* Draw content */
    attron(COLOR_PAIR(color));

    if (!alive) {
        /* Offline square */
        mvprintw(y + 3, x + 8, "[OFFLINE]");
        mvprintw(y + 4, x + 6, "Square %d", idx);
    } else {
        /* Active square */
        mvprintw(y + 1, x + 2, "%-14s", s->label);
        mvprintw(y + 2, x + 2, "RAW: %8.2f", s->raw_value);
        mvprintw(y + 3, x + 2, "NRM: %8.4f", s->norm_value);
        mvprintw(y + 4, x + 2, "TRK: %8.4f", s->tracker);
        mvprintw(y + 5, x + 2, "DLT: %8.4f", s->delta);

        /* Zit indicator */
        if (s->status == STATUS_CRIT) {
            attron(A_BOLD | A_BLINK);
            mvprintw(y + 6, x + 6, ">> ZIT <<");
            attroff(A_BOLD | A_BLINK);
        } else if (s->status == STATUS_WARN) {
            mvprintw(y + 6, x + 6, "~ drift ~");
        }
    }

    attroff(COLOR_PAIR(color));
}

static void draw_header(void) {
    attron(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
    mvprintw(0, 0, "╔══════════════════════════════════════════════════════════════════════════════╗");
    mvprintw(1, 0, "║       EntroMorphic OS v1.0  —  The Read-Only Nervous System                  ║");
    attroff(A_BOLD);
    attroff(COLOR_PAIR(COLOR_HEADER));
}

static void draw_footer(void) {
    int footer_y = GRID_ROWS * CELL_HEIGHT + 3;

    attron(COLOR_PAIR(COLOR_HEADER));
    mvprintw(footer_y, 0,
             "╚══════════════════════════════════════════════════════════════════════════════╝");
    mvprintw(footer_y + 1, 2,
             "Active: %d/9 | Zits: %lu | Uptime: %lu ticks | 'q' to quit",
             __builtin_popcount(grid->active_mask),
             grid->total_zits,
             grid->uptime_ticks);
    attroff(COLOR_PAIR(COLOR_HEADER));
}

static void draw_grid(void) {
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            draw_square(r, c);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

static void wait_for_shm(void) {
    printf("EntroMorphic OS Director v1.0\n\n");
    printf("Waiting for Sentinels to create shared memory...\n");
    printf("(Start at least one Sentinel first)\n\n");

    while (init_shm_reader() != 0) {
        printf(".");
        fflush(stdout);
        sleep(1);
    }
    printf("\nConnected!\n");
    sleep(1);
}

int main(void) {
    /* Wait for shared memory to exist */
    if (init_shm_reader() != 0) {
        wait_for_shm();
    }

    /* Initialize ncurses */
    initscr();
    start_color();
    cbreak();
    noecho();
    curs_set(0);
    timeout(REFRESH_MS);
    keypad(stdscr, TRUE);

    /* Setup color pairs */
    init_pair(COLOR_OK,      COLOR_GREEN,  COLOR_BLACK);
    init_pair(COLOR_WARN,    COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_CRIT,    COLOR_WHITE,  COLOR_RED);
    init_pair(COLOR_OFFLINE, COLOR_WHITE,  COLOR_BLACK);
    init_pair(COLOR_HEADER,  COLOR_CYAN,   COLOR_BLACK);

    /* Main loop */
    int ch;
    while ((ch = getch()) != 'q' && ch != 'Q') {
        clear();
        draw_header();
        draw_grid();
        draw_footer();
        refresh();
    }

    /* Cleanup */
    endwin();

    if (grid != NULL) {
        munmap((void *)grid, sizeof(GridMemory));
    }

    printf("Director shutdown.\n");
    return 0;
}
