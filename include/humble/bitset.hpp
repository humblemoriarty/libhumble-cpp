#ifndef LIBHUMBLE_CPP_BITSET_HPP_
#define LIBHUMBLE_CPP_BITSET_HPP_

#include <cstddef>
#include <cstdint>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <bit>
#include <concepts>
#include <limits>
#include <type_traits>

#include "detail/memory_traits.h"
#include "constants.hpp"
#include "utils.hpp"

#include <cstdio>

namespace hmbl
{

namespace detail
{

template <typename TWord>
concept BitsetWordConcept = std::unsigned_integral<TWord>;

template <size_t Size, typename TWord>
concept BitsetOptimizedParamsConcept = BitsetWordConcept<TWord>
                                    && Size <= std::numeric_limits<TWord>::digits;

} // namespace detail

template <size_t Size, typename TWord = uint64_t,
          template <typename> typename TMemTraits = detail::static_memory_traits>
    requires detail::BitsetWordConcept<TWord>
class bitset
{
    using memory_traits = TMemTraits<TWord>;

    static constexpr size_t c_word_size_     = sizeof(TWord);
    static constexpr size_t c_n_bits_        = Size;
    static constexpr size_t c_n_bytes_       = utils::div_celling(Size, C::bits_per_byte);
    static constexpr size_t c_n_words_       = utils::div_celling(c_n_bytes_, c_word_size_);
    static constexpr size_t c_bits_per_word_ = c_word_size_ * C::bits_per_byte;

    static constexpr TWord c_word_none_mask_       = static_cast<TWord>(0);
    static constexpr TWord c_word_all_mask_        = ~c_word_none_mask_;
    static constexpr TWord c_hi_word_all_mask_     = c_word_all_mask_ >> (c_n_words_ * c_bits_per_word_ - c_n_bits_);

    constexpr size_t        word_idx_(size_t pos) const noexcept { return pos / c_bits_per_word_; }
    constexpr TWord &       word_(size_t pos)           noexcept { return words_[word_idx_(pos)]; }
    constexpr const TWord & word_(size_t pos)     const noexcept { return words_[word_idx_(pos)]; }
    constexpr TWord &       hi_word_()                  noexcept { return words_[c_n_words_ - 1]; }
    constexpr const TWord & hi_word_()            const noexcept { return words_[c_n_words_ - 1]; }

    static constexpr size_t bit_idx_(size_t pos)        noexcept { return pos % c_bits_per_word_; }
    static constexpr TWord  bit_mask_(size_t pos)       noexcept { return static_cast<TWord>(1) << bit_idx_(pos); }

    TWord words_[c_n_words_]{};

public:
    constexpr bitset() = default;
    constexpr bitset(TWord val) : words_{val}
    {
        if constexpr (c_n_bits_ < c_bits_per_word_)
        {
            hi_word_() &= c_hi_word_all_mask_;
        }
    }

    static constexpr auto size() noexcept { return c_n_bits_; }

    constexpr bool test(size_t pos) const noexcept
    {
        assert(pos < c_n_bits_);
        return word_(pos) & bit_mask_(pos);
    }

    constexpr bool all() const noexcept
    {
        if (!memory_traits::template const_size_word_cmp<c_word_all_mask_, c_n_words_ - 1>(words_))
            return false;
        return hi_word_() == c_hi_word_all_mask_;
    }

    constexpr bool any() const noexcept
    {
        if (!memory_traits::template const_size_word_cmp<c_word_none_mask_, c_n_words_>(words_))
            return true;
        return false;
    }

    constexpr bool none() const noexcept { return !any(); }

    constexpr size_t count() const noexcept
    {
        size_t res{};
        for (auto w : words_)
            res += std::popcount(w);
        return res;
    }

    auto operator~() const noexcept
    {
        return bitset<Size, TWord, TMemTraits>(*this).flip();
    }

    constexpr bool operator==(const bitset<Size, TWord, TMemTraits> &other) const noexcept
    {
        return memory_traits::template const_size_cmp<c_n_words_>(words_, other.words_);
    }

    auto & operator&=(const bitset<Size, TWord, TMemTraits> &other) noexcept
    {
        memory_traits::template const_size_bin_op<c_n_words_>(words_, words_, other.words_,
                                                              [](auto v1, auto v2) { return v1 & v2; });
        return *this;
    }

    auto & operator|=(const bitset<Size, TWord, TMemTraits> &other) noexcept
    {
        memory_traits::template const_size_bin_op<c_n_words_>(words_, words_, other.words_,
                                                   [](auto v1, auto v2) { return v1 | v2; });
        return *this;
    }

    auto & operator^=(const bitset<Size, TWord, TMemTraits> &other) noexcept
    {
        memory_traits::template const_size_bin_op<c_n_words_>(words_, words_, other.words_,
                                                   [](auto v1, auto v2) { return v1 ^ v2; });
        return *this;
    }

