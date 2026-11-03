#ifndef SS_CONFIG_H
#define SS_CONFIG_H

/* ========================================================================
 * Signal-Slot Library Configuration
 * ======================================================================== */

/* Presets come first so the defaults below follow from them. Like in
 * earlier versions, a preset overrides the options it controls. */

/* Minimal Build: strip optional features */
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

/* Embedded Build: static memory, no threading, small slot limit */
#ifdef SS_EMBEDDED_BUILD
    #undef SS_USE_STATIC_MEMORY
    #define SS_USE_STATIC_MEMORY 1
    #undef SS_ENABLE_THREAD_SAFETY
    #define SS_ENABLE_THREAD_SAFETY 0
    #undef SS_DEFAULT_MAX_SLOTS_PER_SIGNAL
    #define SS_DEFAULT_MAX_SLOTS_PER_SIGNAL 10
    #ifndef SS_DEFERRED_QUEUE_SIZE
        #define SS_DEFERRED_QUEUE_SIZE 16
    #endif
#endif

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
#endif

#ifndef SS_MAX_SIGNAL_NAME_LENGTH
    #if SS_USE_STATIC_MEMORY
        #define SS_MAX_SIGNAL_NAME_LENGTH 32
    #else
        #define SS_MAX_SIGNAL_NAME_LENGTH 256
    #endif
#endif

#if SS_USE_STATIC_MEMORY
    /* Per-signal description buffer, including the terminator. Longer
     * descriptions are rejected with SS_ERR_WOULD_OVERFLOW. */
    #ifndef SS_MAX_DESCRIPTION_LENGTH
        #define SS_MAX_DESCRIPTION_LENGTH 32
    #endif

    /* Inline payload buffer per deferred/batch entry for string and
     * custom data. Larger payloads are rejected with SS_ERR_WOULD_OVERFLOW. */
    #ifndef SS_DEFERRED_PAYLOAD_SIZE
        #define SS_DEFERRED_PAYLOAD_SIZE 32
    #endif

    /* 1: ss_init() keeps the library context in static storage (.bss).
     * 0: drop that storage when ss_init_static() supplies a pool; ss_init()
     *    then allocates the context with SS_CALLOC, as in 2.1. */
    #ifndef SS_STATIC_CONTEXT
        #define SS_STATIC_CONTEXT 1
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

/* The ISR queue claims entries with C11 atomics, GCC/Clang __atomic
 * builtins or MSVC Interlocked functions. On targets without atomic
 * compare-and-swap (e.g. Cortex-M0, AVR), define both of these to disable
 * and restore interrupts, e.g.:
 *   #define SS_ISR_CRITICAL_ENTER() uint32_t ss_irq_ = __get_PRIMASK(); __disable_irq()
 *   #define SS_ISR_CRITICAL_EXIT()  __set_PRIMASK(ss_irq_)
 */
/* #define SS_ISR_CRITICAL_ENTER() */
/* #define SS_ISR_CRITICAL_EXIT() */

#ifndef SS_ISR_QUEUE_SIZE
    #define SS_ISR_QUEUE_SIZE 16
#endif

#ifndef SS_CACHE_LINE_SIZE
    #define SS_CACHE_LINE_SIZE 64
#endif

/* Limits */
#ifndef SS_DEFAULT_MAX_SLOTS_PER_SIGNAL
    #define SS_DEFAULT_MAX_SLOTS_PER_SIGNAL 100
#endif

#ifndef SS_DEFERRED_QUEUE_SIZE
    #define SS_DEFERRED_QUEUE_SIZE 64
#endif

/* Entries per ss_batch_t */
#ifndef SS_BATCH_MAX_ENTRIES
    #define SS_BATCH_MAX_ENTRIES SS_DEFERRED_QUEUE_SIZE
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

/* SS_STRDUP(str) may be defined to override string duplication. By
 * default the library copies strings with SS_MALLOC, so overriding
 * SS_MALLOC and SS_FREE is enough to route every allocation. */

/* Deprecated: SS_TRACE is kept for source compatibility; the library no
 * longer uses it (see ss_enable_trace()). */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
    #if SS_ENABLE_DEBUG_TRACE
        #include <stdio.h>
        #define SS_TRACE(...) do { \
            fprintf(stderr, "[SS_TRACE] "); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
        } while(0)
    #else
        #define SS_TRACE(...) ((void)0)
    #endif
#else
    #define SS_TRACE (void)sizeof
#endif

#endif /* SS_CONFIG_H */
