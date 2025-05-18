#ifndef LIBHUMBLE_CPP_CONCURRENT_RCU_H_
#define LIBHUMBLE_CPP_CONCURRENT_RCU_H_

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
    using ResourcePointer               = const T*;
    using OwningResourcePointer         = std::unique_ptr<const T, Deleter>;

private:
    using AtomicConstResourcePointer    = std::atomic<ConstPointer>;

    static_assert(AtomicConstResourcePointer::is_always_lock_free);

    OwningResourcePointer           resource_{};                ///< A current active resourse.
    OwningResourcePointer           outdated_resource_{};       ///< A resourse is about to removed, but potentially in use.
    AtomicConstResourcePointer      cached_resource_ptr_{};     ///< An atomic pointer to `resource_`.
    size_t                          active_readers_{};          ///< A number of readers online.
    std::atomic<size_t>             readers_upd_in_progress_{}; ///< A number of readers which haven't fetched the new pointer yet.
    std::mutex                      mtx_;

    void notify_updated() noexcept
    {
        readers_upd_in_progress_.fetch_sub(1, std::memory_order_release);
    }

    ResourcePointer fetch_and_notify(ResourcePointer p) noexcept
    {
        auto new_p = cached_resource_ptr_.load(std::memory_order_acquire);
        if (p != new_p) [[unlikely]]
            notify_updated();
        return new_p;
    }

    ResourcePointer activate_reader() noexcept
    {
        std::lock_guard lock{mtx_};
        ++active_readers_;
        return resource_.get();
    }

    void deactivate_reader(ResourcePointer p) noexcept
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

    /// @warning The context MUST have no active readers.
    ~RcuContext()
    {
        assert(!in_progress());
        assert(!active_readers_);
    }

    auto readers_in_progress() const noexcept   { return readers_upd_in_progress_.load(std::memory_order_acquire); }

    /// @brief Checks if there is at least one active reader that haven't fetched the new pointer yet (i.e. an update is in progress).
    bool in_progress() const noexcept           { return !!readers_in_progress(); }

    /// @brief Assigns a new resourse pointer.
    /// @param p A resource pointer.
    /// @warning There MUST be no any operation in progress.
    void update(OwningResourcePointer p) noexcept
    {
        assert(!in_progress()); // no readers waiting for update

        // protect with mutex to make sure there's no activation/deactivation in progress
        std::lock_guard lock{mtx_};
        readers_upd_in_progress_.store(active_readers_, std::memory_order_release);

        outdated_resource_ = std::exchange(resource_, std::move(p)); // update stored owning pointer
        cached_resource_ptr_.store(value_ptr_.get(), std::memory_order_release); // make visible a new pointer for readers
    }

    /// @brief If there is no update in progress - releases the old resource.
    /// @return `true` - if the operation succeeded, otherwise - `false`.
    bool check_update_complete() noexcept
    {
        if (!in_progress())
        {
            outdated_resource_.reset();
            return true;
        }
        return false;
    }

    /// @brief Blocks the current thread and wait for update completion.
    /// @note Exploits a spin-lock.
    void wait_update_complete() noexcept
    {
        static constexpr unsigned kBusyCycles{256};
        for (unsigned i{1}; !check_update_complete(); ++i)
        {
            if (!(i % kBusyCycles)) [[unlikely]]
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void update_and_wait_complete(OwningResourcePointer p) noexcept
    {
        update(std::move(p));
        wait_update_complete();
    }
};

template <typename T, typename Deleter>
class RcuReader
{
    friend class RcuContext<T, Deleter>;

    using Context       = RcuContext<T, Deleter>;
    using typename        Context::ResourcePointer;

    Context                *ctx_{};
    ResourcePointer         cached_resource_ptr_{};
    bool                    active_{};

public:
    /// @brief Creates an invalid (empty) RCU resource reader.
    RcuReader() = default;

    /// @brief Creates a new valid RCU resource reader.
    /// @param ctx A reference to the parent RCU context.
    /// @param active If the reader is activated at the construction.
    explicit RcuReader(Context &ctx, bool active = true) noexcept
        : ctx_{&ctx}
        , cached_resource_ptr_{active ? ctx_->activate_reader() : nullptr}
        , active_{active}
    {
    }

    RcuReader(const RcuReader & ) = delete;
    RcuReader & operator=(const RcuReader & ) = delete;

    /// @brief Moves the resource from another reader leaving it invalid.
    RcuReader(RcuReader &&other) noexcept
        : ctx_{std::exchange(other->ctx_, nullptr)}
        , cached_resource_ptr_{std::exchange(other->cached_resource_ptr_, nullptr)}
        , active_{std::exchange(other->active_, false)}
    {
    }

    /// @brief Moves the resource from another reader leaving it invalid.
    RcuReader & operator=(RcuReader &&other) noexcept
    {
        ctx_                    = std::exchange(other->ctx_, nullptr);
        cached_resource_ptr_    = std::exchange(other->cached_resource_ptr_, nullptr);
        active_                 = std::exchange(other->active_, false);
        return *this;
    }

    /// @note Also deregisters the reader if it's valid and active.
    ~RcuReader()
    {
        if (is_valid() && is_active())
            ctx_->deactivate_reader(cached_resource_ptr_);
    }

    /// @brief Checks if the object is bound to any parent RCU context.
    /// @note It's the only operation permited on an invalid object (except for constructors and operators).
    bool is_valid() const noexcept  { return !!ctx_; }

    /// @brief Checks if the object registered as active.
    bool is_active() const noexcept { assert(is_valid()); return active_; }

    explicit operator bool() const noexcept { return is_valid(); }

    /// @brief Gets a resource pointer fetch at the last `update()`.
    ResourcePointer get_resource() const noexcept  { assert(is_active()); return cached_resource_ptr_; }

    /// @brief Registers the object as an active in the parent or does nothing if it's already active.
    void activate() noexcept
    {
        if (is_active()) return;
        cached_resource_ptr_    = ctx_->activate_reader();
        active_                 = true;
    }

    /// @brief Deregisters the object in the parent.
    void deactivate() noexcept
    {
        if (!is_active()) return;
        ctx_->deactivate_reader(cached_resource_ptr_);
        cached_resource_ptr_    = nullptr;
        active_                 = false;
    }

    /// @brief Fetchs a new resourse pointer from the parent context. Do nothing if the resource hasn't changed.
    /// @details Compares pointers to determine if the resourse has changed.
    /// @warning The object MUST be valid and active.
    void update() noexcept
    {
        assert(is_active());
        cached_resource_ptr_ = ctx_->fetch_and_notify(cached_resource_ptr_);
    }
};

} // namespace hmbl::concurrent

#endif // LIBHUMBLE_CPP_CONCURRENT_RCU_H_
