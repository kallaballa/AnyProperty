/*
 * anyproperty.hpp
 *
 * A optionally thread-safe std::any propery map.
 *
 * Requires C++17. Drop the header into your include path and:
 *
 *     #include "anyproperty.hpp"
 *     using namespace anyproperty;
 *
 *     enum class Prop { Width, Height };
 *     ThreadSafeAnyMap<Prop> map;
 *     map.create<false>(Prop::Width, int(640), [](const int&){ on_change(); });
 *     map.set(Prop::Width, 800);
 *     int w = map.get<int>(Prop::Width);
 *
 * Properties are created contiguously and identified by an enum key. Keys must
 * be declared read-only (create<true>) or writable (create<false>) up front;
 * writes to read-only keys, out-of-range keys and value of the wrong type all
 * throw. Errors are reported as std::out_of_range for bounds/ordering problems
 * and as anyproperty::property_error for read-only/type-mismatch problems.
 *
 * Note: set() detects changes by comparing old and new value with memcmp, so
 * value types should be trivially copyable/POD. ThreadSafeAnyMap::get() returns
 * a live reference guarded only for the duration of the call; treat it as a
 * read channel, not a lock.
 */

#ifndef ANYPROPERTY_HPP_
#define ANYPROPERTY_HPP_

#include <any>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#ifdef __GNUG__
#include <cxxabi.h>
#endif

namespace anyproperty {

namespace detail {

#ifdef __GNUG__

inline std::string demangle(const char* name) {
    int status = -4; // some arbitrary value to eliminate the compiler warning
    std::unique_ptr<char, void(*)(void*)> res{
        abi::__cxa_demangle(name, nullptr, nullptr, &status),
        std::free
    };

    return (status == 0) ? res.get() : name;
}

#else

inline std::string demangle(const char* name) {
    return name;
}

#endif

template<typename T>
std::string type_name() {
    return demangle(typeid(T).name());
}

} // namespace detail

class property_error : public std::runtime_error {
public:
    explicit property_error(const std::string& message) : std::runtime_error(message) {}
};

class Value : public std::any {
public:
    std::function<void(Value& val)> callback_;
    bool read_;

    Value(const bool& read = false) : std::any(), read_(read) {
    }

    Value(const Value& rhs) = default;
    Value(Value&& rhs) = default;

    using std::any::operator=;

    Value& operator=(const Value& rhs) = default;
    Value& operator=(Value&& rhs) = default;

    const std::type_info& type() const noexcept {
        return std::any::type();
    }
};

template<typename K>
class AnyPropertyMap {
private:
    std::vector<Value> properties_;

    size_t index(K key) const {
        return static_cast<size_t>(key);
    }

    template<typename V>
    constexpr void check_value_type() const {
        using U = std::remove_cv_t<std::remove_reference_t<V>>;

        static_assert(std::is_constructible<V, const U&>::value, "Illegal value type: Can't construct const V&");
        static_assert(std::is_constructible<V, U&>::value, "Illegal value type: Can't construct V&");
        static_assert(std::is_constructible<V, U>::value, "Illegal value type: Can't construct V from itself");
        static_assert(!std::is_void<V>::value, "Illegal value type: V may not be void");
    }

    void check_write(K key) {
        if (index(key) >= properties_.size())
            throw std::out_of_range("AnyPropertyMap: key out of range");
        if (properties_[index(key)].read_)
            throw property_error("AnyPropertyMap: trying to set a read only property");
    }

    template<bool Tread, typename V>
    void create_impl(K key, const V& value, std::function<void(const V&)> cb) {
        check_value_type<V>();
        if (index(key) != properties_.size())
            throw std::out_of_range("AnyPropertyMap::create: properties must be created contiguously in increasing key order");
        if constexpr (Tread) {
            if (cb)
                throw std::invalid_argument("AnyPropertyMap::create: read only properties accept no callback");
            Value val(Tread);
            val.callback_ = [](const Value& v){ (void)v; };
            val = value;
            properties_.emplace_back(val);
        } else {
            if (!cb)
                cb = [](const V&){};

            properties_.emplace_back(Value(Tread));
            properties_[index(key)] = value;
            Value& val = properties_[index(key)];
            val.callback_ = [cb](const Value& v){ cb(std::any_cast<V>(v)); };
        }
    }

