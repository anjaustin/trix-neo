/*
 * errors.c — TriX Error Handling Implementation
 *
 * Part of TriX v1.0 production hardening
 * Created: March 19, 2026
 */

#include "../include/trixc/errors.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* Thread-local error storage */
static _Thread_local trix_error_context_t g_last_error;

/*
 * Error name and description tables
 */

static const char* error_names[] = {
    [TRIX_OK] = "TRIX_OK",
    [TRIX_ERROR_UNKNOWN] = "TRIX_ERROR_UNKNOWN",
    [TRIX_ERROR_NOT_IMPLEMENTED] = "TRIX_ERROR_NOT_IMPLEMENTED",
    [TRIX_ERROR_INVALID_ARGUMENT] = "TRIX_ERROR_INVALID_ARGUMENT",
    [TRIX_ERROR_NULL_POINTER] = "TRIX_ERROR_NULL_POINTER",
    [TRIX_ERROR_INTERNAL] = "TRIX_ERROR_INTERNAL",
    
    [TRIX_ERROR_FILE_NOT_FOUND] = "TRIX_ERROR_FILE_NOT_FOUND",
    [TRIX_ERROR_FILE_READ] = "TRIX_ERROR_FILE_READ",
    [TRIX_ERROR_FILE_WRITE] = "TRIX_ERROR_FILE_WRITE",
    [TRIX_ERROR_FILE_PERMISSION] = "TRIX_ERROR_FILE_PERMISSION",
    [TRIX_ERROR_FILE_EXISTS] = "TRIX_ERROR_FILE_EXISTS",
    [TRIX_ERROR_INVALID_PATH] = "TRIX_ERROR_INVALID_PATH",
    [TRIX_ERROR_PATH_TOO_LONG] = "TRIX_ERROR_PATH_TOO_LONG",
    [TRIX_ERROR_DIRECTORY_NOT_FOUND] = "TRIX_ERROR_DIRECTORY_NOT_FOUND",
    
    [TRIX_ERROR_PARSE_FAILED] = "TRIX_ERROR_PARSE_FAILED",
    [TRIX_ERROR_INVALID_YAML] = "TRIX_ERROR_INVALID_YAML",
    [TRIX_ERROR_MISSING_FIELD] = "TRIX_ERROR_MISSING_FIELD",
    [TRIX_ERROR_INVALID_FIELD] = "TRIX_ERROR_INVALID_FIELD",
    [TRIX_ERROR_UNEXPECTED_TOKEN] = "TRIX_ERROR_UNEXPECTED_TOKEN",
    [TRIX_ERROR_SYNTAX_ERROR] = "TRIX_ERROR_SYNTAX_ERROR",
    [TRIX_ERROR_INVALID_HEX] = "TRIX_ERROR_INVALID_HEX",
    [TRIX_ERROR_INVALID_BASE64] = "TRIX_ERROR_INVALID_BASE64",
    
    [TRIX_ERROR_INVALID_SPEC] = "TRIX_ERROR_INVALID_SPEC",
    [TRIX_ERROR_INVALID_NAME] = "TRIX_ERROR_INVALID_NAME",
    [TRIX_ERROR_NAME_TOO_LONG] = "TRIX_ERROR_NAME_TOO_LONG",
    [TRIX_ERROR_INVALID_VERSION] = "TRIX_ERROR_INVALID_VERSION",
    [TRIX_ERROR_INVALID_STATE_BITS] = "TRIX_ERROR_INVALID_STATE_BITS",
    [TRIX_ERROR_INVALID_SHAPE] = "TRIX_ERROR_INVALID_SHAPE",
    [TRIX_ERROR_UNKNOWN_SHAPE] = "TRIX_ERROR_UNKNOWN_SHAPE",
    [TRIX_ERROR_TOO_MANY_SHAPES] = "TRIX_ERROR_TOO_MANY_SHAPES",
    [TRIX_ERROR_INVALID_SIGNATURE] = "TRIX_ERROR_INVALID_SIGNATURE",
    [TRIX_ERROR_TOO_MANY_SIGNATURES] = "TRIX_ERROR_TOO_MANY_SIGNATURES",
    [TRIX_ERROR_INVALID_PATTERN] = "TRIX_ERROR_INVALID_PATTERN",
    [TRIX_ERROR_PATTERN_TOO_SHORT] = "TRIX_ERROR_PATTERN_TOO_SHORT",
    [TRIX_ERROR_PATTERN_TOO_LONG] = "TRIX_ERROR_PATTERN_TOO_LONG",
    [TRIX_ERROR_INVALID_THRESHOLD] = "TRIX_ERROR_INVALID_THRESHOLD",
    [TRIX_ERROR_THRESHOLD_OUT_OF_RANGE] = "TRIX_ERROR_THRESHOLD_OUT_OF_RANGE",
    [TRIX_ERROR_DUPLICATE_SIGNATURE] = "TRIX_ERROR_DUPLICATE_SIGNATURE",
    [TRIX_ERROR_INVALID_MODE] = "TRIX_ERROR_INVALID_MODE",
    [TRIX_ERROR_TOO_MANY_LAYERS] = "TRIX_ERROR_TOO_MANY_LAYERS",
    [TRIX_ERROR_INVALID_DIMENSIONS] = "TRIX_ERROR_INVALID_DIMENSIONS",
    
    [TRIX_ERROR_NOT_INITIALIZED] = "TRIX_ERROR_NOT_INITIALIZED",
    [TRIX_ERROR_ALREADY_INITIALIZED] = "TRIX_ERROR_ALREADY_INITIALIZED",
    [TRIX_ERROR_STATE_MISMATCH] = "TRIX_ERROR_STATE_MISMATCH",
    [TRIX_ERROR_INFERENCE_FAILED] = "TRIX_ERROR_INFERENCE_FAILED",
    [TRIX_ERROR_NO_MATCH] = "TRIX_ERROR_NO_MATCH",
    [TRIX_ERROR_TIMEOUT] = "TRIX_ERROR_TIMEOUT",
    [TRIX_ERROR_CANCELLED] = "TRIX_ERROR_CANCELLED",
    [TRIX_ERROR_OVERFLOW] = "TRIX_ERROR_OVERFLOW",
    [TRIX_ERROR_UNDERFLOW] = "TRIX_ERROR_UNDERFLOW",
    
    [TRIX_ERROR_OUT_OF_MEMORY] = "TRIX_ERROR_OUT_OF_MEMORY",
    [TRIX_ERROR_ALLOCATION_FAILED] = "TRIX_ERROR_ALLOCATION_FAILED",
    [TRIX_ERROR_BUFFER_TOO_SMALL] = "TRIX_ERROR_BUFFER_TOO_SMALL",
    [TRIX_ERROR_BUFFER_OVERFLOW] = "TRIX_ERROR_BUFFER_OVERFLOW",
    [TRIX_ERROR_MEMORY_LEAK] = "TRIX_ERROR_MEMORY_LEAK",
    
    [TRIX_ERROR_UNSUPPORTED_PLATFORM] = "TRIX_ERROR_UNSUPPORTED_PLATFORM",
    [TRIX_ERROR_UNSUPPORTED_FEATURE] = "TRIX_ERROR_UNSUPPORTED_FEATURE",
    [TRIX_ERROR_UNSUPPORTED_TARGET] = "TRIX_ERROR_UNSUPPORTED_TARGET",
    [TRIX_ERROR_MISSING_SIMD] = "TRIX_ERROR_MISSING_SIMD",
    [TRIX_ERROR_CPU_DETECTION_FAILED] = "TRIX_ERROR_CPU_DETECTION_FAILED",
};

