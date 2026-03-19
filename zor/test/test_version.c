/*
 * test_version.c — Version and API stability tests
 *
 * Tests CRITICAL ITEM 6: API Stability
 */

#include "../include/trixc/version.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void) {
    printf("\n");
    printf("╭────────────────────────────────────────────────────────────╮\n");
    printf("│  TriX Version and API Stability Tests                      │\n");
    printf("│  CRITICAL 6: API Stability                                │\n");
    printf("╰────────────────────────────────────────────────────────────╯\n");
    printf("\n");
    
    /* Get version info */
    const trix_version_info_t* version = trix_get_version();
    
    printf("Version Information:\n");
    printf("  Version:        %s\n", version->version_string);
    printf("  Major.Minor.Patch: %d.%d.%d\n", version->major, version->minor, version->patch);
    printf("  Version Number: %llu\n", version->version_number);
    printf("  API Version:    %d\n", version->api_version);
    printf("  ABI Version:    %s\n", version->abi_version);
    printf("\n");
    
    printf("Build Information:\n");
    printf("  Platform:       %s\n", trix_platform_name());
    printf("  Compiler:       %s\n", trix_compiler_name());
    printf("  Build Config:   %s\n", trix_build_config());
    printf("  Build Time:     %s\n", trix_build_timestamp());
    printf("\n");
    
    printf("Feature Detection:\n");
    printf("  Error Handling: %s\n", TRIX_HAS_ERROR_HANDLING ? "✓" : "✗");
    printf("  Logging:        %s\n", TRIX_HAS_LOGGING ? "✓" : "✗");
    printf("  Memory Safety:  %s\n", TRIX_HAS_MEMORY_SAFETY ? "✓" : "✗");
    printf("  Input Validation: %s\n", TRIX_HAS_INPUT_VALIDATION ? "✓" : "✗");
    printf("  Thread Safety:  %s\n", TRIX_HAS_THREAD_SAFETY ? "✓" : "✗");
    printf("  Atomic Ops:     %s\n", TRIX_HAS_ATOMIC_OPS ? "✓" : "✗");
    printf("  Thread Pool:    %s\n", TRIX_HAS_THREAD_POOL ? "✓" : "✗");
    printf("\n");
    
    /* Test version checking */
    printf("Running Tests:\n");
    
    /* Should always pass - check for version 0.0.0 or later */
    assert(trix_version_check(0, 0, 0));
    printf("  ✓ Version check 0.0.0: PASS\n");
    
    /* Should pass - check for current version */
    assert(trix_version_check(TRIX_VERSION_MAJOR, TRIX_VERSION_MINOR, TRIX_VERSION_PATCH));
    printf("  ✓ Version check %d.%d.%d: PASS\n", 
           TRIX_VERSION_MAJOR, TRIX_VERSION_MINOR, TRIX_VERSION_PATCH);
    
    /* Should fail - check for future version */
    assert(!trix_version_check(99, 0, 0));
    printf("  ✓ Version check 99.0.0 (future): PASS (correctly failed)\n");
    
    /* API version check */
    assert(trix_api_version_check(TRIX_API_VERSION));
    printf("  ✓ API version check %d: PASS\n", TRIX_API_VERSION);
    
    /* Should fail - wrong API version */
    assert(!trix_api_version_check(999));
    printf("  ✓ API version check 999 (wrong): PASS (correctly failed)\n");
    
    printf("\n");
    printf("════════════════════════════════════════════════════════════\n");
    printf("  ALL TESTS PASSED (5/5)\n");
    printf("════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    return 0;
}