    static_assert(std::is_enum<K>::value, "AnyPropertyMap: K must be an enum");

public:
    AnyPropertyMap() {
        properties_.reserve(100);
    }

    template<bool Tread, typename V>
    void create(K key, const V& value) {
        create_impl<Tread>(key, value, std::function<void(const V&)>());
    }

    template<bool Tread, typename V, typename F>
    void create(K key, const V& value, F&& cb) {
        create_impl<Tread>(key, value, std::function<void(const V&)>(std::forward<F>(cb)));
    }

    template<typename V>
    void set(K key, const V& value, bool fire = true) {
        check_value_type<V>();
        check_write(key);
        V* p = std::any_cast<V>(&properties_[index(key)]);

        if (!p)
            throw property_error("AnyPropertyMap::set: type mismatch for key " + std::to_string(int(key)) + ". Expected: " + detail::demangle(properties_[index(key)].type().name()) + ", got: " + detail::type_name<V>() + ".");
        V oldVal = *p;
        *p = value;

        if (fire && memcmp(&oldVal, p, sizeof(V)) != 0)
            properties_[index(key)].callback_(properties_[index(key)]);
    }

    template<typename V>
    const V& get(K key) const {
        check_value_type<V>();
        if (index(key) >= properties_.size())
            throw std::out_of_range("AnyPropertyMap::get: key out of range");
        const V* p = std::any_cast<V>(&properties_[index(key)]);
        if (!p)
            throw property_error("AnyPropertyMap::get: type mismatch for key " + std::to_string(int(key)) + ". Expected: " + detail::demangle(properties_[index(key)].type().name()) + ", got: " + detail::type_name<V>() + ".");
        return *p;
    }

    template<typename V>
    V apply(K key, std::function<V(V& val)> func) {
        check_value_type<V>();
        check_write(key);
        V* p = std::any_cast<V>(&properties_[index(key)]);
        if (!p)
            throw property_error("AnyPropertyMap::apply: type mismatch for key " + std::to_string(int(key)) + ". Expected: " + detail::demangle(properties_[index(key)].type().name()) + ", got: " + detail::type_name<V>() + ".");
        V ret = func(*p);
        return ret;
    }

    template<typename V>
    const V* ptr(K key) const {
        check_value_type<V>();
        if (index(key) >= properties_.size())
            return nullptr;
        return std::any_cast<V>(&properties_[index(key)]);
    }

    size_t size() const {
        return properties_.size();
    }

    bool empty() const {
        return properties_.empty();
    }
};

template<typename K>
class ThreadSafeAnyMap : public AnyPropertyMap<K> {
private:
    mutable std::mutex mtx_;
    using parent_t = AnyPropertyMap<K>;
public:
    template<bool Tread, typename V>
    void create(K key, const V& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        parent_t::template create<Tread>(key, value);
    }

    template<bool Tread, typename V, typename F>
    void create(K key, const V& value, F&& cb) {
        std::lock_guard<std::mutex> lock(mtx_);
        parent_t::template create<Tread>(key, value, std::forward<F>(cb));
    }

    template<typename V>
    void set(K key, const V& value, bool fire = true) {
        std::lock_guard<std::mutex> lock(mtx_);
        parent_t::set(key, value, fire);
    }

    template<typename V>
    const V& get(K key) {
        std::lock_guard<std::mutex> lock(mtx_);
        return parent_t::template get<V>(key);
    }

    template<typename V>
    V apply(K key, std::function<V(V& val)> func) {
        std::lock_guard<std::mutex> lock(mtx_);
        return parent_t::template apply<V>(key, func);
    }

    template<typename V>
    const V* ptr(K key) const {
        return parent_t::template ptr<V>(key);
    }
};

} // namespace anyproperty

#endif // ANYPROPERTY_HPP_
