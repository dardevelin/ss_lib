#include "ss_lib_v2.h"
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
    struct ss_slot* next;  /* Always needed for slot chains */
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
    g_context->memory_stats.signals_used = g_context->signal_count;
#if SS_USE_STATIC_MEMORY
    g_context->memory_stats.signals_allocated = SS_MAX_SIGNALS;
    g_context->memory_stats.slots_allocated = SS_MAX_SLOTS;
#else  
    g_context->memory_stats.signals_allocated++;
#endif
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
    
#if !SS_USE_STATIC_MEMORY
    /* Insert at head for dynamic memory (simpler) */
    new_slot->next = sig->slots;
#endif

    sig->slots = new_slot;
    
    sig->slot_count++;
    
#if SS_ENABLE_MEMORY_STATS
    /* Update total slot count across all signals */
    size_t total_slots = 0;
#if SS_USE_STATIC_MEMORY
    size_t i;
    for (i = 0; i < SS_MAX_SIGNALS; i++) {
        if (g_context->signal_used[i]) {
            total_slots += g_context->signals[i].slot_count;
        }
    }
#else
    ss_signal_t* s = g_context->signals;
    while (s) {
        total_slots += s->slot_count;
        s = s->next;
    }
#endif
    g_context->memory_stats.slots_used = total_slots;
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
#if SS_ENABLE_PERFORMANCE_STATS
    uint64_t start_time = 0;
#endif
    
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
#if SS_USE_STATIC_MEMORY
        /* For static memory, need to find next valid slot */
        if (slot->next) {
            slot = slot->next;
        } else {
            break;
        }
#else
        slot = slot->next;
#endif

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
    char signal_name[32];  /* Fixed size for ISR safety */
    int value;
    volatile int pending;
} g_isr_queue[16];

ss_error_t ss_emit_from_isr(const char* signal_name, int value) {
    int i;
    for (i = 0; i < 16; i++) {
        if (!g_isr_queue[i].pending) {
            strncpy((char*)g_isr_queue[i].signal_name, signal_name, 31);
            g_isr_queue[i].signal_name[31] = '\0';
            g_isr_queue[i].value = value;
            g_isr_queue[i].pending = 1;
            return SS_OK;
        }
    }
    return SS_ERR_WOULD_OVERFLOW;
}
#endif

/* Data handling functions */
ss_data_t* ss_data_create(ss_data_type_t type) {
    ss_data_t* data = (ss_data_t*)SS_CALLOC(1, sizeof(ss_data_t));
    if (data) {
        data->type = type;
    }
    return data;
}

void ss_data_destroy(ss_data_t* data) {
    if (!data) return;
    
    if (data->type == SS_TYPE_STRING && data->value.s_val) {
        SS_FREE(data->value.s_val);
    }
#if SS_ENABLE_CUSTOM_DATA
    else if (data->type == SS_TYPE_CUSTOM && data->custom_data) {
        SS_FREE(data->custom_data);
    }
#endif
    
    SS_FREE(data);
}

ss_error_t ss_data_set_int(ss_data_t* data, int value) {
    if (!data) return SS_ERR_NULL_PARAM;
    data->type = SS_TYPE_INT;
    data->value.i_val = value;
    return SS_OK;
}

ss_error_t ss_data_set_float(ss_data_t* data, float value) {
    if (!data) return SS_ERR_NULL_PARAM;
    data->type = SS_TYPE_FLOAT;
    data->value.f_val = value;
    return SS_OK;
}

ss_error_t ss_data_set_double(ss_data_t* data, double value) {
    if (!data) return SS_ERR_NULL_PARAM;
    data->type = SS_TYPE_DOUBLE;
    data->value.d_val = value;
    return SS_OK;
}

ss_error_t ss_data_set_string(ss_data_t* data, const char* value) {
    if (!data) return SS_ERR_NULL_PARAM;
    
    if (data->type == SS_TYPE_STRING && data->value.s_val) {
        SS_FREE(data->value.s_val);
    }
    
    data->type = SS_TYPE_STRING;
    data->value.s_val = value ? SS_STRDUP(value) : NULL;
    return SS_OK;
}

ss_error_t ss_data_set_pointer(ss_data_t* data, void* value) {
    if (!data) return SS_ERR_NULL_PARAM;
    data->type = SS_TYPE_POINTER;
    data->value.p_val = value;
    return SS_OK;
}

#if SS_ENABLE_CUSTOM_DATA
ss_error_t ss_data_set_custom(ss_data_t* data, void* value, size_t size, 
                             ss_cleanup_func_t cleanup) {
    (void)cleanup; /* Reserved for future use */
    if (!data || !value) return SS_ERR_NULL_PARAM;
    
    if (data->type == SS_TYPE_CUSTOM && data->custom_data) {
        SS_FREE(data->custom_data);
    }
    
    data->type = SS_TYPE_CUSTOM;
    data->custom_data = SS_MALLOC(size);
    if (!data->custom_data) return SS_ERR_MEMORY;
    
    memcpy(data->custom_data, value, size);
    data->size = size;
    return SS_OK;
}
#endif

/* Data retrieval functions */
int ss_data_get_int(const ss_data_t* data, int default_val) {
    if (!data || data->type != SS_TYPE_INT) return default_val;
    return data->value.i_val;
}

float ss_data_get_float(const ss_data_t* data, float default_val) {
    if (!data || data->type != SS_TYPE_FLOAT) return default_val;
    return data->value.f_val;
}

double ss_data_get_double(const ss_data_t* data, double default_val) {
    if (!data || data->type != SS_TYPE_DOUBLE) return default_val;
    return data->value.d_val;
}

const char* ss_data_get_string(const ss_data_t* data) {
    if (!data || data->type != SS_TYPE_STRING) return NULL;
    return data->value.s_val;
}

void* ss_data_get_pointer(const ss_data_t* data) {
    if (!data || data->type != SS_TYPE_POINTER) return NULL;
    return data->value.p_val;
}

#if SS_ENABLE_CUSTOM_DATA
void* ss_data_get_custom(const ss_data_t* data, size_t* size) {
    if (!data || data->type != SS_TYPE_CUSTOM) return NULL;
    if (size) *size = data->size;
    return data->custom_data;
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
#else
/* Stub when profiling is disabled */
ss_error_t ss_enable_profiling(int enabled) {
    (void)enabled;
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
