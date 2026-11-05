# Migrating from 2.x to 3.0

SS_Lib 3.0 is in development on the `v3` branch. This guide grows with it:
each change that breaks 2.x code adds its section here, in the same pull
request. Sections without content describe changes that have not landed yet;
[3.0-plan.md](3.0-plan.md) has the design.

3.0 changes the API in ways the compiler reports: every call that must change
fails to compile until it is changed, and none silently changes meaning.

## Building and linking

- The shared library is `libss_lib.so.3` (Linux) or `libss_lib.3.dylib`
  (macOS). Rebuild and relink programs; a program built against 2.x keeps
  using `libss_lib.so.2`, and both can be installed side by side.
- With CMake, ask for the new major version: `find_package(ss_lib 3 REQUIRED)`.
  A request for 2 does not accept 3.0.
- pkg-config keeps the name `ss_lib`, and headers stay in `include/ss_lib/`.

## Creating and destroying a bus

2.x kept all state in one global context. 3.0 keeps it in a bus that you
create, pass to every call and destroy. The library has no default bus.

| 2.x | 3.0 |
|---|---|
| `ss_init()`, `ss_init_abi(SS_ABI_TAG)` | `ss_bus_create(&bus, 0)` |
| `ss_init_static(pool, size)`, `ss_init_static_abi(pool, size, SS_ABI_TAG)` | `ss_bus_create_in(&bus, pool, size, 0)` |
| `ss_get_required_pool_size()` | `ss_bus_pool_size()` |
| `ss_cleanup()` | `ss_bus_destroy(bus)` |

```c
/* 2.x */
if (ss_init() != SS_OK) return 1;
ss_signal_register("ready");
/* ... */
ss_cleanup();

/* 3.0 */
ss_bus_t* bus = NULL;
if (ss_bus_create(&bus, 0) != SS_OK) return 1;
ss_signal_register(bus, "ready");
/* ... */
ss_bus_destroy(bus);
```

- `ss_bus_create()` and `ss_bus_create_in()` are macros that pass
  `SS_ABI_TAG`, so every creation checks that the caller and the library
  agree on the layout of `ss_data_t`; a mismatch returns
  `SS_ERR_CONFIG_MISMATCH`. Code that called `ss_init_abi(SS_ABI_TAG)` gets
  this check without asking. Callers that cannot use C macros (FFI) call
  `ss_bus_create_abi()` / `ss_bus_create_in_abi()` with the tag.
- On failure the bus pointer is set to NULL. Check the return value: there is
  no global to fall back on.
- **Pools no longer fall back.** `ss_init_static()` with a NULL or too small
  pool behaved like `ss_init()`. `ss_bus_create_in()` returns
  `SS_ERR_NULL_PARAM` or `SS_ERR_BUFFER_TOO_SMALL` instead. A pool of
  `ss_bus_pool_size()` bytes always suffices; it needs no particular
  alignment, must outlive the bus and is never freed by the library. It
  works in both memory modes.
- **Static mode never allocates a bus.** `ss_bus_create()` takes one of the
  `SS_STATIC_BUSES` built-in buses (default 1) and returns
  `SS_ERR_WOULD_OVERFLOW` when all are in use. Create and destroy built-in
  buses from one thread, normally at startup: claiming them is not locked.
- **After `ss_bus_destroy()` the bus is gone.** In 2.x a call after
  `ss_cleanup()` returned `SS_ERR_NULL_PARAM`; in 3.0 using a destroyed bus
  is undefined, like using memory after `free()`. Set your pointer to NULL.
  `ss_bus_destroy(NULL)` is a no-op.
- **Destroying from a slot** works as `ss_cleanup()` did: the remaining slots
  of the emission are skipped, the bus refuses every call
  (`SS_ERR_NULL_PARAM`) and is freed when its outermost call returns. Other
  buses are not affected.
- Calling `ss_init()` twice was harmless in 2.x. Creating twice gives two
  independent buses: make sure each part of the program uses the bus it
  means.

## The bus argument

Every function that uses library state takes the bus as its first argument
and keeps its name. A call that is not changed fails to compile, because the
number of arguments changes; no call silently changes meaning.

