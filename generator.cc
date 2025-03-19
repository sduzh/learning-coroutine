#include <coroutine>
#include <iostream>
#include <type_traits>
#include <source_location>

template <typename T>
class Generator {
public:
    class promise_type;
    class Iter;
    using Handle = std::coroutine_handle<promise_type>;

    explicit Generator(Handle h) : handle_{h} {}

    ~Generator() { if (handle_) handle_.destroy(); }

    auto begin() -> Iter;

    auto end() -> std::default_sentinel_t;
     
private:
    Handle handle_;
};

template <typename T>
class Generator<T>::promise_type {
    using Handle = Generator::Handle;
public:
    Generator get_return_object() { return Generator{Handle::from_promise(*this)}; }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(T obj) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        value_ = obj;
        return {};
    }
    void unhandled_exception() {}

    auto value() const -> T { return value_; }

private:
    T value_{};
};

template <typename T>
class Generator<T>::Iter {
public:
    explicit Iter(Handle handle) : handle_(handle) {}

    auto operator*() const {
        return handle_.promise().value();
    }

    auto operator++() {
        handle_.resume();
    }

    auto operator==(std::default_sentinel_t /*sentinel*/) {
        return !handle_ || handle_.done();
    }

private:
    Handle handle_;
};

template <typename T>
auto Generator<T>::begin() -> Iter {
    return Iter{handle_};
}

template <typename T>
auto Generator<T>::end() -> std::default_sentinel_t {
    return std::default_sentinel;
}

template <typename T>
Generator<T> xrange(T first, T last, T step=1) {
    if (step == 0) throw std::invalid_argument("step cannot be zero");
    if (first < last && step < 0) throw std::invalid_argument("invalid step");
    if (first > last && step > 0) throw std::invalid_argument("invalid step");
    if (step > 0) {
        for (auto cur = first; cur < last; cur += step) {
            co_yield cur;
        }
    } else {
        for (auto cur = first; cur > last; cur += step) {
            co_yield cur;
        }
    }
}

int main() {
    std::cout << "xrange(1, 1)\n";
    for (auto n : xrange(1, 1)) {
    }
    std::cout << "xrange(0, 10)\n";
    for (auto n : xrange(0, 10)) {
        std::cout << n << '\n';
    }
    std::cout << "xrange(1, 2)\n";
    for (auto n : xrange(1, 2)) {
        std::cout << n << '\n';
    }
    std::cout << "xrange(2, 1)\n";
    for (auto n : xrange(2, 1)) {
        std::cout << n << '\n';
    }
    std::cout << "iter\n";
    auto g = xrange(1, 5);
    for (auto iter = g.begin(); iter != g.end(); ++iter) {
        std::cout << *iter << ", " << *iter << '\n';
    }
    std::cout << "xrange(10, 1, -2)\n";
    for (auto n : xrange(10, 1, -2)) {
        std::cout << n << '\n';
    }
    return 0;
}
