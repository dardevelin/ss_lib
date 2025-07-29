#include "ss_lib.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

typedef struct ss_slot {
    ss_slot_func_t func;
    void* user_data;
    struct ss_slot* next;
} ss_slot_t;

typedef struct ss_signal {
    char* name;
    ss_slot_t* slots;
    size_t slot_count;
    struct ss_signal* next;
} ss_signal_t;

typedef struct {
    ss_signal_t* signals;
    size_t signal_count;
    size_t max_slots_per_signal;
    int thread_safe;
    ss_mutex_t mutex;
} ss_context_t;

static ss_context_t* g_context = NULL;

/* Reserved for future hash table implementation
static unsigned int hash_string(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}
*/

static ss_signal_t* find_signal(const char* name) {
    if (!g_context || !name) return NULL;
    
    ss_signal_t* sig = g_context->signals;
    while (sig) {
        if (strcmp(sig->name, name) == 0)
            return sig;
        sig = sig->next;
    }
    return NULL;
}

ss_error_t ss_init(void) {
    if (g_context) return SS_OK;
    
    g_context = (ss_context_t*)calloc(1, sizeof(ss_context_t));
    if (!g_context) return SS_ERR_MEMORY;
    
    g_context->max_slots_per_signal = 100;
    g_context->thread_safe = 0;
    
    return SS_OK;
}

void ss_cleanup(void) {
    ss_signal_t* sig;
    if (!g_context) return;
    
    sig = g_context->signals;
    while (sig) {
        ss_signal_t* next_sig = sig->next;
        
        ss_slot_t* slot = sig->slots;
        while (slot) {
            ss_slot_t* next_slot = slot->next;
            free(slot);
            slot = next_slot;
        }
        
        free(sig->name);
        free(sig);
        sig = next_sig;
    }
    
    if (g_context->thread_safe) {
        SS_MUTEX_DESTROY(&g_context->mutex);
    }
    
    free(g_context);
    g_context = NULL;
}

ss_error_t ss_signal_register(const char* signal_name) {
    if (!g_context) return SS_ERR_NULL_PARAM;
    if (!signal_name || strlen(signal_name) == 0) return SS_ERR_NULL_PARAM;
    
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
    
    if (find_signal(signal_name)) {
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
        return SS_ERR_ALREADY_EXISTS;
    }
    
    ss_signal_t* new_sig = (ss_signal_t*)calloc(1, sizeof(ss_signal_t));
    if (!new_sig) {
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
        return SS_ERR_MEMORY;
    }
    
    new_sig->name = strdup(signal_name);
    if (!new_sig->name) {
        free(new_sig);
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
        return SS_ERR_MEMORY;
    }
    
    new_sig->next = g_context->signals;
    g_context->signals = new_sig;
    g_context->signal_count++;
    
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
    return SS_OK;
}

ss_error_t ss_signal_unregister(const char* signal_name) {
    if (!g_context || !signal_name) return SS_ERR_NULL_PARAM;
    
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
    
    ss_signal_t* sig = g_context->signals;
    ss_signal_t* prev = NULL;
    
    while (sig) {
        if (strcmp(sig->name, signal_name) == 0) {
            if (prev) {
                prev->next = sig->next;
            } else {
                g_context->signals = sig->next;
            }
            
            ss_slot_t* slot = sig->slots;
            while (slot) {
                ss_slot_t* next = slot->next;
                free(slot);
                slot = next;
            }
            
            free(sig->name);
            free(sig);
            g_context->signal_count--;
            
            if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
            return SS_OK;
        }
        prev = sig;
        sig = sig->next;
    }
    
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
    return SS_ERR_NOT_FOUND;
}

int ss_signal_exists(const char* signal_name) {
    if (!g_context || !signal_name) return 0;
    
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
    int exists = (find_signal(signal_name) != NULL);
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
    
    return exists;
}

ss_error_t ss_connect(const char* signal_name, ss_slot_func_t slot, void* user_data) {
    if (!g_context || !signal_name || !slot) return SS_ERR_NULL_PARAM;
    
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
    
    ss_signal_t* sig = find_signal(signal_name);
    if (!sig) {
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
        return SS_ERR_NOT_FOUND;
    }
    
    if (sig->slot_count >= g_context->max_slots_per_signal) {
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
        return SS_ERR_MAX_SLOTS;
    }
    
    ss_slot_t* new_slot = (ss_slot_t*)calloc(1, sizeof(ss_slot_t));
    if (!new_slot) {
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
        return SS_ERR_MEMORY;
    }
    
    new_slot->func = slot;
    new_slot->user_data = user_data;
    new_slot->next = sig->slots;
    sig->slots = new_slot;
    sig->slot_count++;
    
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
    return SS_OK;
}

ss_error_t ss_disconnect(const char* signal_name, ss_slot_func_t slot) {
    if (!g_context || !signal_name || !slot) return SS_ERR_NULL_PARAM;
    
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
    
    ss_signal_t* sig = find_signal(signal_name);
    if (!sig) {
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
        return SS_ERR_NOT_FOUND;
    }
    
    ss_slot_t* s = sig->slots;
    ss_slot_t* prev = NULL;
    
    while (s) {
        if (s->func == slot) {
            if (prev) {
                prev->next = s->next;
            } else {
                sig->slots = s->next;
            }
            free(s);
            sig->slot_count--;
            
            if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
            return SS_OK;
        }
        prev = s;
        s = s->next;
    }
    
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
    return SS_ERR_NOT_FOUND;
}

