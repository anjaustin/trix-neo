/*
 * errors.h — TriX Error Handling System
 *
 * Comprehensive error codes and error context for production deployment.
 * All TriX functions should return trix_error_t and provide detailed context.
 *
 * Part of TriX v1.0 production hardening
 * Created: March 19, 2026
 */

#ifndef TRIXC_ERRORS_H
#define TRIXC_ERRORS_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Error Codes
 * 
 * Convention:
 *   0           = Success
 *   1-99        = General errors
 *   100-199     = File I/O errors
 *   200-299     = Parse errors
 *   300-399     = Validation errors
 *   400-499     = Runtime errors
 *   500-599     = Memory errors
 *   600-699     = Platform errors
 */
typedef enum {
    /* Success */
    TRIX_OK = 0,
    
    /* General Errors (1-99) */
    TRIX_ERROR_UNKNOWN = 1,
    TRIX_ERROR_NOT_IMPLEMENTED = 2,
    TRIX_ERROR_INVALID_ARGUMENT = 3,
    TRIX_ERROR_NULL_POINTER = 4,
    TRIX_ERROR_INTERNAL = 5,
    
    /* File I/O Errors (100-199) */
    TRIX_ERROR_FILE_NOT_FOUND = 100,
    TRIX_ERROR_FILE_READ = 101,
    TRIX_ERROR_FILE_WRITE = 102,
    TRIX_ERROR_FILE_PERMISSION = 103,
    TRIX_ERROR_FILE_EXISTS = 104,
    TRIX_ERROR_INVALID_PATH = 105,
    TRIX_ERROR_PATH_TOO_LONG = 106,
    TRIX_ERROR_DIRECTORY_NOT_FOUND = 107,
    
    /* Parse Errors (200-299) */
    TRIX_ERROR_PARSE_FAILED = 200,
    TRIX_ERROR_INVALID_YAML = 201,
    TRIX_ERROR_MISSING_FIELD = 202,
    TRIX_ERROR_INVALID_FIELD = 203,
    TRIX_ERROR_UNEXPECTED_TOKEN = 204,
    TRIX_ERROR_SYNTAX_ERROR = 205,
    TRIX_ERROR_INVALID_HEX = 206,
    TRIX_ERROR_INVALID_BASE64 = 207,
    
    /* Validation Errors (300-399) */
    TRIX_ERROR_INVALID_SPEC = 300,
    TRIX_ERROR_INVALID_NAME = 301,
    TRIX_ERROR_NAME_TOO_LONG = 302,
    TRIX_ERROR_INVALID_VERSION = 303,
    TRIX_ERROR_INVALID_STATE_BITS = 304,
    TRIX_ERROR_INVALID_SHAPE = 305,
    TRIX_ERROR_UNKNOWN_SHAPE = 306,
    TRIX_ERROR_TOO_MANY_SHAPES = 307,
    TRIX_ERROR_INVALID_SIGNATURE = 308,
    TRIX_ERROR_TOO_MANY_SIGNATURES = 309,
    TRIX_ERROR_INVALID_PATTERN = 310,
    TRIX_ERROR_PATTERN_TOO_SHORT = 311,
    TRIX_ERROR_PATTERN_TOO_LONG = 312,
    TRIX_ERROR_INVALID_THRESHOLD = 313,
    TRIX_ERROR_THRESHOLD_OUT_OF_RANGE = 314,
    TRIX_ERROR_DUPLICATE_SIGNATURE = 315,
    TRIX_ERROR_INVALID_MODE = 316,
    TRIX_ERROR_TOO_MANY_LAYERS = 317,
    TRIX_ERROR_INVALID_DIMENSIONS = 318,
    
    /* Runtime Errors (400-499) */
    TRIX_ERROR_NOT_INITIALIZED = 400,
    TRIX_ERROR_ALREADY_INITIALIZED = 401,
    TRIX_ERROR_STATE_MISMATCH = 402,
    TRIX_ERROR_INFERENCE_FAILED = 403,
    TRIX_ERROR_NO_MATCH = 404,
    TRIX_ERROR_TIMEOUT = 405,
    TRIX_ERROR_CANCELLED = 406,
    TRIX_ERROR_OVERFLOW = 407,
    TRIX_ERROR_UNDERFLOW = 408,
    TRIX_ERROR_WOULD_BLOCK = 409,
    
    /* Thread Errors (420-449) */
    TRIX_ERROR_THREAD_INIT = 420,
    TRIX_ERROR_THREAD_CREATE = 421,
    TRIX_ERROR_THREAD_JOIN = 422,
    TRIX_ERROR_THREAD_DETACH = 423,
    TRIX_ERROR_THREAD_LOCK = 424,
    TRIX_ERROR_THREAD_UNLOCK = 425,
    TRIX_ERROR_THREAD_COND = 426,
    TRIX_ERROR_THREAD_SHUTDOWN = 427,
    TRIX_ERROR_QUEUE_FULL = 428,
    TRIX_ERROR_DEADLOCK = 429,
    
    /* Memory Errors (500-599) */
    TRIX_ERROR_OUT_OF_MEMORY = 500,
    TRIX_ERROR_ALLOCATION_FAILED = 501,
    TRIX_ERROR_BUFFER_TOO_SMALL = 502,
    TRIX_ERROR_BUFFER_OVERFLOW = 503,
    TRIX_ERROR_MEMORY_LEAK = 504,
    
    /* Platform Errors (600-699) */
    TRIX_ERROR_UNSUPPORTED_PLATFORM = 600,
    TRIX_ERROR_UNSUPPORTED_FEATURE = 601,
    TRIX_ERROR_UNSUPPORTED_TARGET = 602,
    TRIX_ERROR_MISSING_SIMD = 603,
    TRIX_ERROR_CPU_DETECTION_FAILED = 604,
    
    /* Sentinel (must be last) */
    TRIX_ERROR_COUNT
} trix_error_t;

