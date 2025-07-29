#include "ss_lib.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    char name[50];
    int score;
} Player;

void on_player_scored(const ss_data_t* data, void* user_data) {
    Player* player = (Player*)ss_data_get_pointer(data);
    if (player) {
        printf("[Game] %s scored! Total: %d points\n", player->name, player->score);
    }
}

void on_game_over(const ss_data_t* data, void* user_data) {
    const char* winner = ss_data_get_string(data);
    printf("[Game] Game Over! Winner: %s\n", winner ? winner : "Nobody");
}

void on_button_clicked(const ss_data_t* data, void* user_data) {
    const char* button_name = (const char*)user_data;
    printf("[UI] Button '%s' was clicked\n", button_name);
}

void on_value_changed(const ss_data_t* data, void* user_data) {
    int new_value = ss_data_get_int(data, 0);
    printf("[UI] Slider value changed to: %d\n", new_value);
}

void game_example(void) {
    printf("\n=== Game Events Example ===\n");
    
    ss_signal_register("player_scored");
    ss_signal_register("game_over");
    
    ss_connect("player_scored", on_player_scored, NULL);
    ss_connect("game_over", on_game_over, NULL);
    
    Player player1 = {"Alice", 0};
    Player player2 = {"Bob", 0};
    
    player1.score += 10;
    ss_emit_pointer("player_scored", &player1);
    
    player2.score += 15;
    ss_emit_pointer("player_scored", &player2);
    
    player1.score += 20;
    ss_emit_pointer("player_scored", &player1);
    
    const char* winner = player1.score > player2.score ? player1.name : player2.name;
    ss_emit_string("game_over", winner);
    
    ss_signal_unregister("player_scored");
    ss_signal_unregister("game_over");
}

void ui_example(void) {
    printf("\n=== UI Events Example ===\n");
    
    ss_signal_register("button_click");
    ss_signal_register("slider_changed");
    
    ss_connect("button_click", on_button_clicked, "Save");
    ss_connect("button_click", on_button_clicked, "Load");
    ss_connect("button_click", on_button_clicked, "Exit");
    
    ss_connect("slider_changed", on_value_changed, NULL);
    
    ss_emit_void("button_click");
    
    for (int i = 0; i <= 100; i += 25) {
        ss_emit_int("slider_changed", i);
    }
    
    ss_disconnect_all("button_click");
    ss_disconnect_all("slider_changed");
    
    ss_signal_unregister("button_click");
    ss_signal_unregister("slider_changed");
}

typedef struct {
    int x;
    int y;
} Point;

void on_mouse_moved(const ss_data_t* data, void* user_data) {
    size_t size;
    Point* pos = (Point*)ss_data_get_custom(data, &size);
    if (pos && size == sizeof(Point)) {
        printf("[Input] Mouse moved to (%d, %d)\n", pos->x, pos->y);
    }
}

void custom_data_example(void) {
    printf("\n=== Custom Data Example ===\n");
    
    ss_signal_register("mouse_move");
    ss_connect("mouse_move", on_mouse_moved, NULL);
    
    ss_data_t* data = ss_data_create(SS_TYPE_CUSTOM);
    
    Point positions[] = {{100, 100}, {150, 120}, {200, 140}, {250, 160}};
    
    for (int i = 0; i < 4; i++) {
        ss_data_set_custom(data, &positions[i], sizeof(Point), NULL);
        ss_emit("mouse_move", data);
    }
    
    ss_data_destroy(data);
    
    ss_signal_unregister("mouse_move");
}

void introspection_example(void) {
    printf("\n=== Signal Introspection Example ===\n");
    
    const char* signals[] = {"app_start", "app_stop", "file_open", "file_save", "file_close"};
    
    for (int i = 0; i < 5; i++) {
        ss_signal_register(signals[i]);
    }
    
    ss_connect("file_open", on_button_clicked, "file_open");
    ss_connect("file_save", on_button_clicked, "file_save");
    ss_connect("file_save", on_button_clicked, "file_save_backup");
    
    ss_signal_info_t* list = NULL;
    size_t count = 0;
    
    if (ss_get_signal_list(&list, &count) == SS_OK) {
        printf("Registered signals:\n");
        for (size_t i = 0; i < count; i++) {
            printf("  - %s (slots: %zu)\n", list[i].name, list[i].slot_count);
        }
        ss_free_signal_list(list, count);
    }
    
    for (int i = 0; i < 5; i++) {
        ss_signal_unregister(signals[i]);
    }
}

int main(void) {
    printf("Signal-Slot Library Examples\n");
    printf("===========================\n");
    
    if (ss_init() != SS_OK) {
        fprintf(stderr, "Failed to initialize signal-slot library\n");
        return 1;
    }
    
    game_example();
    ui_example();
    custom_data_example();
    introspection_example();
    
    ss_cleanup();
    
    printf("\nAll examples completed successfully!\n");
    return 0;
}
