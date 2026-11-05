/*
 * SS_Lib - Signal-Slot Library for C
 *
 * Written in the common subset of C89 and C11 (declarations at the top of
 * blocks, no // comments). C11-only facilities are selected with
 * __STDC_VERSION__ checks. src/ss_lib_c89.c is the C89 copy of this file
 * and must receive every change made here.
 *
 * Internal structure:
 *   - one context (g_context) guarded by one recursive mutex;
 *   - ctx_enter()/ctx_leave() wrap every public call: locking, nesting
 *     depth, deferred cleanup and error reporting live only there;
 *   - signal/slot storage has one interface with a static-array and a
 *     heap implementation (signal_create, signal_destroy, signal_next,
 *     slot_alloc, slot_release);
 *   - slots and signals are never freed while their signal is emitting:
 *     they are marked (slot->removed, sig->retired) and released when the
 *     outermost emission of that signal ends;
 *   - the deferred queue and batches share one entry queue that stores
 *     payload copies and pops each entry before dispatching it;
 *   - the ISR queue is a fixed array claimed with atomic compare-and-swap.
 */

/* SS_FEATURE_MACROS_BEGIN (hoisted to the top by create_single_header.sh) */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L  /* recursive pthread mutexes, clock_gettime */
#endif
/* SS_FEATURE_MACROS_END */

#include "ss_lib.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(_WIN32) && (SS_ENABLE_THREAD_SAFETY || SS_ENABLE_PERFORMANCE_STATS || \
                        (SS_ENABLE_ISR_SAFE && defined(_MSC_VER)))
#include <windows.h>
#endif

#if SS_ENABLE_PERFORMANCE_STATS && !defined(_WIN32)
#include <time.h>
#endif

#if SS_ENABLE_DEBUG_TRACE
#include <stdarg.h>
#endif

/* ------------------------------------------------------------------ */
/* Mutex: recursive, so slots may call back into the library           */
/* ------------------------------------------------------------------ */

#if SS_ENABLE_THREAD_SAFETY
#ifdef _WIN32
typedef CRITICAL_SECTION ss_mutex_t;   /* recursive by design */
static int ss_mutex_init(ss_mutex_t* m) {
    InitializeCriticalSection(m);
    return 0;
}
#define SS_MUTEX_LOCK(m) EnterCriticalSection(m)
#define SS_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define SS_MUTEX_DESTROY(m) DeleteCriticalSection(m)
#else
#include <pthread.h>
typedef pthread_mutex_t ss_mutex_t;
static int ss_mutex_init(ss_mutex_t* m) {
    pthread_mutexattr_t attr;
    int rc;
    if (pthread_mutexattr_init(&attr) != 0) return -1;
    rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (rc == 0) rc = pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);
    return rc;
}
#define SS_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define SS_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define SS_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#endif
#endif

/* ------------------------------------------------------------------ */
/* Internal structures                                                 */
/* ------------------------------------------------------------------ */

typedef struct ss_slot {
    ss_slot_func_t func;
    void* user_data;
    ss_priority_t priority;
    ss_connection_t handle;
    struct ss_slot* next;
    unsigned char removed; /* disconnected; freed by the next sweep */
} ss_slot_t;               /* static mode: func == NULL marks a free entry */

typedef struct ss_signal {
#if SS_USE_STATIC_MEMORY
    char name[SS_MAX_SIGNAL_NAME_LENGTH];   /* "" marks a free entry */
#if SS_ENABLE_INTROSPECTION
    char description[SS_MAX_DESCRIPTION_LENGTH];
#endif
#else
    char* name;
#if SS_ENABLE_INTROSPECTION
    char* description;
#endif
    struct ss_signal* next;
#endif
    ss_slot_t* slots;
    size_t slot_count;     /* live slots, excluding removed ones */
    ss_priority_t priority;
    int emitting;          /* depth of emissions of this signal in progress */
    unsigned char retired; /* unregistered; freed once it stops emitting */
    unsigned char dirty;   /* has removed slots waiting for a sweep */
#if SS_ENABLE_PERFORMANCE_STATS
    ss_perf_stats_t perf_stats;
#endif
} ss_signal_t;

/* A queued emission. Payload pointers are rebuilt by entry_view(), so an
 * entry can be copied freely. */
typedef struct {
    char signal_name[SS_MAX_SIGNAL_NAME_LENGTH];
    ss_data_t data;        /* type and scalar value; pointers cleared */
    unsigned long seq;     /* push order, bounds a flush */
    int has_payload;
#if SS_USE_STATIC_MEMORY
    union {   /* aligned for any standard type, like malloc memory */
        unsigned char bytes[SS_DEFERRED_PAYLOAD_SIZE];
        long double align_ld;
        void* align_p;
        long align_l;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
        max_align_t align_max;
#endif
#ifdef __SIZEOF_INT128__   /* GCC/Clang malloc memory suits __int128 too */
        __extension__ __int128 align_i128;
#endif
    } payload;
#else
    void* payload;         /* heap copy of the string or custom data */
#endif
} ss_entry_t;

typedef struct {
    ss_entry_t* entries;
    size_t capacity;
    size_t head;
    size_t count;
    unsigned long pushed;  /* entries ever pushed; next entry's seq */
} ss_queue_t;

typedef struct {
#if SS_USE_STATIC_MEMORY
    ss_signal_t signals[SS_MAX_SIGNALS];
    ss_slot_t slots[SS_MAX_SLOTS];
#else
    ss_signal_t* signals;
#endif
    size_t signal_count;   /* live signals, excluding retired ones */
    size_t max_slots_per_signal;
    ss_connection_t next_handle;
    char ns[SS_MAX_SIGNAL_NAME_LENGTH];
    void (*error_handler)(ss_error_t, const char*);
    int call_depth;        /* public calls in progress (nested via slots) */
    int closing;           /* ss_cleanup() requested from inside a call */
    int heap_owned;        /* context itself came from SS_CALLOC */
#if SS_ENABLE_THREAD_SAFETY
    int thread_safe;
    ss_mutex_t mutex;
#endif
#if SS_ENABLE_PERFORMANCE_STATS
    int profiling_enabled;
#endif
#if SS_ENABLE_MEMORY_STATS
    size_t bytes_in_use;
    size_t peak_bytes;
#endif
#if SS_ENABLE_DEBUG_TRACE
    FILE* trace_output;
#endif
    ss_entry_t deferred_entries[SS_DEFERRED_QUEUE_SIZE];
    ss_queue_t deferred;
} ss_context_t;

typedef struct {
    char c;
    ss_context_t ctx;
} ss_context_align_probe_t;
#define SS_CONTEXT_ALIGN offsetof(ss_context_align_probe_t, ctx)

static ss_context_t* g_context = NULL;
#if SS_USE_STATIC_MEMORY && SS_STATIC_CONTEXT
static ss_context_t g_static_context;   /* ss_init() storage in static mode */
#endif

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* Inlining for the few helpers on the emit path; -Os keeps them out of
 * line to protect code size on small targets. */
#if defined(__GNUC__) && !defined(__OPTIMIZE_SIZE__)
#define SS_INLINE static __inline__ __attribute__((always_inline))
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define SS_INLINE static inline
#elif defined(_MSC_VER)
#define SS_INLINE static __inline
#else
#define SS_INLINE static
#endif

static char* ss_strdup(const char* s) {
#ifdef SS_STRDUP
    return SS_STRDUP(s);
#else
    size_t n = strlen(s) + 1;
    char* copy = (char*)SS_MALLOC(n);
    if (copy) memcpy(copy, s, n);
    return copy;
#endif
}

