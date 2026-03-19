/*
 * thread.h — Thread Safety and Synchronization Primitives
 *
 * Part of TriX Runtime (zor)
 * Production-grade thread safety for multi-threaded applications
 *
 * CRITICAL ITEM 5: Thread Safety
 *
 * Features:
 * - Cross-platform mutex wrappers (POSIX/Windows)
 * - Read-write locks
 * - Atomic operations (load, store, add, CAS)
 * - Thread-local storage
 * - Thread creation and management
 * - Condition variables
 * - Spinlocks for low-latency scenarios
 * - Deadlock prevention utilities
 * - Thread-safe reference counting
 *
 * Usage:
 *   // Mutex
 *   trix_mutex_t mutex;
 *   trix_mutex_init(&mutex);
 *   trix_mutex_lock(&mutex);
 *   // ... critical section ...
 *   trix_mutex_unlock(&mutex);
 *   trix_mutex_destroy(&mutex);
 *
 *   // Atomic operations
 *   trix_atomic_t counter = TRIX_ATOMIC_INIT(0);
 *   trix_atomic_add(&counter, 1);
 *   int value = trix_atomic_load(&counter);
 *
 *   // Thread-local storage
 *   TRIX_THREAD_LOCAL int my_thread_data = 0;
 */

#ifndef TRIXC_THREAD_H
#define TRIXC_THREAD_H

#include "errors.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*═══════════════════════════════════════════════════════════════════════════
 * Platform Detection and Threading API Selection
 *═══════════════════════════════════════════════════════════════════════════*/

/* Detect platform */
#if defined(_WIN32) || defined(_WIN64)
    #define TRIX_PLATFORM_WINDOWS
    #include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
    #define TRIX_PLATFORM_POSIX
    #include <pthread.h>
    #include <unistd.h>
    #include <errno.h>
#else
    #error "Unsupported platform for threading"
#endif

/* C11 atomics if available */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
    #define TRIX_HAS_C11_ATOMICS
    #include <stdatomic.h>
#endif

/*═══════════════════════════════════════════════════════════════════════════
 * Thread-Local Storage
 *═══════════════════════════════════════════════════════════════════════════*/

#if defined(TRIX_PLATFORM_WINDOWS)
    #define TRIX_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
    #define TRIX_THREAD_LOCAL __thread
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define TRIX_THREAD_LOCAL _Thread_local
#else
    #warning "Thread-local storage not supported on this platform"
    #define TRIX_THREAD_LOCAL
#endif

/*═══════════════════════════════════════════════════════════════════════════
 * Mutex (Mutual Exclusion Lock)
 *═══════════════════════════════════════════════════════════════════════════*/

#ifdef TRIX_PLATFORM_POSIX
    typedef pthread_mutex_t trix_mutex_t;
#else
    typedef CRITICAL_SECTION trix_mutex_t;
#endif

/**
 * Initialize a mutex
 * @return TRIX_OK on success, error code on failure
 */
trix_error_t trix_mutex_init(trix_mutex_t* mutex);

/**
 * Destroy a mutex
 */
void trix_mutex_destroy(trix_mutex_t* mutex);

/**
 * Lock a mutex (blocking)
 * @return TRIX_OK on success, error code on failure
 */
trix_error_t trix_mutex_lock(trix_mutex_t* mutex);

/**
 * Try to lock a mutex (non-blocking)
 * @return TRIX_OK if locked, TRIX_ERROR_WOULD_BLOCK if already locked
 */
trix_error_t trix_mutex_trylock(trix_mutex_t* mutex);

/**
 * Unlock a mutex
 * @return TRIX_OK on success, error code on failure
 */
trix_error_t trix_mutex_unlock(trix_mutex_t* mutex);

/*═══════════════════════════════════════════════════════════════════════════
 * Read-Write Lock
 *═══════════════════════════════════════════════════════════════════════════*/

#ifdef TRIX_PLATFORM_POSIX
    typedef pthread_rwlock_t trix_rwlock_t;
#else
    typedef SRWLOCK trix_rwlock_t;
#endif

/**
 * Initialize a read-write lock
 */
trix_error_t trix_rwlock_init(trix_rwlock_t* rwlock);

/**
 * Destroy a read-write lock
 */
void trix_rwlock_destroy(trix_rwlock_t* rwlock);

/**
 * Acquire read lock (multiple readers allowed)
 */
trix_error_t trix_rwlock_rdlock(trix_rwlock_t* rwlock);

