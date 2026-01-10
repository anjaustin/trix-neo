# TriX Pure C - Makefile
#
# "Frozen shapes. Native code. No runtime."
#
# Usage:
#   make            - Build all examples
#   make test       - Run test suite
#   make clean      - Remove build artifacts

CC = gcc
CFLAGS = -O3 -Wall -Wextra -I./include
LDFLAGS = -lm

BUILD_DIR = build

# Examples
EXAMPLES = \
	$(BUILD_DIR)/01_hello_xor \
	$(BUILD_DIR)/02_logic_gates \
	$(BUILD_DIR)/03_full_adder \
	$(BUILD_DIR)/04_activations \
	$(BUILD_DIR)/05_matmul \
	$(BUILD_DIR)/06_tiny_mlp

# Tests
TESTS = $(BUILD_DIR)/test_rigorous

.PHONY: all examples test clean size

all: $(BUILD_DIR) examples

examples: $(BUILD_DIR) $(EXAMPLES)

test: $(BUILD_DIR) $(TESTS)
	./$(BUILD_DIR)/test_rigorous

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Examples
$(BUILD_DIR)/01_hello_xor: examples/01_hello_xor.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/02_logic_gates: examples/02_logic_gates.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/03_full_adder: examples/03_full_adder.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/04_activations: examples/04_activations.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/05_matmul: examples/05_matmul.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/06_tiny_mlp: examples/06_tiny_mlp.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Test suite
$(BUILD_DIR)/test_rigorous: test/test_rigorous.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Show binary sizes
size: all
	@echo ""
	@echo "Binary Sizes:"
	@echo "============="
	@size $(BUILD_DIR)/* 2>/dev/null || ls -la $(BUILD_DIR)/*

clean:
	rm -rf $(BUILD_DIR)
