# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.1.0] - 2026-02-27

### Added
- Namespace support for signal emission (`ss_set_namespace`, `ss_get_namespace`, `ss_emit_namespaced`)
- Deferred emission with configurable queue size (`ss_emit_deferred`, `ss_flush_deferred`)
- Batch operations for grouped signal emission (`ss_batch_create`, `ss_batch_destroy`, `ss_batch_add`, `ss_batch_emit`)
- Getter and setter functions (`ss_get_max_slots_per_signal`, `ss_set_error_handler`)
- Stats reset functions (`ss_reset_memory_stats`, `ss_reset_perf_stats`)
- Error handler with `report_error` support
- Signal name length validation (`SS_MAX_SIGNAL_NAME_LENGTH`)
- Custom data size validation for zero-size payloads
- ISR queue NULL check and portable write barrier
- Configurable ISR queue constants (`SS_ISR_QUEUE_SIZE`)
- C89-compatible implementation with full feature parity (`src/ss_lib_c89.c`)
- Complete documentation suite: architecture, configuration, embedded guide, performance, examples

### Changed
- String field in `ss_data_t` changed from `char*` to `const char*` for const-correctness
- Replaced `strncpy` with `ss_strscpy` for safer string handling
- `ss_get_signal_list` now returns duplicated names via `SS_STRDUP` instead of raw internal pointers
- Custom cleanup callbacks are now stored and invoked via `ss_data_t`

### Fixed
- Use-after-free in `ss_emit` when callbacks call `ss_disconnect` during iteration
- Static-mode slot chaining only running last-connected slot
- Priority-sorted insertion not applied during `ss_connect`
- Raw `free()` calls breaking custom allocator support (replaced with `SS_FREE()`)
- Static-mode `ss_disconnect_all` and `ss_disconnect_handle` leaking slots (missing `free_slot()`)
- `ss_cleanup` not freeing description strings in static memory mode
- Dead code block with commented-out `report_error()` removed
- `SS_STRDUP` failure silently ignored in `ss_data_set_string`
- Memory stats `string_bytes` counter accumulating without reset
- Peak memory calculation using wrong byte count
- Single-header generator stripping conditional include guards for platform headers

### Documentation
- Rewrote README with complete API overview and no emoji formatting
- Expanded API reference from ~30% to 100% coverage
- Fixed incorrect filenames in getting-started guide (`ss_lib_v2.h` → `ss_lib.h`)
- Created 5 new documentation files (architecture, configuration, embedded guide, performance, examples)
- Fixed broken links in docs index

### Maintenance
- Removed stale v2 references from CMakeLists.txt, Doxyfile, benchmarks, pkg-config files, CONTRIBUTING.md, test_simple.c
- Fixed C89 SS_TRACE macro compatibility (variadic macros are C99)
- Include docs/ directory in release archives

### Security
- Fixed use-after-free vulnerability in signal emission during slot disconnection
- Added ISR queue bounds checking and NULL signal name validation

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

[Unreleased]: https://github.com/dardevelin/ss_lib/compare/v2.1.0...HEAD
[2.1.0]: https://github.com/dardevelin/ss_lib/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/dardevelin/ss_lib/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/dardevelin/ss_lib/releases/tag/v1.0.0