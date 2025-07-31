# SS_Lib - Production-Ready Signal-Slot Library for C

[![Build](https://github.com/dardevelin/ss_lib/actions/workflows/build.yml/badge.svg)](https://github.com/dardevelin/ss_lib/actions/workflows/build.yml)
[![Benchmarks](https://github.com/dardevelin/ss_lib/actions/workflows/benchmarks.yml/badge.svg)](https://github.com/dardevelin/ss_lib/actions/workflows/benchmarks.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A lightweight, efficient signal-slot implementation designed for embedded systems, game engines, and resource-constrained environments.

## 🎯 Key Features

- **Zero Dependencies** - Pure ANSI C, no external libraries
- **Static Memory Option** - No malloc/free at runtime for embedded systems  
- **Thread-Safe** - Optional mutex protection
- **ISR-Safe** - Emit signals from interrupt handlers
- **Configurable** - Compile-time feature flags
- **Single Header** - Easy integration option
- **Performance Profiling** - Built-in timing statistics
- **Memory Diagnostics** - Track usage in real-time

## 🚀 Quick Start

### Basic Usage

```c
#include "ss_lib_v2.h"

void on_button_click(const ss_data_t* data, void* user_data) {
    printf("Button clicked!\n");
}

int main() {
    ss_init();
    
    ss_signal_register("button_click");
    ss_connect("button_click", on_button_click, NULL);
    
    ss_emit_void("button_click");
    
    ss_cleanup();
    return 0;
}
```

### Embedded System (Static Memory)

```c
// Configure for embedded use
#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 16
#define SS_MAX_SLOTS 32
#define SS_ENABLE_THREAD_SAFETY 0

#include "ss_lib_v2.h"

// In your ISR
void ADC_ISR(void) {
    uint16_t value = ADC->DATA;
    ss_emit_from_isr("adc_ready", value);  // Lock-free, no malloc
}
```

## 📊 Who Should Use This?

### ✅ Perfect For:

1. **Embedded Systems** (MCUs, RTOSes)
   - Static allocation, no heap fragmentation
   - ISR-safe operations
   - Minimal footprint (~2KB)

2. **Game Engines** 
   - Performance profiling per signal
   - Priority-based slot execution
   - Zero-allocation emit path

3. **IoT/Edge Devices**
   - Configurable features
   - Memory-constrained operation
   - Power-efficient design

4. **Legacy C Projects**
   - No C++ required
   - Stable ABI
   - Easy integration

### ❌ Not Ideal For:

- Large C++ applications (use Qt or Boost.Signals2)
- Web/JavaScript projects
- Safety-critical systems requiring formal verification

## 🔧 Configuration Options

### Compile-Time Flags

```c
// Memory model
#define SS_USE_STATIC_MEMORY 1      // Use static allocation
#define SS_MAX_SIGNALS 32           // Maximum signals (static mode)
#define SS_MAX_SLOTS 128            // Maximum slots (static mode)

// Features
#define SS_ENABLE_THREAD_SAFETY 1   // Mutex protection
#define SS_ENABLE_INTROSPECTION 1   // Signal listing/discovery
#define SS_ENABLE_CUSTOM_DATA 1     // Custom data types
#define SS_ENABLE_PERFORMANCE_STATS 1 // Timing statistics
#define SS_ENABLE_MEMORY_STATS 1    // Memory tracking
#define SS_ENABLE_ISR_SAFE 1        // ISR-safe operations

// Minimal build for tiny systems
#define SS_MINIMAL_BUILD            // Strips all optional features
```

### Platform Examples

#### Arduino/AVR (8-bit MCU)
```c
#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 8
#define SS_MAX_SLOTS 16
#define SS_MINIMAL_BUILD
```

#### STM32/ARM Cortex-M
```c
#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 32
#define SS_MAX_SLOTS 64
#define SS_ENABLE_ISR_SAFE 1
#define SS_ENABLE_THREAD_SAFETY 0  // Using RTOS instead
```

#### Linux Embedded
```c
#define SS_USE_STATIC_MEMORY 0      // Use dynamic allocation
#define SS_ENABLE_THREAD_SAFETY 1
#define SS_ENABLE_PERFORMANCE_STATS 1
```

## 📈 Performance Comparison

| Feature | SS_Lib v2 | Qt Signals | libsigc++ | GObject |
|---------|-----------|------------|-----------|---------|
| Memory Model | Static/Dynamic | Dynamic | Dynamic | Dynamic |
| Minimum RAM | ~2KB | ~5MB | ~100KB | ~1MB |
| ISR-Safe | ✅ | ❌ | ❌ | ❌ |
| Thread-Safe | Optional | ✅ | ❌ | ✅ |
| Zero Dependencies | ✅ | ❌ | ✅ | ❌ |
| C Compatible | ✅ | ❌ | ❌ | ✅ |
| Static Allocation | ✅ | ❌ | ❌ | ❌ |

### Benchmark Results

Performance varies by platform and configuration. Here are typical results:

#### Apple M1 (macOS)
```
Signal emission (dynamic memory, thread-safe):
- Empty signal: 26 ns
- With 1 slot: 27 ns  
- With 5 slots: 27 ns
- With 10 slots: 27 ns

Basic operations:
- Signal registration: 1233 ns
- Slot connection: 1352 ns
- Signal lookup: 150 ns
- Disconnect by handle: 53 ns
```

#### x86_64 Linux (Intel i7 @ 3.5GHz)
```
Signal emission (dynamic memory, thread-safe):
- Empty signal: ~40-50 ns
- With 1 slot: ~70-80 ns  
- With 5 slots: ~200-250 ns
- With 10 slots: ~350-400 ns
```

#### STM32F4 @ 168MHz (Cortex-M4)
```
Signal emission (static memory, no thread safety):
- Empty signal: 89 ns
- With 1 slot: 124 ns  
- With 5 slots: 287 ns

Memory usage (16 signals, 32 slots):
- Total allocation: 2.1 KB
- Per signal overhead: 48 bytes
- Per slot overhead: 24 bytes
```

View the [latest benchmark results](https://github.com/dardevelin/ss_lib/actions/workflows/benchmarks.yml) or run benchmarks locally:
```bash
make benchmark        # Quick benchmark
make benchmark-all    # Comprehensive suite
```

## 🛠️ Advanced Features

### Connection Handles

```c
ss_connection_t conn;
ss_connect_ex("data_changed", on_data, NULL, SS_PRIORITY_HIGH, &conn);

// Later...
ss_disconnect_handle(conn);  // Easier than remembering function pointer
```

### Performance Profiling

```c
ss_enable_profiling(1);

// After running...
ss_perf_stats_t stats;
ss_get_perf_stats("render_frame", &stats);
printf("Avg time: %llu ns\n", stats.avg_time_ns);
```

### Memory Diagnostics

```c
ss_memory_stats_t mem;
ss_get_memory_stats(&mem);
printf("Signals: %zu/%zu\n", mem.signals_used, mem.signals_allocated);
printf("Slots: %zu/%zu\n", mem.slots_used, mem.slots_allocated);
```

### Signal Priorities

```c
// Critical handlers run first
ss_connect_ex("alarm", on_critical, NULL, SS_PRIORITY_CRITICAL, NULL);
ss_connect_ex("alarm", on_log, NULL, SS_PRIORITY_LOW, NULL);
```

## 🏗️ Building

### Standard Build
```bash
make          # Build library and tests
make examples # Build example programs
make test     # Run tests
```

### Embedded Build
```bash
# Configure in ss_config.h or via compiler flags
gcc -DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=16 -c src/ss_lib_v2.c -Iinclude

# Or use make with flags
make CFLAGS="-DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=16"
```

### Single Header
```bash
./create_single_header.sh
# Creates ss_lib_single.h

# In your code:
#define SS_IMPLEMENTATION
#include "ss_lib_single.h"
```

## 📖 API Reference

### Core Functions

| Function | Description |
|----------|-------------|
| `ss_init()` | Initialize library |
| `ss_init_static(buffer, size)` | Init with custom memory pool |
| `ss_cleanup()` | Clean up all resources |

### Signal Management

| Function | Description |
|----------|-------------|
| `ss_signal_register(name)` | Register a signal |
| `ss_signal_register_ex(name, desc, priority)` | Register with metadata |
| `ss_connect(signal, func, data)` | Connect slot to signal |
| `ss_connect_ex(...)` | Connect with priority & handle |
| `ss_disconnect(signal, func)` | Disconnect specific slot |
| `ss_disconnect_handle(handle)` | Disconnect using handle |
| `ss_disconnect_all(signal)` | Disconnect all slots |
| `ss_signal_exists(name)` | Check if signal exists |

### Signal Emission

| Function | Description |
|----------|-------------|
| `ss_emit(signal, data)` | Emit with custom data |
| `ss_emit_void(signal)` | Emit without data |
| `ss_emit_int(signal, value)` | Emit integer |
| `ss_emit_float(signal, value)` | Emit float |
| `ss_emit_double(signal, value)` | Emit double |
| `ss_emit_string(signal, str)` | Emit string |
| `ss_emit_pointer(signal, ptr)` | Emit pointer |
| `ss_emit_from_isr(signal, value)` | ISR-safe emission |

## 🔄 Migration from V1

### Key Differences

1. **Header Files**
   ```c
   // V1
   #include "ss_lib.h"
   
   // V2
   #include "ss_lib_v2.h"
   ```

2. **Configuration**
   ```c
   // V2: Configure before including
   #define SS_USE_STATIC_MEMORY 1
   #include "ss_lib_v2.h"
   ```

3. **New Features**
   - Connection handles
   - Signal priorities  
   - Performance stats
   - ISR-safe operations

### Compatibility

V2 maintains API compatibility for basic operations. Existing V1 code should work with minimal changes.

## 📝 Examples

### Temperature Monitor (Embedded)
```c
void on_temp_critical(const ss_data_t* data, void* user_data) {
    // Activate cooling, log event, etc.
}

// In main loop
if (read_temperature() > THRESHOLD) {
    ss_emit_void("temp_critical");
}
```

### Game Engine Events
```c
// Register with priority
ss_signal_register_ex("entity_damaged", "Entity takes damage", SS_PRIORITY_HIGH);

// Multiple handlers
ss_connect("entity_damaged", update_health_bar, NULL);
ss_connect("entity_damaged", play_hurt_sound, NULL);  
ss_connect("entity_damaged", check_death, NULL);

// Emit with data
ss_emit_pointer("entity_damaged", entity);
```

### Plugin System
```c
// Plugins can connect without modifying core
void plugin_init() {
    ss_connect("file_saved", my_backup_function, NULL);
    ss_connect("app_closing", cleanup_plugin, NULL);
}
```

## 🐛 Debugging & Testing

### Enable Debug Traces
```c
#define SS_ENABLE_DEBUG_TRACE 1
#include "ss_lib_v2.h"

ss_enable_trace(stderr);
```

Output:
```
[SS_TRACE] Signal registered: button_click
[SS_TRACE] Connected slot to signal: button_click (handle: 1)
[SS_TRACE] Emitting signal: button_click to 1 slots
```

### Run Tests with Sanitizers
```bash
make test-asan     # AddressSanitizer
make test-tsan     # ThreadSanitizer  
make test-ubsan    # UndefinedBehaviorSanitizer
make test-valgrind # Valgrind memory check
make coverage      # Generate coverage report
```

## 📄 License

MIT License - See LICENSE file for details

## 🤝 Contributing

1. Fork the repository
2. Create your feature branch
3. Ensure tests pass
4. Submit pull request

## 🔗 See Also

- [Design Rationale](docs/design.md)
- [API Reference](docs/api-reference.md)
- [Getting Started Guide](docs/getting-started.md)
- [Benchmarks](https://github.com/dardevelin/ss_lib/actions/workflows/benchmarks.yml)

## 📦 Installation

### Using pkg-config
```bash
sudo make install
pkg-config --cflags --libs ss_lib
```

### Using CMake
```cmake
find_package(ss_lib REQUIRED)
target_link_libraries(your_app ss_lib::ss_lib)
```

### Using Homebrew (macOS)
```bash
brew tap dardevelin/ss-lib
brew install ss-lib
```

### Single Header
```bash
./create_single_header.sh
# Copy ss_lib_single.h to your project
```