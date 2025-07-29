/*
 * Signal-Slot Library - Single Header Version
 * 
 * Usage:
 *   #define SS_IMPLEMENTATION
 *   #include "ss_lib_single.h"
 */

#ifndef SS_LIB_SINGLE_H
#define SS_LIB_SINGLE_H

/* Configuration options - define before including this file */
/* #define SS_USE_STATIC_MEMORY 1 */
/* #define SS_MAX_SIGNALS 32 */
/* #define SS_MAX_SLOTS 128 */
/* #define SS_ENABLE_THREAD_SAFETY 0 */


/* ========================================================================
 * Signal-Slot Library Configuration
 * ======================================================================== */

/* Memory Configuration */
#ifndef SS_USE_STATIC_MEMORY
    #define SS_USE_STATIC_MEMORY 0
#endif

#if SS_USE_STATIC_MEMORY
    #ifndef SS_MAX_SIGNALS
        #define SS_MAX_SIGNALS 32
    #endif
    
    #ifndef SS_MAX_SLOTS
        #define SS_MAX_SLOTS 128
    #endif
    
    #ifndef SS_MAX_SIGNAL_NAME_LENGTH
        #define SS_MAX_SIGNAL_NAME_LENGTH 32
    #endif
#endif

/* Feature Flags */
#ifndef SS_ENABLE_THREAD_SAFETY
    #define SS_ENABLE_THREAD_SAFETY 1
#endif

#ifndef SS_ENABLE_INTROSPECTION
    #define SS_ENABLE_INTROSPECTION 1
#endif

#ifndef SS_ENABLE_CUSTOM_DATA
    #define SS_ENABLE_CUSTOM_DATA 1
#endif

#ifndef SS_ENABLE_PERFORMANCE_STATS
    #define SS_ENABLE_PERFORMANCE_STATS 0
#endif

#ifndef SS_ENABLE_MEMORY_STATS
    #define SS_ENABLE_MEMORY_STATS 0
#endif

#ifndef SS_ENABLE_DEBUG_TRACE
    #define SS_ENABLE_DEBUG_TRACE 0
#endif

/* Platform Configuration */
#ifndef SS_ENABLE_ISR_SAFE
    #define SS_ENABLE_ISR_SAFE 0
#endif

#ifndef SS_CACHE_LINE_SIZE
    #define SS_CACHE_LINE_SIZE 64
#endif

/* Limits */
#ifndef SS_DEFAULT_MAX_SLOTS_PER_SIGNAL
    #define SS_DEFAULT_MAX_SLOTS_PER_SIGNAL 100
#endif

/* Custom Memory Functions */
#ifndef SS_MALLOC
    #define SS_MALLOC(size) malloc(size)
#endif

#ifndef SS_FREE
    #define SS_FREE(ptr) free(ptr)
#endif

#ifndef SS_CALLOC
    #define SS_CALLOC(count, size) calloc(count, size)
#endif

#ifndef SS_STRDUP
    #define SS_STRDUP(str) strdup(str)
#endif

/* Debug Macros */
#if SS_ENABLE_DEBUG_TRACE
    #include <stdio.h>
    #define SS_TRACE(fmt, ...) fprintf(stderr, "[SS_TRACE] " fmt "\n", ##__VA_ARGS__)
#else
    #define SS_TRACE(fmt, ...) ((void)0)
#endif

/* Minimal Build */
#ifdef SS_MINIMAL_BUILD
    #undef SS_ENABLE_THREAD_SAFETY
    #define SS_ENABLE_THREAD_SAFETY 0
    
    #undef SS_ENABLE_INTROSPECTION
    #define SS_ENABLE_INTROSPECTION 0
    
    #undef SS_ENABLE_CUSTOM_DATA
    #define SS_ENABLE_CUSTOM_DATA 0
    
    #undef SS_ENABLE_PERFORMANCE_STATS
    #define SS_ENABLE_PERFORMANCE_STATS 0
    
    #undef SS_ENABLE_MEMORY_STATS
    #define SS_ENABLE_MEMORY_STATS 0
#endif

/* Embedded Build */
#ifdef SS_EMBEDDED_BUILD
    #undef SS_USE_STATIC_MEMORY
    #define SS_USE_STATIC_MEMORY 1
    
    #undef SS_ENABLE_THREAD_SAFETY
    #define SS_ENABLE_THREAD_SAFETY 0
    
    #undef SS_DEFAULT_MAX_SLOTS_PER_SIGNAL
    #define SS_DEFAULT_MAX_SLOTS_PER_SIGNAL 10
