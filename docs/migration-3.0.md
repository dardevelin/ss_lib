# Migrating from 2.x to 3.0

SS_Lib 3.0 is in development on the `v3` branch. This guide grows with it:
each change that breaks 2.x code adds its section here, in the same pull
request. Sections without content describe changes that have not landed yet;
[3.0-plan.md](3.0-plan.md) has the design.

3.0 changes the API in ways the compiler reports: every call that must change
fails to compile until it is changed, and none silently changes meaning.

## Building and linking

- The shared library is `libss_lib.so.3` (Linux) or `libss_lib.3.dylib`
  (macOS). Rebuild and relink programs; a program built against 2.x keeps
  using `libss_lib.so.2`, and both can be installed side by side.
- With CMake, ask for the new major version: `find_package(ss_lib 3 REQUIRED)`.
  A request for 2 does not accept 3.0.
- pkg-config keeps the name `ss_lib`, and headers stay in `include/ss_lib/`.

## Creating and destroying a bus

Not landed yet: `ss_init()` and `ss_cleanup()` become `ss_bus_create()` and
`ss_bus_destroy()`.

## The bus argument

Not landed yet: every function that uses library state takes the bus as its
first argument.

## Thread safety

Not landed yet: `ss_set_thread_safe(1)` becomes the `SS_BUS_THREAD_SAFE` flag
of `ss_bus_create()`.

## Error handler

Not landed yet: the handler receives the bus and a user pointer.

## Batches

Not landed yet: `ss_batch_create(bus)`.

## ISR queue

Not landed yet: `ss_emit_from_isr()` and `ss_process_isr_queue()` take the bus.

## Data ownership

Not landed yet: `ss_data_t` records whether it owns its payload.

## Configuration

Not landed yet: `SS_STATIC_CONTEXT` becomes `SS_STATIC_BUSES`, and no
configuration macro changes a public type layout.
