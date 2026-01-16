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
	$(BUILD_DIR)/08_evolution_demo \
	$(BUILD_DIR)/09_hsos_demo

# Tests
TESTS = $(BUILD_DIR)/test_rigorous $(BUILD_DIR)/test_cfc

.PHONY: all examples test test-cfc bench gym genesis genesis-sine genesis-anomaly genesis-control genesis-pattern sentinel director entromorph cortex visor hsos forge testdata fuse fuse-compile validate validate-quick clean size

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

$(BUILD_DIR)/09_hsos_demo: examples/09_hsos_demo.c src/hsos.c
	$(CC) $(CFLAGS) examples/09_hsos_demo.c src/hsos.c -o $@ $(LDFLAGS)

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

# ═══════════════════════════════════════════════════════════════
# ENTROMORPHIC OS v1.0 — The Read-Only Nervous System
# ═══════════════════════════════════════════════════════════════

# Sentinel daemon (perception agent with V3 CfC chip)
$(BUILD_DIR)/sentinel: src/sentinel.c foundry/seeds/sine_seed.h include/shm_protocol.h
	$(CC) $(CFLAGS) -I./foundry src/sentinel.c -o $@ -lrt $(LDFLAGS)

# Director (ncurses grid viewer)
$(BUILD_DIR)/director: bin/director.c include/shm_protocol.h
	$(CC) $(CFLAGS) bin/director.c -o $@ -lncurses -lrt

# Build all EntroMorphic OS components
entromorph: $(BUILD_DIR) $(BUILD_DIR)/sentinel $(BUILD_DIR)/director_text
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "EntroMorphic OS v1.0 — The Read-Only Nervous System"
	@echo "═══════════════════════════════════════════════════════════════"
	@echo ""
	@echo "Components built:"
	@echo "  $(BUILD_DIR)/sentinel       — Perception agent (V3 CfC + AGC)"
	@echo "  $(BUILD_DIR)/director_text  — Grid viewer (ANSI text mode)"
	@echo ""
	@echo "IGNITION SEQUENCE:"
	@echo ""
	@echo "  # Terminal 1: Launch sentinels (start 1-4 for demo)"
	@echo "  ./$(BUILD_DIR)/sentinel 0 CPU_0 --mock &"
	@echo "  ./$(BUILD_DIR)/sentinel 1 CPU_1 --mock &"
	@echo "  ./$(BUILD_DIR)/sentinel 4 NET_TX --mock &"
	@echo "  ./$(BUILD_DIR)/sentinel 8 MEM_FREE --mock &"
	@echo ""
	@echo "  # Terminal 2: Launch director (shows 3x3 grid)"
	@echo "  ./$(BUILD_DIR)/director_text"
	@echo ""
	@echo "Controls: 'q' to quit director"
	@echo "Threshold: 0.1122911624 (Second Star Constant)"
	@echo ""

# Legacy sentinel target (for compatibility)
sentinel: $(BUILD_DIR) $(BUILD_DIR)/sentinel
	@echo "Sentinel built: ./$(BUILD_DIR)/sentinel <ID 0-8> <LABEL> [--mock]"

# Director target (ncurses version)
director: $(BUILD_DIR) $(BUILD_DIR)/director
	@echo "Director built: ./$(BUILD_DIR)/director"

# Director text mode (no ncurses dependency)
$(BUILD_DIR)/director_text: bin/director_text.c include/shm_protocol.h
	$(CC) $(CFLAGS) bin/director_text.c -o $@ -lrt

director-text: $(BUILD_DIR) $(BUILD_DIR)/director_text
	@echo "Director (text) built: ./$(BUILD_DIR)/director_text"

# ═══════════════════════════════════════════════════════════════
# THE FORGE — Data in, Chip out
# ═══════════════════════════════════════════════════════════════

$(BUILD_DIR)/forge: bin/forge.c
	$(CC) $(CFLAGS) -I./foundry bin/forge.c -o $@ $(LDFLAGS)

