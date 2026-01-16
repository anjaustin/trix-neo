/*
 * TRIXC EntroMorph — Evolutionary CfC Engine
 *
 * "Don't train. Evolve."
 *
 * This is not gradient descent. This is natural selection on silicon.
 * Genomes mutate, compete, and die. The fittest get frozen into chips.
 *
 * Performance target: Millions of generations per second per core.
 *
 * Created by: Tripp + Claude
 * Date: January 2026
 */

#ifndef TRIXC_ENTROMORPH_H
#define TRIXC_ENTROMORPH_H

#include "cfc_shapes.h"
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Configuration
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Maximum dimensions (tune for your target) */
#ifndef ENTROMORPH_MAX_INPUT
#define ENTROMORPH_MAX_INPUT   16
#endif

#ifndef ENTROMORPH_MAX_HIDDEN
#define ENTROMORPH_MAX_HIDDEN  32
#endif

#ifndef ENTROMORPH_MAX_OUTPUT
#define ENTROMORPH_MAX_OUTPUT  8
#endif

#define ENTROMORPH_MAX_CONCAT  (ENTROMORPH_MAX_INPUT + ENTROMORPH_MAX_HIDDEN)

/* ═══════════════════════════════════════════════════════════════════════════
 * The Liquid Genome — Mutable DNA of a CfC Network
 *
 * Unlike frozen chips, this structure is designed to be mutated.
 * All weights are inline (no pointers) for cache locality.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    /* Dimensions (immutable during evolution of this lineage) */
    uint8_t input_dim;
    uint8_t hidden_dim;
    uint8_t output_dim;

    /* ─────────────────────────────────────────────────────────────────────
     * Topology Genes — "Which neurons connect to what"
     *
     * input_map[i] = which input index feeds hidden neuron i
     *                (0xFF = no connection, use bias only)
     *
     * This enables sparse, evolved connectivity rather than full dense.
     * ───────────────────────────────────────────────────────────────────── */
    uint8_t input_map[ENTROMORPH_MAX_HIDDEN];

    /* ─────────────────────────────────────────────────────────────────────
     * Time Genes — "How fast each neuron forgets"
     *
     * tau[i] = time constant for hidden neuron i
     * Larger tau = slower decay = longer memory
     * ───────────────────────────────────────────────────────────────────── */
    float tau[ENTROMORPH_MAX_HIDDEN];

    /* ─────────────────────────────────────────────────────────────────────
     * Gate Weights — "How much new info to accept"
     *
     * W_gate[i * concat_dim + j] = weight from concat[j] to gate[i]
     * b_gate[i] = bias for gate neuron i
     * ───────────────────────────────────────────────────────────────────── */
    float W_gate[ENTROMORPH_MAX_HIDDEN * ENTROMORPH_MAX_CONCAT];
    float b_gate[ENTROMORPH_MAX_HIDDEN];

    /* ─────────────────────────────────────────────────────────────────────
     * Candidate Weights — "What new info looks like"
     * ───────────────────────────────────────────────────────────────────── */
    float W_cand[ENTROMORPH_MAX_HIDDEN * ENTROMORPH_MAX_CONCAT];
    float b_cand[ENTROMORPH_MAX_HIDDEN];

    /* ─────────────────────────────────────────────────────────────────────
     * Output Weights — "How to interpret the hidden state"
     * ───────────────────────────────────────────────────────────────────── */
    float W_out[ENTROMORPH_MAX_HIDDEN * ENTROMORPH_MAX_OUTPUT];
    float b_out[ENTROMORPH_MAX_OUTPUT];

    /* ─────────────────────────────────────────────────────────────────────
     * Fitness — "How well this genome performs"
     *
     * Updated by evaluation function. Used for selection.
     * ───────────────────────────────────────────────────────────────────── */
    float fitness;

    /* Unique ID for tracking lineage */
    uint32_t id;
    uint32_t generation;
    uint32_t parent_a;
    uint32_t parent_b;

} LiquidGenome;

