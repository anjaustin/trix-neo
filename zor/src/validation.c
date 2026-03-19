/*
 * validation.c — Input Validation and Sanitization Implementation
 *
 * Part of TriX Runtime (zor)
 * Production-grade input validation for safety-critical systems
 */

#include "../include/trixc/validation.h"
#include "../include/trixc/errors.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <float.h>

/*═══════════════════════════════════════════════════════════════════════════
 * Basic Type Validation
 *═══════════════════════════════════════════════════════════════════════════*/

bool trix_validate_int(int64_t value, int64_t min, int64_t max) {
    return value >= min && value <= max;
}

bool trix_validate_uint(uint64_t value, uint64_t min, uint64_t max) {
    return value >= min && value <= max;
}

bool trix_validate_float(double value, double min, double max) {
    /* Check for NaN and infinity */
    if (isnan(value) || isinf(value)) {
        return false;
    }
    return value >= min && value <= max;
}

bool trix_validate_string_length(const char* str, size_t min_len, size_t max_len) {
    if (str == NULL) {
        return false;
    }
    size_t len = strlen(str);
    return len >= min_len && len <= max_len;
}

bool trix_validate_string_charset(const char* str, const char* allowed_chars) {
    if (str == NULL || allowed_chars == NULL) {
        return false;
    }
    
    while (*str) {
        if (strchr(allowed_chars, *str) == NULL) {
            return false;
        }
        str++;
    }
    return true;
}

bool trix_validate_buffer(const void* buf, size_t size, size_t min_size, size_t max_size) {
    if (buf == NULL) {
        return false;
    }
    return size >= min_size && size <= max_size;
}

bool trix_validate_ptr(const void* ptr) {
    return ptr != NULL;
}

bool trix_validate_array_index(size_t index, size_t array_size) {
    return index < array_size;
}

/*═══════════════════════════════════════════════════════════════════════════
 * Format Validation
 *═══════════════════════════════════════════════════════════════════════════*/

bool trix_validate_email(const char* email) {
    if (email == NULL) {
        return false;
    }
    
    /* Simple email validation: xxx@yyy.zzz */
    const char* at = strchr(email, '@');
    if (at == NULL || at == email) {
        return false;  /* No @ or @ at start */
    }
    
    const char* dot = strrchr(at, '.');
    if (dot == NULL || dot == at + 1 || dot[1] == '\0') {
        return false;  /* No dot after @, or dot right after @, or dot at end */
    }
    
    /* Check for valid characters */
    for (const char* p = email; *p; p++) {
        if (!isalnum(*p) && *p != '@' && *p != '.' && *p != '_' && 
            *p != '-' && *p != '+') {
            return false;
        }
    }
    
    return true;
}

bool trix_validate_url(const char* url) {
    if (url == NULL) {
        return false;
    }
    
    /* Check for http:// or https:// prefix */
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        return false;
    }
    
    /* Very basic check - at least has protocol:// + something */
    const char* after_protocol = strstr(url, "://");
    if (after_protocol == NULL || after_protocol[3] == '\0') {
        return false;
    }
    
    return true;
}

bool trix_validate_hex(const char* hex, size_t expected_bytes) {
    if (hex == NULL) {
        return false;
    }
    
    /* Skip 0x prefix if present */
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex += 2;
    }
    
    size_t len = strlen(hex);
    
    /* Check length matches expected bytes (2 hex chars per byte) */
    if (expected_bytes > 0 && len != expected_bytes * 2) {
        return false;
    }
    
    /* Validate all characters are hex digits */
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit(hex[i])) {
            return false;
        }
    }
    
    return true;
}

bool trix_validate_base64(const char* b64) {
    if (b64 == NULL) {
        return false;
    }
    
    size_t len = strlen(b64);
    if (len == 0 || len % 4 != 0) {
        return false;  /* Base64 length must be multiple of 4 */
    }
    
    /* Valid base64 characters */
    const char* valid = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
    
    for (size_t i = 0; i < len; i++) {
        if (strchr(valid, b64[i]) == NULL) {
            return false;
        }
        /* = padding only allowed at end */
        if (b64[i] == '=' && i < len - 2) {
            return false;
        }
    }
    
    return true;
}

