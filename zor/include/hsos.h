/*
 * hsos.h — Hollywood Squares OS for TriX
 *
 * "Structure is meaning."
 * "The wiring determines the behavior.
 *  The messages carry the computation.
 *  The trace tells the story."
 *
 * A distributed microkernel for semantic computation.
 * Port of anjaustin/hollywood-squares-os to frozen C.
 *
 * This is NOT about CPU/memory/IO management.
 * This IS about:
 *   - Causality (event ordering)
 *   - Message determinism (reproducible delivery)
 *   - Semantic execution (meaningful computation)
 */

#ifndef HSOS_H
#define HSOS_H

#include <stdint.h>
#include <stdbool.h>

/* Result struct carried in OP_COMPUTE_OK payload.
 * Packed to guarantee sizeof == HSOS_PAYLOAD_MAX (10). */
typedef struct __attribute__((packed)) {
    int16_t match;      /* Signature index, -1 = no match */
    int16_t distance;   /* Hamming distance */
    int16_t threshold;  /* Threshold used */
    char    label[4];   /* First 3 chars of label + null (see strncpy note) */
} hsos_compute_result_t;

_Static_assert(sizeof(hsos_compute_result_t) == 10,
               "hsos_compute_result_t must fit in HSOS_PAYLOAD_MAX");

typedef void (*hsos_compute_fn_t)(const uint8_t *input, uint8_t input_len,
                                   void *ctx,
                                   hsos_compute_result_t *out);

/* ═══════════════════════════════════════════════════════════════════════════
 * MESSAGE PROTOCOL — 16-byte fixed frames
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Message Frame Layout:
 * ┌────┬────┬────┬────┬────┬────┬────────────────────────┐
 * │ 00 │ 01 │ 02 │ 03 │ 04 │ 05 │ 06-0F (10 bytes)       │
 * │type│seq │src │dst │len │flag│ payload                │
 * └────┴────┴────┴────┴────┴────┴────────────────────────┘
 */

#define HSOS_MSG_SIZE       16
#define HSOS_PAYLOAD_MAX    10

/* Message types (opcodes) */
typedef enum {
    /* Control */
    OP_NOP          = 0x00,     /* No operation */
    OP_PING         = 0x01,     /* Connectivity check */
    OP_PONG         = 0x02,     /* Ping response */
    OP_RESET        = 0x03,     /* Reset node */
    OP_HALT         = 0x04,     /* Stop node */

    /* Execution */
    OP_EXEC         = 0x10,     /* Execute operation */
    OP_EXEC_OK      = 0x11,     /* Execution success */
    OP_EXEC_ERR     = 0x12,     /* Execution error */

    /* Memory */
    OP_LOAD         = 0x20,     /* Load program chunk */
    OP_LOAD_OK      = 0x21,     /* Load success */
    OP_DUMP         = 0x22,     /* Request memory dump */
    OP_DUMP_DATA    = 0x23,     /* Memory dump response */

    /* Diagnostics */
    OP_STATUS       = 0x30,     /* Request status */
    OP_STATUS_RPL   = 0x31,     /* Status reply */
    OP_TRACE        = 0x32,     /* Trace event */
    OP_ROUTE        = 0x33,     /* Routing request */

    /* Neural / Compute */
    OP_COMPUTE      = 0x40,     /* Neural compute */
    OP_COMPUTE_OK   = 0x41,     /* Compute success */

    /* Compare-Swap (BubbleMachine) */
    OP_CSWAP        = 0x50,     /* Compare-swap request */
    OP_CSWAP_OK     = 0x51,     /* Compare-swap response */

    /* Constraint (ConstraintField) */
    OP_DOMAIN_GET   = 0x60,     /* Get domain bitset */
    OP_DOMAIN_SET   = 0x61,     /* Set domain bitset */
    OP_DOMAIN_DELTA = 0x62,     /* Remove values from domain */
    OP_IS_SINGLETON = 0x63,     /* Check if single value */
    OP_GET_VALUE    = 0x64,     /* Get fixed value */
} hsos_opcode_t;

/* Message flags */
#define MSG_FLAG_ACK_REQ    0x01    /* Acknowledgment required */
#define MSG_FLAG_PRIORITY   0x02    /* High priority */
#define MSG_FLAG_FRAGMENT   0x04    /* Part of fragmented message */
#define MSG_FLAG_LAST_FRAG  0x08    /* Last fragment */
#define MSG_FLAG_BROADCAST  0x10    /* Broadcast to all */

