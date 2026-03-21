/*
 * hsos.c — Hollywood Squares OS Implementation
 *
 * "Structure is meaning."
 *
 * Port of anjaustin/hollywood-squares-os to frozen C.
 * Deterministic message passing + bounded local semantics +
 * enforced observability ⇒ global convergence with inherited correctness.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../include/hsos.h"

#if HSOS_PARALLEL
#include <pthread.h>
#endif

#define HSOS_DEFAULT_RECORD_CAP  4096

/* ═══════════════════════════════════════════════════════════════════════════
 * MESSAGE HELPERS
 * ═══════════════════════════════════════════════════════════════════════════ */

static void msg_clear(hsos_msg_t *msg) {
    memset(msg, 0, sizeof(hsos_msg_t));
}

void hsos_msg_ping(hsos_msg_t *msg, uint8_t src, uint8_t dst) {
    msg_clear(msg);
    msg->type = OP_PING;
    msg->src = src;
    msg->dst = dst;
    msg->len = 0;
    msg->flags = MSG_FLAG_ACK_REQ;
}

void hsos_msg_exec(hsos_msg_t *msg, uint8_t src, uint8_t dst,
                   hsos_alu_op_t op, uint8_t a, uint8_t b) {
    msg_clear(msg);
    msg->type = OP_EXEC;
    msg->src = src;
    msg->dst = dst;
    msg->len = 3;
    msg->payload[0] = (uint8_t)op;
    msg->payload[1] = a;
    msg->payload[2] = b;
}

