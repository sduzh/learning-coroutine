#include <coroutine>
#include <iostream>

struct Promise;

struct Coroutine : public std::coroutine_handle<Promise> {
    using promise_type = ::Promise;
};

struct Promise {
    using Handle = std::coroutine_handle<Promise>;

    Coroutine get_return_object() { return (Coroutine)Handle::from_promise(*this); }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
};

int main() {
    auto co = []() -> Coroutine {
        for (int i = 0; i < 10; i++) {
          std::cout << "i=" << i << '\n';
          // co_await std::suspend_never{};
          co_await std::suspend_always{};
        }
    }();
    while (!co.done()) {
        std::cout << "resume\n";
        co.resume();
    }
    co.destroy();
    return 0;
}