#if SS_ENABLE_MEMORY_STATS && !SS_USE_STATIC_MEMORY
static void account(ss_context_t* ctx, size_t add, size_t sub) {
    ctx->bytes_in_use = ctx->bytes_in_use + add - sub;
    if (ctx->bytes_in_use > ctx->peak_bytes) ctx->peak_bytes = ctx->bytes_in_use;
}
#define SS_ACCOUNT(ctx, add, sub) account((ctx), (add), (sub))
#else
#define SS_ACCOUNT(ctx, add, sub) ((void)0)
#endif

/* SS_TRACE((fmt, args...)): double parentheses keep it valid C89. Replaces
 * the deprecated public SS_TRACE from ss_config.h inside the library. */
#undef SS_TRACE
#if SS_ENABLE_DEBUG_TRACE
static void ss_trace(const char* fmt, ...) {
    va_list args;
    FILE* out = g_context ? g_context->trace_output : NULL;
    if (!out) return;
    va_start(args, fmt);
    fputs("[SS_TRACE] ", out);
    vfprintf(out, fmt, args);
    fputc('\n', out);
    va_end(args);
}
#define SS_TRACE(args) ss_trace args
#else
#define SS_TRACE(args) ((void)0)
#endif

#if SS_ENABLE_PERFORMANCE_STATS
static uint64_t get_time_ns(void) {
    const uint64_t ns_per_s = (uint64_t)1000000000L;
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    uint64_t f, c;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    f = (uint64_t)freq.QuadPart;
    c = (uint64_t)count.QuadPart;
    /* Split to avoid overflowing c * 1e9 */
    return (c / f) * ns_per_s + (c % f) * ns_per_s / f;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * ns_per_s + (uint64_t)ts.tv_nsec;
#endif
}
#endif

/* An ss_data_t of the given type with everything else zeroed */
static ss_data_t data_of(ss_data_type_t type) {
    ss_data_t data;
    memset(&data, 0, sizeof(data));
    data.type = type;
    return data;
}

/* ------------------------------------------------------------------ */
/* Context entry and exit                                              */
/* ------------------------------------------------------------------ */

static void context_destroy(ss_context_t* ctx);

/* Lock and return the live context, or NULL if uninitialized/closing.
 * *locked records whether this call took the mutex. */
SS_INLINE ss_context_t* ctx_enter(int* locked) {
    ss_context_t* ctx = g_context;
    *locked = 0;
    if (!ctx) return NULL;
#if SS_ENABLE_THREAD_SAFETY
    if (ctx->thread_safe) {
        SS_MUTEX_LOCK(&ctx->mutex);
        *locked = 1;
    }
#endif
    if (ctx->closing) {
#if SS_ENABLE_THREAD_SAFETY
        if (*locked) SS_MUTEX_UNLOCK(&ctx->mutex);
#endif
        return NULL;
    }
    ctx->call_depth++;
    return ctx;
}

/* After unlocking: finish a deferred cleanup or report err */
static ss_error_t ctx_leave_slow(ss_context_t* ctx, int destroy, ss_error_t err,
                                 const char* detail,
                                 void (*handler)(ss_error_t, const char*)) {
    if (destroy) {
        context_destroy(ctx);
    } else if (handler) {
        handler(err, detail ? detail : ss_error_string(err));
    }
    return err;
}

/* Unlock, finish a deferred cleanup, report err to the error handler. */
SS_INLINE ss_error_t ctx_leave(ss_context_t* ctx, int locked, ss_error_t err,
                               const char* detail) {
    void (*handler)(ss_error_t, const char*) = ctx->error_handler;
    int destroy = (--ctx->call_depth == 0 && ctx->closing);
#if SS_ENABLE_THREAD_SAFETY
    if (locked) SS_MUTEX_UNLOCK(&ctx->mutex);
#else
    (void)locked;
#endif
    if (destroy || err != SS_OK) return ctx_leave_slow(ctx, destroy, err, detail, handler);
    return SS_OK;
}

/* ------------------------------------------------------------------ */
/* Name resolution                                                     */
/* ------------------------------------------------------------------ */

/* Join "ns::name" into buf (SS_MAX_SIGNAL_NAME_LENGTH bytes); 0 if too long */
static int qualify(const char* ns, const char* name, char* buf) {
    size_t ns_len = strlen(ns);
    size_t len = strlen(name);
    if (ns_len + 2 + len >= SS_MAX_SIGNAL_NAME_LENGTH) return 0;
    memcpy(buf, ns, ns_len);
    buf[ns_len] = ':';
    buf[ns_len + 1] = ':';
    memcpy(buf + ns_len + 2, name, len + 1);
    return 1;
}

/* Qualify name with the default namespace into buf when needed. Returns
 * NULL with *err set when the name is empty or does not fit. */
