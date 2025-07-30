# API Reference

## Overview

SS_Lib provides a clean, efficient API for signal-slot programming in C. This reference covers all public functions, data types, and configuration options.

## Core Concepts

### Signals
A signal is a named event that can be emitted. When emitted, all connected slots (callback functions) are invoked.

### Slots
A slot is a callback function that gets invoked when its connected signal is emitted. Multiple slots can be connected to a single signal.

### Connection Handles
Connection handles provide an easy way to disconnect slots without needing to remember the function pointer.

## Initialization

### ss_init()
```c
ss_error_t ss_init(void);
```
Initialize the library with dynamic memory allocation.

**Returns:** `SS_OK` on success, error code on failure.

### ss_init_static()
```c
ss_error_t ss_init_static(void* memory_pool, size_t pool_size);
```
Initialize the library with a static memory pool (for embedded systems).

**Parameters:**
- `memory_pool`: Pointer to pre-allocated memory
- `pool_size`: Size of the memory pool in bytes

**Returns:** `SS_OK` on success, error code on failure.

### ss_cleanup()
```c
void ss_cleanup(void);
```
Clean up all resources and reset the library.

## Signal Management

### ss_signal_register()
```c
ss_error_t ss_signal_register(const char* signal_name);
```
Register a new signal.

**Parameters:**
- `signal_name`: Name of the signal (must be unique)

**Returns:** `SS_OK` on success, `SS_ERR_ALREADY_EXISTS` if signal exists.

### ss_signal_register_ex()
```c
ss_error_t ss_signal_register_ex(const char* signal_name, 
                                const char* description,
                                ss_priority_t priority);
```
Register a signal with additional metadata.

**Parameters:**
- `signal_name`: Name of the signal
- `description`: Human-readable description
- `priority`: Default priority for the signal

### ss_signal_exists()
```c
int ss_signal_exists(const char* signal_name);
```
Check if a signal exists.

**Returns:** 1 if exists, 0 otherwise.

## Connection Management

### ss_connect()
```c
ss_error_t ss_connect(const char* signal_name, 
                     ss_slot_func_t slot, 
                     void* user_data);
```
Connect a slot to a signal.

**Parameters:**
- `signal_name`: Name of the signal
- `slot`: Callback function
- `user_data`: User data passed to the callback

### ss_connect_ex()
```c
ss_error_t ss_connect_ex(const char* signal_name, 
                        ss_slot_func_t slot,
                        void* user_data, 
                        ss_priority_t priority,
                        ss_connection_t* handle);
```
Connect with priority and get a connection handle.

**Parameters:**
- `priority`: Execution priority (higher = earlier)
- `handle`: Output connection handle for easy disconnection

### ss_disconnect()
```c
ss_error_t ss_disconnect(const char* signal_name, 
                        ss_slot_func_t slot);
```
Disconnect a specific slot from a signal.

### ss_disconnect_handle()
```c
ss_error_t ss_disconnect_handle(ss_connection_t handle);
```
Disconnect using a connection handle.

## Signal Emission

### ss_emit()
```c
ss_error_t ss_emit(const char* signal_name, 
                  const ss_data_t* data);
```
Emit a signal with custom data.

### ss_emit_void()
```c
ss_error_t ss_emit_void(const char* signal_name);
```
Emit a signal without data.

### ss_emit_int()
```c
ss_error_t ss_emit_int(const char* signal_name, int value);
```
Emit a signal with an integer value.

### ss_emit_from_isr()
```c
ss_error_t ss_emit_from_isr(const char* signal_name, int value);
```
ISR-safe signal emission (requires `SS_ENABLE_ISR_SAFE`).

## Data Types

### ss_data_t
```c
typedef struct ss_data {
    ss_data_type_t type;
    union {
        int i_val;
        float f_val;
        double d_val;
        char* s_val;
        void* p_val;
    } value;
    // Additional fields if SS_ENABLE_CUSTOM_DATA
} ss_data_t;
```

### Error Codes
```c
typedef enum {
    SS_OK = 0,
    SS_ERR_NULL_PARAM,
    SS_ERR_MEMORY,
    SS_ERR_NOT_FOUND,
    SS_ERR_ALREADY_EXISTS,
    // ... more codes
} ss_error_t;
```

### Priority Levels
```c
typedef enum {
    SS_PRIORITY_LOW = 0,
    SS_PRIORITY_NORMAL = 5,
    SS_PRIORITY_HIGH = 10,
    SS_PRIORITY_CRITICAL = 15
} ss_priority_t;
```

## Configuration Macros

| Macro | Default | Description |
|-------|---------|-------------|
| `SS_USE_STATIC_MEMORY` | 0 | Use static memory allocation |
| `SS_MAX_SIGNALS` | 32 | Maximum signals (static mode) |
| `SS_MAX_SLOTS` | 128 | Maximum slots (static mode) |
| `SS_ENABLE_THREAD_SAFETY` | 1 | Enable mutex protection |
| `SS_ENABLE_ISR_SAFE` | 0 | Enable ISR-safe operations |
| `SS_ENABLE_PERFORMANCE_STATS` | 0 | Enable timing statistics |
| `SS_MINIMAL_BUILD` | undefined | Strip all optional features |

## Thread Safety

When `SS_ENABLE_THREAD_SAFETY` is enabled:
- All API functions are thread-safe
- Signals can be emitted from multiple threads
- Connections can be modified during emission
- Performance impact is minimal (~10-20ns per call)

## Memory Management

### Dynamic Mode (Default)
- Uses malloc/free
- No limits on signals or slots
- Suitable for desktop/server applications

### Static Mode
- Pre-allocated memory pool
- Fixed maximum signals/slots
- Zero dynamic allocation after init
- Ideal for embedded systems

## Best Practices

1. **Initialize Early**: Call `ss_init()` before any other library calls
2. **Check Returns**: Always check error codes, especially in embedded systems
3. **Use Handles**: Prefer connection handles for dynamic connections
4. **Priority Wisely**: Use priorities to ensure critical handlers run first
5. **Clean Up**: Always call `ss_cleanup()` before program exit