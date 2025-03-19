#include <cassert>
#include <cstring>
#include <coroutine>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

#include <arpa/inet.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <fcntl.h>


struct promise_type;

using CoroutineHandle = std::coroutine_handle<promise_type>;

struct Coroutine : public CoroutineHandle {
  using promise_type = ::promise_type;
};

struct promise_type {
  Coroutine get_return_object() { return (Coroutine)CoroutineHandle::from_promise(*this); }
  std::suspend_always initial_suspend() noexcept { return {}; }
  std::suspend_always final_suspend() noexcept { return {}; }
  void return_void() {}
  void unhandled_exception() {}
};

void setsockopt_i(int fd, int level, int optname, int value) {
  if (auto r = setsockopt(fd, level, optname, &value, 4); r != 0) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }
}

void epoll_ctl_ex(int epfd, int op, int fd, epoll_event* event) {
  if (auto r = epoll_ctl(epfd, op, fd, event); r != 0) {
    perror("epoll_ctl");
    exit(EXIT_FAILURE);
  }
}

void setnonblock(int fd) {
  auto flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    perror("fcntl");
    exit(EXIT_FAILURE);
  }
  if (auto r = fcntl(fd, F_SETFL, flags | O_NONBLOCK); r == -1) {
    perror("fcntl");
    exit(EXIT_FAILURE);
  }
}

auto ToString(const sockaddr_in& addr) -> std::string {
  char buff[INET_ADDRSTRLEN + 1];
  if (!inet_ntop(AF_INET, &addr, buff, sizeof(addr))) {
    perror("inet_ntop");
    exit(EXIT_FAILURE);
  }
  return buff;
}

struct ReadAwaiter {
  bool ready;
  ssize_t ret;
  int fd;
  char* buf;
  size_t len;

  static ReadAwaiter Ready(int fd, int ret) {
    return {.ready = true, .ret = ret, .fd = fd};
  }

  static ReadAwaiter Suspend(int fd, char* buf, size_t len) {
    return {.ready = false, .fd = fd, .buf = buf, .len = len};
  }

  bool await_ready() const noexcept { return ready; }

  void await_suspend(std::coroutine_handle<> handle) {
    assert(!ready);
    std::cout << "[" << fd << "] suspended by read\n";
  }

  ssize_t await_resume() {
    if (!ready) {
      std::cout << "[" << fd << "] resumed from read\n";
      ret = read(fd, buf, len);
    }
    std::cout << "[" << fd << "] received " << ret << " byte(s)\n";
    return ret;
  }
};

