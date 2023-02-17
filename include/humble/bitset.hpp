#ifndef LIBHUMBLE_CPP_BITSET_HPP_
#define LIBHUMBLE_CPP_BITSET_HPP_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <bit>
#include <concepts>
#include <limits>
#include <type_traits>

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

template <size_t Size, typename TWord>
    requires detail::BitsetWordConcept<TWord>
class bitset
{
    template <size_t OtherBitSize, typename TWordOther>
    friend class bitset;

    static constexpr size_t c_word_size_     = sizeof(TWord);
    static constexpr size_t c_n_bits_        = Size;
    static constexpr size_t c_n_bytes_       = utils::div_celling(Size, C::bits_per_byte);
    static constexpr size_t c_n_words_       = utils::div_celling(c_n_bytes_, c_word_size_);
    static constexpr size_t c_bits_per_word_ = c_word_size_ * C::bits_per_byte;

    static constexpr size_t c_word_none_mask_       = static_cast<TWord>(0);
    static constexpr size_t c_word_all_mask_        = c_word_none_mask_;
    static constexpr size_t c_hi_word_all_mask_     = c_word_all_mask_ >> (c_n_words_ * c_bits_per_word_ - c_n_bits_);

    constexpr size_t        word_idx_(size_t pos) const noexcept { return pos / c_bits_per_word_; }
    constexpr TWord &       word_(size_t pos)           noexcept { return words_[word_idx_(pos)]; }
    constexpr const TWord & word_(size_t pos)     const noexcept { return words_[word_idx_(pos)]; }
    constexpr TWord &       hi_word_()                  noexcept { return words_[c_n_words_ - 1]; }
    constexpr const TWord & hi_word_()            const noexcept { return words_[c_n_words_ - 1]; }

    static constexpr size_t bit_idx_(size_t pos)        noexcept { return pos % c_bits_per_word_; }
    static constexpr TWord  bit_mask_(size_t pos)       noexcept { return static_cast<TWord>(1) << bit_idx_(pos); }

    TWord words_[c_n_words_]{};

public:
    bitset() = default;
    bitset(TWord val) : words_{val}
    {
        if constexpr (c_n_bits_ < c_bits_per_word_)
        {
            hi_word_() &= c_hi_word_all_mask_;
        }
    }

    static constexpr bool is_optimized() { return false; }

    static constexpr bool size()  noexcept { return c_n_bits_; }

    constexpr bool test(size_t pos) const noexcept
    {
        assert(pos < c_n_bits_);
        return word_(pos) & bit_mask_(pos);
    }

    constexpr bool all() const noexcept
    {
        for (size_t wi{}; wi <= c_n_words_ - 1; ++wi)
            if (words_[wi] != c_word_all_mask_)
                return false;
        return hi_word_() == c_hi_word_all_mask_;
    }

