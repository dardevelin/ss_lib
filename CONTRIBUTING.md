# Contributing to SS_Lib

Thank you for your interest in contributing to SS_Lib! This document provides guidelines and instructions for contributing.

## Code of Conduct

By participating in this project, you agree to abide by our [Code of Conduct](CODE_OF_CONDUCT.md).

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
3. Make your changes
4. Add/update tests as needed
5. Update documentation
6. Run tests locally
7. Commit with clear messages
8. Push to your fork
9. Open a Pull Request

## Development Setup

```bash
# Clone your fork
git clone https://github.com/YOUR-USERNAME/ss_lib.git
cd ss_lib

# Build the library
make clean && make all_v2

# Run tests
make test
make test_embedded

# Build documentation
make docs
```

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
# Run all tests
make test_all

# Run specific test suite
./test_ss_lib

# Run with valgrind (memory checks)
valgrind --leak-check=full ./test_ss_lib
```

### Writing Tests

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