/* ═══════════════════════════════════════════════════════════════════════════
 * Fast PRNG — Xorshift64 (Deterministic, No malloc)
 *
 * Fast enough to not bottleneck mutation.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint64_t state;
} EntroRNG;

static inline void entro_rng_seed(EntroRNG* rng, uint64_t seed) {
    rng->state = seed ? seed : 0x853c49e6748fea9bULL;
}

static inline uint64_t entro_rng_next(EntroRNG* rng) {
    uint64_t x = rng->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng->state = x;
    return x * 0x2545f4914f6cdd1dULL;
}

/* Uniform float in [0, 1) */
static inline float entro_rng_float(EntroRNG* rng) {
    return (entro_rng_next(rng) >> 11) * (1.0f / 9007199254740992.0f);
}

/* Uniform float in [min, max) */
static inline float entro_rng_range(EntroRNG* rng, float min, float max) {
    return min + entro_rng_float(rng) * (max - min);
}

/* Gaussian approximation (Box-Muller would be better but this is faster) */
static inline float entro_rng_gaussian(EntroRNG* rng, float mean, float std) {
    /* Sum of 12 uniforms approximates gaussian (CLT) */
    float sum = -6.0f;
    for (int i = 0; i < 12; i++) {
        sum += entro_rng_float(rng);
    }
    return mean + sum * std;
}

