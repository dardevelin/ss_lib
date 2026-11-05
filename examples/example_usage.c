#include "ss_lib.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    char name[50];
    int score;
} Player;

void on_player_scored(const ss_data_t* data, void* user_data) {
    (void)user_data;
    Player* player = (Player*)ss_data_get_pointer(data);
    if (player) {
        printf("[Game] %s scored! Total: %d points\n", player->name, player->score);
    }
}

void on_game_over(const ss_data_t* data, void* user_data) {
    (void)user_data;
    const char* winner = ss_data_get_string(data);
    printf("[Game] Game Over! Winner: %s\n", winner ? winner : "Nobody");
}

void on_button_clicked(const ss_data_t* data, void* user_data) {
    (void)data;
    const char* button_name = (const char*)user_data;
    printf("[UI] Button '%s' was clicked\n", button_name);
}

void on_value_changed(const ss_data_t* data, void* user_data) {
    (void)user_data;
    int new_value = ss_data_get_int(data, 0);
    printf("[UI] Slider value changed to: %d\n", new_value);
}

void game_example(ss_bus_t* bus) {
    printf("\n=== Game Events Example ===\n");
    
    ss_signal_register(bus, "player_scored");
    ss_signal_register(bus, "game_over");
    
    ss_connect(bus, "player_scored", on_player_scored, NULL);
    ss_connect(bus, "game_over", on_game_over, NULL);
    
    Player player1 = {"Alice", 0};
    Player player2 = {"Bob", 0};
    
    player1.score += 10;
    ss_emit_pointer(bus, "player_scored", &player1);
    
    player2.score += 15;
    ss_emit_pointer(bus, "player_scored", &player2);
    
    player1.score += 20;
    ss_emit_pointer(bus, "player_scored", &player1);
    
    const char* winner = player1.score > player2.score ? player1.name : player2.name;
    ss_emit_string(bus, "game_over", winner);
    
    ss_signal_unregister(bus, "player_scored");
    ss_signal_unregister(bus, "game_over");
}

void ui_example(ss_bus_t* bus) {
    printf("\n=== UI Events Example ===\n");
    
    ss_signal_register(bus, "button_click");
    ss_signal_register(bus, "slider_changed");
    
    ss_connect(bus, "button_click", on_button_clicked, "Save");
    ss_connect(bus, "button_click", on_button_clicked, "Load");
    ss_connect(bus, "button_click", on_button_clicked, "Exit");
    
    ss_connect(bus, "slider_changed", on_value_changed, NULL);
    
    ss_emit_void(bus, "button_click");
    
    for (int i = 0; i <= 100; i += 25) {
        ss_emit_int(bus, "slider_changed", i);
    }
    
    ss_disconnect_all(bus, "button_click");
    ss_disconnect_all(bus, "slider_changed");
    
    ss_signal_unregister(bus, "button_click");
    ss_signal_unregister(bus, "slider_changed");
}

typedef struct {
    int x;
    int y;
} Point;

void on_mouse_moved(const ss_data_t* data, void* user_data) {
    (void)user_data;
    size_t size;
    Point* pos = (Point*)ss_data_get_custom(data, &size);
    if (pos && size == sizeof(Point)) {
        printf("[Input] Mouse moved to (%d, %d)\n", pos->x, pos->y);
    }
}

void custom_data_example(ss_bus_t* bus) {
    printf("\n=== Custom Data Example ===\n");
    
    ss_signal_register(bus, "mouse_move");
    ss_connect(bus, "mouse_move", on_mouse_moved, NULL);
    
    ss_data_t* data = ss_data_create(SS_TYPE_CUSTOM);
    
    Point positions[] = {{100, 100}, {150, 120}, {200, 140}, {250, 160}};
    
    for (int i = 0; i < 4; i++) {
        ss_data_set_custom(data, &positions[i], sizeof(Point), NULL);
        ss_emit(bus, "mouse_move", data);
    }
    
    ss_data_destroy(data);
    
    ss_signal_unregister(bus, "mouse_move");
}

