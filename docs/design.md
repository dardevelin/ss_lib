# Design Rationale

## Overview

SS_Lib was designed from the ground up to be a production-ready signal-slot library for C that can work in constrained environments while still providing features needed for complex applications.

## Design Goals

1. **Zero Dependencies**: Pure ANSI C with no external dependencies
2. **Embedded-Friendly**: Support for static memory allocation and ISR-safe operations
3. **Performance**: Minimal overhead for signal emission
4. **Flexibility**: Configurable features via compile-time flags
5. **Safety**: Thread-safe operations with optional mutex protection
6. **Simplicity**: Clean, intuitive API

## Architecture

### Memory Model

SS_Lib supports two memory models:

#### Dynamic Memory (Default)
- Uses standard malloc/free
- No predetermined limits
- Grows as needed
- Suitable for desktop/server applications

#### Static Memory
- Signals, slots, descriptions, namespace and deferred entries live in fixed arrays sized at compile time
- Register, connect, emit, deferred, namespace, disconnect, unregister and cleanup never touch the heap
- Only functions that hand the caller an allocated object (`ss_data_create`, `ss_data_set_string`/`_custom`, `ss_get_signal_list`, `ss_batch_create`) allocate
- Fixed limits are reported as `SS_ERR_WOULD_OVERFLOW`, never silently truncated
- Deterministic memory usage, suitable for embedded systems

#### Context Placement
- `ss_init()` puts the context on the heap (dynamic) or in static storage (static; `SS_STATIC_CONTEXT=0` drops that storage and uses the heap)
- `ss_init_static(pool, size)` puts it in a caller-owned pool, in either mode; a NULL or too-small pool falls back to `ss_init()` for 2.1 compatibility

### Data Structures

#### Signal Registry
- Linear lookup with `strcmp`: a singly-linked list (dynamic) or a fixed array (static)
- No hash table: a linear scan keeps code and RAM small and is adequate for small signal counts
- Lookup cost grows with the number of registered signals

#### Slot Lists
- Singly-linked, sorted by priority at connection time (equal priorities keep connection order)
- Removal during emission only marks the slot; it is freed after the emission (see [architecture](architecture.md))
- Connection handles are monotonically increasing integers stored in the slot

### Thread Safety

When enabled, thread safety is implemented using:
- One recursive mutex per context, created by `ss_init`, used once `ss_set_thread_safe(1)` is called
- Held while slots run, so emission order is consistent; recursion lets slots call back into the library
- Every function that touches shared state locks; `ss_init`/`ss_cleanup` are not thread-safe
- Fine-grained and read-write locking rejected for complexity and embedded overhead
- Cost: a slot must not wait for another thread that is itself calling the library

### Re-entrancy

Slots may call any library function, including on the signal being emitted:
- Every public call goes through one entry/exit pair that tracks nesting depth
- Disconnect and unregister mark objects and free them when the emission ends
- A slot connected during an emission of the same signal may or may not be called by that emission (it depends on its priority position); callers must not rely on either
- `ss_cleanup` from a slot is deferred until the outermost call returns
- Queued emissions are popped before they are emitted, so slots can queue more

This keeps callback code free of "not from a slot" rules, at the cost of a small amount of bookkeeping per call.

### ISR Safety

ISR-safe mode provides:
- `ss_emit_from_isr`: no locks, no allocation, no slot calls; entries claimed with atomic compare-and-swap
- A fixed queue of `SS_ISR_QUEUE_SIZE` int events
- Delivery from normal context by `ss_process_isr_queue()`, oldest first
- A critical-section hook (`SS_ISR_CRITICAL_ENTER/EXIT`) for cores without compare-and-swap

### Configuration Consistency (ABI Check)

Feature flags change public types (`SS_ENABLE_CUSTOM_DATA` changes `ss_data_t`). A program compiled with different flags than the library would silently corrupt memory. `ss_init_abi(SS_ABI_TAG)` / `ss_init_static_abi(pool, size, SS_ABI_TAG)` pass the caller's `sizeof(ss_data_t)` to the library, which returns `SS_ERR_CONFIG_MISMATCH` on a difference; they are the recommended initialization for new code. The check is opt-in because turning `ss_init()` into a macro could break existing code that re-declares it, which a minor release must not do. Configuration must be identical for the library and every caller.

## Performance Considerations

### Signal Emission
- Direct function calls (no virtual dispatch)
- Minimal indirection
- Priority sorting happens at connection time, not emission

### Memory Allocation
- Fixed arrays in static mode, individual `SS_MALLOC`/`SS_CALLOC` allocations in dynamic mode
- All allocation routed through `SS_MALLOC`/`SS_FREE`/`SS_CALLOC`
- Memory statistics for profiling

### String Handling
- Names are compared with `strcmp` during lookup
- Names are copied once at registration (heap in dynamic mode, fixed-size buffers in static mode)
- Queued emissions (deferred, batch) copy their string/custom payloads so callers may free theirs at once

## Trade-offs

### Simplicity vs Features
- Chose simple mutex over complex locking schemes
- Single signal registry over per-thread registries
- Direct callbacks over message queues

### Performance vs Safety
- Names and payloads are length-checked and rejected (`SS_ERR_WOULD_OVERFLOW`) instead of truncated
- Re-entrancy bookkeeping and payload copies for queued emissions cost a little time per call
- Statistics gathering and tracing can be compiled out

### Flexibility vs Size
- Feature flags increase code complexity
- Single header option trades compilation time for ease of use
- Multiple API levels (basic vs extended)

## Comparison with Alternatives

### vs Qt Signals/Slots
- No code generation required
- Much smaller footprint
- C instead of C++
- Less type safety but more portable

### vs libsigc++
- Pure C implementation
- Static memory option
- Simpler API
- Better embedded support

### vs GObject
- No object system required
- Lighter weight
- Easier to integrate
- More focused scope

## Future Directions

### Version 3.0 Considerations
- An instance handle (`ss_bus_t*`) passed to every call instead of the global context, allowing several independent buses (breaking API change)
- Making the configuration (ABI) check the default for `ss_init()` / `ss_init_static()`
- Lock-free signal emission
- Type-safe signal definitions
- Coroutine support
- Network-transparent signals

### Potential Optimizations
- Faster signal lookup for large signal counts
- SIMD slot iteration
- JIT compilation for hot paths
- Hardware acceleration hooks

## Lessons Learned

### What Worked Well
- Compile-time configuration
- Separation of core and optional features
- Clean API design
- Comprehensive test suite

### Challenges
- Balancing features with simplicity
- Supporting diverse use cases
- Maintaining ABI stability
- Documentation maintenance

## Contributing

When contributing to SS_Lib, please consider:
- Maintain backward compatibility
- Add tests for new features
- Update documentation
- Consider embedded constraints
- Benchmark performance impacts