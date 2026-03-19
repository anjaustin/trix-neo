# Contributing to TriX

Thank you for your interest in contributing to TriX! We welcome contributions from the community.

## Development Setup

```bash
# Clone the repository
git clone https://github.com/trix-ai/trix.git
cd trix

# Create build directory
mkdir build && cd build

# Configure (Debug mode with AddressSanitizer)
cmake .. -DCMAKE_BUILD_TYPE=Debug -DTRIX_ENABLE_ASAN=ON

# Build
cmake --build . --parallel 4

# Run tests
ctest --output-on-failure
```

## Coding Standards

- **Language:** C11 standard (ISO/IEC 9899:2011)
- **Style:** Follow existing code style
- **Comments:** Use `/* */` for multi-line, `//` for single-line
- **Naming:** 
  - Functions: `trix_module_function()`
  - Types: `trix_type_t`
  - Constants: `TRIX_CONSTANT`
- **Error handling:** Use goto cleanup pattern
- **Memory:** Always use safe wrappers (`trix_malloc`, `trix_strcpy_safe`)

### Example

```c
trix_result_t my_function(const char* input, char* output, size_t output_size) {
    trix_result_t result = TRIX_SUCCESS;
    char* buffer = NULL;
    
    // Validate inputs
    if (!input || !output || output_size == 0) {
        result = TRIX_ERROR_NULL_POINTER;
        goto cleanup;
    }
    
    // Allocate resources
    result = trix_malloc_safe((void**)&buffer, 1024);
    if (result != TRIX_SUCCESS) {
        goto cleanup;
    }
    
    // Do work...
    
cleanup:
    trix_free_safe((void**)&buffer);
    return result;
}
```

## Testing

All new features must include tests.

```bash
cd build
ctest --output-on-failure

# Run with AddressSanitizer
cmake .. -DTRIX_ENABLE_ASAN=ON
cmake --build .
ctest
```

### Test Guidelines

- Add tests to appropriate file in `zor/test/`
- Follow existing test patterns
- Aim for 100% code coverage of new code
- Test both success and failure paths
- Verify no memory leaks (AddressSanitizer)

## Submitting Changes

1. **Fork** the repository
2. **Create a branch** from `main`:
   ```bash
   git checkout -b feature/my-feature
   ```
3. **Make your changes** following coding standards
4. **Add tests** for new functionality
5. **Run tests** and ensure they pass
6. **Commit** with clear message:
   ```bash
   git commit -m "Add support for feature X
   
   Detailed explanation of what this adds and why."
   ```
7. **Push** to your fork:
   ```bash
   git push origin feature/my-feature
   ```
8. **Create Pull Request** on GitHub

## Commit Message Guidelines

- Use present tense ("Add feature" not "Added feature")
- First line: short summary (50 chars max)
- Blank line
- Detailed explanation (if needed)

### Good Examples

```
Add INT4 quantization support

Implements INT4 quantized inference with bit-exact reproducibility.
Includes optimized kernels for ARM NEON and x86 AVX2.
Adds comprehensive tests and documentation.
```

```
Fix memory leak in codegen_generate

The generated code buffer was not being freed in error paths.
Now uses goto cleanup pattern consistently.
```

## What to Contribute

### Good First Issues

Look for issues tagged with `good-first-issue`:
- Add new activation functions
- Improve documentation
- Add Python bindings
- Write tutorial notebooks
- Add platform-specific optimizations

### High Priority

- Toolchain hardening (error handling, tests)
- Additional hardware targets (CUDA, Metal)
- More frozen shape implementations
- Performance optimizations
- Documentation improvements

## Code Review Process

1. Automated tests must pass (CI/CD)
2. At least one maintainer reviews code
3. AddressSanitizer must show zero leaks
4. Code coverage should not decrease
5. Documentation must be updated

## Questions?

- Open an issue for bugs or feature requests
- Start a discussion for questions
- Join our community chat (coming soon)

## License

By contributing, you agree that your contributions will be licensed under the MIT License.

---

Thank you for helping make TriX better! 🚛💨