/* The 16-byte message frame */
typedef struct __attribute__((packed)) {
    uint8_t type;                   /* Opcode */
    uint8_t seq;                    /* Sequence number */
    uint8_t src;                    /* Source node ID */
    uint8_t dst;                    /* Destination node ID */
    uint8_t len;                    /* Payload length (0-10) */
    uint8_t flags;                  /* Message flags */
    uint8_t payload[HSOS_PAYLOAD_MAX]; /* Payload data */
} hsos_msg_t;

/* ═══════════════════════════════════════════════════════════════════════════
 * ARITHMETIC OPERATIONS — For EXEC payload
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    ALU_ADD         = 0x00,
    ALU_SUB         = 0x01,
    ALU_CMP         = 0x02,
    ALU_AND         = 0x03,
    ALU_OR          = 0x04,
    ALU_XOR         = 0x05,
    ALU_NOT         = 0x06,
    ALU_SHIFT_L     = 0x07,
    ALU_SHIFT_R     = 0x08,
    ALU_PEEK        = 0x09,
    ALU_POKE        = 0x0A,
    ALU_COPY        = 0x0B,
} hsos_alu_op_t;

/* ═══════════════════════════════════════════════════════════════════════════
 * NODE STATE MACHINE
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    NODE_OFFLINE    = 0x00,     /* Inactive */
    NODE_IDLE       = 0x01,     /* Ready for work */
    NODE_BUSY       = 0x02,     /* Processing */
    NODE_ERROR      = 0x03,     /* Fault condition */
    NODE_HALTED     = 0x04,     /* Stopped */
} hsos_node_state_t;

/* ═══════════════════════════════════════════════════════════════════════════
 * NODE KERNEL — Runs on each processor
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Manages:
 *   - Mailbox (incoming/outgoing message queues)
 *   - Dispatcher (route messages to handlers)
 *   - Scheduler (cooperative, deterministic)
 *   - Memory (64KB simulated)
 */

#define HSOS_MEMORY_SIZE    65536   /* 64KB per node */
#define HSOS_QUEUE_SIZE     16      /* Message queue depth */
#define HSOS_FRAG_BUF_SIZE  64      /* Max reassembly payload (one input vector) */

typedef struct {
    /* Identity */
    uint8_t node_id;
    hsos_node_state_t state;

    /* Message queues */
    hsos_msg_t inbox[HSOS_QUEUE_SIZE];
    hsos_msg_t outbox[HSOS_QUEUE_SIZE];
    uint8_t inbox_head, inbox_tail;
    uint8_t outbox_head, outbox_tail;

    /* Sequence tracking */
    uint8_t seq_tx;             /* Outgoing sequence number */
    uint8_t seq_rx;             /* Last received sequence */

    /* Memory (for constraint field / bubble machine) */
    uint8_t memory[256];        /* Reduced for embedded: 256 bytes */

    /* Fragment reassembly */
    uint8_t frag_buf[HSOS_FRAG_BUF_SIZE]; /* Accumulated payload */
    uint8_t frag_len;           /* Bytes accumulated so far */
    uint8_t frag_src;           /* Source node of in-progress reassembly */
    uint8_t frag_type;          /* Opcode of message being reassembled */
    bool    frag_active;        /* Reassembly in progress */

    /* Compute callback — set by bridge, called by handle_compute() */
    hsos_compute_fn_t compute_fn;   /* NULL = no compute capability */
    void             *compute_ctx;  /* Opaque shard context for compute_fn */

    /* Statistics */
    uint32_t msgs_rx;
    uint32_t msgs_tx;
    uint32_t errors;

    /* Load metric (0-255) */
    uint8_t load;
} hsos_node_t;

/* ═══════════════════════════════════════════════════════════════════════════
 * FABRIC KERNEL — Runs on master (node 0) only
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Services:
 *   - Directory (node registry)
 *   - Router (load-aware message routing)
 *   - Supervisor (heartbeat, health monitoring)
 *   - Loader (program deployment)
 */

#define HSOS_MAX_NODES      9       /* Master + 8 workers */

