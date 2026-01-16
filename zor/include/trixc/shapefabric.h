/*
 * TRIXC ShapeFabric — Binary Executable Graphs
 *
 * "The graph IS the program. The program IS the genome."
 *
 * No interpretation. No compilation. Just memory and math.
 *
 * Memory Layout:
 *   ShapeGraph {
 *     ShapeNode nodes[N]    <- 16-byte aligned, cache-friendly
 *     float registers[M]    <- The "wires" between nodes
 *   }
 *
 * Execution:
 *   for each node: read inputs, compute, write output
 *
 * Mutation:
 *   nodes[i].in_a_idx = random()  <- One integer write = rewired
 *
 * Created by: Tripp + Claude
 * Date: January 2026
 */

#ifndef TRIXC_SHAPEFABRIC_H
#define TRIXC_SHAPEFABRIC_H

#include <stdint.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Shape Opcodes — The Frozen Instruction Set
 *
 * These are the 5 Primes + their derivatives, encoded as bytes.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    SHAPE_NOP = 0,

    /* Logic (Polynomial Forms) */
    SHAPE_XOR_POLY,      /* a + b - 2ab */
    SHAPE_AND_POLY,      /* ab */
    SHAPE_OR_POLY,       /* a + b - ab */
    SHAPE_NOT_POLY,      /* 1 - a */
    SHAPE_NAND_POLY,     /* 1 - ab */
    SHAPE_NOR_POLY,      /* 1 - (a + b - ab) */
    SHAPE_XNOR_POLY,     /* 1 - (a + b - 2ab) */

    /* Arithmetic (The 5 Primes) */
    SHAPE_ADD,           /* a + b */
    SHAPE_SUB,           /* a - b */
    SHAPE_MUL,           /* a * b */
    SHAPE_DIV,           /* a / b */
    SHAPE_NEG,           /* -a */
    SHAPE_ABS,           /* |a| */
    SHAPE_MAX,           /* max(a, b) */
    SHAPE_MIN,           /* min(a, b) */

    /* Exponentials */
    SHAPE_EXP,           /* exp(a) */
    SHAPE_LOG,           /* log(a) */
    SHAPE_POW,           /* a^b */
    SHAPE_SQRT,          /* sqrt(a) */

    /* Activations (Composed from Primes) */
    SHAPE_SIGMOID,       /* 1 / (1 + exp(-a)) */
    SHAPE_TANH,          /* tanh(a) */
    SHAPE_RELU,          /* max(0, a) */
    SHAPE_SOFTSIGN,      /* a / (1 + |a|) — fast sigmoid approx */

    /* Memory */
    SHAPE_CONST,         /* Output = node.value (immediate) */
    SHAPE_COPY,          /* Output = Input A */
    SHAPE_LOAD,          /* Output = registers[in_a_idx] */
    SHAPE_STORE,         /* registers[out_idx] = Input A */

    /* CfC-Specific */
    SHAPE_DECAY,         /* a * exp(-dt/tau), tau in node.value */
    SHAPE_GATE,          /* sigmoid(a) * b + (1-sigmoid(a)) * c */

    SHAPE_COUNT
} ShapeOpcode;

/* ═══════════════════════════════════════════════════════════════════════════
 * ShapeNode — The Atomic Unit of Computation
 *
 * 16 bytes, aligned for cache efficiency and SIMD potential.
 *
 * Layout:
 *   [0-1]   opcode + flags
 *   [2-3]   out_idx (output register)
 *   [4-7]   in_a_idx (input A register)
 *   [8-11]  in_b_idx (input B register)
 *   [12-15] value (immediate constant or parameter)
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t  opcode;     /* ShapeOpcode */
    uint8_t  flags;      /* Bit 0: active, Bit 1: locked, Bit 2-7: reserved */
    uint16_t out_idx;    /* Output register index */
    uint32_t in_a_idx;   /* Input A register index */
    uint32_t in_b_idx;   /* Input B register index */
    float    value;      /* Immediate value (CONST, DECAY tau, etc.) */
} __attribute__((packed, aligned(16))) ShapeNode;