#endif


/* ======== Header Section ======== */


#include <stddef.h>
#include <stdint.h>

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


#ifdef SS_IMPLEMENTATION

/* ======== Implementation Section ======== */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#if SS_ENABLE_THREAD_SAFETY
#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION ss_mutex_t;
#define SS_MUTEX_INIT(m) InitializeCriticalSection(m)
#define SS_MUTEX_LOCK(m) EnterCriticalSection(m)
#define SS_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define SS_MUTEX_DESTROY(m) DeleteCriticalSection(m)
#else
#include <pthread.h>
typedef pthread_mutex_t ss_mutex_t;
#define SS_MUTEX_INIT(m) pthread_mutex_init(m, NULL)
#define SS_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define SS_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define SS_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#endif
#endif

/* Internal structures */
typedef struct ss_slot {
    ss_slot_func_t func;
    void* user_data;
    ss_priority_t priority;
    ss_connection_t handle;
#if !SS_USE_STATIC_MEMORY
    struct ss_slot* next;
#endif
} ss_slot_t;

typedef struct ss_signal {
    char* name;
    char* description;
    ss_slot_t* slots;
    size_t slot_count;
    ss_priority_t priority;
#if SS_ENABLE_PERFORMANCE_STATS
    ss_perf_stats_t perf_stats;
#endif
#if !SS_USE_STATIC_MEMORY
    struct ss_signal* next;
#endif
} ss_signal_t;

typedef struct {
#if SS_USE_STATIC_MEMORY
    /* Static allocation */
    ss_signal_t signals[SS_MAX_SIGNALS];
    ss_slot_t slots[SS_MAX_SLOTS];
    char signal_names[SS_MAX_SIGNALS][SS_MAX_SIGNAL_NAME_LENGTH];
    uint8_t signal_used[SS_MAX_SIGNALS];
    uint8_t slot_used[SS_MAX_SLOTS];
    size_t signal_count;
    size_t slot_count;
#else
    /* Dynamic allocation */
    ss_signal_t* signals;
    size_t signal_count;
#endif
    
    size_t max_slots_per_signal;
    int thread_safe;
    int profiling_enabled;
    char* namespace;
    
#if SS_ENABLE_THREAD_SAFETY
    ss_mutex_t mutex;
#endif
    
#if SS_ENABLE_MEMORY_STATS
    ss_memory_stats_t memory_stats;
#endif
    
    void (*error_handler)(ss_error_t, const char*);
    ss_connection_t next_handle;
    
#if SS_ENABLE_DEBUG_TRACE
    FILE* trace_output;
#endif
} ss_context_t;

/* Global context */
static ss_context_t* g_context = NULL;

/* Helper functions */
static uint64_t get_time_ns(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (uint64_t)(count.QuadPart * 1000000000ULL / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
}

static void report_error(ss_error_t error, const char* msg) {
    if (g_context && g_context->error_handler) {
        g_context->error_handler(error, msg);
    }
}

#if SS_USE_STATIC_MEMORY
static ss_signal_t* find_signal(const char* name) {
    size_t i;
    if (!g_context || !name) return NULL;
    
    for (i = 0; i < SS_MAX_SIGNALS; i++) {
        if (g_context->signal_used[i] && 
            strcmp(g_context->signals[i].name, name) == 0) {
            return &g_context->signals[i];
        }
    }
    return NULL;
}

static ss_slot_t* allocate_slot(void) {
    size_t i;
    for (i = 0; i < SS_MAX_SLOTS; i++) {
        if (!g_context->slot_used[i]) {
            g_context->slot_used[i] = 1;
            g_context->slot_count++;
#if SS_ENABLE_MEMORY_STATS
            if (g_context->slot_count > g_context->memory_stats.peak_bytes_allocated) {
                g_context->memory_stats.peak_bytes_allocated = g_context->slot_count;
            }
#endif
            return &g_context->slots[i];
        }
    }
    return NULL;
}

static void free_slot(ss_slot_t* slot) {
    size_t index = slot - g_context->slots;
    if (index < SS_MAX_SLOTS) {
        g_context->slot_used[index] = 0;
        g_context->slot_count--;
        memset(slot, 0, sizeof(ss_slot_t));
    }
}
#else
static ss_signal_t* find_signal(const char* name) {
    ss_signal_t* sig;
    if (!g_context || !name) return NULL;
    
    sig = g_context->signals;
    while (sig) {
        if (strcmp(sig->name, name) == 0)
            return sig;
        sig = sig->next;
    }
    return NULL;
}
#endif

/* Core implementation */
ss_error_t ss_init(void) {
    if (g_context) return SS_OK;
    
    g_context = (ss_context_t*)SS_CALLOC(1, sizeof(ss_context_t));
    if (!g_context) return SS_ERR_MEMORY;
    
    g_context->max_slots_per_signal = SS_DEFAULT_MAX_SLOTS_PER_SIGNAL;
    g_context->thread_safe = SS_ENABLE_THREAD_SAFETY;
    g_context->next_handle = 1;
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) {
        SS_MUTEX_INIT(&g_context->mutex);
    }
#endif
    
    SS_TRACE("Signal-slot library initialized");
    return SS_OK;
}

