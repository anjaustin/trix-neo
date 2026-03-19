/*
 * version.h — Version Information and API Stability
 *
 * Part of TriX Runtime (zor)
 * Production-grade API versioning for ABI stability
 *
 * CRITICAL ITEM 6: API Stability
 *
 * Features:
 * - Semantic versioning (MAJOR.MINOR.PATCH)
 * - API version checking at compile-time
 * - Runtime version validation
 * - ABI stability guarantees
 * - Deprecation warnings
 * - Feature detection macros
 * - Build information
 *
 * Usage:
 *   // Compile-time version check
 *   #if TRIX_VERSION_MAJOR < 1
 *       #error "Requires TriX 1.0 or later"
 *   #endif
 *
 *   // Runtime version check
 *   if (trix_version_check(1, 0, 0)) {
 *       // Compatible version
 *   }
 *
 *   // Feature detection
 *   #ifdef TRIX_HAS_THREAD_SAFETY
 *       // Use thread-safe features
 *   #endif
 */

#ifndef TRIXC_VERSION_H
#define TRIXC_VERSION_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*═══════════════════════════════════════════════════════════════════════════
 * Version Numbers
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Semantic versioning: MAJOR.MINOR.PATCH
 * 
 * MAJOR: Incompatible API changes
 * MINOR: Backward-compatible new features
 * PATCH: Backward-compatible bug fixes
 */
#define TRIX_VERSION_MAJOR 0
#define TRIX_VERSION_MINOR 1
#define TRIX_VERSION_PATCH 0

/**
 * Pre-release identifier (empty for stable releases)
 * Examples: "alpha", "beta", "rc1", ""
 */
#define TRIX_VERSION_PRERELEASE ""

/**
 * Build metadata (empty for release builds)
 * Examples: "git.abc123", "20260320", ""
 */
#define TRIX_VERSION_BUILD ""

/**
 * Combined version number for numeric comparison
 * Format: MMMMmmmmpppp (MAJOR=4 digits, MINOR=4 digits, PATCH=4 digits)
 * Example: 1.2.3 = 10002000003
 */
#define TRIX_VERSION_NUMBER \
    ((TRIX_VERSION_MAJOR * 100000000ULL) + \
     (TRIX_VERSION_MINOR * 10000ULL) + \
     (TRIX_VERSION_PATCH))

/**
 * Version string
 * Format: "MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]"
 * Example: "0.1.0", "1.0.0-beta", "1.2.3+git.abc123"
 */
#define TRIX_VERSION_STRING_HELPER(a, b, c, pre, build) \
    #a "." #b "." #c \
    (sizeof(pre) > 1 ? "-" pre : "") \
    (sizeof(build) > 1 ? "+" build : "")

#define TRIX_VERSION_STRING \
    TRIX_VERSION_STRING_HELPER(TRIX_VERSION_MAJOR, TRIX_VERSION_MINOR, \
                                TRIX_VERSION_PATCH, TRIX_VERSION_PRERELEASE, \
                                TRIX_VERSION_BUILD)

/*═══════════════════════════════════════════════════════════════════════════
 * API Version (ABI Compatibility)
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * API version changes when binary incompatible changes are made
 * 
 * ABI Stability Rules:
 * - Same API version = Binary compatible (can swap libraries)
 * - Different API version = Binary incompatible (must recompile)
 * 
 * API version increments when:
 * - Struct layouts change (size or field order)
 * - Function signatures change
 * - Enum values change or reorder
 * - Memory layout changes
 * 
 * API version does NOT change when:
 * - New functions added
 * - New enums/structs added
 * - Implementation details change
 * - Bug fixes
 */
#define TRIX_API_VERSION 1

/**
 * ABI version string
 */
#define TRIX_ABI_VERSION "1.0"

/*═══════════════════════════════════════════════════════════════════════════
 * Feature Detection
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Feature availability macros
 * These can be used for conditional compilation
 */
