/* Quick logging demo */
#include "trixc/logging.h"
#include <stdint.h>

int main(void) {
    /* Initialize with default config */
    trix_log_init();
    
    printf("\n══════════════════════════════════════════════════════════\n");
    printf("  TriX Logging System Demo\n");
    printf("══════════════════════════════════════════════════════════\n\n");
    
    /* Test all log levels */
    log_error("This is an ERROR message");
    log_warn("This is a WARNING message");
    log_info("This is an INFO message");
    log_debug("This is a DEBUG message (filtered out at INFO level)");
    log_trace("This is a TRACE message (filtered out at INFO level)");
    
    printf("\n--- Changing to DEBUG level ---\n\n");
    
    trix_log_set_level(TRIX_LOG_DEBUG);
    
    log_error("Error with context: file='%s', code=%d", "test.trix", 404);
    log_warn("Warning: threshold %d exceeds maximum %d", 600, 512);
    log_info("Processing %d records", 1000);
    log_debug("Variable x = %d, y = %d", 42, 100);
    log_trace("This is still filtered (level is DEBUG, not TRACE)");
    
    printf("\n--- Testing hexdump ---\n\n");
    
    uint8_t data[] = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x2c, 0x20, 0x54, 0x72, 0x69, 0x58, 0x21};
    log_hexdump_debug(data, sizeof(data), "Sample data");
    
    printf("\n══════════════════════════════════════════════════════════\n");
    printf("  Demo Complete!\n");
    printf("══════════════════════════════════════════════════════════\n\n");
    
    trix_log_shutdown();
    return 0;
}