static const char* error_descriptions[] = {
    [TRIX_OK] = "Success",
    [TRIX_ERROR_UNKNOWN] = "Unknown error",
    [TRIX_ERROR_NOT_IMPLEMENTED] = "Feature not implemented",
    [TRIX_ERROR_INVALID_ARGUMENT] = "Invalid argument",
    [TRIX_ERROR_NULL_POINTER] = "Null pointer",
    [TRIX_ERROR_INTERNAL] = "Internal error",
    
    [TRIX_ERROR_FILE_NOT_FOUND] = "File not found",
    [TRIX_ERROR_FILE_READ] = "File read error",
    [TRIX_ERROR_FILE_WRITE] = "File write error",
    [TRIX_ERROR_FILE_PERMISSION] = "File permission denied",
    [TRIX_ERROR_FILE_EXISTS] = "File already exists",
    [TRIX_ERROR_INVALID_PATH] = "Invalid file path",
    [TRIX_ERROR_PATH_TOO_LONG] = "Path too long",
    [TRIX_ERROR_DIRECTORY_NOT_FOUND] = "Directory not found",
    
    [TRIX_ERROR_PARSE_FAILED] = "Parse failed",
    [TRIX_ERROR_INVALID_YAML] = "Invalid YAML",
    [TRIX_ERROR_MISSING_FIELD] = "Missing required field",
    [TRIX_ERROR_INVALID_FIELD] = "Invalid field value",
    [TRIX_ERROR_UNEXPECTED_TOKEN] = "Unexpected token",
    [TRIX_ERROR_SYNTAX_ERROR] = "Syntax error",
    [TRIX_ERROR_INVALID_HEX] = "Invalid hexadecimal string",
    [TRIX_ERROR_INVALID_BASE64] = "Invalid base64 string",
    
    [TRIX_ERROR_INVALID_SPEC] = "Invalid soft chip specification",
    [TRIX_ERROR_INVALID_NAME] = "Invalid name",
    [TRIX_ERROR_NAME_TOO_LONG] = "Name too long",
    [TRIX_ERROR_INVALID_VERSION] = "Invalid version",
    [TRIX_ERROR_INVALID_STATE_BITS] = "Invalid state bits",
    [TRIX_ERROR_INVALID_SHAPE] = "Invalid shape",
    [TRIX_ERROR_UNKNOWN_SHAPE] = "Unknown shape type",
    [TRIX_ERROR_TOO_MANY_SHAPES] = "Too many shapes",
    [TRIX_ERROR_INVALID_SIGNATURE] = "Invalid signature",
    [TRIX_ERROR_TOO_MANY_SIGNATURES] = "Too many signatures",
    [TRIX_ERROR_INVALID_PATTERN] = "Invalid pattern",
    [TRIX_ERROR_PATTERN_TOO_SHORT] = "Pattern too short",
    [TRIX_ERROR_PATTERN_TOO_LONG] = "Pattern too long",
    [TRIX_ERROR_INVALID_THRESHOLD] = "Invalid threshold",
    [TRIX_ERROR_THRESHOLD_OUT_OF_RANGE] = "Threshold out of range",
    [TRIX_ERROR_DUPLICATE_SIGNATURE] = "Duplicate signature name",
    [TRIX_ERROR_INVALID_MODE] = "Invalid inference mode",
    [TRIX_ERROR_TOO_MANY_LAYERS] = "Too many linear layers",
    [TRIX_ERROR_INVALID_DIMENSIONS] = "Invalid dimensions",
    
    [TRIX_ERROR_NOT_INITIALIZED] = "Not initialized",
    [TRIX_ERROR_ALREADY_INITIALIZED] = "Already initialized",
    [TRIX_ERROR_STATE_MISMATCH] = "State mismatch",
    [TRIX_ERROR_INFERENCE_FAILED] = "Inference failed",
    [TRIX_ERROR_NO_MATCH] = "No signature matched",
    [TRIX_ERROR_TIMEOUT] = "Operation timed out",
    [TRIX_ERROR_CANCELLED] = "Operation cancelled",
    [TRIX_ERROR_OVERFLOW] = "Arithmetic overflow",
    [TRIX_ERROR_UNDERFLOW] = "Arithmetic underflow",
    
    [TRIX_ERROR_OUT_OF_MEMORY] = "Out of memory",
    [TRIX_ERROR_ALLOCATION_FAILED] = "Memory allocation failed",
    [TRIX_ERROR_BUFFER_TOO_SMALL] = "Buffer too small",
    [TRIX_ERROR_BUFFER_OVERFLOW] = "Buffer overflow",
    [TRIX_ERROR_MEMORY_LEAK] = "Memory leak detected",
    
    [TRIX_ERROR_UNSUPPORTED_PLATFORM] = "Unsupported platform",
    [TRIX_ERROR_UNSUPPORTED_FEATURE] = "Unsupported feature",
    [TRIX_ERROR_UNSUPPORTED_TARGET] = "Unsupported target",
    [TRIX_ERROR_MISSING_SIMD] = "SIMD instructions not available",
    [TRIX_ERROR_CPU_DETECTION_FAILED] = "CPU feature detection failed",
};

