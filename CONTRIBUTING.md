# Contributing to SS_Lib

Thank you for your interest in contributing to SS_Lib! This document provides guidelines and instructions for contributing.

## Code of Conduct

By participating in this project, you agree to maintain a respectful and constructive environment.

## How Can I Contribute?

### Reporting Bugs

Before creating bug reports, please check existing issues to avoid duplicates. When creating a bug report, include:

- A clear and descriptive title
- Steps to reproduce the issue
- Expected behavior vs actual behavior
- System information (OS, compiler, version)
- Minimal code example demonstrating the issue

### Suggesting Enhancements

Enhancement suggestions are welcome! Please include:

- Use case for the feature
- Proposed API/implementation approach
- Why this enhancement would be useful to most users

### Pull Requests

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes (for a bug fix, first write a test that fails without the fix)
4. Add/update tests as needed
5. Update documentation
6. Run tests locally (`make test-all` and the sanitizer targets)
7. Commit with clear messages
8. Push to your fork
9. Open a Pull Request

## Development Setup

```bash
# Clone your fork
git clone https://github.com/YOUR-USERNAME/ss_lib.git
cd ss_lib

# Build the library
make clean && make all

# Run tests
make test-all
make test_embedded

# Build documentation
make docs
```

`make help` lists all targets. The Makefile honours `CC` and builds everything with one `FEATURES` variable.

## Coding Standards

### C Style Guide

- Use 4 spaces for indentation (no tabs)
- Maximum line length: 100 characters
- Opening braces on same line for functions
- Use meaningful variable names
- Comment complex logic

Example:
```c
ss_error_t ss_example_function(const char* param) {
    if (!param) {
        return SS_ERR_NULL_PARAM;
    }
    
    /* Complex logic explanation */
    return SS_OK;
}
```

### Source Rules

- `src/ss_lib.c` is written in the common subset of C89 and C11: block comments only, declarations at the top of blocks, C11-only facilities behind `__STDC_VERSION__` checks.
- `src/ss_lib_c89.c` is the C89 copy and must stay in sync: its body is identical to `src/ss_lib.c`, only the header comment differs. `scripts/check_c89_sync.sh` checks it; `scripts/check_c89_sync.sh --fix` copies the body over. `make test-c89` builds the copy as strict C89 against the test suite.
- `include/ss_lib_single.h` is generated. After changing `include/ss_lib.h`, `include/ss_config.h` or `src/ss_lib.c`, run `make single-header` and commit the result; `make check-single-header` fails CI when it is stale. Never edit it by hand.
- The version lives only in `include/ss_lib.h`. To release, bump `SS_VERSION_MAJOR`/`MINOR`/`PATCH`, `SS_VERSION_STRING` and the `@version` comment, regenerate the single header and add a `## [x.y.z]` section to `CHANGELOG.md`; `make check-version` verifies them. The Makefile, CMake, Doxygen and pkg-config read the version from the header.
- Configuration macros must not be `#define`d in examples or tests; pass them on the command line so the library and its callers agree.

### Commit Messages

- Use present tense ("Add feature" not "Added feature")
- Limit first line to 72 characters
- Reference issues and pull requests

Example:
```
Add static memory allocation support

- Implement fixed-size pools for embedded systems
- Add SS_USE_STATIC_MEMORY configuration option
- Update documentation with embedded examples

Fixes #123
```

## Testing

### Running Tests

```bash
# Run the test suite (default configuration)
make test

# Everything CI checks locally: static, minimal, C89 and newest-standard
# builds, C89..C2y compile check, single-header and version consistency
make test-all

# Sanitizers and memory checks
make test-asan
make test-ubsan
make test-tsan        # runs test_threads under ThreadSanitizer
make test-msan        # clang only
make test-valgrind

# Run specific test suite
./build/test_ss_lib
```

Test programs: `tests/test_ss_lib.c` (main suite, including regression tests), `tests/test_simple.c`, `tests/test_memory.c` (tracking allocator: no leaks, allocator routing, zero heap in static mode), and `tests/test_threads.c` (pthread stress test).

### Writing Tests

- For bug fixes, write a failing test first, then fix the bug
- Add tests for new features
- Ensure edge cases are covered
- Test error conditions
- Verify thread safety if applicable

## Documentation

- Update README.md for user-facing changes
- Add inline documentation for new APIs
- Update examples if API changes
- Consider adding to docs/ for complex features

## Performance Considerations

For embedded systems focus:
- Do not add heap use to the static-memory paths (register, connect, emit, deferred, namespace, disconnect, unregister, cleanup); `tests/test_memory.c` checks this
- Minimize dynamic allocations
- Consider static memory options
- Profile changes on target hardware
- Document resource usage

## Platform Support

Ensure changes work on:
- Linux (GCC, Clang)
- Windows (MinGW, MSVC)
- macOS (Clang)
- Embedded (ARM GCC, AVR GCC)

## Questions?

Feel free to:
- Open an issue for discussion
- Ask in pull request comments
- Check existing documentation

Thank you for contributing!