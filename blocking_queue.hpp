#include <condition_variable>
#include <optional>
#include <queue>
#include <mutex>

template <class T>
class BlockingQueue {
public:
  explicit BlockingQueue(size_t capacity) : cap_{capacity} {}

  void put(T e);

  void emplace(auto&&... args);

  // Block until 
  T take();
  
private:
  const size_t cap_;
  mutable std::mutex mtx_{};
  std::queue<T> queue_{};
  std::condition_variable not_empty_{};
  std::condition_variable not_full_{};
};

template <class T>
inline void BlockingQueue<T>::put(T e) {
  std::unique_lock l(mtx_);
  not_full_.wait(l, [this]() { return queue_.size() < cap_; });
  queue_.emplace(std::move(e));
  l.unlock();
  not_empty_.notify_one();
}

template <class T>
inline void BlockingQueue<T>::emplace(auto&&... args) {
  std::unique_lock l(mtx_);
  not_full_.wait(l, [this]() { return queue_.size() < cap_; });
  queue_.emplace(std::forward<decltype(args)>(args)...);
  l.unlock();
  not_empty_.notify_one();
}

template <class T>
inline T BlockingQueue<T>::take() {
  std::unique_lock l(mtx_);
  not_empty_.wait(l, [this]() { return !queue_.empty(); });
  auto r = std::move(queue_.front());
  queue_.pop();
  l.unlock();
  not_full_.notify_one();
  return r;
}

