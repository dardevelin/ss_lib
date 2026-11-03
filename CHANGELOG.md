# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- `make install` on macOS: programs linked against the installed library failed
  to start with "Library not loaded: @rpath/libss_lib.dylib" (since 2.1). The
  installed dylib now carries its absolute install path as install name, and is
  named by major version like on Linux and in the CMake build:
  `libss_lib.dylib` -> `libss_lib.2.dylib` -> `libss_lib.2.2.0.dylib`.
- `make test-all` includes `check-install`, which installs into `build/`, runs
  a program against the installed shared library and checks `make uninstall`.

### Removed
- `benchmarks/requirements.txt` and the Dependabot updates for it: the project
  keeps zero external dependencies, including in its tooling. The benchmark
  scripts use only the Python standard library; `visualize.py` draws plots only
  when matplotlib is already installed.

## [2.2.0] - 2026-11-03

Security, robustness and maintenance release. Existing code keeps compiling;
public struct layouts are unchanged.

### Security
- Fixed a use-after-free when a slot unregisters its own signal or calls
  `ss_cleanup()`: the signal is retired and freed when its emission ends, and a
  cleanup requested from a slot completes when the outermost call returns.
- Fixed a use-after-free and double free in `ss_flush_deferred()` when a slot
  queues a deferred emission during the flush: entries are now popped from a
  FIFO ring before they are emitted, and a flush only emits the entries queued
  when it started, also across nested flushes.
- Regenerated `include/ss_lib_single.h`, which still contained the
  disconnect-during-emit use-after-free fixed in 2.1.0; CI now fails when it is stale.
- Thread safety: one recursive mutex created at `ss_init()` and used by every
  function that touches shared state (the deferred queue, namespace, error handler,
  profiling flag and slot limit were unlocked). Toggling it no longer creates or
  destroys the mutex.
- Fixed a deadlock on POSIX when a slot called back into the library with thread
  safety enabled (the mutex was not recursive; Windows already was).
- Deferred and batched emissions copy custom data instead of keeping the
  caller's pointer.
- `ss_emit_from_isr()` claims queue entries atomically, so nested interrupts
  and concurrent producers cannot corrupt an entry.
- Over-long signal names are rejected with `SS_ERR_WOULD_OVERFLOW` by deferred,
  batch and ISR emission instead of being truncated (which could fire another signal).
- `ss_get_signal_list()` returns copies of descriptions instead of pointers into
  library storage.
- Static memory mode: `ss_cleanup()` no longer leaks pending deferred strings and
  the namespace, and unregistering clears the signal's description, priority and statistics.
- Windows performance timer no longer overflows after about 15 minutes of uptime.
- Strings are copied with `SS_MALLOC`: the default `SS_STRDUP` used libc
  `strdup`, so a custom `SS_FREE` received memory it never allocated.

### Added
- `ss_process_isr_queue()` delivers events queued by `ss_emit_from_isr()`
  (nothing delivered them before). Targets without compare-and-swap (Cortex-M0,
  AVR) can define `SS_ISR_CRITICAL_ENTER()`/`SS_ISR_CRITICAL_EXIT()`.
- `SS_VERSION_MAJOR`/`MINOR`/`PATCH`/`STRING` and `ss_version()`.
- ABI check: `ss_init_abi(SS_ABI_TAG)` and `ss_init_static_abi(pool, size,
  SS_ABI_TAG)` return the new `SS_ERR_CONFIG_MISMATCH` when the caller was built
  with a different `ss_data_t` layout than the library. Recommended for new code.
- `ss_get_required_pool_size()` for `ss_init_static()`.
- Configuration: `SS_MAX_DESCRIPTION_LENGTH`, `SS_DEFERRED_PAYLOAD_SIZE` and
  `SS_STATIC_CONTEXT` (static mode), `SS_BATCH_MAX_ENTRIES`.