/* Node flags */
#define NODE_FLAG_ACTIVE  0x01
#define NODE_FLAG_LOCKED  0x02  /* Don't mutate this node */
#define NODE_FLAG_INPUT   0x04  /* This is an input node */
#define NODE_FLAG_OUTPUT  0x08  /* This is an output node */

/* ═══════════════════════════════════════════════════════════════════════════
 * ShapeGraph — The Executable Fabric
 *
 * A complete program in flat memory. No pointers to chase.
 * Can be mmap'd, DMA'd to GPU, or transmitted over network as-is.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    ShapeNode* nodes;       /* Array of computation nodes */
    float* registers;       /* Register file (the "wires") */
    uint32_t node_count;    /* Number of nodes */
    uint32_t reg_count;     /* Number of registers */
    uint32_t input_start;   /* First input register */
    uint32_t input_count;   /* Number of input registers */
    uint32_t output_start;  /* First output register */
    uint32_t output_count;  /* Number of output registers */
} ShapeGraph;

/* ═══════════════════════════════════════════════════════════════════════════
 * Graph Execution — The Tight Loop
 *
 * No branches inside the switch where possible. Pure throughput.
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Execute one node
 */
static inline float shape_execute_node(
    const ShapeNode* node,
    const float* regs,
    float dt  /* For DECAY op */
) {
    float a = regs[node->in_a_idx];
    float b = regs[node->in_b_idx];
    float v = node->value;

    switch (node->opcode) {
        case SHAPE_NOP:        return regs[node->out_idx];

        /* Logic (Polynomial) */
        case SHAPE_XOR_POLY:   return a + b - 2.0f * a * b;
        case SHAPE_AND_POLY:   return a * b;
        case SHAPE_OR_POLY:    return a + b - a * b;
        case SHAPE_NOT_POLY:   return 1.0f - a;
        case SHAPE_NAND_POLY:  return 1.0f - a * b;
        case SHAPE_NOR_POLY:   return 1.0f - (a + b - a * b);
        case SHAPE_XNOR_POLY:  return 1.0f - (a + b - 2.0f * a * b);

        /* Arithmetic */
        case SHAPE_ADD:        return a + b;
        case SHAPE_SUB:        return a - b;
        case SHAPE_MUL:        return a * b;
        case SHAPE_DIV:        return b != 0.0f ? a / b : 0.0f;
        case SHAPE_NEG:        return -a;
        case SHAPE_ABS:        return fabsf(a);
        case SHAPE_MAX:        return a > b ? a : b;
        case SHAPE_MIN:        return a < b ? a : b;

        /* Exponentials */
        case SHAPE_EXP:        return expf(a);
        case SHAPE_LOG:        return a > 0.0f ? logf(a) : -INFINITY;
        case SHAPE_POW:        return powf(a, b);
        case SHAPE_SQRT:       return a >= 0.0f ? sqrtf(a) : 0.0f;

        /* Activations */
        case SHAPE_SIGMOID:    return 1.0f / (1.0f + expf(-a));
        case SHAPE_TANH:       return tanhf(a);
        case SHAPE_RELU:       return a > 0.0f ? a : 0.0f;
        case SHAPE_SOFTSIGN:   return a / (1.0f + fabsf(a));

        /* Memory */
        case SHAPE_CONST:      return v;
        case SHAPE_COPY:       return a;
        case SHAPE_LOAD:       return regs[node->in_a_idx];
        case SHAPE_STORE:      return a;  /* Side effect: write to out_idx */

        /* CfC-Specific */
        case SHAPE_DECAY:      return a * expf(-dt / v);  /* v = tau */
        case SHAPE_GATE: {
            /* gate(a, b, c) = sigmoid(a) * b + (1 - sigmoid(a)) * c */
            /* in_a = gate input, in_b = new value, value = old value idx */
            float gate = 1.0f / (1.0f + expf(-a));
            return gate * b + (1.0f - gate) * regs[(uint32_t)v];
        }

        default:               return 0.0f;
    }
}

