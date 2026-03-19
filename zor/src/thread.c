/*
 * thread.c — Thread Safety and Synchronization Implementation
 *
 * Part of TriX Runtime (zor)
 * Production-grade thread safety for multi-threaded applications
 */

#include "../include/trixc/thread.h"
#include "../include/trixc/logging.h"
#include <stdlib.h>
#include <string.h>

#ifdef TRIX_PLATFORM_POSIX
    #include <sys/time.h>
    #include <time.h>
#endif

/*═══════════════════════════════════════════════════════════════════════════
 * Mutex Implementation
 *═══════════════════════════════════════════════════════════════════════════*/

trix_error_t trix_mutex_init(trix_mutex_t* mutex) {
    if (mutex == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_mutex_init(mutex, NULL);
    if (result != 0) {
        log_error("Failed to initialize mutex: %d", result);
        return TRIX_ERROR_THREAD_INIT;
    }
#else
    InitializeCriticalSection(mutex);
#endif
    
    return TRIX_OK;
}

void trix_mutex_destroy(trix_mutex_t* mutex) {
    if (mutex == NULL) {
        return;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    pthread_mutex_destroy(mutex);
#else
    DeleteCriticalSection(mutex);
#endif
}

trix_error_t trix_mutex_lock(trix_mutex_t* mutex) {
    if (mutex == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_mutex_lock(mutex);
    if (result != 0) {
        log_error("Failed to lock mutex: %d", result);
        return TRIX_ERROR_THREAD_LOCK;
    }
#else
    EnterCriticalSection(mutex);
#endif
    
    return TRIX_OK;
}

trix_error_t trix_mutex_trylock(trix_mutex_t* mutex) {
    if (mutex == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_mutex_trylock(mutex);
    if (result == EBUSY) {
        return TRIX_ERROR_WOULD_BLOCK;
    } else if (result != 0) {
        log_error("Failed to trylock mutex: %d", result);
        return TRIX_ERROR_THREAD_LOCK;
    }
#else
    if (!TryEnterCriticalSection(mutex)) {
        return TRIX_ERROR_WOULD_BLOCK;
    }
#endif
    
    return TRIX_OK;
}

trix_error_t trix_mutex_unlock(trix_mutex_t* mutex) {
    if (mutex == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_mutex_unlock(mutex);
    if (result != 0) {
        log_error("Failed to unlock mutex: %d", result);
        return TRIX_ERROR_THREAD_UNLOCK;
    }
#else
    LeaveCriticalSection(mutex);
#endif
    
    return TRIX_OK;
}

/*═══════════════════════════════════════════════════════════════════════════
 * Read-Write Lock Implementation
 *═══════════════════════════════════════════════════════════════════════════*/

trix_error_t trix_rwlock_init(trix_rwlock_t* rwlock) {
    if (rwlock == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_rwlock_init(rwlock, NULL);
    if (result != 0) {
        log_error("Failed to initialize rwlock: %d", result);
        return TRIX_ERROR_THREAD_INIT;
    }
#else
    InitializeSRWLock(rwlock);
#endif
    
    return TRIX_OK;
}

void trix_rwlock_destroy(trix_rwlock_t* rwlock) {
    if (rwlock == NULL) {
        return;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    pthread_rwlock_destroy(rwlock);
#else
    /* SRW locks don't need cleanup on Windows */
#endif
}

trix_error_t trix_rwlock_rdlock(trix_rwlock_t* rwlock) {
    if (rwlock == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_rwlock_rdlock(rwlock);
    if (result != 0) {
        log_error("Failed to rdlock: %d", result);
        return TRIX_ERROR_THREAD_LOCK;
    }
#else
    AcquireSRWLockShared(rwlock);
#endif
    
    return TRIX_OK;
}

trix_error_t trix_rwlock_wrlock(trix_rwlock_t* rwlock) {
    if (rwlock == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_rwlock_wrlock(rwlock);
    if (result != 0) {
        log_error("Failed to wrlock: %d", result);
        return TRIX_ERROR_THREAD_LOCK;
    }
#else
    AcquireSRWLockExclusive(rwlock);
#endif
    
    return TRIX_OK;
}

trix_error_t trix_rwlock_unlock(trix_rwlock_t* rwlock) {
    if (rwlock == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_rwlock_unlock(rwlock);
    if (result != 0) {
        log_error("Failed to unlock rwlock: %d", result);
        return TRIX_ERROR_THREAD_UNLOCK;
    }
#else
    /* Windows requires caller to know if it was shared or exclusive */
    /* This is a limitation - for now we assume exclusive */
    ReleaseSRWLockExclusive(rwlock);
#endif
    
    return TRIX_OK;
}

/*═══════════════════════════════════════════════════════════════════════════
 * Condition Variable Implementation
 *═══════════════════════════════════════════════════════════════════════════*/

trix_error_t trix_cond_init(trix_cond_t* cond) {
    if (cond == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_cond_init(cond, NULL);
    if (result != 0) {
        log_error("Failed to initialize condition variable: %d", result);
        return TRIX_ERROR_THREAD_INIT;
    }
#else
    InitializeConditionVariable(cond);
#endif
    
    return TRIX_OK;
}

void trix_cond_destroy(trix_cond_t* cond) {
    if (cond == NULL) {
        return;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    pthread_cond_destroy(cond);
#else
    /* Condition variables don't need cleanup on Windows */
#endif
}

trix_error_t trix_cond_wait(trix_cond_t* cond, trix_mutex_t* mutex) {
    if (cond == NULL || mutex == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_cond_wait(cond, mutex);
    if (result != 0) {
        log_error("Failed to wait on condition variable: %d", result);
        return TRIX_ERROR_THREAD_COND;
    }
#else
    if (!SleepConditionVariableCS(cond, mutex, INFINITE)) {
        log_error("Failed to wait on condition variable: %lu", GetLastError());
        return TRIX_ERROR_THREAD_COND;
    }
#endif
    
    return TRIX_OK;
}

trix_error_t trix_cond_timedwait(trix_cond_t* cond, trix_mutex_t* mutex, 
                                 uint64_t timeout_ms) {
    if (cond == NULL || mutex == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    struct timespec ts;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    ts.tv_sec = tv.tv_sec + (timeout_ms / 1000);
    ts.tv_nsec = (tv.tv_usec * 1000) + ((timeout_ms % 1000) * 1000000);
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    
    int result = pthread_cond_timedwait(cond, mutex, &ts);
    if (result == ETIMEDOUT) {
        return TRIX_ERROR_TIMEOUT;
    } else if (result != 0) {
        log_error("Failed to timedwait on condition variable: %d", result);
        return TRIX_ERROR_THREAD_COND;
    }
#else
    if (!SleepConditionVariableCS(cond, mutex, (DWORD)timeout_ms)) {
        DWORD error = GetLastError();
        if (error == ERROR_TIMEOUT) {
            return TRIX_ERROR_TIMEOUT;
        }
        log_error("Failed to timedwait on condition variable: %lu", error);
        return TRIX_ERROR_THREAD_COND;
    }
#endif
    
    return TRIX_OK;
}

trix_error_t trix_cond_signal(trix_cond_t* cond) {
    if (cond == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_cond_signal(cond);
    if (result != 0) {
        log_error("Failed to signal condition variable: %d", result);
        return TRIX_ERROR_THREAD_COND;
    }
#else
    WakeConditionVariable(cond);
#endif
    
    return TRIX_OK;
}

trix_error_t trix_cond_broadcast(trix_cond_t* cond) {
    if (cond == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_cond_broadcast(cond);
    if (result != 0) {
        log_error("Failed to broadcast condition variable: %d", result);
        return TRIX_ERROR_THREAD_COND;
    }
#else
    WakeAllConditionVariable(cond);
#endif
    
    return TRIX_OK;
}

/*═══════════════════════════════════════════════════════════════════════════
 * Atomic Operations Implementation
 *═══════════════════════════════════════════════════════════════════════════*/

int32_t trix_atomic_load(const trix_atomic_t* ptr) {
#ifdef TRIX_HAS_C11_ATOMICS
    return atomic_load(ptr);
#else
    return *ptr;
#endif
}

int64_t trix_atomic64_load(const trix_atomic64_t* ptr) {
#ifdef TRIX_HAS_C11_ATOMICS
    return atomic_load(ptr);
#else
    return *ptr;
#endif
}

uintptr_t trix_atomic_ptr_load(const trix_atomic_ptr_t* ptr) {
#ifdef TRIX_HAS_C11_ATOMICS
    return atomic_load(ptr);
#else
    return *ptr;
#endif
}

void trix_atomic_store(trix_atomic_t* ptr, int32_t value) {
#ifdef TRIX_HAS_C11_ATOMICS
    atomic_store(ptr, value);
#else
    *ptr = value;
#endif
}

void trix_atomic64_store(trix_atomic64_t* ptr, int64_t value) {
#ifdef TRIX_HAS_C11_ATOMICS
    atomic_store(ptr, value);
#else
    *ptr = value;
#endif
}

void trix_atomic_ptr_store(trix_atomic_ptr_t* ptr, uintptr_t value) {
#ifdef TRIX_HAS_C11_ATOMICS
    atomic_store(ptr, value);
#else
    *ptr = value;
#endif
}

int32_t trix_atomic_add(trix_atomic_t* ptr, int32_t value) {
#ifdef TRIX_HAS_C11_ATOMICS
    return atomic_fetch_add(ptr, value);
#elif defined(__GNUC__) || defined(__clang__)
    return __sync_fetch_and_add(ptr, value);
#else
    /* Not thread-safe fallback */
    int32_t old = *ptr;
    *ptr += value;
    return old;
#endif
}

int64_t trix_atomic64_add(trix_atomic64_t* ptr, int64_t value) {
#ifdef TRIX_HAS_C11_ATOMICS
    return atomic_fetch_add(ptr, value);
#elif defined(__GNUC__) || defined(__clang__)
    return __sync_fetch_and_add(ptr, value);
#else
    /* Not thread-safe fallback */
    int64_t old = *ptr;
    *ptr += value;
    return old;
#endif
}

int32_t trix_atomic_sub(trix_atomic_t* ptr, int32_t value) {
#ifdef TRIX_HAS_C11_ATOMICS
    return atomic_fetch_sub(ptr, value);
#elif defined(__GNUC__) || defined(__clang__)
    return __sync_fetch_and_sub(ptr, value);
#else
    /* Not thread-safe fallback */
    int32_t old = *ptr;
    *ptr -= value;
    return old;
#endif
}

int64_t trix_atomic64_sub(trix_atomic64_t* ptr, int64_t value) {
#ifdef TRIX_HAS_C11_ATOMICS
    return atomic_fetch_sub(ptr, value);
#elif defined(__GNUC__) || defined(__clang__)
    return __sync_fetch_and_sub(ptr, value);
#else
    /* Not thread-safe fallback */
    int64_t old = *ptr;
    *ptr -= value;
    return old;
#endif
}

bool trix_atomic_cas(trix_atomic_t* ptr, int32_t* expected, int32_t desired) {
#ifdef TRIX_HAS_C11_ATOMICS
    return atomic_compare_exchange_strong(ptr, expected, desired);
#elif defined(__GNUC__) || defined(__clang__)
    return __sync_bool_compare_and_swap(ptr, *expected, desired);
#else
    /* Not thread-safe fallback */
    if (*ptr == *expected) {
        *ptr = desired;
        return true;
    }
    *expected = *ptr;
    return false;
#endif
}

bool trix_atomic64_cas(trix_atomic64_t* ptr, int64_t* expected, int64_t desired) {
#ifdef TRIX_HAS_C11_ATOMICS
    return atomic_compare_exchange_strong(ptr, expected, desired);
#elif defined(__GNUC__) || defined(__clang__)
    return __sync_bool_compare_and_swap(ptr, *expected, desired);
#else
    /* Not thread-safe fallback */
    if (*ptr == *expected) {
        *ptr = desired;
        return true;
    }
    *expected = *ptr;
    return false;
#endif
}

bool trix_atomic_ptr_cas(trix_atomic_ptr_t* ptr, uintptr_t* expected, uintptr_t desired) {
#ifdef TRIX_HAS_C11_ATOMICS
    return atomic_compare_exchange_strong(ptr, expected, desired);
#elif defined(__GNUC__) || defined(__clang__)
    return __sync_bool_compare_and_swap(ptr, *expected, desired);
#else
    /* Not thread-safe fallback */
    if (*ptr == *expected) {
        *ptr = desired;
        return true;
    }
    *expected = *ptr;
    return false;
#endif
}

int32_t trix_atomic_exchange(trix_atomic_t* ptr, int32_t value) {
#ifdef TRIX_HAS_C11_ATOMICS
    return atomic_exchange(ptr, value);
#elif defined(__GNUC__) || defined(__clang__)
    return __sync_lock_test_and_set(ptr, value);
#else
    /* Not thread-safe fallback */
    int32_t old = *ptr;
    *ptr = value;
    return old;
#endif
}

int64_t trix_atomic64_exchange(trix_atomic64_t* ptr, int64_t value) {
#ifdef TRIX_HAS_C11_ATOMICS
    return atomic_exchange(ptr, value);
#elif defined(__GNUC__) || defined(__clang__)
    return __sync_lock_test_and_set(ptr, value);
#else
    /* Not thread-safe fallback */
    int64_t old = *ptr;
    *ptr = value;
    return old;
#endif
}

uintptr_t trix_atomic_ptr_exchange(trix_atomic_ptr_t* ptr, uintptr_t value) {
#ifdef TRIX_HAS_C11_ATOMICS
    return atomic_exchange(ptr, value);
#elif defined(__GNUC__) || defined(__clang__)
    return __sync_lock_test_and_set(ptr, value);
#else
    /* Not thread-safe fallback */
    uintptr_t old = *ptr;
    *ptr = value;
    return old;
#endif
}

void trix_atomic_thread_fence_acquire(void) {
#ifdef TRIX_HAS_C11_ATOMICS
    atomic_thread_fence(memory_order_acquire);
#elif defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
#endif
}

void trix_atomic_thread_fence_release(void) {
#ifdef TRIX_HAS_C11_ATOMICS
    atomic_thread_fence(memory_order_release);
#elif defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
#endif
}

void trix_atomic_thread_fence_seq_cst(void) {
#ifdef TRIX_HAS_C11_ATOMICS
    atomic_thread_fence(memory_order_seq_cst);
#elif defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
#endif
}

/*═══════════════════════════════════════════════════════════════════════════
 * Spinlock Implementation
 *═══════════════════════════════════════════════════════════════════════════*/

void trix_spinlock_init(trix_spinlock_t* lock) {
    if (lock) {
        trix_atomic_store(&lock->flag, 0);
    }
}

void trix_spinlock_lock(trix_spinlock_t* lock) {
    if (lock == NULL) {
        return;
    }
    
    while (true) {
        int32_t expected = 0;
        if (trix_atomic_cas(&lock->flag, &expected, 1)) {
            break;
        }
        /* Spin */
        trix_thread_yield();
    }
}

bool trix_spinlock_trylock(trix_spinlock_t* lock) {
    if (lock == NULL) {
        return false;
    }
    
    int32_t expected = 0;
    return trix_atomic_cas(&lock->flag, &expected, 1);
}

void trix_spinlock_unlock(trix_spinlock_t* lock) {
    if (lock) {
        trix_atomic_store(&lock->flag, 0);
    }
}

/*═══════════════════════════════════════════════════════════════════════════
 * Thread Management Implementation
 *═══════════════════════════════════════════════════════════════════════════*/

trix_error_t trix_thread_create(trix_thread_t* thread, trix_thread_func_t func, 
                                void* arg) {
    if (thread == NULL || func == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_create(thread, NULL, func, arg);
    if (result != 0) {
        log_error("Failed to create thread: %d", result);
        return TRIX_ERROR_THREAD_CREATE;
    }
#else
    *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    if (*thread == NULL) {
        log_error("Failed to create thread: %lu", GetLastError());
        return TRIX_ERROR_THREAD_CREATE;
    }
#endif
    
    return TRIX_OK;
}

trix_error_t trix_thread_join(trix_thread_t thread, void** return_value) {
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_join(thread, return_value);
    if (result != 0) {
        log_error("Failed to join thread: %d", result);
        return TRIX_ERROR_THREAD_JOIN;
    }
#else
    WaitForSingleObject(thread, INFINITE);
    if (return_value) {
        DWORD exit_code;
        GetExitCodeThread(thread, &exit_code);
        *return_value = (void*)(uintptr_t)exit_code;
    }
    CloseHandle(thread);
#endif
    
    return TRIX_OK;
}

trix_error_t trix_thread_detach(trix_thread_t thread) {
#ifdef TRIX_PLATFORM_POSIX
    int result = pthread_detach(thread);
    if (result != 0) {
        log_error("Failed to detach thread: %d", result);
        return TRIX_ERROR_THREAD_DETACH;
    }
#else
    CloseHandle(thread);
#endif
    
    return TRIX_OK;
}

uint64_t trix_thread_id(void) {
#ifdef TRIX_PLATFORM_POSIX
    return (uint64_t)pthread_self();
#else
    return (uint64_t)GetCurrentThreadId();
#endif
}

void trix_thread_sleep(uint64_t milliseconds) {
#ifdef TRIX_PLATFORM_POSIX
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#else
    Sleep((DWORD)milliseconds);
#endif
}

void trix_thread_yield(void) {
#ifdef TRIX_PLATFORM_POSIX
    #ifdef __APPLE__
        pthread_yield_np();
    #else
        sched_yield();
    #endif
#else
    SwitchToThread();
#endif
}

/*═══════════════════════════════════════════════════════════════════════════
 * Reference Counting Implementation
 *═══════════════════════════════════════════════════════════════════════════*/

void trix_refcount_init(trix_refcount_t* rc, int32_t initial) {
    if (rc) {
        trix_atomic_store(&rc->count, initial);
    }
}

int32_t trix_refcount_inc(trix_refcount_t* rc) {
    if (rc == NULL) {
        return -1;
    }
    return trix_atomic_add(&rc->count, 1) + 1;
}

int32_t trix_refcount_dec(trix_refcount_t* rc) {
    if (rc == NULL) {
        return -1;
    }
    return trix_atomic_sub(&rc->count, 1) - 1;
}

int32_t trix_refcount_get(const trix_refcount_t* rc) {
    if (rc == NULL) {
        return -1;
    }
    return trix_atomic_load(&rc->count);
}

/*═══════════════════════════════════════════════════════════════════════════
 * Scoped Lock Implementation
 *═══════════════════════════════════════════════════════════════════════════*/

trix_scoped_lock_t trix_scoped_lock_create(trix_mutex_t* mutex) {
    trix_scoped_lock_t lock = {.mutex = mutex, .locked = false};
    
    if (mutex && trix_mutex_lock(mutex) == TRIX_OK) {
        lock.locked = true;
    }
    
    return lock;
}

void trix_scoped_lock_destroy(trix_scoped_lock_t* lock) {
    if (lock && lock->locked && lock->mutex) {
        trix_mutex_unlock(lock->mutex);
        lock->locked = false;
    }
}

/*═══════════════════════════════════════════════════════════════════════════
 * Deadlock Prevention Utilities
 *═══════════════════════════════════════════════════════════════════════════*/

/* Comparison function for sorting mutexes by address */
static int compare_mutex_ptrs(const void* a, const void* b) {
    uintptr_t ptr_a = (uintptr_t)(*(trix_mutex_t**)a);
    uintptr_t ptr_b = (uintptr_t)(*(trix_mutex_t**)b);
    
    if (ptr_a < ptr_b) return -1;
    if (ptr_a > ptr_b) return 1;
    return 0;
}

trix_error_t trix_mutex_lock_multiple(trix_mutex_t** mutexes, size_t count) {
    if (mutexes == NULL || count == 0) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
    /* Sort mutexes by address to prevent deadlock */
    trix_mutex_t** sorted = malloc(count * sizeof(trix_mutex_t*));
    if (sorted == NULL) {
        return TRIX_ERROR_OUT_OF_MEMORY;
    }
    
    memcpy(sorted, mutexes, count * sizeof(trix_mutex_t*));
    qsort(sorted, count, sizeof(trix_mutex_t*), compare_mutex_ptrs);
    
    /* Lock in sorted order */
    for (size_t i = 0; i < count; i++) {
        trix_error_t err = trix_mutex_lock(sorted[i]);
        if (err != TRIX_OK) {
            /* Unlock all previously locked mutexes */
            for (size_t j = 0; j < i; j++) {
                trix_mutex_unlock(sorted[j]);
            }
            free(sorted);
            return err;
        }
    }
    
    free(sorted);
    return TRIX_OK;
}

void trix_mutex_unlock_multiple(trix_mutex_t** mutexes, size_t count) {
    if (mutexes == NULL || count == 0) {
        return;
    }
    
    /* Unlock in any order (order doesn't matter for unlock) */
    for (size_t i = 0; i < count; i++) {
        if (mutexes[i]) {
            trix_mutex_unlock(mutexes[i]);
        }
    }
}

/*═══════════════════════════════════════════════════════════════════════════
 * Thread Pool Implementation
 *═══════════════════════════════════════════════════════════════════════════*/

#define TRIX_THREAD_POOL_QUEUE_SIZE 1024

typedef struct {
    trix_work_func_t func;
    void* arg;
} trix_work_item_t;

struct trix_thread_pool {
    trix_thread_t* threads;
    size_t num_threads;
    
    trix_work_item_t queue[TRIX_THREAD_POOL_QUEUE_SIZE];
    size_t queue_head;
    size_t queue_tail;
    size_t queue_count;
    
    trix_mutex_t mutex;
    trix_cond_t cond;
    bool shutdown;
};

static void* thread_pool_worker(void* arg) {
    trix_thread_pool_t* pool = (trix_thread_pool_t*)arg;
    
    while (true) {
        trix_mutex_lock(&pool->mutex);
        
        /* Wait for work or shutdown signal */
        while (pool->queue_count == 0 && !pool->shutdown) {
            trix_cond_wait(&pool->cond, &pool->mutex);
        }
        
        if (pool->shutdown && pool->queue_count == 0) {
            trix_mutex_unlock(&pool->mutex);
            break;
        }
        
        /* Get work item */
        trix_work_item_t item = pool->queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % TRIX_THREAD_POOL_QUEUE_SIZE;
        pool->queue_count--;
        
        trix_mutex_unlock(&pool->mutex);
        
        /* Execute work */
        if (item.func) {
            item.func(item.arg);
        }
    }
    
    return NULL;
}

trix_thread_pool_t* trix_thread_pool_create(size_t num_threads) {
    if (num_threads == 0) {
        return NULL;
    }
    
    trix_thread_pool_t* pool = calloc(1, sizeof(trix_thread_pool_t));
    if (pool == NULL) {
        return NULL;
    }
    
    pool->num_threads = num_threads;
    pool->threads = calloc(num_threads, sizeof(trix_thread_t));
    if (pool->threads == NULL) {
        free(pool);
        return NULL;
    }
    
    trix_mutex_init(&pool->mutex);
    trix_cond_init(&pool->cond);
    pool->shutdown = false;
    
    /* Create worker threads */
    for (size_t i = 0; i < num_threads; i++) {
        if (trix_thread_create(&pool->threads[i], thread_pool_worker, pool) != TRIX_OK) {
            pool->shutdown = true;
            trix_cond_broadcast(&pool->cond);
            
            /* Join threads that were created */
            for (size_t j = 0; j < i; j++) {
                trix_thread_join(pool->threads[j], NULL);
            }
            
            free(pool->threads);
            trix_mutex_destroy(&pool->mutex);
            trix_cond_destroy(&pool->cond);
            free(pool);
            return NULL;
        }
    }
    
    return pool;
}

trix_error_t trix_thread_pool_submit(trix_thread_pool_t* pool, 
                                     trix_work_func_t func, void* arg) {
    if (pool == NULL || func == NULL) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
    trix_mutex_lock(&pool->mutex);
    
    if (pool->shutdown) {
        trix_mutex_unlock(&pool->mutex);
        return TRIX_ERROR_THREAD_SHUTDOWN;
    }
    
    if (pool->queue_count >= TRIX_THREAD_POOL_QUEUE_SIZE) {
        trix_mutex_unlock(&pool->mutex);
        return TRIX_ERROR_QUEUE_FULL;
    }
    
    /* Add work to queue */
    pool->queue[pool->queue_tail].func = func;
    pool->queue[pool->queue_tail].arg = arg;
    pool->queue_tail = (pool->queue_tail + 1) % TRIX_THREAD_POOL_QUEUE_SIZE;
    pool->queue_count++;
    
    trix_cond_signal(&pool->cond);
    trix_mutex_unlock(&pool->mutex);
    
    return TRIX_OK;
}

void trix_thread_pool_wait(trix_thread_pool_t* pool) {
    if (pool == NULL) {
        return;
    }
    
    /* Wait for queue to be empty */
    while (true) {
        trix_mutex_lock(&pool->mutex);
        bool empty = (pool->queue_count == 0);
        trix_mutex_unlock(&pool->mutex);
        
        if (empty) {
            break;
        }
        
        trix_thread_sleep(10);
    }
}

void trix_thread_pool_destroy(trix_thread_pool_t* pool) {
    if (pool == NULL) {
        return;
    }
    
    /* Signal shutdown */
    trix_mutex_lock(&pool->mutex);
    pool->shutdown = true;
    trix_cond_broadcast(&pool->cond);
    trix_mutex_unlock(&pool->mutex);
    
    /* Wait for all threads to finish */
    for (size_t i = 0; i < pool->num_threads; i++) {
        trix_thread_join(pool->threads[i], NULL);
    }
    
    free(pool->threads);
    trix_mutex_destroy(&pool->mutex);
    trix_cond_destroy(&pool->cond);
    free(pool);
}
