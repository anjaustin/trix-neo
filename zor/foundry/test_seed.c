/*
 * test_seed.c — Verify the generated seed works
 *
 * Build:
 *   gcc -O3 test_seed.c -o test_seed -lm
 *
 * Run:
 *   ./test_seed
 */

#include <stdio.h>
#include <math.h>
#include "seeds/sine_seed.h"

int main(void) {
    printf("\n");
    printf("╭──────────────────────────────────────────────────────────────╮\n");
    printf("│  Sine Seed Verification                                      │\n");
    printf("│  \"The seed that grew in 60 seconds\"                         │\n");
    printf("╰──────────────────────────────────────────────────────────────╯\n");
    printf("\n");

    printf("Seed Statistics:\n");
    printf("  Nodes: %d\n", sine_SEED_NODES);
    printf("  Registers: %d\n", sine_SEED_REGS);
    printf("  Fitness: %.0f\n", sine_SEED_FITNESS);
    printf("  Generation: %d\n", sine_SEED_GEN);
    printf("\n");

    /* Initialize state */
    float state[sine_SEED_REGS] = {0};
    sine_seed_reset(state);

    /* Test on complex sine wave (same as genesis curriculum) */
    printf("Prediction Test (100 samples, showing first 20):\n");
    printf("  t  |  Input  | Predict |  Truth  | Error | Dir\n");
    printf("-----+---------+---------+---------+-------+-----\n");

    int correct_dir = 0;
    float total_error = 0.0f;
    float output_sum = 0.0f;
    float output_sq_sum = 0.0f;

    #define TEST_SAMPLES 100

    for (int t = 0; t < TEST_SAMPLES; t++) {
        /* Complex signal matching genesis curriculum */
        float phase1 = t * 0.1f;
        float phase2 = t * 0.4f;
        float phase3 = t * 0.023f;
        float input = 0.6f * sinf(phase1) + 0.25f * sinf(phase2) + 0.15f * sinf(phase3);

        float phase1_next = (t + 1) * 0.1f;
        float phase2_next = (t + 1) * 0.4f;
        float phase3_next = (t + 1) * 0.023f;
        float truth = 0.6f * sinf(phase1_next) + 0.25f * sinf(phase2_next) + 0.15f * sinf(phase3_next);

        float output = sine_seed_step(input, state);

        float error = fabsf(output - truth);
        total_error += error;

        /* Derivative prediction: does output sign match slope? */
        float derivative = truth - input;
        int pred_rising = (output > 0) ? 1 : 0;
        int true_rising = (derivative > 0) ? 1 : 0;
        int dir_correct = (pred_rising == true_rising) || (fabsf(derivative) < 0.01f);
        correct_dir += dir_correct;

        /* Only print first 20 samples */
        if (t < 20) {
            printf(" %2d  | %6.3f  | %6.3f  | %6.3f  | %5.3f | %s\n",
                   t, input, output, truth, error, dir_correct ? "Y" : "N");
        }

        output_sum += output;
        output_sq_sum += output * output;
    }

    /* Calculate output variance */
    float mean = output_sum / TEST_SAMPLES;
    float variance = (output_sq_sum / TEST_SAMPLES) - (mean * mean);

    printf("  ... (%d more samples)\n", TEST_SAMPLES - 20);
    /* Compute correlation with input signal */
    float sum_xy = 0.0f, sum_x = 0.0f, sum_y = 0.0f;
    float sum_x2 = 0.0f, sum_y2 = 0.0f;

    /* Reset and recompute outputs for correlation */
    sine_seed_reset(state);
    for (int t = 0; t < TEST_SAMPLES; t++) {
        float phase1 = t * 0.1f;
        float phase2 = t * 0.4f;
        float phase3 = t * 0.023f;
        float input = 0.6f * sinf(phase1) + 0.25f * sinf(phase2) + 0.15f * sinf(phase3);

        float output = sine_seed_step(input, state);

        sum_xy += output * input;
        sum_x += output;
        sum_y += input;
        sum_x2 += output * output;
        sum_y2 += input * input;
    }

    float n = (float)TEST_SAMPLES;
    float corr_num = n * sum_xy - sum_x * sum_y;
    float corr_den1 = n * sum_x2 - sum_x * sum_x;
    float corr_den2 = n * sum_y2 - sum_y * sum_y;
    float corr_den = sqrtf(fabsf(corr_den1) * fabsf(corr_den2));
    float correlation = (corr_den > 0.001f) ? (corr_num / corr_den) : 0.0f;

    printf("\n");
    printf("Results (over %d samples):\n", TEST_SAMPLES);
    printf("  Mean absolute error: %.3f\n", total_error / TEST_SAMPLES);
    printf("  Output variance: %.4f\n", variance);
    printf("  Output mean: %.3f\n", mean);
    printf("  Signal correlation: %.3f\n", correlation);
    printf("\n");

    /* Verdict based on correlation */
    if (variance < 0.01f) {
        printf("VERDICT: DC TRAP (flat output)\n");
    } else if (correlation > 0.9f) {
        printf("VERDICT: EXCELLENT TRACKER (r=%.2f)\n", correlation);
    } else if (correlation > 0.7f) {
        printf("VERDICT: GOOD TRACKER (r=%.2f)\n", correlation);
    } else if (correlation > 0.5f) {
        printf("VERDICT: MODERATE TRACKER (r=%.2f)\n", correlation);
    } else {
        printf("VERDICT: WEAK TRACKING (r=%.2f)\n", correlation);
    }
    printf("\n");

    return 0;
}
