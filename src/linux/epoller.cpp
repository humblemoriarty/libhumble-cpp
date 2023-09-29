#include <cstdio>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstdint>

#include <coroutine>
#include <span>
#include <future>

#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

namespace hmbl::async
{

// forwards
class EPollFdHandle;
class EPoller;

namespace detail
{

class EPollEvent
{
    EPoller       *poller_{};
    EPollFdHandle *fd_handle_{};
    uint32_t       enabled_types_{};

public:
    EPollEvent() = default;
    EPollEvent(EPoller &poller, EPollFdHandle &fd_hdl, uint32_t events);
    ~EPollEvent();

    auto watched()        const { return enabled_types_; }
    void watch(uint32_t events) { enabled_types_ = events;}

    void modify(uint32_t events);
};

} // namespace detail

class EPollFdHandle
{
    friend class detail::EPollEvent;
    friend class EPoller;

    // for intrusive list
    EPollFdHandle          *prev_{};
    EPollFdHandle          *next_{};

    std::coroutine_handle<> coro_handle_;
    detail::EPollEvent      event_;
    int                     fd_;

public:
    EPollFdHandle() = default;
    EPollFdHandle(int fd, EPoller &poller, uint32_t events);

    ~EPollFdHandle()
    {
        printf("~EPollFdHandler() this = %p, prev_ = %p, next_ = %p\n", this, prev_, next_);
        if (prev_) prev_->next_ = next_;
        if (next_) next_->prev_ = prev_;
    }

    EPollFdHandle(const EPollFdHandle & ) = delete;
    EPollFdHandle & operator=(const EPollFdHandle & ) = delete;

    // Regular methods
    std::span<const char> try_read_some(void *buffer, size_t capacity)
    {
        auto n = ::read(fd_, buffer, capacity);
        if (n < 0)
        {
            // TODO: process error
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                std::abort();
            return {};
        }
        return {static_cast<const char*>(buffer), static_cast<size_t>(n)};
    }

    // Awaitable methods
    auto read_some_async(void *buffer, size_t capacity)
    {
        struct Awaiter
        {
            EPollFdHandle  &epoll_hdl;
            void           *buf;
            size_t          cap;


            bool await_ready() const noexcept { return false; }

            std::span<const char> await_resume()
            {
                epoll_hdl.coro_handle_ = std::coroutine_handle<>();
                return epoll_hdl.try_read_some(buf, cap);
            }

            void await_suspend(std::coroutine_handle<> hdl) noexcept
            {
                epoll_hdl.coro_handle_ = std::move(hdl);
            }
        };
        return Awaiter{*this, buffer, capacity};
    }

private:
    void try_resume(uint32_t events)
    {
        if (!(events & event_.watched())) return;
        // do nothing is if the coroutine doesn't suspended on async method of this class
        if (coro_handle_) coro_handle_.resume();
    }

    void force_resume()
    {
        if (coro_handle_) coro_handle_.resume();
    }
};

class EPoller
{
    friend class detail::EPollEvent;
    friend class EPollFdHandle;

    static constexpr uint64_t kStopFlag = 0; // indicates stop signal

    EPollFdHandle fd_handlers_list_; // intrusive list of all events
    int           epoll_fd_;
    int           stop_pipe_[2];
    bool          stop_flag_{};

public:
    EPoller() : epoll_fd_{::epoll_create1(0)}
    {
        // TODO: process error
        if (epoll_fd_ < 0)
            std::abort();
        // TODO
        if (::pipe(stop_pipe_) != 0)
            std::abort();

        epoll_event stop_event;
        stop_event.data.u64 = kStopFlag;
        stop_event.events   = EPOLLIN | EPOLLET;

        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, stop_pipe_[0], &stop_event) < 0)
        {
            // TODO: process
            std::abort();
        }
    }

    ~EPoller()
    {
        printf("~EPoller() fd_handlers_list_ = %p, fd_handlers_list_.next_ = %p\n",
                &fd_handlers_list_, fd_handlers_list_.next_);
        assert(!fd_handlers_list_.next_);
        close(epoll_fd_);
        close(stop_pipe_[0]);
        close(stop_pipe_[1]);
    }

    EPoller(const EPoller & )             = delete;
    EPoller & operator=(const EPoller & ) = delete;

    void poll();

    void stop()
    {
        // TODO
        if (::write(stop_pipe_[1], "1234567890", 10) < 0) // just write one symbol
            std::abort();
        printf("Stop signaled\n");
    }

    bool is_stopped() const noexcept { return stop_flag_; }

private:
    auto register_handler(EPollFdHandle &hdl, uint32_t events)
    {
        // link to intrusive
        hdl.next_ =  fd_handlers_list_.next_;
        hdl.prev_ = &fd_handlers_list_;
        fd_handlers_list_.next_ = &hdl;

        return detail::EPollEvent(*this, hdl, events);
    }
};

