/**
 * @file ss_lib.h
 * @brief SS_Lib - Lightweight Signal-Slot Library for C
 * @version 3.0.0
 * 
 * SS_Lib provides a simple, efficient signal-slot mechanism for C programs.
 * It's designed to work in resource-constrained environments while still
 * providing features needed for complex applications.
 * 
 * @copyright MIT License
 */

#ifndef SS_LIB_H
#define SS_LIB_H

#include <stddef.h>
#include <stdint.h>
#include "ss_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Library version (semantic versioning) */
#define SS_VERSION_MAJOR 3
#define SS_VERSION_MINOR 0
#define SS_VERSION_PATCH 0
#define SS_VERSION_STRING "3.0.0"

/**
 * @brief Error codes returned by SS_Lib functions
 */
typedef enum {
    SS_OK = 0,                  /**< Success */
    SS_ERR_NULL_PARAM,          /**< NULL parameter passed */
    SS_ERR_MEMORY,              /**< Memory allocation failed */
    SS_ERR_NOT_FOUND,           /**< Signal or slot not found */
    SS_ERR_ALREADY_EXISTS,      /**< Signal already registered */
    SS_ERR_INVALID_TYPE,        /**< Reserved, not currently returned */
    SS_ERR_BUFFER_TOO_SMALL,    /**< Reserved, not currently returned */
    SS_ERR_MAX_SLOTS,           /**< Maximum slots per signal reached */
    SS_ERR_TIMEOUT,             /**< Reserved, not currently returned */
    SS_ERR_WOULD_OVERFLOW,      /**< Name, payload or queue exceeds a fixed limit */
    SS_ERR_ISR_UNSAFE,          /**< Reserved, not currently returned */
    SS_ERR_CONFIG_MISMATCH      /**< Caller and library built with different flags */
} ss_error_t;

/**
 * @brief Data types supported by signal emission
 */
typedef enum {
    SS_TYPE_VOID,               /**< No data */
    SS_TYPE_INT,                /**< Integer data */
    SS_TYPE_FLOAT,              /**< Float data */
    SS_TYPE_DOUBLE,             /**< Double data */
    SS_TYPE_STRING,             /**< String data (null-terminated) */
    SS_TYPE_POINTER             /**< Generic pointer data */
#if SS_ENABLE_CUSTOM_DATA
    , SS_TYPE_CUSTOM            /**< Custom data with size */
#endif
} ss_data_type_t;

/**
 * @brief Priority levels for slot execution order
 * 
 * Higher priority slots are executed before lower priority ones.
 */
typedef enum {
    SS_PRIORITY_LOW = 0,        /**< Lowest priority */
    SS_PRIORITY_NORMAL = 5,     /**< Default priority */
    SS_PRIORITY_HIGH = 10,      /**< High priority */
    SS_PRIORITY_CRITICAL = 15   /**< Highest priority */
} ss_priority_t;

/**
 * @brief Cleanup function for custom data
 *
 * Called by ss_data_set_custom() and ss_data_destroy() with the library's
 * copy of the custom data, so it can release resources the copy refers
 * to. The library frees the copy itself afterwards: the cleanup function
 * must not free the pointer it receives.
 *
 * @param data The library-owned copy of the custom data
 */
typedef void (*ss_cleanup_func_t)(void* data);

/**
 * @brief Data structure passed to slot functions
 *
 * Ownership: an ss_data_t created with ss_data_create() owns the string
 * and custom data stored into it by ss_data_set_string() and
 * ss_data_set_custom(); ss_data_destroy() frees them. Do not assign
 * value.s_val or custom_data directly on such an object. A stack
 * ss_data_t filled in by hand (as the ss_emit_* helpers do) borrows its
 * pointers and must not be passed to ss_data_destroy() or ss_data_set_*.
 */
