# Getting Started with SS_Lib

## Installation

### Method 1: Single Header (Easiest)

1. Download `ss_lib_single.h`
2. Include in your project:

```c
#define SS_IMPLEMENTATION
#include "ss_lib_single.h"
```

### Method 2: Traditional Library

1. Clone the repository:
```bash
git clone https://github.com/yourusername/ss_lib.git
cd ss_lib
```

2. Build the library:
```bash
make lib_v2
```

3. Include headers and link:
```c
#include "ss_lib_v2.h"
```

```bash
gcc myapp.c -I/path/to/ss_lib/include -L/path/to/ss_lib -lsslib_v2
```

## Basic Concepts

### Signals
- Named events that can be emitted
- Can carry data of various types
- Must be registered before use

### Slots
- Functions that respond to signals
- Can have user-defined context data
- Multiple slots can connect to one signal

### Connections
- Links between signals and slots
- Can be created and destroyed at runtime
- Support priorities and handles

## Your First Program

```c
#include "ss_lib_v2.h"
#include <stdio.h>

// Define a slot function
void on_button_click(const ss_data_t* data, void* user_data) {
    printf("Button clicked!\n");
}

int main() {
    // Initialize the library
    ss_init();
    
    // Register a signal
    ss_signal_register("button_clicked");
    
    // Connect the slot
    ss_connect("button_clicked", on_button_click, NULL);
    
    // Emit the signal
    ss_emit_void("button_clicked");
    
    // Clean up
    ss_cleanup();
    
    return 0;
}
```

## Passing Data

```c
void on_temperature_changed(const ss_data_t* data, void* user_data) {
    int temp = ss_data_get_int(data, 0);
    printf("Temperature: %d°C\n", temp);
}

int main() {
    ss_init();
    
    ss_signal_register("temp_changed");
    ss_connect("temp_changed", on_temperature_changed, NULL);
    
    // Emit with integer data
    ss_emit_int("temp_changed", 25);
    
    ss_cleanup();
    return 0;
}
```

## Next Steps

- [Configuration Guide](configuration.md) - Customize for your platform
- [API Reference](api-reference.md) - Detailed function documentation
- [Examples](examples.md) - More complex usage patterns