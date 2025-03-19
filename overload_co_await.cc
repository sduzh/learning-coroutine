#include <ctime>
#include <chrono>
#include <coroutine>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "blocking_queue.hpp"

auto g_task_queue = BlockingQueue<std::coroutine_handle<>>{100};

struct Task {
  struct promise_type {
    Task get_return_object() { return {std::coroutine_handle<promise_type>::from_promise(*this)}; }
    std::suspend_always initial_suspend() const noexcept { return {}; }
    std::suspend_never final_suspend() const noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };

  std::coroutine_handle<promise_type> handle;
};

template <class Rep, class Period>
auto operator co_await(std::chrono::duration<Rep, Period> d) {
  struct Awaiter {
    std::chrono::system_clock::duration duration;

    Awaiter(std::chrono::system_clock::duration d) : duration(d) {}
    bool await_ready() const { return duration.count() <= 0; }
    void await_suspend(std::coroutine_handle<> h) {
      auto t = std::thread([d=duration, h=h]() {
          std::this_thread::sleep_for(d);
          g_task_queue.put(h);
          });
      t.detach();
    }
    void await_resume() {}
  };
  return Awaiter{d};
}

auto CurrentDateTime() {
  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);

  std::stringstream ss;
  ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
  return ss.str();
}

int main() {
  for (auto i = 0; i < 10; i++) {
    auto task = [=](int id) -> Task {
      std::cout << CurrentDateTime() << ": Task [" << id << "] started\n"; 
      co_await std::chrono::seconds(1);
      std::cout << CurrentDateTime() << ": Task [" << id << "] resumed\n"; 
      co_return;
    }(i);
    g_task_queue.put(task.handle);
  }

  while (auto h = g_task_queue.take()) {
    h.resume();
  }
  return 0;
}