$(BUILD_DIR)/generate_dirty_data: bin/generate_dirty_data.c
	$(CC) $(CFLAGS) bin/generate_dirty_data.c -o $@ $(LDFLAGS)

forge: $(BUILD_DIR) $(BUILD_DIR)/forge
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "THE FORGE — EntroMorphic Chip Compiler"
	@echo "═══════════════════════════════════════════════════════════════"
	@echo ""
	@echo "Usage:"
	@echo "  ./$(BUILD_DIR)/forge train --input data.csv --out chip.h"
	@echo ""
	@echo "Example with test data:"
	@echo "  ./$(BUILD_DIR)/generate_dirty_data motor > motor.csv"
	@echo "  ./$(BUILD_DIR)/forge train -i motor.csv -o motor_chip.h -t 30"
	@echo ""

testdata: $(BUILD_DIR) $(BUILD_DIR)/generate_dirty_data
	@echo "Generating test datasets..."
	./$(BUILD_DIR)/generate_dirty_data motor > $(BUILD_DIR)/motor_vibration.csv
	./$(BUILD_DIR)/generate_dirty_data cpu   > $(BUILD_DIR)/cpu_load.csv
	./$(BUILD_DIR)/generate_dirty_data voice > $(BUILD_DIR)/voice_pitch.csv
	@echo ""
	@echo "Generated:"
	@echo "  $(BUILD_DIR)/motor_vibration.csv"
	@echo "  $(BUILD_DIR)/cpu_load.csv"
	@echo "  $(BUILD_DIR)/voice_pitch.csv"

# THE FUSE BOX — Universal Chip Tester
# Usage: make fuse CHIP_FILE=motor_chip.h CHIP_NAME=MOTOR_CHIP
# Then:  ./build/fuse_test data.csv
CHIP_FILE ?= chip.h
CHIP_NAME ?= CHIP

$(BUILD_DIR)/fuse_test: bin/fuse_box.c $(CHIP_FILE)
	$(CC) $(CFLAGS) -DCHIP=$(CHIP_NAME) -include $(CHIP_FILE) bin/fuse_box.c -o $@ $(LDFLAGS)

fuse:
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "THE FUSE BOX — Universal Chip Tester"
	@echo "═══════════════════════════════════════════════════════════════"
	@echo ""
	@echo "Usage:"
	@echo "  make fuse-compile CHIP_FILE=my_chip.h CHIP_NAME=MY_CHIP"
	@echo "  ./$(BUILD_DIR)/fuse_test data.csv"
	@echo ""
	@echo "Example:"
	@echo "  make fuse-compile CHIP_FILE=$(BUILD_DIR)/motor_chip.h CHIP_NAME=MOTOR_CHIP"
	@echo "  ./$(BUILD_DIR)/fuse_test $(BUILD_DIR)/motor_vibration.csv"
	@echo ""

fuse-compile: $(BUILD_DIR)
	$(CC) $(CFLAGS) -DCHIP=$(CHIP_NAME) -include $(CHIP_FILE) bin/fuse_box.c -o $(BUILD_DIR)/fuse_test $(LDFLAGS)
	@echo "Fuse tester built: ./$(BUILD_DIR)/fuse_test"

# ═══════════════════════════════════════════════════════════════
# GPU CORTEX — 16 Million Liquid Neurons (CUDA)
# ═══════════════════════════════════════════════════════════════

NVCC = nvcc
NVFLAGS = -O3 -gencode arch=compute_89,code=sm_89

$(BUILD_DIR)/cortex_bench: src/cortex.cu include/alpha_device.cuh
	$(NVCC) $(NVFLAGS) src/cortex.cu -o $@

$(BUILD_DIR)/visor: src/visor.cu include/alpha_device.cuh
	$(NVCC) $(NVFLAGS) src/visor.cu -o $@ -lglfw -lGL -lGLEW -lX11

