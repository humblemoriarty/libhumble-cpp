#ifndef LIBHUMBLE_CPP_DETAIL_MEMORY_TRAITS_HPP_
#define LIBHUMBLE_CPP_DETAIL_MEMORY_TRAITS_HPP_

#include <cstdlib>
#include <cstddef>
#include <concepts>
#include <memory>

namespace hmbl::detail
{
struct static_memory_traits_base
{
    // static constexpr size_t c_small_len = 16;
    static constexpr size_t c_alignment = 16;
};

template <typename TWord>
struct static_memory_traits : public static_memory_traits_base
{
    template <size_t DataSize>
    static constexpr bool const_size_cmp(const TWord *v1, const TWord *v2) noexcept
    {
        for (size_t i = 0; i < DataSize; ++i)
        {
            if (v1[i] != v2[i])
                return false;
        }
        return true;
    }

    /// Returns true if every byte equal the given word
    template <TWord W, size_t DataSize>
    static constexpr bool const_size_word_cmp(const TWord *v) noexcept
    {
        for (size_t i = 0; i < DataSize; ++i)
            if (v[i] != W)
                return false;
        return true;
    }

    template <TWord W, size_t DataSize>
    static constexpr void const_size_word_set(TWord *v) noexcept
    {
        for (size_t i = 0; i < DataSize; ++i)
            v[i] = W;
    }

    template <TWord W>
    static constexpr void word_set(TWord *v, size_t n) noexcept
    {
        for (size_t i = 0; i < n; ++i)
            v[i] = W;
    }

    template <size_t DataSize, typename Op>
    static constexpr void const_size_apply(TWord *v, Op&& op) noexcept
    {
        for (size_t i = 0; i < DataSize; ++i)
            v[i] = op(v[i]);
    }

    template <size_t DataSize, typename Op>
    static constexpr void const_size_bin_op(TWord *dst_v, const TWord *v1, const TWord *v2, Op&& op) noexcept
    {
        for (size_t i = 0; i < DataSize; ++i)
            dst_v[i] = op(v1[i], v2[i]);
    }
};

} // namespace hmbl::detail

#endif // header guard