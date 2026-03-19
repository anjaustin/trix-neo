/*
 * test_validation.c — Unit tests for input validation
 *
 * Tests CRITICAL ITEM 4: Input Validation
 */

#include "../include/trixc/validation.h"
#include <stdio.h>
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
 * Basic Type Validation Tests
 *═══════════════════════════════════════════════════════════════════════════*/

void test_validate_int() {
    TEST("int validation");
    
    ASSERT(trix_validate_int(50, 0, 100), "Valid int should pass");
    ASSERT(trix_validate_int(0, 0, 100), "Lower bound should pass");
    ASSERT(trix_validate_int(100, 0, 100), "Upper bound should pass");
    ASSERT(!trix_validate_int(-1, 0, 100), "Below range should fail");
    ASSERT(!trix_validate_int(101, 0, 100), "Above range should fail");
    
    PASS();
}

void test_validate_uint() {
    TEST("unsigned int validation");
    
    ASSERT(trix_validate_uint(50, 0, 100), "Valid uint should pass");
    ASSERT(trix_validate_uint(0, 0, 100), "Lower bound should pass");
    ASSERT(trix_validate_uint(100, 0, 100), "Upper bound should pass");
    ASSERT(!trix_validate_uint(101, 0, 100), "Above range should fail");
    
    PASS();
}

void test_validate_float() {
    TEST("float validation");
    
    ASSERT(trix_validate_float(50.5, 0.0, 100.0), "Valid float should pass");
    ASSERT(trix_validate_float(0.0, 0.0, 100.0), "Lower bound should pass");
    ASSERT(trix_validate_float(100.0, 0.0, 100.0), "Upper bound should pass");
    ASSERT(!trix_validate_float(-0.1, 0.0, 100.0), "Below range should fail");
    ASSERT(!trix_validate_float(100.1, 0.0, 100.0), "Above range should fail");
    
    /* NaN and infinity checks */
    ASSERT(!trix_validate_float(0.0/0.0, 0.0, 100.0), "NaN should fail");
    ASSERT(!trix_validate_float(1.0/0.0, 0.0, 100.0), "Infinity should fail");
    
    PASS();
}

void test_validate_string_length() {
    TEST("string length validation");
    
    ASSERT(trix_validate_string_length("hello", 1, 10), "Valid string should pass");
    ASSERT(trix_validate_string_length("x", 1, 10), "Min length should pass");
    ASSERT(trix_validate_string_length("0123456789", 1, 10), "Max length should pass");
    ASSERT(!trix_validate_string_length("", 1, 10), "Too short should fail");
    ASSERT(!trix_validate_string_length("01234567890", 1, 10), "Too long should fail");
    ASSERT(!trix_validate_string_length(NULL, 1, 10), "NULL should fail");
    
    PASS();
}

