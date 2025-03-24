/**
 * g++ -std=c++20 -fcoroutines -fsanitize=address -ggdb -g3 ./stack-overflow.cpp
 * ./a.out
 *
 * Reference: https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer#the-stack-overflow-problem
 */
#include <coroutine>

class task {
public:
  ~task() { coro_.destroy(); }

  struct promise_type;

  struct final_awaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
      if (auto prev = h.promise().continuation; prev) {
        prev.resume();
      }
    }
    void await_resume() noexcept {}
  };

  struct promise_type {
    auto get_return_object() noexcept { return task(std::coroutine_handle<promise_type>::from_promise(*this)); }
    auto initial_suspend() noexcept { return std::suspend_always{}; }
    auto final_suspend() noexcept { return final_awaiter{}; }
    void return_void() noexcept {}
    void unhandled_exception() { throw; }

    std::coroutine_handle<> continuation;
  };

  struct awaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) {
      coro.promise().continuation = h;
      coro.resume();
    }
    void await_resume() noexcept {}

    std::coroutine_handle<promise_type> coro;
  };

  auto operator co_await() { return awaiter{coro_}; }

  auto operator()() { coro_(); }

private:
  explicit task(std::coroutine_handle<promise_type> coro) noexcept : coro_(coro) {}

  std::coroutine_handle<promise_type> coro_;
};

task complete_synchronously() {
  co_return;
}

task loop_synchronously(int count) {
  for (int i = 0; i < count; ++i) {
    co_await complete_synchronously();
  }
}

int main() {
  task t = loop_synchronously(10000000);
  t();
  return 0;
}
