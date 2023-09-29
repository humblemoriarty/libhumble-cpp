#ifndef LIBHUMBLE_CPP_SPARSE_DYNAMIC_BITSET_H_
#define LIBHUMBLE_CPP_SPARSE_DYNAMIC_BITSET_H_

#include <cassert>
#include <cstdint>
#include <cstring>
#include <concepts>
#include <bit>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <immintrin.h>

#include "posix/aligned_allocator.h"
#include "constants.hpp"
#include "utils.hpp"

#ifdef __clang__
    #define ALWAYS_UNROLL _Pragma("clang loop unroll(full)")
#elif defined(__GNUC__)
    #define ALWAYS_UNROLL _Pragma("GCC unroll 65534")
#endif

namespace hmbl
{

template <size_t kVectorByteSize_, typename TWord, typename TCompressMask, typename TAllocator>
struct SparseDynamicBitsetBase
{
    using Word         = TWord;
    using CompressMask = TCompressMask; // one bit masks one word

    using CompressMaskAlloc = typename TAllocator::template rebind<CompressMask>::other;
    using WordsAlloc        = typename TAllocator::template rebind<Word>::other;

    static constexpr size_t kVectorByteSize = kVectorByteSize_;
    static constexpr size_t kVectorBitSize  = kVectorByteSize * K::kBitsPerByte;

    static constexpr size_t kWordByteSize      = sizeof(Word);
    static constexpr size_t kWordBitSize       = kWordByteSize * K::kBitsPerByte;
    static constexpr size_t kWordPackByteSize  = kVectorByteSize / kWordByteSize;

    static constexpr size_t kCompressMaskByteSize     = sizeof(CompressMask);
    static constexpr size_t kCompressMaskBitSize      = kCompressMaskByteSize * K::kBitsPerByte;
    static constexpr size_t kCompressMaskPackByteSize = kVectorByteSize / kCompressMaskByteSize;

    struct CompressMaskHolder
    {
        using WordOffset = unsigned;
        std::vector<CompressMask, CompressMaskAlloc> mem;
        std::vector<WordOffset>                      offsets; // compressed words per mask pack to improve skipping

        // takes original pos
        void set_bit(size_t pos, bool v) noexcept
        {
            auto mask_i = pos / kVectorBitSize;
            auto bit_i  = (pos - mask_i * kVectorBitSize) / kWordBitSize;
            if (v)
                mem[mask_i] |= CompressMask(1) << bit_i;
            else
                mem[mask_i] &= ~(CompressMask(1) << bit_i);
        }

        template <typename TPoses>
        CompressMaskHolder(TPoses &poses, size_t bit_size)
        {
            size_t size     = utils::div_celling(bit_size, kVectorBitSize);
            size_t mem_size = utils::align_up<size_t, kVectorByteSize>(size);
            mem.reserve(mem_size);
            mem.resize(size);
            assert(!(mem_size % kCompressMaskPackByteSize));
            offsets.resize(mem_size / kCompressMaskPackByteSize);
            std::memset(std::data(mem), 0, kCompressMaskByteSize * mem_size);
            std::memset(std::data(offsets), 0, sizeof(WordOffset) * std::size(offsets));

            for (auto pos : poses)
                set_bit(pos, true);

            // fill pack offset
            for(size_t mi = 0, pi = 0; mi < std::size(mem);)
            {
                assert(pi < size_packs());
                offsets[pi] += std::popcount(mem[mi]);
                if (!(++mi % kCompressMaskPackByteSize)) ++pi;
            }
        }

        auto popcount() noexcept
        {
            size_t res{};
            for (size_t i = 0; i < size(); ++i)
                res += std::popcount(mem[i]);
            return res;
        }

        auto size() const noexcept       { return std::size(mem); }
        auto size_packs() const noexcept { return std::size(offsets); } // get number of mask packs

        const auto *data() const noexcept { return std::data(mem); }
        auto       *data() noexcept       { return std::data(mem); }
    };

    struct WordsHolder
    {
        std::vector<Word, WordsAlloc> mem;

        template <typename TPoses>
        WordsHolder(TPoses &poses, size_t size)
        {
            size_t mem_size = utils::align_up<size_t, kVectorByteSize>(size);
            mem.reserve(mem_size);
            mem.resize(size);
            std::memset(std::data(mem), 0, kWordByteSize * mem_size);
        }

        auto size() const noexcept { return std::size(mem); }

        const auto *data() const noexcept { return std::data(mem); }
        auto       *data() noexcept       { return std::data(mem); }
    };

