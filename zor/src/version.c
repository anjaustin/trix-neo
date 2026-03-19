/*
 * version.c — Version Information Implementation
 *
 * Part of TriX Runtime (zor)
 * Production-grade API versioning for ABI stability
 */

#include "../include/trixc/version.h"
#include <stdio.h>
#include <string.h>

/*═══════════════════════════════════════════════════════════════════════════
 * Build Configuration
 *═══════════════════════════════════════════════════════════════════════════*/

/* Build configuration - set by CMake */
#ifndef TRIX_BUILD_CONFIG
    #ifdef NDEBUG
        #define TRIX_BUILD_CONFIG "Release"
    #else
        #define TRIX_BUILD_CONFIG "Debug"
    #endif
#endif

/* Build timestamp - set by CMake or use compile time */
#ifndef TRIX_BUILD_TIMESTAMP
    #define TRIX_BUILD_TIMESTAMP __DATE__ " " __TIME__
#endif

/*═══════════════════════════════════════════════════════════════════════════
 * Version Information
 *═══════════════════════════════════════════════════════════════════════════*/

/* Construct version string at compile time */
static const char* build_version_string(void) {
    static char version_buf[128];
    
    /* Base version */
    snprintf(version_buf, sizeof(version_buf), "%d.%d.%d",
             TRIX_VERSION_MAJOR, TRIX_VERSION_MINOR, TRIX_VERSION_PATCH);
    
    /* Add prerelease if present */
    if (strlen(TRIX_VERSION_PRERELEASE) > 0) {
        strncat(version_buf, "-", sizeof(version_buf) - strlen(version_buf) - 1);
        strncat(version_buf, TRIX_VERSION_PRERELEASE, 
                sizeof(version_buf) - strlen(version_buf) - 1);
    }
    
    /* Add build metadata if present */
    if (strlen(TRIX_VERSION_BUILD) > 0) {
        strncat(version_buf, "+", sizeof(version_buf) - strlen(version_buf) - 1);
        strncat(version_buf, TRIX_VERSION_BUILD,
                sizeof(version_buf) - strlen(version_buf) - 1);
    }
    
    return version_buf;
}

/* Static version info */
static trix_version_info_t version_info = {
    .major = TRIX_VERSION_MAJOR,
    .minor = TRIX_VERSION_MINOR,
    .patch = TRIX_VERSION_PATCH,
    .prerelease = TRIX_VERSION_PRERELEASE,
    .build = TRIX_VERSION_BUILD,
    .version_string = NULL,  /* Initialized on first call */
    .version_number = TRIX_VERSION_NUMBER,
    .api_version = TRIX_API_VERSION,
    .abi_version = TRIX_ABI_VERSION
};

const trix_version_info_t* trix_get_version(void) {
    /* Initialize version string on first call */
    if (version_info.version_string == NULL) {
        version_info.version_string = build_version_string();
    }
    return &version_info;
}

bool trix_version_check(int required_major, int required_minor, int required_patch) {
    /* Build required version number */
    uint64_t required = (required_major * 100000000ULL) +
                        (required_minor * 10000ULL) +
                        (required_patch);
    
    /* Check if runtime version is >= required version */
    return TRIX_VERSION_NUMBER >= required;
}

bool trix_api_version_check(int required_api_version) {
    /* API version must match exactly for ABI compatibility */
    return TRIX_API_VERSION == required_api_version;
}

const char* trix_platform_name(void) {
    return TRIX_PLATFORM_NAME;
}

const char* trix_compiler_name(void) {
    return TRIX_COMPILER_NAME;
}

const char* trix_build_config(void) {
    return TRIX_BUILD_CONFIG;
}

const char* trix_build_timestamp(void) {
    return TRIX_BUILD_TIMESTAMP;
}
