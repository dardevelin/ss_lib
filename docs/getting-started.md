# Getting Started with SS_Lib

## Installation

### Method 1: Single Header (Easiest)

1. Generate it with `make single-header` (writes `include/ss_lib_single.h`) or download it
2. In exactly one C file:

```c
#define SS_IMPLEMENTATION
#include "ss_lib_single.h"
```

3. Everywhere else, include it without `SS_IMPLEMENTATION`. Configuration macros must be identical in every file; pass them on the command line.

### Method 2: Traditional Library

1. Clone the repository:
```bash
git clone https://github.com/dardevelin/ss_lib.git
cd ss_lib
```

2. Build the library:
```bash
make lib
```

3. Include headers and link:
```c
#include "ss_lib.h"
```

```bash
gcc myapp.c -I/path/to/ss_lib/include -L/path/to/ss_lib/build -lss_lib -pthread
```

Compile your program with the same configuration flags as the library. `make lib` uses the Makefile's `FEATURES` (`make info` prints them); `make lib FEATURES="..."` changes them. See the [Configuration Guide](configuration.md).

### Method 3: System Install

```bash
sudo make install          # or: cmake --install, after a CMake build
# Then use pkg-config:
gcc myapp.c $(pkg-config --cflags --libs ss_lib)
```

Or, from CMake:

```cmake
find_package(ss_lib 3 REQUIRED)
target_link_libraries(myapp ss_lib::ss_lib)
```

## Basic Concepts

### Buses
- A bus holds signals, slots, the deferred queue and the settings
- Every function that uses library state takes the bus as its first argument
- Buses are independent: the same signal name on two buses names two signals
- The library has no default bus; a program that wants one global bus keeps it in a variable of its own

### Signals
- Named events that can be emitted
- Can carry data of various types (int, float, string, pointer, custom)
- Must be registered on a bus before use

### Slots
- Callback functions that respond to signals
- Can have user-defined context data (`user_data`); a slot that needs its bus gets it this way
- Multiple slots can connect to one signal
- Execute in priority order
- May call any library function, including disconnecting themselves, unregistering the signal being emitted or destroying the bus

### Connections
- Links between signals and slots
- Can be created and destroyed at runtime
- Support priorities and handles for easy management

## Your First Program

```c
#include "ss_lib.h"
#include <stdio.h>

void on_button_click(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
    printf("Button clicked!\n");
}

int main(void) {
    ss_bus_t* bus = NULL;

    /* Create a bus. The macro also checks that this program and the
     * library agree on the public layouts (SS_ERR_CONFIG_MISMATCH). */
    if (ss_bus_create(&bus, 0) != SS_OK) {
        return 1;
    }

    /* Register a signal */
    ss_signal_register(bus, "button_clicked");

    /* Connect the slot */
    ss_connect(bus, "button_clicked", on_button_click, NULL);

    /* Emit the signal */
    ss_emit_void(bus, "button_clicked");

    /* Release the bus and everything it holds */
    ss_bus_destroy(bus);

    return 0;
}
```

Build and run:
```bash
gcc -Iinclude myapp.c -Lbuild -lss_lib -pthread -o myapp
./myapp
```

## Passing Data

```c
void on_temperature_changed(const ss_data_t* data, void* user_data) {
    int temp = ss_data_get_int(data, 0);
    (void)user_data;
    printf("Temperature: %d\n", temp);
}

int main(void) {
    ss_bus_t* bus = NULL;
    if (ss_bus_create(&bus, 0) != SS_OK) return 1;

    ss_signal_register(bus, "temp_changed");
    ss_connect(bus, "temp_changed", on_temperature_changed, NULL);

    /* Emit with integer data */
    ss_emit_int(bus, "temp_changed", 25);

    ss_bus_destroy(bus);
    return 0;
}
```

`ss_emit_string()` and the other `ss_emit_*` helpers pass borrowed data: the slot must not free it, and the string only has to stay valid during the call. Deferred and batched emissions store their own copy. An `ss_data_t` owns its payload only when `ss_data_set_string()` or `ss_data_set_custom()` copied it there; release it with `ss_data_clear()` (or `ss_data_destroy()` for an object from `ss_data_create()`). The library never frees borrowed data, such as a pointer stored with `ss_data_set_string_ref()`:

