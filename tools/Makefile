# TriX Toolchain Makefile
#
# Builds:
#   - trix CLI (main toolchain)
#   - Linear Kingdom forge tests
#   - CfC forge tests

CC = clang
CFLAGS = -O2 -Wall -Wextra -std=c11 -I include
CFLAGS_NEON = -O3 -Wall -std=c11 -I include -mcpu=apple-m4 -march=armv8.6-a+dotprod
LDFLAGS =

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = ../bin
TEST_DIR = test

# Main CLI sources
CLI_SRCS = $(SRC_DIR)/trix.c $(SRC_DIR)/softchip.c $(SRC_DIR)/codegen.c $(SRC_DIR)/linear_forge.c
CLI_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CLI_SRCS))
TARGET = $(BIN_DIR)/trix

# Test targets
TEST_LINEAR = $(BUILD_DIR)/test_linear_forge
TEST_NEON_COMPILE = $(BUILD_DIR)/test_forged_neon_compile
TEST_CFC = $(BUILD_DIR)/test_cfc_forge
TEST_I8MM = $(BUILD_DIR)/test_i8mm_forge
BENCH_FORGED = $(BUILD_DIR)/bench_forged
E2E_DEMO = $(BUILD_DIR)/e2e_cfc_demo

.PHONY: all clean install test bench demo test-i8mm

all: $(TARGET)

# === DIRECTORIES ===
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# === MAIN CLI ===
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(CLI_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(CLI_OBJS)
	@echo ""
	@echo "Built: $(TARGET)"
	@echo ""

# === TESTS ===
$(TEST_LINEAR): $(TEST_DIR)/test_linear_forge.c $(SRC_DIR)/linear_forge.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_NEON_COMPILE): $(TEST_DIR)/test_forged_neon_compile.c $(SRC_DIR)/linear_forge.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_CFC): $(TEST_DIR)/test_cfc_forge.c $(SRC_DIR)/cfc_forge.c $(SRC_DIR)/linear_forge.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BENCH_FORGED): $(TEST_DIR)/bench_forged_vs_handwritten.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_NEON) -o $@ $<

$(E2E_DEMO): $(TEST_DIR)/e2e_cfc_demo.c $(SRC_DIR)/cfc_forge.c $(SRC_DIR)/linear_forge.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ -lm

$(TEST_I8MM): $(TEST_DIR)/test_i8mm_forge.c $(SRC_DIR)/linear_forge.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TEST_LINEAR) $(TEST_NEON_COMPILE) $(TEST_CFC)
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "  Running TriX Linear Kingdom Tests"
	@echo "═══════════════════════════════════════════════════════════════"
	@echo ""
	@echo "--- Test 1: Linear Forge Basic ---"
	$(TEST_LINEAR)
	@echo ""
	@echo "--- Test 2: NEON Compile Test ---"
	$(TEST_NEON_COMPILE)
	@echo ""
	@echo "--- Test 3: CfC Forge Test ---"
	$(TEST_CFC)
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "  All Tests Complete"
	@echo "═══════════════════════════════════════════════════════════════"

bench: $(BENCH_FORGED)
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "  Running M4 Performance Benchmark"
	@echo "═══════════════════════════════════════════════════════════════"
	@echo ""
	$(BENCH_FORGED)

demo: $(E2E_DEMO)
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "  Running End-to-End CfC Forge Demo"
	@echo "═══════════════════════════════════════════════════════════════"
	@echo ""
	$(E2E_DEMO)

test-i8mm: $(TEST_I8MM)
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "  Running I8MM (SMMLA) Batch Inference Test"
	@echo "═══════════════════════════════════════════════════════════════"
	@echo ""
	$(TEST_I8MM)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Install to /usr/local/bin (requires sudo)
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/trix
	@echo "Installed: /usr/local/bin/trix"