typedef struct ss_data {
    ss_data_type_t type;        /**< Type of data */
    union {
        int i_val;              /**< Integer value */
        float f_val;            /**< Float value */
        double d_val;           /**< Double value */
        const char* s_val;      /**< String value */
        void* p_val;            /**< Pointer value */
    } value;                    /**< Data value union */
#if SS_ENABLE_CUSTOM_DATA
    size_t size;                /**< Size of custom data */
    void* custom_data;          /**< Pointer to custom data */
    ss_cleanup_func_t custom_cleanup; /**< Optional cleanup for custom data */
#endif
} ss_data_t;

/**
 * @brief Slot function signature
 * @param data Signal data passed by emitter
 * @param user_data User data passed during connection
 */
typedef void (*ss_slot_func_t)(const ss_data_t* data, void* user_data);

/**
 * @brief Opaque handle for a signal-slot connection
 * 
 * Used with ss_disconnect_handle() for easy disconnection.
 */
typedef uintptr_t ss_connection_t;

/**
 * @defgroup core Core Functions
 * @brief Library initialization and cleanup
 * @{
 */

/**
 * @brief Initialize the library
 *
 * In dynamic mode the library context is heap-allocated; with
 * SS_USE_STATIC_MEMORY it lives in static storage (unless
 * SS_STATIC_CONTEXT is 0) and no heap is used.
 * Not thread-safe: call before other threads use the library.
 * Prefer ss_init_abi(SS_ABI_TAG), which also detects a library built
 * with different configuration flags.
 *
 * @return SS_OK on success (also if already initialized),
 *         SS_ERR_ALREADY_EXISTS if a deferred ss_cleanup() is pending
 */
ss_error_t ss_init(void);

/**
 * @brief Initialize the library with its context in a caller-owned pool
 *
 * The pool needs no particular alignment; given at least
 * ss_get_required_pool_size() bytes it holds the library context, must
 * outlive ss_cleanup(), and is never freed. Signals and slots still
 * follow the memory model (heap in dynamic mode, fixed arrays in static
 * mode). For compatibility with 2.1, a NULL or smaller pool is ignored
 * and the call behaves like ss_init().
 *
 * @param memory_pool Caller-owned buffer
 * @param pool_size Size of the buffer in bytes
 * @return as ss_init()
 */
ss_error_t ss_init_static(void* memory_pool, size_t pool_size);

/**
 * @brief Bytes needed by ss_init_static(), including alignment slack
 */
size_t ss_get_required_pool_size(void);

/**
 * @brief Tag describing the configuration-dependent ABI as the caller sees
 * it; currently the layout of ss_data_t (which SS_ENABLE_CUSTOM_DATA
 * changes).
 */
#define SS_ABI_TAG ((size_t)sizeof(ss_data_t))

/**
 * @brief ss_init() that first checks the caller's SS_ABI_TAG
 *
 * Call as ss_init_abi(SS_ABI_TAG). Returns SS_ERR_CONFIG_MISMATCH when
 * the caller was compiled with different flags than the library, which
 * would otherwise corrupt memory later.
 */
ss_error_t ss_init_abi(size_t abi_tag);

/** @brief ss_init_static() that first checks the caller's SS_ABI_TAG */
ss_error_t ss_init_static_abi(void* memory_pool, size_t pool_size, size_t abi_tag);

/**
 * @brief Clean up all resources and reset the library
 *
 * Releases signals, slots, pending deferred and ISR events. Called from
 * inside a slot, the cleanup is deferred until the outermost emission
 * returns, and the library behaves as uninitialized meanwhile. Not
 * thread-safe with respect to other threads using the library.
 */
void ss_cleanup(void);

/** @brief Runtime version string, equal to SS_VERSION_STRING */
const char* ss_version(void);

/** @} */

/**
 * @defgroup signals Signal Management
 * @brief Functions for registering and managing signals
 * @{
 */

/**
 * @brief Register a new signal
 * @param signal_name Unique name for the signal
 * @return SS_OK on success, SS_ERR_ALREADY_EXISTS if signal exists
 */
ss_error_t ss_signal_register(const char* signal_name);

