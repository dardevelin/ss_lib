#ifndef SS_LIB_V2_H
#define SS_LIB_V2_H

#include <stddef.h>
#include <stdint.h>
#include "ss_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
typedef enum {
    SS_OK = 0,
    SS_ERR_NULL_PARAM,
    SS_ERR_MEMORY,
    SS_ERR_NOT_FOUND,
    SS_ERR_ALREADY_EXISTS,
    SS_ERR_INVALID_TYPE,
    SS_ERR_BUFFER_TOO_SMALL,
    SS_ERR_MAX_SLOTS,
    SS_ERR_TIMEOUT,
    SS_ERR_WOULD_OVERFLOW,
    SS_ERR_ISR_UNSAFE
} ss_error_t;

/* Data types */
typedef enum {
    SS_TYPE_VOID,
    SS_TYPE_INT,
    SS_TYPE_FLOAT,
    SS_TYPE_DOUBLE,
    SS_TYPE_STRING,
    SS_TYPE_POINTER,
#if SS_ENABLE_CUSTOM_DATA
    SS_TYPE_CUSTOM
#endif
} ss_data_type_t;

/* Priority levels */
typedef enum {
    SS_PRIORITY_LOW = 0,
    SS_PRIORITY_NORMAL = 5,
    SS_PRIORITY_HIGH = 10,
    SS_PRIORITY_CRITICAL = 15
} ss_priority_t;

/* Data structure */
typedef struct ss_data {
    ss_data_type_t type;
    union {
        int i_val;
        float f_val;
        double d_val;
        char* s_val;
        void* p_val;
    } value;
#if SS_ENABLE_CUSTOM_DATA
    size_t size;
    void* custom_data;
#endif
} ss_data_t;

/* Function types */
typedef void (*ss_slot_func_t)(const ss_data_t* data, void* user_data);
typedef void (*ss_cleanup_func_t)(void* data);

/* Connection handle for easier disconnection */
typedef uintptr_t ss_connection_t;

/* Core API */
ss_error_t ss_init(void);
ss_error_t ss_init_static(void* memory_pool, size_t pool_size);
void ss_cleanup(void);

/* Signal management */
ss_error_t ss_signal_register(const char* signal_name);
ss_error_t ss_signal_register_ex(const char* signal_name, 
                                const char* description,
                                ss_priority_t priority);
ss_error_t ss_signal_unregister(const char* signal_name);
int ss_signal_exists(const char* signal_name);

/* Connection management */
ss_error_t ss_connect(const char* signal_name, ss_slot_func_t slot, void* user_data);
ss_error_t ss_connect_ex(const char* signal_name, ss_slot_func_t slot, 
                        void* user_data, ss_priority_t priority,
                        ss_connection_t* handle);
ss_error_t ss_disconnect(const char* signal_name, ss_slot_func_t slot);
ss_error_t ss_disconnect_handle(ss_connection_t handle);
ss_error_t ss_disconnect_all(const char* signal_name);

/* Signal emission */
ss_error_t ss_emit(const char* signal_name, const ss_data_t* data);
ss_error_t ss_emit_void(const char* signal_name);
ss_error_t ss_emit_int(const char* signal_name, int value);
ss_error_t ss_emit_float(const char* signal_name, float value);
ss_error_t ss_emit_double(const char* signal_name, double value);
ss_error_t ss_emit_string(const char* signal_name, const char* value);
ss_error_t ss_emit_pointer(const char* signal_name, void* value);

#if SS_ENABLE_ISR_SAFE
/* ISR-safe emission (no locks, no malloc) */
ss_error_t ss_emit_from_isr(const char* signal_name, int value);
#endif

/* Deferred emission */
ss_error_t ss_emit_deferred(const char* signal_name, const ss_data_t* data);
ss_error_t ss_flush_deferred(void);

/* Data handling */
ss_data_t* ss_data_create(ss_data_type_t type);
void ss_data_destroy(ss_data_t* data);
ss_error_t ss_data_set_int(ss_data_t* data, int value);
ss_error_t ss_data_set_float(ss_data_t* data, float value);
ss_error_t ss_data_set_double(ss_data_t* data, double value);
ss_error_t ss_data_set_string(ss_data_t* data, const char* value);
ss_error_t ss_data_set_pointer(ss_data_t* data, void* value);

#if SS_ENABLE_CUSTOM_DATA
ss_error_t ss_data_set_custom(ss_data_t* data, void* value, size_t size, 
                             ss_cleanup_func_t cleanup);
#endif

/* Data retrieval */
int ss_data_get_int(const ss_data_t* data, int default_val);
float ss_data_get_float(const ss_data_t* data, float default_val);
double ss_data_get_double(const ss_data_t* data, double default_val);
const char* ss_data_get_string(const ss_data_t* data);
void* ss_data_get_pointer(const ss_data_t* data);

#if SS_ENABLE_CUSTOM_DATA
void* ss_data_get_custom(const ss_data_t* data, size_t* size);
#endif

#if SS_ENABLE_INTROSPECTION
/* Signal information */
typedef struct ss_signal_info {
    char* name;
    size_t slot_count;
    const char* description;
    ss_priority_t priority;
} ss_signal_info_t;

size_t ss_get_signal_count(void);
ss_error_t ss_get_signal_list(ss_signal_info_t** list, size_t* count);
void ss_free_signal_list(ss_signal_info_t* list, size_t count);
#endif

#if SS_ENABLE_MEMORY_STATS
/* Memory statistics */
typedef struct ss_memory_stats {
    size_t signals_allocated;
    size_t signals_used;
    size_t slots_allocated;
    size_t slots_used;
    size_t total_bytes_allocated;
    size_t peak_bytes_allocated;
    size_t string_bytes;
} ss_memory_stats_t;

ss_error_t ss_get_memory_stats(ss_memory_stats_t* stats);
void ss_reset_memory_stats(void);
#endif

#if SS_ENABLE_PERFORMANCE_STATS
/* Performance statistics */
typedef struct ss_perf_stats {
    uint64_t total_emissions;
    uint64_t total_time_ns;
    uint64_t avg_time_ns;
    uint64_t max_time_ns;
    uint64_t min_time_ns;
} ss_perf_stats_t;

ss_error_t ss_get_perf_stats(const char* signal_name, ss_perf_stats_t* stats);
ss_error_t ss_enable_profiling(int enabled);
void ss_reset_perf_stats(void);
#endif

/* Error handling */
const char* ss_error_string(ss_error_t error);
void ss_set_error_handler(void (*handler)(ss_error_t error, const char* msg));

/* Configuration */
void ss_set_max_slots_per_signal(size_t max_slots);
size_t ss_get_max_slots_per_signal(void);

#if SS_ENABLE_THREAD_SAFETY
void ss_set_thread_safe(int enabled);
int ss_is_thread_safe(void);
#endif

/* Namespace support */
ss_error_t ss_set_namespace(const char* namespace);
const char* ss_get_namespace(void);
ss_error_t ss_emit_namespaced(const char* namespace, const char* signal_name, 
                              const ss_data_t* data);

/* Batch operations */
typedef struct ss_batch ss_batch_t;

ss_batch_t* ss_batch_create(void);
void ss_batch_destroy(ss_batch_t* batch);
ss_error_t ss_batch_add(ss_batch_t* batch, const char* signal_name, 
                       const ss_data_t* data);
ss_error_t ss_batch_emit(ss_batch_t* batch);

/* Debug support */
#if SS_ENABLE_DEBUG_TRACE
void ss_enable_trace(FILE* output);
void ss_disable_trace(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SS_LIB_V2_H */