ss_error_t ss_init_static(void* memory_pool, size_t pool_size) {
    /* For future implementation of custom memory pool */
    (void)memory_pool;
    (void)pool_size;
    return ss_init();
}

void ss_cleanup(void) {
    if (!g_context) return;
    
#if SS_USE_STATIC_MEMORY
    /* Clear all static memory */
    memset(g_context, 0, sizeof(ss_context_t));
#else
    /* Free dynamic memory */
    ss_signal_t* sig = g_context->signals;
    while (sig) {
        ss_signal_t* next_sig = sig->next;
        ss_slot_t* slot = sig->slots;
        
        while (slot) {
            ss_slot_t* next_slot = slot->next;
            SS_FREE(slot);
            slot = next_slot;
        }
        
        SS_FREE(sig->name);
        if (sig->description) SS_FREE(sig->description);
        SS_FREE(sig);
        sig = next_sig;
    }
#endif
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) {
        SS_MUTEX_DESTROY(&g_context->mutex);
    }
#endif
    
    if (g_context->namespace) SS_FREE(g_context->namespace);
    SS_FREE(g_context);
    g_context = NULL;
    
    SS_TRACE("Signal-slot library cleaned up");
}

ss_error_t ss_signal_register(const char* signal_name) {
    return ss_signal_register_ex(signal_name, NULL, SS_PRIORITY_NORMAL);
}

ss_error_t ss_signal_register_ex(const char* signal_name, 
                                const char* description,
                                ss_priority_t priority) {
    ss_signal_t* new_sig;
    
    if (!g_context) return SS_ERR_NULL_PARAM;
    if (!signal_name || strlen(signal_name) == 0) return SS_ERR_NULL_PARAM;
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif
    
    if (find_signal(signal_name)) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_ALREADY_EXISTS;
    }
    
#if SS_USE_STATIC_MEMORY
    /* Find free signal slot */
    size_t i;
    for (i = 0; i < SS_MAX_SIGNALS; i++) {
        if (!g_context->signal_used[i]) {
            new_sig = &g_context->signals[i];
            g_context->signal_used[i] = 1;
            
            /* Use pre-allocated name buffer */
            strncpy(g_context->signal_names[i], signal_name, 
                    SS_MAX_SIGNAL_NAME_LENGTH - 1);
            g_context->signal_names[i][SS_MAX_SIGNAL_NAME_LENGTH - 1] = '\0';
            new_sig->name = g_context->signal_names[i];
            break;
        }
    }
    
    if (i == SS_MAX_SIGNALS) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_WOULD_OVERFLOW;
    }
#else
    new_sig = (ss_signal_t*)SS_CALLOC(1, sizeof(ss_signal_t));
    if (!new_sig) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_MEMORY;
    }
    
    new_sig->name = SS_STRDUP(signal_name);
    if (!new_sig->name) {
        SS_FREE(new_sig);
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_MEMORY;
    }
#endif
    
    if (description) {
        new_sig->description = SS_STRDUP(description);
    }
    new_sig->priority = priority;
    
#if !SS_USE_STATIC_MEMORY
    new_sig->next = g_context->signals;
    g_context->signals = new_sig;
#endif
    
    g_context->signal_count++;
    
#if SS_ENABLE_MEMORY_STATS
    g_context->memory_stats.signals_used++;
    g_context->memory_stats.signals_allocated++;
#endif
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
    
    SS_TRACE("Registered signal: %s", signal_name);
    return SS_OK;
}

