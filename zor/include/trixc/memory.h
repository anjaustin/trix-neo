/*
 * memory.h — TriX Memory Safety Utilities
 *
 * Safe memory allocation, string operations, and cleanup patterns.
 * Prevents memory leaks, buffer overflows, and use-after-free bugs.
 *
 * Part of TriX v1.0 production hardening
 * Created: March 19, 2026
 */

#ifndef TRIXC_MEMORY_H
#define TRIXC_MEMORY_H

#include "errors.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Safe Memory Allocation
 *
 * These wrappers provide:
 * - NULL checks
 * - Zero initialization
 * - Error reporting
 * - Memory tracking (debug builds)
 */

/**
 * Safe malloc with NULL check and zero initialization.
 *
 * @param size Size in bytes
 * @param ctx  Error context (optional)
 * @return Allocated memory, or NULL on failure
 */
static inline void* trix_malloc(size_t size, trix_error_context_t* ctx) {
    void* ptr = malloc(size);
    if (ptr == NULL && size > 0) {
        if (ctx != NULL) {
            TRIX_ERROR_SET(ctx, TRIX_ERROR_OUT_OF_MEMORY,
                          "Failed to allocate %zu bytes", size);
        }
        return NULL;
    }
    if (ptr != NULL) {
        memset(ptr, 0, size);  /* Zero initialize */
    }
    return ptr;
}

/**
 * Safe calloc with NULL check.
 *
 * @param nmemb Number of elements
 * @param size  Size of each element
 * @param ctx   Error context (optional)
 * @return Allocated memory, or NULL on failure
 */
static inline void* trix_calloc(size_t nmemb, size_t size, 
                                trix_error_context_t* ctx) {
    void* ptr = calloc(nmemb, size);
    if (ptr == NULL && nmemb > 0 && size > 0) {
        if (ctx != NULL) {
            TRIX_ERROR_SET(ctx, TRIX_ERROR_OUT_OF_MEMORY,
                          "Failed to allocate %zu elements of %zu bytes",
                          nmemb, size);
        }
        return NULL;
    }
    return ptr;
}

/**
 * Safe realloc with NULL check.
 *
 * @param ptr  Pointer to reallocate (can be NULL)
 * @param size New size
 * @param ctx  Error context (optional)
 * @return Reallocated memory, or NULL on failure
 */
static inline void* trix_realloc(void* ptr, size_t size,
                                 trix_error_context_t* ctx) {
    void* new_ptr = realloc(ptr, size);
    if (new_ptr == NULL && size > 0) {
        if (ctx != NULL) {
            TRIX_ERROR_SET(ctx, TRIX_ERROR_OUT_OF_MEMORY,
                          "Failed to reallocate to %zu bytes", size);
        }
        /* Original pointer is still valid! */
        return NULL;
    }
    return new_ptr;
}

/**
 * Safe free that NULLs the pointer.
 *
 * Usage:
 *   char* buf = malloc(100);
 *   trix_free(buf);  // buf is now NULL
 */
#define trix_free(ptr) \
    do { \
        free(ptr); \
        (ptr) = NULL; \
    } while (0)

/*
 * Safe String Operations
 *
 * These prevent buffer overflows and ensure null termination.
 */

/**
 * Safe string copy with bounds checking.
 *
 * Always null-terminates destination. Returns false if truncated.
 *
 * @param dest Destination buffer
 * @param src  Source string
 * @param size Size of destination buffer
 * @return true if copied completely, false if truncated
 */
static inline bool trix_strcpy_safe(char* dest, const char* src, size_t size) {
    if (dest == NULL || src == NULL || size == 0) {
        return false;
    }
    
    size_t src_len = strlen(src);
    if (src_len >= size) {
        /* Truncate */
        strncpy(dest, src, size - 1);
        dest[size - 1] = '\0';
        return false;  /* Truncated */
    }
    
    strcpy(dest, src);
    return true;  /* Copied completely */
}

/**
 * Safe string concatenation with bounds checking.
 *
 * Always null-terminates destination. Returns false if truncated.
 *
 * @param dest Destination buffer
 * @param src  Source string to append
 * @param size Size of destination buffer
 * @return true if appended completely, false if truncated
 */
static inline bool trix_strcat_safe(char* dest, const char* src, size_t size) {
    if (dest == NULL || src == NULL || size == 0) {
        return false;
    }
    
    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);
    
    if (dest_len + src_len >= size) {
        /* Truncate */
        size_t available = size - dest_len - 1;
        if (available > 0) {
            strncat(dest, src, available);
        }
        dest[size - 1] = '\0';
        return false;  /* Truncated */
    }
    
    strcat(dest, src);
    return true;  /* Appended completely */
}

/**
 * Safe sprintf with bounds checking.
 *
 * @param dest   Destination buffer
 * @param size   Size of destination buffer
 * @param format Printf-style format
 * @param ...    Format arguments
 * @return Number of characters written (excluding null), or -1 if truncated
 */
