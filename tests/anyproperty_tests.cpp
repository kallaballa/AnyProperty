#include <anyproperty.hpp>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* expr, const char* file, int line) {
    if (!ok) {
        std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
        ++g_failures;
    }
    ++g_checks;
}

void check_near(double a, double b, const char* file, int line) {
    if (std::fabs(a - b) > 1e-9) {
        std::fprintf(stderr, "FAIL %s:%d: %f != %f\n", file, line, a, b);
        ++g_failures;
    }
    ++g_checks;
}

} // namespace

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_NEAR(a, b) check_near(double(a), double(b), __FILE__, __LINE__)

#define CHECK_THROWS_AS(expr, ex)                                                       \
    do {                                                                                \
        bool caught_ = false;                                                           \
        try {                                                                           \
            (void)(expr);                                                               \
        } catch (const ex&) {                                                           \
            caught_ = true;                                                             \
        } catch (...) {                                                                 \
        }                                                                               \
        check(caught_, "expected " #ex ": " #expr, __FILE__, __LINE__);                 \
    } while (0)

enum class Key : int {
    Width = 0,
    Height,
    Name,
    Freq,
    Enabled,
    Pod,
    Count
};

struct Pod {
    int a;
    double b;
};

Pod make_pod(int a, double b) {
    return Pod{a, b};
}

void test_basic_create_get_size() {
    anyproperty::AnyPropertyMap<Key> map;
    CHECK(map.empty());
    CHECK(map.size() == 0);

    map.create<false>(Key::Width, 640, nullptr);
    CHECK(!map.empty());
    CHECK(map.size() == 1);
    CHECK(map.get<int>(Key::Width) == 640);

    map.create<false>(Key::Height, 480, nullptr);
    map.create<false>(Key::Name, std::string("hello"), nullptr);
    CHECK(map.size() == 3);
    CHECK(map.get<int>(Key::Height) == 480);
    CHECK(map.get<std::string>(Key::Name) == "hello");
}

void test_create_read_only() {
    anyproperty::AnyPropertyMap<Key> map;
    map.create<true>(Key::Width, 640, nullptr);
    CHECK(map.get<int>(Key::Width) == 640);
    CHECK_THROWS_AS(map.set(Key::Width, 1), anyproperty::property_error);
    CHECK_THROWS_AS(map.apply<int>(Key::Width, [](int& v) { return ++v; }), anyproperty::property_error);
    CHECK(map.get<int>(Key::Width) == 640);

    CHECK_THROWS_AS(map.create<true>(Key::Height, 1, [](const int&) {}), std::invalid_argument);
}

void test_set_fires_callback_on_change() {
    anyproperty::AnyPropertyMap<Key> map;
    int fires = 0;
    map.create<false>(Key::Width, 10, [&](const int&) { ++fires; });

    map.set(Key::Width, 10);   // unchanged: memcmp equal -> no fire
    CHECK(fires == 0);

    map.set(Key::Width, 20);
    CHECK(fires == 1);
    CHECK(map.get<int>(Key::Width) == 20);

    map.set(Key::Width, 30, false); // fire disabled
    CHECK(fires == 1);
    CHECK(map.get<int>(Key::Width) == 30);
}

void test_callback_receives_new_value() {
    anyproperty::AnyPropertyMap<Key> map;
    int last = -1;
    map.create<false>(Key::Width, 0, [&](const int& v) { last = v; });
    map.set(Key::Width, 99);
    CHECK(last == 99);
}

void test_set_type_mismatch() {
    anyproperty::AnyPropertyMap<Key> map;
    map.create<false>(Key::Width, 640, nullptr);
    CHECK_THROWS_AS(map.set(Key::Width, std::string("x")), anyproperty::property_error);

    bool threw = false;
    try {
        map.set(Key::Width, 1.0);
    } catch (const anyproperty::property_error& e) {
        threw = true;
        std::string what = e.what();
        CHECK(what.find("type mismatch") != std::string::npos);
        CHECK(what.find("Expected: int") != std::string::npos);
        CHECK(what.find("got: double") != std::string::npos);
    }
    CHECK(threw);
}

void test_get_type_mismatch() {
    anyproperty::AnyPropertyMap<Key> map;
    map.create<false>(Key::Width, 640, nullptr);
    CHECK_THROWS_AS(map.get<std::string>(Key::Width), anyproperty::property_error);
}

void test_out_of_range() {
    anyproperty::AnyPropertyMap<Key> map;
    map.create<false>(Key::Width, 5, nullptr);
    map.create<false>(Key::Height, 3, nullptr);

    CHECK_THROWS_AS(map.get<int>(Key::Name), std::out_of_range);
    CHECK_THROWS_AS(map.set(Key::Freq, 3), std::out_of_range);
    CHECK_THROWS_AS(map.get<std::string>(Key::Count), std::out_of_range);
    CHECK(map.ptr<std::string>(Key::Name) == nullptr);
    CHECK(map.ptr<std::string>(Key::Count) == nullptr);
}

void test_non_contiguous_create() {
    anyproperty::AnyPropertyMap<Key> map;
    CHECK_THROWS_AS(map.create<false>(Key::Name, std::string("hi"), nullptr), std::out_of_range);
}

void test_apply() {
    anyproperty::AnyPropertyMap<Key> map;
    map.create<false>(Key::Width, 1, nullptr);

    int ret = map.apply<int>(Key::Width, [](int& v) { return (v *= 10) + 1; });
    CHECK(ret == 11);
    CHECK(map.get<int>(Key::Width) == 10);

    CHECK_THROWS_AS(map.apply<std::string>(Key::Width, [](std::string& v) { return v; }),
                    anyproperty::property_error);
}

void test_ptr() {
    anyproperty::AnyPropertyMap<Key> map;
    map.create<false>(Key::Width, 5, nullptr);

    const int* p = map.ptr<int>(Key::Width);
    CHECK(p != nullptr);
    CHECK(*p == 5);

    const anyproperty::AnyPropertyMap<Key>& cmap = map;
    const int* cp = cmap.ptr<int>(Key::Width);
    CHECK(cp != nullptr);
    CHECK(*cp == 5);

    CHECK(map.ptr<std::string>(Key::Width) == nullptr);
}

void test_various_value_types() {
    anyproperty::AnyPropertyMap<Key> map;
    map.create<false>(Key::Width, int(640), nullptr);
    map.create<false>(Key::Height, 480U, nullptr);
    map.create<false>(Key::Name, std::string("n"), nullptr);
    map.create<false>(Key::Freq, 3.5, nullptr);
    map.create<false>(Key::Enabled, true, nullptr);
    map.create<false>(Key::Pod, make_pod(1, 2.0), nullptr);

    CHECK(map.get<int>(Key::Width) == 640);
    CHECK(map.get<unsigned>(Key::Height) == 480U);
    CHECK(map.get<std::string>(Key::Name) == "n");
    CHECK_NEAR(map.get<double>(Key::Freq), 3.5);
    CHECK(map.get<bool>(Key::Enabled) == true);
    CHECK(map.get<Pod>(Key::Pod).a == 1);
    CHECK_NEAR(map.get<Pod>(Key::Pod).b, 2.0);

    map.set(Key::Pod, make_pod(7, 8.5));
    CHECK(map.get<Pod>(Key::Pod).a == 7);
    CHECK_NEAR(map.get<Pod>(Key::Pod).b, 8.5);
}

void test_copy_semantics() {
    anyproperty::AnyPropertyMap<Key> map;
    map.create<false>(Key::Width, 42, nullptr);

    anyproperty::AnyPropertyMap<Key> copy{map};
    CHECK(copy.size() == map.size());
    CHECK(copy.get<int>(Key::Width) == 42);

    anyproperty::AnyPropertyMap<Key> assigned;
    assigned.create<false>(Key::Width, 1, nullptr);
    assigned = map;
    CHECK(assigned.size() == map.size());
    CHECK(assigned.get<int>(Key::Width) == 42);
}

void test_value_read_flag() {
    anyproperty::AnyPropertyMap<Key> map;
    map.create<true>(Key::Width, 1, nullptr);
    map.create<false>(Key::Height, 2, nullptr);

    CHECK_THROWS_AS(map.set(Key::Width, 9), anyproperty::property_error);
    map.set(Key::Height, 9); // writable property: must not throw
    CHECK(map.get<int>(Key::Width) == 1);
    CHECK(map.get<int>(Key::Height) == 9);
}

void test_threadsafe_basic() {
    anyproperty::ThreadSafeAnyMap<Key> map;
    map.create<false>(Key::Width, 0, nullptr);
    map.create<false>(Key::Height, 480, nullptr);

    map.set(Key::Width, 5);
    CHECK(map.get<int>(Key::Width) == 5);
    map.set(Key::Width, 6, false);
    CHECK(map.get<int>(Key::Width) == 6);

    int ret = map.apply<int>(Key::Width, [](int& v) { return ++v; });
    CHECK(ret == 7);
    CHECK(map.get<int>(Key::Width) == 7);

    map.create<true>(Key::Name, std::string("n"), nullptr);
    CHECK(map.get<std::string>(Key::Name) == "n");
    CHECK_THROWS_AS(map.set(Key::Name, std::string("ro")), anyproperty::property_error);
    CHECK_THROWS_AS(map.get<double>(Key::Width), anyproperty::property_error);
}

void test_threadsafe_create_ordering() {
    anyproperty::ThreadSafeAnyMap<Key> map;
    map.create<false>(Key::Width, 1, nullptr);
    map.create<false>(Key::Height, 2, nullptr);

    CHECK_THROWS_AS(map.create<false>(Key::Width, 3, nullptr), std::out_of_range);
    CHECK_THROWS_AS(map.create<false>(Key::Height, 4, nullptr), std::out_of_range);
    CHECK(map.size() == 2);
    CHECK(map.get<int>(Key::Width) == 1);
    CHECK(map.get<int>(Key::Height) == 2);
}

void test_threadsafe_readers_writers() {
    constexpr int kWriters = 4;
    constexpr int kReaders = 4;
    constexpr int kIterations = 100000;

    anyproperty::ThreadSafeAnyMap<Key> map;
    std::atomic<int> fires{0};
    map.create<false>(Key::Width, 0, [&](const int& v) { (void)v; ++fires; });

    std::vector<std::thread> threads;
    for (int i = 0; i < kWriters; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < kIterations; ++j)
                map.set(Key::Width, i);
        });
    }
    for (int i = 0; i < kReaders; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < kIterations; ++j) {
                // Snapshot the value while the lock is held (get() returns a
                // reference that outlives the lock, so it must not be read
                // across threads).
                int v = map.apply<int>(Key::Width, [](int& v) { return v; });
                if (v < 0 || v >= kWriters)
                    std::fprintf(stderr, "bad value %d\n", v);
            }
        });
    }
    for (auto& t : threads)
        t.join();

    int final = map.get<int>(Key::Width);
    CHECK(final >= 0 && final < kWriters);
    CHECK(fires > 0);
}

