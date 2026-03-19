/*
 * test_memory.c — Memory Safety Unit Tests
 *
 * Tests CRITICAL 3: Memory Safety implementation
 * Run with valgrind to detect leaks
 *
 * Compile:
 *   gcc -I../include -o test_memory test_memory.c ../src/errors.c -std=c11
 *
 * Run:
 *   ./test_memory
 *   valgrind --leak-check=full ./test_memory
 */

#include "trixc/memory.h"
#include "trixc/errors.h"
#include <stdio.h>
#include <assert.h>

/* Test counter */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("Running: %s ... ", name); \
        fflush(stdout); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASS\n"); \
    } while (0)

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL\n  Assertion failed: %s\n", #cond); \
            return; \
        } \
    } while (0)

/* Test safe malloc */
void test_safe_malloc(void) {
    TEST("safe malloc");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    /* Normal allocation */
    void* ptr = trix_malloc(1024, &ctx);
    ASSERT(ptr != NULL);
    ASSERT(ctx.code == TRIX_OK);
    
    /* Check zero initialization */
    char* bytes = (char*)ptr;
    bool all_zero = true;
    for (int i = 0; i < 1024; i++) {
        if (bytes[i] != 0) {
            all_zero = false;
            break;
        }
    }
    ASSERT(all_zero);
    
    /* Free safely */
    trix_free(ptr);
    ASSERT(ptr == NULL);  /* Macro NULLs the pointer */
    
    /* Double free is safe (does nothing) */
    trix_free(ptr);
    
    PASS();
}

/* Test safe calloc */
void test_safe_calloc(void) {
    TEST("safe calloc");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    /* Allocate array */
    int* array = (int*)trix_calloc(100, sizeof(int), &ctx);
    ASSERT(array != NULL);
    ASSERT(ctx.code == TRIX_OK);
    
    /* Check zero initialization */
    for (int i = 0; i < 100; i++) {
        ASSERT(array[i] == 0);
    }
    
    /* Free */
    trix_free(array);
    ASSERT(array == NULL);
    
    PASS();
}

/* Test safe realloc */
void test_safe_realloc(void) {
    TEST("safe realloc");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    /* Initial allocation */
    char* buf = (char*)trix_malloc(10, &ctx);
    ASSERT(buf != NULL);
    strcpy(buf, "hello");
    
    /* Grow */
    char* new_buf = (char*)trix_realloc(buf, 100, &ctx);
    ASSERT(new_buf != NULL);
    ASSERT(strcmp(new_buf, "hello") == 0);
    
    buf = new_buf;
    
    /* Shrink */
    new_buf = (char*)trix_realloc(buf, 10, &ctx);
    ASSERT(new_buf != NULL);
    
    trix_free(new_buf);
    
    PASS();
}

/* Test safe string copy */
void test_safe_strcpy(void) {
    TEST("safe string copy");
    
    char dest[10];
    
    /* Normal copy */
    bool result = trix_strcpy_safe(dest, "hello", sizeof(dest));
    ASSERT(result == true);
    ASSERT(strcmp(dest, "hello") == 0);
    
    /* Truncation */
    result = trix_strcpy_safe(dest, "this is too long", sizeof(dest));
    ASSERT(result == false);  /* Truncated */
    ASSERT(dest[sizeof(dest) - 1] == '\0');  /* Null terminated */
    ASSERT(strlen(dest) == sizeof(dest) - 1);
    
    /* NULL handling */
    result = trix_strcpy_safe(NULL, "test", 10);
    ASSERT(result == false);
    
    result = trix_strcpy_safe(dest, NULL, 10);
    ASSERT(result == false);
    
    PASS();
}

/* Test safe string concatenation */
void test_safe_strcat(void) {
    TEST("safe string concatenation");
    
    char dest[20] = "hello";
    
    /* Normal concatenation */
    bool result = trix_strcat_safe(dest, " world", sizeof(dest));
    ASSERT(result == true);
    ASSERT(strcmp(dest, "hello world") == 0);
    
    /* Truncation */
    strcpy(dest, "hello");
    result = trix_strcat_safe(dest, " this is way too long", sizeof(dest));
    ASSERT(result == false);  /* Truncated */
    ASSERT(dest[sizeof(dest) - 1] == '\0');  /* Null terminated */
    
    PASS();
}

/* Test safe sprintf */
void test_safe_sprintf(void) {
    TEST("safe sprintf");
    
    char buf[20];
    
    /* Normal formatting */
    int n = trix_sprintf_safe(buf, sizeof(buf), "value = %d", 42);
    ASSERT(n > 0);
    ASSERT(strcmp(buf, "value = 42") == 0);
    
    /* Truncation */
    n = trix_sprintf_safe(buf, sizeof(buf), 
                         "this is a very long string that will be truncated");
    ASSERT(n > 0);
    ASSERT(buf[sizeof(buf) - 1] == '\0');
    
    PASS();
}