/**
 * Execute entire graph once
 */
static inline void shape_execute_graph(ShapeGraph* g, float dt) {
    for (uint32_t i = 0; i < g->node_count; i++) {
        const ShapeNode* node = &g->nodes[i];
        if (node->flags & NODE_FLAG_ACTIVE) {
            g->registers[node->out_idx] = shape_execute_node(node, g->registers, dt);
        }
    }
}

/**
 * Execute graph N times (for sequence processing)
 */
static inline void shape_execute_sequence(
    ShapeGraph* g,
    const float* inputs,  /* [seq_len * input_count] */
    float* outputs,       /* [seq_len * output_count] */
    int seq_len,
    float dt
) {
    for (int t = 0; t < seq_len; t++) {
        /* Load inputs */
        memcpy(&g->registers[g->input_start],
               &inputs[t * g->input_count],
               g->input_count * sizeof(float));

        /* Execute */
        shape_execute_graph(g, dt);

        /* Store outputs */
        memcpy(&outputs[t * g->output_count],
               &g->registers[g->output_start],
               g->output_count * sizeof(float));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Graph Construction — Factory Functions
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Initialize graph with preallocated memory
 */
static inline void shape_graph_init(
    ShapeGraph* g,
    ShapeNode* node_buffer,
    float* reg_buffer,
    uint32_t max_nodes,
    uint32_t max_regs
) {
    g->nodes = node_buffer;
    g->registers = reg_buffer;
    g->node_count = 0;
    g->reg_count = 0;

    /* Clear memory */
    memset(node_buffer, 0, max_nodes * sizeof(ShapeNode));
    memset(reg_buffer, 0, max_regs * sizeof(float));

    /* Set defaults */
    g->input_start = 0;
    g->input_count = 0;
    g->output_start = 0;
    g->output_count = 0;

    (void)max_nodes;
    (void)max_regs;
}

/**
 * Allocate registers for inputs
 */
static inline void shape_graph_set_inputs(ShapeGraph* g, uint32_t count) {
    g->input_start = g->reg_count;
    g->input_count = count;
    g->reg_count += count;
}

/**
 * Allocate registers for outputs
 */
static inline void shape_graph_set_outputs(ShapeGraph* g, uint32_t count) {
    g->output_start = g->reg_count;
    g->output_count = count;
    g->reg_count += count;
}

/**
 * Allocate a new register
 */
static inline uint32_t shape_alloc_reg(ShapeGraph* g) {
    return g->reg_count++;
}

/**
 * Add a node to the graph
 */
static inline uint32_t shape_add_node(
    ShapeGraph* g,
    ShapeOpcode opcode,
    uint32_t out_idx,
    uint32_t in_a_idx,
    uint32_t in_b_idx,
    float value
) {
    uint32_t idx = g->node_count++;
    g->nodes[idx].opcode = (uint8_t)opcode;
    g->nodes[idx].flags = NODE_FLAG_ACTIVE;
    g->nodes[idx].out_idx = (uint16_t)out_idx;
    g->nodes[idx].in_a_idx = in_a_idx;
    g->nodes[idx].in_b_idx = in_b_idx;
    g->nodes[idx].value = value;
    return idx;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CfC Tile Builder — Stamp a Liquid Cell into the Graph
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Build a single CfC neuron as a ShapeGraph fragment
 *
 * This is the "stamp" that creates a liquid neuron.
 *
 * @param g           Graph to build into
 * @param in_reg      Input register
 * @param state_reg   State register (read and written)
 * @param tau         Time constant
 *
 * The CfC equation:
 *   gate = sigmoid(W * x + b)
 *   candidate = tanh(V * x + c)
 *   decay = exp(-dt / tau)
 *   state_new = (1 - gate) * state * decay + gate * candidate
 *
 * Simplified form (for single-neuron tile):
 *   gate = softsign(in)
 *   decay = exp(-dt / tau)
 *   state_new = (1 - gate) * state * decay + gate * in
 */
static inline void shape_build_cfc_neuron(
    ShapeGraph* g,
    uint32_t in_reg,
    uint32_t state_reg,
    float tau
) {
    /* gate = softsign(in) = in / (1 + |in|) */
    uint32_t abs_reg = shape_alloc_reg(g);
    shape_add_node(g, SHAPE_ABS, abs_reg, in_reg, 0, 0.0f);

    uint32_t one_reg = shape_alloc_reg(g);
    shape_add_node(g, SHAPE_CONST, one_reg, 0, 0, 1.0f);

    uint32_t denom_reg = shape_alloc_reg(g);
    shape_add_node(g, SHAPE_ADD, denom_reg, one_reg, abs_reg, 0.0f);

    uint32_t gate_reg = shape_alloc_reg(g);
    shape_add_node(g, SHAPE_DIV, gate_reg, in_reg, denom_reg, 0.0f);

    /* decay = state * exp(-dt / tau) — using DECAY opcode */
    uint32_t decay_reg = shape_alloc_reg(g);
    shape_add_node(g, SHAPE_DECAY, decay_reg, state_reg, 0, tau);

    /* (1 - gate) */
    uint32_t inv_gate_reg = shape_alloc_reg(g);
    shape_add_node(g, SHAPE_SUB, inv_gate_reg, one_reg, gate_reg, 0.0f);

    /* retention = (1 - gate) * decay */
    uint32_t retention_reg = shape_alloc_reg(g);
    shape_add_node(g, SHAPE_MUL, retention_reg, inv_gate_reg, decay_reg, 0.0f);

    /* update = gate * in */
    uint32_t update_reg = shape_alloc_reg(g);
    shape_add_node(g, SHAPE_MUL, update_reg, gate_reg, in_reg, 0.0f);

    /* state_new = retention + update */
    shape_add_node(g, SHAPE_ADD, state_reg, retention_reg, update_reg, 0.0f);
}

/**
 * Build a complete CfC layer (multiple neurons)
 */
static inline void shape_build_cfc_layer(
    ShapeGraph* g,
    const uint32_t* input_regs,   /* Input register indices */
    uint32_t* state_regs,         /* State register indices (in/out) */
    int hidden_dim,
    const float* taus             /* Time constants per neuron */
) {
    for (int i = 0; i < hidden_dim; i++) {
        /* Each neuron gets one input (simplified connectivity) */
        shape_build_cfc_neuron(g, input_regs[i % hidden_dim], state_regs[i], taus[i]);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Graph Mutation — Zero-Copy Evolution
 *
 * Mutate by changing integers and floats. No allocation. No copying.
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Rewire a random connection
 */
static inline void shape_mutate_rewire(
    ShapeGraph* g,
    uint32_t node_idx,
    uint32_t new_input_idx
) {
    if (!(g->nodes[node_idx].flags & NODE_FLAG_LOCKED)) {
        g->nodes[node_idx].in_a_idx = new_input_idx;
    }
}

/**
 * Perturb a constant value
 */
static inline void shape_mutate_value(
    ShapeGraph* g,
    uint32_t node_idx,
    float delta
) {
    if (!(g->nodes[node_idx].flags & NODE_FLAG_LOCKED)) {
        g->nodes[node_idx].value += delta;
    }
}

/**
 * Change node opcode (structural mutation)
 */
static inline void shape_mutate_opcode(
    ShapeGraph* g,
    uint32_t node_idx,
    ShapeOpcode new_opcode
) {
    if (!(g->nodes[node_idx].flags & NODE_FLAG_LOCKED)) {
        g->nodes[node_idx].opcode = (uint8_t)new_opcode;
    }
}

/**
 * Disable a node (soft deletion)
 */
static inline void shape_mutate_disable(
    ShapeGraph* g,
    uint32_t node_idx
) {
    if (!(g->nodes[node_idx].flags & NODE_FLAG_LOCKED)) {
        g->nodes[node_idx].flags &= ~NODE_FLAG_ACTIVE;
    }
}

/**
 * Enable a disabled node
 */
static inline void shape_mutate_enable(
    ShapeGraph* g,
    uint32_t node_idx
) {
    g->nodes[node_idx].flags |= NODE_FLAG_ACTIVE;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Graph Serialization — Save/Load Binary
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Binary format header */
typedef struct {
    uint32_t magic;         /* 'TRIX' = 0x58495254 */
    uint32_t version;       /* Format version */
    uint32_t node_count;
    uint32_t reg_count;
    uint32_t input_start;
    uint32_t input_count;
    uint32_t output_start;
    uint32_t output_count;
} ShapeGraphHeader;

#define SHAPE_GRAPH_MAGIC 0x58495254  /* 'TRIX' */
#define SHAPE_GRAPH_VERSION 1

/**
 * Calculate serialized size
 */
static inline size_t shape_graph_serial_size(const ShapeGraph* g) {
    return sizeof(ShapeGraphHeader)
         + g->node_count * sizeof(ShapeNode)
         + g->reg_count * sizeof(float);
}

/**
 * Serialize graph to buffer
 */
static inline void shape_graph_serialize(
    const ShapeGraph* g,
    void* buffer
) {
    ShapeGraphHeader* header = (ShapeGraphHeader*)buffer;
    header->magic = SHAPE_GRAPH_MAGIC;
    header->version = SHAPE_GRAPH_VERSION;
    header->node_count = g->node_count;
    header->reg_count = g->reg_count;
    header->input_start = g->input_start;
    header->input_count = g->input_count;
    header->output_start = g->output_start;
    header->output_count = g->output_count;

    uint8_t* ptr = (uint8_t*)buffer + sizeof(ShapeGraphHeader);

    memcpy(ptr, g->nodes, g->node_count * sizeof(ShapeNode));
    ptr += g->node_count * sizeof(ShapeNode);

    memcpy(ptr, g->registers, g->reg_count * sizeof(float));
}

/**
 * Deserialize graph from buffer (into preallocated graph)
 */
static inline int shape_graph_deserialize(
    ShapeGraph* g,
    const void* buffer
) {
    const ShapeGraphHeader* header = (const ShapeGraphHeader*)buffer;

    if (header->magic != SHAPE_GRAPH_MAGIC) return -1;
    if (header->version != SHAPE_GRAPH_VERSION) return -2;

    g->node_count = header->node_count;
    g->reg_count = header->reg_count;
    g->input_start = header->input_start;
    g->input_count = header->input_count;
    g->output_start = header->output_start;
    g->output_count = header->output_count;

    const uint8_t* ptr = (const uint8_t*)buffer + sizeof(ShapeGraphHeader);

    memcpy(g->nodes, ptr, g->node_count * sizeof(ShapeNode));
    ptr += g->node_count * sizeof(ShapeNode);

    memcpy(g->registers, ptr, g->reg_count * sizeof(float));

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Graph Statistics
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline size_t shape_graph_memory(const ShapeGraph* g) {
    return g->node_count * sizeof(ShapeNode) + g->reg_count * sizeof(float);
}

static inline uint32_t shape_graph_active_nodes(const ShapeGraph* g) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < g->node_count; i++) {
        if (g->nodes[i].flags & NODE_FLAG_ACTIVE) count++;
    }
    return count;
}

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_SHAPEFABRIC_H */
