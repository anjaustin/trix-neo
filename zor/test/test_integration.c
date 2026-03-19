/*
 * test_integration.c — Integration tests for TriX runtime
 *
 * Tests CRITICAL ITEM 8: Testing - Integration scenarios
 * 
 * Tests multiple APIs working together in realistic scenarios
 */

#include "../include/trixc/errors.h"
#include "../include/trixc/logging.h"
#include "../include/trixc/memory.h"
#include "../include/trixc/validation.h"
#include "../include/trixc/thread.h"
#include "../include/trixc/version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test stats */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("  Testing: %-50s", name); \
    fflush(stdout);

#define PASS() \
    do { \
        printf(" ✓\n"); \
        tests_passed++; \
        return; \
    } while (0)

#define FAIL(msg) \
    do { \
        printf(" ✗ %s\n", msg); \
        tests_failed++; \
        return; \
    } while (0)

#define ASSERT(cond, msg) \
    if (!(cond)) FAIL(msg)

/*═══════════════════════════════════════════════════════════════════════════
 * Integration Test 1: Error Handling + Logging
 *═══════════════════════════════════════════════════════════════════════════*/

void test_error_with_logging() {
    TEST("Error handling with logging integration");
    
    /* Initialize logging */
    trix_log_init();
    trix_log_set_level(TRIX_LOG_ERROR);
    
    /* Trigger an error and log it */
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    trix_error_set(&ctx, TRIX_ERROR_FILE_NOT_FOUND, "test.txt", 42, __func__);
    
    log_error("Error occurred: %s (code: %d)", 
              ctx.message, ctx.code);
    
    ASSERT(ctx.code == TRIX_ERROR_FILE_NOT_FOUND, "Error code mismatch");
    
    trix_log_shutdown();
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Integration Test 2: Memory Safety + Error Handling
 *═══════════════════════════════════════════════════════════════════════════*/

void test_memory_with_errors() {
    TEST("Memory safety with error handling");
    
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    /* Allocate memory with error checking */
    void* ptr = trix_malloc(1024, &ctx);
    ASSERT(ptr != NULL, "Allocation should succeed");
    ASSERT(ctx.code == TRIX_OK, "Should have no error");
    
    /* Safe string operations */
    char buffer[32];
    bool result = trix_strcpy_safe(buffer, "Hello, World!", sizeof(buffer));
    ASSERT(result, "Safe strcpy should succeed");
    ASSERT(strcmp(buffer, "Hello, World!") == 0, "String should match");
    
    /* Cleanup */
    free(ptr);
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Integration Test 3: Input Validation + Error Reporting
 *═══════════════════════════════════════════════════════════════════════════*/

void test_validation_with_errors() {
    TEST("Input validation with error context");
    
    /* Test valid input */
    ASSERT(trix_validate_int(50, 0, 100), "Valid int should pass");
    
    /* Test invalid input and check error can be reported */
    if (!trix_validate_int(150, 0, 100)) {
        log_warn("Validation failed: value 150 out of range [0, 100]");
    }
    
    /* Test email validation with error logging */
    const char* test_emails[] = {
        "valid@example.com",
        "invalid-email",
        NULL
    };
    
    for (int i = 0; test_emails[i] != NULL; i++) {
        if (!trix_validate_email(test_emails[i])) {
            log_info("Invalid email rejected: %s", test_emails[i]);
        }
    }
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Integration Test 4: Thread Safety + Logging
 *═══════════════════════════════════════════════════════════════════════════*/

static trix_mutex_t test_mutex;
static int shared_counter = 0;

void* threaded_log_worker(void* arg) {
    int iterations = *(int*)arg;
    
    for (int i = 0; i < iterations; i++) {
        trix_mutex_lock(&test_mutex);
        shared_counter++;
        log_debug("Thread %llu: counter = %d", trix_thread_id(), shared_counter);
        trix_mutex_unlock(&test_mutex);
    }
    
    return NULL;
}

void test_threading_with_logging() {
    TEST("Thread safety with logging");
    
    trix_log_init();
    trix_log_set_level(TRIX_LOG_INFO);
    
    trix_mutex_init(&test_mutex);
    shared_counter = 0;
    
    const int NUM_THREADS = 4;
    const int ITERATIONS = 100;
    trix_thread_t threads[NUM_THREADS];
    int iter_counts[NUM_THREADS];
    
    /* Create threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        iter_counts[i] = ITERATIONS;
        trix_thread_create(&threads[i], threaded_log_worker, &iter_counts[i]);
    }
    
    /* Join threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        trix_thread_join(threads[i], NULL);
    }
    
    ASSERT(shared_counter == NUM_THREADS * ITERATIONS, "Counter should be correct");
    
    trix_mutex_destroy(&test_mutex);
    trix_log_shutdown();
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Integration Test 5: Version Info + Logging
 *═══════════════════════════════════════════════════════════════════════════*/

void test_version_with_logging() {
    TEST("Version information with logging");
    
    trix_log_init();
    trix_log_set_level(TRIX_LOG_INFO);
    
    const trix_version_info_t* version = trix_get_version();
    
    log_info("TriX Runtime Version: %s", version->version_string);
    log_info("API Version: %d", version->api_version);
    log_info("Platform: %s", trix_platform_name());
    log_info("Compiler: %s", trix_compiler_name());
    
    ASSERT(version->major == TRIX_VERSION_MAJOR, "Version major should match");
    ASSERT(version->minor == TRIX_VERSION_MINOR, "Version minor should match");
    
    trix_log_shutdown();
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Integration Test 6: Memory + Thread Safety
 *═══════════════════════════════════════════════════════════════════════════*/

static trix_atomic_t allocation_count;

void* memory_stress_worker(void* arg) {
    int iterations = *(int*)arg;
    trix_error_context_t ctx;
    
    for (int i = 0; i < iterations; i++) {
        trix_error_init(&ctx);
        
        /* Allocate */
        void* ptr = trix_malloc(1024, &ctx);
        if (ptr) {
            trix_atomic_add(&allocation_count, 1);
            
            /* Use the memory */
            memset(ptr, 0xAA, 1024);
            
            /* Free */
            free(ptr);
            trix_atomic_sub(&allocation_count, 1);
        }
    }
    
    return NULL;
}

void test_memory_thread_safety() {
    TEST("Memory operations with thread safety");
    
    trix_atomic_store(&allocation_count, 0);
    
    const int NUM_THREADS = 4;
    const int ITERATIONS = 50;
    trix_thread_t threads[NUM_THREADS];
    int iter_counts[NUM_THREADS];
    
    /* Create threads doing memory operations */
    for (int i = 0; i < NUM_THREADS; i++) {
        iter_counts[i] = ITERATIONS;
        trix_thread_create(&threads[i], memory_stress_worker, &iter_counts[i]);
    }
    
    /* Join threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        trix_thread_join(threads[i], NULL);
    }
    
    /* All allocations should be freed */
    int final_count = trix_atomic_load(&allocation_count);
    ASSERT(final_count == 0, "All allocations should be freed");
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Integration Test 7: Validation + Sanitization + Logging
 *═══════════════════════════════════════════════════════════════════════════*/

void test_validation_sanitization_logging() {
    TEST("Validation, sanitization, and logging");
    
    trix_log_init();
    trix_log_set_level(TRIX_LOG_INFO);
    
    /* Dangerous input */
    const char* dangerous_inputs[] = {
        "<script>alert('xss')</script>",
        "'; DROP TABLE users; --",
        "../../../etc/passwd",
        NULL
    };
    
    char safe_output[256];
    
    for (int i = 0; dangerous_inputs[i] != NULL; i++) {
        /* Sanitize HTML */
        if (trix_escape_html(dangerous_inputs[i], safe_output, sizeof(safe_output)) > 0) {
            log_info("Sanitized input: %s -> %s", dangerous_inputs[i], safe_output);
        }
        
        /* Validate path */
        if (!trix_validate_path(dangerous_inputs[i])) {
            log_warn("Rejected dangerous path: %s", dangerous_inputs[i]);
        }
    }
    
    trix_log_shutdown();
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Integration Test 8: Full Stack - All Systems Together
 *═══════════════════════════════════════════════════════════════════════════*/

void test_full_stack() {
    TEST("Full stack integration (all systems)");
    
    /* Initialize all systems */
    trix_log_init();
    trix_log_set_level(TRIX_LOG_INFO);
    
    const trix_version_info_t* version = trix_get_version();
    log_info("Starting full stack test with TriX %s", version->version_string);
    
    /* Error handling */
    trix_error_context_t ctx;
    trix_error_init(&ctx);
    
    /* Memory allocation with error checking */
    void* data = trix_malloc(4096, &ctx);
    ASSERT(data != NULL, "Allocation should succeed");
    ASSERT(ctx.code == TRIX_OK, "No error should occur");
    
    /* Thread-safe operations */
    trix_mutex_t mutex;
    trix_mutex_init(&mutex);
    
    trix_mutex_lock(&mutex);
    memset(data, 0, 4096);
    trix_mutex_unlock(&mutex);
    
    /* Input validation */
    const char* test_email = "user@example.com";
    if (trix_validate_email(test_email)) {
        log_info("Email validated: %s", test_email);
    }
    
    /* String sanitization */
    char safe_str[128];
    trix_sanitize_string("Hello<script>World</script>", safe_str, sizeof(safe_str),
                        TRIX_SANITIZE_ALPHANUMERIC);
    log_info("Sanitized string: %s", safe_str);
    
    /* Cleanup */
    trix_mutex_destroy(&mutex);
    free(data);
    
    log_info("Full stack test completed successfully");
    trix_log_shutdown();
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Main Test Runner
 *═══════════════════════════════════════════════════════════════════════════*/

int main(void) {
    printf("\n");
    printf("╭────────────────────────────────────────────────────────────╮\n");
    printf("│  TriX Integration Tests                                    │\n");
    printf("│  CRITICAL 8: Testing - Integration Scenarios              │\n");
    printf("╰────────────────────────────────────────────────────────────╯\n");
    printf("\n");
    
    /* Run integration tests */
    test_error_with_logging();
    test_memory_with_errors();
    test_validation_with_errors();
    test_threading_with_logging();
    test_version_with_logging();
    test_memory_thread_safety();
    test_validation_sanitization_logging();
    test_full_stack();
    
    /* Print summary */
    printf("\n");
    printf("════════════════════════════════════════════════════════════\n");
    if (tests_failed == 0) {
        printf("  ALL TESTS PASSED (%d/%d)\n", tests_passed, tests_passed + tests_failed);
    } else {
        printf("  TESTS FAILED: %d passed, %d failed\n", tests_passed, tests_failed);
    }
    printf("════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    return (tests_failed == 0) ? 0 : 1;
}
