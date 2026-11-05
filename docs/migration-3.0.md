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

Not landed yet: `ss_init()` and `ss_cleanup()` become `ss_bus_create()` and
`ss_bus_destroy()`.

## The bus argument

Not landed yet: every function that uses library state takes the bus as its
first argument.

## Thread safety

Not landed yet: `ss_set_thread_safe(1)` becomes the `SS_BUS_THREAD_SAFE` flag
of `ss_bus_create()`.

## Error handler

Not landed yet: the handler receives the bus and a user pointer.

## Batches

Not landed yet: `ss_batch_create(bus)`.

## ISR queue

Not landed yet: `ss_emit_from_isr()` and `ss_process_isr_queue()` take the bus.

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
  ss_emit("status", &d);
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
  64-bit hosts and 24 on Cortex-M (2.x: 40 and 32 with custom data, 16 and 16
  without). `owned` sits in the padding after `type`. Builds without custom
  data, such as `SS_MINIMAL_BUILD`, grow by 8 bytes per object on Cortex-M,
  which includes each deferred and batch entry; see the
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
  Keep passing the macro; a program that passed a hand-computed
  `sizeof(ss_data_t)` to `ss_init_abi()` now gets `SS_ERR_CONFIG_MISMATCH`.

Not landed yet: `SS_STATIC_CONTEXT` becomes `SS_STATIC_BUSES`.