namespace detail
{

EPollEvent::EPollEvent(EPoller &poller, EPollFdHandle &fd_hdl, uint32_t events)
    : poller_{&poller}
    , fd_handle_{&fd_hdl}
    , enabled_types_{events}
{
    epoll_event event;
    event.data.ptr = fd_handle_;
    event.events   = events;

    if (::epoll_ctl(poller_->epoll_fd_, EPOLL_CTL_ADD, fd_handle_->fd_, &event) < 0)
    {
        // TODO: process
        std::abort();
    }
}

EPollEvent::~EPollEvent()
{
    // if empty event
    if (!poller_) [[unlikely]]
        return;

    ::epoll_ctl(poller_->epoll_fd_, EPOLL_CTL_DEL, fd_handle_->fd_, NULL);
}

void EPollEvent::modify(uint32_t events)
{
    epoll_event event;
    event.data.ptr = &fd_handle_;
    event.events   = events;
    if (::epoll_ctl(poller_->epoll_fd_, EPOLL_CTL_ADD, fd_handle_->fd_, &event) < 0)
    {
        // TODO: process
        std::abort();
    }
}

} // namespace detail

EPollFdHandle::EPollFdHandle(int fd, EPoller &poller, uint32_t events)
    : event_{poller.register_handler(*this, events)}
    , fd_{fd}
{
    printf("EPollFdHandle() prev_ = %p, next_ = %p\n", prev_, next_);
}

void EPoller::poll()
{
    constexpr int kMaxEvents = 1'024;
    epoll_event events[kMaxEvents];
    int n = ::epoll_wait(epoll_fd_, events, kMaxEvents, -1);
    if (n < 0) [[unlikely]]
    {
        // TODO: process errors
        if (errno != EINTR)
            std::abort();
    }
    // process events
    for (size_t i = 0; i < n; ++i)
    {
        auto &event = events[i];
        if (event.data.u64 == kStopFlag) [[unlikely]]
        {
            printf("Got stop\n");
            // notify all to stop
            stop_flag_ = true;
            for (auto *hdl = fd_handlers_list_.next_; hdl; hdl = hdl->next_)
                hdl->force_resume();
            return;
        }
        // TODO: process error events
        auto &fd_handle = *reinterpret_cast<EPollFdHandle*>(event.data.ptr);
        fd_handle.try_resume(event.events);
    }
}

class EPollCoroutine
{
public:
    struct promise_type
    {
        void                unhandled_exception() noexcept {}
        EPollCoroutine      get_return_object()            { return EPollCoroutine(*this); }
        std::suspend_never  initial_suspend()     noexcept { return {}; }
        std::suspend_never  final_suspend()       noexcept { return {}; } // TODO: never?
        void                return_void()         noexcept {}
    };

private:
    using Handle = std::coroutine_handle<promise_type>;

    Handle handle_;

    explicit EPollCoroutine(promise_type &p)
        : handle_{Handle::from_promise(p)}
    {}

public:
    ~EPollCoroutine()
    {
        if (handle_) handle_.destroy();
    }
};

} // namespace hmbl::async

hmbl::async::EPollCoroutine test_coro(hmbl::async::EPoller &poller, int fd)
{
    hmbl::async::EPollFdHandle hdl(fd, poller, EPOLLIN);
    char buf[1025];
    while (1)
    {
        auto res = co_await hdl.read_some_async(buf, sizeof(buf - 1));
        if(poller.is_stopped())
        {
            printf("Stop flag detected! Bye!\n");
            break;
        }

        if (res.data())
        {
            printf("buf=%p, res.data()=%p, res.size() = %lu\n", buf, res.data(), res.size());
            buf[res.size()] = '\0';
            printf("res = '%s'\n", res.data());
        }
        else
        {
            std::abort();
        }
    }
}

int main()
{
    int fds[2];
    if (::pipe(fds) != 0)
    {
        // TODO
        std::abort();
    }

    hmbl::async::EPoller poller;
    auto coro = test_coro(poller, fds[0]);
    if (::write(fds[1], "Hello World!\n", 13) < 0)
    {
        // TODO
        std::abort();
    }
    ::fsync(fds[1]);
    printf("Write OK\n");

    poller.poll();
    poller.poll();

    // if (::write(fds[1], "Hello World!", 12) < 0)
    // {
    //     // TODO
    //     std::abort();
    // }
    auto res = std::async(std::launch::async,
        [&]() { std::this_thread::sleep_for(std::chrono::seconds(3)); poller.stop(); });
    poller.poll();
    // poller.stop();

    return 0;
}