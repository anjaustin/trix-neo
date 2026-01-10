# 6502 Proof of Concept - Makefile
#
# This is a DEMO, not core TriX.
# It proves that frozen shapes can implement a complete ALU.
#
# Usage:
#   make        - Build the demo
#   make run    - Run the demo
#   make clean  - Clean build artifacts

CC = gcc
CFLAGS = -O3 -Wall -Wextra -I./include -I../../zor/include
LDFLAGS = -lm

BUILD_DIR = build

.PHONY: all run clean

all: $(BUILD_DIR)/frozen_6502

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/frozen_6502: $(BUILD_DIR) frozen_6502_standalone.c
	$(CC) $(CFLAGS) frozen_6502_standalone.c -o $@ $(LDFLAGS)

run: $(BUILD_DIR)/frozen_6502
	./$(BUILD_DIR)/frozen_6502

clean:
	rm -rf $(BUILD_DIR)