void hsos_msg_cswap(hsos_msg_t *msg, uint8_t src, uint8_t dst,
                    uint8_t value, uint8_t ascending) {
    msg_clear(msg);
    msg->type = OP_CSWAP;
    msg->src = src;
    msg->dst = dst;
    msg->len = 2;
    msg->payload[0] = value;
    msg->payload[1] = ascending;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * NODE KERNEL — Queue Operations
 * ═══════════════════════════════════════════════════════════════════════════ */

static bool inbox_empty(hsos_node_t *node) {
    return node->inbox_head == node->inbox_tail;
}

static bool outbox_empty(hsos_node_t *node) {
    return node->outbox_head == node->outbox_tail;
}

static bool inbox_full(hsos_node_t *node) {
    return ((node->inbox_tail + 1) % HSOS_QUEUE_SIZE) == node->inbox_head;
}

static bool outbox_full(hsos_node_t *node) {
    return ((node->outbox_tail + 1) % HSOS_QUEUE_SIZE) == node->outbox_head;
}

static void inbox_enqueue(hsos_node_t *node, const hsos_msg_t *msg) {
    if (!inbox_full(node)) {
        node->inbox[node->inbox_tail] = *msg;
        node->inbox_tail = (node->inbox_tail + 1) % HSOS_QUEUE_SIZE;
    }
}

static void outbox_enqueue(hsos_node_t *node, const hsos_msg_t *msg) {
    if (!outbox_full(node)) {
        node->outbox[node->outbox_tail] = *msg;
        node->outbox_tail = (node->outbox_tail + 1) % HSOS_QUEUE_SIZE;
    }
}

static bool inbox_dequeue(hsos_node_t *node, hsos_msg_t *msg) {
    if (inbox_empty(node)) return false;
    *msg = node->inbox[node->inbox_head];
    node->inbox_head = (node->inbox_head + 1) % HSOS_QUEUE_SIZE;
    return true;
}

static bool outbox_dequeue(hsos_node_t *node, hsos_msg_t *msg) {
    if (outbox_empty(node)) return false;
    *msg = node->outbox[node->outbox_head];
    node->outbox_head = (node->outbox_head + 1) % HSOS_QUEUE_SIZE;
    return true;
}

void hsos_send(hsos_node_t *node, hsos_msg_t *msg) {
    msg->src = node->node_id;
    msg->seq = node->seq_tx++;
    outbox_enqueue(node, msg);
    node->msgs_tx++;
}

bool hsos_recv(hsos_node_t *node, hsos_msg_t *msg) {
    if (inbox_dequeue(node, msg)) {
        node->seq_rx = msg->seq;
        node->msgs_rx++;
        return true;
    }
    return false;
}

void hsos_send_fragmented(hsos_node_t *node, uint8_t type, uint8_t dst,
                          const uint8_t *data, uint8_t len) {
    uint8_t offset = 0;
    while (offset < len) {
        hsos_msg_t frag;
        msg_clear(&frag);
        frag.type = type;
        frag.dst  = dst;

        uint8_t chunk = len - offset;
        if (chunk > HSOS_PAYLOAD_MAX) chunk = HSOS_PAYLOAD_MAX;

        memcpy(frag.payload, data + offset, chunk);
        frag.len = chunk;

        frag.flags = MSG_FLAG_FRAGMENT;
        if (offset + chunk >= len) {
            frag.flags |= MSG_FLAG_LAST_FRAG;
        }

        hsos_send(node, &frag);
        offset += chunk;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * THREAD POOL — Parallel worker tick
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Safety: within a tick, workers only read their own inbox and write their
 * own outbox. No cross-worker state sharing. Bus delivery (serial) runs
 * after all workers complete. This makes parallel worker steps safe.
 */

#if HSOS_PARALLEL

/* Forward declaration — node_step is defined later in this file */
static bool node_step(hsos_node_t *node);

static void *worker_thread_fn(void *arg) {
    /* Use the embedded arg struct — no pointer-packing needed */
    struct hsos_worker_arg *wa = (struct hsos_worker_arg *)arg;
    hsos_system_t *sys  = wa->sys;
    int            widx = wa->widx;

    while (1) {
        /* Wait for work or shutdown signal */
        pthread_mutex_lock(&sys->worker_mutex);
        while (sys->worker_target[widx] == NULL && !sys->worker_shutdown) {
            pthread_cond_wait(&sys->worker_start_cond, &sys->worker_mutex);
        }

        if (sys->worker_shutdown) {
            pthread_mutex_unlock(&sys->worker_mutex);
            return NULL;
        }

        hsos_node_t *target = sys->worker_target[widx];
        sys->worker_target[widx] = NULL;  /* Claim the work */
        pthread_mutex_unlock(&sys->worker_mutex);

        /* Execute node step — no locks needed: inbox/outbox are per-node.
         * Store the return value so parallel_step_workers() can faithfully
         * replicate the work_done |= node_step() semantics of the sequential path. */
        wa->step_done = node_step(target);

        /* Signal completion */
        pthread_mutex_lock(&sys->worker_mutex);
        sys->worker_pending--;
        if (sys->worker_pending == 0) {
            pthread_cond_signal(&sys->worker_done_cond);
        }
        pthread_mutex_unlock(&sys->worker_mutex);
    }
}

void hsos_parallel_init(hsos_system_t *sys) {
    pthread_mutex_init(&sys->worker_mutex, NULL);
    pthread_cond_init(&sys->worker_start_cond, NULL);
    pthread_cond_init(&sys->worker_done_cond, NULL);
    sys->worker_pending = 0;
    sys->worker_shutdown = 0;
    memset(sys->worker_target, 0, sizeof(sys->worker_target));

    for (int i = 0; i < 8; i++) {
        /* Initialize embedded arg struct — no alignment assumption needed */
        sys->worker_args[i].sys  = sys;
        sys->worker_args[i].widx = i;
        pthread_create(&sys->worker_threads[i], NULL, worker_thread_fn,
                       &sys->worker_args[i]);
    }
}

void hsos_parallel_destroy(hsos_system_t *sys) {
    pthread_mutex_lock(&sys->worker_mutex);
    sys->worker_shutdown = 1;
    pthread_cond_broadcast(&sys->worker_start_cond);
    pthread_mutex_unlock(&sys->worker_mutex);

    for (int i = 0; i < 8; i++) {
        pthread_join(sys->worker_threads[i], NULL);
    }
    pthread_mutex_destroy(&sys->worker_mutex);
    pthread_cond_destroy(&sys->worker_start_cond);
    pthread_cond_destroy(&sys->worker_done_cond);
}

/* Dispatch all 8 workers in parallel, block until all complete.
 * Returns the OR of all node_step() return values, faithfully replicating the
 * work_done |= node_step() semantics of the sequential path so hsos_run()
 * quiescence detection is unaffected by parallelism. */
static bool parallel_step_workers(hsos_system_t *sys) {
    pthread_mutex_lock(&sys->worker_mutex);
    for (int i = 0; i < 8; i++) {
        sys->worker_args[i].step_done = false;
        sys->worker_target[i] = &sys->workers[i];
    }
    sys->worker_pending = 8;
    pthread_cond_broadcast(&sys->worker_start_cond);
    while (sys->worker_pending > 0) {
        pthread_cond_wait(&sys->worker_done_cond, &sys->worker_mutex);
    }
    bool any_work = false;
    for (int i = 0; i < 8; i++)
        any_work |= sys->worker_args[i].step_done;
    pthread_mutex_unlock(&sys->worker_mutex);
    return any_work;
}

#else  /* HSOS_PARALLEL == 0 */

void hsos_parallel_init(hsos_system_t *sys) { (void)sys; }
void hsos_parallel_destroy(hsos_system_t *sys) { (void)sys; }

#endif /* HSOS_PARALLEL */

/* ═══════════════════════════════════════════════════════════════════════════
 * NODE KERNEL — ALU Operations
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t result;
    uint8_t flags;  /* Bit 0: carry/overflow */
} alu_result_t;

static alu_result_t alu_execute(hsos_node_t *node, hsos_alu_op_t op,
                                uint8_t a, uint8_t b) {
    alu_result_t r = {0, 0};
    uint16_t tmp;

    switch (op) {
        case ALU_ADD:
            tmp = (uint16_t)a + (uint16_t)b;
            r.result = tmp & 0xFF;
            r.flags = (tmp > 255) ? 1 : 0;
            break;

        case ALU_SUB:
            tmp = (uint16_t)a - (uint16_t)b;
            r.result = tmp & 0xFF;
            r.flags = (a < b) ? 1 : 0;  /* Borrow */
            break;

        case ALU_CMP:
            r.result = (a == b) ? 0 : ((a > b) ? 1 : 255);
            r.flags = 0;
            break;

        case ALU_AND:
            r.result = a & b;
            break;

        case ALU_OR:
            r.result = a | b;
            break;

        case ALU_XOR:
            r.result = a ^ b;
            break;

        case ALU_NOT:
            r.result = ~a;
            break;

        case ALU_SHIFT_L:
            r.result = a << (b & 7);
            break;

        case ALU_SHIFT_R:
            r.result = a >> (b & 7);
            break;

        case ALU_PEEK:
            r.result = node->memory[a];  /* Read memory[a] */
            break;

        case ALU_POKE:
            node->memory[a] = b;         /* Write b to memory[a] */
            r.result = b;
            break;

        case ALU_COPY:
            r.result = node->memory[a];
            node->memory[b] = r.result;  /* Copy memory[a] to memory[b] */
            break;

        default:
            r.result = 0;
            r.flags = 0xFF;  /* Unknown op */
            break;
    }

    return r;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * NODE KERNEL — Message Handlers
 * ═══════════════════════════════════════════════════════════════════════════ */

static void handle_ping(hsos_node_t *node, const hsos_msg_t *msg) {
    hsos_msg_t reply;
    msg_clear(&reply);
    reply.type = OP_PONG;
    reply.dst = msg->src;
    reply.seq = msg->seq;  /* Echo sequence */
    hsos_send(node, &reply);
}

static void handle_exec(hsos_node_t *node, const hsos_msg_t *msg) {
    hsos_msg_t reply;
    msg_clear(&reply);

    if (msg->len >= 3) {
        hsos_alu_op_t op = (hsos_alu_op_t)msg->payload[0];
        uint8_t a = msg->payload[1];
        uint8_t b = msg->payload[2];

        alu_result_t r = alu_execute(node, op, a, b);

        reply.type = OP_EXEC_OK;
        reply.dst = msg->src;
        reply.seq = msg->seq;
        reply.len = 2;
        reply.payload[0] = r.result;
        reply.payload[1] = r.flags;
    } else {
        reply.type = OP_EXEC_ERR;
        reply.dst = msg->src;
        reply.seq = msg->seq;
        reply.len = 1;
        reply.payload[0] = 0x01;  /* Bad payload */
        node->errors++;
    }

    hsos_send(node, &reply);
}

static void handle_cswap(hsos_node_t *node, const hsos_msg_t *msg) {
    /* Compare-swap for bubble machine
     *
     * Protocol:
     *   A sends: CSWAP(value_a, ascending)
     *   B receives, compares, and:
     *     - If swap needed: B takes value_a, sends value_b back
     *     - If no swap: B keeps value_b, sends value_a back (no change)
     *   A receives: CSWAP_OK(returned_value, swapped)
     *     - A always sets its value to returned_value
     */
    hsos_msg_t reply;
    msg_clear(&reply);

    uint8_t their_value = msg->payload[0];  /* A's value */
    uint8_t ascending = msg->payload[1];
    uint8_t my_value = node->memory[0];     /* B's value */

    reply.type = OP_CSWAP_OK;
    reply.dst = msg->src;
    reply.seq = msg->seq;
    reply.len = 2;

    /* For ascending sort: smaller values should go to lower-numbered nodes
     * A (lower node) sends to B (higher node)
     * If B < A: swap (B takes A's larger value, A gets B's smaller value)
     */
    bool should_swap = ascending ?
        (my_value < their_value) :   /* Ascending: swap if B is smaller than A */
        (my_value > their_value);    /* Descending: swap if B is bigger than A */

    if (should_swap) {
        /* Exchange values */
        node->memory[0] = their_value;   /* B takes A's value */
        reply.payload[0] = my_value;     /* A gets B's old value */
        reply.payload[1] = 1;            /* Swapped flag */
    } else {
        /* No swap - A keeps its value */
        reply.payload[0] = their_value;  /* Return A's value unchanged */
        reply.payload[1] = 0;            /* No swap */
    }

    hsos_send(node, &reply);
}

static void handle_cswap_ok(hsos_node_t *node, const hsos_msg_t *msg) {
    /* Response from compare-swap - update our value */
    node->memory[0] = msg->payload[0];
}

static void handle_compute(hsos_node_t *node, const hsos_msg_t *msg) {
    hsos_msg_t reply;
    msg_clear(&reply);
    reply.type = OP_COMPUTE_OK;
    reply.dst  = msg->src;
    reply.seq  = msg->seq;
    reply.len  = sizeof(hsos_compute_result_t);

    hsos_compute_result_t result;
    memset(&result, 0, sizeof(result));
    result.match    = -1;
    result.distance = INT16_MAX;

    if (node->compute_fn && node->frag_len == HSOS_FRAG_BUF_SIZE) {
        node->compute_fn(node->frag_buf, node->frag_len,
                         node->compute_ctx, &result);
    } else {
        node->errors++;
    }

    memcpy(reply.payload, &result, sizeof(result));
    hsos_send(node, &reply);
}

static void handle_domain_ops(hsos_node_t *node, const hsos_msg_t *msg) {
    /* Constraint field domain operations */
    hsos_msg_t reply;
    msg_clear(&reply);

    /* Domain stored in memory[0-1] as 16-bit little-endian */
    uint16_t domain = node->memory[0] | ((uint16_t)node->memory[1] << 8);

    switch (msg->type) {
        case OP_DOMAIN_GET:
            reply.type = OP_DOMAIN_GET;  /* Same opcode for reply */
            reply.dst = msg->src;
            reply.len = 2;
            reply.payload[0] = domain & 0xFF;
            reply.payload[1] = (domain >> 8) & 0xFF;
            break;

        case OP_DOMAIN_SET:
            domain = msg->payload[0] | ((uint16_t)msg->payload[1] << 8);
            node->memory[0] = domain & 0xFF;
            node->memory[1] = (domain >> 8) & 0xFF;
            reply.type = OP_DOMAIN_SET;
            reply.dst = msg->src;
            reply.len = 0;
            break;

        case OP_DOMAIN_DELTA: {
            /* Remove values from domain */
            uint16_t remove = msg->payload[0] | ((uint16_t)msg->payload[1] << 8);
            uint16_t old_domain = domain;
            domain &= ~remove;
            node->memory[0] = domain & 0xFF;
            node->memory[1] = (domain >> 8) & 0xFF;

            /* Count remaining bits (entropy) */
            uint8_t entropy = 0;
            for (uint16_t d = domain; d; d >>= 1) entropy += (d & 1);

            reply.type = OP_DOMAIN_DELTA;
            reply.dst = msg->src;
            reply.len = 2;
            reply.payload[0] = (domain != old_domain) ? 1 : 0;  /* Changed? */
            reply.payload[1] = entropy;
            break;
        }

        case OP_IS_SINGLETON: {
            /* Check if exactly one bit set */
            uint8_t count = 0;
            for (uint16_t d = domain; d; d >>= 1) count += (d & 1);
            reply.type = OP_IS_SINGLETON;
            reply.dst = msg->src;
            reply.len = 1;
            reply.payload[0] = (count == 1) ? 1 : 0;
            break;
        }

        case OP_GET_VALUE: {
            /* Return the single value (1-9) if singleton, else 0 */
            uint8_t count = 0, value = 0;
            for (uint16_t d = domain, v = 1; d; d >>= 1, v++) {
                if (d & 1) { count++; value = v; }
            }
            reply.type = OP_GET_VALUE;
            reply.dst = msg->src;
            reply.len = 1;
            reply.payload[0] = (count == 1) ? value : 0;
            break;
        }

        default:
            return;  /* Unknown domain op */
    }

    hsos_send(node, &reply);
}

static void handle_reset(hsos_node_t *node, const hsos_msg_t *msg) {
    (void)msg;
    node->state = NODE_IDLE;
    node->inbox_head = node->inbox_tail = 0;
    node->outbox_head = node->outbox_tail = 0;
    memset(node->memory, 0, sizeof(node->memory));
    node->errors = 0;
    node->load = 0;
}

static void handle_halt(hsos_node_t *node, const hsos_msg_t *msg) {
    (void)msg;
    node->state = NODE_HALTED;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * NODE KERNEL — Main Step Function
 * ═══════════════════════════════════════════════════════════════════════════ */

static bool node_step(hsos_node_t *node) {
    if (node->state == NODE_HALTED || node->state == NODE_OFFLINE) {
        return false;
    }

    hsos_msg_t msg;
    if (!hsos_recv(node, &msg)) {
        return false;  /* No messages */
    }

    node->state = NODE_BUSY;

    /* Fragment reassembly — accumulate before dispatch */
    if (msg.flags & MSG_FLAG_FRAGMENT) {
        if (node->frag_active) {
            if (msg.src != node->frag_src || msg.type != node->frag_type) {
                /* New sender/type arrived mid-reassembly — discard stale buffer */
                node->frag_len    = 0;
                node->frag_active = false;
            }
        }
        if (!node->frag_active) {
            /* Begin new reassembly */
            node->frag_active = true;
            node->frag_src    = msg.src;
            node->frag_type   = msg.type;
            node->frag_len    = 0;
        }

        uint8_t space = HSOS_FRAG_BUF_SIZE - node->frag_len;
        uint8_t chunk = (msg.len < space) ? msg.len : space;
        memcpy(node->frag_buf + node->frag_len, msg.payload, chunk);
        node->frag_len += chunk;

        if (!(msg.flags & MSG_FLAG_LAST_FRAG)) {
            /* More fragments coming — stay idle, wait */
            node->state = NODE_IDLE;
            return true;
        }

        /* Last fragment received — promote to a dispatchable message.
         * Overwrite msg fields so the dispatch switch below sees a
         * complete message; handlers read node->frag_buf for payload. */
        node->frag_active = false;
        msg.src   = node->frag_src;
        msg.type  = node->frag_type;
        msg.len   = node->frag_len;
        msg.flags = 0;
        /* payload field is intentionally stale here; handlers must use
         * node->frag_buf when processing fragmented message types. */
    }

    /* Dispatch by message type */
    switch (msg.type) {
        case OP_PING:       handle_ping(node, &msg); break;
        case OP_PONG:       /* Response, handled at fabric level */ break;
        case OP_EXEC:       handle_exec(node, &msg); break;
        case OP_EXEC_OK:    /* Response */ break;
        case OP_EXEC_ERR:   /* Response */ break;
        case OP_RESET:      handle_reset(node, &msg); break;
        case OP_HALT:       handle_halt(node, &msg); break;
        case OP_CSWAP:      handle_cswap(node, &msg); break;
        case OP_CSWAP_OK:   handle_cswap_ok(node, &msg); break;
        case OP_COMPUTE:    handle_compute(node, &msg); break;
        case OP_COMPUTE_OK: /* Collected by bridge via hsos_recv(master) */ break;
        case OP_DOMAIN_GET:
        case OP_DOMAIN_SET:
        case OP_DOMAIN_DELTA:
        case OP_IS_SINGLETON:
        case OP_GET_VALUE:  handle_domain_ops(node, &msg); break;
        default:
            node->errors++;
            break;
    }

    /* Update load based on queue depth */
    uint8_t inbox_depth = (node->inbox_tail + HSOS_QUEUE_SIZE - node->inbox_head)
                          % HSOS_QUEUE_SIZE;
    node->load = inbox_depth * 16;  /* Scale to 0-255 */

    node->state = NODE_IDLE;
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * NODE INITIALIZATION
 * ═══════════════════════════════════════════════════════════════════════════ */

static void node_init(hsos_node_t *node, uint8_t id) {
    memset(node, 0, sizeof(hsos_node_t));
    node->node_id = id;
    node->state = NODE_OFFLINE;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MESSAGE BUS — Star Topology Routing
 * ═══════════════════════════════════════════════════════════════════════════ */

static hsos_node_t* get_node(hsos_system_t *sys, uint8_t id) {
    if (id == 0) return &sys->master;
    if (id >= 1 && id <= 8) return &sys->workers[id - 1];
    return NULL;
}

static void bus_deliver(hsos_system_t *sys) {
    /* Collect all outgoing messages and route them */

    /* From master */
    hsos_msg_t msg;
    while (outbox_dequeue(&sys->master, &msg)) {
        if (msg.flags & MSG_FLAG_BROADCAST) {
            /* Broadcast to all workers */
            for (int i = 0; i < 8; i++) {
                if (sys->workers[i].state != NODE_OFFLINE) {
                    inbox_enqueue(&sys->workers[i], &msg);
                    sys->bus_delivered++;
                }
            }
        } else {
            hsos_node_t *dst = get_node(sys, msg.dst);
            if (dst && dst->state != NODE_OFFLINE) {
                inbox_enqueue(dst, &msg);
                sys->bus_delivered++;
            } else {
                sys->bus_dropped++;
            }
        }

        /* Record if recording, but not during replay */
        if (sys->recording && !sys->replay_mode &&
                sys->record_count < sys->record_capacity) {
            sys->record_buffer[sys->record_count++] = msg;
        }
    }

    /* From workers (star topology: all go through master or direct) */
    for (int i = 0; i < 8; i++) {
        while (outbox_dequeue(&sys->workers[i], &msg)) {
            hsos_node_t *dst = get_node(sys, msg.dst);
            if (dst && dst->state != NODE_OFFLINE) {
                inbox_enqueue(dst, &msg);
                sys->bus_delivered++;
            } else {
                sys->bus_dropped++;
            }

            if (sys->recording && !sys->replay_mode &&
                    sys->record_count < sys->record_capacity) {
                sys->record_buffer[sys->record_count++] = msg;
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TRACE BUFFER
 * ═══════════════════════════════════════════════════════════════════════════ */

static void trace_add(hsos_system_t *sys, uint8_t node_id, uint8_t msg_type,
                      uint8_t e0, uint8_t e1) {
    hsos_trace_entry_t *e = &sys->trace[sys->trace_head];
    e->tick = sys->tick;
    e->node_id = node_id;
    e->msg_type = msg_type;
    e->extra[0] = e0;
    e->extra[1] = e1;
    sys->trace_head = (sys->trace_head + 1) % HSOS_TRACE_SIZE;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FABRIC KERNEL — Directory & Router
 * ═══════════════════════════════════════════════════════════════════════════ */

static void fabric_update_directory(hsos_system_t *sys) {
    for (int i = 1; i <= 8; i++) {
        hsos_node_t *w = &sys->workers[i - 1];
        hsos_directory_entry_t *d = &sys->fabric.directory[i];
        d->status = w->state;
        d->load = w->load;
        d->msgs_total = w->msgs_rx + w->msgs_tx;
        d->errors_total = w->errors;
    }
}

static uint8_t fabric_route_least_loaded(hsos_system_t *sys) {
    uint8_t best = 0;
    uint8_t best_load = 255;

    for (int i = 1; i <= 8; i++) {
        hsos_directory_entry_t *d = &sys->fabric.directory[i];
        if (d->status == NODE_IDLE && d->load < best_load) {
            best = i;
            best_load = d->load;
        }
    }

    /* Fallback: round-robin among idle nodes */
    if (best == 0) {
        for (int j = 0; j < 8; j++) {
            uint8_t idx = (sys->fabric.round_robin_idx + j) % 8 + 1;
            if (sys->fabric.directory[idx].status == NODE_IDLE) {
                best = idx;
                sys->fabric.round_robin_idx = idx;
                break;
            }
        }
    }

    return best;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SYSTEM API
 * ═══════════════════════════════════════════════════════════════════════════ */

void hsos_init_with_capacity(hsos_system_t *sys, uint16_t record_capacity) {
    memset(sys, 0, sizeof(hsos_system_t));

    /* Initialize master */
    node_init(&sys->master, 0);
    sys->master.state = NODE_IDLE;

    /* Initialize workers */
    for (int i = 0; i < 8; i++) {
        node_init(&sys->workers[i], i + 1);
    }

    /* Initialize fabric */
    sys->fabric.heartbeat_interval = 256;
    sys->fabric.heartbeat_timeout = 1024;

    /* Allocate record buffer */
    sys->record_capacity = record_capacity;
    sys->record_buffer = (hsos_msg_t *)calloc(record_capacity,
                                               sizeof(hsos_msg_t));
    /* record_buffer may be NULL if capacity is 0 or alloc fails;
     * recording will be silently disabled in that case */

    sys->booted = false;
    sys->tick = 0;

#if HSOS_PARALLEL
    hsos_parallel_init(sys);
#endif
}

void hsos_init(hsos_system_t *sys) {
    hsos_init_with_capacity(sys, HSOS_DEFAULT_RECORD_CAP);
}

void hsos_system_free(hsos_system_t *sys) {
#if HSOS_PARALLEL
    hsos_parallel_destroy(sys);
#endif
    if (sys && sys->record_buffer) {
        free(sys->record_buffer);
        sys->record_buffer = NULL;
        sys->record_capacity = 0;
    }
}

int hsos_boot(hsos_system_t *sys) {
    int online_count = 0;

    /* Bring all workers online */
    for (int i = 0; i < 8; i++) {
        sys->workers[i].state = NODE_IDLE;

        /* Send ping */
        hsos_msg_t ping;
        hsos_msg_ping(&ping, 0, i + 1);
        hsos_send(&sys->master, &ping);
    }

    /* Process boot sequence */
    for (int tick = 0; tick < 100; tick++) {
        bus_deliver(sys);
        for (int i = 0; i < 8; i++) {
            node_step(&sys->workers[i]);
        }
        node_step(&sys->master);
        bus_deliver(sys);
    }

    /* Count online workers */
    for (int i = 0; i < 8; i++) {
        if (sys->workers[i].state == NODE_IDLE) {
            sys->fabric.directory[i + 1].status = NODE_IDLE;
            sys->fabric.directory[i + 1].capabilities = CAP_BASIC | CAP_MEMORY;
            sys->fabric.directory[i + 1].last_heartbeat = sys->tick;
            online_count++;
        }
    }

    sys->booted = true;
    return online_count;
}

bool hsos_step(hsos_system_t *sys) {
    bool work_done = false;

    sys->tick++;

    /* Step master */
    work_done |= node_step(&sys->master);

    /* Step workers */
#if HSOS_PARALLEL
    work_done |= parallel_step_workers(sys);
#else
    for (int i = 0; i < 8; i++) {
        work_done |= node_step(&sys->workers[i]);
    }
#endif

    /* Route messages */
    bus_deliver(sys);

    /* Update fabric directory */
    fabric_update_directory(sys);
    sys->fabric.tick = sys->tick;

    return work_done;
}

bool hsos_step_workers(hsos_system_t *sys) {
    bool work_done = false;

    sys->tick++;

#if HSOS_PARALLEL
    work_done |= parallel_step_workers(sys);
#else
    for (int i = 0; i < 8; i++) {
        work_done |= node_step(&sys->workers[i]);
    }
#endif

    bus_deliver(sys);
    fabric_update_directory(sys);
    sys->fabric.tick = sys->tick;

    return work_done;
}

int hsos_run(hsos_system_t *sys, int max_ticks) {
    int ticks = 0;
    while (ticks < max_ticks && hsos_step(sys)) {
        ticks++;
    }
    /* Run a few extra ticks to drain queues */
    for (int i = 0; i < 10; i++) {
        hsos_step(sys);
        ticks++;
    }
    return ticks;
}

int hsos_exec(hsos_system_t *sys, uint8_t node, hsos_alu_op_t op,
              uint8_t a, uint8_t b) {
    hsos_msg_t msg;
    hsos_msg_exec(&msg, 0, node, op, a, b);
    hsos_send(&sys->master, &msg);

    /* Run until response or timeout */
    for (int t = 0; t < 100; t++) {
        hsos_step(sys);

        /* Check master inbox for response */
        hsos_msg_t resp;
        if (hsos_recv(&sys->master, &resp)) {
            if (resp.type == OP_EXEC_OK && resp.src == node) {
                return resp.payload[0];  /* Return result */
            } else if (resp.type == OP_EXEC_ERR) {
                return -1;  /* Error */
            }
        }
    }

    return -2;  /* Timeout */
}

void hsos_broadcast_exec(hsos_system_t *sys, hsos_alu_op_t op,
                         uint8_t a, uint8_t b) {
    hsos_msg_t msg;
    hsos_msg_exec(&msg, 0, 0, op, a, b);
    msg.flags |= MSG_FLAG_BROADCAST;
    hsos_send(&sys->master, &msg);
    hsos_run(sys, 50);
}

int hsos_route_exec(hsos_system_t *sys, hsos_alu_op_t op,
                    uint8_t a, uint8_t b, uint8_t *assigned_node) {
    uint8_t node = fabric_route_least_loaded(sys);
    if (node == 0) {
        if (assigned_node) *assigned_node = 0;
        return -3;  /* No available node */
    }
    if (assigned_node) *assigned_node = node;
    return hsos_exec(sys, node, op, a, b);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RECORDING & REPLAY
 * ═══════════════════════════════════════════════════════════════════════════ */

void hsos_start_recording(hsos_system_t *sys) {
    sys->recording = true;
    sys->record_count = 0;
}

int hsos_stop_recording(hsos_system_t *sys) {
    sys->recording = false;
    return sys->record_count;
}

void hsos_replay(hsos_system_t *sys) {
    /* Reset all nodes */
    for (int i = 0; i < 8; i++) {
        hsos_msg_t reset_msg;
        msg_clear(&reset_msg);
        reset_msg.type = OP_RESET;
        reset_msg.dst = i + 1;
        inbox_enqueue(&sys->workers[i], &reset_msg);
    }
    hsos_run(sys, 20);

    /* Replay recorded messages through the bus so routing, counters, and
     * any bus-level logic execute identically to the original run.
     * replay_mode suppresses re-recording of the replayed messages. */
    sys->replay_mode = true;
    uint16_t count = sys->record_count;

    for (uint16_t i = 0; i < count; i++) {
        hsos_msg_t *msg = &sys->record_buffer[i];
        hsos_node_t *src = get_node(sys, msg->src);
        if (src) {
            outbox_enqueue(src, msg);
        }
        /* Step once per message to preserve original delivery ordering */
        hsos_step(sys);
    }

    hsos_run(sys, 1000);
    sys->replay_mode = false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * INTROSPECTION
 * ═══════════════════════════════════════════════════════════════════════════ */

hsos_node_state_t hsos_node_status(hsos_system_t *sys, uint8_t node) {
    hsos_node_t *n = get_node(sys, node);
    return n ? n->state : NODE_OFFLINE;
}

bool hsos_ping(hsos_system_t *sys, uint8_t node) {
    hsos_msg_t ping;
    hsos_msg_ping(&ping, 0, node);
    hsos_send(&sys->master, &ping);

    for (int t = 0; t < 50; t++) {
        hsos_step(sys);
        hsos_msg_t resp;
        if (hsos_recv(&sys->master, &resp)) {
            if (resp.type == OP_PONG && resp.src == node) {
                return true;
            }
        }
    }
    return false;
}

void hsos_stats(hsos_system_t *sys, uint32_t *ticks, uint32_t *delivered,
                uint32_t *dropped) {
    if (ticks) *ticks = sys->tick;
    if (delivered) *delivered = sys->bus_delivered;
    if (dropped) *dropped = sys->bus_dropped;
}

void hsos_dump_trace(hsos_system_t *sys) {
    printf("\n═══ HSOS TRACE ═══\n");
    printf("Tick     Node  Type  Extra\n");
    printf("───────────────────────────\n");

    for (int i = 0; i < HSOS_TRACE_SIZE; i++) {
        int idx = (sys->trace_head + i) % HSOS_TRACE_SIZE;
        hsos_trace_entry_t *e = &sys->trace[idx];
        if (e->tick == 0 && e->node_id == 0) continue;
        printf("%8u  %3u   0x%02X  %02X %02X\n",
               e->tick, e->node_id, e->msg_type, e->extra[0], e->extra[1]);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * BUBBLE MACHINE — Distributed Sorting
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * "Every comparison is a message. Every swap is traceable.
 *  Every execution is replayable."
 */

void hsos_bubble_init(hsos_bubble_t *bm, hsos_system_t *sys,
                      hsos_topology_t topo) {
    memset(bm, 0, sizeof(hsos_bubble_t));
    bm->sys = sys;
    bm->topology = topo;
}

void hsos_bubble_load(hsos_bubble_t *bm, const uint8_t values[8]) {
    /* Load values into worker memory[0] */
    for (int i = 0; i < 8; i++) {
        bm->values[i] = values[i];
        bm->sys->workers[i].memory[0] = values[i];
    }
}

static void capture_values(hsos_bubble_t *bm, uint8_t before[8]) {
    for (int i = 0; i < 8; i++) {
        before[i] = bm->sys->workers[i].memory[0];
    }
}

static int count_changes(hsos_bubble_t *bm, const uint8_t before[8]) {
    int changes = 0;
    for (int i = 0; i < 8; i++) {
        if (bm->sys->workers[i].memory[0] != before[i]) {
            changes++;
        }
    }
    return changes / 2;  /* Each swap changes 2 values */
}

static int bubble_even_phase(hsos_bubble_t *bm) {
    /* Compare-swap pairs: (1,2), (3,4), (5,6), (7,8) */
    uint8_t before[8];
    capture_values(bm, before);

    /* Even pairs: indices 0-1, 2-3, 4-5, 6-7 */
    for (int i = 0; i < 8; i += 2) {
        uint8_t node_a = i + 1;
        uint8_t node_b = i + 2;
        if (node_b > 8) break;

        uint8_t val_a = bm->sys->workers[i].memory[0];

        /* Node A sends CSWAP to Node B */
        hsos_msg_t msg;
        hsos_msg_cswap(&msg, node_a, node_b, val_a, 1);  /* ascending */
        hsos_send(&bm->sys->workers[i], &msg);
        bm->messages++;
    }

    /* Process messages until quiescent */
    hsos_run(bm->sys, 20);

    return count_changes(bm, before);
}

static int bubble_odd_phase(hsos_bubble_t *bm) {
    /* Compare-swap pairs: (2,3), (4,5), (6,7) */
    uint8_t before[8];
    capture_values(bm, before);

    /* Odd pairs: indices 1-2, 3-4, 5-6 */
    for (int i = 1; i < 7; i += 2) {
        uint8_t node_a = i + 1;
        uint8_t node_b = i + 2;

        uint8_t val_a = bm->sys->workers[i].memory[0];

        hsos_msg_t msg;
        hsos_msg_cswap(&msg, node_a, node_b, val_a, 1);
        hsos_send(&bm->sys->workers[i], &msg);
        bm->messages++;
    }

    hsos_run(bm->sys, 20);

    return count_changes(bm, before);
}

int hsos_bubble_run(hsos_bubble_t *bm) {
    bm->cycles = 0;
    bm->swaps = 0;
    bm->messages = 0;

    int max_cycles = 10;  /* Should converge in N/2 cycles */

    for (int c = 0; c < max_cycles; c++) {
        int phase_swaps = 0;

        phase_swaps += bubble_even_phase(bm);
        phase_swaps += bubble_odd_phase(bm);

        bm->swaps += phase_swaps;
        bm->cycles++;

        if (phase_swaps == 0) {
            break;  /* Quiescent: sorted */
        }
    }

    return bm->cycles;
}

void hsos_bubble_read(hsos_bubble_t *bm, uint8_t values[8]) {
    for (int i = 0; i < 8; i++) {
        values[i] = bm->sys->workers[i].memory[0];
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CONSTRAINT FIELD — CSP Solver
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * "Every elimination has a reason. Every reason is traceable.
 *  Every solution is explainable."
 */

void hsos_constraint_init(hsos_constraint_t *cf, hsos_system_t *sys) {
    memset(cf, 0, sizeof(hsos_constraint_t));
    cf->sys = sys;

    /* Initialize all domains to full (values 1-9 possible) */
    for (int i = 0; i < 8; i++) {
        cf->domains[i] = DOMAIN_FULL;
        /* Store in worker memory */
        sys->workers[i].memory[0] = DOMAIN_FULL & 0xFF;
        sys->workers[i].memory[1] = (DOMAIN_FULL >> 8) & 0xFF;
    }

    /* Default: all-different constraint (everyone constrains everyone) */
    for (int i = 0; i < 8; i++) {
        cf->neighbor_count[i] = 0;
        for (int j = 0; j < 8; j++) {
            if (i != j) {
                cf->neighbors[i][cf->neighbor_count[i]++] = j;
            }
        }
    }
}

void hsos_constraint_set_given(hsos_constraint_t *cf, uint8_t cell, uint8_t value) {
    if (cell >= 8 || value < 1 || value > 9) return;

    /* Set domain to single value */
    uint16_t domain = 1 << (value - 1);
    cf->domains[cell] = domain;

    /* Update worker memory */
    cf->sys->workers[cell].memory[0] = domain & 0xFF;
    cf->sys->workers[cell].memory[1] = (domain >> 8) & 0xFF;

    /* Log event */
    if (cf->event_count < 256) {
        hsos_prop_event_t *e = &cf->events[cf->event_count++];
        e->tick = cf->sys->tick;
        e->cell = cell;
        e->removed = DOMAIN_FULL & ~domain;
        e->reason_cell = cell;  /* Given */
        e->domain_before = DOMAIN_FULL;
        e->domain_after = domain;
    }
}

static int popcount16(uint16_t x) {
    int c = 0;
    while (x) { c += x & 1; x >>= 1; }
    return c;
}

static uint8_t get_singleton_value(uint16_t domain) {
    if (popcount16(domain) != 1) return 0;
    for (uint8_t v = 1; v <= 9; v++) {
        if (domain & (1 << (v - 1))) return v;
    }
    return 0;
}

int hsos_constraint_propagate(hsos_constraint_t *cf) {
    int total_eliminations = 0;
    bool changed = true;

    while (changed) {
        changed = false;

        for (int i = 0; i < 8; i++) {
            uint16_t domain_i = cf->domains[i];
            uint8_t val = get_singleton_value(domain_i);
            if (val == 0) continue;  /* Not a singleton */

            /* Eliminate val from all neighbors */
            uint16_t remove_mask = 1 << (val - 1);

            for (int n = 0; n < cf->neighbor_count[i]; n++) {
                uint8_t j = cf->neighbors[i][n];
                uint16_t domain_j = cf->domains[j];

                if (popcount16(domain_j) == 1) continue;  /* Already singleton */
                if (!(domain_j & remove_mask)) continue;  /* Doesn't have val */

                /* Eliminate */
                uint16_t new_domain = domain_j & ~remove_mask;

                /* Send DOMAIN_DELTA message */
                hsos_msg_t msg;
                msg_clear(&msg);
                msg.type = OP_DOMAIN_DELTA;
                msg.src = 0;
                msg.dst = j + 1;
                msg.len = 2;
                msg.payload[0] = remove_mask & 0xFF;
                msg.payload[1] = (remove_mask >> 8) & 0xFF;
                hsos_send(&cf->sys->master, &msg);
                cf->messages++;

                /* Update local state */
                cf->domains[j] = new_domain;
                cf->sys->workers[j].memory[0] = new_domain & 0xFF;
                cf->sys->workers[j].memory[1] = (new_domain >> 8) & 0xFF;

                /* Log event */
                if (cf->event_count < 256) {
                    hsos_prop_event_t *e = &cf->events[cf->event_count++];
                    e->tick = cf->sys->tick;
                    e->cell = j;
                    e->removed = remove_mask;
                    e->reason_cell = i;
                    e->domain_before = domain_j;
                    e->domain_after = new_domain;
                }

                total_eliminations++;
                cf->eliminations++;
                changed = true;
            }
        }

        hsos_run(cf->sys, 10);
    }

    return total_eliminations;
}

uint8_t hsos_constraint_get_value(hsos_constraint_t *cf, uint8_t cell) {
    if (cell >= 8) return 0;
    return get_singleton_value(cf->domains[cell]);
}

void hsos_constraint_why(hsos_constraint_t *cf, uint8_t cell) {
    printf("\n═══ WHY cell %d? ═══\n", cell);

    uint16_t domain = cf->domains[cell];
    uint8_t val = get_singleton_value(domain);

    if (val) {
        printf("Cell %d = %d\n", cell, val);
    } else {
        printf("Cell %d domain: {", cell);
        for (int v = 1; v <= 9; v++) {
            if (domain & (1 << (v - 1))) printf(" %d", v);
        }
        printf(" }\n");
    }

    printf("\nHistory:\n");
    for (uint16_t i = 0; i < cf->event_count; i++) {
        hsos_prop_event_t *e = &cf->events[i];
        if (e->cell != cell) continue;

        printf("[t=%4u] eliminated {", e->tick);
        for (int v = 1; v <= 9; v++) {
            if (e->removed & (1 << (v - 1))) printf(" %d", v);
        }
        printf(" } via cell(%d)=%d\n", e->reason_cell,
               get_singleton_value(cf->domains[e->reason_cell]));
    }
}