bool trix_validate_uuid(const char* uuid) {
    if (uuid == NULL) {
        return false;
    }
    
    /* UUID format: 8-4-4-4-12 hex digits with hyphens */
    /* Example: 550e8400-e29b-41d4-a716-446655440000 */
    if (strlen(uuid) != 36) {
        return false;
    }
    
    /* Check hyphens in correct positions */
    if (uuid[8] != '-' || uuid[13] != '-' || uuid[18] != '-' || uuid[23] != '-') {
        return false;
    }
    
    /* Check all other characters are hex digits */
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            continue;  /* Skip hyphens */
        }
        if (!isxdigit(uuid[i])) {
            return false;
        }
    }
    
    return true;
}

bool trix_validate_ipv4(const char* ip) {
    if (ip == NULL) {
        return false;
    }
    
    int octets[4];
    char extra;
    int num_parsed = sscanf(ip, "%d.%d.%d.%d%c", &octets[0], &octets[1], &octets[2], &octets[3], &extra);
    
    /* Should parse exactly 4 octets, no extra characters */
    if (num_parsed != 4) {
        return false;
    }
    
    /* Each octet must be 0-255 */
    for (int i = 0; i < 4; i++) {
        if (octets[i] < 0 || octets[i] > 255) {
            return false;
        }
    }
    
    return true;
}

bool trix_validate_ipv6(const char* ip) {
    if (ip == NULL) {
        return false;
    }
    
    /* IPv6 validation is complex, so we do a simplified check */
    /* Valid characters: 0-9, a-f, A-F, : */
    int colon_count = 0;
    int hex_group_count = 0;
    bool has_double_colon = false;
    
    for (const char* p = ip; *p; p++) {
        if (*p == ':') {
            colon_count++;
            if (p[1] == ':') {
                if (has_double_colon) {
                    return false;  /* Only one :: allowed */
                }
                has_double_colon = true;
                p++;  /* Skip second colon */
            }
        } else if (isxdigit(*p)) {
            hex_group_count++;
        } else {
            return false;  /* Invalid character */
        }
    }
    
    /* IPv6 has 7 colons (8 groups) or uses :: compression */
    if (!has_double_colon && colon_count != 7) {
        return false;
    }
    
    return hex_group_count > 0;
}

bool trix_validate_format(const char* str, TrixFormatType format) {
    switch (format) {
        case TRIX_FORMAT_EMAIL:
            return trix_validate_email(str);
        case TRIX_FORMAT_URL:
            return trix_validate_url(str);
        case TRIX_FORMAT_HEX:
            return trix_validate_hex(str, 0);  /* Any length */
        case TRIX_FORMAT_BASE64:
            return trix_validate_base64(str);
        case TRIX_FORMAT_UUID:
            return trix_validate_uuid(str);
        case TRIX_FORMAT_IPV4:
            return trix_validate_ipv4(str);
        case TRIX_FORMAT_IPV6:
            return trix_validate_ipv6(str);
        default:
            return false;
    }
}

/*═══════════════════════════════════════════════════════════════════════════
 * Path Validation (Security Critical)
 *═══════════════════════════════════════════════════════════════════════════*/

bool trix_validate_path(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    
    /* Check for directory traversal attempts */
    if (strstr(path, "..") != NULL) {
        return false;
    }
    
    /* Check for absolute paths (may be disallowed in some contexts) */
    /* For now we allow both relative and absolute */
    
    /* Check for dangerous characters */
    for (const char* p = path; *p; p++) {
        /* Allow alphanumeric, /, -, _, . */
        if (!isalnum(*p) && *p != '/' && *p != '-' && *p != '_' && *p != '.') {
            return false;
        }
    }
    
    return true;
}

bool trix_validate_path_in_dir(const char* path, const char* allowed_dir) {
    if (path == NULL || allowed_dir == NULL) {
        return false;
    }
    
    /* First validate the path itself */
    if (!trix_validate_path(path)) {
        return false;
    }
    
    /* Check if path starts with allowed_dir */
    size_t dir_len = strlen(allowed_dir);
    if (strncmp(path, allowed_dir, dir_len) != 0) {
        return false;
    }
    
    /* Make sure there's a / after the directory name (or end of string) */
    if (path[dir_len] != '\0' && path[dir_len] != '/') {
        return false;
    }
    
    return true;
}

