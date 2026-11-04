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
find_package(ss_lib 2 REQUIRED)
target_link_libraries(myapp ss_lib::ss_lib)
```

## Basic Concepts

### Signals
- Named events that can be emitted
- Can carry data of various types (int, float, string, pointer, custom)
- Must be registered before use

### Slots
- Callback functions that respond to signals
- Can have user-defined context data
- Multiple slots can connect to one signal
- Execute in priority order
- May call any library function, including disconnecting themselves or unregistering the signal being emitted

### Connections
- Links between signals and slots
- Can be created and destroyed at runtime
- Support priorities and handles for easy management

## Your First Program

```c
#include "ss_lib.h"
#include <stdio.h>

void on_button_click(const ss_data_t* data, void* user_data) {
    printf("Button clicked!\n");
}

int main() {
    /* Initialize the library. ss_init() also works; ss_init_abi()
     * additionally detects a library built with different flags. */
    if (ss_init_abi(SS_ABI_TAG) != SS_OK) {
        return 1;
    }

    /* Register a signal */
    ss_signal_register("button_clicked");

    /* Connect the slot */
    ss_connect("button_clicked", on_button_click, NULL);

    /* Emit the signal */
    ss_emit_void("button_clicked");

    /* Clean up */
    ss_cleanup();

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
    printf("Temperature: %d\n", temp);
}

int main() {
    ss_init();

    ss_signal_register("temp_changed");
    ss_connect("temp_changed", on_temperature_changed, NULL);

    /* Emit with integer data */
    ss_emit_int("temp_changed", 25);

    ss_cleanup();
    return 0;
}
```

`ss_emit_string()` and the other `ss_emit_*` helpers pass borrowed data: the slot must not free it, and the string only has to stay valid during the call. Deferred and batched emissions store their own copy. An `ss_data_t` from `ss_data_create()` owns the string or custom data set on it and is freed with `ss_data_destroy()`.

## Using Connection Handles

Connection handles make it easy to disconnect slots without remembering the function pointer:

```c
ss_connection_t handle;
ss_connect_ex("event", my_handler, NULL, SS_PRIORITY_NORMAL, &handle);

/* Later, disconnect by handle */
ss_disconnect_handle(handle);
```

## Priority-Based Execution

Slots with higher priority execute first:

```c
ss_connect_ex("alarm", critical_handler, NULL, SS_PRIORITY_CRITICAL, NULL);
ss_connect_ex("alarm", normal_handler, NULL, SS_PRIORITY_NORMAL, NULL);
ss_connect_ex("alarm", log_handler, NULL, SS_PRIORITY_LOW, NULL);

/* When emitted: critical_handler -> normal_handler -> log_handler */
ss_emit_void("alarm");
```

## Error Handling

All functions return `ss_error_t`. Always check return values:

```c
ss_error_t err = ss_signal_register("my_signal");
if (err != SS_OK) {
    printf("Error: %s\n", ss_error_string(err));
}
```

You can also set a global error handler (after `ss_init()`; `ss_cleanup()` resets it). It is called for every failing call, including parameter errors, outside the library's lock; `msg` is the signal name when there is one, otherwise the error string:

```c
void my_error_handler(ss_error_t error, const char* msg) {
    fprintf(stderr, "SS_Lib error %s: %s\n", ss_error_string(error), msg);
}

ss_set_error_handler(my_error_handler);
```

## Next Steps

- [Configuration Guide](configuration.md) — Customize for your platform
- [API Reference](api-reference.md) — Detailed function documentation
- [Embedded Guide](embedded-guide.md) — Static memory and ISR-safe usage
- [Examples](examples.md) — More complex usage patterns
