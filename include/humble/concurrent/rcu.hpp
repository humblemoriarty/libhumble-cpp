#ifndef LIBHUMBLE_CPP_RCU_H_
#define LIBHUMBLE_CPP_RCU_H_

#include <cassert>
#include <chrono>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace hmbl::concurrent
{

template <typename T, typename Deleter>
class RcuReader;

template <typename T, typename Deleter = std::default_delete<T>>
class RcuContext
{
    friend class RcuReader<T, Deleter>;

public:
    using ConstResourcePointer          = const T*;
    using OwningResourcePointer         = std::unique_ptr<const T, Deleter>;

private:
    using AtomicConstResourcePointer    = std::atomic<ConstPointer>;

    static_assert(AtomicConstPointer::is_always_lock_free);

    OwningResourcePointer           resource_{};
    OwningResourcePointer           resource_to_remove_{};
    AtomicConstResourcePointer      cached_resource_ptr_{};
    size_t                          active_readers_{};          ///< A number of readers online
    std::atomic<size_t>             readers_upd_in_progress_{}; ///< A number of readers which haven't fetched the new pointer yet
    std::mutex                      mtx_;

    void notify_updated() noexcept
    {
        readers_upd_in_progress_.fetch_sub(1, std::memory_order_release);
    }

    ConstResourcePointer fetch_and_notify(ConstResourcePointer p) noexcept
    {
        auto new_p = cached_resource_ptr_.load(std::memory_order_acquire);
        if (p != new_p) [[unlikely]]
            notify_updated();
        return new_p;
    }

    ConstResourcePointer activate_reader() noexcept
    {
        std::lock_guard lock{mtx_};
        ++active_readers_;
        return resource_.get();
    }

    void deactivate_reader(ConstResourcePointer p) noexcept
    {
        std::lock_guard lock{mtx_};
        assert(active_readers_);
        --active_readers_;
        if (resource_.get() != p) [[unlikely]]
            notify_updated();
    }

public:
    explicit RcuContext(OwningResourcePointer p = nullptr) noexcept
        : resource_{std::move(p)}
    {
    }

    RcuContext(const RcuContext &) = delete;
    RcuContext & operator=(const RcuContext &) = delete;

    ~RcuContext()
    {
        assert(!in_progress());
        assert(!active_readers_);
    }

    auto readers_in_progress() const noexcept   { return readers_upd_in_progress_.load(std::memory_order_acquire); }

    /// @brief Check if there is any reader having not fetched the new pointer yet (an update is in progress)
    bool in_progress() const noexcept           { return !!readers_in_progress(); }

    void update(OwningResourcePointer p) noexcept
    {
        assert(!in_progress()); // no readers waiting for update

        // protect with mutex to make sure there's no activation/deactivation in progress
        std::lock_guard lock{mtx_};
        readers_upd_in_progress_.store(active_readers_, std::memory_order_release);

        resource_to_remove_ = std::exchange(resource_, std::move(p)); // update stored owning pointer
        cached_resource_ptr_.store(value_ptr_.get(), std::memory_order_release); // make visible a new pointer for readers
    }

    /// @brief Checks if an update in progress and releases the old resource if not.
    /// @return `true` if there are no readers that haven't updated their caches, otherwise - `false`
    bool check_update_complete() noexcept
    {
        if (!in_progress())
        {
            resource_to_remove_.reset();
            return true;
        }
        return false;
    }

    void wait_update_complete() noexcept
    {
        static constexpr unsigned kBusyCycles{256};
        for (unsigned i{1}; !check_update_complete(); ++i)
        {
            if (!(i % kBusyCycles)) [[unlikely]]
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void wait_and_update(OwningResourcePointer p) noexcept
    {
        wait_update_complete();
        update(std::move(p));
    }
};

template <typename T, typename Deleter>
class RcuReader
{
    friend class RcuContext<T, Deleter>;

    using Context       = RcuContext<T, Deleter>;
    using typename Context::ConstResourcePointer;

    Context                *ctx_{};
    ConstResourcePointer    cached_resource_ptr_{};
    bool                    active_{};

public:
    RcuReader() = default;

    explicit RcuReader(Context &ctx, bool active = true) noexcept
        : ctx_{&ctx}
        , cached_resource_ptr_{active ? ctx_->activate_reader() : nullptr}
        , active_{active}
    {
    }

    RcuReader(const RcuReader & ) = delete;
    RcuReader & operator=(const RcuReader & ) = delete;

    RcuReader(RcuReader &&other) noexcept
        : ctx_{std::exchange(other->ctx_, nullptr)}
        , cached_resource_ptr_{std::exchange(other->cached_resource_ptr_, nullptr)}
        , active_{std::exchange(other->active_, false)}
    {
    }

    RcuReader & operator=(RcuReader &&other) noexcept
    {
        ctx_                    = std::exchange(other->ctx_, nullptr);
        cached_resource_ptr_    = std::exchange(other->cached_resource_ptr_, nullptr);
        active_                 = std::exchange(other->active_, false);
        return *this;
    }

    ~RcuReader()
    {
        if (is_valid() && is_active())
            ctx_->deactivate_reader(cached_resource_ptr_);
    }

    bool is_valid() const noexcept  { return !!ctx_; }
    bool is_active() const noexcept { assert(is_valid()); return active_; }

    explicit operator bool() const noexcept { return is_valid(); }

    ConstResourcePointer get_resource() const noexcept  { assert(is_active()); return cached_resource_ptr_; }

    void activate() noexcept
    {
        assert(!is_active());
        cached_resource_ptr_    = ctx_->activate_reader();
        active_                 = true;
    }

    void deactivate() noexcept
    {
        assert(is_active());
        ctx_->deactivate_reader(cached_resource_ptr_);
        cached_resource_ptr_    = nullptr;
        active_                 = false;
    }

    void update() noexcept
    {
        assert(is_active());
        cached_resource_ptr_ = ctx_->fetch_and_notify(cached_resource_ptr_);
    }
};

} // namespace hmbl::concurrent

#endif // LIBHUMBLE_CPP_RCU_H_