/*
 * Public API Implementation
 */

const char* trix_error_name(trix_error_t error) {
    if (error < 0 || error >= TRIX_ERROR_COUNT) {
        return "TRIX_ERROR_INVALID";
    }
    if (error_names[error] == NULL) {
        return "TRIX_ERROR_UNKNOWN";
    }
    return error_names[error];
}

const char* trix_error_description(trix_error_t error) {
    if (error < 0 || error >= TRIX_ERROR_COUNT) {
        return "Invalid error code";
    }
    if (error_descriptions[error] == NULL) {
        return "Unknown error";
    }
    return error_descriptions[error];
}

void trix_error_init(trix_error_context_t* ctx) {
    if (ctx == NULL) return;
    
    memset(ctx, 0, sizeof(trix_error_context_t));
    ctx->code = TRIX_OK;
    ctx->line = -1;
    ctx->column = -1;
    ctx->source_line = -1;
}

void trix_error_set(trix_error_context_t* ctx, trix_error_t code, 
                    const char* message, ...) {
    if (ctx == NULL) return;
    
    ctx->code = code;
    
    if (message != NULL) {
        va_list args;
        va_start(args, message);
        vsnprintf(ctx->message, TRIX_ERROR_MESSAGE_MAX, message, args);
        va_end(args);
    } else {
        strncpy(ctx->message, trix_error_description(code), 
                TRIX_ERROR_MESSAGE_MAX - 1);
    }
}