/* Node capability flags */
#define CAP_BASIC           0x01
#define CAP_NEURAL_ALU      0x02
#define CAP_NEURAL_CMP      0x04
#define CAP_NEURAL_LOGIC    0x08
#define CAP_MEMORY          0x10
#define CAP_CUSTOM          0x20

typedef struct {
    hsos_node_state_t status;
    uint8_t load;
    uint8_t capabilities;
    uint32_t last_heartbeat;
    uint32_t msgs_total;
    uint32_t errors_total;
} hsos_directory_entry_t;

typedef struct {
    /* Directory */
    hsos_directory_entry_t directory[HSOS_MAX_NODES];

    /* Routing state */
    uint8_t round_robin_idx;

    /* Supervisor state */
    uint32_t tick;
    uint32_t heartbeat_interval;
    uint32_t heartbeat_timeout;

    /* Statistics */
    uint32_t msgs_routed;
    uint32_t msgs_dropped;
} hsos_fabric_t;

/* ═══════════════════════════════════════════════════════════════════════════
 * COMPLETE SYSTEM — HSquaresOS
 * ═══════════════════════════════════════════════════════════════════════════ */

#define HSOS_TRACE_SIZE     64      /* Circular trace buffer */

typedef struct {
    uint32_t tick;
    uint8_t node_id;
    uint8_t msg_type;
    uint8_t extra[2];
} hsos_trace_entry_t;

typedef struct {
    /* Nodes */
    hsos_node_t master;
    hsos_node_t workers[8];

    /* Fabric (runs on master) */
    hsos_fabric_t fabric;

    /* Global state */
    uint32_t tick;
    bool booted;

    /* Message bus statistics */
    uint32_t bus_delivered;
    uint32_t bus_dropped;

    /* Trace buffer */
    hsos_trace_entry_t trace[HSOS_TRACE_SIZE];
    uint8_t trace_head;

    /* Recording state */
    bool recording;
    bool replay_mode;           /* Suppress re-recording during replay */
    hsos_msg_t *record_buffer;  /* Dynamically allocated */
    uint16_t record_capacity;   /* Allocated capacity */
    uint16_t record_count;      /* Messages recorded so far */
} hsos_system_t;

/* ═══════════════════════════════════════════════════════════════════════════
 * API — System Control
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Initialize the system (default record buffer capacity: 4096 messages) */
void hsos_init(hsos_system_t *sys);

/* Initialize with a custom record buffer capacity */
void hsos_init_with_capacity(hsos_system_t *sys, uint16_t record_capacity);

/* Free dynamic resources (record buffer) */
void hsos_system_free(hsos_system_t *sys);

/* Boot all nodes, returns count of online workers */
int hsos_boot(hsos_system_t *sys);

/* Execute one tick, returns true if work was done */
bool hsos_step(hsos_system_t *sys);

/* Execute one tick for workers + bus only — master inbox is NOT drained.
 * Use during OP_COMPUTE_OK collection so replies remain in master inbox. */
bool hsos_step_workers(hsos_system_t *sys);

/* Run until quiescent or max_ticks reached */
int hsos_run(hsos_system_t *sys, int max_ticks);

/* Execute operation on specific node */
int hsos_exec(hsos_system_t *sys, uint8_t node, hsos_alu_op_t op,
              uint8_t a, uint8_t b);

/* Broadcast operation to all workers */
void hsos_broadcast_exec(hsos_system_t *sys, hsos_alu_op_t op,
                         uint8_t a, uint8_t b);

/* Route operation to best available node */
int hsos_route_exec(hsos_system_t *sys, hsos_alu_op_t op,
                    uint8_t a, uint8_t b, uint8_t *assigned_node);

/* ═══════════════════════════════════════════════════════════════════════════
 * API — Recording & Replay
 * ═══════════════════════════════════════════════════════════════════════════ */

void hsos_start_recording(hsos_system_t *sys);
int hsos_stop_recording(hsos_system_t *sys);
void hsos_replay(hsos_system_t *sys);

/* ═══════════════════════════════════════════════════════════════════════════
 * API — Introspection
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Get node status */
hsos_node_state_t hsos_node_status(hsos_system_t *sys, uint8_t node);

/* Ping a node, returns true if responsive */
bool hsos_ping(hsos_system_t *sys, uint8_t node);