ss_error_t ss_connect(const char* signal_name, ss_slot_func_t slot, void* user_data) {
    return ss_connect_ex(signal_name, slot, user_data, SS_PRIORITY_NORMAL, NULL);
}

ss_error_t ss_connect_ex(const char* signal_name, ss_slot_func_t slot, 
                        void* user_data, ss_priority_t priority,
                        ss_connection_t* handle) {
    ss_signal_t* sig;
    ss_slot_t* new_slot;
    
    if (!g_context || !signal_name || !slot) return SS_ERR_NULL_PARAM;
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif
    
    sig = find_signal(signal_name);
    if (!sig) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_NOT_FOUND;
    }
    
    if (sig->slot_count >= g_context->max_slots_per_signal) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_MAX_SLOTS;
    }
    
#if SS_USE_STATIC_MEMORY
    new_slot = allocate_slot();
    if (!new_slot) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_WOULD_OVERFLOW;
    }
#else
    new_slot = (ss_slot_t*)SS_CALLOC(1, sizeof(ss_slot_t));
    if (!new_slot) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_MEMORY;
    }
#endif
    
    new_slot->func = slot;
    new_slot->user_data = user_data;
    new_slot->priority = priority;
    new_slot->handle = g_context->next_handle++;
    
    if (handle) {
        *handle = new_slot->handle;
    }
    
#if SS_USE_STATIC_MEMORY
    /* Insert sorted by priority for static memory */
    if (!sig->slots || priority > sig->slots->priority) {
        new_slot->next = sig->slots;
        sig->slots = new_slot;
    } else {
        ss_slot_t* s = sig->slots;
        while (s->next && s->next->priority >= priority) {
            s = s->next;
        }
        new_slot->next = s->next;
        s->next = new_slot;
    }
#else
    /* Insert at head for dynamic memory (simpler) */
    new_slot->next = sig->slots;
    sig->slots = new_slot;
#endif
    
    sig->slot_count++;
    
#if SS_ENABLE_MEMORY_STATS
    g_context->memory_stats.slots_used++;
    g_context->memory_stats.slots_allocated++;
#endif
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
    
    SS_TRACE("Connected slot to signal: %s (handle: %lu)", signal_name, 
             (unsigned long)new_slot->handle);
    return SS_OK;
}

ss_error_t ss_emit(const char* signal_name, const ss_data_t* data) {
    ss_signal_t* sig;
    ss_slot_t* slot;
    uint64_t start_time = 0;
    
    if (!g_context || !signal_name) return SS_ERR_NULL_PARAM;
    
#if SS_ENABLE_PERFORMANCE_STATS
    if (g_context->profiling_enabled) {
        start_time = get_time_ns();
    }
#endif
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif
    
    sig = find_signal(signal_name);
    if (!sig) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_NOT_FOUND;
    }
    
    SS_TRACE("Emitting signal: %s to %zu slots", signal_name, sig->slot_count);
    
    slot = sig->slots;
    while (slot) {
        slot->func(data, slot->user_data);
        slot = slot->next;
    }
    
#if SS_ENABLE_PERFORMANCE_STATS
    if (g_context->profiling_enabled) {
        uint64_t elapsed = get_time_ns() - start_time;
        sig->perf_stats.total_emissions++;
        sig->perf_stats.total_time_ns += elapsed;
        sig->perf_stats.avg_time_ns = sig->perf_stats.total_time_ns / 
                                      sig->perf_stats.total_emissions;
        if (elapsed > sig->perf_stats.max_time_ns) {
            sig->perf_stats.max_time_ns = elapsed;
        }
        if (sig->perf_stats.min_time_ns == 0 || elapsed < sig->perf_stats.min_time_ns) {
            sig->perf_stats.min_time_ns = elapsed;
        }
    }
#endif
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
    
    return SS_OK;
}

/* Convenience emission functions */
ss_error_t ss_emit_void(const char* signal_name) {
    ss_data_t data = {0};
    data.type = SS_TYPE_VOID;
    return ss_emit(signal_name, &data);
}

ss_error_t ss_emit_int(const char* signal_name, int value) {
    ss_data_t data = {0};
    data.type = SS_TYPE_INT;
    data.value.i_val = value;
    return ss_emit(signal_name, &data);
}

ss_error_t ss_emit_float(const char* signal_name, float value) {
    ss_data_t data = {0};
    data.type = SS_TYPE_FLOAT;
    data.value.f_val = value;
    return ss_emit(signal_name, &data);
}