/* Test bounds checking */
void test_bounds_checking(void) {
    TEST("bounds checking");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    int array[10] = {0};
    
    /* Valid access */
    trix_error_t result = trix_check_bounds(5, 10, &ctx);
    ASSERT(result == TRIX_OK);
    
    /* Out of bounds (negative) */
    result = trix_check_bounds(-1, 10, &ctx);
    ASSERT(result == TRIX_ERROR_BUFFER_OVERFLOW);
    
    /* Out of bounds (too large) */
    result = trix_check_bounds(10, 10, &ctx);
    ASSERT(result == TRIX_ERROR_BUFFER_OVERFLOW);
    
    /* Edge cases */
    result = trix_check_bounds(0, 10, &ctx);
    ASSERT(result == TRIX_OK);
    
    result = trix_check_bounds(9, 10, &ctx);
    ASSERT(result == TRIX_OK);
    
    PASS();
}

/* Test buffer size checking */
void test_buffer_size_check(void) {
    TEST("buffer size checking");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    /* Fits */
    trix_error_t result = trix_check_buffer_size(10, 20, &ctx);
    ASSERT(result == TRIX_OK);
    
    /* Exactly fits */
    result = trix_check_buffer_size(20, 20, &ctx);
    ASSERT(result == TRIX_OK);
    
    /* Too large */
    result = trix_check_buffer_size(30, 20, &ctx);
    ASSERT(result == TRIX_ERROR_BUFFER_TOO_SMALL);
    
    PASS();
}

/* Test safe strdup */
void test_safe_strdup(void) {
    TEST("safe strdup");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    /* Normal duplicate */
    char* dup = trix_strdup_safe("hello", &ctx);
    ASSERT(dup != NULL);
    ASSERT(strcmp(dup, "hello") == 0);
    
    /* Verify it's a copy */
    dup[0] = 'H';
    ASSERT(strcmp(dup, "Hello") == 0);
    
    trix_free(dup);
    ASSERT(dup == NULL);
    
    /* NULL handling */
    dup = trix_strdup_safe(NULL, &ctx);
    ASSERT(dup == NULL);
    
    PASS();
}

/* Test safe memdup */
void test_safe_memdup(void) {
    TEST("safe memdup");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    int original[] = {1, 2, 3, 4, 5};
    
    /* Duplicate */
    int* dup = (int*)trix_memdup_safe(original, sizeof(original), &ctx);
    ASSERT(dup != NULL);
    
    /* Verify copy */
    for (int i = 0; i < 5; i++) {
        ASSERT(dup[i] == original[i]);
    }
    
    /* Verify it's a copy (not a reference) */
    dup[0] = 999;
    ASSERT(original[0] == 1);
    ASSERT(dup[0] == 999);
    
    trix_free(dup);
    
    PASS();
}

/* Test cleanup pattern */
void test_cleanup_pattern(void) {
    TEST("cleanup pattern");
    
    /* Simulate function with multiple allocations and error paths */
    FILE* file = NULL;
    char* buffer1 = NULL;
    char* buffer2 = NULL;
    trix_error_t result = TRIX_OK;
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    /* Allocation 1 */
    buffer1 = (char*)trix_malloc(100, &ctx);
    if (buffer1 == NULL) {
        result = TRIX_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }
    
    /* Allocation 2 */
    buffer2 = (char*)trix_malloc(200, &ctx);
    if (buffer2 == NULL) {
        result = TRIX_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }
    
    /* Simulate error condition */
    if (1) {  /* Always true for test */
        result = TRIX_ERROR_INTERNAL;
        goto cleanup;
    }
    
cleanup:
    /* All resources freed, even on error */
    if (file != NULL) {
        fclose(file);
    }
    trix_free(buffer1);  /* Safe even if NULL */
    trix_free(buffer2);
    
    /* Verify cleanup worked */
    ASSERT(buffer1 == NULL);
    ASSERT(buffer2 == NULL);
    ASSERT(result == TRIX_ERROR_INTERNAL);
    
    PASS();
}

/* Main test runner */
int main(void) {
    printf("\n");
    printf("╭────────────────────────────────────────────────────────────╮\n");
    printf("│  TriX Memory Safety Unit Tests                            │\n");
    printf("│  CRITICAL 3: Memory Safety                                │\n");
    printf("╰────────────────────────────────────────────────────────────╯\n");
    printf("\n");
    
    /* Run tests */
    test_safe_malloc();
    test_safe_calloc();
    test_safe_realloc();
    test_safe_strcpy();
    test_safe_strcat();
    test_safe_sprintf();
    test_bounds_checking();
    test_buffer_size_check();
    test_safe_strdup();
    test_safe_memdup();
    test_cleanup_pattern();
    
    /* Summary */
    printf("\n");
    printf("════════════════════════════════════════════════════════════\n");
    if (tests_passed == tests_run) {
        printf("  ALL TESTS PASSED (%d/%d)\n", tests_passed, tests_run);
        printf("════════════════════════════════════════════════════════════\n");
        printf("\n");
        printf("✅ Run with valgrind to verify zero memory leaks:\n");
        printf("   valgrind --leak-check=full --show-leak-kinds=all ./test_memory\n");
        printf("\n");
        return 0;
    } else {
        printf("  SOME TESTS FAILED (%d/%d passed)\n", tests_passed, tests_run);
        printf("════════════════════════════════════════════════════════════\n");
        printf("\n");
        return 1;
    }
}