ReadAwaiter Read(int fd, char* buf, size_t len) {
  auto ret = read(fd, buf, len);
  auto ready = !(ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
  return ready ? ReadAwaiter::Ready(fd, ret) : ReadAwaiter::Suspend(fd, buf, len);
}

struct WriteAwaiter {
  bool ready;
  ssize_t ret;
  int epfd;
  int sockfd;
  const char* buf;
  size_t len;
  void* handle_address;

  static WriteAwaiter Ready(int sockfd, ssize_t ret) {
    return WriteAwaiter{.ready = true, .ret = ret, .sockfd = sockfd};
  }

  static WriteAwaiter Suspend(int epfd, int sockfd, const char* buf, size_t len) {
    return WriteAwaiter{.ready = false, .epfd = epfd, .sockfd=sockfd, .buf = buf, .len = len};
  }

  bool await_ready() const noexcept { return ready; }

  void await_suspend(std::coroutine_handle<> handle) {
    assert(!ready);
    std::cout << "[" << sockfd << "] suspended by write\n";
    handle_address = handle.address();
    // add EPOLLOUT and pass the coroutine handle to epoll
    auto ev = epoll_event{};
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.ptr = handle.address();
    epoll_ctl_ex(epfd, EPOLL_CTL_MOD, sockfd, &ev);
  }

  ssize_t await_resume() {
    if (!ready) {
      assert(handle_address);
      std::cout << "[" << sockfd << "] resumed from write\n";
      // remove EPOLLOUT
      auto ev = epoll_event{};
      ev.events = EPOLLIN;
      ev.data.ptr = handle_address;
      epoll_ctl_ex(epfd, EPOLL_CTL_MOD, sockfd, &ev);
      // write message
      ret = write(sockfd, buf, len);
    }
    std::cout << "[" << sockfd << "] sent " << ret << " byte(s)\n";
    return ret;
  }
};

WriteAwaiter Write(int epfd, int fd, const char* buf, size_t len) {
#ifdef SIMULATE_PARTIAL_WRITE // 模拟partial write
  len = len > 1 ? len / 2 : len;
#endif

#ifdef SIMULATE_BLOCK_WRITE // 模拟write阻塞的情况
  return WriteAwaiter::Suspend(epfd, fd, buf, len);
#else
  auto ret = write(fd, buf, len);
  auto ready = !(ret == -1 && (errno = EAGAIN || errno == EWOULDBLOCK));
  return ready ? WriteAwaiter::Ready(fd, ret) : WriteAwaiter::Suspend(epfd, fd, buf, len);
#endif
}

Coroutine HandleConnection(int epfd, int fd) {
  auto buff = std::string{};
  auto writable = (size_t)0;
  buff.resize(1024);
  while (true) {
    if (writable) {
      auto r = co_await Write(epfd, fd, buff.data(), writable);
      if (r < 0) {
        std::cerr << "[" << fd << "] write failed: " << strerror(errno) << '\n';
        break;
      } else if (r < writable) { // partial write
        std::move(buff.begin() + r, buff.begin() + writable, buff.begin());
      }
      writable -= r; 
    } else {
      auto nr = co_await Read(fd, buff.data(), buff.size());
      if (nr < 0) {
        std::cerr << "[" << fd << "] read failed: " << strerror(errno) << '\n';
        break;
      } else if (nr == 0) {
        std::cout << "[" << fd << "] disconnected\n";
        break;
      } else {
        writable = nr;
      }
    }
  }
  epoll_ctl_ex(epfd, EPOLL_CTL_DEL, fd, nullptr);
  (void)close(fd);
  std::cout << "[" << fd << "] closed\n";
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " port\n";
    return -1;
  }
  auto port = (uint16_t)atoi(argv[1]);
  auto bindfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (bindfd < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  setsockopt_i(bindfd, SOL_SOCKET, SO_REUSEADDR, 1);
  auto bindaddr = sockaddr_in {
    .sin_family = AF_INET, 
    .sin_port = htons(port),
    .sin_addr = {
      .s_addr = htons(INADDR_ANY)
    }
  };
  if (auto r = bind(bindfd, (const sockaddr*)&bindaddr, sizeof(bindaddr)); r != 0) {
    perror("bind");
    exit(EXIT_FAILURE);
  }
  if (auto r = listen(bindfd, 100); r != 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }
  auto epfd = epoll_create(1);
  auto ev = epoll_event{};
  ev.events = EPOLLIN;
  ev.data.fd = bindfd;
  epoll_ctl_ex(epfd, EPOLL_CTL_ADD, bindfd, &ev);
  epoll_event events[128];
  for (;;) {
    auto ne = epoll_wait(epfd, events, sizeof(events)/sizeof(events[0]), -1);
    if (ne == -1) {
      perror("epoll_wait");
      exit(EXIT_FAILURE);
    }
    for (auto i = 0; i < ne; i++) {
      auto& e = events[i];
      if (e.data.fd == bindfd) {
        auto peer_addr = sockaddr_in {};
        auto addr_len = (socklen_t)(sizeof(peer_addr));
        auto fd = accept(bindfd, (sockaddr*)&peer_addr, &addr_len);
        if (fd < 0) {
          perror("accept");
          exit(EXIT_FAILURE);
        }
        std::cout << "[" << fd << "]: " << ToString(peer_addr) << " connected\n";
        setnonblock(fd);
        auto coro = HandleConnection(epfd, fd);
        ev = epoll_event{};
        ev.events = EPOLLIN;
        ev.data.ptr = coro.address();
        epoll_ctl_ex(epfd, EPOLL_CTL_ADD, fd, &ev);
      } else {
        auto handle = std::coroutine_handle<>::from_address(e.data.ptr);
        handle.resume();
      }
    }
  }
  return 0;
}


