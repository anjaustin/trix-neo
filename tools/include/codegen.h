/*
 * codegen.h — Code generation for soft chips
 *
 * Part of TriX toolchain
 * Generates C, NEON, and other targets from SoftChipSpec
 */

#ifndef CODEGEN_H
#define CODEGEN_H

#include "softchip.h"
#include <stdio.h>

/* Target platforms */
typedef enum {
    TARGET_C = 0,      /* Pure portable C */
    TARGET_NEON,       /* ARM NEON intrinsics */
    TARGET_AVX2,       /* Intel AVX2 */
    TARGET_AVX512,     /* Intel AVX-512 */
    TARGET_WASM,       /* WebAssembly */
    TARGET_VERILOG,    /* FPGA/ASIC RTL */
    TARGET_COUNT
} CodegenTarget;

/* Code generation options */
typedef struct {
    CodegenTarget target;
    bool generate_test;      /* Generate test harness */
    bool generate_bench;     /* Generate benchmark code */
    bool optimize;           /* Enable optimizations */
    const char* output_dir;  /* Output directory */
} CodegenOptions;

/* Main code generation function */
int codegen_generate(const SoftChipSpec* spec, const CodegenOptions* opts);

/* Target-specific generators */
int codegen_c(const SoftChipSpec* spec, const CodegenOptions* opts);
int codegen_neon(const SoftChipSpec* spec, const CodegenOptions* opts);
int codegen_header(const SoftChipSpec* spec, FILE* out);

/* Helper functions */
const char* target_name(CodegenTarget target);
CodegenTarget target_from_name(const char* name);
void codegen_options_init(CodegenOptions* opts);

/* File generation helpers */
int generate_source_file(const SoftChipSpec* spec, const CodegenOptions* opts,
                         const char* filename);
int generate_header_file(const SoftChipSpec* spec, const CodegenOptions* opts,
                         const char* filename);
int generate_test_file(const SoftChipSpec* spec, const CodegenOptions* opts,
                       const char* filename);
int generate_makefile(const SoftChipSpec* spec, const CodegenOptions* opts,
                      const char* filename);

/* Linear Kingdom code generation */
int generate_linear_layers(const SoftChipSpec* spec, const CodegenOptions* opts,
                           const char* filename);

#endif /* CODEGEN_H */
