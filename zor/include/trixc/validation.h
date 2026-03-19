/*
 * validation.h — Input Validation and Sanitization
 *
 * Part of TriX Runtime (zor)
 * Production-grade input validation for safety-critical systems
 *
 * CRITICAL ITEM 4: Input Validation
 *
 * Features:
 * - Type validation (int, float, string, buffer, etc.)
 * - Range checking and bounds validation
 * - String sanitization (SQL, HTML, path, etc.)
 * - Schema validation for structured data
 * - Whitelist/blacklist filtering
 * - Format validation (email, URL, hex, base64)
 * - File path validation (no traversal, canonical paths)
 * - Array and buffer validation
 * - Fuzz testing support
 *
 * Usage:
 *   // Basic validation
 *   if (!trix_validate_int(value, 0, 100)) {
 *       return TRIX_ERR_INVALID_ARG;
 *   }
 *
 *   // String sanitization
 *   char clean[256];
 *   trix_sanitize_string(dirty, clean, sizeof(clean), TRIX_SANITIZE_ALPHANUMERIC);
 *
 *   // Schema validation
 *   TrixSchema schema = {
 *       .fields = {
 *           {"name", TRIX_TYPE_STRING, .required = true},
 *           {"age", TRIX_TYPE_INT, .min = 0, .max = 150}
 *       }
 *   };
 *   if (!trix_validate_schema(&data, &schema)) {
 *       return TRIX_ERR_INVALID_INPUT;
 *   }
 */

#ifndef TRIXC_VALIDATION_H
#define TRIXC_VALIDATION_H

#include "errors.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*═══════════════════════════════════════════════════════════════════════════
 * Type Definitions
 *═══════════════════════════════════════════════════════════════════════════*/

/* Data types for validation */
typedef enum {
    TRIX_TYPE_INT,
    TRIX_TYPE_UINT,
    TRIX_TYPE_FLOAT,
    TRIX_TYPE_DOUBLE,
    TRIX_TYPE_STRING,
    TRIX_TYPE_BUFFER,
    TRIX_TYPE_BOOL,
    TRIX_TYPE_ENUM,
    TRIX_TYPE_ARRAY,
    TRIX_TYPE_OBJECT,
    TRIX_TYPE_COUNT
} TrixDataType;

/* Sanitization modes */
typedef enum {
    TRIX_SANITIZE_NONE = 0,
    TRIX_SANITIZE_ALPHANUMERIC = (1 << 0),  /* Allow only [a-zA-Z0-9] */
    TRIX_SANITIZE_PRINTABLE = (1 << 1),     /* Allow printable ASCII */
    TRIX_SANITIZE_PATH = (1 << 2),          /* Safe file path (no ../ etc) */
    TRIX_SANITIZE_SQL = (1 << 3),           /* Escape SQL special chars */
    TRIX_SANITIZE_HTML = (1 << 4),          /* Escape HTML entities */
    TRIX_SANITIZE_URL = (1 << 5),           /* URL encode special chars */
    TRIX_SANITIZE_SHELL = (1 << 6),         /* Escape shell metacharacters */
    TRIX_SANITIZE_TRIM = (1 << 7),          /* Trim leading/trailing whitespace */
    TRIX_SANITIZE_LOWERCASE = (1 << 8),     /* Convert to lowercase */
    TRIX_SANITIZE_UPPERCASE = (1 << 9)      /* Convert to uppercase */
} TrixSanitizeMode;

/* Format validation types */
typedef enum {
    TRIX_FORMAT_EMAIL,
    TRIX_FORMAT_URL,
    TRIX_FORMAT_HEX,
    TRIX_FORMAT_BASE64,
    TRIX_FORMAT_UUID,
    TRIX_FORMAT_IPV4,
    TRIX_FORMAT_IPV6,
    TRIX_FORMAT_HOSTNAME,
    TRIX_FORMAT_MAC_ADDRESS,
    TRIX_FORMAT_DATE_ISO8601,
    TRIX_FORMAT_COUNT
} TrixFormatType;

/* Validation result */
typedef struct {
    bool valid;                    /* Overall validation result */
    trix_error_t error_code;       /* Specific error if !valid */
    const char* error_field;       /* Which field failed (if applicable) */
    char error_msg[256];           /* Human-readable error message */
} TrixValidationResult;

/* Schema field definition */
typedef struct {
    const char* name;              /* Field name */
    TrixDataType type;             /* Expected type */
    bool required;                 /* Is this field required? */
    
    /* Numeric constraints */
    union {
        struct {
            int64_t min;
            int64_t max;
        } int_range;
        struct {
            uint64_t min;
            uint64_t max;
        } uint_range;
        struct {
            double min;
            double max;
        } float_range;
    };
    
    /* String constraints */
    size_t min_length;
    size_t max_length;
    const char** allowed_values;   /* Whitelist (NULL-terminated) */
    const char** forbidden_values; /* Blacklist (NULL-terminated) */
    TrixFormatType format;         /* Format validation */
    
    /* Array/buffer constraints */
    size_t min_elements;
    size_t max_elements;
    TrixDataType element_type;     /* For array validation */
} TrixSchemaField;

/* Schema definition */
#define TRIX_MAX_SCHEMA_FIELDS 64

typedef struct {
    TrixSchemaField fields[TRIX_MAX_SCHEMA_FIELDS];
    size_t num_fields;
    bool allow_extra_fields;       /* Allow fields not in schema? */
} TrixSchema;

/*═══════════════════════════════════════════════════════════════════════════
 * Basic Type Validation
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Validate integer is within range
 */
bool trix_validate_int(int64_t value, int64_t min, int64_t max);

/**
 * Validate unsigned integer is within range
 */