bool trix_canonicalize_path(const char* path, char* canonical, size_t size) {
    if (path == NULL || canonical == NULL || size == 0) {
        return false;
    }
    
    /* Simple canonicalization - resolve ./ and ../ */
    /* This is a simplified version - production code should use realpath() */
    
    char temp[PATH_MAX];
    if (strlen(path) >= sizeof(temp)) {
        return false;
    }
    
    strcpy(temp, path);
    
    /* Remove ./ */
    char* p;
    while ((p = strstr(temp, "./")) != NULL) {
        if (p == temp || p[-1] == '/') {
            memmove(p, p + 2, strlen(p + 2) + 1);
        } else {
            p++;
        }
    }
    
    /* Check for ../ (should have been caught by trix_validate_path) */
    if (strstr(temp, "../") != NULL) {
        return false;
    }
    
    /* Copy to output */
    if (strlen(temp) >= size) {
        return false;
    }
    
    strcpy(canonical, temp);
    return true;
}

/*═══════════════════════════════════════════════════════════════════════════
 * Sanitization
 *═══════════════════════════════════════════════════════════════════════════*/

int trix_sanitize_string(const char* input, char* output, size_t output_size,
                         uint32_t modes) {
    if (input == NULL || output == NULL || output_size == 0) {
        return -1;
    }
    
    char temp[4096];
    const char* src = input;
    char* dst = temp;
    size_t remaining = sizeof(temp) - 1;
    
    /* Trim leading whitespace if requested */
    if (modes & TRIX_SANITIZE_TRIM) {
        while (isspace(*src)) {
            src++;
        }
    }
    
    /* Process each character */
    while (*src && remaining > 0) {
        char c = *src;
        bool keep = true;
        
        /* Apply filters */
        if (modes & TRIX_SANITIZE_ALPHANUMERIC) {
            keep = isalnum(c) || isspace(c);
        } else if (modes & TRIX_SANITIZE_PRINTABLE) {
            keep = isprint(c);
        }
        
        if (keep) {
            /* Apply transformations */
            if (modes & TRIX_SANITIZE_LOWERCASE) {
                c = tolower(c);
            } else if (modes & TRIX_SANITIZE_UPPERCASE) {
                c = toupper(c);
            }
            
            *dst++ = c;
            remaining--;
        }
        
        src++;
    }
    
    /* Trim trailing whitespace if requested */
    if (modes & TRIX_SANITIZE_TRIM) {
        while (dst > temp && isspace(dst[-1])) {
            dst--;
        }
    }
    
    *dst = '\0';
    
    /* Copy to output */
    size_t len = dst - temp;
    if (len >= output_size) {
        return -1;  /* Output buffer too small */
    }
    
    strcpy(output, temp);
    return (int)len;
}

int trix_escape_sql(const char* input, char* output, size_t output_size) {
    if (input == NULL || output == NULL || output_size < 2) {
        return -1;
    }
    
    const char* src = input;
    char* dst = output;
    size_t remaining = output_size - 1;
    
    while (*src && remaining > 1) {
        /* Escape single quotes by doubling them */
        if (*src == '\'') {
            if (remaining < 2) {
                return -1;  /* Not enough space */
            }
            *dst++ = '\'';
            *dst++ = '\'';
            remaining -= 2;
        } else {
            *dst++ = *src;
            remaining--;
        }
        src++;
    }
    
    *dst = '\0';
    return (int)(dst - output);
}

int trix_escape_html(const char* input, char* output, size_t output_size) {
    if (input == NULL || output == NULL || output_size == 0) {
        return -1;
    }
    
    const char* src = input;
    char* dst = output;
    size_t remaining = output_size - 1;
    
    while (*src && remaining > 0) {
        const char* entity = NULL;
        size_t entity_len = 0;
        
        switch (*src) {
            case '<':  entity = "&lt;";   entity_len = 4; break;
            case '>':  entity = "&gt;";   entity_len = 4; break;
            case '&':  entity = "&amp;";  entity_len = 5; break;
            case '"':  entity = "&quot;"; entity_len = 6; break;
            case '\'': entity = "&#39;";  entity_len = 5; break;
            default:   break;
        }
        
        if (entity) {
            if (remaining < entity_len) {
                return -1;
            }
            memcpy(dst, entity, entity_len);
            dst += entity_len;
            remaining -= entity_len;
        } else {
            *dst++ = *src;
            remaining--;
        }
        
        src++;
    }
    
    *dst = '\0';
    return (int)(dst - output);
}

int trix_url_encode(const char* input, char* output, size_t output_size) {
    if (input == NULL || output == NULL || output_size < 4) {
        return -1;
    }
    
    const char* src = input;
    char* dst = output;
    size_t remaining = output_size - 1;
    
    while (*src && remaining > 0) {
        unsigned char c = (unsigned char)*src;
        
        /* Encode non-alphanumeric characters except - _ . ~ */
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            *dst++ = c;
            remaining--;
        } else {
            if (remaining < 3) {
                return -1;
            }
            snprintf(dst, 4, "%%%02X", c);
            dst += 3;
            remaining -= 3;
        }
        
        src++;
    }
    
    *dst = '\0';
    return (int)(dst - output);
}

