/*
 * test_thread.c — Unit tests for thread safety
 *
 * Tests CRITICAL ITEM 5: Thread Safety
 */

#include "../include/trixc/thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test runner */
typedef struct {
    int passed;
    int failed;
} TestStats;

static TestStats stats = {0, 0};

#define TEST(name) \
    printf("Running: %-50s", name); \
    fflush(stdout);

#define PASS() \
    do { \
        printf(" PASS\n"); \
        stats.passed++; \
        return; \
    } while (0)

#define FAIL(msg) \
    do { \
        printf(" FAIL: %s\n", msg); \
        stats.failed++; \
        return; \
    } while (0)

#define ASSERT(cond, msg) \
    if (!(cond)) FAIL(msg)

/*═══════════════════════════════════════════════════════════════════════════
 * Mutex Tests
 *═══════════════════════════════════════════════════════════════════════════*/

void test_mutex_basic() {
    TEST("mutex initialization and locking");
    
    trix_mutex_t mutex;
    ASSERT(trix_mutex_init(&mutex) == TRIX_OK, "Mutex init should succeed");
    
    ASSERT(trix_mutex_lock(&mutex) == TRIX_OK, "Lock should succeed");
    ASSERT(trix_mutex_unlock(&mutex) == TRIX_OK, "Unlock should succeed");
    
    trix_mutex_destroy(&mutex);
    
    PASS();
}

