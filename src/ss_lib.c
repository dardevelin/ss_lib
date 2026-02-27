#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L  /* For strdup on macOS */
#define _GNU_SOURCE               /* For strdup on Linux */
#endif

#include "ss_lib.h"
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
    int removed;           /* Deferred removal flag for safe emit iteration */
} ss_slot_t;

typedef struct ss_signal {
    char* name;
    char* description;
    ss_slot_t* slots;
    size_t slot_count;
    ss_priority_t priority;
    int emitting;          /* Non-zero while slots are being invoked */
#if SS_ENABLE_PERFORMANCE_STATS
    ss_perf_stats_t perf_stats;
#endif

#if !SS_USE_STATIC_MEMORY
    struct ss_signal* next;
#endif

} ss_signal_t;

typedef struct {
    char signal_name[SS_MAX_SIGNAL_NAME_LENGTH];
    ss_data_t data;
    int has_string;  /* Non-zero if data.value.s_val was duplicated */
} ss_deferred_entry_t;

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

    /* Deferred emission queue */
    ss_deferred_entry_t deferred_queue[SS_DEFERRED_QUEUE_SIZE];
    size_t deferred_count;
    
#if SS_ENABLE_DEBUG_TRACE
    FILE* trace_output;
#endif

} ss_context_t;

/* Global context */
static ss_context_t* g_context = NULL;

/* Helper functions */

/*
 * ss_strscpy - Safe string copy with guaranteed null-termination
 *
 * Copies up to (count - 1) characters from src to dst, always
 * null-terminates. Does not pad with zeroes (unlike strncpy).
 * Returns number of characters copied, or -1 if truncated.
 */