    size_t             bit_size_{};
    CompressMaskHolder mask_;
    WordsHolder        words_;

public:
    // init_pos should be sorted
    template <typename TPoses>
    SparseDynamicBitsetBase(const TPoses &poses, size_t bit_size)
        : bit_size_{bit_size}
        , mask_(poses, bit_size)
        , words_(poses, mask_.popcount())
    {
    }
};

#if defined(__AVX512F__) && defined(__AVX512VL__)

namespace detail
{

union Word512
{
    __m512i  vec;
    uint64_t val64[sizeof(__m512i) / sizeof(uint64_t)];
    uint32_t val32[sizeof(__m512i) / sizeof(uint32_t)];
    uint16_t val16[sizeof(__m512i) / sizeof(uint16_t)];
    uint8_t  val8 [sizeof(__m512i) / sizeof(uint8_t)];

    Word512() = default;
    Word512(const __m512i &v) : vec{v} {}
};

}
template <typename TAllocator = posix::AlignedAllocator<uint64_t, 64>> // aligned for AVX512
class SparseDynamicBitset : private SparseDynamicBitsetBase<64, uint32_t, __mmask16, TAllocator>
{
    using Base = SparseDynamicBitsetBase<64, uint32_t, __mmask16, TAllocator>;

    using typename Base::Word;
    using typename Base::CompressMask;

    using Base::kVectorByteSize;
    using Base::kVectorBitSize;

    using Base::kWordBitSize;
    using Base::kWordByteSize;
    using Base::kWordPackByteSize;

    using Base::kCompressMaskByteSize;
    using Base::kCompressMaskBitSize;
    using Base::kCompressMaskPackByteSize;

    static_assert(kCompressMaskBitSize * kWordByteSize == kVectorByteSize); // one mask in mask MUST denote one word in pack

    using Base::bit_size_;
    using Base::mask_;
    using Base::words_;

    using Base::SparseDynamicBitsetBase;

public:
    template <typename TPoses>
    SparseDynamicBitset(const TPoses &poses, size_t bit_size)
        : Base::SparseDynamicBitsetBase(poses, bit_size)
    {
        if (std::empty(poses)) [[unlikely]]
            return;

        size_t prev_word_i{*std::cbegin(poses) / kWordBitSize};
        for (auto *w = words_.data(); auto pos : poses)
        {
            size_t word_i = pos / kWordBitSize;
            assert(word_i >= prev_word_i);
            if (word_i > prev_word_i) ++w;
            *w |= Word(1) << (pos % kWordBitSize);
            prev_word_i = word_i;
        }
    }

