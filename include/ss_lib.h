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
 *
 * The values are explicit and never change. 8 and 10 are unused (two 2.x
 * codes that were never returned, removed in 3.0).
 */
typedef enum {
    SS_OK = 0,                    /**< Success */
    SS_ERR_NULL_PARAM = 1,        /**< NULL parameter passed */
    SS_ERR_MEMORY = 2,            /**< Memory allocation failed */
    SS_ERR_NOT_FOUND = 3,         /**< Signal or slot not found */
    SS_ERR_ALREADY_EXISTS = 4,    /**< Signal already registered */
    SS_ERR_INVALID_TYPE = 5,      /**< Custom data queued (deferred or batch) in a
                                       library built without SS_ENABLE_CUSTOM_DATA */
    SS_ERR_BUFFER_TOO_SMALL = 6,  /**< Pool too small for ss_bus_create_in(), or
                                       buffer too small for ss_get_namespace() */
    SS_ERR_MAX_SLOTS = 7,         /**< Maximum slots per signal reached */
    SS_ERR_WOULD_OVERFLOW = 9,    /**< Name, payload, queue or the built-in buses
                                       exceed a fixed limit */
    SS_ERR_CONFIG_MISMATCH = 11   /**< Caller's SS_ABI_TAG differs from the library's,
                                       or a bus flag the library does not support */
} ss_error_t;

/**
 * @brief Data types supported by signal emission
 *
 * The enumerators are the same in every configuration. The custom data
 * functions, and queuing SS_TYPE_CUSTOM data, need
 * SS_ENABLE_CUSTOM_DATA.
 */