int trix_escape_shell(const char* input, char* output, size_t output_size) {
    if (input == NULL || output == NULL || output_size < 2) {
        return -1;
    }
    
    const char* src = input;
    char* dst = output;
    size_t remaining = output_size - 1;
    
    /* Shell metacharacters that need escaping */
    const char* metacharacters = "|&;<>()$`\\\"' \t\n*?[]#~=%";
    
    while (*src && remaining > 1) {
        if (strchr(metacharacters, *src) != NULL) {
            if (remaining < 2) {
                return -1;
            }
            *dst++ = '\\';
            remaining--;
        }
        *dst++ = *src++;
        remaining--;
    }
    
    *dst = '\0';
    return (int)(dst - output);
}

/*═══════════════════════════════════════════════════════════════════════════
 * Whitelist/Blacklist
 *═══════════════════════════════════════════════════════════════════════════*/

bool trix_in_whitelist(const char* value, const char** whitelist) {
    if (value == NULL || whitelist == NULL) {
        return false;
    }
    
    for (const char** p = whitelist; *p != NULL; p++) {
        if (strcmp(value, *p) == 0) {
            return true;
        }
    }
    
    return false;
}

bool trix_not_in_blacklist(const char* value, const char** blacklist) {
    if (value == NULL) {
        return false;
    }
    
    if (blacklist == NULL) {
        return true;  /* No blacklist means everything is allowed */
    }
    
    for (const char** p = blacklist; *p != NULL; p++) {
        if (strcmp(value, *p) == 0) {
            return false;  /* Found in blacklist */
        }
    }
    
    return true;
}

bool trix_validate_enum(int value, int min, int max) {
    return value >= min && value < max;
}

/*═══════════════════════════════════════════════════════════════════════════
 * Schema Validation
 *═══════════════════════════════════════════════════════════════════════════*/

void trix_schema_init(TrixSchema* schema) {
    if (schema == NULL) {
        return;
    }
    
    memset(schema, 0, sizeof(TrixSchema));
    schema->allow_extra_fields = false;
}

bool trix_schema_add_field(TrixSchema* schema, const TrixSchemaField* field) {
    if (schema == NULL || field == NULL) {
        return false;
    }
    
    if (schema->num_fields >= TRIX_MAX_SCHEMA_FIELDS) {
        return false;  /* Schema full */
    }
    
    schema->fields[schema->num_fields] = *field;
    schema->num_fields++;
    
    return true;
}

TrixValidationResult trix_validate_schema(const char** keys, const void** values,
                                          size_t num_pairs, const TrixSchema* schema) {
    TrixValidationResult result = {
        .valid = true,
        .error_code = TRIX_OK,
        .error_field = NULL,
        .error_msg = {0}
    };
    
    if (keys == NULL || values == NULL || schema == NULL) {
        result.valid = false;
        result.error_code = TRIX_ERROR_NULL_POINTER;
        snprintf(result.error_msg, sizeof(result.error_msg), "NULL parameter");
        return result;
    }
    
    /* Check each schema field */
    for (size_t i = 0; i < schema->num_fields; i++) {
        const TrixSchemaField* field = &schema->fields[i];
        
        /* Find this field in the input */
        bool found = false;
        const void* value = NULL;
        
        for (size_t j = 0; j < num_pairs; j++) {
            if (strcmp(keys[j], field->name) == 0) {
                found = true;
                value = values[j];
                break;
            }
        }
        
        /* Check if required field is missing */
        if (!found && field->required) {
            result.valid = false;
            result.error_code = TRIX_ERROR_MISSING_FIELD;
            result.error_field = field->name;
            snprintf(result.error_msg, sizeof(result.error_msg),
                    "Required field '%s' is missing", field->name);
            return result;
        }
        
        /* Validate field if present */
        if (found) {
            /* Type-specific validation would go here */
            /* For this basic implementation, we just check it exists */
            if (value == NULL) {
                result.valid = false;
                result.error_code = TRIX_ERROR_NULL_POINTER;
                result.error_field = field->name;
                snprintf(result.error_msg, sizeof(result.error_msg),
                        "Field '%s' has NULL value", field->name);
                return result;
            }
        }
    }
    
    return result;
}
