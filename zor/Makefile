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
	$(BUILD_DIR)/06_tiny_mlp \
	$(BUILD_DIR)/07_cfc_demo \
	$(BUILD_DIR)/08_evolution_demo

# Tests
TESTS = $(BUILD_DIR)/test_rigorous $(BUILD_DIR)/test_cfc

.PHONY: all examples test test-cfc bench gym genesis genesis-sine genesis-anomaly genesis-control genesis-pattern sentinel clean size

all: $(BUILD_DIR) examples

examples: $(BUILD_DIR) $(EXAMPLES)

test: $(BUILD_DIR) $(TESTS)
	./$(BUILD_DIR)/test_rigorous
	./$(BUILD_DIR)/test_cfc

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

$(BUILD_DIR)/07_cfc_demo: examples/07_cfc_demo.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/08_evolution_demo: examples/08_evolution_demo.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Benchmarks
$(BUILD_DIR)/bench_evolution: test/bench_evolution.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/bench_gym: test/bench_gym.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

bench: $(BUILD_DIR) $(BUILD_DIR)/bench_evolution
	./$(BUILD_DIR)/bench_evolution

gym: $(BUILD_DIR) $(BUILD_DIR)/bench_gym
	./$(BUILD_DIR)/bench_gym

# Foundry (Seed Genesis)
$(BUILD_DIR)/genesis: foundry/genesis.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

genesis: $(BUILD_DIR) $(BUILD_DIR)/genesis
	cd foundry && ../$(BUILD_DIR)/genesis $(TARGET)

genesis-sine: $(BUILD_DIR) $(BUILD_DIR)/genesis
	cd foundry && ../$(BUILD_DIR)/genesis sine

genesis-anomaly: $(BUILD_DIR) $(BUILD_DIR)/genesis
	cd foundry && ../$(BUILD_DIR)/genesis anomaly

genesis-control: $(BUILD_DIR) $(BUILD_DIR)/genesis
	cd foundry && ../$(BUILD_DIR)/genesis control

genesis-pattern: $(BUILD_DIR) $(BUILD_DIR)/genesis
	cd foundry && ../$(BUILD_DIR)/genesis pattern

# Sentinel daemon
$(BUILD_DIR)/sentinel: src/sentinel.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

sentinel: $(BUILD_DIR) $(BUILD_DIR)/sentinel
	@echo "Sentinel built. Run with:"
	@echo "  ./$(BUILD_DIR)/sentinel --cpu"
	@echo "  ./$(BUILD_DIR)/sentinel --memory"
	@echo "  ./$(BUILD_DIR)/sentinel --network"

# Test suite
$(BUILD_DIR)/test_rigorous: test/test_rigorous.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_cfc: test/test_cfc.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test-cfc: $(BUILD_DIR) $(BUILD_DIR)/test_cfc
	./$(BUILD_DIR)/test_cfc

# Show binary sizes
size: all
	@echo ""
	@echo "Binary Sizes:"
	@echo "============="
	@size $(BUILD_DIR)/* 2>/dev/null || ls -la $(BUILD_DIR)/*

clean:
	rm -rf $(BUILD_DIR)