#define TRIX_HAS_ERROR_HANDLING 1
#define TRIX_HAS_LOGGING 1
#define TRIX_HAS_MEMORY_SAFETY 1
#define TRIX_HAS_INPUT_VALIDATION 1
#define TRIX_HAS_THREAD_SAFETY 1
#define TRIX_HAS_ATOMIC_OPS 1
#define TRIX_HAS_THREAD_POOL 1

/**
 * Platform feature detection
 */
#if defined(_WIN32) || defined(_WIN64)
    #define TRIX_PLATFORM_WINDOWS 1
    #define TRIX_PLATFORM_NAME "Windows"
#elif defined(__APPLE__)
    #define TRIX_PLATFORM_MACOS 1
    #define TRIX_PLATFORM_NAME "macOS"
#elif defined(__linux__)
    #define TRIX_PLATFORM_LINUX 1
    #define TRIX_PLATFORM_NAME "Linux"
#elif defined(__unix__)
    #define TRIX_PLATFORM_UNIX 1
    #define TRIX_PLATFORM_NAME "Unix"
#else
    #define TRIX_PLATFORM_UNKNOWN 1
    #define TRIX_PLATFORM_NAME "Unknown"
#endif

/**
 * Compiler detection
 */
#if defined(__clang__)
    #define TRIX_COMPILER_CLANG 1
    #define TRIX_COMPILER_NAME "Clang"
    #define TRIX_COMPILER_VERSION __clang_version__
#elif defined(__GNUC__)
    #define TRIX_COMPILER_GCC 1
    #define TRIX_COMPILER_NAME "GCC"
    #define TRIX_COMPILER_VERSION __VERSION__
#elif defined(_MSC_VER)
    #define TRIX_COMPILER_MSVC 1
    #define TRIX_COMPILER_NAME "MSVC"
    #define TRIX_COMPILER_VERSION _MSC_FULL_VER
#else
    #define TRIX_COMPILER_UNKNOWN 1
    #define TRIX_COMPILER_NAME "Unknown"
    #define TRIX_COMPILER_VERSION "Unknown"
#endif

/*═══════════════════════════════════════════════════════════════════════════
 * Deprecation Warnings
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Mark function/type as deprecated
 * Usage: TRIX_DEPRECATED("Use new_function() instead") void old_function(void);
 */
#if defined(__GNUC__) || defined(__clang__)
    #define TRIX_DEPRECATED(msg) __attribute__((deprecated(msg)))
#elif defined(_MSC_VER)
    #define TRIX_DEPRECATED(msg) __declspec(deprecated(msg))
#else
    #define TRIX_DEPRECATED(msg)
#endif

/**
 * Mark function as deprecated in a specific version
 * Usage: TRIX_DEPRECATED_SINCE(0, 2, 0, "Use new_api()") void old_api(void);
 */