typedef enum {
    SS_TYPE_VOID,               /**< No data */
    SS_TYPE_INT,                /**< Integer data */
    SS_TYPE_FLOAT,              /**< Float data */
    SS_TYPE_DOUBLE,             /**< Double data */
    SS_TYPE_STRING,             /**< String data (null-terminated) */
    SS_TYPE_POINTER,            /**< Generic pointer data */
    SS_TYPE_CUSTOM              /**< Custom data with size (value.custom) */
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
 * Called once with the library's copy of the custom data, made by
 * ss_data_set_custom(), just before the library frees that copy (when a
 * setter replaces it, or on ss_data_clear() / ss_data_destroy()), so it
 * can release resources the copy refers to. The copy is const because
 * the library frees it itself: the cleanup function must not.
 *
 * @param data The library-owned copy of the custom data
 * @param size Its size in bytes
 */
typedef void (*ss_cleanup_func_t)(const void* data, size_t size);

/**
 * @brief Data structure passed to slot functions
 *
 * Ownership: only ss_data_set_string() and ss_data_set_custom() copy their
 * payload. They set owned, and the copy belongs to the object: the next
 * ss_data_set_* call, ss_data_clear() or ss_data_destroy() frees it.
 * Everything else borrows and leaves owned at 0: direct field writes,
 * ss_data_set_string_ref() / ss_data_set_custom_ref(), the ss_emit_*
 * helpers, and the data that slots receive. The library never frees a
 * borrowed payload.
 *
 * Start from SS_DATA_INIT or ss_data_init() (ss_data_create() returns an
 * initialized object). Release an object that owns its payload with
 * ss_data_clear() (or ss_data_destroy() if created) before it goes out
 * of scope, and do not write its payload fields by hand until then.
 *
 * The layout is the same in every configuration: custom data lives in the
 * value union whether or not SS_ENABLE_CUSTOM_DATA is set. Prefer
 * ss_data_get_custom() to reading value.custom.
 */
typedef struct ss_data {
    ss_data_type_t type;        /**< Type of data */
    unsigned char owned;        /**< Nonzero: the string or custom payload is
                                     the library's copy. Read-only for callers */
    union {
        int i_val;              /**< Integer value */
        float f_val;            /**< Float value */
        double d_val;           /**< Double value */
        const char* s_val;      /**< String value */
        void* p_val;            /**< Pointer value */
        struct {
            void* ptr;          /**< Custom data */
            size_t size;        /**< Its size in bytes */
            ss_cleanup_func_t cleanup; /**< Cleanup for an owned copy, or NULL */
        } custom;               /**< SS_TYPE_CUSTOM value */
    } value;                    /**< Data value union */
} ss_data_t;

/** @brief Initializer for an empty ss_data_t (SS_TYPE_VOID, nothing owned) */
#define SS_DATA_INIT { SS_TYPE_VOID, 0, { 0 } }

/**
 * @brief Slot function signature
 * @param data Signal data passed by emitter, borrowed (owned is 0) and
 *             valid only during the call
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
 * @defgroup core Buses
 * @brief Creating and destroying a bus
 *
 * A bus holds signals, slots, the deferred queue, the default namespace
 * and the settings below. Every function that uses library state takes
 * the bus as its first argument; buses share nothing, so the same signal
 * name on two buses names two signals. The library has no default bus.
 * @{
 */

/** @brief Opaque bus; created by ss_bus_create() or ss_bus_create_in() */
typedef struct ss_bus ss_bus_t;

/**
 * @brief ss_bus_create() flag: guard the bus with its own recursive mutex
 *
 * Needs a library built with SS_ENABLE_THREAD_SAFETY; otherwise creation
 * returns SS_ERR_CONFIG_MISMATCH. Without the flag the bus has no mutex
 * and must be used from one thread at a time.
 */
#define SS_BUS_THREAD_SAFE 0x1u

/** @brief Revision of the public layouts, bumped when one changes (1 to 15) */
#define SS_ABI_REVISION 1

/**
 * @brief Tag describing the ABI as the caller sees it: SS_ABI_REVISION and
 * sizeof(ss_data_t)
 *
 * No configuration macro changes a public layout, so the tag differs only
 * when the caller and the library come from different versions or were
 * compiled with options that change layouts (such as -fshort-enums or
 * structure packing). It fits a 16-bit size_t. The ss_bus_create macros
 * pass it, so every bus creation checks it.
 */
#define SS_ABI_TAG ((size_t)(sizeof(ss_data_t) * 16u + SS_ABI_REVISION))

/**
 * @brief Create a bus: call as ss_bus_create(&bus, flags)
 *
 * In dynamic mode the bus is allocated with SS_CALLOC. With
 * SS_USE_STATIC_MEMORY it is one of SS_STATIC_BUSES built-in buses and no
 * heap is used; claiming and releasing built-in buses is not locked, so
 * create and destroy them from one thread (normally at startup).
 *
 * @param bus Receives the bus, or NULL on failure
 * @param flags 0 or SS_BUS_THREAD_SAFE
 * @param abi_tag SS_ABI_TAG as the caller sees it (the macro passes it)
 * @return SS_OK, SS_ERR_NULL_PARAM, SS_ERR_MEMORY, SS_ERR_WOULD_OVERFLOW
 *         (static mode: every built-in bus is in use) or
 *         SS_ERR_CONFIG_MISMATCH (abi_tag differs from the library's, or a
 *         flag the library does not support)
 */
ss_error_t ss_bus_create_abi(ss_bus_t** bus, unsigned int flags, size_t abi_tag);

/**
 * @brief Create a bus in caller memory: call as
 * ss_bus_create_in(&bus, pool, pool_size, flags)
 *
 * Works in both memory modes. The pool needs no particular alignment;
 * ss_bus_pool_size() bytes always suffice. It must outlive the bus and is
 * never freed by the library. Signals and slots still follow the memory
 * model (heap in dynamic mode, the bus's fixed arrays in static mode).
 *
 * @return as ss_bus_create_abi(), plus SS_ERR_NULL_PARAM for a NULL pool
 *         and SS_ERR_BUFFER_TOO_SMALL for a pool that is too small
 */
ss_error_t ss_bus_create_in_abi(ss_bus_t** bus, void* pool, size_t pool_size,
                                unsigned int flags, size_t abi_tag);

/** @brief Create a bus, checking the caller's SS_ABI_TAG (see ss_bus_create_abi()) */
#define ss_bus_create(bus, flags) ss_bus_create_abi((bus), (flags), SS_ABI_TAG)

/** @brief Create a bus in a caller pool, checking the caller's SS_ABI_TAG */
#define ss_bus_create_in(bus, pool, size, flags) \
    ss_bus_create_in_abi((bus), (pool), (size), (flags), SS_ABI_TAG)

/** @brief Pool size for ss_bus_create_in(), including alignment slack */
size_t ss_bus_pool_size(void);

/**
 * @brief Destroy a bus and everything it holds
 *
 * Releases signals, slots and pending deferred emissions. NULL is a no-op.
 * Called from a slot, the destruction is deferred until the bus's
 * outermost call returns; meanwhile the bus refuses every call
 * (SS_ERR_NULL_PARAM). Afterwards the bus must not be used, like memory
 * after free(). Destroy batches created for the bus first. Not
 * thread-safe with respect to other threads using the same bus.
 */
void ss_bus_destroy(ss_bus_t* bus);

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
ss_error_t ss_signal_register(ss_bus_t* bus, const char* signal_name);

/**
 * @brief Register a signal with extended options
 * @param signal_name Unique name for the signal
 * @param description Human-readable description (can be NULL)
 * @param priority Default priority for this signal
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_signal_register_ex(ss_bus_t* bus, const char* signal_name,
                                 const char* description, ss_priority_t priority);

/**
 * @brief Unregister a signal and disconnect all slots
 *
 * Safe to call from a slot of the same signal: the remaining slots of
 * that emission are skipped and the memory is released when it ends.
 *
 * @param signal_name Name of the signal to remove
 * @return SS_OK on success, SS_ERR_NOT_FOUND if signal doesn't exist
 */
ss_error_t ss_signal_unregister(ss_bus_t* bus, const char* signal_name);

/**
 * @brief Check if a signal exists
 * @param signal_name Name of the signal
 * @return 1 if exists, 0 otherwise
 */
int ss_signal_exists(ss_bus_t* bus, const char* signal_name);

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
ss_error_t ss_connect(ss_bus_t* bus, const char* signal_name, ss_slot_func_t slot,
                      void* user_data);

/**
 * @brief Connect a slot with extended options
 * @param signal_name Name of the signal
 * @param slot Function to call when signal is emitted
 * @param user_data User data passed to slot function
 * @param priority Execution priority (higher = earlier)
 * @param handle Optional output for connection handle
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_connect_ex(ss_bus_t* bus, const char* signal_name, ss_slot_func_t slot,
                         void* user_data, ss_priority_t priority,
                         ss_connection_t* handle);

/**
 * @brief Disconnect a specific slot from a signal
 * @param signal_name Name of the signal
 * @param slot Function to disconnect
 * @return SS_OK on success, SS_ERR_NOT_FOUND if not connected
 */
ss_error_t ss_disconnect(ss_bus_t* bus, const char* signal_name, ss_slot_func_t slot);

/**
 * @brief Disconnect using a connection handle
 * @param handle Connection handle from ss_connect_ex()
 * @return SS_OK on success, SS_ERR_NOT_FOUND if invalid handle
 */
ss_error_t ss_disconnect_handle(ss_bus_t* bus, ss_connection_t handle);

/**
 * @brief Disconnect all slots from a signal
 * @param signal_name Name of the signal
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_disconnect_all(ss_bus_t* bus, const char* signal_name);

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
ss_error_t ss_emit(ss_bus_t* bus, const char* signal_name, const ss_data_t* data);

/**
 * @brief Emit a signal without data
 * @param signal_name Name of the signal to emit
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_emit_void(ss_bus_t* bus, const char* signal_name);

/**
 * @brief Emit a signal with integer data
 * @param signal_name Name of the signal to emit
 * @param value Integer value to pass
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_emit_int(ss_bus_t* bus, const char* signal_name, int value);

/**
 * @brief Emit a signal with float data
 * @param signal_name Name of the signal to emit
 * @param value Float value to pass
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_emit_float(ss_bus_t* bus, const char* signal_name, float value);

/**
 * @brief Emit a signal with double data
 * @param signal_name Name of the signal to emit
 * @param value Double value to pass
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_emit_double(ss_bus_t* bus, const char* signal_name, double value);

/**
 * @brief Emit a signal with string data
 * @param signal_name Name of the signal to emit
 * @param value String value to pass (must be null-terminated)
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_emit_string(ss_bus_t* bus, const char* signal_name, const char* value);

/**
 * @brief Emit a signal with pointer data
 * @param signal_name Name of the signal to emit
 * @param value Pointer value to pass
 * @return SS_OK on success, error code on failure
 */
ss_error_t ss_emit_pointer(ss_bus_t* bus, const char* signal_name, void* value);

/** @} */

#if SS_ENABLE_ISR_SAFE
/**
 * @brief Queue an integer event from an interrupt handler
 *
 * No locks, no allocation, no slot calls: the event is stored in a
 * fixed queue of SS_ISR_QUEUE_SIZE entries and delivered later by
 * ss_process_isr_queue(). Safe with nested interrupts and from several
 * threads. The queue is shared by every bus and usable before any bus
 * exists; an event goes to the bus that next processes the queue.
 *
 * @return SS_OK, SS_ERR_NULL_PARAM, or SS_ERR_WOULD_OVERFLOW if the
 *         queue is full or the name exceeds SS_MAX_SIGNAL_NAME_LENGTH-1
 */
ss_error_t ss_emit_from_isr(const char* signal_name, int value);

/**
 * @brief Deliver queued ISR events to bus, oldest first, from normal context
 *
 * Call regularly from the main loop. Names are resolved against the bus's
 * default namespace at delivery time. Events stay queued when a bus is
 * destroyed.
 *
 * @return SS_OK, SS_ERR_NULL_PARAM for a NULL or closing bus, or the last
 *         emission error (e.g. SS_ERR_NOT_FOUND for unknown signals)
 */
ss_error_t ss_process_isr_queue(ss_bus_t* bus);
#endif

/**
 * @brief Queue an emission for the next ss_flush_deferred()
 *
 * The queue stores a copy of the payload: string and custom data are
 * copied, and the custom cleanup function is not called for the copy,
 * which slots see as borrowed data.
 * In static memory mode string and custom payloads must fit in
 * SS_DEFERRED_PAYLOAD_SIZE bytes.
 *
 * @return SS_OK, SS_ERR_NULL_PARAM, SS_ERR_MEMORY,
 *         SS_ERR_WOULD_OVERFLOW if the queue is full or the name or
 *         payload is too long, or SS_ERR_INVALID_TYPE for custom data in
 *         a library built without SS_ENABLE_CUSTOM_DATA
 */
ss_error_t ss_emit_deferred(ss_bus_t* bus, const char* signal_name, const ss_data_t* data);

/**
 * @brief Emit every queued deferred emission, oldest first
 *
 * Entries queued by slots during the flush wait for the next flush.
 *
 * @return SS_OK, or the last emission error
 */
ss_error_t ss_flush_deferred(ss_bus_t* bus);

/**
 * @defgroup data Data Objects
 * @brief Filling and releasing ss_data_t payloads
 *
 * ss_data_t describes who owns a payload. These functions take no lock:
 * each object belongs to its caller.
 * @{
 */

/**
 * @brief Allocate an empty object of the given type (heap, in both modes)
 * @return The object, or NULL; release it with ss_data_destroy()
 */
ss_data_t* ss_data_create(ss_data_type_t type);

/** @brief ss_data_clear(), then free an object from ss_data_create(); NULL is a no-op */
void ss_data_destroy(ss_data_t* data);

/**
 * @brief Make *data an empty SS_TYPE_VOID object, whatever it contains
 *
 * Same as assigning SS_DATA_INIT, for memory that holds no object yet. It
 * does not release a payload: use ss_data_clear() on an object in use.
 * NULL is a no-op.
 */
void ss_data_init(ss_data_t* data);

/**
 * @brief Release an owned payload and make the object SS_TYPE_VOID
 *
 * An owned custom payload's cleanup runs first. A borrowed payload is
 * left alone. NULL is a no-op.
 */
void ss_data_clear(ss_data_t* data);

/* Every setter first releases the payload the object owns, if any. */
ss_error_t ss_data_set_int(ss_data_t* data, int value);
ss_error_t ss_data_set_float(ss_data_t* data, float value);
ss_error_t ss_data_set_double(ss_data_t* data, double value);
ss_error_t ss_data_set_pointer(ss_data_t* data, void* value);

/**
 * @brief Store a copy of a string, owned by the object
 *
 * Allocates in both memory modes. A NULL value stores a NULL string.
 * @return SS_OK, SS_ERR_NULL_PARAM, or SS_ERR_MEMORY (object unchanged)
 */
ss_error_t ss_data_set_string(ss_data_t* data, const char* value);

/**
 * @brief Store a borrowed string, which must outlive every use of the object
 *
 * Never allocates; the library never frees the string. Passing the
 * object's own payload leaves it unchanged, still owned.
 * @return SS_OK, or SS_ERR_NULL_PARAM if data is NULL
 */
ss_error_t ss_data_set_string_ref(ss_data_t* data, const char* value);

#if SS_ENABLE_CUSTOM_DATA
/**
 * @brief Store a copy of size bytes at value, owned by the object
 *
 * Allocates in both memory modes. cleanup (may be NULL) is called once on
 * the copy just before it is freed.
 * @return SS_OK, SS_ERR_NULL_PARAM (NULL data or value, or size 0), or
 *         SS_ERR_MEMORY (object unchanged)
 */
ss_error_t ss_data_set_custom(ss_data_t* data, const void* value, size_t size,
                              ss_cleanup_func_t cleanup);

/**
 * @brief Store borrowed custom data, which must outlive every use of the object
 *
 * Never allocates and has no cleanup; the library never frees the data.
 * Passing the object's own payload (same pointer and size) leaves it
 * unchanged, still owned.
 * @return SS_OK, or SS_ERR_NULL_PARAM (NULL data or value, or size 0)
 */
ss_error_t ss_data_set_custom_ref(ss_data_t* data, void* value, size_t size);
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

/** @} */

#if SS_ENABLE_INTROSPECTION
/* Signal information */
typedef struct ss_signal_info {
    char* name;
    size_t slot_count;
    const char* description;
    ss_priority_t priority;
} ss_signal_info_t;

size_t ss_get_signal_count(ss_bus_t* bus);

/**
 * @brief Snapshot of all registered signals
 *
 * The list and its name and description strings are owned by the caller
 * and stay valid after the signals change; release them with
 * ss_free_signal_list(). Allocates in both memory modes.
 */
ss_error_t ss_get_signal_list(ss_bus_t* bus, ss_signal_info_t** list, size_t* count);
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

ss_error_t ss_get_memory_stats(ss_bus_t* bus, ss_memory_stats_t* stats);
void ss_reset_memory_stats(ss_bus_t* bus);
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

ss_error_t ss_get_perf_stats(ss_bus_t* bus, const char* signal_name,
                             ss_perf_stats_t* stats);
ss_error_t ss_enable_profiling(ss_bus_t* bus, int enabled);
void ss_reset_perf_stats(ss_bus_t* bus);
#endif

/* Error handling */
const char* ss_error_string(ss_error_t error);

/**
 * @brief Set the bus's error handler (NULL removes it)
 *
 * Called, after the bus is unlocked, with every error a call on this bus
 * returns. Errors of calls given no bus (a NULL bus or batch) reach no
 * handler.
 */
void ss_set_error_handler(ss_bus_t* bus, void (*handler)(ss_error_t error, const char* msg));

/* Configuration: limit of live slots per signal on this bus */
void ss_set_max_slots_per_signal(ss_bus_t* bus, size_t max_slots);
size_t ss_get_max_slots_per_signal(ss_bus_t* bus);

/**
 * @brief Set the default namespace (NULL or "" clears it)
 *
 * While set, every signal name without "::" is resolved as "ns::name" by
 * all functions that take a signal name. Names containing "::" are used
 * as given.
 *
 * @return SS_OK, SS_ERR_NULL_PARAM for a NULL bus, or
 *         SS_ERR_WOULD_OVERFLOW if ns cannot prefix any valid name
 */
ss_error_t ss_set_namespace(ss_bus_t* bus, const char* ns);

/**
 * @brief Copy the default namespace into buf ("" when none is set)
 *
 * The copy is made under the bus's lock, so it may run while other
 * threads set the namespace. A buffer of SS_MAX_SIGNAL_NAME_LENGTH bytes
 * always fits. On an error buf holds "" (when size > 0), never a
 * truncated namespace.
 *
 * @return SS_OK, SS_ERR_NULL_PARAM for a NULL bus or buf, or
 *         SS_ERR_BUFFER_TOO_SMALL if the namespace and its terminator
 *         need more than size bytes
 */
ss_error_t ss_get_namespace(ss_bus_t* bus, char* buf, size_t size);

/** @brief Emit "ns::signal_name" regardless of the default namespace */
ss_error_t ss_emit_namespaced(ss_bus_t* bus, const char* ns, const char* signal_name,
                              const ss_data_t* data);

/*
 * Batch operations. A batch belongs to the bus it was created for: that
 * bus supplies the default namespace when entries are added, runs the
 * emission and reports errors. A batch stores copies of its payloads like
 * the deferred queue, and ss_batch_add() returns the same errors as
 * ss_emit_deferred(). A batch is owned by the caller and must not be used
 * from several threads at once; slots may add to it or destroy it while
 * it is being emitted.
 */
typedef struct ss_batch ss_batch_t;

/** @brief A batch for bus (heap, in both modes), or NULL */
ss_batch_t* ss_batch_create(ss_bus_t* bus);

/**
 * @brief Free a batch and its pending entries; NULL is a no-op
 *
 * Destroy batches before their bus. ss_batch_destroy() itself does not use
 * the bus, so a batch whose bus was destroyed from one of its slots can
 * still be destroyed; no other batch call may follow.
 */
void ss_batch_destroy(ss_batch_t* batch);
ss_error_t ss_batch_add(ss_batch_t* batch, const char* signal_name,
                       const ss_data_t* data);
ss_error_t ss_batch_emit(ss_batch_t* batch);

/* Debug support: a bus's trace is off until ss_enable_trace() names a stream */
#if SS_ENABLE_DEBUG_TRACE
#include <stdio.h>
void ss_enable_trace(ss_bus_t* bus, FILE* output);
void ss_disable_trace(ss_bus_t* bus);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SS_LIB_H */
