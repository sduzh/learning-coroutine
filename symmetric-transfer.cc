// 使用symmetric coroutine transfer并结合编译器tail-call optimization来
// 避免stack-overflow问题，参考https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer#enter-symmetric-transfer
//
// 使用GCC编译下面的程序时，需要增加优化选项-foptimize-sibling-calls(O2/O3/Os默认打开)，
// 否则仍然会出现stack-overflow问题
#include <coroutine>
#include <utility>

class task {
public:
  task(task&& t) noexcept : coro_(std::exchange(t.coro_, {})) {}

  ~task() { if (coro_) coro_.destroy(); }

  struct promise_type;

  struct final_awaiter {
    bool await_ready() const noexcept { return false; }
    auto await_suspend(std::coroutine_handle<promise_type> h) noexcept {
      return h.promise().continuation;
    }
    void await_resume() noexcept {}
  };

  struct promise_type {
    auto get_return_object() noexcept { return task(std::coroutine_handle<promise_type>::from_promise(*this)); }
    auto initial_suspend() noexcept { return std::suspend_always{}; }
    auto final_suspend() noexcept { return final_awaiter{}; }
    void return_void() noexcept {}
    void unhandled_exception() { throw; }

    std::coroutine_handle<> continuation{std::noop_coroutine()};
  };

  struct awaiter {
    bool await_ready() const noexcept { return false; }
    auto await_suspend(std::coroutine_handle<> h) noexcept {
      coro.promise().continuation = h;
      return coro;
    }
    void await_resume() noexcept {}

    std::coroutine_handle<promise_type> coro;
  };

  auto operator co_await() && noexcept { return awaiter{coro_}; }

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