    // static extent only to unroll internal cycles
    template <typename TBitsets>
    static std::optional<size_t> and_any(TBitsets &&operands) noexcept
    {
        return and_any_(std::span(std::forward<TBitsets>(operands)));
    }

private:
    template <typename TBitset, size_t kNOperands>
        requires (kNOperands > 0) &&
                 (std::same_as<std::decay_t<TBitset>, SparseDynamicBitset>)
    static std::optional<size_t> and_any_(std::span<TBitset*, kNOperands> operands) noexcept;
};

template <typename TAlloc>
template <typename TBitset, size_t kNOperands>
    requires (kNOperands > 0) &&
             (std::same_as<std::decay_t<TBitset>, SparseDynamicBitset<TAlloc>>)
std::optional<size_t> SparseDynamicBitset<TAlloc>::and_any_(std::span<TBitset*, kNOperands> operands) noexcept
{
    // fill first bit for every
    const Word *op_words[kNOperands];
    ALWAYS_UNROLL for (size_t op_i = 0; op_i < kNOperands; ++op_i)
        op_words[op_i] = operands[op_i]->words_.data();

    auto msize = operands[0]->mask_.size(); // MUST be equal for all operands
    for (size_t mask_i = 0; mask_i < msize; ) // loop by mask packs
    {
        // check if at least one mask has bits set
        __m512i packed_mask = _mm512_set1_epi64(0xFFFFFFFFFFFFFFFF);
        ALWAYS_UNROLL for (size_t op_i = 0; op_i < kNOperands; ++op_i)
        {
            const auto &op = *operands[op_i];
            assert(op.mask_.size() == msize);
            const CompressMask *mask_p = &(op.mask_.data()[mask_i]);
            assert(!(std::intptr_t(mask_p) % kVectorByteSize));
            __m512i op_packed_mask = _mm512_load_epi64(mask_p); // load mask pack
            packed_mask = _mm512_and_epi64(packed_mask, op_packed_mask);
            if (!_mm512_test_epi64_mask(packed_mask, packed_mask))
            {
                // no intersection - skip words
                ALWAYS_UNROLL for (size_t op_i = 0; op_i < kNOperands; ++op_i)
                {
                    op_words[op_i] += operands[op_i]->mask_.offsets[mask_i / kCompressMaskPackByteSize];
                }
                mask_i += kCompressMaskPackByteSize;
                goto outer_loop;
            }
        }

        // if at least one non-zero mask found check bytes
        for (; mask_i < mask_i + kCompressMaskPackByteSize && mask_i < msize; ++mask_i)
        {
            detail::Word512 packed_data;

            packed_data.vec = _mm512_set1_epi64(0xFFFFFFFFFFFFFFFF);
            ALWAYS_UNROLL for (size_t op_i = 0; op_i < kNOperands; ++op_i)
            {
                const auto  &op = *operands[op_i];
                CompressMask mask   = op.mask_.data()[mask_i];

                if (!mask)
                {
                    packed_data.vec = _mm512_setzero_si512();
                    break;
                }

                Word const *word_p = op_words[op_i];

                __m512i op_packed_data = _mm512_maskz_expandloadu_epi32(mask, word_p);
                packed_data.vec = _mm512_and_epi64(packed_data.vec, op_packed_data);
            }

            ALWAYS_UNROLL for (size_t op_i = 0; op_i < kNOperands; ++op_i)
            {
                op_words[op_i] += std::popcount(operands[op_i]->mask_.data()[mask_i]);
            }

            if (_mm512_test_epi64_mask(packed_data.vec, packed_data.vec))
            {
                // result is found
                ALWAYS_UNROLL for (size_t word_i = 0; word_i < kWordPackByteSize; ++word_i)
                {
                    if (packed_data.val32[word_i])
                    {
                        return {  mask_i * kVectorBitSize
                                + word_i * kWordBitSize
                                + std::countr_zero(packed_data.val32[word_i])};
                    }
                }
                assert(0 && "MUST not happen");
                return std::nullopt;
            }
        }
        outer_loop:;
    }
    return std::nullopt;
}

#elif defined(__SSE2__) // end AVX512

namespace detail
{

union Word128
{
    __m128i  vec;
    uint64_t val64[sizeof(vec) / sizeof(uint64_t)];
    uint32_t val32[sizeof(vec) / sizeof(uint32_t)];
    uint16_t val16[sizeof(vec) / sizeof(uint16_t)];
    uint8_t  val8 [sizeof(vec) / sizeof(uint8_t)];

    Word128() = default;
    Word128(const __m128i &v) : vec{v} {}
};

static_assert(sizeof(Word128) == 16);

}

template <typename TAllocator = posix::AlignedAllocator<detail::Word128, 16>>
class SparseDynamicBitset : private SparseDynamicBitsetBase<16, detail::Word128, uint8_t, TAllocator>
{
    using Base = SparseDynamicBitsetBase<16, detail::Word128, uint8_t, TAllocator>;

    using typename Base::Word;
    using typename Base::CompressMask;

    using Base::kVectorByteSize;
    using Base::kVectorBitSize;

    using Base::kWordBitSize;
    using Base::kWordByteSize;
    using Base::kWordPackByteSize;

    using Base::kCompressMaskByteSize;
    using Base::kCompressMaskBitSize;
    using Base::kCompressMaskPackByteSize;

    static constexpr size_t kHalfWordBitSize = kWordBitSize / 2;

    using Base::bit_size_;
    using Base::mask_;
    using Base::words_;

public:
    using Base::SparseDynamicBitsetBase;

    template <typename TPoses>
    SparseDynamicBitset(const TPoses &poses, size_t bit_size)
        : Base::SparseDynamicBitsetBase(poses, bit_size)
    {
        if (std::empty(poses)) [[unlikely]]
            return;

        size_t prev_word_i{*std::cbegin(poses) / kWordBitSize};
        for (auto *w = words_.data(); auto pos : poses)
        {
            size_t word_i = pos / kWordBitSize;
            assert(word_i >= prev_word_i);
            if (word_i > prev_word_i) ++w;

            size_t shift = pos % kWordBitSize;
            if (shift < kHalfWordBitSize)
                w->val64[0] |= uint64_t(1) << shift;
            else
                w->val64[1] |= uint64_t(1) << (shift - kHalfWordBitSize);

            prev_word_i = word_i;
        }
    }

