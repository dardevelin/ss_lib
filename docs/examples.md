# Examples

This document walks through the example programs included in the `examples/` directory and demonstrates additional usage patterns.

Every example creates a bus with `ss_bus_create()` and passes it as the first argument of every call that uses library state. The examples do not `#define` configuration macros in their source: configuration must be identical for the library and every caller, so it is passed on the command line (the Makefile's `FEATURES`).

## Included Examples

### example_usage.c

Basic demonstration of creating a bus, signal registration, connection, emission, and teardown:

```bash
make examples
./build/example_usage
```

Covers:
- `ss_bus_create()` / `ss_bus_destroy()`, and a second, independent bus
- A slot that gets its bus through `user_data` and emits on it
- `ss_signal_register()` / `ss_signal_unregister()` / `ss_connect()` / `ss_disconnect_all()`
- `ss_emit_void()` / `ss_emit_int()` / `ss_emit_string()` / `ss_emit_pointer()`
- Custom data with `ss_data_create()` / `ss_data_set_custom()` / `ss_data_destroy()`
- Introspection with `ss_get_signal_list()` / `ss_free_signal_list()`

### example_embedded.c

Demonstrates embedded usage with ISR-safe emission. It needs the embedded configuration (static memory, ISR, memory and performance stats) and fails to compile otherwise, so it is built by `make test_embedded`, not `make examples`:

```bash
make test_embedded
```

Covers:
- Queueing events from simulated ISRs with `ss_emit_from_isr()`
- Delivering them from the main loop with `ss_process_isr_queue()`
- Static memory with the `SS_EMBEDDED_BUILD` preset: the bus is the built-in one, and the example prints its size (`ss_bus_pool_size()`)
- Memory statistics and per-signal profiling

### example_embedded_simple.c

Minimal embedded example without ISR features. `make examples` builds it with the default (dynamic) configuration; `make test_embedded` builds and runs it against a library compiled with `SS_EMBEDDED_BUILD`:

```bash
make test_embedded
```

## Usage Patterns

### Temperature Monitor

A typical embedded pattern where sensors emit signals on threshold events:

```c
#include "ss_lib.h"
#include <stdio.h>

static void on_temp_warning(const ss_data_t* data, void* user_data) {
    int temp = ss_data_get_int(data, 0);
    printf("WARNING: Temperature is %d degrees\n", temp);
}

static void on_temp_critical(const ss_data_t* data, void* user_data) {
    int temp = ss_data_get_int(data, 0);
    printf("CRITICAL: Temperature is %d degrees! Shutting down.\n", temp);
}

int main(void) {
    ss_bus_t* bus = NULL;
    int current_temp;

    if (ss_bus_create(&bus, 0) != SS_OK) return 1;

    ss_signal_register(bus, "temp_warning");
    ss_signal_register(bus, "temp_critical");

    ss_connect(bus, "temp_warning", on_temp_warning, NULL);
    ss_connect(bus, "temp_critical", on_temp_critical, NULL);

    /* Simulated sensor loop */
    current_temp = 85;
    if (current_temp > 80) {
        ss_emit_int(bus, "temp_warning", current_temp);
    }

    current_temp = 105;
    if (current_temp > 100) {
        ss_emit_int(bus, "temp_critical", current_temp);
    }

    ss_bus_destroy(bus);
    return 0;
}
```

### Plugin System

Use signals to decouple a core application from plugins:

```c
#include "ss_lib.h"
#include <stdio.h>

/* Core application creates the bus and registers events */
static ss_bus_t* app_init(void) {
    ss_bus_t* bus = NULL;
    if (ss_bus_create(&bus, 0) != SS_OK) return NULL;
    ss_signal_register(bus, "file_opened");
    ss_signal_register(bus, "file_saved");
    ss_signal_register(bus, "app_closing");
    return bus;
}

/* Plugin connects to events without modifying core */
static void backup_on_save(const ss_data_t* data, void* user_data) {
    const char* filename = ss_data_get_string(data);
    (void)user_data;
    printf("Backup plugin: creating backup of %s\n", filename);
}

static void backup_on_close(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
    printf("Backup plugin: shutting down\n");
}

void backup_plugin_init(ss_bus_t* bus) {
    ss_connect(bus, "file_saved", backup_on_save, NULL);
    ss_connect(bus, "app_closing", backup_on_close, NULL);
}

/* Another plugin */
static void log_on_open(const ss_data_t* data, void* user_data) {
    const char* filename = ss_data_get_string(data);
    (void)user_data;
    printf("Log plugin: file opened: %s\n", filename);
}

void log_plugin_init(ss_bus_t* bus) {
    ss_connect(bus, "file_opened", log_on_open, NULL);
}

int main(void) {
    ss_bus_t* bus = app_init();
    if (!bus) return 1;

    backup_plugin_init(bus);
    log_plugin_init(bus);

    ss_emit_string(bus, "file_opened", "document.txt");
    ss_emit_string(bus, "file_saved", "document.txt");
    ss_emit_void(bus, "app_closing");

    ss_bus_destroy(bus);
    return 0;
}
```

### Game Engine Events

Priority-based signal handling for game entity damage:

```c
#include "ss_lib.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    int health;
    int armor;
    char name[32];
} entity_t;

static void check_death(const ss_data_t* data, void* user_data) {
    entity_t* e = (entity_t*)ss_data_get_pointer(data);
    if (e && e->health <= 0) {
        printf("[HIGH] %s has died!\n", e->name);
    }
}

static void apply_damage(const ss_data_t* data, void* user_data) {
    entity_t* e = (entity_t*)ss_data_get_pointer(data);
    int damage = *(int*)user_data;
    if (e) {
        e->health -= damage;
        printf("[CRITICAL] %s took %d damage, health: %d\n",
               e->name, damage, e->health);
    }
}

static void play_hurt_sound(const ss_data_t* data, void* user_data) {
    entity_t* e = (entity_t*)ss_data_get_pointer(data);
    if (e) {
        printf("[NORMAL] Playing hurt sound for %s\n", e->name);
    }
}

int main(void) {
    ss_bus_t* bus = NULL;
    entity_t player;
    int damage;

    if (ss_bus_create(&bus, 0) != SS_OK) return 1;

    player.health = 100;
    player.armor = 50;
    strcpy(player.name, "Player1");
    damage = 150;

    ss_signal_register_ex(bus, "entity_damaged", "Entity takes damage",
                          SS_PRIORITY_HIGH);

    /* Damage is applied first (highest priority), then the death check */
    ss_connect_ex(bus, "entity_damaged", apply_damage, &damage,
                  SS_PRIORITY_CRITICAL, NULL);
    ss_connect_ex(bus, "entity_damaged", check_death, NULL,
                  SS_PRIORITY_HIGH, NULL);
    ss_connect_ex(bus, "entity_damaged", play_hurt_sound, NULL,
                  SS_PRIORITY_NORMAL, NULL);

    ss_emit_pointer(bus, "entity_damaged", &player);

    ss_bus_destroy(bus);
    return 0;
}
```

### Namespace Organization

Organize signals by subsystem using namespaces:

```c
#include "ss_lib.h"
#include <stdio.h>

static void on_button(const ss_data_t* data, void* user_data) {
    printf("Button event received\n");
}

static void on_network_data(const ss_data_t* data, void* user_data) {
    printf("Network data received\n");
}

int main(void) {
    ss_bus_t* bus = NULL;
    if (ss_bus_create(&bus, 0) != SS_OK) return 1;

    /* Register namespaced signals */
    ss_signal_register(bus, "ui::button_click");
    ss_signal_register(bus, "ui::slider_change");
    ss_signal_register(bus, "net::data_received");
    ss_signal_register(bus, "net::connection_lost");

    ss_connect(bus, "ui::button_click", on_button, NULL);
    ss_connect(bus, "net::data_received", on_network_data, NULL);

    /* Emit using namespace helper */
    ss_emit_namespaced(bus, "ui", "button_click", NULL);
    ss_emit_namespaced(bus, "net", "data_received", NULL);

    /* Default namespace: names without "::" resolve as "ui::name" */
    ss_set_namespace(bus, "ui");
    ss_emit_void(bus, "button_click");           /* emits "ui::button_click" */
    ss_emit_void(bus, "net::data_received");     /* contains "::", used as given */
    ss_set_namespace(bus, NULL);

    ss_bus_destroy(bus);
    return 0;
}
```

### Deferred Emission (Frame-Based Processing)

Queue signals during a frame and process them all at once:

```c
#include "ss_lib.h"
#include <stdio.h>

static void on_physics_update(const ss_data_t* data, void* user_data) {
    printf("Physics update: %d\n", ss_data_get_int(data, 0));
}

static void on_render(const ss_data_t* data, void* user_data) {
    printf("Render frame\n");
}

int main(void) {
    ss_bus_t* bus = NULL;
    int frame;
    ss_data_t* d;

    if (ss_bus_create(&bus, 0) != SS_OK) return 1;

    ss_signal_register(bus, "physics_update");
    ss_signal_register(bus, "render");

    ss_connect(bus, "physics_update", on_physics_update, NULL);
    ss_connect(bus, "render", on_render, NULL);

    /* Simulate game loop */
    for (frame = 0; frame < 3; frame++) {
        printf("--- Frame %d ---\n", frame);

        /* Queue updates during processing; the queue stores a copy
         * of the data, so d can be destroyed at once */
        d = ss_data_create(SS_TYPE_INT);
        ss_data_set_int(d, frame * 16);
        ss_emit_deferred(bus, "physics_update", d);
        ss_data_destroy(d);

        ss_emit_deferred(bus, "render", NULL);

        /* Process all at end of frame */
        ss_flush_deferred(bus);
    }

    ss_bus_destroy(bus);
    return 0;
}
```

### Batch Operations

Group related emissions:

```c
#include "ss_lib.h"
#include <stdio.h>

static void on_update(const ss_data_t* data, void* user_data) {
    const char* component = (const char*)user_data;
    printf("%s updated: %d\n", component, ss_data_get_int(data, 0));
}

int main(void) {
    ss_bus_t* bus = NULL;
    ss_batch_t* batch;
    ss_data_t* d;

    if (ss_bus_create(&bus, 0) != SS_OK) return 1;

    ss_signal_register(bus, "position_changed");
    ss_signal_register(bus, "velocity_changed");
    ss_signal_register(bus, "health_changed");

    ss_connect(bus, "position_changed", on_update, "Position");
    ss_connect(bus, "velocity_changed", on_update, "Velocity");
    ss_connect(bus, "health_changed", on_update, "Health");

    /* Create a batch of updates for this bus (up to SS_BATCH_MAX_ENTRIES);
     * like the deferred queue, it stores copies of the data. Destroy it
     * before the bus. */
    batch = ss_batch_create(bus);

    d = ss_data_create(SS_TYPE_INT);
    ss_data_set_int(d, 100);
    ss_batch_add(batch, "position_changed", d);
    ss_data_destroy(d);

    d = ss_data_create(SS_TYPE_INT);
    ss_data_set_int(d, 50);
    ss_batch_add(batch, "velocity_changed", d);
    ss_data_destroy(d);

    d = ss_data_create(SS_TYPE_INT);
    ss_data_set_int(d, 75);
    ss_batch_add(batch, "health_changed", d);
    ss_data_destroy(d);

    /* Emit all at once */
    ss_batch_emit(batch);
    ss_batch_destroy(batch);

    ss_bus_destroy(bus);
    return 0;
}
```

### IoT Sensor Hub

Multiple sensors reporting through a central signal system:

```c
#include "ss_lib.h"
#include <stdio.h>

static void cloud_upload(const ss_data_t* data, void* user_data) {
    const char* sensor = (const char*)user_data;
    printf("Uploading %s reading: %.1f\n", sensor,
           ss_data_get_float(data, 0.0f));
}

static void local_display(const ss_data_t* data, void* user_data) {
    const char* sensor = (const char*)user_data;
    printf("Display: %s = %.1f\n", sensor,
           ss_data_get_float(data, 0.0f));
}

int main(void) {
    ss_bus_t* bus = NULL;
    if (ss_bus_create(&bus, 0) != SS_OK) return 1;

    ss_signal_register(bus, "sensor::temperature");
    ss_signal_register(bus, "sensor::humidity");
    ss_signal_register(bus, "sensor::pressure");

    /* Multiple handlers per sensor */
    ss_connect(bus, "sensor::temperature", cloud_upload, "temperature");
    ss_connect(bus, "sensor::temperature", local_display, "temperature");
    ss_connect(bus, "sensor::humidity", cloud_upload, "humidity");
    ss_connect(bus, "sensor::pressure", cloud_upload, "pressure");

    /* Simulate readings */
    ss_emit_float(bus, "sensor::temperature", 22.5f);
    ss_emit_float(bus, "sensor::humidity", 65.0f);
    ss_emit_float(bus, "sensor::pressure", 1013.25f);

    ss_bus_destroy(bus);
    return 0;
}
```

### Independent Buses

Each subsystem can have its own bus. Buses share nothing: the same signal name on two buses names two signals, and a test can create a fresh bus instead of resetting global state:

```c
#include "ss_lib.h"
#include <stdio.h>

static void on_ready(const ss_data_t* data, void* user_data) {
    (void)data;
    printf("%s is ready\n", (const char*)user_data);
}

int main(void) {
    ss_bus_t* audio = NULL;
    ss_bus_t* video = NULL;
    if (ss_bus_create(&audio, 0) != SS_OK) return 1;
    if (ss_bus_create(&video, 0) != SS_OK) {
        ss_bus_destroy(audio);
        return 1;
    }

    ss_signal_register(audio, "ready");
    ss_signal_register(video, "ready");
    ss_connect(audio, "ready", on_ready, "audio");
    ss_connect(video, "ready", on_ready, "video");

    ss_emit_void(audio, "ready");      /* prints only "audio is ready" */

    ss_bus_destroy(video);
    ss_bus_destroy(audio);
    return 0;
}
```

In static mode `ss_bus_create()` takes one of `SS_STATIC_BUSES` built-in buses (default 1) and returns `SS_ERR_WOULD_OVERFLOW` when all are in use. Raise `SS_STATIC_BUSES` for the library and every caller, or place further buses in memory of your own with `ss_bus_create_in(&bus, pool, ss_bus_pool_size(), 0)`.

### Threads and Cross-Bus Calls

A bus shared by several threads is created with `SS_BUS_THREAD_SAFE`. A slot of one bus may emit on another, as long as every such call goes the same way (here `input` to `ui`, never `ui` to `input`); otherwise two threads could each hold one bus's lock and wait for the other's:

```c
#include "ss_lib.h"

static void forward_key(const ss_data_t* data, void* user_data) {
    ss_bus_t* ui = (ss_bus_t*)user_data;
    ss_emit(ui, "key", data);          /* input -> ui only */
}

int setup(ss_bus_t** input, ss_bus_t** ui) {
    if (ss_bus_create(input, SS_BUS_THREAD_SAFE) != SS_OK) return 0;
    if (ss_bus_create(ui, SS_BUS_THREAD_SAFE) != SS_OK) {
        ss_bus_destroy(*input);
        return 0;
    }
    ss_signal_register(*input, "key");
    ss_signal_register(*ui, "key");
    ss_connect(*input, "key", forward_key, *ui);
    return 1;
}
```