| Takes the bus first | Unchanged (no bus) |
|---|---|
| `ss_signal_register`, `ss_signal_register_ex`, `ss_signal_unregister`, `ss_signal_exists` | `ss_version`, `ss_error_string` |
| `ss_connect`, `ss_connect_ex`, `ss_disconnect`, `ss_disconnect_handle`, `ss_disconnect_all` | `ss_data_*` |
| `ss_emit`, `ss_emit_void`, `ss_emit_int`, `ss_emit_float`, `ss_emit_double`, `ss_emit_string`, `ss_emit_pointer`, `ss_emit_namespaced` | `ss_free_signal_list` |
| `ss_emit_deferred`, `ss_flush_deferred` | `ss_batch_add`, `ss_batch_emit`, `ss_batch_destroy` (the batch knows its bus) |
| `ss_set_namespace`, `ss_get_namespace` (which also copies into a buffer, see [below](#removed-and-changed-in-passing)) | `ss_emit_from_isr` (until the ISR queue moves into the bus) |
| `ss_get_signal_count`, `ss_get_signal_list` | |
| `ss_get_memory_stats`, `ss_reset_memory_stats` | |
| `ss_get_perf_stats`, `ss_enable_profiling`, `ss_reset_perf_stats` | |
| `ss_set_error_handler`, `ss_set_max_slots_per_signal`, `ss_get_max_slots_per_signal` | |
| `ss_enable_trace`, `ss_disable_trace` | |
| `ss_process_isr_queue`, `ss_batch_create` | |

For code that has one bus in a variable named `bus`, this `sed` command adds
the argument to every call in the table. It needs GNU sed (`gsed` on
macOS). Review the diff: it does not touch function pointers, or calls split
across lines before the opening parenthesis.

```sh
sed -i -E \
  -e 's/\b(ss_flush_deferred|ss_get_signal_count|ss_reset_memory_stats|ss_reset_perf_stats|ss_get_max_slots_per_signal|ss_disable_trace|ss_process_isr_queue|ss_batch_create)\(\s*\)/\1(bus)/g' \
  -e 's/\b(ss_signal_register(_ex)?|ss_signal_unregister|ss_signal_exists|ss_connect(_ex)?|ss_disconnect(_handle|_all)?|ss_emit(_void|_int|_float|_double|_string|_pointer|_namespaced|_deferred)?|ss_set_namespace|ss_get_signal_list|ss_get_memory_stats|ss_get_perf_stats|ss_enable_profiling|ss_set_error_handler|ss_set_max_slots_per_signal|ss_enable_trace)\(/\1(bus, /g' \
  src/*.c
```

Then replace `ss_init()` and `ss_cleanup()` as shown in the previous section,
and `ss_get_namespace()` as shown in
[Removed and changed in passing](#removed-and-changed-in-passing).

**Slots that call the library** need the bus too. The slot signature does
not change; pass the bus as `user_data`, or in a struct that `user_data`
points to:

```c
static void on_saved(const ss_data_t* data, void* user_data) {
    ss_bus_t* bus = (ss_bus_t*)user_data;
    ss_emit_string(bus, "status", ss_data_get_string(data));
}

ss_connect(bus, "saved", on_saved, bus);
```

**A program that wants one global bus** can keep its 2.x shape with a small
wrapper of its own. The library provides none, because a global is the
application's choice:

```c
/* app_bus.h */
#include "ss_lib.h"
extern ss_bus_t* app_bus;
ss_error_t app_bus_init(void);
void app_bus_cleanup(void);

/* app_bus.c */
#include "app_bus.h"
ss_bus_t* app_bus = NULL;
ss_error_t app_bus_init(void) { return ss_bus_create(&app_bus, 0); }
void app_bus_cleanup(void) { ss_bus_destroy(app_bus); app_bus = NULL; }
```

Calls then read `ss_emit_int(app_bus, "tick", n)`; the `sed` command above
works with `bus` replaced by `app_bus`.

## Thread safety

`ss_set_thread_safe(1)` becomes the `SS_BUS_THREAD_SAFE` flag at creation,
and `ss_is_thread_safe()` is gone: a bus is thread-safe for its whole life
or not at all.

```c
/* 2.x */
ss_init();
ss_set_thread_safe(1);

/* 3.0 */
ss_bus_create(&bus, SS_BUS_THREAD_SAFE);
```

- The flag needs a library built with `SS_ENABLE_THREAD_SAFETY` (the
  default). Without it, creation returns `SS_ERR_CONFIG_MISMATCH` instead of
  quietly giving an unlocked bus.
- Each such bus has its own recursive mutex; a bus created without the flag
  has none. Buses share no state, so threads that each use their own bus
  need no flag at all.
- Inside a bus nothing changes from 2.x: slots run with the bus's lock held
  and may call back into the same bus.
- **Cross-bus calls from slots go in one direction only.** A slot of bus A
  that calls bus B holds A's lock while it takes B's. If another thread's
  slot of B calls A at the same time, the two threads deadlock.
- `ss_bus_destroy()` is not thread-safe with respect to other threads using
  the same bus, as `ss_cleanup()` was not.

## Error handler

Not landed yet: the handler receives the bus and a user pointer. For now
`ss_set_error_handler(bus, handler)` sets a handler per bus, with the 2.x
handler signature. Errors of calls given no bus (a NULL bus or batch, or a
failed `ss_bus_create()`) reach no handler.

## Batches

`ss_batch_create()` takes the bus the batch belongs to:
`ss_batch_create(bus)`. That bus supplies the default namespace when
entries are added, runs `ss_batch_emit()` and reports errors.
`ss_batch_add()`, `ss_batch_emit()` and `ss_batch_destroy()` are unchanged.
Destroy batches before their bus. A batch whose bus was destroyed from one
of its own slots can still be destroyed, but not used.

## ISR queue

Not landed yet: `ss_emit_from_isr()` takes the bus. For now the ISR queue is
still shared by every bus: `ss_emit_from_isr(name, value)` is unchanged and
`ss_process_isr_queue(bus)` delivers the queued events to the given bus.
Destroying a bus no longer drops queued events; they wait for the next
`ss_process_isr_queue()`.
## Data ownership

`ss_data_t` has a new `owned` field. The library frees a string or custom
payload only when `owned` is set, and only `ss_data_set_string()` and
`ss_data_set_custom()`, which copy, set it. Everything else borrows: direct
field writes, the new `ss_data_set_string_ref()` / `ss_data_set_custom_ref()`,
the `ss_emit_*` helpers, and the data slots receive. In 2.x the setters and
`ss_data_destroy()` freed any string or custom pointer they found, so a
borrowed pointer was freed by mistake.

What to change:

- **Initialize every `ss_data_t`.** Use `ss_data_t d = SS_DATA_INIT;` or
  `ss_data_init(&d)` for stack and embedded objects (`ss_data_create()`
  already returns an initialized one). Code that filled a stack object after
  `memset(&d, 0, sizeof(d))` or `= {0}` keeps working. An object declared
  without an initializer has a garbage `owned`; before passing it to a setter
  or `ss_data_clear()`, initialize it.
- **Clear stack objects that use the copying setters.** In 2.x a stack object
  could only borrow. In 3.0 `ss_data_set_string()` and `ss_data_set_custom()`
  work on stack objects too, and the copy must be released with
  `ss_data_clear()` before the object goes out of scope:

  ```c
  ss_data_t d = SS_DATA_INIT;
  ss_data_set_string(&d, buffer);   /* copies buffer */
  ss_emit(bus, "status", &d);
  ss_data_clear(&d);                /* frees the copy */
  ```

  `ss_data_destroy()` is now `ss_data_clear()` plus freeing an object from
  `ss_data_create()`.
- **Setters on borrowed objects are safe.** `ss_data_set_*()` on an object
  holding a borrowed pointer no longer frees it (2.x documented this as
  misuse). To store a pointer without copying, use
  `ss_data_set_string_ref()` / `ss_data_set_custom_ref()` instead of writing
  the fields by hand; neither allocates.
- **Cleanup functions take `(const void* data, size_t size)`.** They are
  called once, on the library's copy, just before the library frees it, and
  must not free it themselves: the `const` turns a `free(data)` into a
  compiler diagnostic. Change `void my_cleanup(void* p)` to
  `void my_cleanup(const void* p, size_t size)`. Compile with
  `-Werror=incompatible-pointer-types` while migrating (with clang,
  `-Werror=incompatible-function-pointer-types`), because older compilers
  only warn when a 2.x cleanup is passed to `ss_data_set_custom()`.
- **`ss_data_set_custom()` takes `const void* value`**, since it only copies
  it. Callers need no change.
- **Custom data moves into the value union.** `data->custom_data`,
  `data->size` and `data->custom_cleanup` become `data->value.custom.ptr`,
  `data->value.custom.size` and `data->value.custom.cleanup`. Prefer
  `ss_data_get_custom(data, &size)` to read it, and `ss_data_set_custom()` /
  `ss_data_set_custom_ref()` to store it; code that writes the fields by hand
  changes as follows:

  ```c
  /* 2.x */
  d.type = SS_TYPE_CUSTOM;
  d.custom_data = &point;
  d.size = sizeof(point);

  /* 3.0 */
  ss_data_set_custom_ref(&d, &point, sizeof(point));
  ```

  The old names no longer exist, so the compiler finds every use.
- **A hand-written `s_val` (or `value.custom.ptr`) on an `ss_data_create()` object
  is no longer freed** by `ss_data_destroy()` or the next setter: it is
  borrowed. 2.x documented this as misuse; if your code relied on it, store
  the string with `ss_data_set_string()` instead (and free your own buffer),
  or free it yourself.
- **Do not write payload fields by hand on an object that owns its payload**
  (after a copying setter): the next setter, `ss_data_clear()` or
  `ss_data_destroy()` would free your pointer as if it were the copy, and the
  copy would leak. Call `ss_data_clear()` first.
- **Slots always receive borrowed data** (`owned` is 0), also when the
  emitter's object owns its payload, so a slot that copies its `ss_data_t`
  and reuses the copy with a setter no longer frees the emitter's payload.
- **Size:** `ss_data_t` is the same in every configuration: 32 bytes on
  64-bit hosts and 24 on Cortex-M. In 2.x it was 40 and 32 bytes with custom
  data and 16 on both without. `owned` sits in the padding after `type`.
  Builds without custom data, such as `SS_MINIMAL_BUILD`, grow by 16 bytes
  per object on 64-bit hosts and 8 on Cortex-M, which includes each deferred
  and batch entry; see the
  [embedded guide](embedded-guide.md#memory-budget).

## Configuration

- **No configuration macro changes a public type layout or enum value.** In
  2.x `SS_ENABLE_CUSTOM_DATA` changed `ss_data_t` and `ss_data_type_t`, so a
  caller built with different flags than the library corrupted memory. Now
  `SS_TYPE_CUSTOM` and `value.custom` exist in every configuration and
  `SS_DATA_INIT` is the same everywhere. Code that wrapped uses of
  `SS_TYPE_CUSTOM` in `#if SS_ENABLE_CUSTOM_DATA` may drop the guard; the
  custom data functions are still declared only with the flag.
- **Flags must still match** between the library and every caller, because
  they decide which functions are declared. A caller that uses a feature the
  library lacks now fails to compile or link.
- **A library built without custom data refuses to queue it:**
  `ss_emit_deferred()` and `ss_batch_add()` return `SS_ERR_INVALID_TYPE` for
  `SS_TYPE_CUSTOM` data (2.x had no such type there). `ss_emit()` passes it
  to the slots as given.
- **`SS_ABI_TAG` changes** to `sizeof(ss_data_t) * 16 + SS_ABI_REVISION`.
  The `ss_bus_create()` and `ss_bus_create_in()` macros pass it; a program
  that passed a hand-computed `sizeof(ss_data_t)` to `ss_init_abi()` and now
  calls `ss_bus_create_abi()` the same way gets `SS_ERR_CONFIG_MISMATCH`.
- **`SS_STATIC_CONTEXT` becomes `SS_STATIC_BUSES`**, the number of built-in
  buses that `ss_bus_create()` uses in static mode (default 1). Each costs
  `ss_bus_pool_size()` bytes of `.bss`, what the 2.x context cost. 2.x
  `SS_STATIC_CONTEXT=0` (no static context when a pool is supplied) becomes
  `SS_STATIC_BUSES=0` with `ss_bus_create_in()`; `ss_bus_create()` then
  returns `SS_ERR_WOULD_OVERFLOW` instead of allocating, because static mode
  never allocates a bus.

## Removed and changed in passing

Smaller cleanups that 3.0 makes while it breaks the API anyway. Each one is
a compile error until the code is changed.

- **The `SS_TRACE` macro is removed** from `ss_config.h`. It was deprecated
  in 2.x and the library no longer used it. Use `ss_enable_trace(bus,
  stream)` to see the library's trace (with `SS_ENABLE_DEBUG_TRACE=1`), and
  your own logging for your own messages.
- **`SS_ERR_TIMEOUT` and `SS_ERR_ISR_UNSAFE` are removed.** No function ever
  returned them, so a `case` or comparison for them can be deleted. Every
  other code keeps its number (`SS_ERR_WOULD_OVERFLOW` is still 9,
  `SS_ERR_CONFIG_MISMATCH` still 11); 8 and 10 stay unused, and
  `ss_error_string()` returns `"Unknown error"` for them.
- **`ss_get_namespace()` copies the namespace into a buffer you pass** and
  returns an error code. In 2.x it returned a pointer into the library's
  context, which another thread's `ss_set_namespace()` could change while it
  was read; the copy is made under the bus's lock, so that rule is gone. The
  copy is `""` when no namespace is set (2.x returned NULL). A buffer of
  `SS_MAX_SIGNAL_NAME_LENGTH` bytes always fits; a smaller one that cannot
  hold the namespace gives `SS_ERR_BUFFER_TOO_SMALL` and holds `""`.

  2.x:

  ```c
  const char* ns = ss_get_namespace();
  if (ns) printf("namespace %s\n", ns);

  const char* saved = ss_get_namespace();   /* a pointer, not a copy */
  ```

  3.0:

  ```c
  char ns[SS_MAX_SIGNAL_NAME_LENGTH];
  if (ss_get_namespace(bus, ns, sizeof(ns)) == SS_OK && ns[0] != '\0') {
      printf("namespace %s\n", ns);
  }

  char saved[SS_MAX_SIGNAL_NAME_LENGTH];    /* a copy: restore it with */
  ss_get_namespace(bus, saved, sizeof(saved));
  /* ... */
  ss_set_namespace(bus, saved);             /* "" clears */
  ```