void test_threadsafe_concurrent_callback_count() {
    anyproperty::ThreadSafeAnyMap<Key> map;
    std::atomic<long long> total{0};
    map.create<false>(Key::Width, 0, [&](const int& v) { total.fetch_add(v); });

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < 10000; ++j)
                map.set(Key::Width, i);
        });
    }
    for (auto& t : threads)
        t.join();

    CHECK(total.load() >= 0);
    int final = map.get<int>(Key::Width);
    CHECK(final >= 0 && final < 8);
}

void test_threadsafe_ptr_const() {
    anyproperty::ThreadSafeAnyMap<Key> map;
    map.create<false>(Key::Width, 1, nullptr);

    const anyproperty::ThreadSafeAnyMap<Key>& cmap = map;
    const int* p = cmap.ptr<int>(Key::Width);
    CHECK(p != nullptr && *p == 1);
    CHECK(cmap.ptr<std::string>(Key::Width) == nullptr);
}

int main() {
    test_basic_create_get_size();
    test_create_read_only();
    test_set_fires_callback_on_change();
    test_callback_receives_new_value();
    test_set_type_mismatch();
    test_get_type_mismatch();
    test_out_of_range();
    test_non_contiguous_create();
    test_apply();
    test_ptr();
    test_various_value_types();
    test_copy_semantics();
    test_value_read_flag();
    test_threadsafe_basic();
    test_threadsafe_create_ordering();
    test_threadsafe_readers_writers();
    test_threadsafe_concurrent_callback_count();
    test_threadsafe_ptr_const();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d / %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("all %d checks passed\n", g_checks);
    return 0;
}