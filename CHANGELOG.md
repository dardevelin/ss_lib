# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Initial public release
- Core signal-slot functionality
- Static memory allocation option for embedded systems
- Thread-safe operations (optional)
- ISR-safe signal emission
- Performance profiling support
- Memory usage diagnostics
- Single-header distribution option
- Comprehensive test suite
- Examples for embedded systems

### Changed
- N/A (initial release)

### Deprecated
- N/A (initial release)

### Removed
- N/A (initial release)

### Fixed
- N/A (initial release)

### Security
- N/A (initial release)

## [2.0.0] - 2024-01-XX

### Added
- Static memory allocation mode (`SS_USE_STATIC_MEMORY`)
- Compile-time configuration options
- ISR-safe operations (`ss_emit_from_isr`)
- Connection handles for easier disconnection
- Signal priorities
- Performance statistics API
- Memory statistics API
- Extended signal registration with metadata
- Debug trace support
- Platform-specific optimizations

### Changed
- Complete rewrite for embedded systems focus
- Modular feature system via compile flags
- Improved API consistency
- Better error reporting

## [1.0.0] - 2024-01-XX

### Added
- Basic signal-slot implementation
- Dynamic memory allocation
- Thread safety support
- Multiple data type support
- Signal introspection
- Basic examples and tests

[Unreleased]: https://github.com/username/ss_lib/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/username/ss_lib/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/username/ss_lib/releases/tag/v1.0.0