/*
 * test_linear_runtime.c — Tests for linear layer forward pass and binarization
 */

#include "../include/trixc/runtime.h"
#include "../include/trixc/linear_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ── test 1: portable matvec correctness ─────────────────────────────────── */

static int test_portable_matvec(void) {
    printf("[TEST] portable_matvec\n");

    /*
     * K=4, N=2
     * W = [[1, 0, -1, 1],   (row 0)
     *       [0, 1,  1, -1]]  (row 1)
     * x = [1, 2, 3, 4]
     * y[0] = 1*1 + 0*2 + (-1)*3 + 1*4 = 2
     * y[1] = 0*1 + 1*2 + 1*3 + (-1)*4 = 1
     */
    int8_t W[8] = {1, 0, -1, 1, 0, 1, 1, -1};
    int8_t x[4] = {1, 2, 3, 4};
    int32_t y[2] = {0};

    int ret = linear_matvec_portable(4, 2, x, W, y);
    if (ret != 0) {
        fprintf(stderr, "FAIL portable_matvec: returned %d\n", ret);
        return 1;
    }

    if (y[0] != 2) {
        fprintf(stderr, "FAIL portable_matvec: y[0]=%d want 2\n", y[0]);
        return 1;
    }
    if (y[1] != 1) {
        fprintf(stderr, "FAIL portable_matvec: y[1]=%d want 1\n", y[1]);
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

/* ── test 2: sign binarize ───────────────────────────────────────────────── */

static int test_sign_binarize(void) {
    printf("[TEST] sign_binarize\n");

    /*
     * 8 values → 1 byte (MSB first):
     * z = [1, -1, 5, -3, 0, 2, -1, 7]
     * bits (> 0 → 1): [1, 0, 1, 0, 0, 1, 0, 1] → 0b10100101 = 0xA5
     */
    int32_t z[8] = {1, -1, 5, -3, 0, 2, -1, 7};
    uint8_t out[1] = {0};

    linear_sign_binarize(z, 8, out);

    if (out[0] != 0xA5) {
        fprintf(stderr, "FAIL sign_binarize: got 0x%02x want 0xA5\n", out[0]);
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

/* ── test 3: end-to-end weight load + trix_exec_linear ───────────────────── */

static int test_exec_linear_end_to_end(void) {
    printf("[TEST] exec_linear_end_to_end\n");

    /*
     * K=4, N=512 (state_bits=512)
     * W[0][0] = 1, all other weights = 0
     * x = [1, 0, 0, 0]
     * y[0] = 1 > 0 → bit 1
     * y[1..511] = 0 ≤ 0 → bit 0
     * Expected out[0] = 0x80, out[1..63] = 0x00
     */

    /* Write weight file: 512 rows × 4 columns = 2048 bytes */
    const char *wfile = "/tmp/test_linear_weights.bin";
    int8_t *W = calloc(512 * 4, 1);
    if (!W) { fprintf(stderr, "FAIL alloc\n"); return 1; }
    W[0] = 1;  /* W[0][0] = 1 */
    FILE *f = fopen(wfile, "wb");
    if (!f) { free(W); fprintf(stderr, "FAIL open weights file\n"); return 1; }
    fwrite(W, 1, 512 * 4, f);
    fclose(f);
    free(W);

    /* Write .trix file */
    const char *tfile = "/tmp/test_linear_chip.trix";
    f = fopen(tfile, "w");
    if (!f) { fprintf(stderr, "FAIL open trix file\n"); return 1; }
    fprintf(f, "softchip:\n  name: test_linear\n  version: 1.0.0\n");
    fprintf(f, "state:\n  bits: 512\n");
    fprintf(f, "linear:\n");
    fprintf(f, "  embed:\n");
    fprintf(f, "    input_dim: 4\n");
    fprintf(f, "    output_dim: 512\n");
    fprintf(f, "    weights: %s\n", wfile);
    fprintf(f, "inference:\n  mode: first_match\n  default: unknown\n");
    fclose(f);

    int err = 0;
    trix_chip_t *chip = trix_load(tfile, &err);
    if (!chip) {
        fprintf(stderr, "FAIL trix_load error=%d\n", err);
        return 1;
    }

    uint8_t input[64] = {1, 0, 0, 0};  /* rest zero-initialized */
    uint8_t out[64] = {0};

    int rc = trix_exec_linear(chip, input, out);
    trix_chip_free(chip);

    if (rc != 0) {
        fprintf(stderr, "FAIL trix_exec_linear returned %d\n", rc);
        return 1;
    }
    if (out[0] != 0x80) {
        fprintf(stderr, "FAIL out[0]=0x%02x want 0x80\n", out[0]);
        return 1;
    }
    for (int i = 1; i < 64; i++) {
        if (out[i] != 0) {
            fprintf(stderr, "FAIL out[%d]=0x%02x want 0x00\n", i, out[i]);
            return 1;
        }
    }
    printf("  PASS\n");
    return 0;
}

/* ── test 4: zero linear layers returns error ─────────────────────────────── */

static int test_exec_linear_no_layers(void) {
    printf("[TEST] exec_linear_no_layers\n");

    /* Build a chip with no linear layers */
    const char *tfile = "/tmp/test_nolayers.trix";
    FILE *f = fopen(tfile, "w");
    fprintf(f, "softchip:\n  name: nolayers\n  version: 1.0.0\n");
    fprintf(f, "state:\n  bits: 512\n");
    fprintf(f, "signatures:\n  sig0:\n    pattern: ");
    for (int i = 0; i < 64; i++) fprintf(f, "00");
    fprintf(f, "\n    threshold: 0\n");
    fprintf(f, "inference:\n  mode: first_match\n  default: unknown\n");
    fclose(f);

    int err = 0;
    trix_chip_t *chip = trix_load(tfile, &err);
    if (!chip) {
        fprintf(stderr, "FAIL trix_load error=%d\n", err);
        return 1;
    }

    uint8_t input[64] = {0};
    uint8_t out[64]   = {0};
    int rc = trix_exec_linear(chip, input, out);
    trix_chip_free(chip);

    /* Should return non-zero (error: no layers) */
    if (rc == 0) {
        fprintf(stderr, "FAIL: expected error on zero layers, got 0\n");
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

/* ── test 5: trix_infer calls linear layers automatically ─────────────────── */

static int test_infer_with_linear_layers(void) {
    printf("[TEST] infer_with_linear_layers\n");

    /* Same chip as test 3: K=4, N=512, W[0][0]=1 */
    const char *wfile = "/tmp/test_linear_weights.bin";
    /* File already created by test 3; recreate to be safe */
    int8_t *W = calloc(512 * 4, 1);
    if (!W) return 1;
    W[0] = 1;
    FILE *f = fopen(wfile, "wb");
    if (!f) { free(W); return 1; }
    fwrite(W, 1, 512 * 4, f);
    fclose(f);
    free(W);

    /* .trix with one linear layer + one signature matching expected output */
    const char *tfile = "/tmp/test_infer_linear.trix";
    f = fopen(tfile, "w");
    fprintf(f, "softchip:\n  name: infer_linear\n  version: 1.0.0\n");
    fprintf(f, "state:\n  bits: 512\n");
    fprintf(f, "linear:\n  embed:\n");
    fprintf(f, "    input_dim: 4\n    output_dim: 512\n    weights: %s\n", wfile);
    fprintf(f, "signatures:\n  match:\n    pattern: ");
    /* out[0]=0x80, out[1..63]=0x00 */
    fprintf(f, "80");
    for (int i = 1; i < 64; i++) fprintf(f, "00");
    fprintf(f, "\n    threshold: 0\n");
    fprintf(f, "inference:\n  mode: first_match\n  default: unknown\n");
    fclose(f);

    int err = 0;
    trix_chip_t *chip = trix_load(tfile, &err);
    if (!chip) {
        fprintf(stderr, "FAIL trix_load error=%d\n", err);
        return 1;
    }

    uint8_t input[64] = {1, 0, 0, 0};
    trix_result_t result = trix_infer(chip, input);
    trix_chip_free(chip);

    if (result.match != 0) {
        fprintf(stderr, "FAIL match=%d want 0\n", result.match);
        return 1;
    }
    if (result.distance != 0) {
        fprintf(stderr, "FAIL distance=%d want 0\n", result.distance);
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void) {
    int failures = 0;
    failures += test_portable_matvec();
    failures += test_sign_binarize();
    failures += test_exec_linear_end_to_end();
    failures += test_exec_linear_no_layers();
    failures += test_infer_with_linear_layers();
    if (failures == 0) {
        printf("All linear_runtime tests passed.\n");
    } else {
        printf("%d linear_runtime test(s) FAILED.\n", failures);
    }
    return failures;
}
