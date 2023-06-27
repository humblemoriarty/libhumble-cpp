#ifndef HUMBLE_ALIGNED_ALLOCATOR_H_
#define HUMBLE_ALIGNED_ALLOCATOR_H_

#include <cstddef>
#include <concepts>
#include <stdexcept>
#include <type_traits>

#include "humble/utils.hpp"

namespace hmbl::posix
{

/// @brief Allocator supports an alignment as a template argument
/// @tparam T An allocated data type
/// @tparam kAlignment A given alignment
/// @details Satisfies Allocator requirements.
/// Additionally guaranties the proper alignment of an allocated dada chunk
template <typename T, size_t kAlignment = 16>
struct AlignedAllocator
{
    using value_type =  T;

    template<typename U>
    struct rebind
    {
        using other = AlignedAllocator<U, kAlignment>;
    };

    static constexpr size_t alignment() noexcept { return kAlignment; }

    value_type * allocate(size_t n)
    {
        void *p;
        if(posix_memalign(&p, kAlignment, sizeof(value_type) * n))
        {
            throw std::bad_alloc();
        }
        return static_cast<value_type *>(p);
    }

    void deallocate(value_type * p, size_t /*n*/) noexcept
    {
        free(p);
    }
};

template <typename TAllocator>
concept CAlignedAllocator = requires
{
    requires (utils::is_pow_2(TAllocator::alignment()));
    { TAllocator::alignment() } noexcept;
};

}

#endif // HUMBLE_ALIGNED_ALLOCATOR_H_