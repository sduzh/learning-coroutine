#include <coroutine>
#include <iostream>
#include <source_location>

struct promise;

struct Task : public std::coroutine_handle<promise> {
    using promise_type = ::promise;
};

struct promise {
    Task get_return_object() {
        std::cout << std::source_location::current().function_name() << '\n'; 
        return (Task)Task::from_promise(*this);
    }
    std::suspend_always initial_suspend() noexcept {
        std::cout << std::source_location::current().function_name() << '\n'; 
        return {};
    }
    std::suspend_never final_suspend() noexcept {
        std::cout << std::source_location::current().function_name() << '\n'; 
        return {};
    }
    void return_void() {
        std::cout << std::source_location::current().function_name() << '\n'; 
    }
    void unhandled_exception() {
    }
};

int main() {
    auto f = [](int i) -> Task {
        std::cout << i << '\n';
        co_return;
    };
    auto t = f(0);
    std::cout << "before resume\n";
    t.resume();
    std::cout << "before destroy\n";
    t.destroy();
    return 0;
}
