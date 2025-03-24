#include <ctime>
#include <chrono>
#include <coroutine>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "timer.hpp"

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

auto CurrentDateTime() {
  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);

  std::stringstream ss;
  ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
  return ss.str();
}

int main() {
  auto coros = std::vector<Task>();
  for (auto i = 0; i < 10; i++) {
    auto task = [=](int id) -> Task {
      std::cout << CurrentDateTime() << ": Task [" << id << "] started\n"; 
      co_await std::chrono::milliseconds(1000 - id * 10);
      std::cout << CurrentDateTime() << ": Task [" << id << "] resumed\n"; 
      co_return;
    }(i);
    coros.push_back(task);
  }

  for (auto& t: coros) {
    t.handle.resume();
  }
  std::this_thread::sleep_for(std::chrono::seconds(2));
  Timer::Instance().Stop();
  return 0;
}