/**
 * Acquire write lock (exclusive)
 */
trix_error_t trix_rwlock_wrlock(trix_rwlock_t* rwlock);

/**
 * Release read or write lock
 */
trix_error_t trix_rwlock_unlock(trix_rwlock_t* rwlock);

/*═══════════════════════════════════════════════════════════════════════════
 * Condition Variable
 *═══════════════════════════════════════════════════════════════════════════*/

#ifdef TRIX_PLATFORM_POSIX
    typedef pthread_cond_t trix_cond_t;
#else
    typedef CONDITION_VARIABLE trix_cond_t;
#endif

/**
 * Initialize condition variable
 */
trix_error_t trix_cond_init(trix_cond_t* cond);

/**
 * Destroy condition variable
 */
void trix_cond_destroy(trix_cond_t* cond);

/**
 * Wait on condition variable (releases mutex while waiting)
 */
trix_error_t trix_cond_wait(trix_cond_t* cond, trix_mutex_t* mutex);

/**
 * Wait with timeout (milliseconds)
 */
trix_error_t trix_cond_timedwait(trix_cond_t* cond, trix_mutex_t* mutex, 
                                 uint64_t timeout_ms);

/**
 * Signal one waiting thread
 */
trix_error_t trix_cond_signal(trix_cond_t* cond);

/**
 * Signal all waiting threads
 */
trix_error_t trix_cond_broadcast(trix_cond_t* cond);

/*═══════════════════════════════════════════════════════════════════════════
 * Atomic Operations
 *═══════════════════════════════════════════════════════════════════════════*/

#ifdef TRIX_HAS_C11_ATOMICS
    typedef _Atomic int32_t trix_atomic_t;
    typedef _Atomic int64_t trix_atomic64_t;
    typedef _Atomic uintptr_t trix_atomic_ptr_t;
#else
    /* Fallback to volatile (not truly atomic, but better than nothing) */
    typedef volatile int32_t trix_atomic_t;
    typedef volatile int64_t trix_atomic64_t;
    typedef volatile uintptr_t trix_atomic_ptr_t;
#endif

/**
 * Initialize atomic variable
 */
#define TRIX_ATOMIC_INIT(val) (val)

/**
 * Atomic load (acquire semantics)
 */
int32_t trix_atomic_load(const trix_atomic_t* ptr);
int64_t trix_atomic64_load(const trix_atomic64_t* ptr);
uintptr_t trix_atomic_ptr_load(const trix_atomic_ptr_t* ptr);

/**
 * Atomic store (release semantics)
 */
void trix_atomic_store(trix_atomic_t* ptr, int32_t value);
void trix_atomic64_store(trix_atomic64_t* ptr, int64_t value);
void trix_atomic_ptr_store(trix_atomic_ptr_t* ptr, uintptr_t value);

/**
 * Atomic add (returns old value)
 */
int32_t trix_atomic_add(trix_atomic_t* ptr, int32_t value);
int64_t trix_atomic64_add(trix_atomic64_t* ptr, int64_t value);

/**
 * Atomic subtract (returns old value)
 */
int32_t trix_atomic_sub(trix_atomic_t* ptr, int32_t value);
int64_t trix_atomic64_sub(trix_atomic64_t* ptr, int64_t value);

/**
 * Atomic compare-and-swap (CAS)
 * Returns true if exchange succeeded
 */
bool trix_atomic_cas(trix_atomic_t* ptr, int32_t* expected, int32_t desired);
bool trix_atomic64_cas(trix_atomic64_t* ptr, int64_t* expected, int64_t desired);
bool trix_atomic_ptr_cas(trix_atomic_ptr_t* ptr, uintptr_t* expected, uintptr_t desired);

/**
 * Atomic exchange (swap)
 */
int32_t trix_atomic_exchange(trix_atomic_t* ptr, int32_t value);
int64_t trix_atomic64_exchange(trix_atomic64_t* ptr, int64_t value);
uintptr_t trix_atomic_ptr_exchange(trix_atomic_ptr_t* ptr, uintptr_t value);

/**
 * Memory barriers
 */
void trix_atomic_thread_fence_acquire(void);
void trix_atomic_thread_fence_release(void);
void trix_atomic_thread_fence_seq_cst(void);

/*═══════════════════════════════════════════════════════════════════════════
 * Spinlock (for very short critical sections)
 *═══════════════════════════════════════════════════════════════════════════*/