bool trix_validate_uint(uint64_t value, uint64_t min, uint64_t max);

/**
 * Validate float is within range and not NaN/Inf
 */
bool trix_validate_float(double value, double min, double max);

/**
 * Validate string length
 */
bool trix_validate_string_length(const char* str, size_t min_len, size_t max_len);

/**
 * Validate string is not NULL and contains only allowed characters
 */
bool trix_validate_string_charset(const char* str, const char* allowed_chars);

/**
 * Validate buffer size
 */
bool trix_validate_buffer(const void* buf, size_t size, size_t min_size, size_t max_size);

/**
 * Validate pointer is not NULL
 */
bool trix_validate_ptr(const void* ptr);

/**
 * Validate array bounds
 */
bool trix_validate_array_index(size_t index, size_t array_size);

/*═══════════════════════════════════════════════════════════════════════════
 * Format Validation
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Validate string matches expected format
 */
bool trix_validate_format(const char* str, TrixFormatType format);

/**
 * Validate email address format
 */
bool trix_validate_email(const char* email);

/**
 * Validate URL format
 */
bool trix_validate_url(const char* url);

/**
 * Validate hexadecimal string
 */
bool trix_validate_hex(const char* hex, size_t expected_bytes);

/**
 * Validate base64 string
 */
bool trix_validate_base64(const char* b64);

/**
 * Validate UUID format
 */
bool trix_validate_uuid(const char* uuid);

/**
 * Validate IPv4 address
 */
bool trix_validate_ipv4(const char* ip);

/**
 * Validate IPv6 address
 */
bool trix_validate_ipv6(const char* ip);

/*═══════════════════════════════════════════════════════════════════════════
 * Path Validation (Security Critical)
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Validate file path (no directory traversal, no special chars)
 * Returns true if path is safe to use
 */
bool trix_validate_path(const char* path);

/**
 * Validate path is within allowed directory (no escaping via ../)
 */
bool trix_validate_path_in_dir(const char* path, const char* allowed_dir);

/**
 * Canonicalize path (resolve .., ., symlinks) and validate
 */
bool trix_canonicalize_path(const char* path, char* canonical, size_t size);

/*═══════════════════════════════════════════════════════════════════════════
 * Sanitization
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Sanitize string by removing/escaping dangerous characters
 * Returns length of sanitized string (or -1 on error)
 */
int trix_sanitize_string(const char* input, char* output, size_t output_size, 
                         uint32_t modes);

/**
 * Escape SQL special characters
 */
int trix_escape_sql(const char* input, char* output, size_t output_size);

/**
 * Escape HTML entities
 */
int trix_escape_html(const char* input, char* output, size_t output_size);

/**
 * URL encode string
 */
int trix_url_encode(const char* input, char* output, size_t output_size);

/**
 * Escape shell metacharacters
 */
int trix_escape_shell(const char* input, char* output, size_t output_size);

/*═══════════════════════════════════════════════════════════════════════════
 * Whitelist/Blacklist
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Check if value is in whitelist (NULL-terminated array)
 */
bool trix_in_whitelist(const char* value, const char** whitelist);

/**
 * Check if value is NOT in blacklist (NULL-terminated array)
 */
bool trix_not_in_blacklist(const char* value, const char** blacklist);

/**
 * Validate enum value is in allowed range
 */
bool trix_validate_enum(int value, int min, int max);

/*═══════════════════════════════════════════════════════════════════════════
 * Schema Validation
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Initialize schema with defaults
 */
void trix_schema_init(TrixSchema* schema);

/**
 * Add field to schema
 */
bool trix_schema_add_field(TrixSchema* schema, const TrixSchemaField* field);

/**
 * Validate data against schema
 * For simple key-value validation, pass arrays of keys and values
 */
TrixValidationResult trix_validate_schema(const char** keys, const void** values,
                                          size_t num_pairs, const TrixSchema* schema);

/*═══════════════════════════════════════════════════════════════════════════
 * Convenience Macros
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Validate and return error if invalid
 * Usage: TRIX_VALIDATE_OR_RETURN(trix_validate_int(x, 0, 100), TRIX_ERR_INVALID_ARG);
 */
#define TRIX_VALIDATE_OR_RETURN(condition, error) \
    do { \
        if (!(condition)) { \
            TRIX_ERROR_SET(error, "Validation failed: " #condition); \
            return error; \
        } \
    } while (0)

/**
 * Validate pointer not NULL
 */
#define TRIX_VALIDATE_PTR(ptr) \
    TRIX_VALIDATE_OR_RETURN(trix_validate_ptr(ptr), TRIX_ERR_NULL_PTR)

/**
 * Validate int range
 */
#define TRIX_VALIDATE_INT_RANGE(val, min, max) \
    TRIX_VALIDATE_OR_RETURN(trix_validate_int(val, min, max), TRIX_ERR_OUT_OF_RANGE)

/**
 * Validate string not NULL and within length bounds
 */
#define TRIX_VALIDATE_STRING(str, min_len, max_len) \
    do { \
        TRIX_VALIDATE_PTR(str); \
        TRIX_VALIDATE_OR_RETURN(trix_validate_string_length(str, min_len, max_len), \
                               TRIX_ERR_INVALID_INPUT); \
    } while (0)

/**
 * Validate buffer not NULL and within size bounds
 */
#define TRIX_VALIDATE_BUFFER(buf, size, min, max) \
    do { \
        TRIX_VALIDATE_PTR(buf); \
        TRIX_VALIDATE_OR_RETURN(trix_validate_buffer(buf, size, min, max), \
                               TRIX_ERR_BUFFER_TOO_SMALL); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_VALIDATION_H */