static long ss_strscpy(char* dst, const char* src, size_t count) {
    size_t i;
    if (count == 0) return -1;
    for (i = 0; i < count - 1 && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
    if (src[i] != '\0') return -1;
    return (long)i;
}

#if SS_ENABLE_PERFORMANCE_STATS
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
#endif

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
            {
                size_t peak = g_context->slot_count * sizeof(ss_slot_t);
                if (peak > g_context->memory_stats.peak_bytes_allocated) {
                    g_context->memory_stats.peak_bytes_allocated = peak;
                }
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

/* Sweep slots marked as removed after emission completes */
static void sweep_removed_slots(ss_signal_t* sig) {
    ss_slot_t* prev = NULL;
    ss_slot_t* curr = sig->slots;
    while (curr) {
        ss_slot_t* next = curr->next;
        if (curr->removed) {
            if (prev) {
                prev->next = next;
            } else {
                sig->slots = next;
            }
            sig->slot_count--;
#if SS_USE_STATIC_MEMORY
            free_slot(curr);
#else
            SS_FREE(curr);
#endif
        } else {
            prev = curr;
        }
        curr = next;
    }
}

/* Error handler */
static void report_error(ss_error_t error, const char* msg) {
    if (g_context && g_context->error_handler) {
        g_context->error_handler(error, msg);
    }
}

/* Core implementation */
ss_error_t ss_init(void) {
    if (g_context) return SS_OK;
    
    g_context = (ss_context_t*)SS_CALLOC(1, sizeof(ss_context_t));
    if (!g_context) return SS_ERR_MEMORY;
    
    g_context->max_slots_per_signal = SS_DEFAULT_MAX_SLOTS_PER_SIGNAL;
    g_context->thread_safe = 0;  /* Thread safety disabled by default, enable with ss_set_thread_safe(1) */
    g_context->next_handle = 1;

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
    /* Free dynamically-allocated description strings before clearing */
    {
        size_t i;
        for (i = 0; i < SS_MAX_SIGNALS; i++) {
            if (g_context->signal_used[i] && g_context->signals[i].description) {
                SS_FREE(g_context->signals[i].description);
            }
        }
    }
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

    
    /* Free pending deferred emission strings */
    {
        size_t i;
        for (i = 0; i < g_context->deferred_count; i++) {
            if (g_context->deferred_queue[i].has_string) {
                SS_FREE((void*)g_context->deferred_queue[i].data.value.s_val);
            }
        }
    }

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
    if (!signal_name || strlen(signal_name) == 0) {
        report_error(SS_ERR_NULL_PARAM, "signal name is NULL or empty");
        return SS_ERR_NULL_PARAM;
    }
    if (strlen(signal_name) >= SS_MAX_SIGNAL_NAME_LENGTH) {
        report_error(SS_ERR_WOULD_OVERFLOW, "signal name exceeds maximum length");
        return SS_ERR_WOULD_OVERFLOW;
    }

#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif

    if (find_signal(signal_name)) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        report_error(SS_ERR_ALREADY_EXISTS, signal_name);
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
            ss_strscpy(g_context->signal_names[i], signal_name,
                        SS_MAX_SIGNAL_NAME_LENGTH);
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
    
    if (!g_context || !signal_name || !slot) {
        report_error(SS_ERR_NULL_PARAM, "connect requires signal name and slot");
        return SS_ERR_NULL_PARAM;
    }

#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif


    sig = find_signal(signal_name);
    if (!sig) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        report_error(SS_ERR_NOT_FOUND, signal_name);
        return SS_ERR_NOT_FOUND;
    }

    if (sig->slot_count >= g_context->max_slots_per_signal) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        report_error(SS_ERR_MAX_SLOTS, signal_name);
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
    
    /* Priority-sorted insertion: higher priority slots execute first */
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
    
    if (!g_context || !signal_name) {
        report_error(SS_ERR_NULL_PARAM, "emit requires signal name");
        return SS_ERR_NULL_PARAM;
    }

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
        report_error(SS_ERR_NOT_FOUND, signal_name);
        return SS_ERR_NOT_FOUND;
    }
    
    SS_TRACE("Emitting signal: %s to %zu slots", signal_name, sig->slot_count);
    
    sig->emitting++;
    slot = sig->slots;
    while (slot) {
        ss_slot_t* next_slot = slot->next;
        if (!slot->removed) {
            slot->func(data, slot->user_data);
        }
        slot = next_slot;
    }
    sig->emitting--;
    if (sig->emitting == 0) {
        sweep_removed_slots(sig);
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
    data.value.s_val = value;
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
} g_isr_queue[SS_ISR_QUEUE_SIZE];

/* Portable compiler write barrier for ISR safety */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#define SS_WRITE_BARRIER() atomic_signal_fence(memory_order_release)
#elif defined(__GNUC__)
#define SS_WRITE_BARRIER() __asm__ volatile("" ::: "memory")
#else
#define SS_WRITE_BARRIER() ((void)0)
#endif

ss_error_t ss_emit_from_isr(const char* signal_name, int value) {
    int i;
    if (!signal_name) return SS_ERR_NULL_PARAM;
    for (i = 0; i < SS_ISR_QUEUE_SIZE; i++) {
        if (!g_isr_queue[i].pending) {
            ss_strscpy((char*)g_isr_queue[i].signal_name, signal_name,
                       SS_MAX_SIGNAL_NAME_LENGTH);
            g_isr_queue[i].value = value;
            SS_WRITE_BARRIER();
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
        SS_FREE((void*)data->value.s_val);
    }
#if SS_ENABLE_CUSTOM_DATA
    else if (data->type == SS_TYPE_CUSTOM && data->custom_data) {
        if (data->custom_cleanup) {
            data->custom_cleanup(data->custom_data);
        } else {
            SS_FREE(data->custom_data);
        }
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
        SS_FREE((void*)data->value.s_val);
    }

    data->type = SS_TYPE_STRING;
    if (value) {
        data->value.s_val = SS_STRDUP(value);
        if (!data->value.s_val) return SS_ERR_MEMORY;
    } else {
        data->value.s_val = NULL;
    }
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
    if (!data || !value || size == 0) return SS_ERR_NULL_PARAM;

    if (data->type == SS_TYPE_CUSTOM && data->custom_data) {
        if (data->custom_cleanup) {
            data->custom_cleanup(data->custom_data);
        } else {
            SS_FREE(data->custom_data);
        }
    }

    data->type = SS_TYPE_CUSTOM;
    data->custom_data = SS_MALLOC(size);
    if (!data->custom_data) return SS_ERR_MEMORY;
    data->custom_cleanup = cleanup;
    
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
    stats->string_bytes = 0;
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

void ss_reset_memory_stats(void) {
    if (!g_context) return;

#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif

    memset(&g_context->memory_stats, 0, sizeof(ss_memory_stats_t));

#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
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

void ss_reset_perf_stats(void) {
    if (!g_context) return;

#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif

#if SS_USE_STATIC_MEMORY
    {
        size_t i;
        for (i = 0; i < SS_MAX_SIGNALS; i++) {
            if (g_context->signal_used[i]) {
                memset(&g_context->signals[i].perf_stats, 0, sizeof(ss_perf_stats_t));
            }
        }
    }
#else
    {
        ss_signal_t* sig = g_context->signals;
        while (sig) {
            memset(&sig->perf_stats, 0, sizeof(ss_perf_stats_t));
            sig = sig->next;
        }
    }
#endif

#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
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

/* Signal existence check */
int ss_signal_exists(const char* signal_name) {
    if (!g_context || !signal_name) return 0;
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif
    
    ss_signal_t* sig = find_signal(signal_name);
    int exists = (sig != NULL);
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
    
    return exists;
}

/* Disconnect using handle */
ss_error_t ss_disconnect_handle(ss_connection_t handle) {
    if (!g_context || handle == 0) return SS_ERR_NULL_PARAM;

#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif

    ss_error_t result = SS_ERR_NOT_FOUND;

#if SS_USE_STATIC_MEMORY
    size_t i;
    for (i = 0; i < SS_MAX_SIGNALS; i++) {
        if (!g_context->signal_used[i]) continue;

        ss_signal_t* sig = &g_context->signals[i];
        ss_slot_t* prev = NULL;
        ss_slot_t* curr = sig->slots;

        while (curr) {
            if (curr->handle == handle) {
                if (sig->emitting) {
                    curr->removed = 1;
                } else {
                    if (prev) {
                        prev->next = curr->next;
                    } else {
                        sig->slots = curr->next;
                    }
                    sig->slot_count--;
                    free_slot(curr);
                }
                result = SS_OK;
                goto done;
            }
            prev = curr;
            curr = curr->next;
        }
    }
#else
    ss_signal_t* sig = g_context->signals;
    while (sig) {
        ss_slot_t* prev = NULL;
        ss_slot_t* curr = sig->slots;

        while (curr) {
            if (curr->handle == handle) {
                if (sig->emitting) {
                    curr->removed = 1;
                } else {
                    if (prev) {
                        prev->next = curr->next;
                    } else {
                        sig->slots = curr->next;
                    }
                    SS_FREE(curr);
                    sig->slot_count--;
                }
                result = SS_OK;
                goto done;
            }
            prev = curr;
            curr = curr->next;
        }
        sig = sig->next;
    }
#endif

done:
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif

    return result;
}

/* Disconnect all slots from a signal */
ss_error_t ss_disconnect_all(const char* signal_name) {
    if (!g_context || !signal_name) return SS_ERR_NULL_PARAM;
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif
    
    ss_signal_t* sig = find_signal(signal_name);
    if (!sig) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_NOT_FOUND;
    }

    if (sig->emitting) {
        /* Defer removal: mark all slots as removed */
        ss_slot_t* curr = sig->slots;
        while (curr) {
            curr->removed = 1;
            curr = curr->next;
        }
    } else {
#if SS_USE_STATIC_MEMORY
        ss_slot_t* curr = sig->slots;
        while (curr) {
            ss_slot_t* next = curr->next;
            free_slot(curr);
            curr = next;
        }
#else
        ss_slot_t* curr = sig->slots;
        while (curr) {
            ss_slot_t* next = curr->next;
            SS_FREE(curr);
            curr = next;
        }
#endif
        sig->slots = NULL;
        sig->slot_count = 0;
    }

#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif

    return SS_OK;
}

/* Disconnect a specific slot from a signal */
ss_error_t ss_disconnect(const char* signal_name, ss_slot_func_t slot) {
    if (!g_context || !signal_name || !slot) return SS_ERR_NULL_PARAM;
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif
    
    ss_signal_t* sig = find_signal(signal_name);
    if (!sig) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_NOT_FOUND;
    }
    
    ss_slot_t* prev = NULL;
    ss_slot_t* curr = sig->slots;

    while (curr) {
        if (curr->func == slot && !curr->removed) {
            if (sig->emitting) {
                curr->removed = 1;
            } else {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    sig->slots = curr->next;
                }
                sig->slot_count--;
#if SS_USE_STATIC_MEMORY
                free_slot(curr);
#else
                SS_FREE(curr);
#endif
            }

#if SS_ENABLE_THREAD_SAFETY
            if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
            return SS_OK;
        }
        prev = curr;
        curr = curr->next;
    }
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
    
    return SS_ERR_NOT_FOUND;
}

/* Unregister a signal */
ss_error_t ss_signal_unregister(const char* signal_name) {
    if (!g_context || !signal_name) return SS_ERR_NULL_PARAM;
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif
    
    ss_signal_t* sig = find_signal(signal_name);
    if (!sig) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_NOT_FOUND;
    }
    
    /* First disconnect all slots - do it inline to avoid deadlock */
#if SS_USE_STATIC_MEMORY
    /* Mark all slots as unused in static allocation */
    ss_slot_t* curr = sig->slots;
    while (curr) {
        size_t i;
        for (i = 0; i < SS_MAX_SLOTS; i++) {
            if (&g_context->slots[i] == curr) {
                g_context->slot_used[i] = 0;
                g_context->slot_count--;
                break;
            }
        }
        curr = curr->next;
    }
#else
    /* Free all slots for dynamic memory */
    ss_slot_t* curr = sig->slots;
    while (curr) {
        ss_slot_t* next = curr->next;
        SS_FREE(curr);
        curr = next;
    }
#endif
    
    sig->slots = NULL;
    sig->slot_count = 0;
    
#if SS_USE_STATIC_MEMORY
    /* Mark signal as unused in static allocation */
    size_t i;
    for (i = 0; i < SS_MAX_SIGNALS; i++) {
        if (&g_context->signals[i] == sig) {
            g_context->signal_used[i] = 0;
            g_context->signal_count--;
            break;
        }
    }
#else
    /* Remove from linked list in dynamic allocation */
    if (sig->name) SS_FREE(sig->name);
    if (sig->description) SS_FREE(sig->description);
    
    ss_signal_t** sig_curr = &g_context->signals;
    while (*sig_curr) {
        if (*sig_curr == sig) {
            *sig_curr = sig->next;
            SS_FREE(sig);
            g_context->signal_count--;
            break;
        }
        sig_curr = &(*sig_curr)->next;
    }
#endif
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
    
    return SS_OK;
}

#if SS_ENABLE_INTROSPECTION
/* Get the number of registered signals */
size_t ss_get_signal_count(void) {
    if (!g_context) return 0;
    return g_context->signal_count;
}

/* Get list of registered signals */
ss_error_t ss_get_signal_list(ss_signal_info_t** list, size_t* count) {
    if (!g_context || !list || !count) return SS_ERR_NULL_PARAM;
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
#endif
    
    *count = g_context->signal_count;
    if (*count == 0) {
        *list = NULL;
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_OK;
    }
    
    *list = (ss_signal_info_t*)SS_CALLOC(*count, sizeof(ss_signal_info_t));
    if (!*list) {
#if SS_ENABLE_THREAD_SAFETY
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
        return SS_ERR_MEMORY;
    }
    
    size_t idx = 0;
#if SS_USE_STATIC_MEMORY
    size_t i;
    for (i = 0; i < SS_MAX_SIGNALS && idx < *count; i++) {
        if (g_context->signal_used[i]) {
            (*list)[idx].name = SS_STRDUP(g_context->signals[i].name);
            (*list)[idx].description = g_context->signals[i].description;
            (*list)[idx].slot_count = g_context->signals[i].slot_count;
            (*list)[idx].priority = g_context->signals[i].priority;
            idx++;
        }
    }
#else
    ss_signal_t* sig = g_context->signals;
    while (sig && idx < *count) {
        (*list)[idx].name = SS_STRDUP(sig->name);
        (*list)[idx].description = sig->description;
        (*list)[idx].slot_count = sig->slot_count;
        (*list)[idx].priority = sig->priority;
        sig = sig->next;
        idx++;
    }
#endif
    
#if SS_ENABLE_THREAD_SAFETY
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
#endif
    
    return SS_OK;
}

/* Free signal list allocated by ss_get_signal_list */
void ss_free_signal_list(ss_signal_info_t* list, size_t count) {
    if (list) {
        size_t i;
        for (i = 0; i < count; i++) {
            if (list[i].name) SS_FREE(list[i].name);
        }
        SS_FREE(list);
    }
}
#endif

/* Set maximum slots per signal */
void ss_set_max_slots_per_signal(size_t max_slots) {
    if (g_context) {
        g_context->max_slots_per_signal = max_slots;
    }
}

/* Get maximum slots per signal */
size_t ss_get_max_slots_per_signal(void) {
    if (!g_context) return 0;
    return g_context->max_slots_per_signal;
}

void ss_set_error_handler(void (*handler)(ss_error_t error, const char* msg)) {
    if (g_context) {
        g_context->error_handler = handler;
    }
}

/* Set thread safety */
void ss_set_thread_safe(int enabled) {
#if SS_ENABLE_THREAD_SAFETY
    if (!g_context) return;
    
    /* If enabling thread safety for the first time, initialize mutex */
    if (enabled && !g_context->thread_safe) {
        SS_MUTEX_INIT(&g_context->mutex);
    }
    /* If disabling thread safety, destroy mutex */
    else if (!enabled && g_context->thread_safe) {
        SS_MUTEX_DESTROY(&g_context->mutex);
    }
    
    g_context->thread_safe = enabled;
#else
    (void)enabled; /* unused */
#endif
}

/* Check if thread safety is enabled */
int ss_is_thread_safe(void) {
    if (!g_context) return 0;
    return g_context->thread_safe;
}

/* Namespace support */
ss_error_t ss_set_namespace(const char* ns) {
    if (!g_context) return SS_ERR_NULL_PARAM;

    if (g_context->namespace) {
        SS_FREE(g_context->namespace);
        g_context->namespace = NULL;
    }

    if (ns) {
        g_context->namespace = SS_STRDUP(ns);
        if (!g_context->namespace) return SS_ERR_MEMORY;
    }

    return SS_OK;
}

const char* ss_get_namespace(void) {
    if (!g_context) return NULL;
    return g_context->namespace;
}

ss_error_t ss_emit_namespaced(const char* ns, const char* signal_name,
                              const ss_data_t* data) {
    char buf[SS_MAX_SIGNAL_NAME_LENGTH];
    size_t ns_len, name_len;

    if (!ns || !signal_name) {
        report_error(SS_ERR_NULL_PARAM, "namespace and signal name required");
        return SS_ERR_NULL_PARAM;
    }

    ns_len = strlen(ns);
    name_len = strlen(signal_name);

    /* "ns::signal" needs ns_len + 2 + name_len + 1 */
    if (ns_len + 2 + name_len + 1 > SS_MAX_SIGNAL_NAME_LENGTH) {
        report_error(SS_ERR_WOULD_OVERFLOW, "namespaced signal name too long");
        return SS_ERR_WOULD_OVERFLOW;
    }

    memcpy(buf, ns, ns_len);
    buf[ns_len] = ':';
    buf[ns_len + 1] = ':';
    memcpy(buf + ns_len + 2, signal_name, name_len);
    buf[ns_len + 2 + name_len] = '\0';

    return ss_emit(buf, data);
}

/* Deferred emission */
ss_error_t ss_emit_deferred(const char* signal_name, const ss_data_t* data) {
    ss_deferred_entry_t* entry;

    if (!g_context || !signal_name) {
        report_error(SS_ERR_NULL_PARAM, "deferred emit requires signal name");
        return SS_ERR_NULL_PARAM;
    }

    if (g_context->deferred_count >= SS_DEFERRED_QUEUE_SIZE) {
        report_error(SS_ERR_WOULD_OVERFLOW, "deferred queue full");
        return SS_ERR_WOULD_OVERFLOW;
    }

    entry = &g_context->deferred_queue[g_context->deferred_count];
    ss_strscpy(entry->signal_name, signal_name, SS_MAX_SIGNAL_NAME_LENGTH);
    entry->has_string = 0;

    if (data) {
        entry->data = *data;
        /* Duplicate string data to avoid dangling pointer */
        if (data->type == SS_TYPE_STRING && data->value.s_val) {
            entry->data.value.s_val = SS_STRDUP(data->value.s_val);
            if (!entry->data.value.s_val) return SS_ERR_MEMORY;
            entry->has_string = 1;
        }
    } else {
        memset(&entry->data, 0, sizeof(ss_data_t));
        entry->data.type = SS_TYPE_VOID;
    }

    g_context->deferred_count++;
    return SS_OK;
}

ss_error_t ss_flush_deferred(void) {
    size_t i, count;
    ss_error_t result = SS_OK;

    if (!g_context) return SS_ERR_NULL_PARAM;

    /* Snapshot count to avoid infinite loops if slots enqueue more */
    count = g_context->deferred_count;
    g_context->deferred_count = 0;

    for (i = 0; i < count; i++) {
        ss_deferred_entry_t* entry = &g_context->deferred_queue[i];
        ss_error_t err = ss_emit(entry->signal_name, &entry->data);
        if (err != SS_OK) result = err;

        if (entry->has_string) {
            SS_FREE((void*)entry->data.value.s_val);
        }
    }

    return result;
}

/* Batch operations */
#ifndef SS_BATCH_MAX_ENTRIES
#define SS_BATCH_MAX_ENTRIES SS_DEFERRED_QUEUE_SIZE
#endif

struct ss_batch {
    ss_deferred_entry_t entries[SS_BATCH_MAX_ENTRIES];
    size_t count;
};

ss_batch_t* ss_batch_create(void) {
    return (ss_batch_t*)SS_CALLOC(1, sizeof(ss_batch_t));
}

void ss_batch_destroy(ss_batch_t* batch) {
    size_t i;
    if (!batch) return;

    for (i = 0; i < batch->count; i++) {
        if (batch->entries[i].has_string) {
            SS_FREE((void*)batch->entries[i].data.value.s_val);
        }
    }
    SS_FREE(batch);
}

ss_error_t ss_batch_add(ss_batch_t* batch, const char* signal_name,
                        const ss_data_t* data) {
    ss_deferred_entry_t* entry;

    if (!batch || !signal_name) return SS_ERR_NULL_PARAM;
    if (batch->count >= SS_BATCH_MAX_ENTRIES) return SS_ERR_WOULD_OVERFLOW;

    entry = &batch->entries[batch->count];
    ss_strscpy(entry->signal_name, signal_name, SS_MAX_SIGNAL_NAME_LENGTH);
    entry->has_string = 0;

    if (data) {
        entry->data = *data;
        if (data->type == SS_TYPE_STRING && data->value.s_val) {
            entry->data.value.s_val = SS_STRDUP(data->value.s_val);
            if (!entry->data.value.s_val) return SS_ERR_MEMORY;
            entry->has_string = 1;
        }
    } else {
        memset(&entry->data, 0, sizeof(ss_data_t));
        entry->data.type = SS_TYPE_VOID;
    }

    batch->count++;
    return SS_OK;
}

ss_error_t ss_batch_emit(ss_batch_t* batch) {
    size_t i;
    ss_error_t result = SS_OK;

    if (!batch) return SS_ERR_NULL_PARAM;

    for (i = 0; i < batch->count; i++) {
        ss_deferred_entry_t* entry = &batch->entries[i];
        ss_error_t err = ss_emit(entry->signal_name, &entry->data);
        if (err != SS_OK) result = err;

        if (entry->has_string) {
            SS_FREE((void*)entry->data.value.s_val);
            entry->has_string = 0;
        }
    }

    batch->count = 0;
    return result;
}
