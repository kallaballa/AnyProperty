# AnyProperty

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

A single-header, type-erased property map for C++17 — with an optional thread-safe variant.

`AnyProperty` gives you a `std::any`-backed container keyed by an `enum`, where each key is
declared once (read-only or writable) and can fire a callback when its value actually changes.

- **Header-only.** Copy `anyproperty.hpp` into your include path, or use CMake.
- **No dependencies** beyond the standard library.
- **Typed access with real error messages** — type mismatches report the expected and actual type.
- **Change notification** via callbacks, fired only when the value differs.
- **Optional thread safety** — `ThreadSafeAnyMap` guards every operation with a mutex.
- **C++17**, ~270 lines.

## Table of Contents

- [Motivation](#motivation)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Concepts](#concepts)
- [API](#api)
- [Error handling](#error-handling)
- [Thread safety](#thread-safety)
- [Caveats](#caveats)
- [Building the tests](#building-the-tests)
- [Design notes](#design-notes)
- [License](#license)

## Motivation

Property maps are a common way to pass a bag of tunables around — video parameters, render
settings, config blobs. Rolling your own usually means a `std::map<Enum, Any>` plus a lot of
ad-hoc casting. This header does the same thing with:

- **Declaration-time mutability.** A key is created as read-only (`create<true>`) or writable
  (`create<false>`). Writing a read-only key is a `property_error`, not a silent no-op.
- **Change callbacks.** Register a callback per key; it runs only when the stored value actually
  changes, so you can wire properties straight into invalidation or redraw logic.
- **Explicit failures.** Out-of-range keys throw `std::out_of_range`, type mismatches throw
  `anyproperty::property_error` with a demangled type name in the message.

## Installation

### Header-only (copy it)

```bash
cp anyproperty.hpp /path/to/your/include/
```

```cpp
#include "anyproperty.hpp"
```

### CMake

The project exports an interface target:

```cmake
add_subdirectory(AnyProperty)
target_link_libraries(your_target PRIVATE AnyProperty::AnyProperty)
```

`AnyProperty::AnyProperty` carries the include directory and the `cxx_std_17` requirement.
If you `add_subdirectory()` without a name, turn tests off:

```cmake
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(AnyProperty)
```

## Quick start

```cpp
#include <anyproperty.hpp>
#include <iostream>

enum class Prop : int { Width, Height, Name };

int main() {
    using namespace anyproperty;

    ThreadSafeAnyMap<Prop> map;

    // Writable key with a change callback.
    map.create<false>(Prop::Width, 640, [](const int& w) {
        std::cout << "width -> " << w << '\n';
    });

    // Read-only key: no callback allowed, and writes are rejected.
    map.create<true>(Prop::Height, 480);
    map.create<true>(Prop::Name, std::string("main"));

    map.set(Prop::Width, 800);                 // callback fires, prints "width -> 800"
    map.set(Prop::Width, 800);                 // unchanged, callback does NOT fire
    map.set(Prop::Width, 1024, /*fire=*/false); // value updates silently

    std::cout << map.get<int>(Prop::Width) << '\n';

    try {
        map.set(Prop::Height, 720);
    } catch (const property_error& e) {
        std::cout << "rejected: " << e.what() << '\n';
    }
}
```

Keys are **created contiguously in increasing enum order** — `Width`, then `Height`, then `Name`.
Skipping a key throws `std::out_of_range`.

## Concepts

**Key.** Any `enum` type, typically a scoped `enum class : int`. The key's integer value is its
index in the backing `std::vector`, which is what makes creation-order enforcement possible and
access a constant-time lookup. `K` must be an enum; this is `static_assert`ed.

**Value.** Any type `std::any` can hold. The value type is deduced at `create` time and is
remembered as the key's `std::type_info`. `get<V>`, `set<V>`, `apply<V>`, and `ptr<V>` must all
name that same type.

**Read-only vs writable.** Chosen as a template argument at creation:

| Declaration | Writable | Callback |
| --- | --- | --- |
| `create<false>(key, value)` | yes | none |
| `create<false>(key, value, cb)` | yes | `cb`, fired on change |
| `create<true>(key, value)` | no | none (a callback is rejected) |
| `create<true>(key, value, cb)` | no | `std::invalid_argument` |

**Change detection.** `set` copies the old value, writes the new one, and compares the two with
`memcmp`. If they differ (and `fire` is `true`), the callback runs with the new value.

## API

### `AnyPropertyMap<K>`

| Member | Description |
| --- | --- |
| `template<bool Tread, typename V> void create(K key, const V& value)` | Declare a property. `Tread` selects read-only (`true`) or writable (`false`). |
| `template<bool Tread, typename V, typename F> void create(K key, const V& value, F&& cb)` | As above with a change callback. Read-only keys reject a callback. `nullptr` is accepted for "no callback". |
| `template<typename V> void set(K key, const V& value, bool fire = true)` | Write a writable key. Fires the callback only if the value changed and `fire` is `true`. |
| `template<typename V> const V& get(K key) const` | Read a key. **Returns a reference** — see [Caveats](#caveats). |
| `template<typename V> V apply(K key, std::function<V(V&)> func)` | Mutate the value in place under the map's own guard and return a result. Rejects read-only keys. |
| `template<typename V> const V* ptr(K key) const` | Pointer access. Returns `nullptr` instead of throwing for an out-of-range key or a type mismatch. |
| `size_t size() const` | Number of properties created. |
| `bool empty() const` | Whether the map is empty. |

`get<V>` on a writable key is the fast path. Use `apply<V>` when you need read-modify-write
atomicity, e.g. `map.apply<int>(Prop::Width, [](int& w) { return ++w; })`.

### `ThreadSafeAnyMap<K>`

Derives from `AnyPropertyMap<K>` and adds a `std::mutex`, held for the duration of every
`create`, `set`, `get`, and `apply`. Same API and same semantics; see
[Thread safety](#thread-safety) for the caveats.

### `anyproperty::property_error`

```cpp
class property_error : public std::runtime_error { ... };
```

Thrown for read-only violations and type mismatches.

### `anyproperty::Value`

A `std::any` plus a `callback_` and a `read_` flag. It is the element type stored in the
container; you normally interact with it only through the map.

## Error handling

| Situation | Exception |
| --- | --- |
| `get`/`set` on a key that was never created | `std::out_of_range` |
| `create` skipping a key, or re-creating an existing one | `std::out_of_range` |
| `create<true>` with a callback | `std::invalid_argument` |
| `set`/`apply` on a read-only key | `anyproperty::property_error` |
| `get`/`set`/`apply` with the wrong value type | `anyproperty::property_error` |
| Illegal `V` for `create` (e.g. `void`) | `static_assert` at compile time |

Type mismatch messages name both types:

```
AnyPropertyMap::set: type mismatch for key 0. Expected: int, got: double.
```

On GCC/Clang these are demangled via `abi::__cxa_demangle`; elsewhere the raw `typeid` name is used.

Note that `std::out_of_range` is caught by a `catch (const std::exception&)` handler, and
`property_error` derives from `std::runtime_error`. If you want only the property-specific
failures, catch `property_error` first.

## Thread safety

`AnyPropertyMap` is **not** synchronized. `ThreadSafeAnyMap` serializes all public access
through a single mutex, so concurrent `set`/`get`/`apply` calls are safe, and a callback runs
while the lock is held.

Read references and pointers are the exception:

```cpp
const int& w = map.get<int>(Prop::Width);  // lock released when get() returns
// ... another thread may set(Prop::Width, ...) here; `w` is now racing
```

`get` is a **read channel, not a lock**. For a value that is guaranteed consistent across
threads, copy it out immediately or use `apply`:

```cpp
// Snapshot the value while the lock is still held.
int w = map.apply<int>(Prop::Width, [](int& v) { return v; });
```

`ptr` is likewise unsynchronized in the thread-safe variant — it takes no lock at all. Use it for
fast local access or identity checks, not across threads.

Because a callback runs with the mutex held, a callback that writes to the same map will
deadlock. Keep callbacks short and side-effect free; push work onto a queue if you need more.

## Caveats

- **Value types should be trivially copyable.** Change detection uses `memcmp`, so padding bytes
  and self-referential pointers can produce spurious "changed" results for non-POD types. The
  container still stores them correctly, and `std::any` supports non-trivial types.
- **`get` returns a reference into the container.** The reference is valid until the map is
  destroyed or copied, but concurrent mutation invalidates the *value*, not the reference. Copy
  what you need.
- **The container reserves capacity for 100 properties** in its constructor.
- **`ThreadSafeAnyMap` is not lock-free** — one mutex serializes all keys, so a slow callback
  blocks unrelated readers and writers.
- **`create` after construction is only additive.** There is no `destroy`/`erase`; the map grows
  for its lifetime.

## Building the tests

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The suite covers creation and ordering, read-only enforcement, change detection and callback
firing, type-mismatch messages, out-of-range access, `apply`, `ptr`, copy semantics, mixed value
types, and multithreaded readers/writers.

Or build the test directly, without CMake:

```bash
g++ -std=c++17 -Wall -Wextra -pthread -I. tests/anyproperty_tests.cpp -o anyproperty_tests && ./anyproperty_tests
```

## Design notes

The whole implementation is one header and roughly 270 lines. The pieces:

- `AnyPropertyMap<K>` — a `std::vector<Value>` where the key's integer value indexes the vector.
  Lookup is arithmetic, not a hash lookup.
- `Value` — a `std::any` with a `std::function<void(Value&)> callback_` and a `bool read_` flag.
  Since it owns a `std::any`, moving the vector during a reallocation is safe.
- `create_impl` — the shared implementation behind both `create` overloads. The `if constexpr`
  on `Tread` drops the callback machinery for read-only properties entirely.
- `detail::type_name<T>()` / `detail::demangle()` — produce the type names used in error
  messages; demangling is GCC/Clang-only.
- `ThreadSafeAnyMap<K>` — a thin wrapper that holds a `mutable std::mutex` around each call into
  the parent.

Because `K` is the only template parameter, a map costs one vector plus one mutex. There is no
vtable, no allocation per lookup, and no registration step beyond the `create` calls.

## License

Licensed under the [Apache License, Version 2.0](LICENSE).

```text
Copyright (c) <year> <amir@viel-zu.org>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

The full license text is in [`LICENSE`](LICENSE). Note that the Apache 2.0 license requires
preserving this notice in redistributions; the license file itself has no copyright line filled
in yet. Add yours in a `NOTICE` file or in the header of `anyproperty.hpp` before publishing.