static const char* resolve_name(const ss_context_t* ctx, const char* name,
                                char* buf, ss_error_t* err) {
    if (!name || name[0] == '\0') {
        *err = SS_ERR_NULL_PARAM;
        return NULL;
    }
    if (!ctx || ctx->ns[0] == '\0' || strstr(name, "::")) {
        if (strlen(name) < SS_MAX_SIGNAL_NAME_LENGTH) return name;
    } else if (qualify(ctx->ns, name, buf)) {
        return buf;
    }
    *err = SS_ERR_WOULD_OVERFLOW;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Signal and slot storage                                             */
/* ------------------------------------------------------------------ */

#if SS_USE_STATIC_MEMORY

static ss_signal_t* signal_next(ss_context_t* ctx, ss_signal_t* sig) {
    size_t i = sig ? (size_t)(sig - ctx->signals) + 1 : 0;
    for (; i < SS_MAX_SIGNALS; i++) {
        if (ctx->signals[i].name[0]) return &ctx->signals[i];
    }
    return NULL;
}

static ss_signal_t* signal_create(ss_context_t* ctx, const char* name,
                                  const char* description, ss_error_t* err) {
    size_t i;
#if SS_ENABLE_INTROSPECTION
    if (description && strlen(description) >= SS_MAX_DESCRIPTION_LENGTH) {
        *err = SS_ERR_WOULD_OVERFLOW;
        return NULL;
    }
#else
    (void)description;                     /* only introspection reads it */
#endif
    for (i = 0; i < SS_MAX_SIGNALS; i++) {
        ss_signal_t* sig = &ctx->signals[i];
        if (!sig->name[0]) {
            memset(sig, 0, sizeof(*sig));
            strcpy(sig->name, name);           /* length checked by resolve_name */
#if SS_ENABLE_INTROSPECTION
            if (description) strcpy(sig->description, description);
#endif
            return sig;
        }
    }
    *err = SS_ERR_WOULD_OVERFLOW;
    return NULL;
}

#if SS_ENABLE_INTROSPECTION
static const char* signal_description(const ss_signal_t* sig) {
    return sig->description[0] ? sig->description : NULL;
}
#endif

static ss_slot_t* slot_alloc(ss_context_t* ctx) {
    size_t i;
    for (i = 0; i < SS_MAX_SLOTS; i++) {
        if (!ctx->slots[i].func) {             /* caller sets func at once */
            memset(&ctx->slots[i], 0, sizeof(ss_slot_t));
            return &ctx->slots[i];
        }
    }
    return NULL;
}

static void slot_release(ss_context_t* ctx, ss_slot_t* slot) {
    (void)ctx;
    memset(slot, 0, sizeof(*slot));
}

static void signal_release_storage(ss_context_t* ctx, ss_signal_t* sig) {
    (void)ctx;
    memset(sig, 0, sizeof(*sig));
}

#else /* dynamic memory */

static ss_signal_t* signal_next(ss_context_t* ctx, ss_signal_t* sig) {
    return sig ? sig->next : ctx->signals;
}

#if SS_ENABLE_MEMORY_STATS
/* Heap bytes held by a signal node and its strings */
static size_t signal_bytes(const ss_signal_t* sig) {
    size_t bytes = sizeof(ss_signal_t) + (sig->name ? strlen(sig->name) + 1 : 0);
#if SS_ENABLE_INTROSPECTION
    if (sig->description) bytes += strlen(sig->description) + 1;
#endif
    return bytes;
}
#endif

static void signal_release_storage(ss_context_t* ctx, ss_signal_t* sig);

static ss_signal_t* signal_create(ss_context_t* ctx, const char* name,
                                  const char* description, ss_error_t* err) {
    ss_signal_t* sig = (ss_signal_t*)SS_CALLOC(1, sizeof(ss_signal_t));
    int ok;
    if (!sig) {
        *err = SS_ERR_MEMORY;
        return NULL;
    }
    sig->name = ss_strdup(name);
    ok = sig->name != NULL;
#if SS_ENABLE_INTROSPECTION
    sig->description = description ? ss_strdup(description) : NULL;
    ok = ok && (!description || sig->description);
#else
    (void)description;                     /* only introspection reads it */
#endif
    if (!ok) {
        signal_release_storage(ctx, sig);
        *err = SS_ERR_MEMORY;
        return NULL;
    }
    sig->next = ctx->signals;
    ctx->signals = sig;
    SS_ACCOUNT(ctx, signal_bytes(sig), 0);
    return sig;
}

#if SS_ENABLE_INTROSPECTION
static const char* signal_description(const ss_signal_t* sig) {
    return sig->description;
}
#endif

static ss_slot_t* slot_alloc(ss_context_t* ctx) {
    ss_slot_t* slot = (ss_slot_t*)SS_CALLOC(1, sizeof(ss_slot_t));
    if (slot) SS_ACCOUNT(ctx, sizeof(ss_slot_t), 0);
    (void)ctx;
    return slot;
}

static void slot_release(ss_context_t* ctx, ss_slot_t* slot) {
    SS_ACCOUNT(ctx, 0, sizeof(ss_slot_t));
    (void)ctx;
    SS_FREE(slot);
}

static void signal_release_storage(ss_context_t* ctx, ss_signal_t* sig) {
    ss_signal_t** link = &ctx->signals;
    while (*link && *link != sig) link = &(*link)->next;
    if (*link) {                           /* linked: it was accounted */
        *link = sig->next;
        SS_ACCOUNT(ctx, 0, signal_bytes(sig));
    }
    SS_FREE(sig->name);
#if SS_ENABLE_INTROSPECTION
    SS_FREE(sig->description);
#endif
    SS_FREE(sig);
}

#endif

/* Next registered signal that has not been retired */
SS_INLINE ss_signal_t* live_next(ss_context_t* ctx, ss_signal_t* sig) {
    do {
        sig = signal_next(ctx, sig);
    } while (sig && sig->retired);
    return sig;
}

/* Live signal with this fully qualified name */
SS_INLINE ss_signal_t* signal_find(ss_context_t* ctx, const char* name) {
    ss_signal_t* sig;
    for (sig = live_next(ctx, NULL); sig; sig = live_next(ctx, sig)) {
        if (strcmp(sig->name, name) == 0) return sig;
    }
    return NULL;
}

/* Name to look up: like resolve_name(), but a name that cannot fit is
 * simply not found, and without a namespace nothing needs scanning (the
 * emit fast path). */
SS_INLINE const char* lookup_name(const ss_context_t* ctx, const char* name,
                              char* buf, ss_error_t* err) {
    const char* qualified;
    if (name && name[0] != '\0' && ctx->ns[0] == '\0') return name;
    qualified = resolve_name(ctx, name, buf, err);
    if (!qualified && *err == SS_ERR_WOULD_OVERFLOW) *err = SS_ERR_NOT_FOUND;
    return qualified;
}

static ss_signal_t* signal_lookup(ss_context_t* ctx, const char* name, ss_error_t* err) {
    char buf[SS_MAX_SIGNAL_NAME_LENGTH];
    ss_signal_t* sig;
    const char* qualified = lookup_name(ctx, name, buf, err);
    if (!qualified) return NULL;
    sig = signal_find(ctx, qualified);
    if (!sig) *err = SS_ERR_NOT_FOUND;
    return sig;
}

static void signal_destroy(ss_context_t* ctx, ss_signal_t* sig) {
    ss_slot_t* slot = sig->slots;
    while (slot) {
        ss_slot_t* next = slot->next;
        slot_release(ctx, slot);
        slot = next;
    }
    signal_release_storage(ctx, sig);
}

/* Free the slots marked removed. Never called while the signal emits. */
static void sweep_removed_slots(ss_context_t* ctx, ss_signal_t* sig) {
    ss_slot_t** link = &sig->slots;
    while (*link) {
        ss_slot_t* slot = *link;
        if (slot->removed) {
            *link = slot->next;
            slot_release(ctx, slot);
        } else {
            link = &slot->next;
        }
    }
    sig->dirty = 0;
}

/* Disconnect a live slot. While the signal emits it is only marked, and
 * the sweep at the end of the emission frees it. */
static void slot_remove(ss_context_t* ctx, ss_signal_t* sig, ss_slot_t* slot) {
    slot->removed = 1;
    sig->slot_count--;
    sig->dirty = 1;
    if (!sig->emitting) sweep_removed_slots(ctx, sig);
}

static void slots_remove_all(ss_context_t* ctx, ss_signal_t* sig) {
    ss_slot_t* slot;
    for (slot = sig->slots; slot; slot = slot->next) slot->removed = 1;
    sig->slot_count = 0;
    sig->dirty = 1;
    if (!sig->emitting) sweep_removed_slots(ctx, sig);
}

/* First live slot calling func (when given), else with this handle */
static ss_slot_t* slot_find(ss_signal_t* sig, ss_slot_func_t func, ss_connection_t handle) {
    ss_slot_t* slot;
    for (slot = sig->slots; slot; slot = slot->next) {
        if (!slot->removed && (func ? slot->func == func : slot->handle == handle)) {
            return slot;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Emission                                                            */
/* ------------------------------------------------------------------ */

/* Emit a fully qualified name. Caller holds the context. */
SS_INLINE ss_error_t emit_locked(ss_context_t* ctx, const char* name, const ss_data_t* data) {
    ss_signal_t* sig = signal_find(ctx, name);
    ss_slot_t* slot;
#if SS_ENABLE_PERFORMANCE_STATS
    uint64_t start_time = ctx->profiling_enabled ? get_time_ns() : 0;
#endif

    if (!sig) return SS_ERR_NOT_FOUND;
    SS_TRACE(("Emitting signal: %s to %lu slots", name, (unsigned long)sig->slot_count));

    sig->emitting++;
    for (slot = sig->slots; slot; ) {
        /* Nodes are only marked while emitting, so next stays valid.
         * Unregister and cleanup mark every slot, which ends the loop. */
        ss_slot_t* next = slot->next;
        if (!slot->removed) slot->func(data, slot->user_data);
        slot = next;
    }
    sig->emitting--;

#if SS_ENABLE_PERFORMANCE_STATS
    if (ctx->profiling_enabled) {
        uint64_t elapsed = get_time_ns() - start_time;
        ss_perf_stats_t* ps = &sig->perf_stats;
        ps->total_emissions++;
        ps->total_time_ns += elapsed;
        ps->avg_time_ns = ps->total_time_ns / ps->total_emissions;
        if (elapsed > ps->max_time_ns) ps->max_time_ns = elapsed;
        if (ps->min_time_ns == 0 || elapsed < ps->min_time_ns) ps->min_time_ns = elapsed;
    }
#endif

    if (sig->emitting == 0) {
        if (sig->retired) {
            signal_destroy(ctx, sig);
        } else if (sig->dirty) {
            sweep_removed_slots(ctx, sig);
        }
    }
    return SS_OK;
}

/* ------------------------------------------------------------------ */
/* Queued emissions (deferred queue and batches)                       */
/* ------------------------------------------------------------------ */

static void queue_init(ss_queue_t* q, ss_entry_t* entries, size_t capacity) {
    q->entries = entries;
    q->capacity = capacity;
    q->head = 0;
    q->count = 0;
}

static void entry_release(ss_entry_t* e) {
#if !SS_USE_STATIC_MEMORY
    if (e->has_payload) SS_FREE(e->payload);
    e->payload = NULL;
#endif
    e->has_payload = 0;
}

/* Copy name and data into e; strings and custom data are copied. */
static ss_error_t entry_fill(ss_entry_t* e, const char* name, const ss_data_t* data) {
    const void* src = NULL;
    size_t len = 0;

    strcpy(e->signal_name, name);              /* length checked by resolve_name */
    e->has_payload = 0;
#if !SS_USE_STATIC_MEMORY
    e->payload = NULL;     /* a reused ring slot may hold a moved-out pointer */
#endif
    if (!data) {
        memset(&e->data, 0, sizeof(e->data));
        e->data.type = SS_TYPE_VOID;
        return SS_OK;
    }
    e->data = *data;
    if (data->type == SS_TYPE_STRING && data->value.s_val) {
        src = data->value.s_val;
        len = strlen(data->value.s_val) + 1;
        e->data.value.s_val = NULL;
    }
#if SS_ENABLE_CUSTOM_DATA
    if (data->type == SS_TYPE_CUSTOM && data->custom_data) {
        src = data->custom_data;
        len = data->size;
    }
    e->data.custom_data = NULL;
    e->data.custom_cleanup = NULL;   /* belongs to the caller's object */
#endif
    if (!src) return SS_OK;

#if SS_USE_STATIC_MEMORY
    if (len > SS_DEFERRED_PAYLOAD_SIZE) return SS_ERR_WOULD_OVERFLOW;
    memcpy(e->payload.bytes, src, len);
#else
    e->payload = SS_MALLOC(len ? len : 1);
    if (!e->payload) return SS_ERR_MEMORY;
    memcpy(e->payload, src, len);
#endif
    e->has_payload = 1;
    return SS_OK;
}

/* The ss_data_t a slot sees for e, pointing into e's payload copy */
static void entry_view(ss_entry_t* e, ss_data_t* out) {
    void* payload;
    *out = e->data;
    if (!e->has_payload) return;
#if SS_USE_STATIC_MEMORY
    payload = e->payload.bytes;
#else
    payload = e->payload;
#endif
    if (e->data.type == SS_TYPE_STRING) out->value.s_val = (const char*)payload;
#if SS_ENABLE_CUSTOM_DATA
    if (e->data.type == SS_TYPE_CUSTOM) out->custom_data = payload;
#endif
}

static ss_error_t queue_push(ss_queue_t* q, const char* name, const ss_data_t* data) {
    size_t tail = q->head + q->count;
    ss_error_t err;
    if (q->count == q->capacity) return SS_ERR_WOULD_OVERFLOW;
    if (tail >= q->capacity) tail -= q->capacity;
    err = entry_fill(&q->entries[tail], name, data);
    if (err == SS_OK) {
        q->entries[tail].seq = q->pushed++;
        q->count++;
    }
    return err;
}

/* Move the oldest entry out of the queue, so slots can push meanwhile */
static int queue_pop(ss_queue_t* q, ss_entry_t* out) {
    if (q->count == 0) return 0;
    *out = q->entries[q->head];
    if (++q->head == q->capacity) q->head = 0;
    q->count--;
    return 1;
}

static void queue_clear(ss_queue_t* q) {
    ss_entry_t e;
    while (queue_pop(q, &e)) entry_release(&e);
}

/* Emit the entries queued when the flush starts, oldest first; entries
 * queued meanwhile (by slots, or around a nested flush) wait for the next
 * flush. Each entry is popped before it is
 * emitted, so slots may push to (or flush) the same queue. Stops early
 * when *stop or ctx->closing is set. Caller holds the context. */
static ss_error_t queue_flush(ss_context_t* ctx, ss_queue_t* q, const int* stop) {
    ss_error_t result = SS_OK;
    ss_entry_t entry;
    unsigned long limit = q->pushed;   /* entries pushed from now on wait */
    /* Queued entries have seq in [limit - count, limit): wrap-safe test */
    while (q->count > 0 && limit - q->entries[q->head].seq - 1UL < (unsigned long)q->capacity &&
           !*stop && !ctx->closing && queue_pop(q, &entry)) {
        ss_data_t view;
        ss_error_t err;
        entry_view(&entry, &view);
        err = emit_locked(ctx, entry.signal_name, &view);
        entry_release(&entry);
        if (err != SS_OK) result = err;
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* ISR queue                                                           */
/* ------------------------------------------------------------------ */

#if SS_ENABLE_ISR_SAFE

#if defined(SS_ISR_CRITICAL_ENTER) && defined(SS_ISR_CRITICAL_EXIT)
typedef volatile int ss_atomic_int;
static int ss_atomic_load(ss_atomic_int* p) {
    int v;
    SS_ISR_CRITICAL_ENTER();
    v = *p;
    SS_ISR_CRITICAL_EXIT();
    return v;
}
static void ss_atomic_store(ss_atomic_int* p, int v) {
    SS_ISR_CRITICAL_ENTER();
    *p = v;
    SS_ISR_CRITICAL_EXIT();
}
static int ss_atomic_cas(ss_atomic_int* p, int expected, int desired) {
    int ok;
    SS_ISR_CRITICAL_ENTER();
    ok = (*p == expected);
    if (ok) *p = desired;
    SS_ISR_CRITICAL_EXIT();
    return ok;
}
static int ss_atomic_fetch_add(ss_atomic_int* p, int v) {
    int old;
    SS_ISR_CRITICAL_ENTER();
    old = *p;
    *p = (int)((unsigned int)old + (unsigned int)v);
    SS_ISR_CRITICAL_EXIT();
    return old;
}
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && \
      !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
typedef atomic_int ss_atomic_int;
static int ss_atomic_load(ss_atomic_int* p) {
    return atomic_load_explicit(p, memory_order_acquire);
}
static void ss_atomic_store(ss_atomic_int* p, int v) {
    atomic_store_explicit(p, v, memory_order_release);
}
static int ss_atomic_cas(ss_atomic_int* p, int expected, int desired) {
    return atomic_compare_exchange_strong_explicit(p, &expected, desired,
                                                   memory_order_acq_rel,
                                                   memory_order_acquire);
}
static int ss_atomic_fetch_add(ss_atomic_int* p, int v) {
    return atomic_fetch_add_explicit(p, v, memory_order_relaxed);
}
#elif defined(__GNUC__)
typedef int ss_atomic_int;
static int ss_atomic_load(ss_atomic_int* p) {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
static void ss_atomic_store(ss_atomic_int* p, int v) {
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}
static int ss_atomic_cas(ss_atomic_int* p, int expected, int desired) {
    return __atomic_compare_exchange_n(p, &expected, desired, 0,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
static int ss_atomic_fetch_add(ss_atomic_int* p, int v) {
    return __atomic_fetch_add(p, v, __ATOMIC_RELAXED);
}
#elif defined(_MSC_VER)
typedef volatile long ss_atomic_int;
static int ss_atomic_load(ss_atomic_int* p) {
    return (int)InterlockedCompareExchange(p, 0, 0);
}
static void ss_atomic_store(ss_atomic_int* p, int v) {
    InterlockedExchange(p, (long)v);
}
static int ss_atomic_cas(ss_atomic_int* p, int expected, int desired) {
    return InterlockedCompareExchange(p, (long)desired, (long)expected) == (long)expected;
}
static int ss_atomic_fetch_add(ss_atomic_int* p, int v) {
    return (int)InterlockedExchangeAdd(p, (long)v);
}
#else
#error "SS_ENABLE_ISR_SAFE needs C11 atomics, GCC/Clang, MSVC, or SS_ISR_CRITICAL_ENTER/EXIT"
#endif

enum { ISR_FREE = 0, ISR_WRITING = 1, ISR_READY = 2, ISR_READING = 3 };

typedef struct {
    ss_atomic_int state;   /* FREE -> WRITING (ISR) -> READY -> READING -> FREE */
    ss_atomic_int seq;     /* queue order, compared with wrap-around */
    int value;
    char signal_name[SS_MAX_SIGNAL_NAME_LENGTH];
} ss_isr_entry_t;

static ss_isr_entry_t g_isr_queue[SS_ISR_QUEUE_SIZE];
static ss_atomic_int g_isr_seq;

ss_error_t ss_emit_from_isr(const char* signal_name, int value) {
    size_t len, i;
    if (!signal_name || signal_name[0] == '\0') return SS_ERR_NULL_PARAM;
    for (len = 0; len < SS_MAX_SIGNAL_NAME_LENGTH && signal_name[len]; len++) {
    }
    if (len == SS_MAX_SIGNAL_NAME_LENGTH) return SS_ERR_WOULD_OVERFLOW;

    for (i = 0; i < SS_ISR_QUEUE_SIZE; i++) {
        ss_isr_entry_t* e = &g_isr_queue[i];
        if (ss_atomic_cas(&e->state, ISR_FREE, ISR_WRITING)) {
            memcpy(e->signal_name, signal_name, len + 1);
            e->value = value;
            ss_atomic_store(&e->seq, ss_atomic_fetch_add(&g_isr_seq, 1));
            ss_atomic_store(&e->state, ISR_READY);   /* publishes the fields */
            return SS_OK;
        }
    }
    return SS_ERR_WOULD_OVERFLOW;
}

/* Discard queued events (used by cleanup) */
static void isr_queue_drop_ready(void) {
    size_t i;
    for (i = 0; i < SS_ISR_QUEUE_SIZE; i++) {
        if (ss_atomic_cas(&g_isr_queue[i].state, ISR_READY, ISR_READING)) {
            ss_atomic_store(&g_isr_queue[i].state, ISR_FREE);
        }
    }
}

ss_error_t ss_process_isr_queue(void) {
    char name[SS_MAX_SIGNAL_NAME_LENGTH];
    char buf[SS_MAX_SIGNAL_NAME_LENGTH];
    size_t order[SS_ISR_QUEUE_SIZE];
    unsigned int seqs[SS_ISR_QUEUE_SIZE];
    size_t n = 0, i, j;
    ss_error_t result = SS_OK;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;

    /* Snapshot the ready entries in queue order (insertion sort, N small).
     * Events queued after the snapshot wait for the next call. */
    for (i = 0; i < SS_ISR_QUEUE_SIZE; i++) {
        unsigned int seq;
        if (ss_atomic_load(&g_isr_queue[i].state) != ISR_READY) continue;
        seq = (unsigned int)ss_atomic_load(&g_isr_queue[i].seq);
        for (j = n; j > 0 && (int)(seq - seqs[j - 1]) < 0; j--) {
            order[j] = order[j - 1];
            seqs[j] = seqs[j - 1];
        }
        order[j] = i;
        seqs[j] = seq;
        n++;
    }

    for (i = 0; i < n && !ctx->closing; i++) {
        ss_isr_entry_t* e = &g_isr_queue[order[i]];
        ss_data_t data = data_of(SS_TYPE_INT);
        ss_error_t err = SS_OK;
        const char* qualified;
        /* Another consumer may have taken it since the snapshot */
        if (!ss_atomic_cas(&e->state, ISR_READY, ISR_READING)) continue;
        strcpy(name, e->signal_name);
        data.value.i_val = e->value;
        ss_atomic_store(&e->state, ISR_FREE);

        qualified = lookup_name(ctx, name, buf, &err);
        if (qualified) err = emit_locked(ctx, qualified, &data);
        if (err != SS_OK) result = err;
    }
    return ctx_leave(ctx, locked, result, NULL);
}

#endif /* SS_ENABLE_ISR_SAFE */

/* ------------------------------------------------------------------ */
/* Initialization and cleanup                                          */
/* ------------------------------------------------------------------ */

static ss_error_t context_init(ss_context_t* ctx, int heap_owned) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->max_slots_per_signal = SS_DEFAULT_MAX_SLOTS_PER_SIGNAL;
    ctx->next_handle = 1;
    ctx->heap_owned = heap_owned;
    queue_init(&ctx->deferred, ctx->deferred_entries, SS_DEFERRED_QUEUE_SIZE);
#if SS_ENABLE_THREAD_SAFETY
    if (ss_mutex_init(&ctx->mutex) != 0) return SS_ERR_MEMORY;
#endif
#if SS_ENABLE_MEMORY_STATS
    ctx->bytes_in_use = heap_owned ? sizeof(ss_context_t) : 0;
    ctx->peak_bytes = ctx->bytes_in_use;
#endif
    g_context = ctx;
    return SS_OK;
}

/* Common checks; returns 1 when initialization should proceed */
static int init_precheck(size_t abi_tag, ss_error_t* err) {
    if (abi_tag != SS_ABI_TAG) {
        *err = SS_ERR_CONFIG_MISMATCH;
        return 0;
    }
    if (g_context) {
        *err = g_context->closing ? SS_ERR_ALREADY_EXISTS : SS_OK;
        return 0;
    }
    return 1;
}

ss_error_t ss_init_abi(size_t abi_tag) {
    ss_error_t err = SS_OK;
    if (!init_precheck(abi_tag, &err)) return err;
#if SS_USE_STATIC_MEMORY && SS_STATIC_CONTEXT
    return context_init(&g_static_context, 0);
#else
    {
        ss_context_t* ctx = (ss_context_t*)SS_CALLOC(1, sizeof(ss_context_t));
        if (!ctx) return SS_ERR_MEMORY;
        err = context_init(ctx, 1);
        if (err != SS_OK) SS_FREE(ctx);
        return err;
    }
#endif
}

ss_error_t ss_init(void) {
    return ss_init_abi(SS_ABI_TAG);
}

size_t ss_get_required_pool_size(void) {
    return sizeof(ss_context_t) + SS_CONTEXT_ALIGN - 1;
}

ss_error_t ss_init_static_abi(void* memory_pool, size_t pool_size, size_t abi_tag) {
    ss_error_t err = SS_OK;
    size_t pad = 0;
    if (memory_pool) {
        pad = (SS_CONTEXT_ALIGN - (size_t)((uintptr_t)memory_pool % SS_CONTEXT_ALIGN)) %
              SS_CONTEXT_ALIGN;
    }
    /* Like 2.1: without a pool large enough, behave as ss_init() */
    if (!memory_pool || pool_size < pad || pool_size - pad < sizeof(ss_context_t)) {
        return ss_init_abi(abi_tag);
    }
    if (!init_precheck(abi_tag, &err)) return err;
    return context_init((ss_context_t*)((unsigned char*)memory_pool + pad), 0);
}

ss_error_t ss_init_static(void* memory_pool, size_t pool_size) {
    return ss_init_static_abi(memory_pool, pool_size, SS_ABI_TAG);
}

static void context_destroy(ss_context_t* ctx) {
    ss_signal_t* sig;
    g_context = NULL;
    queue_clear(&ctx->deferred);
    while ((sig = signal_next(ctx, NULL)) != NULL) {
        signal_destroy(ctx, sig);
    }
#if SS_ENABLE_ISR_SAFE
    isr_queue_drop_ready();
#endif
#if SS_ENABLE_THREAD_SAFETY
    SS_MUTEX_DESTROY(&ctx->mutex);
#endif
    if (ctx->heap_owned) SS_FREE(ctx);
}

void ss_cleanup(void) {
    ss_signal_t* sig;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return;
    /* Called from a slot, this ends every emission in progress; the
     * context is destroyed when the outermost call returns. */
    ctx->closing = 1;
    for (sig = signal_next(ctx, NULL); sig; sig = signal_next(ctx, sig)) {
        slots_remove_all(ctx, sig);
    }
    (void)ctx_leave(ctx, locked, SS_OK, NULL);
}

const char* ss_version(void) {
    return SS_VERSION_STRING;
}

/* ------------------------------------------------------------------ */
/* Signals                                                             */
/* ------------------------------------------------------------------ */

ss_error_t ss_signal_register(const char* signal_name) {
    return ss_signal_register_ex(signal_name, NULL, SS_PRIORITY_NORMAL);
}

ss_error_t ss_signal_register_ex(const char* signal_name,
                                const char* description,
                                ss_priority_t priority) {
    char buf[SS_MAX_SIGNAL_NAME_LENGTH];
    ss_error_t err = SS_OK;
    ss_signal_t* sig;
    const char* name;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;

    name = resolve_name(ctx, signal_name, buf, &err);
    if (name && signal_find(ctx, name)) err = SS_ERR_ALREADY_EXISTS;
    if (name && err == SS_OK) {
        sig = signal_create(ctx, name, description, &err);
        if (sig) {
            sig->priority = priority;
            ctx->signal_count++;
            SS_TRACE(("Registered signal: %s", name));
        }
    }
    return ctx_leave(ctx, locked, err, signal_name);
}

ss_error_t ss_signal_unregister(const char* signal_name) {
    ss_error_t err = SS_OK;
    ss_signal_t* sig;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;

    sig = signal_lookup(ctx, signal_name, &err);
    if (sig) {
        slots_remove_all(ctx, sig);        /* also ends an emission in progress */
        ctx->signal_count--;
        if (sig->emitting) {
            sig->retired = 1;              /* freed when its emission ends */
        } else {
            signal_destroy(ctx, sig);
        }
    }
    return ctx_leave(ctx, locked, err, signal_name);
}

int ss_signal_exists(const char* signal_name) {
    ss_error_t err = SS_OK;
    int exists;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return 0;
    exists = signal_lookup(ctx, signal_name, &err) != NULL;
    (void)ctx_leave(ctx, locked, SS_OK, NULL);
    return exists;
}

/* ------------------------------------------------------------------ */
/* Connections                                                         */
/* ------------------------------------------------------------------ */

ss_error_t ss_connect(const char* signal_name, ss_slot_func_t slot, void* user_data) {
    return ss_connect_ex(signal_name, slot, user_data, SS_PRIORITY_NORMAL, NULL);
}

ss_error_t ss_connect_ex(const char* signal_name, ss_slot_func_t slot,
                        void* user_data, ss_priority_t priority,
                        ss_connection_t* handle) {
    ss_error_t err = SS_OK;
    ss_signal_t* sig = NULL;
    ss_slot_t* node;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;

    if (!slot) {
        err = SS_ERR_NULL_PARAM;
    } else {
        sig = signal_lookup(ctx, signal_name, &err);
    }
    if (sig && sig->slot_count >= ctx->max_slots_per_signal) err = SS_ERR_MAX_SLOTS;
    if (sig && err == SS_OK) {
        node = slot_alloc(ctx);
        if (!node) {
            err = SS_USE_STATIC_MEMORY ? SS_ERR_WOULD_OVERFLOW : SS_ERR_MEMORY;
        } else {
            node->func = slot;
            node->user_data = user_data;
            node->priority = priority;
            node->handle = ctx->next_handle++;
            if (ctx->next_handle == 0) ctx->next_handle = 1;
            if (handle) *handle = node->handle;

            /* Higher priority first; equal priorities keep connection order */
            if (!sig->slots || priority > sig->slots->priority) {
                node->next = sig->slots;
                sig->slots = node;
            } else {
                ss_slot_t* s = sig->slots;
                while (s->next && s->next->priority >= priority) s = s->next;
                node->next = s->next;
                s->next = node;
            }
            sig->slot_count++;
            SS_TRACE(("Connected slot to signal: %s (handle: %lu)", sig->name,
                     (unsigned long)node->handle));
        }
    }
    return ctx_leave(ctx, locked, err, signal_name);
}

ss_error_t ss_disconnect(const char* signal_name, ss_slot_func_t slot) {
    ss_error_t err = SS_ERR_NULL_PARAM;
    ss_signal_t* sig = NULL;
    ss_slot_t* node;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;

    if (slot) sig = signal_lookup(ctx, signal_name, &err);
    if (sig) {
        node = slot_find(sig, slot, 0);
        err = node ? SS_OK : SS_ERR_NOT_FOUND;
        if (node) slot_remove(ctx, sig, node);
    }
    return ctx_leave(ctx, locked, err, signal_name);
}

ss_error_t ss_disconnect_handle(ss_connection_t handle) {
    ss_signal_t* sig;
    ss_slot_t* node = NULL;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;
    if (handle == 0) return ctx_leave(ctx, locked, SS_ERR_NULL_PARAM, NULL);

    for (sig = live_next(ctx, NULL); sig; sig = live_next(ctx, sig)) {
        node = slot_find(sig, NULL, handle);
        if (node) {
            slot_remove(ctx, sig, node);
            break;
        }
    }
    return ctx_leave(ctx, locked, node ? SS_OK : SS_ERR_NOT_FOUND, NULL);
}

ss_error_t ss_disconnect_all(const char* signal_name) {
    ss_error_t err = SS_OK;
    ss_signal_t* sig;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;

    sig = signal_lookup(ctx, signal_name, &err);
    if (sig) slots_remove_all(ctx, sig);
    return ctx_leave(ctx, locked, err, signal_name);
}

/* ------------------------------------------------------------------ */
/* Emission API                                                        */
/* ------------------------------------------------------------------ */

ss_error_t ss_emit(const char* signal_name, const ss_data_t* data) {
    char buf[SS_MAX_SIGNAL_NAME_LENGTH];
    ss_error_t err = SS_OK;
    const char* name;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;

    name = lookup_name(ctx, signal_name, buf, &err);
    if (name) err = emit_locked(ctx, name, data);
    return ctx_leave(ctx, locked, err, signal_name);
}

ss_error_t ss_emit_void(const char* signal_name) {
    ss_data_t data = data_of(SS_TYPE_VOID);
    return ss_emit(signal_name, &data);
}

ss_error_t ss_emit_int(const char* signal_name, int value) {
    ss_data_t data = data_of(SS_TYPE_INT);
    data.value.i_val = value;
    return ss_emit(signal_name, &data);
}

ss_error_t ss_emit_float(const char* signal_name, float value) {
    ss_data_t data = data_of(SS_TYPE_FLOAT);
    data.value.f_val = value;
    return ss_emit(signal_name, &data);
}

ss_error_t ss_emit_double(const char* signal_name, double value) {
    ss_data_t data = data_of(SS_TYPE_DOUBLE);
    data.value.d_val = value;
    return ss_emit(signal_name, &data);
}

ss_error_t ss_emit_string(const char* signal_name, const char* value) {
    ss_data_t data = data_of(SS_TYPE_STRING);
    data.value.s_val = value;
    return ss_emit(signal_name, &data);
}

ss_error_t ss_emit_pointer(const char* signal_name, void* value) {
    ss_data_t data = data_of(SS_TYPE_POINTER);
    data.value.p_val = value;
    return ss_emit(signal_name, &data);
}

ss_error_t ss_emit_deferred(const char* signal_name, const ss_data_t* data) {
    char buf[SS_MAX_SIGNAL_NAME_LENGTH];
    ss_error_t err = SS_OK;
    const char* name;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;

    name = resolve_name(ctx, signal_name, buf, &err);
    if (name) err = queue_push(&ctx->deferred, name, data);
    return ctx_leave(ctx, locked, err, signal_name);
}

ss_error_t ss_flush_deferred(void) {
    static const int never = 0;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;
    return ctx_leave(ctx, locked, queue_flush(ctx, &ctx->deferred, &never), NULL);
}

ss_error_t ss_emit_namespaced(const char* ns, const char* signal_name,
                              const ss_data_t* data) {
    char buf[SS_MAX_SIGNAL_NAME_LENGTH];
    ss_error_t err = SS_ERR_NULL_PARAM;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;

    if (ns && signal_name) {
        err = qualify(ns, signal_name, buf) ? emit_locked(ctx, buf, data)
                                            : SS_ERR_WOULD_OVERFLOW;
    }
    return ctx_leave(ctx, locked, err, signal_name);
}

/* ------------------------------------------------------------------ */
/* Namespace                                                           */
/* ------------------------------------------------------------------ */

ss_error_t ss_set_namespace(const char* ns) {
    char probe[SS_MAX_SIGNAL_NAME_LENGTH];
    ss_error_t err = SS_OK;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;

    if (!ns || ns[0] == '\0') {
        ctx->ns[0] = '\0';
    } else if (!qualify(ns, "x", probe)) {
        err = SS_ERR_WOULD_OVERFLOW;       /* not even "ns::x" would fit */
    } else if (ns != ctx->ns) {            /* ss_get_namespace() result: no-op */
        strcpy(ctx->ns, ns);
    }
    return ctx_leave(ctx, locked, err, ns);
}

const char* ss_get_namespace(void) {
    const char* ns;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return NULL;
    ns = ctx->ns[0] ? ctx->ns : NULL;
    (void)ctx_leave(ctx, locked, SS_OK, NULL);
    return ns;
}

/* ------------------------------------------------------------------ */
/* Batches                                                             */
/* ------------------------------------------------------------------ */

struct ss_batch {
    ss_entry_t entries[SS_BATCH_MAX_ENTRIES];
    ss_queue_t queue;
    int emitting;
    int destroy_pending;
};

ss_batch_t* ss_batch_create(void) {
    ss_batch_t* batch = (ss_batch_t*)SS_CALLOC(1, sizeof(ss_batch_t));
    if (batch) queue_init(&batch->queue, batch->entries, SS_BATCH_MAX_ENTRIES);
    return batch;
}

static void batch_free(ss_batch_t* batch) {
    queue_clear(&batch->queue);
    SS_FREE(batch);
}

void ss_batch_destroy(ss_batch_t* batch) {
    if (!batch) return;
    if (batch->emitting) {
        batch->destroy_pending = 1;        /* freed when ss_batch_emit returns */
        return;
    }
    batch_free(batch);
}

ss_error_t ss_batch_add(ss_batch_t* batch, const char* signal_name,
                        const ss_data_t* data) {
    char buf[SS_MAX_SIGNAL_NAME_LENGTH];
    ss_error_t err = SS_ERR_NULL_PARAM;
    const char* name;
    int locked;
    /* Works before ss_init() too; the context only supplies the namespace
     * and the error handler. */
    ss_context_t* ctx = ctx_enter(&locked);

    if (batch && !batch->destroy_pending) {
        name = resolve_name(ctx, signal_name, buf, &err);
        if (name) err = queue_push(&batch->queue, name, data);
    }
    return ctx ? ctx_leave(ctx, locked, err, signal_name) : err;
}

ss_error_t ss_batch_emit(ss_batch_t* batch) {
    ss_error_t err = SS_ERR_NULL_PARAM;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;

    if (batch && !batch->destroy_pending) {
        batch->emitting++;
        err = queue_flush(ctx, &batch->queue, &batch->destroy_pending);
        batch->emitting--;
        if (batch->emitting == 0 && batch->destroy_pending) batch_free(batch);
    }
    return ctx_leave(ctx, locked, err, NULL);
}

/* ------------------------------------------------------------------ */
/* Data handling                                                       */
/* ------------------------------------------------------------------ */

ss_data_t* ss_data_create(ss_data_type_t type) {
    ss_data_t* data = (ss_data_t*)SS_CALLOC(1, sizeof(ss_data_t));
    if (data) data->type = type;
    return data;
}

/* Free the string or custom payload an ss_data_t owns (see ss_lib.h) */
static void data_release(ss_data_t* data) {
    if (data->type == SS_TYPE_STRING && data->value.s_val) {
        SS_FREE((void*)data->value.s_val);
        data->value.s_val = NULL;
    }
#if SS_ENABLE_CUSTOM_DATA
    if (data->type == SS_TYPE_CUSTOM && data->custom_data) {
        if (data->custom_cleanup) data->custom_cleanup(data->custom_data);
        SS_FREE(data->custom_data);
        data->custom_data = NULL;
        data->custom_cleanup = NULL;
        data->size = 0;
    }
#endif
}

void ss_data_destroy(ss_data_t* data) {
    if (!data) return;
    data_release(data);
    SS_FREE(data);
}

ss_error_t ss_data_set_int(ss_data_t* data, int value) {
    if (!data) return SS_ERR_NULL_PARAM;
    data_release(data);
    data->type = SS_TYPE_INT;
    data->value.i_val = value;
    return SS_OK;
}

ss_error_t ss_data_set_float(ss_data_t* data, float value) {
    if (!data) return SS_ERR_NULL_PARAM;
    data_release(data);
    data->type = SS_TYPE_FLOAT;
    data->value.f_val = value;
    return SS_OK;
}

ss_error_t ss_data_set_double(ss_data_t* data, double value) {
    if (!data) return SS_ERR_NULL_PARAM;
    data_release(data);
    data->type = SS_TYPE_DOUBLE;
    data->value.d_val = value;
    return SS_OK;
}

ss_error_t ss_data_set_string(ss_data_t* data, const char* value) {
    char* copy = NULL;
    if (!data) return SS_ERR_NULL_PARAM;
    if (value) {
        copy = ss_strdup(value);
        if (!copy) return SS_ERR_MEMORY;   /* previous value left intact */
    }
    data_release(data);
    data->type = SS_TYPE_STRING;
    data->value.s_val = copy;
    return SS_OK;
}

ss_error_t ss_data_set_pointer(ss_data_t* data, void* value) {
    if (!data) return SS_ERR_NULL_PARAM;
    data_release(data);
    data->type = SS_TYPE_POINTER;
    data->value.p_val = value;
    return SS_OK;
}

#if SS_ENABLE_CUSTOM_DATA
ss_error_t ss_data_set_custom(ss_data_t* data, void* value, size_t size,
                             ss_cleanup_func_t cleanup) {
    void* copy;
    if (!data || !value || size == 0) return SS_ERR_NULL_PARAM;
    copy = SS_MALLOC(size);
    if (!copy) return SS_ERR_MEMORY;       /* previous value left intact */
    memcpy(copy, value, size);
    data_release(data);
    data->type = SS_TYPE_CUSTOM;
    data->custom_data = copy;
    data->custom_cleanup = cleanup;
    data->size = size;
    return SS_OK;
}
#endif

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

/* ------------------------------------------------------------------ */
/* Introspection                                                       */
/* ------------------------------------------------------------------ */

#if SS_ENABLE_INTROSPECTION
size_t ss_get_signal_count(void) {
    size_t count;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return 0;
    count = ctx->signal_count;
    (void)ctx_leave(ctx, locked, SS_OK, NULL);
    return count;
}

void ss_free_signal_list(ss_signal_info_t* list, size_t count) {
    size_t i;
    if (!list) return;
    for (i = 0; i < count; i++) {
        SS_FREE(list[i].name);
        SS_FREE((void*)list[i].description);
    }
    SS_FREE(list);
}

ss_error_t ss_get_signal_list(ss_signal_info_t** list, size_t* count) {
    ss_error_t err = SS_OK;
    ss_signal_info_t* items = NULL;
    ss_signal_t* sig;
    size_t n = 0;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;
    if (!list || !count) return ctx_leave(ctx, locked, SS_ERR_NULL_PARAM, NULL);
    *list = NULL;
    *count = 0;

    if (ctx->signal_count > 0) {
        items = (ss_signal_info_t*)SS_CALLOC(ctx->signal_count, sizeof(ss_signal_info_t));
        if (!items) err = SS_ERR_MEMORY;
    }
    for (sig = live_next(ctx, NULL); sig && items && err == SS_OK;
         sig = live_next(ctx, sig)) {
        const char* desc = signal_description(sig);
        items[n].name = ss_strdup(sig->name);
        items[n].description = desc ? ss_strdup(desc) : NULL;
        items[n].slot_count = sig->slot_count;
        items[n].priority = sig->priority;
        n++;
        if (!items[n - 1].name || (desc && !items[n - 1].description)) err = SS_ERR_MEMORY;
    }
    if (err == SS_OK) {
        *list = items;
        *count = n;
    } else {
        ss_free_signal_list(items, n);
    }
    return ctx_leave(ctx, locked, err, NULL);
}
#endif

/* ------------------------------------------------------------------ */
/* Statistics                                                          */
/* ------------------------------------------------------------------ */

#if SS_ENABLE_MEMORY_STATS
ss_error_t ss_get_memory_stats(ss_memory_stats_t* stats) {
    ss_signal_t* sig;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;
    if (!stats) return ctx_leave(ctx, locked, SS_ERR_NULL_PARAM, NULL);

    memset(stats, 0, sizeof(*stats));
    stats->signals_used = ctx->signal_count;
    for (sig = signal_next(ctx, NULL); sig; sig = signal_next(ctx, sig)) {
        stats->slots_used += sig->slot_count;
#if !SS_USE_STATIC_MEMORY
        {
            ss_slot_t* s;
            stats->signals_allocated++;
            for (s = sig->slots; s; s = s->next) stats->slots_allocated++;
            stats->string_bytes += signal_bytes(sig) - sizeof(ss_signal_t);
        }
#endif
    }
#if SS_USE_STATIC_MEMORY
    stats->signals_allocated = SS_MAX_SIGNALS;
    stats->slots_allocated = SS_MAX_SLOTS;
    stats->total_bytes_allocated = sizeof(ss_context_t);
    stats->peak_bytes_allocated = sizeof(ss_context_t);
#else
    stats->total_bytes_allocated = ctx->bytes_in_use;
    stats->peak_bytes_allocated = ctx->peak_bytes;
#endif
    return ctx_leave(ctx, locked, SS_OK, NULL);
}

void ss_reset_memory_stats(void) {
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return;
    ctx->peak_bytes = ctx->bytes_in_use;
    (void)ctx_leave(ctx, locked, SS_OK, NULL);
}
#endif

#if SS_ENABLE_PERFORMANCE_STATS
ss_error_t ss_get_perf_stats(const char* signal_name, ss_perf_stats_t* stats) {
    ss_error_t err = SS_OK;
    ss_signal_t* sig;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;
    sig = stats ? signal_lookup(ctx, signal_name, &err) : NULL;
    if (!stats) err = SS_ERR_NULL_PARAM;
    if (sig) *stats = sig->perf_stats;
    return ctx_leave(ctx, locked, err, signal_name);
}

ss_error_t ss_enable_profiling(int enabled) {
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return SS_ERR_NULL_PARAM;
    ctx->profiling_enabled = enabled;
    return ctx_leave(ctx, locked, SS_OK, NULL);
}

void ss_reset_perf_stats(void) {
    ss_signal_t* sig;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return;
    for (sig = signal_next(ctx, NULL); sig; sig = signal_next(ctx, sig)) {
        memset(&sig->perf_stats, 0, sizeof(ss_perf_stats_t));
    }
    (void)ctx_leave(ctx, locked, SS_OK, NULL);
}
#endif

/* ------------------------------------------------------------------ */
/* Errors, configuration, debug                                        */
/* ------------------------------------------------------------------ */

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
        case SS_ERR_CONFIG_MISMATCH: return "Library and caller built with different configuration";
        default: return "Unknown error";
    }
}

void ss_set_error_handler(void (*handler)(ss_error_t error, const char* msg)) {
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return;
    ctx->error_handler = handler;
    (void)ctx_leave(ctx, locked, SS_OK, NULL);
}

void ss_set_max_slots_per_signal(size_t max_slots) {
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return;
    ctx->max_slots_per_signal = max_slots;
    (void)ctx_leave(ctx, locked, SS_OK, NULL);
}

size_t ss_get_max_slots_per_signal(void) {
    size_t max_slots;
    int locked;
    ss_context_t* ctx = ctx_enter(&locked);
    if (!ctx) return 0;
    max_slots = ctx->max_slots_per_signal;
    (void)ctx_leave(ctx, locked, SS_OK, NULL);
    return max_slots;
}

#if SS_ENABLE_THREAD_SAFETY
void ss_set_thread_safe(int enabled) {
    /* The mutex lives as long as the context; only the flag changes, and
     * ctx_enter() records per call whether it locked. */
    if (g_context) g_context->thread_safe = enabled ? 1 : 0;
}

int ss_is_thread_safe(void) {
    return g_context ? g_context->thread_safe : 0;
}
#endif

#if SS_ENABLE_DEBUG_TRACE
void ss_enable_trace(FILE* output) {
    if (g_context) g_context->trace_output = output;
}

void ss_disable_trace(void) {
    if (g_context) g_context->trace_output = NULL;
}
#endif
