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
- Signals, slots, descriptions, namespace and deferred entries live in fixed arrays inside each bus, sized at compile time
- Bus creation, register, connect, emit, deferred, ISR delivery, namespace, disconnect, unregister and bus destruction never touch the heap
- Only functions that hand the caller an allocated object (`ss_data_create`, `ss_data_set_string`/`_custom`, `ss_get_signal_list`, `ss_batch_create`) allocate
- Fixed limits are reported as `SS_ERR_WOULD_OVERFLOW`, never silently truncated
- Deterministic memory usage, suitable for embedded systems

#### Bus Placement
- `ss_bus_create()` puts the bus on the heap (dynamic) or takes one of `SS_STATIC_BUSES` built-in buses in static storage (static; never the heap)
- `ss_bus_create_in(pool, size)` puts it in a caller-owned pool, in either mode; a NULL or too-small pool is an error, not a fallback

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
- One recursive mutex per bus, created only for a bus made with `SS_BUS_THREAD_SAFE`; a bus without it has no mutex and no locking cost
- Held while slots run, so emission order is consistent; recursion lets slots call back into the same bus
- Every function that touches the bus's state locks; `ss_bus_destroy` is not thread-safe with respect to other users of the bus
- Buses share no state, so threads on separate buses never contend
- Fine-grained and read-write locking rejected for complexity and embedded overhead
- Cost: a slot must not wait for another thread that is itself calling its bus, and cross-bus calls from slots must go in one direction only

### Re-entrancy

Slots may call any library function, including on the signal being emitted:
- Every public call goes through one entry/exit pair that tracks nesting depth
- Disconnect and unregister mark objects and free them when the emission ends
- A slot connected during an emission of the same signal may or may not be called by that emission (it depends on its priority position); callers must not rely on either
- `ss_bus_destroy` from a slot is deferred until the bus's outermost call returns
- Queued emissions are popped before they are emitted, so slots can queue more

This keeps callback code free of "not from a slot" rules, at the cost of a small amount of bookkeeping per call.

### ISR Safety

ISR-safe mode provides:
- `ss_emit_from_isr(bus, ...)`: no locks, no allocation, no slot calls; entries claimed with atomic compare-and-swap
- A fixed queue of `SS_ISR_QUEUE_SIZE` int events in each bus
- Delivery from normal context by `ss_process_isr_queue(bus)`, oldest first, of that bus's events only
- A critical-section hook (`SS_ISR_CRITICAL_ENTER/EXIT`) for cores without compare-and-swap

### Configuration Consistency (ABI Check)

In 2.x feature flags changed public types (`SS_ENABLE_CUSTOM_DATA` changed `ss_data_t`), so a program compiled with different flags than the library silently corrupted memory. Since 3.0 no flag changes a public layout or enum value (`make check-abi` enforces it); flags only gate function declarations, so a mismatch is a compile or link error. The bus creation macros `ss_bus_create()` / `ss_bus_create_in()` pass the caller's `SS_ABI_TAG` (`sizeof(ss_data_t)` and `SS_ABI_REVISION`) to the library, which returns `SS_ERR_CONFIG_MISMATCH` on a difference (a header from another version, or layout-changing compiler options). The check runs on every creation without the caller asking for it; in 2.x it was opt-in, because turning an existing function into a macro could break code that re-declares it. The create functions are new in 3.0, so they could be macros from the start. Configuration must be identical for the library and every caller.

### Version 3.0: the bus

2.x kept all state in one global context. 3.0 replaces it with an instance handle, `ss_bus_t*`, that every stateful function takes first; the names of the functions do not change. The reasons:

- Several independent buses in one program, for example per subsystem or per test, with no shared state between them.
- Tests in isolation: each test creates its own bus instead of resetting a global.
- Thread safety decided at creation (`SS_BUS_THREAD_SAFE`), per bus, instead of a runtime switch.
- The configuration check moves into bus creation, so it always runs.

Choices made along the way:

- The bus is opaque. Its layout depends on the configuration, so a public struct would bring back layout mismatches between library and callers.
- The library has no default bus. A default would bring back the global with its init-order and thread questions, and a failed creation could quietly run against it. A program that wants one global bus keeps the pointer in its own global.
- Slots keep their signature. A slot that needs its bus gets it through `user_data`; adding a bus parameter would break every slot in every program.
- Static mode keeps a small table of built-in buses (`SS_STATIC_BUSES`, default 1) so a one-bus program uses the same RAM as 2.x and never the heap. Each bus has its own signal and slot arrays, so one bus cannot starve another.
- Each bus has its own ISR queue, so `ss_emit_from_isr()` takes the bus. A global queue whose entries name their bus would keep the queue usable before any bus exists, but brings back global state: each drain would have to skip other buses' events, and a destroyed bus would leave orphan events. The cost is that the bus must exist before the interrupts that emit are enabled, and those interrupts must be disabled before the bus is destroyed.

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
- One registry per bus over per-thread registries
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

### Later Considerations
- Signal handles returned by registration, to emit without a name lookup
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