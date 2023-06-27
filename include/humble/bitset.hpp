#ifndef LIBHUMBLE_CPP_BITSET_HPP_
#define LIBHUMBLE_CPP_BITSET_HPP_

#include <cstddef>
#include <cstdint>
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
concept CBitsetWordConcept = std::unsigned_integral<TWord>;

template <size_t kSize, typename TWord>
concept CBitsetOptimizedParamsConcept = CBitsetWordConcept<TWord>
                                     && kSize <= std::numeric_limits<TWord>::digits;

} // namespace detail

template <size_t kSize, typename TWord = uint64_t,
          template <typename> typename TMemTraits = detail::StaticMemoryTraits>
    requires detail::CBitsetWordConcept<TWord>
class Bitset
{
    using MemoryTraits = TMemTraits<TWord>;

    static constexpr size_t kWordSize     = sizeof(TWord);
    static constexpr size_t kNBits        = kSize;
    static constexpr size_t kNBytes       = utils::div_celling(kSize, K::kBitsPerByte);
    static constexpr size_t kNWords       = utils::div_celling(kNBytes, kWordSize);
    static constexpr size_t kBitsPerWord  = kWordSize * K::kBitsPerByte;

    static constexpr TWord kWordNoneMask  = static_cast<TWord>(0);
    static constexpr TWord kWordAllMask   = ~kWordNoneMask;
    static constexpr TWord kHiWordAllMask = kWordAllMask >> (kNWords * kBitsPerWord - kNBits);

    constexpr size_t        word_idx_(size_t pos) const noexcept { return pos / kBitsPerWord; }
    constexpr TWord &       word_(size_t pos)           noexcept { return words_[word_idx_(pos)]; }
    constexpr const TWord & word_(size_t pos)     const noexcept { return words_[word_idx_(pos)]; }
    constexpr TWord &       hi_word_()                  noexcept { return words_[kNWords - 1]; }
    constexpr const TWord & hi_word_()            const noexcept { return words_[kNWords - 1]; }

    static constexpr size_t bit_idx_(size_t pos)        noexcept { return pos % kBitsPerWord; }
    static constexpr TWord  bit_mask_(size_t pos)       noexcept { return TWord(1) << bit_idx_(pos); }

    TWord words_[kNWords]{};

public:
    constexpr Bitset() = default;
    constexpr Bitset(TWord val) : words_{val}
    {
        if constexpr (kNBits < kBitsPerWord)
        {
            hi_word_() &= kHiWordAllMask;
        }
    }

    static constexpr auto size() noexcept { return kNBits; }

    constexpr bool test(size_t pos) const noexcept
    {
        assert(pos < kNBits);
        return word_(pos) & bit_mask_(pos);
    }

    constexpr bool all() const noexcept
    {
        if (!MemoryTraits::template const_size_word_eq<kWordAllMask, kNWords - 1>(words_))
            return false;
        return hi_word_() == kHiWordAllMask;
    }

    constexpr bool any() const noexcept
    {
        if (!MemoryTraits::template const_size_word_eq<kWordNoneMask, kNWords>(words_))
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
        return Bitset<kSize, TWord, TMemTraits>(*this).flip();
    }

    constexpr bool operator==(const Bitset<kSize, TWord, TMemTraits> &other) const noexcept
    {
        return MemoryTraits::template const_size_eq<kNWords>(words_, other.words_);
    }

    auto & operator&=(const Bitset<kSize, TWord, TMemTraits> &other) noexcept
    {
        MemoryTraits::template const_size_bin_op<kNWords>(words_, words_, other.words_,
                                                          [](auto v1, auto v2) { return v1 & v2; });
        return *this;
    }

    auto & operator|=(const Bitset<kSize, TWord, TMemTraits> &other) noexcept
    {
        MemoryTraits::template const_size_bin_op<kNWords>(words_, words_, other.words_,
                                                          [](auto v1, auto v2) { return v1 | v2; });
        return *this;
    }

    auto & operator^=(const Bitset<kSize, TWord, TMemTraits> &other) noexcept
    {
        MemoryTraits::template const_size_bin_op<kNWords>(words_, words_, other.words_,
                                                          [](auto v1, auto v2) { return v1 ^ v2; });
        return *this;
    }