typedef struct {
    trix_atomic_t flag;
} trix_spinlock_t;

/**
 * Initialize spinlock
 */
void trix_spinlock_init(trix_spinlock_t* lock);

/**
 * Acquire spinlock
 */
void trix_spinlock_lock(trix_spinlock_t* lock);

/**
 * Try to acquire spinlock (non-blocking)
 */
bool trix_spinlock_trylock(trix_spinlock_t* lock);

/**
 * Release spinlock
 */
void trix_spinlock_unlock(trix_spinlock_t* lock);

/*═══════════════════════════════════════════════════════════════════════════
 * Thread Management
 *═══════════════════════════════════════════════════════════════════════════*/

#ifdef TRIX_PLATFORM_POSIX
    typedef pthread_t trix_thread_t;
#else
    typedef HANDLE trix_thread_t;
#endif

typedef void* (*trix_thread_func_t)(void* arg);

/**
 * Create and start a new thread
 */
trix_error_t trix_thread_create(trix_thread_t* thread, trix_thread_func_t func, 
                                void* arg);

/**
 * Wait for thread to finish
 */
trix_error_t trix_thread_join(trix_thread_t thread, void** return_value);

/**
 * Detach thread (runs independently)
 */
trix_error_t trix_thread_detach(trix_thread_t thread);

/**
 * Get current thread ID
 */
uint64_t trix_thread_id(void);

/**
 * Sleep for specified milliseconds
 */
void trix_thread_sleep(uint64_t milliseconds);

/**
 * Yield CPU to other threads
 */
void trix_thread_yield(void);

/*═══════════════════════════════════════════════════════════════════════════
 * Thread-Safe Reference Counting
 *═══════════════════════════════════════════════════════════════════════════*/

typedef struct {
    trix_atomic_t count;
} trix_refcount_t;

/**
 * Initialize reference count
 */
void trix_refcount_init(trix_refcount_t* rc, int32_t initial);

/**
 * Increment reference count (returns new count)
 */
int32_t trix_refcount_inc(trix_refcount_t* rc);

/**
 * Decrement reference count (returns new count)
 */
int32_t trix_refcount_dec(trix_refcount_t* rc);

/**
 * Get current reference count
 */
int32_t trix_refcount_get(const trix_refcount_t* rc);

/*═══════════════════════════════════════════════════════════════════════════
 * Scoped Lock (RAII-style automatic unlock)
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Scoped mutex lock (automatically unlocks when scope exits)
 * 
 * Usage:
 *   TRIX_SCOPED_LOCK(&mutex) {
 *       // Critical section
 *   }  // Automatically unlocked here
 */
typedef struct {
    trix_mutex_t* mutex;
    bool locked;
} trix_scoped_lock_t;

trix_scoped_lock_t trix_scoped_lock_create(trix_mutex_t* mutex);
void trix_scoped_lock_destroy(trix_scoped_lock_t* lock);

#define TRIX_SCOPED_LOCK(mutex_ptr) \
    for (trix_scoped_lock_t _lock = trix_scoped_lock_create(mutex_ptr); \
         _lock.locked; \
         trix_scoped_lock_destroy(&_lock))

/*═══════════════════════════════════════════════════════════════════════════
 * Deadlock Prevention Utilities
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Lock multiple mutexes in consistent order (prevents deadlock)
 * Mutexes are sorted by address before locking
 */
trix_error_t trix_mutex_lock_multiple(trix_mutex_t** mutexes, size_t count);

/**
 * Unlock multiple mutexes
 */
void trix_mutex_unlock_multiple(trix_mutex_t** mutexes, size_t count);

/*═══════════════════════════════════════════════════════════════════════════
 * Thread Pool (for work distribution)
 *═══════════════════════════════════════════════════════════════════════════*/

typedef struct trix_thread_pool trix_thread_pool_t;
typedef void (*trix_work_func_t)(void* arg);

/**
 * Create thread pool with specified number of worker threads
 */
trix_thread_pool_t* trix_thread_pool_create(size_t num_threads);

/**
 * Submit work to thread pool
 */
trix_error_t trix_thread_pool_submit(trix_thread_pool_t* pool, 
                                     trix_work_func_t func, void* arg);

/**
 * Wait for all pending work to complete
 */
void trix_thread_pool_wait(trix_thread_pool_t* pool);

/**
 * Destroy thread pool (waits for all work to finish)
 */
void trix_thread_pool_destroy(trix_thread_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_THREAD_H */