    constexpr bool any() const noexcept
    {
        for (size_t wi{}; wi <= c_n_words_; ++wi)
            if (words_[wi] != c_word_none_mask_)
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

    template <typename TWordOther>
    constexpr bool operator==(const bitset<Size, TWordOther> &bs) const noexcept
    {
        // constexpr size_t ws_ratio = bs.c_word_size_ / c_word_size_;
        // size_t i1 = 0;
        // // the other has larger word
        // for (size_t i2 = 0; i2 < bs.c_n_words_ - 1; ++i2, i1 += ws_ratio )
        // {
        //     // hope it unrolls
        //     auto w2 = bs.words_[i2];
        //     for (size_t i = 0; i < ws_ratio; ++i)
        //     {
        //         if ( (w2 >> i) & ~static_cast<TWord>(0) != word_[i1 + i])
        //             return false;
        //     }
        // }

        // // compere hi bits
        // auto w2 = bs.word_[bs.c_n_words_ - 1];
        // for (size_t i = 0; i1 < c_n_bits_; ++i1, ++i)
        // {
        //     if ( (w2 >> i) & ~static_cast<TWord>(0) != word_[i1 + i])
        //         return false;
        // }
        return true;
    }

    template <typename TWordOther>
        requires detail::BitsetOptimizedParamsConcept<Size, TWordOther>
    constexpr bool operator==(const bitset<Size, TWordOther> &bs)
    {
        // printf("val2\n");
        // the other one TBaseType is definitely longer
        // static_assert(sizeof(TWordOther) % c_word_size_ == 0);
        // printf("sizeof1 = %lu arr_sizeof = %lu sizeof2 = %lu\n", c_word_size_, sizeof(words_), sizeof(TWordOther));
        // printf("v1[0] = %lu v1[1] = %lu v2 = %lu\n", words_[0], words_[1], bs.val_);
        // for (auto other = bs.val_; const auto &v : words_)
        // {
        //     printf("this = %u, other = %u\n", v, static_cast<TWord>(other));
        //     if (v != static_cast<TWord>(other))
        //         return false;
        //     other >>= c_bits_per_word_;
        // }
        // printf("ok\n");
        return true;
    }

    constexpr auto & set() noexcept
    {
        std::fill(words_, words_ + c_n_words_, c_word_all_mask_);
        hi_word_() &= c_hi_word_all_mask_;
        return *this;
    }

    constexpr auto & set(size_t pos, bool val = true) noexcept
    {
        assert(pos < c_n_bits_);
        if (val)
            word_(pos) |= bit_mask_(pos);
        else
            word_(pos) &= ~bit_mask_(pos);
        return *this;
    }

    constexpr auto & reset() noexcept
    {
        std::fill(words_, words_ + c_n_words_, c_word_none_mask_);
        return *this;
    }

    constexpr auto & reset(size_t pos) noexcept
    {
        word_(pos) &= ~bit_mask_(pos);
        return *this;
    }

    constexpr auto & flip() noexcept
    {
        for (auto &w : words_)
            w = ~w;
        hi_word_() &= c_hi_word_all_mask_;
        return *this;
    }
};

/// @brief A class specialization when all bits of @p Size fully fits @p BaseType
template <size_t Size, typename TWord>
    requires detail::BitsetOptimizedParamsConcept<Size, TWord>
class bitset<Size, TWord>
{
    template <size_t OtherBitSize, typename TWordOther>
    friend class bitset;

    static constexpr size_t    c_base_digits_ = std::numeric_limits<TWord>::digits;
    static constexpr TWord c_one_bit_     = 0b1;
    static constexpr TWord c_null_set_    = 0;
    static constexpr TWord c_full_set_    = std::numeric_limits<TWord>::max() >> (c_base_digits_ - Size);

    TWord val_{c_null_set_};

public:
    // bitset() { printf("Special bitset\n"); }
    constexpr bitset() noexcept = default;
    constexpr bitset(TWord val) noexcept : val_{val} {}

    static constexpr bool is_optimized() { return true; }

    static constexpr bool size()  noexcept { return Size; }

    constexpr bool   test(size_t pos) const noexcept { return val_ & (c_one_bit_ << pos); }

    constexpr size_t count() const noexcept { return std::popcount(val_); }
    constexpr bool   all()   const noexcept { return count() == Size; }
    constexpr bool   any()   const noexcept { return count(); }
    constexpr bool   none()  const noexcept { return !count(); }


    constexpr bitset &set()   noexcept { val_  = c_full_set_; return *this; }
    constexpr bitset &reset() noexcept { val_  = c_null_set_; return *this; }
    constexpr bitset &flip()  noexcept { val_ ^= c_full_set_; return *this; }

    constexpr bitset &set  (size_t pos) noexcept { val_ |=  (c_one_bit_ << pos); return *this; }
    constexpr bitset &reset(size_t pos) noexcept { val_ &= ~(c_one_bit_ << pos); return *this; }
    constexpr bitset &flip (size_t pos) noexcept { val_ ^=  (c_one_bit_ << pos); return *this; }

    /// The most optimized comparison method
    template <typename TWordOther>
        requires detail::BitsetOptimizedParamsConcept<Size, TWordOther>
    constexpr bool operator==(const bitset<Size, TWordOther> &bs)
    {
        using common_type = std::common_type_t<TWord, TWordOther>;
        return static_cast<common_type>(val_) == static_cast<common_type>(bs.val_);
    }

};

}

#endif // header guard
