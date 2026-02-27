#ifndef SS_CONFIG_H
#define SS_CONFIG_H

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
#endif

#ifndef SS_MAX_SIGNAL_NAME_LENGTH
    #if SS_USE_STATIC_MEMORY
        #define SS_MAX_SIGNAL_NAME_LENGTH 32
    #else
        #define SS_MAX_SIGNAL_NAME_LENGTH 256
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
    #define SS_TRACE(...) do { \
        fprintf(stderr, "[SS_TRACE] "); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } while(0)
#else
    #define SS_TRACE(...) ((void)0)
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

#endif /* SS_CONFIG_H */