    auto & operator>>=(size_t shift) noexcept
    {
        if (!shift) [[unlikely]] return *this;
        if (shift > kNBits) [[unlikely]]
        {
            reset();
            return *this;
        }

        size_t wshift = shift / kBitsPerWord;
        size_t bshift = shift % kBitsPerWord; // local shift
        size_t last_w = kNWords - wshift - 1; // last word to be processed

        if (!bshift) [[unlikely]]
        {
            for (size_t i = 0; i < last_w; ++i)
                words_[i] = words_[last_w + wshift];
        }

        size_t bshift_opposite = kBitsPerWord - bshift;
        for (size_t i = 0; i < last_w; ++i)
        {
            words_[i] = (words_[i + wshift] >> bshift) |
                        (words_[i + wshift + 1] << bshift_opposite);
        }
        words_[last_w] = words_[kNWords - 1] >> bshift;
        MemoryTraits::template word_set<kWordNoneMask>(words_ + last_w + 1, kNWords - last_w - 1);

        // no need to sanitize
        return *this;
    }

    auto & operator<<=(size_t shift) noexcept
    {
        if (!shift) [[unlikely]] return *this;
        if (shift > kNBits) [[unlikely]]
        {
            reset();
            return *this;
        }

        size_t wshift = shift / kBitsPerWord;
        size_t bshift = shift % kBitsPerWord; // local shift
        size_t last_w = kNWords - wshift - 1; // last word to be processed

        if (!bshift) [[unlikely]]
        {
            for (size_t i = kNWords - 1; i >= wshift; --i)
                words_[i] = words_[last_w - wshift];
        }

        size_t bshift_opposite = kBitsPerWord - bshift;
        for (size_t i = kNWords - 1; i > wshift ; --i)
        {
            words_[i] = (words_[i - wshift] << bshift) |
                        (words_[i - wshift - 1] >> bshift_opposite);
        }
        words_[wshift] = words_[0] << bshift;
        MemoryTraits::template word_set<kWordNoneMask>(words_, wshift);

        hi_word_() &= kHiWordAllMask;
        return *this;
    }

    auto & set() noexcept
    {
        MemoryTraits::template const_size_word_set<kWordAllMask, kNWords>(words_);
        hi_word_() &= kHiWordAllMask;
        return *this;
    }

    auto & set(size_t pos, bool val = true) noexcept
    {
        assert(pos < kNBits);
        if (val)
            word_(pos) |= bit_mask_(pos);
        else
            word_(pos) &= ~bit_mask_(pos);
        return *this;
    }

    auto & reset() noexcept
    {
        MemoryTraits::template const_size_word_set<kWordNoneMask, kNWords>(words_);
        return *this;
    }

    auto & reset(size_t pos) noexcept
    {
        word_(pos) &= ~bit_mask_(pos);
        return *this;
    }

    auto & flip() noexcept
    {
        MemoryTraits::template const_size_apply<kNWords>(words_, [](auto v) { return ~v; });
        hi_word_() &= kHiWordAllMask;
        return *this;
    }

    template <size_t Sz, typename TW, template <typename> typename TMT>
    friend Bitset<Sz, TW, TMT> operator&(const Bitset<Sz, TW, TMT> &lhv,
                                         const Bitset<Sz, TW, TMT> &rhv) noexcept
    {
        Bitset tmp(lhv);
        tmp &= rhv;
        return tmp;
    }

    template <size_t Sz, typename TW, template <typename> typename TMT>
    friend Bitset<Sz, TW, TMT> operator|(const Bitset<Sz, TW, TMT> &lhv,
                                         const Bitset<Sz, TW, TMT> &rhv) noexcept
    {
        Bitset tmp(lhv);
        tmp |= rhv;
        return tmp;
    }

    template <size_t Sz, typename TW, template <typename> typename TMT>
    friend Bitset<Sz, TW, TMT> operator^(const Bitset<Sz, TW, TMT> &lhv,
                                         const Bitset<Sz, TW, TMT> &rhv) noexcept
    {
        Bitset tmp(lhv);
        tmp ^= rhv;
        return tmp;
    }

    template <size_t Sz, typename TW, template <typename> typename TMT>
    friend Bitset<Sz, TW, TMT> operator>>(const Bitset<Sz, TW, TMT> &v, size_t shift) noexcept
    {
        Bitset tmp(shift);
        tmp >>= shift;
        return tmp;
    }

    template <size_t Sz, typename TW, template <typename> typename TMT>
    friend Bitset<Sz, TW, TMT> operator<<(const Bitset<Sz, TW, TMT> &v, size_t shift) noexcept
    {
        Bitset tmp(shift);
        tmp <<= shift;
        return tmp;
    }
};

}

#endif // header guard