```c
ss_data_t d = SS_DATA_INIT;
ss_data_set_string(&d, buffer);   /* copied: buffer may change afterwards */
ss_emit(bus, "status", &d);
ss_data_clear(&d);                /* frees the copy */
```

## A Slot That Uses Its Bus

Slots keep the 2.x signature. A slot that needs its bus receives it through `user_data`:

```c
void on_saved(const ss_data_t* data, void* user_data) {
    ss_bus_t* bus = (ss_bus_t*)user_data;
    (void)data;
    ss_emit_string(bus, "status", "saved");
}

ss_connect(bus, "file_saved", on_saved, bus);
```

## Using Connection Handles

Connection handles make it easy to disconnect slots without remembering the function pointer. A handle belongs to the bus it was made on:

```c
ss_connection_t handle;
ss_connect_ex(bus, "event", my_handler, NULL, SS_PRIORITY_NORMAL, &handle);

/* Later, disconnect by handle */
ss_disconnect_handle(bus, handle);
```

## Priority-Based Execution

Slots with higher priority execute first:

```c
ss_connect_ex(bus, "alarm", critical_handler, NULL, SS_PRIORITY_CRITICAL, NULL);
ss_connect_ex(bus, "alarm", normal_handler, NULL, SS_PRIORITY_NORMAL, NULL);
ss_connect_ex(bus, "alarm", log_handler, NULL, SS_PRIORITY_LOW, NULL);

/* When emitted: critical_handler -> normal_handler -> log_handler */
ss_emit_void(bus, "alarm");
```

## Threads

A bus is used from one thread at a time unless it is created with `SS_BUS_THREAD_SAFE` (the library must be built with `SS_ENABLE_THREAD_SAFETY`, the default; otherwise creation returns `SS_ERR_CONFIG_MISMATCH`). Such a bus has its own recursive mutex, held while its slots run, so slots may call back into the library:

```c
ss_bus_t* shared = NULL;
if (ss_bus_create(&shared, SS_BUS_THREAD_SAFE) != SS_OK) return 1;
```

Buses share no state, so threads that each use their own bus need no lock at all. A slot of one bus may call another bus, but such cross-bus calls must always go in the same direction (A to B, never also B to A): a slot holds its bus's lock while it calls the other one, and two threads calling in opposite directions can deadlock.

## Error Handling

All functions return `ss_error_t`. Always check return values:

```c
ss_error_t err = ss_signal_register(bus, "my_signal");
if (err != SS_OK) {
    printf("Error: %s\n", ss_error_string(err));
}
```

You can also set an error handler per bus. It is called for every failing call on that bus, including parameter errors, outside the bus's lock, with the failing bus, the error, a message and the `user_data` you gave when setting it; `msg` is the signal name when there is one, otherwise the error string. Calls given no bus (a NULL bus or batch) reach no handler, and a bus reports nothing once `ss_bus_destroy()` has been called on it:

```c
typedef struct {
    const char* name;
    int errors;
} error_log_t;

void my_error_handler(ss_bus_t* bus, ss_error_t error, const char* msg, void* user_data) {
    error_log_t* log = (error_log_t*)user_data;
    (void)bus;
    log->errors++;
    fprintf(stderr, "%s: %s: %s\n", log->name, ss_error_string(error), msg);
}

static error_log_t ui_log = { "ui", 0 };
ss_set_error_handler(bus, my_error_handler, &ui_log);
```

The handler may call the library, also on its bus, so it can for example emit an `"error"` signal; `ss_set_error_handler(bus, NULL, NULL)` removes it.

## Coming from 2.x

[Migrating from 2.x to 3.0](migration-3.0.md) lists every change, with a `sed` recipe for adding the bus argument.

## Next Steps

- [Configuration Guide](configuration.md) — Customize for your platform
- [API Reference](api-reference.md) — Detailed function documentation
- [Embedded Guide](embedded-guide.md) — Static memory and ISR-safe usage
- [Examples](examples.md) — More complex usage patterns