ss_error_t ss_emit_double(const char* signal_name, double value) {
    ss_data_t data = {0};
    data.type = SS_TYPE_DOUBLE;
    data.value.d_val = value;
    return ss_emit(signal_name, &data);
}

ss_error_t ss_emit_string(const char* signal_name, const char* value) {
    ss_data_t data = {0};
    data.type = SS_TYPE_STRING;
    data.value.s_val = (char*)value;
    return ss_emit(signal_name, &data);
}

ss_error_t ss_emit_pointer(const char* signal_name, void* value) {
    ss_data_t data = {0};
    data.type = SS_TYPE_POINTER;
    data.value.p_val = value;
    return ss_emit(signal_name, &data);
}

#if SS_ENABLE_ISR_SAFE
/* ISR-safe emission - minimal overhead, no locks */
static volatile struct {
    char signal_name[SS_MAX_SIGNAL_NAME_LENGTH];
    int value;
    volatile int pending;
} g_isr_queue[16];

ss_error_t ss_emit_from_isr(const char* signal_name, int value) {
    int i;
    for (i = 0; i < 16; i++) {
        if (!g_isr_queue[i].pending) {
            strncpy((char*)g_isr_queue[i].signal_name, signal_name, 
                   SS_MAX_SIGNAL_NAME_LENGTH - 1);
            g_isr_queue[i].value = value;
            g_isr_queue[i].pending = 1;
            return SS_OK;
        }
    }
    return SS_ERR_WOULD_OVERFLOW;
}
#endif

#if SS_ENABLE_MEMORY_STATS
ss_error_t ss_get_memory_stats(ss_memory_stats_t* stats) {
    if (!g_context || !stats) return SS_ERR_NULL_PARAM;
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif
    
    *stats = g_context->memory_stats;
    
#if SS_USE_STATIC_MEMORY
    stats->signals_allocated = SS_MAX_SIGNALS;
    stats->slots_allocated = SS_MAX_SLOTS;
    stats->total_bytes_allocated = sizeof(ss_context_t);
#else
    /* Calculate dynamic memory usage */
    ss_signal_t* sig = g_context->signals;
    while (sig) {
        stats->string_bytes += strlen(sig->name) + 1;
        if (sig->description) {
            stats->string_bytes += strlen(sig->description) + 1;
        }
        sig = sig->next;
    }
#endif
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
    
    return SS_OK;
}
#endif

#if SS_ENABLE_PERFORMANCE_STATS
ss_error_t ss_get_perf_stats(const char* signal_name, ss_perf_stats_t* stats) {
    ss_signal_t* sig;
    
    if (!g_context || !signal_name || !stats) return SS_ERR_NULL_PARAM;
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif
    
    sig = find_signal(signal_name);
    if (!sig) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_NOT_FOUND;
    }
    
    *stats = sig->perf_stats;
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
    
    return SS_OK;
}

ss_error_t ss_enable_profiling(int enabled) {
    if (!g_context) return SS_ERR_NULL_PARAM;
    g_context->profiling_enabled = enabled;
    return SS_OK;
}
#endif

/* Error handling */
const char* ss_error_string(ss_error_t error) {
    switch (error) {
        case SS_OK: return "Success";
        case SS_ERR_NULL_PARAM: return "Null parameter";
        case SS_ERR_MEMORY: return "Memory allocation failed";
        case SS_ERR_NOT_FOUND: return "Signal not found";
        case SS_ERR_ALREADY_EXISTS: return "Signal already exists";
        case SS_ERR_INVALID_TYPE: return "Invalid data type";
        case SS_ERR_BUFFER_TOO_SMALL: return "Buffer too small";
        case SS_ERR_MAX_SLOTS: return "Maximum slots reached";
        case SS_ERR_TIMEOUT: return "Operation timed out";
        case SS_ERR_WOULD_OVERFLOW: return "Would overflow static buffer";
        case SS_ERR_ISR_UNSAFE: return "Operation not ISR-safe";
        default: return "Unknown error";
    }
}

#if SS_ENABLE_DEBUG_TRACE
void ss_enable_trace(FILE* output) {
    if (g_context) {
        g_context->trace_output = output;
    }
}

void ss_disable_trace(void) {
    if (g_context) {
        g_context->trace_output = NULL;
    }
}
#endif
#endif /* SS_IMPLEMENTATION */

#endif /* SS_LIB_SINGLE_H */