void test_validate_buffer() {
    TEST("buffer validation");
    
    char buf[100];
    ASSERT(trix_validate_buffer(buf, 50, 10, 100), "Valid buffer should pass");
    ASSERT(trix_validate_buffer(buf, 10, 10, 100), "Min size should pass");
    ASSERT(trix_validate_buffer(buf, 100, 10, 100), "Max size should pass");
    ASSERT(!trix_validate_buffer(buf, 9, 10, 100), "Too small should fail");
    ASSERT(!trix_validate_buffer(buf, 101, 10, 100), "Too large should fail");
    ASSERT(!trix_validate_buffer(NULL, 50, 10, 100), "NULL should fail");
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Format Validation Tests
 *═══════════════════════════════════════════════════════════════════════════*/

void test_validate_email() {
    TEST("email validation");
    
    ASSERT(trix_validate_email("user@example.com"), "Valid email should pass");
    ASSERT(trix_validate_email("test.user+tag@sub.example.co.uk"), "Complex email should pass");
    ASSERT(!trix_validate_email("notanemail"), "No @ should fail");
    ASSERT(!trix_validate_email("@example.com"), "No user should fail");
    ASSERT(!trix_validate_email("user@"), "No domain should fail");
    ASSERT(!trix_validate_email("user@domain"), "No TLD should fail");
    ASSERT(!trix_validate_email(NULL), "NULL should fail");
    
    PASS();
}

void test_validate_url() {
    TEST("URL validation");
    
    ASSERT(trix_validate_url("http://example.com"), "HTTP URL should pass");
    ASSERT(trix_validate_url("https://example.com"), "HTTPS URL should pass");
    ASSERT(trix_validate_url("https://example.com/path/to/page"), "URL with path should pass");
    ASSERT(!trix_validate_url("ftp://example.com"), "FTP URL should fail");
    ASSERT(!trix_validate_url("example.com"), "No protocol should fail");
    ASSERT(!trix_validate_url("http://"), "No domain should fail");
    ASSERT(!trix_validate_url(NULL), "NULL should fail");
    
    PASS();
}

void test_validate_hex() {
    TEST("hex string validation");
    
    ASSERT(trix_validate_hex("deadbeef", 4), "Valid hex should pass");
    ASSERT(trix_validate_hex("0xDEADBEEF", 4), "Hex with 0x prefix should pass");
    ASSERT(trix_validate_hex("ABCDEF", 3), "Uppercase hex should pass");
    ASSERT(!trix_validate_hex("deadbee", 4), "Odd length should fail");
    ASSERT(!trix_validate_hex("xyz", 0), "Non-hex chars should fail");
    ASSERT(!trix_validate_hex(NULL, 0), "NULL should fail");
    
    PASS();
}

void test_validate_base64() {
    TEST("base64 validation");
    
    ASSERT(trix_validate_base64("SGVsbG8="), "Valid base64 should pass");
    ASSERT(trix_validate_base64("YWJjZA=="), "Base64 with padding should pass");
    ASSERT(!trix_validate_base64("abc"), "Wrong length should fail");
    ASSERT(!trix_validate_base64("abc!"), "Invalid char should fail");
    ASSERT(!trix_validate_base64(NULL), "NULL should fail");
    
    PASS();
}

void test_validate_uuid() {
    TEST("UUID validation");
    
    ASSERT(trix_validate_uuid("550e8400-e29b-41d4-a716-446655440000"), "Valid UUID should pass");
    ASSERT(!trix_validate_uuid("550e8400-e29b-41d4-a716"), "Too short should fail");
    ASSERT(!trix_validate_uuid("550e8400_e29b_41d4_a716_446655440000"), "Wrong separator should fail");
    ASSERT(!trix_validate_uuid("550e8400-e29b-41d4-a716-44665544000g"), "Non-hex char should fail");
    ASSERT(!trix_validate_uuid(NULL), "NULL should fail");
    
    PASS();
}

void test_validate_ipv4() {
    TEST("IPv4 validation");
    
    ASSERT(trix_validate_ipv4("192.168.1.1"), "Valid IP should pass");
    ASSERT(trix_validate_ipv4("0.0.0.0"), "All zeros should pass");
    ASSERT(trix_validate_ipv4("255.255.255.255"), "All 255s should pass");
    ASSERT(!trix_validate_ipv4("256.1.1.1"), "Out of range octet should fail");
    ASSERT(!trix_validate_ipv4("192.168.1"), "Too few octets should fail");
    ASSERT(!trix_validate_ipv4("192.168.1.1.1"), "Too many octets should fail");
    ASSERT(!trix_validate_ipv4(NULL), "NULL should fail");
    
    PASS();
}

void test_validate_ipv6() {
    TEST("IPv6 validation");
    
    ASSERT(trix_validate_ipv6("2001:0db8:85a3:0000:0000:8a2e:0370:7334"), "Valid IPv6 should pass");
    ASSERT(trix_validate_ipv6("2001:db8::1"), "IPv6 with :: should pass");
    ASSERT(trix_validate_ipv6("::1"), "Loopback should pass");
    ASSERT(!trix_validate_ipv6("gggg::1"), "Invalid hex should fail");
    ASSERT(!trix_validate_ipv6(NULL), "NULL should fail");
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Path Validation Tests
 *═══════════════════════════════════════════════════════════════════════════*/

void test_validate_path() {
    TEST("path validation");
    
    ASSERT(trix_validate_path("file.txt"), "Simple filename should pass");
    ASSERT(trix_validate_path("dir/file.txt"), "Path with dir should pass");
    ASSERT(trix_validate_path("a/b/c/file.txt"), "Nested path should pass");
    ASSERT(!trix_validate_path("../etc/passwd"), "Directory traversal should fail");
    ASSERT(!trix_validate_path("file; rm -rf /"), "Shell injection should fail");
    ASSERT(!trix_validate_path(NULL), "NULL should fail");
    
    PASS();
}

void test_validate_path_in_dir() {
    TEST("path within directory validation");
    
    ASSERT(trix_validate_path_in_dir("/var/www/file.txt", "/var/www"), "Path in dir should pass");
    ASSERT(trix_validate_path_in_dir("/var/www/", "/var/www"), "Exact dir should pass");
    ASSERT(!trix_validate_path_in_dir("/etc/passwd", "/var/www"), "Path outside dir should fail");
    ASSERT(!trix_validate_path_in_dir("../etc/passwd", "/var/www"), "Traversal should fail");
    ASSERT(!trix_validate_path_in_dir(NULL, "/var/www"), "NULL path should fail");
    ASSERT(!trix_validate_path_in_dir("/var/www/file.txt", NULL), "NULL dir should fail");
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Sanitization Tests
 *═══════════════════════════════════════════════════════════════════════════*/

void test_sanitize_alphanumeric() {
    TEST("alphanumeric sanitization");
    
    char output[100];
    int len = trix_sanitize_string("hello123", output, sizeof(output), TRIX_SANITIZE_ALPHANUMERIC);
    ASSERT(len > 0, "Should succeed");
    ASSERT(strcmp(output, "hello123") == 0, "Alphanumeric should pass through");
    
    len = trix_sanitize_string("hello@#$123", output, sizeof(output), TRIX_SANITIZE_ALPHANUMERIC);
    ASSERT(len > 0, "Should succeed");
    ASSERT(strcmp(output, "hello123") == 0, "Special chars should be removed");
    
    PASS();
}

void test_sanitize_trim() {
    TEST("whitespace trimming");
    
    char output[100];
    int len = trix_sanitize_string("  hello  ", output, sizeof(output), 
                                    TRIX_SANITIZE_PRINTABLE | TRIX_SANITIZE_TRIM);
    ASSERT(len > 0, "Should succeed");
    ASSERT(strcmp(output, "hello") == 0, "Whitespace should be trimmed");
    
    PASS();
}

void test_sanitize_case() {
    TEST("case conversion");
    
    char output[100];
    int len = trix_sanitize_string("Hello", output, sizeof(output),
                                    TRIX_SANITIZE_PRINTABLE | TRIX_SANITIZE_LOWERCASE);
    ASSERT(len > 0, "Should succeed");
    ASSERT(strcmp(output, "hello") == 0, "Should be lowercase");
    
    len = trix_sanitize_string("hello", output, sizeof(output),
                              TRIX_SANITIZE_PRINTABLE | TRIX_SANITIZE_UPPERCASE);
    ASSERT(len > 0, "Should succeed");
    ASSERT(strcmp(output, "HELLO") == 0, "Should be uppercase");
    
    PASS();
}

void test_escape_html() {
    TEST("HTML escaping");
    
    char output[100];
    int len = trix_escape_html("<script>alert('xss')</script>", output, sizeof(output));
    ASSERT(len > 0, "Should succeed");
    ASSERT(strstr(output, "&lt;") != NULL, "< should be escaped");
    ASSERT(strstr(output, "&gt;") != NULL, "> should be escaped");
    ASSERT(strstr(output, "&#39;") != NULL, "' should be escaped");
    
    PASS();
}

void test_escape_sql() {
    TEST("SQL escaping");
    
    char output[100];
    int len = trix_escape_sql("O'Reilly", output, sizeof(output));
    ASSERT(len > 0, "Should succeed");
    ASSERT(strcmp(output, "O''Reilly") == 0, "Single quote should be doubled");
    
    PASS();
}

void test_url_encode() {
    TEST("URL encoding");
    
    char output[100];
    int len = trix_url_encode("hello world!", output, sizeof(output));
    ASSERT(len > 0, "Should succeed");
    ASSERT(strstr(output, "%20") != NULL, "Space should be encoded");
    ASSERT(strstr(output, "%21") != NULL, "! should be encoded");
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Whitelist/Blacklist Tests
 *═══════════════════════════════════════════════════════════════════════════*/

void test_whitelist() {
    TEST("whitelist validation");
    
    const char* whitelist[] = {"GET", "POST", "PUT", "DELETE", NULL};
    
    ASSERT(trix_in_whitelist("GET", whitelist), "GET should be in whitelist");
    ASSERT(trix_in_whitelist("POST", whitelist), "POST should be in whitelist");
    ASSERT(!trix_in_whitelist("PATCH", whitelist), "PATCH should not be in whitelist");
    ASSERT(!trix_in_whitelist(NULL, whitelist), "NULL should not be in whitelist");
    
    PASS();
}

void test_blacklist() {
    TEST("blacklist validation");
    
    const char* blacklist[] = {"DROP", "DELETE", "TRUNCATE", NULL};
    
    ASSERT(trix_not_in_blacklist("SELECT", blacklist), "SELECT should not be in blacklist");
    ASSERT(!trix_not_in_blacklist("DROP", blacklist), "DROP should be in blacklist");
    ASSERT(!trix_not_in_blacklist("DELETE", blacklist), "DELETE should be in blacklist");
    ASSERT(!trix_not_in_blacklist(NULL, blacklist), "NULL should always fail");
    
    PASS();
}

void test_validate_enum() {
    TEST("enum validation");
    
    ASSERT(trix_validate_enum(0, 0, 5), "First value should pass");
    ASSERT(trix_validate_enum(4, 0, 5), "Last value should pass");
    ASSERT(!trix_validate_enum(5, 0, 5), "Equal to max should fail");
    ASSERT(!trix_validate_enum(-1, 0, 5), "Below range should fail");
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Schema Validation Tests
 *═══════════════════════════════════════════════════════════════════════════*/

void test_schema_basic() {
    TEST("basic schema validation");
    
    TrixSchema schema;
    trix_schema_init(&schema);
    
    TrixSchemaField name_field = {
        .name = "name",
        .type = TRIX_TYPE_STRING,
        .required = true,
        .min_length = 1,
        .max_length = 100
    };
    
    TrixSchemaField age_field = {
        .name = "age",
        .type = TRIX_TYPE_INT,
        .required = false,
        .int_range = {.min = 0, .max = 150}
    };
    
    ASSERT(trix_schema_add_field(&schema, &name_field), "Should add name field");
    ASSERT(trix_schema_add_field(&schema, &age_field), "Should add age field");
    ASSERT(schema.num_fields == 2, "Should have 2 fields");
    
    /* Valid data */
    const char* keys[] = {"name", "age"};
    const void* values[] = {"John", (void*)42};
    TrixValidationResult result = trix_validate_schema(keys, values, 2, &schema);
    ASSERT(result.valid, "Valid data should pass");
    
    /* Missing required field */
    const char* keys2[] = {"age"};
    const void* values2[] = {(void*)42};
    result = trix_validate_schema(keys2, values2, 1, &schema);
    ASSERT(!result.valid, "Missing required field should fail");
    ASSERT(result.error_code == TRIX_ERROR_MISSING_FIELD, "Should have correct error code");
    
    PASS();
}

/*═══════════════════════════════════════════════════════════════════════════
 * Main Test Runner
 *═══════════════════════════════════════════════════════════════════════════*/

int main(void) {
    printf("\n");
    printf("╭────────────────────────────────────────────────────────────╮\n");
    printf("│  TriX Input Validation Unit Tests                         │\n");
    printf("│  CRITICAL 4: Input Validation                             │\n");
    printf("╰────────────────────────────────────────────────────────────╯\n");
    printf("\n");
    
    /* Basic type validation */
    test_validate_int();
    test_validate_uint();
    test_validate_float();
    test_validate_string_length();
    test_validate_buffer();
    
    /* Format validation */
    test_validate_email();
    test_validate_url();
    test_validate_hex();
    test_validate_base64();
    test_validate_uuid();
    test_validate_ipv4();
    test_validate_ipv6();
    
    /* Path validation */
    test_validate_path();
    test_validate_path_in_dir();
    
    /* Sanitization */
    test_sanitize_alphanumeric();
    test_sanitize_trim();
    test_sanitize_case();
    test_escape_html();
    test_escape_sql();
    test_url_encode();
    
    /* Whitelist/Blacklist */
    test_whitelist();
    test_blacklist();
    test_validate_enum();
    
    /* Schema */
    test_schema_basic();
    
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
