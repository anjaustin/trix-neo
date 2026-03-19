/*
 * logging.h — TriX Logging System
 *
 * Production-grade logging with levels, multiple outputs, and structured logs.
 * Replaces all printf() debugging with proper observability.
 *
 * Part of TriX v1.0 production hardening
 * Created: March 19, 2026
 */

#ifndef TRIXC_LOGGING_H
#define TRIXC_LOGGING_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Log Levels
 * 
 * Convention:
 *   ERROR   - Critical errors that prevent operation
 *   WARN    - Warning conditions that should be investigated
 *   INFO    - Informational messages about normal operation
 *   DEBUG   - Detailed information for debugging
 *   TRACE   - Very detailed trace information
 */
typedef enum {
    TRIX_LOG_ERROR = 0,   /* Critical errors */
    TRIX_LOG_WARN  = 1,   /* Warnings */
    TRIX_LOG_INFO  = 2,   /* Informational */
    TRIX_LOG_DEBUG = 3,   /* Debug info */
    TRIX_LOG_TRACE = 4,   /* Detailed trace */
    TRIX_LOG_LEVEL_COUNT
} trix_log_level_t;

/*
 * Log Output Targets
 */
typedef enum {
    TRIX_LOG_TO_STDOUT   = 0x01,  /* Log to stdout */
    TRIX_LOG_TO_STDERR   = 0x02,  /* Log to stderr */
    TRIX_LOG_TO_FILE     = 0x04,  /* Log to file */
    TRIX_LOG_TO_CALLBACK = 0x08,  /* Log to callback */
} trix_log_output_t;

/*
 * Log Format
 */
typedef enum {
    TRIX_LOG_FORMAT_TEXT = 0,     /* Human-readable text */
    TRIX_LOG_FORMAT_JSON = 1,     /* Structured JSON */
} trix_log_format_t;

/*
 * Log Entry
 * 
 * Represents a single log message with all metadata.
 */
#define TRIX_LOG_MESSAGE_MAX 512

typedef struct {
    trix_log_level_t level;              /* Log level */
    const char* file;                    /* Source file */
    int line;                            /* Source line */
    const char* function;                /* Function name */
    unsigned long timestamp_ms;          /* Timestamp (milliseconds since epoch) */
    unsigned int thread_id;              /* Thread ID */
    char message[TRIX_LOG_MESSAGE_MAX];  /* Log message */
} trix_log_entry_t;

/*
 * Log Callback
 * 
 * User-provided callback for custom log handling.
 * 
 * @param entry    Log entry
 * @param userdata User data passed to callback
 */
typedef void (*trix_log_callback_t)(const trix_log_entry_t* entry, void* userdata);

/*
 * Log Configuration
 */
#define TRIX_LOG_FILE_PATH_MAX 256

typedef struct {
    trix_log_level_t level;                    /* Minimum log level */
    unsigned int output_mask;                  /* Output targets (bitwise OR) */
    trix_log_format_t format;                  /* Log format */
    char file_path[TRIX_LOG_FILE_PATH_MAX];   /* File path (if TO_FILE) */
    bool append;                               /* Append to file or truncate */
    bool timestamps;                           /* Include timestamps */
    bool thread_ids;                           /* Include thread IDs */
    bool colors;                               /* Use ANSI colors (stdout/stderr) */
    trix_log_callback_t callback;              /* Custom callback (if TO_CALLBACK) */
    void* callback_userdata;                   /* User data for callback */
} trix_log_config_t;

/*
 * Initialization & Configuration
 */

/**
 * Initialize logging with default configuration.
 * 
 * Default:
 *   - Level: INFO
 *   - Output: stderr
 *   - Format: text
 *   - Timestamps: enabled
 *   - Colors: enabled (if terminal)
 */
void trix_log_init(void);

/**
 * Initialize logging with custom configuration.
 * 
 * @param config Configuration
 */
void trix_log_init_with_config(const trix_log_config_t* config);

/**
 * Shutdown logging (close files, flush buffers).
 */
void trix_log_shutdown(void);

/**
 * Set minimum log level at runtime.
 * 
 * @param level Minimum level to log
 */
void trix_log_set_level(trix_log_level_t level);

/**
 * Get current log level.
 * 
 * @return Current minimum log level
 */
trix_log_level_t trix_log_get_level(void);

