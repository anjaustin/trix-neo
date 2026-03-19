/*
 * logging.c — TriX Logging System Implementation
 *
 * Part of TriX v1.0 production hardening
 * Created: March 19, 2026
 */

#include "../include/trixc/logging.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <strings.h>

#ifdef __APPLE__
#include <pthread.h>
#define gettid() ((unsigned int)pthread_mach_thread_np(pthread_self()))
#elif defined(__linux__)
#include <unistd.h>
#include <sys/syscall.h>
#define gettid() ((unsigned int)syscall(SYS_gettid))
#else
#define gettid() 0
#endif

/* ANSI color codes */
#define ANSI_RESET   "\033[0m"
#define ANSI_RED     "\033[31m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_GRAY    "\033[90m"

/* Global configuration */
static trix_log_config_t g_config;
static bool g_initialized = false;
static FILE* g_log_file = NULL;

/* Level names */
static const char* level_names[] = {
    [TRIX_LOG_ERROR] = "ERROR",
    [TRIX_LOG_WARN]  = "WARN ",
    [TRIX_LOG_INFO]  = "INFO ",
    [TRIX_LOG_DEBUG] = "DEBUG",
    [TRIX_LOG_TRACE] = "TRACE",
};

/* Level colors */
static const char* level_colors[] = {
    [TRIX_LOG_ERROR] = ANSI_RED,
    [TRIX_LOG_WARN]  = ANSI_YELLOW,
    [TRIX_LOG_INFO]  = ANSI_GREEN,
    [TRIX_LOG_DEBUG] = ANSI_CYAN,
    [TRIX_LOG_TRACE] = ANSI_GRAY,
};

/*
 * Helper Functions
 */

static unsigned long get_timestamp_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000UL + tv.tv_usec / 1000UL;
}

static bool is_terminal(FILE* stream) {
    #ifdef _WIN32
    return false;  /* Simplify for Windows */
    #else
    return isatty(fileno(stream)) != 0;
    #endif
}

/*
 * Public API Implementation
 */

void trix_log_init(void) {
    trix_log_config_t config = {
        .level = TRIX_LOG_INFO,
        .output_mask = TRIX_LOG_TO_STDERR,
        .format = TRIX_LOG_FORMAT_TEXT,
        .append = false,
        .timestamps = true,
        .thread_ids = false,
        .colors = is_terminal(stderr),
        .callback = NULL,
        .callback_userdata = NULL,
    };
    
    trix_log_init_with_config(&config);
}

void trix_log_init_with_config(const trix_log_config_t* config) {
    if (config == NULL) {
        trix_log_init();
        return;
    }
    
    /* Copy configuration */
    memcpy(&g_config, config, sizeof(trix_log_config_t));
    
    /* Open log file if requested */
    if (g_config.output_mask & TRIX_LOG_TO_FILE) {
        const char* mode = g_config.append ? "a" : "w";
        g_log_file = fopen(g_config.file_path, mode);
        if (g_log_file == NULL) {
            fprintf(stderr, "Warning: Failed to open log file: %s\n", 
                    g_config.file_path);
            g_config.output_mask &= ~TRIX_LOG_TO_FILE;
        }
    }
    
    g_initialized = true;
}

void trix_log_shutdown(void) {
    if (!g_initialized) return;
    
    /* Close log file */
    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    
    g_initialized = false;
}

void trix_log_set_level(trix_log_level_t level) {
    if (!g_initialized) trix_log_init();
    g_config.level = level;
}

trix_log_level_t trix_log_get_level(void) {
    if (!g_initialized) trix_log_init();
    return g_config.level;
}

bool trix_log_is_enabled(trix_log_level_t level) {
    if (!g_initialized) trix_log_init();
    return level <= g_config.level;
}

const char* trix_log_level_name(trix_log_level_t level) {
    if (level < 0 || level >= TRIX_LOG_LEVEL_COUNT) {
        return "UNKNOWN";
    }
    return level_names[level];
}

trix_log_level_t trix_log_level_from_name(const char* name) {
    if (name == NULL) return TRIX_LOG_INFO;
    
    for (int i = 0; i < TRIX_LOG_LEVEL_COUNT; i++) {
        if (strcasecmp(name, level_names[i]) == 0) {
            return (trix_log_level_t)i;
        }
    }
    
    return TRIX_LOG_INFO;
}

/*
 * Format and output log entry
 */