    // static extent only to unroll internal cycles
    template <typename TBitsets>
    static std::optional<size_t> and_any(TBitsets &&operands) noexcept
    {
        return and_any_(std::span(std::forward<TBitsets>(operands)));
    }

private:
    static bool test_all_zeros_vec(const auto &vec) noexcept
    {
#ifdef __SSE4_1__
        return !!_mm_test_all_zeros(vec, vec);
#else
        static const detail::Word128 kZeroWord{_mm_setzero_si128()};
        const auto &word = *reinterpret_cast<const detail::Word128*>(&vec);
        return !std::memcmp(word.val64, kZeroWord.val64, sizeof(word.val64));
#endif
    }

    static void set_all_ones_vec(auto &vec) noexcept
    {
#ifdef __SSE4_1__
        vec = _mm_cmpeq_epi64(vec, vec);
#else
        static const __m128i kOnesVec = _mm_set1_epi8(0xFF);
        vec = _mm_move_epi64(kOnesVec);
#endif
    }

    template <typename TBitset, size_t kNOperands>
        requires (kNOperands > 0) &&
                 (std::same_as<std::decay_t<TBitset>, SparseDynamicBitset>)
    static std::optional<size_t> and_any_(std::span<TBitset*, kNOperands> operands) noexcept;
};

template <typename TAlloc>
template <typename TBitset, size_t kNOperands>
    requires (kNOperands > 0) &&
             (std::same_as<std::decay_t<TBitset>, SparseDynamicBitset<TAlloc>>)
std::optional<size_t> SparseDynamicBitset<TAlloc>::and_any_(std::span<TBitset*, kNOperands> operands) noexcept
{
    // fill first bit for every
    const Word *op_words[kNOperands];
    ALWAYS_UNROLL for (size_t op_i = 0; op_i < kNOperands; ++op_i)
        op_words[op_i] = operands[op_i]->words_.data();

    auto msize = operands[0]->mask_.size(); // MUST be equal for all operands
    for (size_t mask_i = 0; mask_i < msize; )
    {
        // check if at least one mask has bits set
        __m128i packed_mask = _mm_setzero_si128();
        ALWAYS_UNROLL for (size_t op_i = 0; op_i < kNOperands; ++op_i)
        {
            const auto &op = *operands[op_i];
            assert(op.mask_.size() == msize);
            const CompressMask *mask_p = &(op.mask_.data()[mask_i]);
            assert(!(std::intptr_t(mask_p) % kVectorByteSize));
            __m128i op_packed_mask = _mm_load_si128(reinterpret_cast<const __m128i*>(mask_p));
            packed_mask = _mm_or_si128(packed_mask, op_packed_mask);
        }
        if (test_all_zeros_vec(packed_mask)) [[likely]]
        {
            mask_i += kCompressMaskPackByteSize;
            continue;
        }

        // if at least one non-zero mask found check bytes
        for (; mask_i < mask_i + kCompressMaskPackByteSize && mask_i < msize; ++mask_i)
        {
            detail::Word128 packed_data;
            set_all_ones_vec(packed_data.vec);
            ALWAYS_UNROLL for (size_t op_i = 0; op_i < kNOperands; ++op_i)
            {
                const auto &op    = *operands[op_i];
                CompressMask mask = op.mask_.data()[mask_i];

                if (!mask) [[likely]]
                {
                    packed_data.vec = _mm_setzero_si128();
                    break;
                }

                Word const *word_p = op_words[op_i];
                ALWAYS_UNROLL for (; mask; mask >>= 1)
                {
                    if (mask & 0x1) [[unlikely]]
                    {
                        packed_data.vec = _mm_and_si128(packed_data.vec, word_p->vec);
                        ++word_p;
                    }
                }
            }

            // advance all offsets
            ALWAYS_UNROLL for (size_t op_i = 0; op_i < kNOperands; ++op_i)
            {
                op_words[op_i] += std::popcount(operands[op_i]->mask_.data()[mask_i]);
            }

            if (!test_all_zeros_vec(packed_data.vec))
            {
                // result is found
                ALWAYS_UNROLL for (size_t word_i = 0; word_i < kWordByteSize / sizeof(uint64_t); ++word_i)
                {
                    if (packed_data.val64[word_i])
                    {
                        return {  mask_i * kVectorBitSize
                                + word_i * kWordBitSize
                                + std::countr_zero(packed_data.val64[word_i])}; // TODO
                    }
                }
                assert(0 && "MUST not happen");
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}
#else // arch end SSE2
#error "Architecture isn't supported"
#endif // end arch

}


#endif