/**
 * Check if level is enabled.
 * 
 * @param level Log level to check
 * @return true if level is enabled
 */
bool trix_log_is_enabled(trix_log_level_t level);

/*
 * Core Logging API
 */

/**
 * Log a message at specified level.
 * 
 * @param level    Log level
 * @param file     Source file
 * @param line     Source line
 * @param function Function name
 * @param format   Printf-style format
 * @param ...      Format arguments
 */
void trix_log(trix_log_level_t level, const char* file, int line,
              const char* function, const char* format, ...);

/**
 * Convenience macros for logging at specific levels.
 * 
 * These macros automatically include file, line, and function context.
 * 
 * Usage:
 *   log_error("Failed to open file: %s", filename);
 *   log_warn("Using default value: %d", default_val);
 *   log_info("Processing %d records", count);
 *   log_debug("Variable x = %d", x);
 *   log_trace("Entering function");
 */
#define log_error(...) \
    trix_log(TRIX_LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define log_warn(...) \
    trix_log(TRIX_LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define log_info(...) \
    trix_log(TRIX_LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define log_debug(...) \
    trix_log(TRIX_LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define log_trace(...) \
    trix_log(TRIX_LOG_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)

/*
 * Conditional Logging
 */

/**
 * Log if condition is true.
 * 
 * Usage:
 *   log_error_if(x < 0, "Invalid value: %d", x);
 */
#define log_error_if(cond, ...) \
    do { if (cond) log_error(__VA_ARGS__); } while (0)

#define log_warn_if(cond, ...) \
    do { if (cond) log_warn(__VA_ARGS__); } while (0)

#define log_info_if(cond, ...) \
    do { if (cond) log_info(__VA_ARGS__); } while (0)

#define log_debug_if(cond, ...) \
    do { if (cond) log_debug(__VA_ARGS__); } while (0)

#define log_trace_if(cond, ...) \
    do { if (cond) log_trace(__VA_ARGS__); } while (0)

/*
 * Hexdump Logging (for debugging binary data)
 */

/**
 * Log hexdump of binary data.
 * 
 * @param level  Log level
 * @param data   Binary data
 * @param size   Data size in bytes
 * @param label  Label for the dump
 */
void trix_log_hexdump(trix_log_level_t level, const void* data, 
                      size_t size, const char* label);

#define log_hexdump_debug(data, size, label) \
    trix_log_hexdump(TRIX_LOG_DEBUG, (data), (size), (label))

#define log_hexdump_trace(data, size, label) \
    trix_log_hexdump(TRIX_LOG_TRACE, (data), (size), (label))

/*
 * Utility Functions
 */

/**
 * Get log level name.
 * 
 * @param level Log level
 * @return Level name (e.g., "ERROR", "WARN", "INFO")
 */
const char* trix_log_level_name(trix_log_level_t level);

/**
 * Parse log level from string.
 * 
 * @param name Level name (case-insensitive)
 * @return Log level, or TRIX_LOG_INFO if invalid
 */
trix_log_level_t trix_log_level_from_name(const char* name);

/**
 * Flush all log output.
 */
void trix_log_flush(void);

/*
 * Compile-Time Log Level Control
 * 
 * Define TRIX_LOG_LEVEL_COMPILE at compile time to remove logs below
 * a certain level from the binary. This eliminates runtime overhead
 * for disabled log levels.
 * 
 * Example:
 *   gcc -DTRIX_LOG_LEVEL_COMPILE=TRIX_LOG_INFO ...
 * 
 * This removes all DEBUG and TRACE logs from the binary.
 */

#ifndef TRIX_LOG_LEVEL_COMPILE
#define TRIX_LOG_LEVEL_COMPILE TRIX_LOG_TRACE  /* Include all logs by default */
#endif

#if TRIX_LOG_LEVEL_COMPILE > TRIX_LOG_TRACE
#undef log_trace
#define log_trace(...) do {} while (0)
#endif

#if TRIX_LOG_LEVEL_COMPILE > TRIX_LOG_DEBUG
#undef log_debug
#define log_debug(...) do {} while (0)
#endif

#if TRIX_LOG_LEVEL_COMPILE > TRIX_LOG_INFO
#undef log_info
#define log_info(...) do {} while (0)
#endif

#if TRIX_LOG_LEVEL_COMPILE > TRIX_LOG_WARN
#undef log_warn
#define log_warn(...) do {} while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_LOGGING_H */