/*
 * Error Context
 * 
 * Provides detailed information about an error for debugging.
 * All parse/validation functions should populate this.
 */
#define TRIX_ERROR_MESSAGE_MAX 256
#define TRIX_ERROR_CONTEXT_MAX 512

typedef struct {
    trix_error_t code;                      /* Error code */
    char message[TRIX_ERROR_MESSAGE_MAX];   /* Human-readable error message */
    char context[TRIX_ERROR_CONTEXT_MAX];   /* Additional context (file, line, etc.) */
    int line;                                /* Line number (for parse errors) */
    int column;                              /* Column number (for parse errors) */
    const char* file;                        /* Source file where error occurred */
    int source_line;                         /* Line in source code where error occurred */
    const char* function;                    /* Function where error occurred */
} trix_error_context_t;

/*
 * Error String Conversion
 */

/**
 * Get human-readable error name.
 * 
 * @param error Error code
 * @return Error name (e.g., "TRIX_ERROR_FILE_NOT_FOUND")
 */
const char* trix_error_name(trix_error_t error);

/**
 * Get human-readable error description.
 * 
 * @param error Error code
 * @return Error description (e.g., "File not found")
 */
const char* trix_error_description(trix_error_t error);

/*
 * Error Context Management
 */

/**
 * Initialize error context.
 * 
 * @param ctx Error context to initialize
 */
void trix_error_init(trix_error_context_t* ctx);

/**
 * Set error with message.
 * 
 * @param ctx     Error context
 * @param code    Error code
 * @param message Error message (printf-style format)
 * @param ...     Format arguments
 */
void trix_error_set(trix_error_context_t* ctx, trix_error_t code, 
                    const char* message, ...);

/**
 * Set error with file location.
 * 
 * @param ctx      Error context
 * @param code     Error code
 * @param file     Source file
 * @param line     Line number
 * @param function Function name
 * @param message  Error message (printf-style format)
 * @param ...      Format arguments
 */
void trix_error_set_location(trix_error_context_t* ctx, trix_error_t code,
                             const char* file, int line, const char* function,
                             const char* message, ...);

/**
 * Add context to existing error.
 * 
 * @param ctx     Error context
 * @param context Additional context (printf-style format)
 * @param ...     Format arguments
 */
void trix_error_add_context(trix_error_context_t* ctx, 
                            const char* context, ...);

/**
 * Get formatted error string.
 * 
 * Returns a complete error description including code, message, and context.
 * Buffer must be at least TRIX_ERROR_CONTEXT_MAX bytes.
 * 
 * @param ctx    Error context
 * @param buffer Output buffer
 * @param size   Buffer size
 */
void trix_error_format(const trix_error_context_t* ctx, 
                       char* buffer, size_t size);

/**
 * Print error to stderr.
 * 
 * @param ctx Error context
 */
void trix_error_print(const trix_error_context_t* ctx);

/*
 * Convenience Macros
 */

/* Set error with current file/line/function */
#define TRIX_ERROR_SET(ctx, code, ...) \
    trix_error_set_location((ctx), (code), __FILE__, __LINE__, __func__, __VA_ARGS__)

/* Return error with context */
#define TRIX_RETURN_ERROR(ctx, code, ...) \
    do { \
        TRIX_ERROR_SET((ctx), (code), __VA_ARGS__); \
        return (code); \
    } while (0)

/* Check condition, return error if false */
#define TRIX_CHECK(condition, ctx, code, ...) \
    do { \
        if (!(condition)) { \
            TRIX_RETURN_ERROR((ctx), (code), __VA_ARGS__); \
        } \
    } while (0)

/* Check pointer not NULL */
#define TRIX_CHECK_NOT_NULL(ptr, ctx, ...) \
    TRIX_CHECK((ptr) != NULL, (ctx), TRIX_ERROR_NULL_POINTER, __VA_ARGS__)

/* Propagate error from function call */
#define TRIX_PROPAGATE(call, ctx) \
    do { \
        trix_error_t _err = (call); \
        if (_err != TRIX_OK) { \
            trix_error_add_context((ctx), "In %s", __func__); \
            return _err; \
        } \
    } while (0)

/*
 * Thread-Local Error Storage
 * 
 * For functions that can't return error context (legacy APIs).
 * Use sparingly - prefer explicit error context parameters.
 */

/**
 * Get thread-local error context.
 * 
 * @return Pointer to thread-local error context
 */
trix_error_context_t* trix_error_get_last(void);

/**
 * Set thread-local error.
 * 
 * @param code    Error code
 * @param message Error message
 */
void trix_error_set_last(trix_error_t code, const char* message);

/**
 * Clear thread-local error.
 */
void trix_error_clear_last(void);

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_ERRORS_H */