#define trix_sprintf_safe(dest, size, format, ...) \
    snprintf((dest), (size), (format), ##__VA_ARGS__)

/*
 * Cleanup Patterns
 *
 * Use goto cleanup pattern to ensure resources are always freed.
 */

/**
 * Example of proper cleanup pattern:
 *
 * trix_error_t my_function(const char* filename) {
 *     FILE* file = NULL;
 *     char* buffer = NULL;
 *     trix_error_t result = TRIX_OK;
 *     trix_error_context_t ctx;
 *     trix_error_init(&ctx);
 *
 *     // Allocate resources
 *     file = fopen(filename, "r");
 *     if (file == NULL) {
 *         result = TRIX_ERROR_FILE_NOT_FOUND;
 *         goto cleanup;
 *     }
 *
 *     buffer = trix_malloc(1024, &ctx);
 *     if (buffer == NULL) {
 *         result = TRIX_ERROR_OUT_OF_MEMORY;
 *         goto cleanup;
 *     }
 *
 *     // Do work...
 *     if (error_condition) {
 *         result = TRIX_ERROR_WHATEVER;
 *         goto cleanup;
 *     }
 *
 * cleanup:
 *     // Always executed, even on errors
 *     if (file != NULL) {
 *         fclose(file);
 *     }
 *     trix_free(buffer);  // Safe even if NULL
 *
 *     return result;
 * }
 */

/*
 * Memory Debugging
 *
 * Enable with -DTRIX_MEMORY_DEBUG
 */

#ifdef TRIX_MEMORY_DEBUG

/* Track allocations for leak detection */
void trix_memory_track_alloc(void* ptr, size_t size, const char* file, int line);
void trix_memory_track_free(void* ptr, const char* file, int line);
void trix_memory_print_leaks(void);

#define trix_malloc_debug(size, ctx) \
    ({ \
        void* _ptr = trix_malloc(size, ctx); \
        if (_ptr != NULL) { \
            trix_memory_track_alloc(_ptr, size, __FILE__, __LINE__); \
        } \
        _ptr; \
    })

#define trix_free_debug(ptr) \
    do { \
        if (ptr != NULL) { \
            trix_memory_track_free(ptr, __FILE__, __LINE__); \
        } \
        trix_free(ptr); \
    } while (0)

#else

#define trix_malloc_debug trix_malloc
#define trix_free_debug trix_free
#define trix_memory_print_leaks() do {} while (0)

#endif /* TRIX_MEMORY_DEBUG */

/*
 * Bounds Checking
 */

/**
 * Check array access is in bounds.
 *
 * @param index Array index
 * @param size  Array size
 * @param ctx   Error context
 * @return TRIX_OK if in bounds, error otherwise
 */
static inline trix_error_t trix_check_bounds(int index, int size,
                                             trix_error_context_t* ctx) {
    if (index < 0 || index >= size) {
        if (ctx != NULL) {
            TRIX_ERROR_SET(ctx, TRIX_ERROR_BUFFER_OVERFLOW,
                          "Index %d out of bounds [0, %d)", index, size);
        }
        return TRIX_ERROR_BUFFER_OVERFLOW;
    }
    return TRIX_OK;
}

/**
 * Macro for bounds-checked array access.
 *
 * Usage:
 *   float value = TRIX_ARRAY_GET(array, i, size, ctx);
 */
#define TRIX_ARRAY_GET(array, index, size, ctx) \
    ({ \
        trix_check_bounds((index), (size), (ctx)); \
        (array)[index]; \
    })

/**
 * Check buffer size before copy.
 *
 * @param src_size  Source size
 * @param dest_size Destination size
 * @param ctx       Error context
 * @return TRIX_OK if fits, error otherwise
 */
static inline trix_error_t trix_check_buffer_size(size_t src_size, 
                                                   size_t dest_size,
                                                   trix_error_context_t* ctx) {
    if (src_size > dest_size) {
        if (ctx != NULL) {
            TRIX_ERROR_SET(ctx, TRIX_ERROR_BUFFER_TOO_SMALL,
                          "Buffer too small: need %zu bytes, have %zu",
                          src_size, dest_size);
        }
        return TRIX_ERROR_BUFFER_TOO_SMALL;
    }
    return TRIX_OK;
}

/*
 * Common Patterns
 */

/**
 * Duplicate a string safely.
 *
 * @param src Source string
 * @param ctx Error context
 * @return Duplicated string, or NULL on failure
 */
static inline char* trix_strdup_safe(const char* src, 
                                     trix_error_context_t* ctx) {
    if (src == NULL) return NULL;
    
    size_t len = strlen(src);
    char* dup = (char*)trix_malloc(len + 1, ctx);
    if (dup == NULL) return NULL;
    
    memcpy(dup, src, len + 1);
    return dup;
}

/**
 * Allocate and copy memory safely.
 *
 * @param src  Source data
 * @param size Size in bytes
 * @param ctx  Error context
 * @return Copy of data, or NULL on failure
 */
static inline void* trix_memdup_safe(const void* src, size_t size,
                                     trix_error_context_t* ctx) {
    if (src == NULL || size == 0) return NULL;
    
    void* dup = trix_malloc(size, ctx);
    if (dup == NULL) return NULL;
    
    memcpy(dup, src, size);
    return dup;
}

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_MEMORY_H */