void introspection_example(ss_bus_t* bus) {
    printf("\n=== Signal Introspection Example ===\n");
    
    const char* signals[] = {"app_start", "app_stop", "file_open", "file_save", "file_close"};
    
    for (int i = 0; i < 5; i++) {
        ss_signal_register(bus, signals[i]);
    }
    
    ss_connect(bus, "file_open", on_button_clicked, "file_open");
    ss_connect(bus, "file_save", on_button_clicked, "file_save");
    ss_connect(bus, "file_save", on_button_clicked, "file_save_backup");
    
    ss_signal_info_t* list = NULL;
    size_t count = 0;
    
    if (ss_get_signal_list(bus, &list, &count) == SS_OK) {
        printf("Registered signals:\n");
        for (size_t i = 0; i < count; i++) {
            printf("  - %s (slots: %zu)\n", list[i].name, list[i].slot_count);
        }
        ss_free_signal_list(list, count);
    }
    
    for (int i = 0; i < 5; i++) {
        ss_signal_unregister(bus, signals[i]);
    }
}

/* A slot that needs its bus gets it through user_data */
void on_file_saved(const ss_data_t* data, void* user_data) {
    ss_bus_t* bus = (ss_bus_t*)user_data;
    printf("[Editor] Saved %s, notifying the status bar\n", ss_data_get_string(data));
    ss_emit_string(bus, "status", "All changes saved");
}

void on_status(const ss_data_t* data, void* user_data) {
    (void)user_data;
    printf("[Status] %s\n", ss_data_get_string(data));
}

/* Buses are independent: the same name is a different signal on each */
void two_buses_example(ss_bus_t* bus) {
    ss_bus_t* other = NULL;
    printf("\n=== Slots, user_data and a second bus ===\n");

    ss_signal_register(bus, "file_saved");
    ss_signal_register(bus, "status");
    ss_connect(bus, "file_saved", on_file_saved, bus);
    ss_connect(bus, "status", on_status, NULL);
    ss_emit_string(bus, "file_saved", "notes.txt");

    if (ss_bus_create(&other, 0) == SS_OK) {
        ss_signal_register(other, "status");    /* not the first bus's "status" */
        ss_emit_string(other, "status", "nobody listens on this bus");
        ss_bus_destroy(other);
    }

    ss_signal_unregister(bus, "file_saved");
    ss_signal_unregister(bus, "status");
}

/* The error handler gets the failing bus and the user_data it was set with */
typedef struct {
    const char* bus_name;
    int errors;
} ErrorLog;

void on_error(ss_bus_t* bus, ss_error_t error, const char* msg, void* user_data) {
    ErrorLog* log = (ErrorLog*)user_data;
    (void)bus;
    log->errors++;
    printf("[%s] %s: %s\n", log->bus_name, ss_error_string(error), msg);
}

void error_handler_example(ss_bus_t* bus) {
    ErrorLog log = {"main bus", 0};
    printf("\n=== Error Handler Example ===\n");

    ss_set_error_handler(bus, on_error, &log);
    ss_emit_void(bus, "no_such_signal");      /* Signal not found: no_such_signal */
    ss_signal_register(bus, "once");
    ss_signal_register(bus, "once");          /* Signal already exists: once */
    ss_signal_unregister(bus, "once");
    printf("%d errors reported\n", log.errors);

    ss_set_error_handler(bus, NULL, NULL);    /* log goes out of scope */
}

int main(void) {
    ss_bus_t* bus = NULL;
    printf("Signal-Slot Library Examples\n");
    printf("===========================\n");

    if (ss_bus_create(&bus, 0) != SS_OK) {
        fprintf(stderr, "Failed to create the signal bus\n");
        return 1;
    }

    game_example(bus);
    ui_example(bus);
    custom_data_example(bus);
    introspection_example(bus);
    two_buses_example(bus);
    error_handler_example(bus);

    ss_bus_destroy(bus);
    
    printf("\nAll examples completed successfully!\n");
    return 0;
}