void test_mutex_trylock() {
    TEST("mutex trylock");
    
    trix_mutex_t mutex;
    trix_mutex_init(&mutex);
    
    ASSERT(trix_mutex_trylock(&mutex) == TRIX_OK, "First trylock should succeed");
    ASSERT(trix_mutex_trylock(&mutex) == TRIX_ERROR_WOULD_BLOCK, "Second trylock should block");
    ASSERT(trix_mutex_unlock(&mutex) == TRIX_OK, "Unlock should succeed");
    
    trix_mutex_destroy(&mutex);
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Atomic Operations Tests
 *═══════════════════════════════════════════════════════════════════════════*/

void test_atomic_load_store() {
    TEST("atomic load and store");
    
    trix_atomic_t atomic = TRIX_ATOMIC_INIT(42);
    
    int32_t value = trix_atomic_load(&atomic);
    ASSERT(value == 42, "Initial value should be 42");
    
    trix_atomic_store(&atomic, 100);
    value = trix_atomic_load(&atomic);
    ASSERT(value == 100, "Stored value should be 100");
    
    PASS();
}

void test_atomic_add() {
    TEST("atomic add");
    
    trix_atomic_t counter = TRIX_ATOMIC_INIT(0);
    
    int32_t old = trix_atomic_add(&counter, 1);
    ASSERT(old == 0, "Old value should be 0");
    
    old = trix_atomic_add(&counter, 5);
    ASSERT(old == 1, "Old value should be 1");
    
    int32_t current = trix_atomic_load(&counter);
    ASSERT(current == 6, "Current value should be 6");
    
    PASS();
}

void test_atomic_sub() {
    TEST("atomic subtract");
    
    trix_atomic_t counter = TRIX_ATOMIC_INIT(10);
    
    int32_t old = trix_atomic_sub(&counter, 3);
    ASSERT(old == 10, "Old value should be 10");
    
    int32_t current = trix_atomic_load(&counter);
    ASSERT(current == 7, "Current value should be 7");
    
    PASS();
}

void test_atomic_cas() {
    TEST("atomic compare-and-swap");
    
    trix_atomic_t atomic = TRIX_ATOMIC_INIT(42);
    
    int32_t expected = 42;
    bool success = trix_atomic_cas(&atomic, &expected, 100);
    ASSERT(success, "CAS should succeed when expected matches");
    ASSERT(trix_atomic_load(&atomic) == 100, "Value should be updated to 100");
    
    expected = 42;
    success = trix_atomic_cas(&atomic, &expected, 200);
    ASSERT(!success, "CAS should fail when expected doesn't match");
    ASSERT(expected == 100, "Expected should be updated to current value");
    ASSERT(trix_atomic_load(&atomic) == 100, "Value should remain 100");
    
    PASS();
}

void test_atomic_exchange() {
    TEST("atomic exchange");
    
    trix_atomic_t atomic = TRIX_ATOMIC_INIT(42);
    
    int32_t old = trix_atomic_exchange(&atomic, 100);
    ASSERT(old == 42, "Old value should be 42");
    ASSERT(trix_atomic_load(&atomic) == 100, "New value should be 100");
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Spinlock Tests
 *═══════════════════════════════════════════════════════════════════════════*/

void test_spinlock() {
    TEST("spinlock operations");
    
    trix_spinlock_t lock;
    trix_spinlock_init(&lock);
    
    ASSERT(trix_spinlock_trylock(&lock), "First trylock should succeed");
    ASSERT(!trix_spinlock_trylock(&lock), "Second trylock should fail");
    
    trix_spinlock_unlock(&lock);
    
    trix_spinlock_lock(&lock);
    trix_spinlock_unlock(&lock);
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Reference Counting Tests
 *═══════════════════════════════════════════════════════════════════════════*/

void test_refcount() {
    TEST("reference counting");
    
    trix_refcount_t rc;
    trix_refcount_init(&rc, 1);
    
    ASSERT(trix_refcount_get(&rc) == 1, "Initial count should be 1");
    
    int32_t count = trix_refcount_inc(&rc);
    ASSERT(count == 2, "After inc, count should be 2");
    
    count = trix_refcount_inc(&rc);
    ASSERT(count == 3, "After inc, count should be 3");
    
    count = trix_refcount_dec(&rc);
    ASSERT(count == 2, "After dec, count should be 2");
    
    count = trix_refcount_dec(&rc);
    ASSERT(count == 1, "After dec, count should be 1");
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Thread Management Tests
 *═══════════════════════════════════════════════════════════════════════════*/

static void* simple_thread_func(void* arg) {
    int* value = (int*)arg;
    *value = 42;
    return NULL;
}

void test_thread_create_join() {
    TEST("thread creation and joining");
    
    int value = 0;
    trix_thread_t thread;
    
    trix_error_t err = trix_thread_create(&thread, simple_thread_func, &value);
    ASSERT(err == TRIX_OK, "Thread creation should succeed");
    
    err = trix_thread_join(thread, NULL);
    ASSERT(err == TRIX_OK, "Thread join should succeed");
    ASSERT(value == 42, "Thread should have set value to 42");
    
    PASS();
}

/* Thread that increments a counter */
static trix_atomic_t shared_counter;

static void* counter_thread_func(void* arg) {
    int iterations = *(int*)arg;
    for (int i = 0; i < iterations; i++) {
        trix_atomic_add(&shared_counter, 1);
    }
    return NULL;
}

void test_multithreaded_atomic() {
    TEST("multi-threaded atomic operations");
    
    const int NUM_THREADS = 4;
    const int ITERATIONS = 1000;
    
    trix_thread_t threads[NUM_THREADS];
    trix_atomic_store(&shared_counter, 0);
    
    /* Create threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        int* iter_ptr = malloc(sizeof(int));
        *iter_ptr = ITERATIONS;
        trix_error_t err = trix_thread_create(&threads[i], counter_thread_func, iter_ptr);
        ASSERT(err == TRIX_OK, "Thread creation should succeed");
    }
    
    /* Join threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        trix_thread_join(threads[i], NULL);
    }
    
    int32_t final_count = trix_atomic_load(&shared_counter);
    ASSERT(final_count == NUM_THREADS * ITERATIONS, 
           "Counter should be NUM_THREADS * ITERATIONS");
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Condition Variable Tests
 *═══════════════════════════════════════════════════════════════════════════*/

static trix_mutex_t cond_mutex;
static trix_cond_t cond_var;
static bool cond_ready = false;

static void* cond_waiter_thread(void* arg) {
    int* result = (int*)arg;
    
    trix_mutex_lock(&cond_mutex);
    while (!cond_ready) {
        trix_cond_wait(&cond_var, &cond_mutex);
    }
    *result = 42;
    trix_mutex_unlock(&cond_mutex);
    
    return NULL;
}

void test_condition_variable() {
    TEST("condition variable signaling");
    
    int result = 0;
    trix_thread_t thread;
    
    trix_mutex_init(&cond_mutex);
    trix_cond_init(&cond_var);
    cond_ready = false;
    
    /* Create waiting thread */
    trix_thread_create(&thread, cond_waiter_thread, &result);
    
    /* Give thread time to start waiting */
    trix_thread_sleep(100);
    
    /* Signal the condition */
    trix_mutex_lock(&cond_mutex);
    cond_ready = true;
    trix_cond_signal(&cond_var);
    trix_mutex_unlock(&cond_mutex);
    
    /* Wait for thread to finish */
    trix_thread_join(thread, NULL);
    
    ASSERT(result == 42, "Thread should have set result to 42");
    
    trix_mutex_destroy(&cond_mutex);
    trix_cond_destroy(&cond_var);
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Deadlock Prevention Tests
 *═══════════════════════════════════════════════════════════════════════════*/

void test_mutex_lock_multiple() {
    TEST("deadlock prevention with multiple mutexes");
    
    trix_mutex_t mutex1, mutex2, mutex3;
    trix_mutex_init(&mutex1);
    trix_mutex_init(&mutex2);
    trix_mutex_init(&mutex3);
    
    trix_mutex_t* mutexes[] = {&mutex3, &mutex1, &mutex2}; /* Unsorted */
    
    trix_error_t err = trix_mutex_lock_multiple(mutexes, 3);
    ASSERT(err == TRIX_OK, "Locking multiple mutexes should succeed");
    
    /* All mutexes should now be locked */
    ASSERT(trix_mutex_trylock(&mutex1) == TRIX_ERROR_WOULD_BLOCK, "mutex1 should be locked");
    ASSERT(trix_mutex_trylock(&mutex2) == TRIX_ERROR_WOULD_BLOCK, "mutex2 should be locked");
    ASSERT(trix_mutex_trylock(&mutex3) == TRIX_ERROR_WOULD_BLOCK, "mutex3 should be locked");
    
    trix_mutex_unlock_multiple(mutexes, 3);
    
    /* All mutexes should now be unlocked */
    ASSERT(trix_mutex_trylock(&mutex1) == TRIX_OK, "mutex1 should be unlocked");
    ASSERT(trix_mutex_trylock(&mutex2) == TRIX_OK, "mutex2 should be unlocked");
    ASSERT(trix_mutex_trylock(&mutex3) == TRIX_OK, "mutex3 should be unlocked");
    
    trix_mutex_unlock(&mutex1);
    trix_mutex_unlock(&mutex2);
    trix_mutex_unlock(&mutex3);
    
    trix_mutex_destroy(&mutex1);
    trix_mutex_destroy(&mutex2);
    trix_mutex_destroy(&mutex3);
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Thread Pool Tests
 *═══════════════════════════════════════════════════════════════════════════*/

static trix_atomic_t pool_work_count;

static void pool_work_func(void* arg) {
    (void)arg;
    trix_atomic_add(&pool_work_count, 1);
    trix_thread_sleep(10); /* Simulate work */
}

void test_thread_pool() {
    TEST("thread pool operations");
    
    trix_atomic_store(&pool_work_count, 0);
    
    trix_thread_pool_t* pool = trix_thread_pool_create(4);
    ASSERT(pool != NULL, "Thread pool creation should succeed");
    
    /* Submit 10 work items */
    for (int i = 0; i < 10; i++) {
        trix_error_t err = trix_thread_pool_submit(pool, pool_work_func, NULL);
        ASSERT(err == TRIX_OK, "Work submission should succeed");
    }
    
    /* Wait for all work to complete */
    trix_thread_pool_wait(pool);
    
    int32_t count = trix_atomic_load(&pool_work_count);
    ASSERT(count == 10, "All 10 work items should have executed");
    
    trix_thread_pool_destroy(pool);
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Main Test Runner
 *═══════════════════════════════════════════════════════════════════════════*/

int main(void) {
    printf("\n");
    printf("╭────────────────────────────────────────────────────────────╮\n");
    printf("│  TriX Thread Safety Unit Tests                            │\n");
    printf("│  CRITICAL 5: Thread Safety                                │\n");
    printf("╰────────────────────────────────────────────────────────────╯\n");
    printf("\n");
    
    /* Mutex tests */
    test_mutex_basic();
    test_mutex_trylock();
    
    /* Atomic operations */
    test_atomic_load_store();
    test_atomic_add();
    test_atomic_sub();
    test_atomic_cas();
    test_atomic_exchange();
    
    /* Spinlock */
    test_spinlock();
    
    /* Reference counting */
    test_refcount();
    
    /* Thread management */
    test_thread_create_join();
    test_multithreaded_atomic();
    test_condition_variable();
    
    /* Deadlock prevention */
    test_mutex_lock_multiple();
    
    /* Thread pool */
    test_thread_pool();
    
    /* Print summary */
    printf("\n");
    printf("════════════════════════════════════════════════════════════\n");
    if (stats.failed == 0) {
        printf("  ALL TESTS PASSED (%d/%d)\n", stats.passed, stats.passed + stats.failed);
    } else {
        printf("  TESTS FAILED: %d passed, %d failed\n", stats.passed, stats.failed);
    }
    printf("════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    return (stats.failed == 0) ? 0 : 1;
}