/* Random integer in [0, max) */
static inline uint32_t entro_rng_int(EntroRNG* rng, uint32_t max) {
    return (uint32_t)(entro_rng_next(rng) % max);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Genesis — Create Random Genomes
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Initialize a genome with random weights
 *
 * @param genome    Genome to initialize
 * @param in_dim    Input dimension
 * @param hid_dim   Hidden dimension
 * @param out_dim   Output dimension
 * @param rng       Random number generator
 * @param id        Unique ID for this genome
 */
static inline void entro_genesis(
    LiquidGenome* genome,
    int in_dim, int hid_dim, int out_dim,
    EntroRNG* rng,
    uint32_t id
) {
    genome->input_dim = (uint8_t)in_dim;
    genome->hidden_dim = (uint8_t)hid_dim;
    genome->output_dim = (uint8_t)out_dim;

    const int concat_dim = in_dim + hid_dim;

    /* Xavier scale for initialization */
    const float gate_scale = sqrtf(2.0f / (float)(concat_dim + hid_dim));
    const float cand_scale = sqrtf(2.0f / (float)(concat_dim + hid_dim));
    const float out_scale = sqrtf(2.0f / (float)(hid_dim + out_dim));

    /* Input mapping: random sparse connectivity */
    for (int i = 0; i < hid_dim; i++) {
        if (entro_rng_float(rng) < 0.8f) {
            /* 80% chance of direct input connection */
            genome->input_map[i] = (uint8_t)entro_rng_int(rng, in_dim);
        } else {
            /* 20% chance of no direct input (recurrent only) */
            genome->input_map[i] = 0xFF;
        }
    }

    /* Time constants: log-uniform in [0.1, 10.0] */
    for (int i = 0; i < hid_dim; i++) {
        float log_tau = entro_rng_range(rng, -1.0f, 1.0f);  /* log10 */
        genome->tau[i] = powf(10.0f, log_tau);
    }

    /* Gate weights */
    for (int i = 0; i < hid_dim * concat_dim; i++) {
        genome->W_gate[i] = entro_rng_gaussian(rng, 0.0f, gate_scale);
    }
    for (int i = 0; i < hid_dim; i++) {
        genome->b_gate[i] = 0.0f;
    }

    /* Candidate weights */
    for (int i = 0; i < hid_dim * concat_dim; i++) {
        genome->W_cand[i] = entro_rng_gaussian(rng, 0.0f, cand_scale);
    }
    for (int i = 0; i < hid_dim; i++) {
        genome->b_cand[i] = 0.0f;
    }

    /* Output weights */
    for (int i = 0; i < hid_dim * out_dim; i++) {
        genome->W_out[i] = entro_rng_gaussian(rng, 0.0f, out_scale);
    }
    for (int i = 0; i < out_dim; i++) {
        genome->b_out[i] = 0.0f;
    }

    /* Metadata */
    genome->fitness = -INFINITY;
    genome->id = id;
    genome->generation = 0;
    genome->parent_a = 0;
    genome->parent_b = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Mutation Operators — Perturb the Genome
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    float weight_mutation_rate;   /* Probability of mutating each weight */
    float weight_mutation_std;    /* Std dev of weight perturbation */
    float tau_mutation_rate;      /* Probability of mutating each tau */
    float tau_mutation_std;       /* Std dev of log-tau perturbation */
    float topology_mutation_rate; /* Probability of rewiring input_map */
    float bias_mutation_rate;     /* Probability of mutating biases */
    float bias_mutation_std;      /* Std dev of bias perturbation */
} MutationParams;

/* Default mutation parameters */
static const MutationParams MUTATION_DEFAULT = {
    .weight_mutation_rate = 0.1f,
    .weight_mutation_std = 0.1f,
    .tau_mutation_rate = 0.05f,
    .tau_mutation_std = 0.2f,
    .topology_mutation_rate = 0.02f,
    .bias_mutation_rate = 0.1f,
    .bias_mutation_std = 0.1f,
};

/**
 * Mutate a genome in-place
 */
static inline void entro_mutate(
    LiquidGenome* genome,
    const MutationParams* params,
    EntroRNG* rng
) {
    const int hid_dim = genome->hidden_dim;
    const int in_dim = genome->input_dim;
    const int out_dim = genome->output_dim;
    const int concat_dim = in_dim + hid_dim;

    /* Mutate topology (input_map) */
    for (int i = 0; i < hid_dim; i++) {
        if (entro_rng_float(rng) < params->topology_mutation_rate) {
            if (entro_rng_float(rng) < 0.8f) {
                genome->input_map[i] = (uint8_t)entro_rng_int(rng, in_dim);
            } else {
                genome->input_map[i] = 0xFF;
            }
        }
    }

    /* Mutate time constants */
    for (int i = 0; i < hid_dim; i++) {
        if (entro_rng_float(rng) < params->tau_mutation_rate) {
            float log_tau = log10f(genome->tau[i]);
            log_tau += entro_rng_gaussian(rng, 0.0f, params->tau_mutation_std);
            log_tau = fmaxf(-2.0f, fminf(2.0f, log_tau));  /* Clamp to [0.01, 100] */
            genome->tau[i] = powf(10.0f, log_tau);
        }
    }

    /* Mutate gate weights */
    for (int i = 0; i < hid_dim * concat_dim; i++) {
        if (entro_rng_float(rng) < params->weight_mutation_rate) {
            genome->W_gate[i] += entro_rng_gaussian(rng, 0.0f, params->weight_mutation_std);
        }
    }
    for (int i = 0; i < hid_dim; i++) {
        if (entro_rng_float(rng) < params->bias_mutation_rate) {
            genome->b_gate[i] += entro_rng_gaussian(rng, 0.0f, params->bias_mutation_std);
        }
    }

    /* Mutate candidate weights */
    for (int i = 0; i < hid_dim * concat_dim; i++) {
        if (entro_rng_float(rng) < params->weight_mutation_rate) {
            genome->W_cand[i] += entro_rng_gaussian(rng, 0.0f, params->weight_mutation_std);
        }
    }
    for (int i = 0; i < hid_dim; i++) {
        if (entro_rng_float(rng) < params->bias_mutation_rate) {
            genome->b_cand[i] += entro_rng_gaussian(rng, 0.0f, params->bias_mutation_std);
        }
    }

    /* Mutate output weights */
    for (int i = 0; i < hid_dim * out_dim; i++) {
        if (entro_rng_float(rng) < params->weight_mutation_rate) {
            genome->W_out[i] += entro_rng_gaussian(rng, 0.0f, params->weight_mutation_std);
        }
    }
    for (int i = 0; i < out_dim; i++) {
        if (entro_rng_float(rng) < params->bias_mutation_rate) {
            genome->b_out[i] += entro_rng_gaussian(rng, 0.0f, params->bias_mutation_std);
        }
    }

    /* Reset fitness (needs re-evaluation) */
    genome->fitness = -INFINITY;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Crossover Operators — Breed Two Genomes
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Uniform crossover: randomly pick each gene from either parent
 */
static inline void entro_crossover_uniform(
    const LiquidGenome* parent_a,
    const LiquidGenome* parent_b,
    LiquidGenome* child,
    EntroRNG* rng,
    uint32_t child_id,
    uint32_t generation
) {
    /* Dimensions must match */
    child->input_dim = parent_a->input_dim;
    child->hidden_dim = parent_a->hidden_dim;
    child->output_dim = parent_a->output_dim;

    const int hid_dim = child->hidden_dim;
    const int in_dim = child->input_dim;
    const int out_dim = child->output_dim;
    const int concat_dim = in_dim + hid_dim;

    /* Crossover topology */
    for (int i = 0; i < hid_dim; i++) {
        child->input_map[i] = (entro_rng_float(rng) < 0.5f) ?
            parent_a->input_map[i] : parent_b->input_map[i];
    }

    /* Crossover time constants */
    for (int i = 0; i < hid_dim; i++) {
        child->tau[i] = (entro_rng_float(rng) < 0.5f) ?
            parent_a->tau[i] : parent_b->tau[i];
    }

    /* Crossover gate weights */
    for (int i = 0; i < hid_dim * concat_dim; i++) {
        child->W_gate[i] = (entro_rng_float(rng) < 0.5f) ?
            parent_a->W_gate[i] : parent_b->W_gate[i];
    }
    for (int i = 0; i < hid_dim; i++) {
        child->b_gate[i] = (entro_rng_float(rng) < 0.5f) ?
            parent_a->b_gate[i] : parent_b->b_gate[i];
    }

    /* Crossover candidate weights */
    for (int i = 0; i < hid_dim * concat_dim; i++) {
        child->W_cand[i] = (entro_rng_float(rng) < 0.5f) ?
            parent_a->W_cand[i] : parent_b->W_cand[i];
    }
    for (int i = 0; i < hid_dim; i++) {
        child->b_cand[i] = (entro_rng_float(rng) < 0.5f) ?
            parent_a->b_cand[i] : parent_b->b_cand[i];
    }

    /* Crossover output weights */
    for (int i = 0; i < hid_dim * out_dim; i++) {
        child->W_out[i] = (entro_rng_float(rng) < 0.5f) ?
            parent_a->W_out[i] : parent_b->W_out[i];
    }
    for (int i = 0; i < out_dim; i++) {
        child->b_out[i] = (entro_rng_float(rng) < 0.5f) ?
            parent_a->b_out[i] : parent_b->b_out[i];
    }

    /* Metadata */
    child->fitness = -INFINITY;
    child->id = child_id;
    child->generation = generation;
    child->parent_a = parent_a->id;
    child->parent_b = parent_b->id;
}

/**
 * Blend crossover: interpolate weights between parents
 */
static inline void entro_crossover_blend(
    const LiquidGenome* parent_a,
    const LiquidGenome* parent_b,
    LiquidGenome* child,
    float blend,  /* 0.0 = all A, 1.0 = all B */
    uint32_t child_id,
    uint32_t generation
) {
    child->input_dim = parent_a->input_dim;
    child->hidden_dim = parent_a->hidden_dim;
    child->output_dim = parent_a->output_dim;

    const int hid_dim = child->hidden_dim;
    const int in_dim = child->input_dim;
    const int out_dim = child->output_dim;
    const int concat_dim = in_dim + hid_dim;

    const float a_weight = 1.0f - blend;
    const float b_weight = blend;

    /* Blend topology: use dominant parent */
    for (int i = 0; i < hid_dim; i++) {
        child->input_map[i] = (blend < 0.5f) ?
            parent_a->input_map[i] : parent_b->input_map[i];
    }

    /* Blend time constants (geometric mean in log space) */
    for (int i = 0; i < hid_dim; i++) {
        float log_tau_a = log10f(parent_a->tau[i]);
        float log_tau_b = log10f(parent_b->tau[i]);
        child->tau[i] = powf(10.0f, a_weight * log_tau_a + b_weight * log_tau_b);
    }

    /* Blend weights */
    for (int i = 0; i < hid_dim * concat_dim; i++) {
        child->W_gate[i] = a_weight * parent_a->W_gate[i] + b_weight * parent_b->W_gate[i];
        child->W_cand[i] = a_weight * parent_a->W_cand[i] + b_weight * parent_b->W_cand[i];
    }
    for (int i = 0; i < hid_dim; i++) {
        child->b_gate[i] = a_weight * parent_a->b_gate[i] + b_weight * parent_b->b_gate[i];
        child->b_cand[i] = a_weight * parent_a->b_cand[i] + b_weight * parent_b->b_cand[i];
    }
    for (int i = 0; i < hid_dim * out_dim; i++) {
        child->W_out[i] = a_weight * parent_a->W_out[i] + b_weight * parent_b->W_out[i];
    }
    for (int i = 0; i < out_dim; i++) {
        child->b_out[i] = a_weight * parent_a->b_out[i] + b_weight * parent_b->b_out[i];
    }

    /* Metadata */
    child->fitness = -INFINITY;
    child->id = child_id;
    child->generation = generation;
    child->parent_a = parent_a->id;
    child->parent_b = parent_b->id;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Genome Evaluation — Run CfC and Compute Fitness
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Build CfC parameters from a genome (for evaluation)
 *
 * This creates a CfCParams struct pointing into the genome's arrays.
 * The genome must outlive the params!
 */
static inline void entro_genome_to_params(
    const LiquidGenome* genome,
    CfCParams* cell_params,
    CfCOutputParams* out_params
) {
    cell_params->input_dim = genome->input_dim;
    cell_params->hidden_dim = genome->hidden_dim;
    cell_params->W_gate = genome->W_gate;
    cell_params->b_gate = genome->b_gate;
    cell_params->W_cand = genome->W_cand;
    cell_params->b_cand = genome->b_cand;
    cell_params->tau = genome->tau;
    cell_params->tau_shared = 0;  /* Per-neuron tau */

    out_params->hidden_dim = genome->hidden_dim;
    out_params->output_dim = genome->output_dim;
    out_params->W_out = genome->W_out;
    out_params->b_out = genome->b_out;
}

/**
 * Evaluate genome on a single sequence
 *
 * @param genome    Genome to evaluate
 * @param inputs    Input sequence [seq_len * input_dim]
 * @param targets   Target outputs [seq_len * output_dim] or [output_dim] for final-only
 * @param seq_len   Sequence length
 * @param dt        Time step
 * @param final_only If true, only compare final output to target
 *
 * @return Mean squared error (lower is better)
 */
static inline float entro_evaluate_mse(
    const LiquidGenome* genome,
    const float* inputs,
    const float* targets,
    int seq_len,
    float dt,
    int final_only
) {
    const int in_dim = genome->input_dim;
    const int hid_dim = genome->hidden_dim;
    const int out_dim = genome->output_dim;

    CfCParams cell_params;
    CfCOutputParams out_params;
    entro_genome_to_params(genome, &cell_params, &out_params);

    /* Hidden state */
    float h[ENTROMORPH_MAX_HIDDEN] = {0};
    float h_new[ENTROMORPH_MAX_HIDDEN];
    float output[ENTROMORPH_MAX_OUTPUT];

    float total_error = 0.0f;
    int count = 0;

    for (int t = 0; t < seq_len; t++) {
        const float* x_t = inputs + t * in_dim;

        /* CfC step */
        trix_cfc_cell(x_t, h, dt, &cell_params, h_new);
        memcpy(h, h_new, hid_dim * sizeof(float));

        if (!final_only || t == seq_len - 1) {
            /* Compute output */
            trix_cfc_output(h, &out_params, output);

            /* Compare to target */
            const float* target_t = final_only ? targets : (targets + t * out_dim);
            for (int i = 0; i < out_dim; i++) {
                float diff = output[i] - target_t[i];
                total_error += diff * diff;
            }
            count += out_dim;
        }
    }

    return count > 0 ? total_error / (float)count : INFINITY;
}

/**
 * Evaluate genome for classification (cross-entropy loss)
 */
static inline float entro_evaluate_classification(
    const LiquidGenome* genome,
    const float* inputs,
    const int* labels,  /* Class indices */
    int num_samples,
    int seq_len,
    float dt
) {
    const int in_dim = genome->input_dim;
    const int hid_dim = genome->hidden_dim;
    const int out_dim = genome->output_dim;

    CfCParams cell_params;
    CfCOutputParams out_params;
    entro_genome_to_params(genome, &cell_params, &out_params);

    float total_loss = 0.0f;
    int correct = 0;

    for (int s = 0; s < num_samples; s++) {
        const float* sample_inputs = inputs + s * seq_len * in_dim;
        int label = labels[s];

        /* Reset hidden state */
        float h[ENTROMORPH_MAX_HIDDEN] = {0};
        float h_new[ENTROMORPH_MAX_HIDDEN];

        /* Process sequence */
        for (int t = 0; t < seq_len; t++) {
            const float* x_t = sample_inputs + t * in_dim;
            trix_cfc_cell(x_t, h, dt, &cell_params, h_new);
            memcpy(h, h_new, hid_dim * sizeof(float));
        }

        /* Get class probabilities */
        float probs[ENTROMORPH_MAX_OUTPUT];
        trix_cfc_output_softmax(h, &out_params, probs);

        /* Cross-entropy loss */
        float prob_correct = fmaxf(probs[label], 1e-7f);
        total_loss -= logf(prob_correct);

        /* Accuracy tracking */
        int pred = 0;
        float max_prob = probs[0];
        for (int i = 1; i < out_dim; i++) {
            if (probs[i] > max_prob) {
                max_prob = probs[i];
                pred = i;
            }
        }
        if (pred == label) correct++;
    }

    /* Return accuracy directly (higher = better) */
    return (float)correct / (float)num_samples;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Selection — Choose Who Survives
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Tournament selection: pick best of K random individuals
 */
static inline int entro_select_tournament(
    const LiquidGenome* population,
    int pop_size,
    int tournament_size,
    EntroRNG* rng
) {
    int best_idx = entro_rng_int(rng, pop_size);
    float best_fitness = population[best_idx].fitness;

    for (int i = 1; i < tournament_size; i++) {
        int idx = entro_rng_int(rng, pop_size);
        if (population[idx].fitness > best_fitness) {
            best_idx = idx;
            best_fitness = population[idx].fitness;
        }
    }

    return best_idx;
}

/**
 * Find elite (top K individuals by fitness)
 *
 * @param population  Population array
 * @param pop_size    Population size
 * @param elite_idx   Output: indices of top K (must be size K)
 * @param K           Number of elite to find
 */
static inline void entro_find_elite(
    const LiquidGenome* population,
    int pop_size,
    int* elite_idx,
    int K
) {
    /* Simple O(K*N) selection - fine for small K */
    for (int k = 0; k < K; k++) {
        float best_fitness = -INFINITY;
        int best_idx = 0;

        for (int i = 0; i < pop_size; i++) {
            /* Skip already selected */
            int already_selected = 0;
            for (int j = 0; j < k; j++) {
                if (elite_idx[j] == i) {
                    already_selected = 1;
                    break;
                }
            }
            if (already_selected) continue;

            if (population[i].fitness > best_fitness) {
                best_fitness = population[i].fitness;
                best_idx = i;
            }
        }

        elite_idx[k] = best_idx;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Evolution Loop — The Main Driver
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    int population_size;
    int elite_count;        /* Number of elite to preserve */
    int tournament_size;    /* Tournament selection size */
    float crossover_rate;   /* Probability of crossover vs. mutation */
    MutationParams mutation;
} EvolutionParams;

static const EvolutionParams EVOLUTION_DEFAULT = {
    .population_size = 64,
    .elite_count = 4,
    .tournament_size = 3,
    .crossover_rate = 0.7f,
    .mutation = {
        .weight_mutation_rate = 0.1f,
        .weight_mutation_std = 0.1f,
        .tau_mutation_rate = 0.05f,
        .tau_mutation_std = 0.2f,
        .topology_mutation_rate = 0.02f,
        .bias_mutation_rate = 0.1f,
        .bias_mutation_std = 0.1f,
    },
};

/**
 * Run one generation of evolution
 *
 * @param population      Current population (modified in place)
 * @param next_population Buffer for next generation (same size as population)
 * @param pop_size        Population size
 * @param params          Evolution parameters
 * @param rng             Random number generator
 * @param next_id         Pointer to next genome ID counter (updated)
 * @param generation      Current generation number
 */
static inline void entro_evolve_generation(
    LiquidGenome* population,
    LiquidGenome* next_population,
    int pop_size,
    const EvolutionParams* params,
    EntroRNG* rng,
    uint32_t* next_id,
    uint32_t generation
) {
    /* Find elite indices */
    int elite_idx[16];  /* Max elite count */
    int elite_count = params->elite_count < 16 ? params->elite_count : 16;
    entro_find_elite(population, pop_size, elite_idx, elite_count);

    /* Copy elite to next generation (elitism) */
    for (int i = 0; i < elite_count; i++) {
        memcpy(&next_population[i], &population[elite_idx[i]], sizeof(LiquidGenome));
        next_population[i].generation = generation;
    }

    /* Fill rest with offspring */
    for (int i = elite_count; i < pop_size; i++) {
        if (entro_rng_float(rng) < params->crossover_rate) {
            /* Crossover */
            int parent_a = entro_select_tournament(population, pop_size, params->tournament_size, rng);
            int parent_b = entro_select_tournament(population, pop_size, params->tournament_size, rng);

            entro_crossover_uniform(
                &population[parent_a],
                &population[parent_b],
                &next_population[i],
                rng,
                (*next_id)++,
                generation
            );

            /* Mutate offspring */
            entro_mutate(&next_population[i], &params->mutation, rng);
        } else {
            /* Mutation only */
            int parent = entro_select_tournament(population, pop_size, params->tournament_size, rng);
            memcpy(&next_population[i], &population[parent], sizeof(LiquidGenome));
            next_population[i].id = (*next_id)++;
            next_population[i].generation = generation;
            next_population[i].parent_a = population[parent].id;
            next_population[i].parent_b = 0;

            entro_mutate(&next_population[i], &params->mutation, rng);
        }
    }

    /* Swap populations (caller should swap pointers) */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Genome Export — Freeze the Winner
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Print genome as C header (to stdout or file)
 *
 * This "freezes" a genome into a production-ready header.
 */
static inline void entro_export_header(
    const LiquidGenome* genome,
    const char* name,
    FILE* out
) {
    const int in_dim = genome->input_dim;
    const int hid_dim = genome->hidden_dim;
    const int out_dim = genome->output_dim;
    const int concat_dim = in_dim + hid_dim;

    fprintf(out, "/*\n");
    fprintf(out, " * %s_evolved.h — Evolved CfC Chip\n", name);
    fprintf(out, " *\n");
    fprintf(out, " * Auto-generated by EntroMorph\n");
    fprintf(out, " * Genome ID: %u, Generation: %u\n", genome->id, genome->generation);
    fprintf(out, " * Fitness: %f\n", genome->fitness);
    fprintf(out, " */\n\n");

    fprintf(out, "#ifndef %s_EVOLVED_H\n", name);
    fprintf(out, "#define %s_EVOLVED_H\n\n", name);

    fprintf(out, "#define %s_INPUT_DIM  %d\n", name, in_dim);
    fprintf(out, "#define %s_HIDDEN_DIM %d\n", name, hid_dim);
    fprintf(out, "#define %s_OUTPUT_DIM %d\n\n", name, out_dim);

    /* Export tau */
    fprintf(out, "static const float %s_tau[%d] = {\n    ", name, hid_dim);
    for (int i = 0; i < hid_dim; i++) {
        fprintf(out, "%.8ff%s", genome->tau[i], i < hid_dim - 1 ? ", " : "");
        if ((i + 1) % 8 == 0 && i < hid_dim - 1) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n\n");

    /* Export W_gate */
    fprintf(out, "static const float %s_W_gate[%d] = {\n", name, hid_dim * concat_dim);
    for (int i = 0; i < hid_dim * concat_dim; i++) {
        if (i % 8 == 0) fprintf(out, "    ");
        fprintf(out, "%.8ff%s", genome->W_gate[i], i < hid_dim * concat_dim - 1 ? ", " : "");
        if ((i + 1) % 8 == 0) fprintf(out, "\n");
    }
    fprintf(out, "};\n\n");

    fprintf(out, "static const float %s_b_gate[%d] = {\n    ", name, hid_dim);
    for (int i = 0; i < hid_dim; i++) {
        fprintf(out, "%.8ff%s", genome->b_gate[i], i < hid_dim - 1 ? ", " : "");
    }
    fprintf(out, "\n};\n\n");

    /* Export W_cand */
    fprintf(out, "static const float %s_W_cand[%d] = {\n", name, hid_dim * concat_dim);
    for (int i = 0; i < hid_dim * concat_dim; i++) {
        if (i % 8 == 0) fprintf(out, "    ");
        fprintf(out, "%.8ff%s", genome->W_cand[i], i < hid_dim * concat_dim - 1 ? ", " : "");
        if ((i + 1) % 8 == 0) fprintf(out, "\n");
    }
    fprintf(out, "};\n\n");

    fprintf(out, "static const float %s_b_cand[%d] = {\n    ", name, hid_dim);
    for (int i = 0; i < hid_dim; i++) {
        fprintf(out, "%.8ff%s", genome->b_cand[i], i < hid_dim - 1 ? ", " : "");
    }
    fprintf(out, "\n};\n\n");

    /* Export W_out */
    fprintf(out, "static const float %s_W_out[%d] = {\n", name, hid_dim * out_dim);
    for (int i = 0; i < hid_dim * out_dim; i++) {
        if (i % 8 == 0) fprintf(out, "    ");
        fprintf(out, "%.8ff%s", genome->W_out[i], i < hid_dim * out_dim - 1 ? ", " : "");
        if ((i + 1) % 8 == 0) fprintf(out, "\n");
    }
    fprintf(out, "};\n\n");

    fprintf(out, "static const float %s_b_out[%d] = {\n    ", name, out_dim);
    for (int i = 0; i < out_dim; i++) {
        fprintf(out, "%.8ff%s", genome->b_out[i], i < out_dim - 1 ? ", " : "");
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "#endif /* %s_EVOLVED_H */\n", name);
}

/**
 * Get size of a genome in bytes
 */
static inline size_t entro_genome_size(const LiquidGenome* genome) {
    (void)genome;  /* Size is fixed */
    return sizeof(LiquidGenome);
}

/**
 * Get number of parameters in a genome
 */
static inline int entro_genome_param_count(const LiquidGenome* genome) {
    const int hid_dim = genome->hidden_dim;
    const int in_dim = genome->input_dim;
    const int out_dim = genome->output_dim;
    const int concat_dim = in_dim + hid_dim;

    return hid_dim                     /* tau */
         + hid_dim * concat_dim        /* W_gate */
         + hid_dim                     /* b_gate */
         + hid_dim * concat_dim        /* W_cand */
         + hid_dim                     /* b_cand */
         + hid_dim * out_dim           /* W_out */
         + out_dim;                    /* b_out */
}

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_ENTROMORPH_H */