ss_error_t ss_disconnect_all(const char* signal_name) {
    if (!g_context || !signal_name) return SS_ERR_NULL_PARAM;
    
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
    
    ss_signal_t* sig = find_signal(signal_name);
    if (!sig) {
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
        return SS_ERR_NOT_FOUND;
    }
    
    ss_slot_t* slot = sig->slots;
    while (slot) {
        ss_slot_t* next = slot->next;
        free(slot);
        slot = next;
    }
    
    sig->slots = NULL;
    sig->slot_count = 0;
    
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
    return SS_OK;
}

ss_error_t ss_emit(const char* signal_name, const ss_data_t* data) {
    if (!g_context || !signal_name) return SS_ERR_NULL_PARAM;
    
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
    
    ss_signal_t* sig = find_signal(signal_name);
    if (!sig) {
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
        return SS_ERR_NOT_FOUND;
    }
    
    ss_slot_t* slot = sig->slots;
    while (slot) {
        slot->func(data, slot->user_data);
        slot = slot->next;
    }
    
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
    return SS_OK;
}

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

ss_data_t* ss_data_create(ss_data_type_t type) {
    ss_data_t* data = (ss_data_t*)calloc(1, sizeof(ss_data_t));
    if (data) {
        data->type = type;
    }
    return data;
}

void ss_data_destroy(ss_data_t* data) {
    if (!data) return;
    
    if (data->type == SS_TYPE_STRING && data->value.s_val) {
        free(data->value.s_val);
    } else if (data->type == SS_TYPE_CUSTOM && data->custom_data) {
        free(data->custom_data);
    }
    
    free(data);
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
        free(data->value.s_val);
    }
    
    data->type = SS_TYPE_STRING;
    data->value.s_val = value ? strdup(value) : NULL;
    return SS_OK;
}

ss_error_t ss_data_set_pointer(ss_data_t* data, void* value) {
    if (!data) return SS_ERR_NULL_PARAM;
    data->type = SS_TYPE_POINTER;
    data->value.p_val = value;
    return SS_OK;
}

ss_error_t ss_data_set_custom(ss_data_t* data, void* value, size_t size, ss_cleanup_func_t cleanup) {
    (void)cleanup; /* Reserved for future cleanup functionality */
    if (!data || !value) return SS_ERR_NULL_PARAM;
    
    if (data->type == SS_TYPE_CUSTOM && data->custom_data) {
        free(data->custom_data);
    }
    
    data->type = SS_TYPE_CUSTOM;
    data->custom_data = malloc(size);
    if (!data->custom_data) return SS_ERR_MEMORY;
    
    memcpy(data->custom_data, value, size);
    data->size = size;
    return SS_OK;
}

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

void* ss_data_get_custom(const ss_data_t* data, size_t* size) {
    if (!data || data->type != SS_TYPE_CUSTOM) return NULL;
    if (size) *size = data->size;
    return data->custom_data;
}

size_t ss_get_signal_count(void) {
    if (!g_context) return 0;
    return g_context->signal_count;
}

ss_error_t ss_get_signal_list(ss_signal_info_t** list, size_t* count) {
    if (!g_context || !list || !count) return SS_ERR_NULL_PARAM;
    
    if (g_context->thread_safe) SS_MUTEX_LOCK(&g_context->mutex);
    
    *count = g_context->signal_count;
    if (*count == 0) {
        *list = NULL;
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
        return SS_OK;
    }
    
    *list = (ss_signal_info_t*)calloc(*count, sizeof(ss_signal_info_t));
    if (!*list) {
        if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
        return SS_ERR_MEMORY;
    }
    
    ss_signal_t* sig = g_context->signals;
    size_t i = 0;
    while (sig && i < *count) {
        (*list)[i].name = strdup(sig->name);
        (*list)[i].slot_count = sig->slot_count;
        sig = sig->next;
        i++;
    }
    
    if (g_context->thread_safe) SS_MUTEX_UNLOCK(&g_context->mutex);
    return SS_OK;
}

void ss_free_signal_list(ss_signal_info_t* list, size_t count) {
    if (!list) return;
    
    for (size_t i = 0; i < count; i++) {
        free(list[i].name);
    }
    free(list);
}

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
        default: return "Unknown error";
    }
}

void ss_set_max_slots_per_signal(size_t max_slots) {
    if (!g_context) return;
    g_context->max_slots_per_signal = max_slots;
}

size_t ss_get_max_slots_per_signal(void) {
    if (!g_context) return 0;
    return g_context->max_slots_per_signal;
}

void ss_set_thread_safe(int enabled) {
    if (!g_context) return;
    
    if (enabled && !g_context->thread_safe) {
        SS_MUTEX_INIT(&g_context->mutex);
    } else if (!enabled && g_context->thread_safe) {
        SS_MUTEX_DESTROY(&g_context->mutex);
    }
    
    g_context->thread_safe = enabled;
}

int ss_is_thread_safe(void) {
    if (!g_context) return 0;
    return g_context->thread_safe;
}