#define TRIX_DEPRECATED_SINCE(major, minor, patch, msg) \
    TRIX_DEPRECATED("Deprecated since " #major "." #minor "." #patch ": " msg)

/**
 * Mark function as to be removed in a future version
 * Usage: TRIX_DEPRECATED_FOR(1, 0, 0, "Use new_api()") void old_api(void);
 */
#define TRIX_DEPRECATED_FOR(major, minor, patch, msg) \
    TRIX_DEPRECATED("Will be removed in " #major "." #minor "." #patch ": " msg)

/*═══════════════════════════════════════════════════════════════════════════
 * API Visibility
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Public API export/import
 */
#if defined(_WIN32) || defined(_WIN64)
    #ifdef TRIX_BUILD_SHARED
        #define TRIX_API __declspec(dllexport)
    #else
        #define TRIX_API __declspec(dllimport)
    #endif
#else
    #if defined(__GNUC__) && __GNUC__ >= 4
        #define TRIX_API __attribute__((visibility("default")))
    #else
        #define TRIX_API
    #endif
#endif

/**
 * Internal API (not part of public API, may change)
 */
#define TRIX_INTERNAL

/**
 * Experimental API (unstable, may change)
 */
#define TRIX_EXPERIMENTAL TRIX_DEPRECATED("Experimental API: may change")

/*═══════════════════════════════════════════════════════════════════════════
 * Version Checking Functions
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Runtime version information
 */
typedef struct {
    int major;
    int minor;
    int patch;
    const char* prerelease;
    const char* build;
    const char* version_string;
    uint64_t version_number;
    int api_version;
    const char* abi_version;
} trix_version_info_t;

/**
 * Get runtime version information
 * This allows checking the version of the actually loaded library
 */
TRIX_API const trix_version_info_t* trix_get_version(void);

/**
 * Check if runtime version is compatible with required version
 * Returns true if runtime version >= required version
 * 
 * @param required_major Minimum required major version
 * @param required_minor Minimum required minor version
 * @param required_patch Minimum required patch version
 * @return true if compatible, false otherwise
 */
TRIX_API bool trix_version_check(int required_major, int required_minor, 
                                  int required_patch);

/**
 * Check if runtime API version matches required API version
 * Returns true only if API versions are exactly equal (ABI compatible)
 * 
 * @param required_api_version Required API version
 * @return true if ABI compatible, false otherwise
 */
TRIX_API bool trix_api_version_check(int required_api_version);

/**
 * Get platform name
 */
TRIX_API const char* trix_platform_name(void);

/**
 * Get compiler name
 */
TRIX_API const char* trix_compiler_name(void);

/**
 * Get build configuration (Debug, Release, RelWithDebInfo, etc.)
 */
TRIX_API const char* trix_build_config(void);

/**
 * Get build timestamp
 */
TRIX_API const char* trix_build_timestamp(void);

/*═══════════════════════════════════════════════════════════════════════════
 * Compile-Time Version Checking
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * Require minimum version at compile time
 * Usage: TRIX_REQUIRE_VERSION(0, 1, 0)
 */
#define TRIX_REQUIRE_VERSION(major, minor, patch) \
    _Static_assert( \
        TRIX_VERSION_NUMBER >= ((major) * 100000000ULL + (minor) * 10000ULL + (patch)), \
        "TriX version " #major "." #minor "." #patch " or later required" \
    )

/**
 * Check version at compile time
 * Usage: #if TRIX_VERSION_AT_LEAST(0, 1, 0)
 */
#define TRIX_VERSION_AT_LEAST(major, minor, patch) \
    (TRIX_VERSION_NUMBER >= ((major) * 100000000ULL + (minor) * 10000ULL + (patch)))

/**
 * Check exact version at compile time
 * Usage: #if TRIX_VERSION_EXACTLY(0, 1, 0)
 */
#define TRIX_VERSION_EXACTLY(major, minor, patch) \
    (TRIX_VERSION_MAJOR == (major) && \
     TRIX_VERSION_MINOR == (minor) && \
     TRIX_VERSION_PATCH == (patch))

/*═══════════════════════════════════════════════════════════════════════════
 * API Stability Guarantees
 *═══════════════════════════════════════════════════════════════════════════*/

/**
 * TriX API Stability Policy:
 * 
 * Pre-1.0 (0.x.y):
 * - API may change between minor versions
 * - ABI may change between minor versions
 * - Backward compatibility is best-effort
 * 
 * Post-1.0 (1.x.y and later):
 * - MAJOR version: Breaking API/ABI changes allowed
 * - MINOR version: Backward-compatible additions only
 * - PATCH version: Bug fixes only, no API changes
 * 
 * Deprecation Policy:
 * - Functions marked deprecated for at least 1 minor version
 * - Deprecated functions removed only in major versions
 * - Warnings provided via TRIX_DEPRECATED macro
 * 
 * ABI Compatibility:
 * - Same API_VERSION = ABI compatible (can swap .so/.dll)
 * - Different API_VERSION = Must recompile
 * - Check with trix_api_version_check() at runtime
 * 
 * Thread Safety:
 * - All public APIs are thread-safe unless documented otherwise
 * - Init/destroy functions must be called from single thread
 * - Const functions are always thread-safe
 */

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_VERSION_H */
