# Examples

This document walks through the example programs included in the `examples/` directory and demonstrates additional usage patterns.

## Included Examples

### example_usage.c

Basic demonstration of signal registration, connection, emission, and cleanup:

```bash
make examples
./build/example_usage
```

Covers:
- `ss_init()` / `ss_cleanup()`
- `ss_signal_register()` / `ss_connect()`
- `ss_emit_void()` / `ss_emit_int()`

### example_embedded.c

Demonstrates embedded usage with ISR-safe emission:

```bash
make examples
./build/example_embedded
```

Covers:
- ISR-safe emission with `ss_emit_from_isr()`
- Static memory configuration
- Simulated interrupt handling

### example_embedded_simple.c

Minimal embedded example without ISR features:

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
    int current_temp;

    ss_init();

    ss_signal_register("temp_warning");
    ss_signal_register("temp_critical");

    ss_connect("temp_warning", on_temp_warning, NULL);
    ss_connect("temp_critical", on_temp_critical, NULL);

    /* Simulated sensor loop */
    current_temp = 85;
    if (current_temp > 80) {
        ss_emit_int("temp_warning", current_temp);
    }

    current_temp = 105;
    if (current_temp > 100) {
        ss_emit_int("temp_critical", current_temp);
    }

    ss_cleanup();
    return 0;
}
```

### Plugin System

Use signals to decouple a core application from plugins:

```c
#include "ss_lib.h"
#include <stdio.h>

/* Core application registers events */
void app_init(void) {
    ss_init();
    ss_signal_register("file_opened");
    ss_signal_register("file_saved");
    ss_signal_register("app_closing");
}

/* Plugin connects to events without modifying core */
static void backup_on_save(const ss_data_t* data, void* user_data) {
    const char* filename = ss_data_get_string(data);
    printf("Backup plugin: creating backup of %s\n", filename);
}

static void backup_on_close(const ss_data_t* data, void* user_data) {
    printf("Backup plugin: shutting down\n");
}

void backup_plugin_init(void) {
    ss_connect("file_saved", backup_on_save, NULL);
    ss_connect("app_closing", backup_on_close, NULL);
}

/* Another plugin */
static void log_on_open(const ss_data_t* data, void* user_data) {
    const char* filename = ss_data_get_string(data);
    printf("Log plugin: file opened: %s\n", filename);
}

void log_plugin_init(void) {
    ss_connect("file_opened", log_on_open, NULL);
}

int main(void) {
    app_init();

    backup_plugin_init();
    log_plugin_init();

    ss_emit_string("file_opened", "document.txt");
    ss_emit_string("file_saved", "document.txt");
    ss_emit_void("app_closing");

    ss_cleanup();
    return 0;
}
```

### Game Engine Events

Priority-based signal handling for game entity damage:

```c
#include "ss_lib.h"
#include <stdio.h>

typedef struct {
    int health;
    int armor;
    char name[32];
} entity_t;

static void check_death(const ss_data_t* data, void* user_data) {
    entity_t* e = (entity_t*)ss_data_get_pointer(data);
    if (e && e->health <= 0) {
        printf("[CRITICAL] %s has died!\n", e->name);
    }
}

static void apply_damage(const ss_data_t* data, void* user_data) {
    entity_t* e = (entity_t*)ss_data_get_pointer(data);
    int damage = *(int*)user_data;
    if (e) {
        e->health -= damage;
        printf("[HIGH] %s took %d damage, health: %d\n",
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
    entity_t player;
    int damage;

    ss_init();

    player.health = 100;
    player.armor = 50;
    strcpy(player.name, "Player1");
    damage = 150;

    ss_signal_register_ex("entity_damaged", "Entity takes damage",
                          SS_PRIORITY_HIGH);

    /* Death check runs first (highest priority) */
    ss_connect_ex("entity_damaged", apply_damage, &damage,
                  SS_PRIORITY_HIGH, NULL);
    ss_connect_ex("entity_damaged", check_death, NULL,
                  SS_PRIORITY_CRITICAL, NULL);
    ss_connect_ex("entity_damaged", play_hurt_sound, NULL,
                  SS_PRIORITY_NORMAL, NULL);

    ss_emit_pointer("entity_damaged", &player);

    ss_cleanup();
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
    ss_init();

    /* Register namespaced signals */
    ss_signal_register("ui::button_click");
    ss_signal_register("ui::slider_change");
    ss_signal_register("net::data_received");
    ss_signal_register("net::connection_lost");

    ss_connect("ui::button_click", on_button, NULL);
    ss_connect("net::data_received", on_network_data, NULL);

    /* Emit using namespace helper */
    ss_emit_namespaced("ui", "button_click", NULL);
    ss_emit_namespaced("net", "data_received", NULL);

    ss_cleanup();
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
    int frame;
    ss_data_t* d;

    ss_init();

    ss_signal_register("physics_update");
    ss_signal_register("render");

    ss_connect("physics_update", on_physics_update, NULL);
    ss_connect("render", on_render, NULL);

    /* Simulate game loop */
    for (frame = 0; frame < 3; frame++) {
        printf("--- Frame %d ---\n", frame);

        /* Queue updates during processing */
        d = ss_data_create(SS_TYPE_INT);
        ss_data_set_int(d, frame * 16);
        ss_emit_deferred("physics_update", d);
        ss_data_destroy(d);

        ss_emit_deferred("render", NULL);

        /* Process all at end of frame */
        ss_flush_deferred();
    }

    ss_cleanup();
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
    ss_batch_t* batch;
    ss_data_t* d;

    ss_init();

    ss_signal_register("position_changed");
    ss_signal_register("velocity_changed");
    ss_signal_register("health_changed");

    ss_connect("position_changed", on_update, "Position");
    ss_connect("velocity_changed", on_update, "Velocity");
    ss_connect("health_changed", on_update, "Health");

    /* Create a batch of updates */
    batch = ss_batch_create();

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

    ss_cleanup();
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
    ss_init();

    ss_signal_register("sensor::temperature");
    ss_signal_register("sensor::humidity");
    ss_signal_register("sensor::pressure");

    /* Multiple handlers per sensor */
    ss_connect("sensor::temperature", cloud_upload, "temperature");
    ss_connect("sensor::temperature", local_display, "temperature");
    ss_connect("sensor::humidity", cloud_upload, "humidity");
    ss_connect("sensor::pressure", cloud_upload, "pressure");

    /* Simulate readings */
    ss_emit_float("sensor::temperature", 22.5f);
    ss_emit_float("sensor::humidity", 65.0f);
    ss_emit_float("sensor::pressure", 1013.25f);

    ss_cleanup();
    return 0;
}
```
