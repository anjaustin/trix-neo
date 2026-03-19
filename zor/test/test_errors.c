/*
 * test_errors.c — Unit tests for error handling system
 *
 * Tests CRITICAL 1: Error Handling implementation
 *
 * Compile:
 *   gcc -I../include -o test_errors test_errors.c ../src/errors.c
 *
 * Run:
 *   ./test_errors
 */

#include "trixc/errors.h"
#include <stdio.h>
#include <string.h>
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

/* Test error name/description */
void test_error_strings(void) {
    TEST("error strings");
    
    /* Test success */
    ASSERT(strcmp(trix_error_name(TRIX_OK), "TRIX_OK") == 0);
    ASSERT(strcmp(trix_error_description(TRIX_OK), "Success") == 0);
    
    /* Test file errors */
    ASSERT(strcmp(trix_error_name(TRIX_ERROR_FILE_NOT_FOUND), 
                  "TRIX_ERROR_FILE_NOT_FOUND") == 0);
    ASSERT(strcmp(trix_error_description(TRIX_ERROR_FILE_NOT_FOUND), 
                  "File not found") == 0);
    
    /* Test parse errors */
    ASSERT(strcmp(trix_error_name(TRIX_ERROR_PARSE_FAILED),
                  "TRIX_ERROR_PARSE_FAILED") == 0);
    
    /* Test memory errors */
    ASSERT(strcmp(trix_error_name(TRIX_ERROR_OUT_OF_MEMORY),
                  "TRIX_ERROR_OUT_OF_MEMORY") == 0);
    
    PASS();
}

/* Test error context initialization */
void test_error_init(void) {
    TEST("error context initialization");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    ASSERT(ctx.code == TRIX_OK);
    ASSERT(ctx.message[0] == '\0');
    ASSERT(ctx.context[0] == '\0');
    ASSERT(ctx.line == -1);
    ASSERT(ctx.column == -1);
    ASSERT(ctx.source_line == -1);
    ASSERT(ctx.file == NULL);
    ASSERT(ctx.function == NULL);
    
    PASS();
}

/* Test error setting */
void test_error_set(void) {
    TEST("error set");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    trix_error_set(&ctx, TRIX_ERROR_FILE_NOT_FOUND, 
                   "Cannot open file '%s'", "test.trix");
    
    ASSERT(ctx.code == TRIX_ERROR_FILE_NOT_FOUND);
    ASSERT(strstr(ctx.message, "test.trix") != NULL);
    
    PASS();
}

/* Test error with location */
void test_error_location(void) {
    TEST("error with location");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    trix_error_set_location(&ctx, TRIX_ERROR_PARSE_FAILED,
                           "test.c", 42, "test_function",
                           "Parse failed at line %d", 10);
    
    ASSERT(ctx.code == TRIX_ERROR_PARSE_FAILED);
    ASSERT(ctx.source_line == 42);
    ASSERT(strcmp(ctx.file, "test.c") == 0);
    ASSERT(strcmp(ctx.function, "test_function") == 0);
    ASSERT(strstr(ctx.message, "line 10") != NULL);
    
    PASS();
}

/* Test adding context */
void test_error_context(void) {
    TEST("error context");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    trix_error_set(&ctx, TRIX_ERROR_INVALID_SPEC, "Invalid spec");
    trix_error_add_context(&ctx, "File: %s", "test.trix");
    trix_error_add_context(&ctx, "Section: %s", "signatures");
    
    ASSERT(ctx.code == TRIX_ERROR_INVALID_SPEC);
    ASSERT(strstr(ctx.context, "test.trix") != NULL);
    ASSERT(strstr(ctx.context, "signatures") != NULL);
    
    PASS();
}

/* Test error formatting */
void test_error_format(void) {
    TEST("error formatting");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    trix_error_set_location(&ctx, TRIX_ERROR_INVALID_THRESHOLD,
                           "validator.c", 123, "validate_threshold",
                           "Threshold %d is out of range (0-512)", -1);
    trix_error_add_context(&ctx, "Signature: %s", "example_a");
    
    char buffer[1024];
    trix_error_format(&ctx, buffer, sizeof(buffer));
    
    ASSERT(strstr(buffer, "TRIX_ERROR_INVALID_THRESHOLD") != NULL);
    ASSERT(strstr(buffer, "out of range") != NULL);
    ASSERT(strstr(buffer, "validator.c:123") != NULL);
    ASSERT(strstr(buffer, "validate_threshold") != NULL);
    ASSERT(strstr(buffer, "example_a") != NULL);
    
    PASS();
}

/* Test convenience macros */
trix_error_t helper_function_success(trix_error_context_t* ctx) {
    TRIX_CHECK_NOT_NULL(ctx, ctx, "Context is NULL");
    return TRIX_OK;
}

trix_error_t helper_function_fail(trix_error_context_t* ctx) {
    TRIX_CHECK(false, ctx, TRIX_ERROR_INVALID_ARGUMENT, "Test failure");
    return TRIX_OK;  /* Never reached */
}

void test_macros(void) {
    TEST("convenience macros");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    /* Test success path */
    trix_error_t result = helper_function_success(&ctx);
    ASSERT(result == TRIX_OK);
    
    /* Test failure path */
    result = helper_function_fail(&ctx);
    ASSERT(result == TRIX_ERROR_INVALID_ARGUMENT);
    ASSERT(strstr(ctx.message, "Test failure") != NULL);
    
    PASS();
}

/* Test thread-local storage */
void test_thread_local(void) {
    TEST("thread-local error storage");
    
    /* Clear any previous error */
    trix_error_clear_last();
    
    trix_error_context_t* last = trix_error_get_last();
    ASSERT(last != NULL);
    ASSERT(last->code == TRIX_OK);
    
    /* Set error */
    trix_error_set_last(TRIX_ERROR_OUT_OF_MEMORY, "Test OOM");
    
    last = trix_error_get_last();
    ASSERT(last->code == TRIX_ERROR_OUT_OF_MEMORY);
    ASSERT(strstr(last->message, "Test OOM") != NULL);
    
    /* Clear error */
    trix_error_clear_last();
    
    last = trix_error_get_last();
    ASSERT(last->code == TRIX_OK);
    
    PASS();
}

/* Main test runner */
int main(void) {
    printf("\n");
    printf("╭────────────────────────────────────────────────────────────╮\n");
    printf("│  TriX Error Handling Unit Tests                           │\n");
    printf("│  CRITICAL 1: Error Handling                               │\n");
    printf("╰────────────────────────────────────────────────────────────╯\n");
    printf("\n");
    
    /* Run tests */
    test_error_strings();
    test_error_init();
    test_error_set();
    test_error_location();
    test_error_context();
    test_error_format();
    test_macros();
    test_thread_local();
    
    /* Summary */
    printf("\n");
    printf("════════════════════════════════════════════════════════════\n");
    if (tests_passed == tests_run) {
        printf("  ALL TESTS PASSED (%d/%d)\n", tests_passed, tests_run);
        printf("════════════════════════════════════════════════════════════\n");
        printf("\n");
        return 0;
    } else {
        printf("  SOME TESTS FAILED (%d/%d passed)\n", tests_passed, tests_run);
        printf("════════════════════════════════════════════════════════════\n");
        printf("\n");
        return 1;
    }
}
