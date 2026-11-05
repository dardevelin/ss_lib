# SS_Lib Documentation

Welcome to the SS_Lib documentation. This directory contains detailed information about using and extending the signal-slot library. It describes version 2.2.4; see the [Changelog](../CHANGELOG.md) for changes from earlier versions.

## Documentation Structure

- [Getting Started](getting-started.md) — Quick introduction and setup
- [API Reference](api-reference.md) — Complete API documentation
- [Configuration Guide](configuration.md) — Compile-time options explained
- [Embedded Guide](embedded-guide.md) — Guide for embedded systems
- [Performance Tuning](performance.md) — Profiling and optimization
- [Examples](examples.md) — Detailed example walkthroughs
- [Architecture](architecture.md) — Internal design documentation
- [Design Rationale](design.md) — Design decisions and trade-offs

## Quick Links

- [Main README](../README.md)
- [Changelog](../CHANGELOG.md)
- [Security policy](../SECURITY.md)
- [License](../LICENSE)

## HTML API Documentation (optional)

The documentation here is Markdown and needs no tool. If [Doxygen](https://www.doxygen.nl) is installed, `make docs` also generates HTML API documentation from the headers and these pages into `docs/api/html/` (not tracked by git). Nothing else needs Doxygen: building, testing and installing SS_Lib do not, and without it `make docs` prints a note and stops.

```bash
make docs
```