static void format_text(const trix_log_entry_t* entry, char* buffer, size_t size,
                       bool use_colors) {
    size_t offset = 0;
    
    /* Timestamp */
    if (g_config.timestamps) {
        time_t sec = entry->timestamp_ms / 1000;
        unsigned int ms = entry->timestamp_ms % 1000;
        struct tm tm;
        localtime_r(&sec, &tm);
        
        offset += snprintf(buffer + offset, size - offset,
                          "%04d-%02d-%02d %02d:%02d:%02d.%03u ",
                          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                          tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    }
    
    /* Thread ID */
    if (g_config.thread_ids) {
        offset += snprintf(buffer + offset, size - offset,
                          "[%u] ", entry->thread_id);
    }
    
    /* Level with color */
    if (use_colors && entry->level < TRIX_LOG_LEVEL_COUNT) {
        offset += snprintf(buffer + offset, size - offset,
                          "%s%-5s%s ",
                          level_colors[entry->level],
                          level_names[entry->level],
                          ANSI_RESET);
    } else {
        offset += snprintf(buffer + offset, size - offset,
                          "%-5s ", level_names[entry->level]);
    }
    
    /* Location */
    offset += snprintf(buffer + offset, size - offset,
                      "[%s:%d] ", entry->function, entry->line);
    
    /* Message */
    offset += snprintf(buffer + offset, size - offset,
                      "%s", entry->message);
}

static void format_json(const trix_log_entry_t* entry, char* buffer, size_t size) {
    snprintf(buffer, size,
             "{\"timestamp\":%lu,\"level\":\"%s\",\"file\":\"%s\","
             "\"line\":%d,\"function\":\"%s\",\"thread\":%u,\"message\":\"%s\"}",
             entry->timestamp_ms,
             trix_log_level_name(entry->level),
             entry->file ? entry->file : "",
             entry->line,
             entry->function ? entry->function : "",
             entry->thread_id,
             entry->message);
}

static void output_entry(const trix_log_entry_t* entry) {
    char buffer[2048];
    
    /* Format based on configuration */
    if (g_config.format == TRIX_LOG_FORMAT_JSON) {
        format_json(entry, buffer, sizeof(buffer));
    } else {
        bool use_colors = false;
        
        /* Output to stdout */
        if (g_config.output_mask & TRIX_LOG_TO_STDOUT) {
            use_colors = g_config.colors && is_terminal(stdout);
            format_text(entry, buffer, sizeof(buffer), use_colors);
            fprintf(stdout, "%s\n", buffer);
        }
        
        /* Output to stderr */
        if (g_config.output_mask & TRIX_LOG_TO_STDERR) {
            use_colors = g_config.colors && is_terminal(stderr);
            format_text(entry, buffer, sizeof(buffer), use_colors);
            fprintf(stderr, "%s\n", buffer);
        }
        
        /* Output to file */
        if ((g_config.output_mask & TRIX_LOG_TO_FILE) && g_log_file != NULL) {
            format_text(entry, buffer, sizeof(buffer), false);  /* No colors in file */
            fprintf(g_log_file, "%s\n", buffer);
        }
        
        return;
    }
    
    /* JSON format - output to all targets */
    if (g_config.output_mask & TRIX_LOG_TO_STDOUT) {
        fprintf(stdout, "%s\n", buffer);
    }
    
    if (g_config.output_mask & TRIX_LOG_TO_STDERR) {
        fprintf(stderr, "%s\n", buffer);
    }
    
    if ((g_config.output_mask & TRIX_LOG_TO_FILE) && g_log_file != NULL) {
        fprintf(g_log_file, "%s\n", buffer);
    }
    
    /* Call user callback */
    if ((g_config.output_mask & TRIX_LOG_TO_CALLBACK) && g_config.callback != NULL) {
        g_config.callback(entry, g_config.callback_userdata);
    }
}

void trix_log(trix_log_level_t level, const char* file, int line,
              const char* function, const char* format, ...) {
    if (!g_initialized) trix_log_init();
    
    /* Check if level is enabled */
    if (!trix_log_is_enabled(level)) return;
    
    /* Build log entry */
    trix_log_entry_t entry = {
        .level = level,
        .file = file,
        .line = line,
        .function = function,
        .timestamp_ms = get_timestamp_ms(),
        .thread_id = gettid(),
    };
    
    /* Format message */
    va_list args;
    va_start(args, format);
    vsnprintf(entry.message, TRIX_LOG_MESSAGE_MAX, format, args);
    va_end(args);
    
    /* Output */
    output_entry(&entry);
}

void trix_log_hexdump(trix_log_level_t level, const void* data, 
                      size_t size, const char* label) {
    if (!trix_log_is_enabled(level)) return;
    if (data == NULL || size == 0) return;
    
    const unsigned char* bytes = (const unsigned char*)data;
    char line_buffer[128];
    
    log_debug("%s (%zu bytes):", label ? label : "Hexdump", size);
    
    for (size_t offset = 0; offset < size; offset += 16) {
        size_t n = (size - offset < 16) ? (size - offset) : 16;
        
        /* Format hex */
        char hex[64];
        size_t hex_offset = 0;
        for (size_t i = 0; i < n; i++) {
            hex_offset += snprintf(hex + hex_offset, sizeof(hex) - hex_offset,
                                  "%02x ", bytes[offset + i]);
        }
        
        /* Format ASCII */
        char ascii[20];
        for (size_t i = 0; i < n; i++) {
            unsigned char c = bytes[offset + i];
            ascii[i] = (c >= 32 && c < 127) ? c : '.';
        }
        ascii[n] = '\0';
        
        snprintf(line_buffer, sizeof(line_buffer),
                "  %04zx: %-48s  %s", offset, hex, ascii);
        
        trix_log(level, __FILE__, __LINE__, __func__, "%s", line_buffer);
    }
}

void trix_log_flush(void) {
    if (g_config.output_mask & TRIX_LOG_TO_STDOUT) {
        fflush(stdout);
    }
    
    if (g_config.output_mask & TRIX_LOG_TO_STDERR) {
        fflush(stderr);
    }
    
    if (g_log_file != NULL) {
        fflush(g_log_file);
    }
}
