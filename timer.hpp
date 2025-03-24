#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

// Simple but non-performant timer
class Timer {
public:
  using Fun = std::function<void()>;

  static Timer& Instance() {
    static Timer obj;
    return obj;
  }

  template <class Rep, class Period>
  void RunAfter(const std::chrono::duration<Rep, Period>& delay, Fun fun);

  void Stop();

private:
  using Clock = std::chrono::steady_clock;
  using TimePoint = std::chrono::time_point<Clock>;

  struct Event {
    TimePoint tp;
    Fun fun;
    uint64_t order;

    bool operator<(const Event& rhs) const noexcept {
      if (tp != rhs.tp) return !(tp < rhs.tp);
      return !(order < rhs.order);
    }
  };

  Timer();

  void Run();

  std::mutex mtx_;
  std::condition_variable cv_;
  std::priority_queue<Event> events_;
  uint64_t order_{0};
  bool stopped_{false};

  std::thread thread_;
};

inline Timer::Timer() {
  thread_ = std::thread([this]() { Run(); });
}

inline void Timer::Stop() {
  {
    auto l = std::lock_guard(mtx_);
    stopped_ = true;
    cv_.notify_one();
  }
  thread_.join();
}

inline void Timer::Run() {
  while (true) {
    auto l = std::unique_lock(mtx_);
    cv_.wait(l, [this]() { return stopped_ || !events_.empty(); });
    if (stopped_) {
      break;
    }
    auto now = Clock::now();
    auto min_tp = events_.top().tp;
    if (min_tp <= now) {
      auto min_ev = events_.top();
      events_.pop();
      l.unlock();
      min_ev.fun();
    } else {
      cv_.wait_for(l, now - min_tp);
    }
  }
}

template <class Rep, class Period>
inline void Timer::RunAfter(const std::chrono::duration<Rep, Period>& delay, Fun fun) {
  auto tp = Clock::now() + delay;
  auto l = std::unique_lock(mtx_);
  auto ev = Event{.tp = tp, .fun = std::move(fun), .order = ++order_};
  events_.push(std::move(ev));
  if (events_.top().order == order_ - 1) {
    cv_.notify_one();
  }
}

template <class Rep, class Period>
inline auto operator co_await(const std::chrono::duration<Rep, Period>& rel_time) {
  struct awaiter {
    bool await_ready() const noexcept { return rel_time.count() <= 0; }
    auto await_resume() noexcept {}
    void await_suspend(std::coroutine_handle<> h) {
      Timer::Instance().RunAfter(rel_time, [h]() { h.resume(); });
    }
  
    std::chrono::duration<Rep, Period> rel_time;
  };
  return awaiter{rel_time};
}