- Tests: re-entrancy regressions, allocation accounting (leaks, allocator
  routing, zero heap in static mode), a pthread stress test for ThreadSanitizer.
  Make targets `test-static`, `test-minimal`, `test-c89`, `test-newest` (the
  suite built as the newest C standard the compiler supports), `check-standards`
  (library, a caller of `ss_lib.h` and the single header compile with `-Werror`
  as C89, C99, C11, C17, C23 and C2y), `test-all`, `check-single-header`,
  `check-version`.

### Changed
- `ss_init_static()` places the library context in the caller's pool when it
  holds at least `ss_get_required_pool_size()` bytes (it ignored its arguments
  before); a NULL or smaller pool still behaves like `ss_init()`.
- The default namespace set by `ss_set_namespace()` now applies: names without
  `::` resolve as `ns::name` in every function that takes a signal name (it was
  stored but had no effect). The prototype's parameter is renamed `ns`;
  callers are unaffected.
- Static memory mode uses no heap in register, connect, emit, deferred,
  namespace, disconnect, unregister and cleanup paths, as documented.
  Descriptions longer than `SS_MAX_DESCRIPTION_LENGTH` are rejected.
- Debug trace is off until `ss_enable_trace()` names a stream (it always
  printed to stderr and `ss_enable_trace()` had no effect).
- `ss_data_set_*` release the string or custom payload the object owned before
  (switching type leaked it) and keep the old value when allocation fails.
- The error handler runs, outside the lock, for every failing call made while
  the library is initialized, including parameter errors.
- `SS_EMBEDDED_BUILD` now compiles (the preset was applied after the defaults it
  controls) and defaults `SS_DEFERRED_QUEUE_SIZE` to 16.
- Memory statistics report the heap actually held and its peak.
- Build: the Makefile honours `CC` and builds library, tests, examples and
  benchmarks with the same `FEATURES`; CMake reads the version from the header,
  adds `SS_ENABLE_ISR_SAFE`, exports the feature definitions with the target and
  ships the missing `cmake/ss_libConfig.cmake.in`; `-DCMAKE_C_STANDARD` now
  overrides its default of C11. `ss_lib.pc` is generated, no longer committed,
  and its `Cflags` carry the library's feature definitions. Examples take their
  configuration from the command line.
- Benchmarks: `benchmarks/compare.py` (moved out of the workflow),
  `benchmarks/requirements.txt`; the ISR benchmark measures the enqueue path.
- CI: least-privilege permissions, SHA-pinned actions with Dependabot,
  sanitizers, C89 to C2y compile check, MSVC, CMake, Cortex-M0/M4 and
  Python 3.11–3.15 jobs.
- Releases: the GitHub release notes are this file's section for the tag
  (`scripts/release_notes.sh`); the release job checks the tag against the
  header and `main`, runs `make test-all`, and publishes the tagged source tree
  as `.tar.gz` and `.zip`, the single header and `SHA256SUMS`. Tags with a `-`
  (`v2.3.0-rc.1`) publish pre-releases.
- Shared library versioning: the major version is the ABI. On Linux the
  library is `libss_lib.so.2.2.0` with soname `libss_lib.so.2` (Makefile and
  CMake); on macOS the dylib records its current and compatibility versions.
  The CMake package accepts `find_package(ss_lib 2.x)` only for the same major
  version (it accepted any newer version).

### Deprecated
- The `SS_TRACE` macro in `ss_config.h`; the library no longer uses it.

### Documentation
- Corrected the description of signal lookup (linear scan, not a hash table)
  and of slot lists (singly linked).

## [2.1.1] - 2026-03-01

### Fixed
- Fixed commit author metadata across repository history

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

[Unreleased]: https://github.com/dardevelin/ss_lib/compare/v2.2.0...HEAD
[2.2.0]: https://github.com/dardevelin/ss_lib/compare/v2.1.1...v2.2.0
[2.1.1]: https://github.com/dardevelin/ss_lib/compare/v2.1.0...v2.1.1
[2.1.0]: https://github.com/dardevelin/ss_lib/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/dardevelin/ss_lib/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/dardevelin/ss_lib/releases/tag/v1.0.0