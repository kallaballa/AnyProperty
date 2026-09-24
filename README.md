# AnyProperty

A small, **single-header**, OpenCV-free C++17 library for type-safe, enum-keyed,
thread-safe property maps — ported out of the Plan-V4D module
([`opencv2/plan/threadsafeanymap.hpp`](https://github.com/kallaballa/Plan-V4D/blob/master/modules/plan/include/opencv2/plan/threadsafeanymap.hpp))
so it can be used as a standalone dependency.

Drop in one header, get a typed `std::any`-backed map whose keys are enums,
whose values carry change callbacks, and whose writable/read-only role is fixed
at creation time.

- **Type-safe**: every access is templated on the value type; mismatches raise
  a descriptive exception instead of silently corrupting data.
- **Compile-time key validation**: keys must be enums, values must be
  copy-constructible.
- **Optionally thread-safe**: `ThreadSafeAnyMap` serializes every operation with
  a mutex.
- **No dependencies**: standard library only, `#include <anyproperty.hpp>`.

---

## Requirements

- A C++17 (or newer) compiler. The library uses `std::any`, `std::function`,
  `std::mutex` and `if constexpr`.
- Nothing else. No OpenCV, no external headers beyond the C++ standard library
  (`<cxxabi.h>` is used only when available, for demangling type names in error
  messages).

---

## Installation

The library is a single header. Either:

1. **Copy-and-drop**: put `anyproperty.hpp` anywhere in your include path.
2. **CMake** (as a subdirectory): the header is exposed as a proper target.

```cmake
add_subdirectory(AnyProperty)

# in your target:
target_link_libraries(my_app PRIVATE AnyProperty::AnyProperty)
```

The CMake target sets `cxx_std_17` and propagates nothing else.

---

## Quick start

```cpp
#include <anyproperty.hpp>
using namespace anyproperty;

enum class Prop {
    Width = 0,
    Height,
    File,          // read-only
};

int main() {
    ThreadSafeAnyMap<Prop> map;

    // Properties must be created as a contiguous run: key 0, then 1, then 2 ...
    map.create<false>(Prop::Width,  640);                       // writable
    map.create<false>(Prop::Height, 480, [](const int&){ /* on change */ });
    map.create<true>(Prop::File, std::string("frame.raw"));     // read-only

    map.set(Prop::Width, 800);
    int w = map.get<int>(Prop::Width);                 // 800

    map.apply<int>(Prop::Height, [](int& h){ return h / 2; });  // in-place update

    return 0;
}
```

---

## API

### Key requirement

`K` (the key type) **must be an enum** — enforced by a `static_assert`. Every
created property occupies the array slot `index(key)`, so create calls must be
issued as an ascending, gap-free run starting at the enum value `0`:

```cpp
map.create<false>(Prop::Width, 640);   // slot 0
map.create<false>(Prop::Height, 480);  // slot 1
```

Creating a key out of order (or twice) throws `std::out_of_range`.

### `AnyPropertyMap<K>`

The non-thread-safe map. All methods below are mutex-free.

| Method | Description |
| --- | --- |
| `create<Tread>(K key, const V& value)` | Create a property. `Tread=true` makes it read-only. |
| `create<Tread>(K key, const V& value, F&& callback)` | Create with a change callback (writable only). |
| `set(K key, const V& value, bool fire = true)` | Update the value; fires the callback if the value actually changed and `fire` is true. |
| `get<V>(K key) const` | Returns `const V&` (throws on wrong type). |
| `apply<V>(K key, std::function<V(V&)>)` | Atomically read-modify-write; returns the function's result. |
| `ptr<V>(K key) const` | `const V*`, or `nullptr` if the key is missing or the type mismatches. |
| `size() const` / `empty() const` | Number of created properties / whether the map is empty. |

### `ThreadSafeAnyMap<K> : AnyPropertyMap<K>`

Same interface; `create`, `set`, `get`, `apply` each take the internal mutex for
the duration of the call. `size`/`empty`/`ptr` are **not** locked.

### `Value`

The underlying storage class (derives from `std::any`). Exposed publicly, but
you normally never touch it. It carries the stored value, the change callback
and a `read_` flag.

### `property_error`

`std::runtime_error` subclass thrown for read-only writes and type mismatches.
Its `what()` includes the key, the stored type and the requested type, e.g.:

```
AnyPropertyMap::set: type mismatch for key 1. Expected: int, got: double.
```

`std::out_of_range` is thrown for out-of-order creates and out-of-range keys.

---

## Read-only properties

`create<true>(...)` marks a property immutable: `set` and `apply` throw
`property_error`. Read-only properties must **not** have a callback; passing one
is rejected at runtime with `std::invalid_argument`, because a callback is
meaningless if the value can never change.

```cpp
map.create<true>(Prop::File, std::string("frame.raw"));
map.set(Prop::File, std::string("other.raw"));   // throws property_error
```

---

## Change detection & callbacks

`set` fires the callback only when the new value actually differs from the old
one. Comparison is done with `memcmp(&old, &new, sizeof(V))`:

- Store **trivially comparable** value types (integers, floats, pointers, plain
  old data structs).
- `memcmp` compares raw bytes, so a "logically equal, byte-different" object
  (e.g. a struct with padding) will fire when a byte-for-byte identical copy of
  a struct that happens to differ in padding is stored — usually harmless.

The callback receives a copy of the new value (the stored value passed through
`std::any_cast<V>`):

```cpp
map.create<false>(Prop::Width, 1, [](const int& v){ print(v); });
map.set(Prop::Width, 1);   // no callback (unchanged)
map.set(Prop::Width, 2);   // callback fires with v == 2
map.set(Prop::Width, 3, /*fire=*/false);   // no callback, value still updates
```

Callbacks may be omitted entirely:

```cpp
map.create<false>(Prop::Width, 640, nullptr);
```

---

## Thread safety

`ThreadSafeAnyMap` guards every operation. Safe to call from multiple threads:

- `create` — the whole create (including the contiguity check) is serialized.
- `set` / `apply` — value update *and* the user callback run inside the lock.
- `get` — the reference is obtained under the lock.

Three caveats you must know:

1. **`get()` returns a live `const V&`.** The lock is released when `get`
   returns, so the reference must not be read/written from another thread while
   someone else may `set`. To read a value safely cross-thread, snapshot it
   under the lock with `apply`:

   ```cpp
   int snapshot = map.apply<int>(Prop::Width, [](int& v){ return v; });
   ```

2. **`ptr()` is explicitly unsynchronized.** It hands you a raw pointer, which
   is only valid if no `set`/`create` runs concurrently. Single-threaded use
   only.

3. **`size()` / `empty()` are not locked.** No `reserve`/realloc contention
   occurs after the constructor's `reserve(100)` unless you create more than 100
   properties, but for strict correctness call them only during initialization
   or after all threads have joined.

For the non-thread-safe `AnyPropertyMap`, use it from one thread at a time.

---

## Error handling

All errors are exceptions:

| Condition | Throws |
| --- | --- |
| Key created out of order / duplicate | `std::out_of_range` |
| Accessing a key that was never created | `std::out_of_range` |
| Writing to a read-only property | `property_error` |
| Value of the wrong type | `property_error` |
| Callback passed to `create<true>` | `std::invalid_argument` |

`property_error` derives from `std::runtime_error`, so one `catch` handles all
property problems if you want a single integration point:

```cpp
try {
    map.set(Prop::Width, 640);
} catch (const anyproperty::property_error& e) {
    std::cerr << e.what() << '\n';
} catch (const std::out_of_range& e) {
    std::cerr << e.what() << '\n';
}
```

---

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The test suite (`tests/anyproperty_tests.cpp`) is a dependency-free harness:
73 checks covering behavior, error paths and concurrency (multi-writer/
multi-reader stress, parallel callback accounting). It builds with GCC and Clang
under C++17/C++20 and runs clean under AddressSanitizer,
UndefinedBehaviorSanitizer and ThreadSanitizer:

```bash
g++ -std=c++17 -fsanitize=thread tests/anyproperty_tests.cpp -o t -pthread && ./t
```

---

## Origin

Ported and de-OpenCV-ified from the Plan-V4D contrib module,
`modules/plan/include/opencv2/plan/threadsafeanymap.hpp` (Apache-2.0 codebase).
Changes beyond stripping the OpenCV dependency:

- GCC/Clang/MSVC portability fixes (the non-GNU type demangler no longer
  contains a syntax error).
- Fixed `ptr()` const-correctness (the original mixed `const V*`/`V*` in a way
  that did not compile).
- Scoped enums now work (keys are explicitly cast to `size_t`).
- `create`'s callback is a template parameter, so lambdas and `nullptr` can be
  passed without wrapping them in `std::function`.
- Bounds checks added to `get`/`ptr`; `get` throws `std::out_of_range` instead
  of reading out of bounds.
- The enclosing `cv::plan` namespace became `anyproperty`; OpenCV's `CV_Error`/
  `CV_Assert` macros became real exceptions (`property_error`,
  `std::out_of_range`, `std::invalid_argument`).