    auto & operator>>=(size_t shift) noexcept
    {
        if (!shift) [[unlikely]] return *this;
        if (shift > c_n_bits_) [[unlikely]]
        {
            reset();
            return *this;
        }

        size_t wshift = shift / c_bits_per_word_;
        size_t bshift = shift % c_bits_per_word_; // local shift
        size_t last_w = c_n_words_ - wshift - 1;  // last word to be processed

        if (!bshift) [[unlikely]]
        {
            for (size_t i = 0; i < last_w; ++i)
                words_[i] = words_[last_w + wshift];
        }

        size_t bshift_opposite = c_bits_per_word_ - bshift;
        for (size_t i = 0; i < last_w; ++i)
        {
            words_[i] = (words_[i + wshift] >> bshift) |
                        (words_[i + wshift + 1] << bshift_opposite);
        }
        words_[last_w] = words_[c_n_words_ - 1] >> bshift;
        memory_traits::template word_set<c_word_none_mask_>(words_ + last_w + 1, c_n_words_ - last_w - 1);

        // no need to sanitize
        return *this;
    }

    auto & operator<<=(size_t shift) noexcept
    {
        if (!shift) [[unlikely]] return *this;
        if (shift > c_n_bits_) [[unlikely]]
        {
            reset();
            return *this;
        }

        size_t wshift = shift / c_bits_per_word_;
        size_t bshift = shift % c_bits_per_word_; // local shift
        size_t last_w = c_n_words_ - wshift - 1;  // last word to be processed

        if (!bshift) [[unlikely]]
        {
            for (size_t i = c_n_words_ - 1; i >= wshift; --i)
                words_[i] = words_[last_w - wshift];
        }

        size_t bshift_opposite = c_bits_per_word_ - bshift;
        for (size_t i = c_n_words_ - 1; i > wshift ; --i)
        {
            words_[i] = (words_[i - wshift] << bshift) |
                        (words_[i - wshift - 1] >> bshift_opposite);
        }
        words_[wshift] = words_[0] << bshift;
        memory_traits::template word_set<c_word_none_mask_>(words_, wshift);

        hi_word_() &= c_hi_word_all_mask_;
        return *this;
    }

    auto & set() noexcept
    {
        memory_traits::template const_size_word_set<c_word_all_mask_, c_n_words_>(words_);
        hi_word_() &= c_hi_word_all_mask_;
        return *this;
    }

    auto & set(size_t pos, bool val = true) noexcept
    {
        assert(pos < c_n_bits_);
        if (val)
            word_(pos) |= bit_mask_(pos);
        else
            word_(pos) &= ~bit_mask_(pos);
        return *this;
    }

    auto & reset() noexcept
    {
        memory_traits::template const_size_word_set<c_word_none_mask_, c_n_words_>(words_);
        return *this;
    }

    auto & reset(size_t pos) noexcept
    {
        word_(pos) &= ~bit_mask_(pos);
        return *this;
    }

    auto & flip() noexcept
    {
        memory_traits::template const_size_apply<c_n_words_>(words_, [](auto v) { return ~v; });
        hi_word_() &= c_hi_word_all_mask_;
        return *this;
    }

    template <size_t Sz, typename TW, template <typename> typename TMT>
    friend bitset<Sz, TW, TMT> operator&(const bitset<Sz, TW, TMT> &lhv,
                                         const bitset<Sz, TW, TMT> &rhv) noexcept
    {
        bitset tmp(lhv);
        tmp &= rhv;
        return tmp;
    }

    template <size_t Sz, typename TW, template <typename> typename TMT>
    friend bitset<Sz, TW, TMT> operator|(const bitset<Sz, TW, TMT> &lhv,
                                         const bitset<Sz, TW, TMT> &rhv) noexcept
    {
        bitset tmp(lhv);
        tmp |= rhv;
        return tmp;
    }

    template <size_t Sz, typename TW, template <typename> typename TMT>
    friend bitset<Sz, TW, TMT> operator^(const bitset<Sz, TW, TMT> &lhv,
                                         const bitset<Sz, TW, TMT> &rhv) noexcept
    {
        bitset tmp(lhv);
        tmp ^= rhv;
        return tmp;
    }

    template <size_t Sz, typename TW, template <typename> typename TMT>
    friend bitset<Sz, TW, TMT> operator>>(const bitset<Sz, TW, TMT> &v, size_t shift) noexcept
    {
        bitset tmp(shift);
        tmp >>= shift;
        return tmp;
    }

    template <size_t Sz, typename TW, template <typename> typename TMT>
    friend bitset<Sz, TW, TMT> operator<<(const bitset<Sz, TW, TMT> &v, size_t shift) noexcept
    {
        bitset tmp(shift);
        tmp <<= shift;
        return tmp;
    }
};

}

#endif // header guard
