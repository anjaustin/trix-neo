# TriX Critical Items - Immediate Action Required
**What Must Be Fixed Before Production Deployment**

Date: March 19, 2026  
Priority: CRITICAL

---

## Executive Summary

**TriX has 8 critical gaps that must be fixed before any production deployment.**

These are **not nice-to-haves**. These are **blockers** for:
- Safety-critical deployment (medical, automotive)
- FDA/ISO certification
- Commercial customers
- Production reliability

**Timeline:** 4 weeks to fix all critical items (1-2 engineers full-time)

---

## The 8 Critical Items

### ❌ CRITICAL 1: Error Handling
**Current State:** Almost none  
**Risk:** Silent failures, crashes, data corruption  
**Impact:** Cannot deploy to production

**Problem:**
```c
// Current code:
FILE* f = fopen(filename, "r");
fread(data, 1, size, f);  // CRASHES if fopen() fails!

SoftChipSpec spec;
softchip_parse(file, &spec);  // Returns 0 or 1, but no error details
```

**What happens in production:**
- File not found → Crash (no error message)
- Malformed YAML → Undefined behavior
- Out of memory → Silent failure or crash
- Invalid signatures → Processes garbage data

**Required Fix:**
```c
// Production code:
trix_error_t error;
FILE* f = fopen(filename, "r");
if (!f) {
    log_error("Cannot open file '%s': %s", filename, strerror(errno));
    return TRIX_ERROR_FILE_NOT_FOUND;
}

SoftChipSpec spec;
trix_error_t result = softchip_parse_safe(file, &spec, &error);
if (result != TRIX_OK) {
    log_error("Parse failed at line %d, col %d: %s", 
              error.line, error.column, error.message);
    return result;
}
```

**Deliverables:**
1. `zor/include/trixc/errors.h` - Error code enum (30+ codes)
2. Error context struct (line, column, message)
3. Update all functions to return error codes
4. Test error paths (invalid files, OOM, etc.)

**Effort:** 3-4 days  
**Files to modify:** ~10 files (all parsers, all I/O)

---

### ❌ CRITICAL 2: Logging System
**Current State:** printf() debugging  
**Risk:** No visibility into failures, can't debug production issues  
**Impact:** Cannot diagnose customer problems

**Problem:**
```c
// Current code:
printf("Generating code:\n");  // Always printed, can't disable
printf("Error: %s\n", msg);    // No context, no timestamps, no filtering
```