/* Get system statistics */
void hsos_stats(hsos_system_t *sys, uint32_t *ticks, uint32_t *delivered,
                uint32_t *dropped);

/* Dump trace buffer */
void hsos_dump_trace(hsos_system_t *sys);

/* ═══════════════════════════════════════════════════════════════════════════
 * API — Message Helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Create messages */
void hsos_msg_ping(hsos_msg_t *msg, uint8_t src, uint8_t dst);
void hsos_msg_exec(hsos_msg_t *msg, uint8_t src, uint8_t dst,
                   hsos_alu_op_t op, uint8_t a, uint8_t b);
void hsos_msg_cswap(hsos_msg_t *msg, uint8_t src, uint8_t dst,
                    uint8_t value, uint8_t ascending);

/* Send message (enqueue to sender's outbox) */
void hsos_send(hsos_node_t *node, hsos_msg_t *msg);

/* Receive message (dequeue from inbox), returns false if empty */
bool hsos_recv(hsos_node_t *node, hsos_msg_t *msg);

/* Send a large payload (>10 bytes) as a sequence of fragment messages.
 * Receiver reassembles transparently; handler is called on last fragment
 * with node->frag_buf populated and node->frag_len set to total length. */
void hsos_send_fragmented(hsos_node_t *node, uint8_t type, uint8_t dst,
                          const uint8_t *data, uint8_t len);

/* ═══════════════════════════════════════════════════════════════════════════
 * BUBBLE MACHINE — Distributed Sorting
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * "Every comparison is a message. Every swap is traceable.
 *  Every execution is replayable."
 *
 * Topology determines algorithm:
 *   - LINE: Odd-even transposition sort
 *   - RING: Circular variant
 *   - GRID: 2D checkerboard phases
 */

typedef enum {
    TOPO_LINE,
    TOPO_RING,
    TOPO_GRID,
} hsos_topology_t;

typedef struct {
    hsos_system_t *sys;
    hsos_topology_t topology;

    /* Values stored in worker memory[0] */
    uint8_t values[8];

    /* Statistics */
    uint32_t cycles;
    uint32_t swaps;
    uint32_t messages;
} hsos_bubble_t;

void hsos_bubble_init(hsos_bubble_t *bm, hsos_system_t *sys,
                      hsos_topology_t topo);
void hsos_bubble_load(hsos_bubble_t *bm, const uint8_t values[8]);
int hsos_bubble_run(hsos_bubble_t *bm);
void hsos_bubble_read(hsos_bubble_t *bm, uint8_t values[8]);

/* ═══════════════════════════════════════════════════════════════════════════
 * CONSTRAINT FIELD — CSP Solver
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * "Every elimination has a reason. Every reason is traceable.
 *  Every solution is explainable."
 *
 * "This system doesn't search for solutions. It relaxes toward them."
 */

/* Domain represented as 16-bit bitset (values 1-9 use bits 0-8) */
#define DOMAIN_FULL     0x01FF      /* All 9 values possible */

typedef struct {
    uint32_t tick;
    uint8_t cell;
    uint16_t removed;               /* Which values were eliminated */
    uint8_t reason_cell;            /* Which cell caused this */
    uint16_t domain_before;
    uint16_t domain_after;
} hsos_prop_event_t;

typedef struct {
    hsos_system_t *sys;

    /* Domains: bits 0-8 represent values 1-9 */
    uint16_t domains[8];

    /* Constraint graph: which cells are neighbors */
    uint8_t neighbors[8][8];        /* neighbors[i] lists j's that constrain i */
    uint8_t neighbor_count[8];

    /* Propagation history */
    hsos_prop_event_t events[256];
    uint16_t event_count;

    /* Statistics */
    uint32_t eliminations;
    uint32_t messages;
} hsos_constraint_t;

void hsos_constraint_init(hsos_constraint_t *cf, hsos_system_t *sys);
void hsos_constraint_set_given(hsos_constraint_t *cf, uint8_t cell, uint8_t value);
int hsos_constraint_propagate(hsos_constraint_t *cf);
uint8_t hsos_constraint_get_value(hsos_constraint_t *cf, uint8_t cell);
void hsos_constraint_why(hsos_constraint_t *cf, uint8_t cell);

#endif /* HSOS_H */
