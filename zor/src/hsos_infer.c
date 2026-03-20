/*
 * hsos_infer.c — Bridge: HSOS distributed inference
 *
 * Connects hsos_system_t (messaging kernel) with trix_chip_t (inference),
 * making inference a traceable, replayable HSOS computation.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../include/hsos.h"
#include "../include/trixc/hsos_infer.h"
#include "../include/trixc/runtime.h"

/* Maximum ticks to wait for all OP_COMPUTE_OK replies.
 * ~3× worst-case round-trip: 7 frags × 8 workers × burst factor. */
#define HSOS_INFER_COLLECT_TIMEOUT 500

/* ── shard context ─────────────────────────────────────────────────────────── */

typedef struct {
    trix_chip_t *chip;
    int shard_start;
    int shard_count;
} hsos_shard_ctx_t;

/* ── worker compute callback ──────────────────────────────────────────────── */

static void hsos_infer_worker_compute(const uint8_t *input, uint8_t input_len,
                                      void *ctx,
                                      hsos_compute_result_t *out) {
    (void)input_len;
    hsos_shard_ctx_t *shard = (hsos_shard_ctx_t *)ctx;

    out->match     = -1;
    out->distance  = 512;  /* Max Hamming distance for 512-bit patterns */
    out->threshold = 0;
    memset(out->label, 0, sizeof(out->label));

    for (int i = shard->shard_start;
         i < shard->shard_start + shard->shard_count; i++) {

        const uint8_t *sig = trix_signature(shard->chip, i);
        if (!sig) continue;

        int threshold = trix_threshold(shard->chip, i);

        /* Hamming distance via XOR + popcount (Kernighan method) */
        int dist = 0;
        for (int b = 0; b < 64; b++) {
            uint8_t x = input[b] ^ sig[b];
            while (x) { dist++; x &= x - 1; }
        }

        if (dist <= threshold && dist < (int)out->distance) {
            out->match     = (int16_t)i;
            out->distance  = (int16_t)dist;
            out->threshold = (int16_t)threshold;

            const char *lbl = trix_label(shard->chip, i);
            memset(out->label, 0, sizeof(out->label));
            if (lbl) {
                strncpy(out->label, lbl, sizeof(out->label) - 1);
            }
        }
    }
}

/* ── aggregation loop ─────────────────────────────────────────────────────── */

static trix_result_t aggregate_replies(hsos_system_t *sys,
                                       trix_chip_t   *chip,
                                       int            active_workers,
                                       uint32_t       tick_start) {
    trix_result_t best;
    best.match          = -1;
    best.distance       = 512;  /* Match trix_infer sentinel */
    best.threshold      = 0;
    best.label          = NULL;
    best.trace_tick_start = tick_start;
    best.trace_tick_end   = 0;

    int replies  = 0;
    int timeout  = 0;

    while (replies < active_workers && timeout < HSOS_INFER_COLLECT_TIMEOUT) {
        hsos_step_workers(sys);
        timeout++;

        hsos_msg_t msg;
        while (hsos_recv(&sys->master, &msg)) {
            if (msg.type != OP_COMPUTE_OK) continue;

            hsos_compute_result_t r;
            memcpy(&r, msg.payload, sizeof(r));

            if (r.match >= 0 && (int)r.distance < best.distance) {
                best.match     = r.match;
                best.distance  = r.distance;
                best.threshold = r.threshold;
                best.label     = trix_label(chip, r.match);
            }
            replies++;
        }
    }

    best.trace_tick_end = sys->tick;
    return best;
}

/* ── public API ───────────────────────────────────────────────────────────── */

trix_result_t hsos_exec_infer(hsos_system_t *sys,
                               trix_chip_t   *chip,
                               const uint8_t  input[64]) {
    trix_result_t no_result;
    no_result.match          = -1;
    no_result.distance       = 512;  /* Match trix_infer sentinel */
    no_result.threshold      = 0;
    no_result.label          = NULL;
    no_result.trace_tick_start = 0;
    no_result.trace_tick_end   = 0;

    if (!sys || !chip || !input) return no_result;

    const trix_chip_info_t *info = trix_info(chip);
    if (!info) return no_result;
    int n = info->num_signatures;
    if (n <= 0) return no_result;

    /* hsos_compute_result_t.match is int16_t — cap to prevent silent wrap */
    if (n > INT16_MAX) {
        n = INT16_MAX;
    }

    uint32_t tick_start = sys->tick;

    /* Count active workers */
    int active = 0;
    for (int i = 0; i < 8; i++) {
        if (sys->workers[i].state != NODE_OFFLINE &&
            sys->workers[i].state != NODE_HALTED) active++;
    }
    if (active == 0) {
        return (trix_result_t){-1, (int)INT16_MAX, 0, NULL, tick_start, tick_start};
    }

    /* Drain residual messages from worker inboxes before sending new fragments.
     * Worker inboxes are 15-slot ring buffers; any stale messages from boot
     * or prior calls would silently displace incoming fragments. */
    {
        int drain = 0;
        while (hsos_step_workers(sys) && drain < 64) drain++;
    }

    /* Discard any stale OP_COMPUTE_OK (or other) messages left in master's
     * inbox from a prior timed-out call, before we send new work. */
    {
        hsos_msg_t stale;
        while (hsos_recv(&sys->master, &stale)) { /* discard */ }
    }

    /* Don't create empty shards */
    if (active > n) active = n;

    /* Allocate shard contexts */
    hsos_shard_ctx_t *ctxs = calloc((size_t)active, sizeof(hsos_shard_ctx_t));
    if (!ctxs) return no_result;

    /* Assign shards to workers */
    int per_worker = n / active;
    int remainder  = n % active;
    int sig_idx = 0, assigned = 0;

    for (int i = 0; i < 8 && assigned < active; i++) {
        hsos_node_t *w = &sys->workers[i];
        if (w->state == NODE_OFFLINE || w->state == NODE_HALTED) continue;

        int count = per_worker + (assigned < remainder ? 1 : 0);
        ctxs[assigned].chip        = chip;
        ctxs[assigned].shard_start = sig_idx;
        ctxs[assigned].shard_count = count;
        w->compute_fn  = hsos_infer_worker_compute;
        w->compute_ctx = &ctxs[assigned];
        sig_idx  += count;
        assigned++;
    }

    /* Send OP_COMPUTE to each assigned worker.
     * A 64-byte payload requires 7 fragment messages.  The master outbox
     * has only 15 usable slots (HSOS_QUEUE_SIZE - 1), so we must flush after each
     * worker to avoid overflow. */
    for (int i = 0; i < 8; i++) {
        if (!sys->workers[i].compute_fn) continue;
        uint8_t dst = sys->workers[i].node_id;
        hsos_send_fragmented(&sys->master, OP_COMPUTE, dst, input, 64);
        /* Flush master outbox → worker inboxes (and let workers process) */
        hsos_step_workers(sys);
    }

    /* Collect replies */
    trix_result_t result = aggregate_replies(sys, chip, active, tick_start);

    /* Cleanup: reset worker compute state */
    for (int i = 0; i < 8; i++) {
        sys->workers[i].compute_fn  = NULL;
        sys->workers[i].compute_ctx = NULL;
    }
    free(ctxs);

    return result;
}