cortex: $(BUILD_DIR) $(BUILD_DIR)/cortex_bench
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "CORTEX — 16 Million Liquid Neuron Benchmark"
	@echo "═══════════════════════════════════════════════════════════════"
	@echo ""
	@echo "Built: ./$(BUILD_DIR)/cortex_bench"
	@echo ""
	@echo "Architecture:"
	@echo "  - 256³ = 16,777,216 voxels"
	@echo "  - 19 neurons per voxel (SoA layout)"
	@echo "  - Double-buffered ping-pong"
	@echo "  - 6-neighbor signal propagation"
	@echo ""
	@echo "Memory: ~1.28 GB VRAM"
	@echo ""
	@echo "Run benchmark:"
	@echo "  ./$(BUILD_DIR)/cortex_bench"
	@echo ""

visor: $(BUILD_DIR) $(BUILD_DIR)/visor
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "VISOR — Volumetric Brain Renderer"
	@echo "═══════════════════════════════════════════════════════════════"
	@echo ""
	@echo "Built: ./$(BUILD_DIR)/visor"
	@echo ""
	@echo "Controls:"
	@echo "  ESC   - Quit"
	@echo "  SPACE - Inject stimulus"
	@echo "  A/D   - Rotate camera"
	@echo "  W/S   - Zoom in/out"
	@echo ""
	@echo "Run:"
	@echo "  ./$(BUILD_DIR)/visor"
	@echo ""

# Hollywood Squares OS demo
hsos: $(BUILD_DIR) $(BUILD_DIR)/09_hsos_demo
	./$(BUILD_DIR)/09_hsos_demo

# Test suite
$(BUILD_DIR)/test_rigorous: test/test_rigorous.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_cfc: test/test_cfc.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test-cfc: $(BUILD_DIR) $(BUILD_DIR)/test_cfc
	./$(BUILD_DIR)/test_cfc

# ═══════════════════════════════════════════════════════════════
# VALIDATION TESTS (The 5 Skeptic Tests)
# ═══════════════════════════════════════════════════════════════

VALIDATION_TESTS = \
	$(BUILD_DIR)/stability_test \
	$(BUILD_DIR)/frequency_sweep \
	$(BUILD_DIR)/noise_ramp \
	$(BUILD_DIR)/generalization_test \
	$(BUILD_DIR)/sawtooth_test \
	$(BUILD_DIR)/determinism_test \
	$(BUILD_DIR)/zit_test

$(BUILD_DIR)/stability_test: test/stability_test.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/frequency_sweep: test/frequency_sweep.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/noise_ramp: test/noise_ramp.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/generalization_test: test/generalization_test.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/sawtooth_test: test/sawtooth_test.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/determinism_test: test/determinism_test.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/zit_test: test/zit_test.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

.PHONY: validate validate-quick

validate: $(BUILD_DIR) $(VALIDATION_TESTS)
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "RUNNING ALL VALIDATION TESTS"
	@echo "═══════════════════════════════════════════════════════════════"
	@echo ""
	./$(BUILD_DIR)/zit_test
	./$(BUILD_DIR)/frequency_sweep
	./$(BUILD_DIR)/noise_ramp
	./$(BUILD_DIR)/generalization_test
	./$(BUILD_DIR)/sawtooth_test
	./$(BUILD_DIR)/determinism_test
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "ALL VALIDATION TESTS PASSED"
	@echo "═══════════════════════════════════════════════════════════════"

validate-quick: $(BUILD_DIR) $(BUILD_DIR)/zit_test $(BUILD_DIR)/determinism_test
	@echo "Quick validation (zit + determinism)..."
	./$(BUILD_DIR)/zit_test
	./$(BUILD_DIR)/determinism_test

# Show binary sizes
size: all
	@echo ""
	@echo "Binary Sizes:"
	@echo "============="
	@size $(BUILD_DIR)/* 2>/dev/null || ls -la $(BUILD_DIR)/*

clean:
	rm -rf $(BUILD_DIR)