void trix_error_set_location(trix_error_context_t* ctx, trix_error_t code,
                             const char* file, int line, const char* function,
                             const char* message, ...) {
    if (ctx == NULL) return;
    
    ctx->code = code;
    ctx->file = file;
    ctx->source_line = line;
    ctx->function = function;
    
    if (message != NULL) {
        va_list args;
        va_start(args, message);
        vsnprintf(ctx->message, TRIX_ERROR_MESSAGE_MAX, message, args);
        va_end(args);
    } else {
        strncpy(ctx->message, trix_error_description(code), 
                TRIX_ERROR_MESSAGE_MAX - 1);
    }
}

void trix_error_add_context(trix_error_context_t* ctx, 
                            const char* context, ...) {
    if (ctx == NULL || context == NULL) return;
    
    size_t current_len = strlen(ctx->context);
    if (current_len >= TRIX_ERROR_CONTEXT_MAX - 1) return;
    
    /* Add separator if there's existing context */
    if (current_len > 0 && 
        current_len < TRIX_ERROR_CONTEXT_MAX - 3) {
        strcat(ctx->context, " | ");
        current_len += 3;
    }
    
    /* Append new context */
    char* dest = ctx->context + current_len;
    size_t remaining = TRIX_ERROR_CONTEXT_MAX - current_len;
    
    va_list args;
    va_start(args, context);
    vsnprintf(dest, remaining, context, args);
    va_end(args);
}

void trix_error_format(const trix_error_context_t* ctx, 
                       char* buffer, size_t size) {
    if (ctx == NULL || buffer == NULL || size == 0) return;
    
    size_t offset = 0;
    
    /* Error code and name */
    offset += snprintf(buffer + offset, size - offset,
                      "[%s] %s", 
                      trix_error_name(ctx->code),
                      ctx->message);
    
    /* Location (if available) */
    if (ctx->file != NULL && ctx->source_line > 0) {
        offset += snprintf(buffer + offset, size - offset,
                          "\n  at %s:%d in %s()",
                          ctx->file, ctx->source_line,
                          ctx->function ? ctx->function : "?");
    }
    
    /* Parse location (if available) */
    if (ctx->line > 0) {
        offset += snprintf(buffer + offset, size - offset,
                          "\n  parse error at line %d, column %d",
                          ctx->line, ctx->column);
    }
    
    /* Additional context */
    if (ctx->context[0] != '\0') {
        offset += snprintf(buffer + offset, size - offset,
                          "\n  context: %s", ctx->context);
    }
}

void trix_error_print(const trix_error_context_t* ctx) {
    if (ctx == NULL) return;
    
    char buffer[TRIX_ERROR_CONTEXT_MAX * 2];
    trix_error_format(ctx, buffer, sizeof(buffer));
    fprintf(stderr, "TriX Error: %s\n", buffer);
}

/*
 * Thread-Local Error Storage
 */

trix_error_context_t* trix_error_get_last(void) {
    return &g_last_error;
}

void trix_error_set_last(trix_error_t code, const char* message) {
    trix_error_set(&g_last_error, code, "%s", message);
}

void trix_error_clear_last(void) {
    trix_error_init(&g_last_error);
}