**What happens in production:**
- Can't disable debug output (performance impact)
- Can't log to file (only stdout)
- No log levels (ERROR vs WARN vs INFO)
- No structured logging (can't parse logs)
- No correlation IDs (can't track request flow)

**Required Fix:**
```c
// Production code:
#include <trixc/logging.h>

// Configure logging
trix_log_config_t config = {
    .level = TRIX_LOG_INFO,           // Filter by level
    .output = TRIX_LOG_TO_FILE,       // Log to file
    .file_path = "/var/log/trix.log",
    .format = TRIX_LOG_FORMAT_JSON,   // Structured logs
};
trix_log_init(&config);

// Use it
log_error("Parse failed: %s (line %d)", error.message, error.line);
log_warn("Using default threshold: %d", DEFAULT_THRESHOLD);
log_info("Forge complete: %s", target_name);
log_debug("Signature %d: distance=%d, threshold=%d", i, dist, thresh);
```

**Deliverables:**
1. `zor/include/trixc/logging.h` - Logging API
2. Log levels (ERROR, WARN, INFO, DEBUG, TRACE)
3. Multiple outputs (stdout, stderr, file, callback)
4. Structured logging (JSON format option)
5. Thread-safe logging (for multi-threaded apps)

**Effort:** 2-3 days  
**Files to modify:** All files (replace printf with log_*)

---

### ❌ CRITICAL 3: Memory Safety
**Current State:** Manual malloc/free, no leak checking  
**Risk:** Memory leaks, buffer overflows, use-after-free  
**Impact:** Crashes, security vulnerabilities

**Problem:**
```c
// Current code:
char* buffer = malloc(1024);
// ... lots of code ...
if (error) return -1;  // LEAK! Forgot to free(buffer)

char name[64];
strcpy(name, user_input);  // OVERFLOW if input > 64 bytes
```

**What happens in production:**
- Memory leaks → Process crashes after hours/days
- Buffer overflows → Security vulnerabilities (RCE)
- Use-after-free → Crashes, data corruption
- Double-free → Crashes

**Required Fix:**
```c
// Production code:
char* buffer = NULL;
int result = 0;

buffer = malloc(1024);
if (!buffer) {
    result = TRIX_ERROR_OUT_OF_MEMORY;
    goto cleanup;
}

// ... lots of code ...
if (error) {
    result = error;
    goto cleanup;  // Always cleanup
}

cleanup:
    free(buffer);  // Always executed
    return result;

// Bounds-checked copy:
char name[64];
if (strlen(user_input) >= sizeof(name)) {
    return TRIX_ERROR_NAME_TOO_LONG;
}
strncpy(name, user_input, sizeof(name) - 1);
name[sizeof(name) - 1] = '\0';  // Ensure null termination
```

**Deliverables:**
1. Audit all malloc/free (use grep, find leaks)
2. Add goto cleanup pattern everywhere
3. Add bounds checking for all arrays
4. Add NULL checks before pointer dereference
5. Run valgrind on all tests (must be clean)
6. Run AddressSanitizer (asan) in CI

**Effort:** 5-6 days  
**Files to modify:** All files with malloc/free (~15 files)

---

### ❌ CRITICAL 4: Input Validation
**Current State:** Trusts all input  
**Risk:** Security vulnerabilities, crashes, data corruption  
**Impact:** Cannot accept untrusted input (customer files)

**Problem:**
```c
// Current code:
softchip_parse("user.trix", &spec);  // What if file is malicious?

// In parser:
spec.num_signatures = yaml_get_int("num_signatures");  // No bounds check
strcpy(spec.name, yaml_get_string("name"));            // No length check
int threshold = atoi(yaml_get_string("threshold"));    // No validation
```

**What happens in production:**
- Malicious .trix file → Crash, RCE, data corruption
- num_signatures = 1000000 → Out of memory
- name = "A" * 1000 → Buffer overflow
- threshold = -1 → Logic errors
- pattern = "invalid hex" → Undefined behavior

**Required Fix:**
```c
// Production code:

// 1. Schema validation
trix_error_t validate_spec(SoftChipSpec* spec) {
    // Check bounds
    if (spec->num_signatures > MAX_SIGNATURES) {
        return TRIX_ERROR_TOO_MANY_SIGNATURES;
    }
    
    // Check string lengths
    if (strlen(spec->name) >= MAX_NAME_LEN) {
        return TRIX_ERROR_NAME_TOO_LONG;
    }
    
    // Check value ranges
    for (int i = 0; i < spec->num_signatures; i++) {
        if (spec->signatures[i].threshold < 0 || 
            spec->signatures[i].threshold > 512) {
            return TRIX_ERROR_INVALID_THRESHOLD;
        }
    }
    
    // Validate hex patterns
    if (!is_valid_hex(spec->signatures[i].pattern)) {
        return TRIX_ERROR_INVALID_PATTERN;
    }
    
    return TRIX_OK;
}

// 2. Sanitize inputs
trix_error_t sanitize_filename(const char* input, char* output, size_t len) {
    // Remove path traversal
    if (strstr(input, "..") != NULL) {
        return TRIX_ERROR_INVALID_PATH;
    }
    
    // Check length
    if (strlen(input) >= len) {
        return TRIX_ERROR_PATH_TOO_LONG;
    }
    
    strncpy(output, input, len - 1);
    output[len - 1] = '\0';
    return TRIX_OK;
}
```

**Deliverables:**
1. JSON Schema for .trix format
2. Schema validation in parser
3. Bounds checking (array sizes, string lengths)
4. Value range checking (thresholds, dimensions)
5. Sanitization (file paths, hex strings)
6. Fuzz testing (AFL, libFuzzer)

**Effort:** 4-5 days  
**Files to modify:** softchip.c, codegen.c, all parsers

---

### ❌ CRITICAL 5: Thread Safety
**Current State:** Not thread-safe  
**Risk:** Data races, crashes in multi-threaded apps  
**Impact:** Cannot use in server applications

**Problem:**
```c
// Current code:
static char error_buffer[256];  // GLOBAL STATE!

const char* get_error_message(void) {
    return error_buffer;  // Multiple threads will corrupt this
}

// In generated code:
static float state[64];  // SHARED STATE!
float infer(float* input) {
    // Multiple threads calling this = data race
    state[0] = input[0] ^ state[0];
    // ...
}
```

**What happens in production:**
- Web server with 100 threads → Data races, crashes
- Batch processing → Corrupted results
- State corruption → Wrong outputs

**Required Fix:**
```c
// Production code:

// 1. Thread-local error storage
_Thread_local char error_buffer[256];

// 2. Pass state explicitly (no globals)
typedef struct {
    uint8_t resonance[64];
    // ... other state
} trix_state_t;

trix_error_t trix_infer(trix_chip_t* chip, 
                        trix_state_t* state,  // Per-thread
                        const float* input, 
                        float* output) {
    // Each thread has its own state
    state->resonance[0] ^= input[0];
    // ...
}

// 3. Lock shared resources
typedef struct {
    pthread_mutex_t lock;
    // ... shared data
} trix_chip_t;

void trix_chip_register_callback(trix_chip_t* chip, callback_t cb) {
    pthread_mutex_lock(&chip->lock);
    chip->callback = cb;
    pthread_mutex_unlock(&chip->lock);
}
```

**Deliverables:**
1. Remove all global/static mutable state
2. Pass state explicitly everywhere
3. Add locks for shared resources
4. Document thread-safety guarantees
5. Test with ThreadSanitizer (tsan)

**Effort:** 3-4 days  
**Files to modify:** All generated code, API layer

---

### ❌ CRITICAL 6: API Stability
**Current State:** No public API  
**Risk:** Cannot integrate, API churn breaks users  
**Impact:** Cannot ship libraries to customers

**Problem:**
```c
// Current: No API, users must copy code
// - No shared library (.so, .dylib, .dll)
// - No header-only option
// - No versioning
// - No stability guarantees
// - Breaking changes anytime
```

**What happens in production:**
- Customers copy code → Can't update (security patches)
- No versioning → Breaking changes break deployments
- No API docs → Customers can't integrate
- No stability → Customers lose trust

**Required Fix:**
```c
// Production code:

// 1. Public API header
// trixc.h
#ifndef TRIXC_H
#define TRIXC_H

// Version
#define TRIX_VERSION_MAJOR 1
#define TRIX_VERSION_MINOR 0
#define TRIX_VERSION_PATCH 0

// API version (increments on breaking changes)
#define TRIX_API_VERSION 1

// Opaque types (ABI stability)
typedef struct trix_chip trix_chip_t;
typedef struct trix_state trix_state_t;

// Public API (stable)
TRIX_API trix_error_t trix_load_chip(const char* file, trix_chip_t** chip);
TRIX_API void trix_free_chip(trix_chip_t* chip);
TRIX_API trix_error_t trix_infer(trix_chip_t* chip, 
                                  trix_state_t* state,
                                  const float* input, 
                                  float* output);

#endif // TRIXC_H

// 2. Stability guarantees
// - API v1 stable until TRIX 2.0
// - Semantic versioning (MAJOR.MINOR.PATCH)
// - Deprecation period (1 year minimum)
// - Changelog for all changes
```

**Deliverables:**
1. Define public API (trixc.h)
2. Hide internal details (opaque types)
3. Version everything (semantic versioning)
4. Write API stability policy
5. Build shared library (libtrix.so)
6. Doxygen docs for API

**Effort:** 3-4 days  
**Files to create:** trixc.h, libtrix build, docs

---

### ❌ CRITICAL 7: Build System
**Current State:** Simple Makefile  
**Risk:** Doesn't build on all platforms, no configuration  
**Impact:** Cannot distribute binaries

**Problem:**
```makefile
# Current Makefile:
CC = clang                 # Hardcoded (no gcc option)
CFLAGS = -O2 -Wall         # No customization
CFLAGS_NEON = -mcpu=apple-m4  # Apple-specific (breaks Linux)

# No Windows support
# No cross-compilation
# No install target
# No package generation
```

**What happens in production:**
- Doesn't build on Windows
- Doesn't build with gcc
- Can't cross-compile for ARM
- Can't install to /usr/local
- Can't generate .deb or .rpm packages

**Required Fix:**
```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(TriX VERSION 1.0.0 LANGUAGES C)

# Options
option(TRIX_BUILD_SHARED "Build shared library" ON)
option(TRIX_BUILD_TESTS "Build tests" ON)
option(TRIX_ENABLE_NEON "Enable ARM NEON" OFF)
option(TRIX_ENABLE_AVX2 "Enable x86 AVX2" OFF)

# Auto-detect CPU features
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(TRIX_ENABLE_NEON ON)
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(TRIX_ENABLE_AVX2 ON)
endif()

# Build library
add_library(trix ${SOURCES})
target_include_directories(trix PUBLIC include)

if(TRIX_ENABLE_NEON)
    target_compile_options(trix PRIVATE -march=armv8-a+fp+simd)
endif()

if(TRIX_ENABLE_AVX2)
    target_compile_options(trix PRIVATE -mavx2 -mfma)
endif()

# Install
install(TARGETS trix DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

# Package
set(CPACK_GENERATOR "DEB;RPM;TGZ")
include(CPack)
```

**Deliverables:**
1. CMakeLists.txt (root + subdirs)
2. Cross-platform builds (macOS, Linux, Windows)
3. Debug/Release configs
4. Sanitizer support (asan, ubsan, tsan)
5. Install target (make install)
6. Package generation (deb, rpm, tgz)

**Effort:** 2-3 days  
**Files to create:** CMakeLists.txt (5-6 files)

---

### ❌ CRITICAL 8: Testing
**Current State:** 7 integration tests only  
**Risk:** Bugs in production, no confidence in changes  
**Impact:** Cannot refactor, cannot ship with confidence

**Problem:**
```
Current tests:
├── stability_test.c      # Integration test
├── frequency_sweep.c     # Integration test
├── noise_ramp.c          # Integration test
├── generalization_test.c # Integration test
├── determinism_test.c    # Integration test
└── ... (7 total)

Missing:
├── Unit tests for shapes.h (0 tests)
├── Unit tests for cfc_shapes.h (0 tests)
├── Unit tests for softchip.c (0 tests)
├── Unit tests for codegen.c (0 tests)
└── ... (~1000 tests needed)
```

**What happens in production:**
- Change breaks XOR → No test catches it
- Memory leak introduced → No test catches it
- Performance regression → No test catches it
- Confidence = 0 → Cannot ship updates

**Required Fix:**
```c
// test/unit/test_shapes.c
#include <check.h>
#include <trixc/shapes.h>

START_TEST(test_xor_truth_table) {
    ck_assert_float_eq_tol(shape_xor(0, 0), 0.0, 1e-6);
    ck_assert_float_eq_tol(shape_xor(0, 1), 1.0, 1e-6);
    ck_assert_float_eq_tol(shape_xor(1, 0), 1.0, 1e-6);
    ck_assert_float_eq_tol(shape_xor(1, 1), 0.0, 1e-6);
}
END_TEST

START_TEST(test_and_truth_table) {
    ck_assert_float_eq_tol(shape_and(0, 0), 0.0, 1e-6);
    ck_assert_float_eq_tol(shape_and(0, 1), 0.0, 1e-6);
    ck_assert_float_eq_tol(shape_and(1, 0), 0.0, 1e-6);
    ck_assert_float_eq_tol(shape_and(1, 1), 1.0, 1e-6);
}
END_TEST

// ... 998 more tests
```

**Test pyramid target:**
```
        Unit Tests (1000+)
      /                   \
     /  Integration (50+)  \
    /__________E2E (10+)____\
```

**Deliverables:**
1. Choose test framework (check or cmocka)
2. Write 1000+ unit tests
3. Code coverage measurement (gcov, lcov)
4. CI/CD integration (GitHub Actions)
5. Target: 80%+ line coverage

**Effort:** 8-10 days  
**Files to create:** test/unit/*.c (~20 files)

---

## Priority & Dependencies

### Must Do First (Parallel)
These can be done in parallel by different people:

**Week 1:**
1. **CRITICAL 1: Error Handling** (3-4 days) - Foundation for everything
2. **CRITICAL 2: Logging System** (2-3 days) - Needed for debugging

**Week 2:**
3. **CRITICAL 3: Memory Safety** (5-6 days) - Critical for stability
4. **CRITICAL 4: Input Validation** (4-5 days) - Critical for security

**Week 3:**
5. **CRITICAL 7: Build System** (2-3 days) - Enables CI/CD
6. **CRITICAL 5: Thread Safety** (3-4 days) - Needed for API

**Week 4:**
7. **CRITICAL 6: API Stability** (3-4 days) - Public interface
8. **CRITICAL 8: Testing** (8-10 days, starts Week 2) - Validates everything

### Dependencies
```
Error Handling → Memory Safety → API Stability
       ↓              ↓              ↓
   Logging    → Input Validation → Testing
       ↓              ↓
Build System → Thread Safety
```

---

## Impact Analysis

### If We Don't Fix These

**Medical Device Company:**
- ❌ Cannot pass FDA review (no error handling)
- ❌ Cannot validate determinism (no tests)
- ❌ Cannot debug field failures (no logging)
- **Result: No sale ($500K lost)**

**Automotive Tier-1:**
- ❌ Cannot pass ISO 26262 (memory leaks)
- ❌ Cannot integrate (no API)
- ❌ Cannot deploy (crashes in production)
- **Result: No sale ($2M lost)**

**Industrial IoT:**
- ❌ Cannot accept customer .trix files (security risk)
- ❌ Cannot build on their platform (build system)
- ❌ Cannot scale to 100 devices (memory leaks)
- **Result: No sale ($200K lost)**

**Total revenue at risk: $2.7M+ per year**

---

## Effort Summary

| Item | Effort | Who | When |
|------|--------|-----|------|
| 1. Error Handling | 3-4 days | Engineer 1 | Week 1 |
| 2. Logging System | 2-3 days | Engineer 2 | Week 1 |
| 3. Memory Safety | 5-6 days | Engineer 1 | Week 2 |
| 4. Input Validation | 4-5 days | Engineer 2 | Week 2 |
| 5. Thread Safety | 3-4 days | Engineer 1 | Week 3 |
| 6. API Stability | 3-4 days | Engineer 2 | Week 3-4 |
| 7. Build System | 2-3 days | Engineer 2 | Week 3 |
| 8. Testing | 8-10 days | Both | Week 2-4 |

**Total: 4 weeks with 2 engineers (or 6 weeks with 1 engineer)**

---

## Validation Checklist

After fixing all 8 critical items, validate with:

### ✅ Error Handling Check
- [ ] All functions return error codes
- [ ] All file I/O checks return values
- [ ] Error messages include context (line, column)
- [ ] Test: Invalid .trix file → Clear error message

### ✅ Logging Check
- [ ] All printf() replaced with log_*()
- [ ] Log levels configurable (ERROR, WARN, INFO, DEBUG)
- [ ] Can log to file, stdout, callback
- [ ] Test: Can disable debug logs

### ✅ Memory Safety Check
- [ ] valgrind reports zero leaks
- [ ] AddressSanitizer (asan) passes
- [ ] All arrays have bounds checks
- [ ] Test: 1000 runs → zero leaks

### ✅ Input Validation Check
- [ ] Schema validation for .trix files
- [ ] Bounds checking on all inputs
- [ ] Fuzz testing passes (1M inputs, zero crashes)
- [ ] Test: Malicious .trix file → Rejected safely

### ✅ Thread Safety Check
- [ ] ThreadSanitizer (tsan) passes
- [ ] No global mutable state
- [ ] State passed explicitly
- [ ] Test: 100 threads → No data races

### ✅ API Stability Check
- [ ] Public API defined (trixc.h)
- [ ] Opaque types for ABI stability
- [ ] Versioned (semantic versioning)
- [ ] Test: Can upgrade library without recompiling app

### ✅ Build System Check
- [ ] Builds on macOS, Linux, Windows
- [ ] Cross-compiles for ARM
- [ ] make install works
- [ ] Test: Install and link against library

### ✅ Testing Check
- [ ] 1000+ unit tests pass
- [ ] 80%+ code coverage
- [ ] CI runs on every commit
- [ ] Test: All tests green

---

## Success Criteria

**After 4 weeks, TriX should:**

1. ✅ **Handle all errors gracefully** (no crashes)
2. ✅ **Log all operations** (full observability)
3. ✅ **Never leak memory** (valgrind clean)
4. ✅ **Reject invalid input safely** (fuzz-tested)
5. ✅ **Work in multi-threaded apps** (tsan clean)
6. ✅ **Have stable public API** (versioned, documented)
7. ✅ **Build on all platforms** (CMake)
8. ✅ **Have 80%+ test coverage** (1000+ tests)

**Then TriX is production-ready for safety-critical deployment.**

---

## Next Steps

### Today (Day 1)
1. Review this document with team
2. Assign engineers to items
3. Set up GitHub project board
4. Create issues for 8 critical items

### Tomorrow (Day 2)
1. Engineer 1: Start CRITICAL 1 (Error Handling)
2. Engineer 2: Start CRITICAL 2 (Logging System)
3. Daily standup: 15 min at 9am

### End of Week 1
1. Demo: Error handling and logging working
2. Review: Code review all changes
3. Plan: Week 2 tasks (Memory Safety, Input Validation)

### End of Week 4
1. Validate: All 8 items complete
2. Test: Run full validation checklist
3. Release: Tag v1.0.0-rc1 (release candidate)
4. Document: Update CHANGELOG.md

---

## Resources

### Tools Needed
- **valgrind** - Memory leak detection
- **AddressSanitizer** - Memory error detection
- **ThreadSanitizer** - Data race detection
- **check** or **cmocka** - Unit test framework
- **gcov/lcov** - Code coverage
- **AFL** or **libFuzzer** - Fuzz testing
- **CMake** - Build system
- **Doxygen** - API documentation

### Install (macOS)
```bash
brew install valgrind check lcov cmake doxygen
```

### Install (Ubuntu)
```bash
sudo apt-get install valgrind check lcov cmake doxygen
```

---

## The Bottom Line

**8 critical items. 4 weeks. 2 engineers.**

**Fix these or don't ship.**

**No exceptions for safety-critical deployment.**

**Start with Error Handling and Logging (Week 1).**

---

*End of Critical Items Document*
