#ifndef LIBHUMBLE_CPP_DETAIL_MEMORY_TRAITS_HPP_
#define LIBHUMBLE_CPP_DETAIL_MEMORY_TRAITS_HPP_

#include <cstddef>

#include "humble/utils.hpp"

namespace hmbl::detail
{

template <typename TWord>
struct StaticMemoryTraits
{
    /// Check for equality two arrays of the same constant size
    template <size_t kDataSize>
    static constexpr bool const_size_eq(const TWord v1[kDataSize], const TWord v2[kDataSize]) noexcept
    {
        for (size_t i = 0; i < kDataSize; ++i)
        {
            if (v1[i] != v2[i])
                return false;
        }
        return true;
    }

    /// Check if every word of the given constant size array equal to @p kW
    template <TWord kW, size_t kDataSize>
    static constexpr bool const_size_word_eq(const TWord v[kDataSize]) noexcept
    {
        for (size_t i = 0; i < kDataSize; ++i)
            if (v[i] != kW)
                return false;
        return true;
    }

    /// Set each word of the constant size array to @p kW
    template <TWord kW, size_t kDataSize>
    static constexpr void const_size_word_set(TWord v[kDataSize]) noexcept
    {
        for (size_t i = 0; i < kDataSize; ++i)
            v[i] = kW;
    }

    /// Sen @p n words of @p v to value @p kW
    template <TWord kW>
    static constexpr void word_set(TWord *v, size_t n) noexcept
    {
        for (size_t i = 0; i < n; ++i)
            v[i] = kW;
    }

    /// Apply @p op to each word of the constant size array
    template <size_t kDataSize, typename TOp>
    static constexpr void const_size_apply(TWord v[kDataSize], TOp&& op) noexcept
    {
        for (size_t i = 0; i < kDataSize; ++i)
            v[i] = op(v[i]);
    }

    /// Apply binary @p op to each word pair of given constant size arrays and save result to @p dst_v
    template <size_t kDataSize, typename TOp>
    static constexpr void const_size_bin_op(TWord dst_v[kDataSize],
        const TWord v1[kDataSize], const TWord v2[kDataSize], TOp&& op) noexcept
    {
        for (size_t i = 0; i < kDataSize; ++i)
            dst_v[i] = op(v1[i], v2[i]);
    }
};

} // namespace hmbl::detail

#endif // header guard