/**
 * @brief Register a signal with extended options
 * @param signal_name Unique name for the signal
 * @param description Human-readable description (can be NULL)
 * @param priority Default priority for this signal
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_signal_register_ex(const char* signal_name, 
                                const char* description,
                                ss_priority_t priority);

/**
 * @brief Unregister a signal and disconnect all slots
 *
 * Safe to call from a slot of the same signal: the remaining slots of
 * that emission are skipped and the memory is released when it ends.
 *
 * @param signal_name Name of the signal to remove
 * @return SS_OK on success, SS_ERR_NOT_FOUND if signal doesn't exist
 */
ss_error_t ss_signal_unregister(const char* signal_name);

/**
 * @brief Check if a signal exists
 * @param signal_name Name of the signal
 * @return 1 if exists, 0 otherwise
 */
int ss_signal_exists(const char* signal_name);

/** @} */

/**
 * @defgroup connections Connection Management
 * @brief Functions for connecting and disconnecting slots
 * @{
 */

/**
 * @brief Connect a slot to a signal
 * @param signal_name Name of the signal
 * @param slot Function to call when signal is emitted
 * @param user_data User data passed to slot function
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_connect(const char* signal_name, ss_slot_func_t slot, void* user_data);

/**
 * @brief Connect a slot with extended options
 * @param signal_name Name of the signal
 * @param slot Function to call when signal is emitted
 * @param user_data User data passed to slot function
 * @param priority Execution priority (higher = earlier)
 * @param handle Optional output for connection handle
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_connect_ex(const char* signal_name, ss_slot_func_t slot, 
                        void* user_data, ss_priority_t priority,
                        ss_connection_t* handle);

/**
 * @brief Disconnect a specific slot from a signal
 * @param signal_name Name of the signal
 * @param slot Function to disconnect
 * @return SS_OK on success, SS_ERR_NOT_FOUND if not connected
 */
ss_error_t ss_disconnect(const char* signal_name, ss_slot_func_t slot);

/**
 * @brief Disconnect using a connection handle
 * @param handle Connection handle from ss_connect_ex()
 * @return SS_OK on success, SS_ERR_NOT_FOUND if invalid handle
 */
ss_error_t ss_disconnect_handle(ss_connection_t handle);

/**
 * @brief Disconnect all slots from a signal
 * @param signal_name Name of the signal
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_disconnect_all(const char* signal_name);

/** @} */

/**
 * @defgroup emission Signal Emission
 * @brief Functions for emitting signals
 * @{
 */

/**
 * @brief Emit a signal with custom data
 * @param signal_name Name of the signal to emit
 * @param data Data to pass to slots (can be NULL)
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_emit(const char* signal_name, const ss_data_t* data);

/**
 * @brief Emit a signal without data
 * @param signal_name Name of the signal to emit
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_emit_void(const char* signal_name);

/**
 * @brief Emit a signal with integer data
 * @param signal_name Name of the signal to emit
 * @param value Integer value to pass
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_emit_int(const char* signal_name, int value);

/**
 * @brief Emit a signal with float data
 * @param signal_name Name of the signal to emit
 * @param value Float value to pass
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_emit_float(const char* signal_name, float value);

/**
 * @brief Emit a signal with double data
 * @param signal_name Name of the signal to emit
 * @param value Double value to pass
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_emit_double(const char* signal_name, double value);

/**
 * @brief Emit a signal with string data
 * @param signal_name Name of the signal to emit
 * @param value String value to pass (must be null-terminated)
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_emit_string(const char* signal_name, const char* value);

/**
 * @brief Emit a signal with pointer data
 * @param signal_name Name of the signal to emit
 * @param value Pointer value to pass
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_emit_pointer(const char* signal_name, void* value);

/** @} */

