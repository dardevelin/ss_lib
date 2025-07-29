#ifndef SS_LIB_H
#define SS_LIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SS_OK = 0,
    SS_ERR_NULL_PARAM,
    SS_ERR_MEMORY,
    SS_ERR_NOT_FOUND,
    SS_ERR_ALREADY_EXISTS,
    SS_ERR_INVALID_TYPE,
    SS_ERR_BUFFER_TOO_SMALL,
    SS_ERR_MAX_SLOTS
} ss_error_t;

typedef enum {
    SS_TYPE_VOID,
    SS_TYPE_INT,
    SS_TYPE_FLOAT,
    SS_TYPE_DOUBLE,
    SS_TYPE_STRING,
    SS_TYPE_POINTER,
    SS_TYPE_CUSTOM
} ss_data_type_t;

typedef struct ss_data {
    ss_data_type_t type;
    union {
        int i_val;
        float f_val;
        double d_val;
        char* s_val;
        void* p_val;
    } value;
    size_t size;
    void* custom_data;
} ss_data_t;

typedef void (*ss_slot_func_t)(const ss_data_t* data, void* user_data);

typedef void (*ss_cleanup_func_t)(void* data);

ss_error_t ss_init(void);
void ss_cleanup(void);

ss_error_t ss_signal_register(const char* signal_name);
ss_error_t ss_signal_unregister(const char* signal_name);
int ss_signal_exists(const char* signal_name);

ss_error_t ss_connect(const char* signal_name, ss_slot_func_t slot, void* user_data);
ss_error_t ss_disconnect(const char* signal_name, ss_slot_func_t slot);
ss_error_t ss_disconnect_all(const char* signal_name);

ss_error_t ss_emit(const char* signal_name, const ss_data_t* data);
ss_error_t ss_emit_void(const char* signal_name);
ss_error_t ss_emit_int(const char* signal_name, int value);
ss_error_t ss_emit_float(const char* signal_name, float value);
ss_error_t ss_emit_double(const char* signal_name, double value);
ss_error_t ss_emit_string(const char* signal_name, const char* value);
ss_error_t ss_emit_pointer(const char* signal_name, void* value);

ss_data_t* ss_data_create(ss_data_type_t type);
void ss_data_destroy(ss_data_t* data);
ss_error_t ss_data_set_int(ss_data_t* data, int value);
ss_error_t ss_data_set_float(ss_data_t* data, float value);
ss_error_t ss_data_set_double(ss_data_t* data, double value);
ss_error_t ss_data_set_string(ss_data_t* data, const char* value);
ss_error_t ss_data_set_pointer(ss_data_t* data, void* value);
ss_error_t ss_data_set_custom(ss_data_t* data, void* value, size_t size, ss_cleanup_func_t cleanup);

int ss_data_get_int(const ss_data_t* data, int default_val);
float ss_data_get_float(const ss_data_t* data, float default_val);
double ss_data_get_double(const ss_data_t* data, double default_val);
const char* ss_data_get_string(const ss_data_t* data);
void* ss_data_get_pointer(const ss_data_t* data);
void* ss_data_get_custom(const ss_data_t* data, size_t* size);

typedef struct ss_signal_info {
    char* name;
    size_t slot_count;
} ss_signal_info_t;

size_t ss_get_signal_count(void);
ss_error_t ss_get_signal_list(ss_signal_info_t** list, size_t* count);
void ss_free_signal_list(ss_signal_info_t* list, size_t count);

const char* ss_error_string(ss_error_t error);

void ss_set_max_slots_per_signal(size_t max_slots);
size_t ss_get_max_slots_per_signal(void);

void ss_set_thread_safe(int enabled);
int ss_is_thread_safe(void);

#ifdef __cplusplus
}
#endif

#endif