#if SS_ENABLE_ISR_SAFE
/**
 * @brief Queue an integer event from an interrupt handler
 *
 * No locks, no allocation, no slot calls: the event is stored in a
 * fixed queue of SS_ISR_QUEUE_SIZE entries and delivered later by
 * ss_process_isr_queue(). Safe with nested interrupts and from several
 * threads. Usable before ss_init().
 *
 * @return SS_OK, SS_ERR_NULL_PARAM, or SS_ERR_WOULD_OVERFLOW if the
 *         queue is full or the name exceeds SS_MAX_SIGNAL_NAME_LENGTH-1
 */
ss_error_t ss_emit_from_isr(const char* signal_name, int value);

/**
 * @brief Deliver queued ISR events, oldest first, from normal context
 *
 * Call regularly from the main loop. Names are resolved against the
 * default namespace at delivery time.
 *
 * @return SS_OK, SS_ERR_NULL_PARAM if not initialized, or the last
 *         emission error (e.g. SS_ERR_NOT_FOUND for unknown signals)
 */
ss_error_t ss_process_isr_queue(void);
#endif

/**
 * @brief Queue an emission for the next ss_flush_deferred()
 *
 * The queue stores a copy of the payload: string and custom data are
 * copied, and the custom cleanup function is not called for the copy.
 * In static memory mode string and custom payloads must fit in
 * SS_DEFERRED_PAYLOAD_SIZE bytes.
 *
 * @return SS_OK, SS_ERR_NULL_PARAM, SS_ERR_MEMORY, or
 *         SS_ERR_WOULD_OVERFLOW if the queue is full or the name or
 *         payload is too long
 */
ss_error_t ss_emit_deferred(const char* signal_name, const ss_data_t* data);

/**
 * @brief Emit every queued deferred emission, oldest first
 *
 * Entries queued by slots during the flush wait for the next flush.
 *
 * @return SS_OK, or the last emission error
 */
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

/**
 * @brief Snapshot of all registered signals
 *
 * The list and its name and description strings are owned by the caller
 * and stay valid after the signals change; release them with
 * ss_free_signal_list(). Allocates in both memory modes.
 */
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
/**
 * @brief Turn locking on or off (off by default)
 *
 * All library state is guarded by one recursive mutex, so slots may call
 * back into the library. Slots run with the mutex held: a slot must not
 * wait for another thread that is itself calling the library. Change
 * the setting only while no other thread is using the library.
 */
void ss_set_thread_safe(int enabled);
int ss_is_thread_safe(void);
#endif

/**
 * @brief Set the default namespace (NULL or "" clears it)
 *
 * While set, every signal name without "::" is resolved as "ns::name" by
 * all functions that take a signal name. Names containing "::" are used
 * as given.
 *
 * @return SS_OK, SS_ERR_NULL_PARAM if not initialized, or
 *         SS_ERR_WOULD_OVERFLOW if ns cannot prefix any valid name
 */
ss_error_t ss_set_namespace(const char* ns);

/**
 * @brief Current default namespace, or NULL; valid until the next change
 *
 * The string is the library's own buffer: with several threads, read it
 * only while no other thread can call ss_set_namespace().
 */
const char* ss_get_namespace(void);

/** @brief Emit "ns::signal_name" regardless of the default namespace */
ss_error_t ss_emit_namespaced(const char* ns, const char* signal_name,
                              const ss_data_t* data);

/*
 * Batch operations. A batch stores copies of its payloads like the
 * deferred queue. A batch is owned by the caller and must not be used
 * from several threads at once; slots may add to it or destroy it while
 * it is being emitted.
 */
typedef struct ss_batch ss_batch_t;

ss_batch_t* ss_batch_create(void);
void ss_batch_destroy(ss_batch_t* batch);
ss_error_t ss_batch_add(ss_batch_t* batch, const char* signal_name,
                       const ss_data_t* data);
ss_error_t ss_batch_emit(ss_batch_t* batch);

/* Debug support: trace is off until ss_enable_trace() names a stream */
#if SS_ENABLE_DEBUG_TRACE
#include <stdio.h>
void ss_enable_trace(FILE* output);
void ss_disable_trace(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SS_LIB_H */
