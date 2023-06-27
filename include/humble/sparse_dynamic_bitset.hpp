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
    #define LOOP_FULL_UNROLL _Pragma("clang loop unroll(full)")
#elif defined(__GNUC__)
    #define LOOP_FULL_UNROLL _Pragma("GCC unroll 65534")
#endif

namespace hmbl
{

#if defined(__AVX512F__) && defined(__AVX512VL__)

template <typename TAllocator = posix::AlignedAllocator<uint64_t, 64>> // aligned for AVX512
class SparseDynamicBitset
{
    using Word         = uint64_t;
    using CompressMask = __mmask8; // one bit masks one word

    using CompressMaskAlloc = typename TAllocator::template rebind<CompressMask>::other;
    using WordsAlloc        = typename TAllocator::template rebind<Word>::other;

    static constexpr size_t kVectorBitSize  = 512;
    static constexpr size_t kVectorByteSize = kVectorBitSize / K::kBitsPerByte; // 64 bytes
    static constexpr size_t kWordBitSize    = sizeof(Word) * K::kBitsPerByte;
    static constexpr size_t kWordsPackSize  = kVectorByteSize / sizeof(Word); // 8 words per pack
    static constexpr size_t kCompressMaskPackSize = kVectorByteSize / sizeof(CompressMask); // 64 masks per pack

    struct CompressMaskHolder
    {
        std::vector<CompressMask, CompressMaskAlloc> mem;

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
            std::memset(std::data(mem), 0, sizeof(CompressMask) * mem_size);

            for (auto pos : poses)
                set_bit(pos, true);
        }

        auto popcount() noexcept
        {
            size_t res{};
            for (size_t i = 0; i < size(); ++i)
                res += std::popcount(mem[i]);
            return res;
        }

        auto size() const noexcept { return std::size(mem); }

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
            std::memset(std::data(mem), 0, sizeof(Word) * mem_size);
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
    SparseDynamicBitset(TPoses &&poses, size_t bit_size)
        : bit_size_{bit_size}
        , mask_(poses, bit_size)
        , words_(poses, mask_.popcount())
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
    LOOP_FULL_UNROLL for (size_t op_i = 0; op_i < kNOperands; ++op_i)
        op_words[op_i] = operands[op_i]->words_.data();

    auto msize = operands[0]->mask_.size(); // MUST be equal for all operands
    for (size_t mask_i = 0; mask_i < msize; )
    {
        // check if at least one mask has bits set
        __m512i packed_mask = _mm512_setzero_epi32();
        LOOP_FULL_UNROLL for (size_t op_i = 0; op_i < kNOperands; ++op_i)
        {
            const auto &op = *operands[op_i];
            assert(op.mask_.size() == msize);
            const CompressMask *mask_p = &(op.mask_.data()[mask_i]);
            assert(!(std::intptr_t(mask_p) % kVectorByteSize));
            __m512i op_packed_mask = _mm512_load_epi64(mask_p);
            packed_mask = _mm512_or_epi64(packed_mask, op_packed_mask);
        }
        if (!_mm512_reduce_or_epi64(packed_mask)) [[likely]]
        {
            mask_i += kCompressMaskPackSize;
            continue;
        }

        // if at least one non-zero mask found check bytes
        for (; mask_i < mask_i + kCompressMaskPackSize && mask_i < msize; ++mask_i)
        {
            __m512i packed_data = _mm512_set1_epi8(0xFF); // AND result
            LOOP_FULL_UNROLL for (size_t op_i = 0; op_i < kNOperands; ++op_i)
            {
                const auto &op = *operands[op_i];

                CompressMask mask   = op.mask_.data()[mask_i];
                Word const *&word_p = op_words[op_i];

                if (!mask)
                {
                    packed_data = _mm512_set1_epi8(0x00);
                    continue;
                }

                __m512i op_packed_data = _mm512_maskz_expandloadu_epi64(mask, word_p);
                packed_data = _mm512_and_epi64(packed_data, op_packed_data);

                word_p += std::popcount(mask);
            }

            if (_mm512_reduce_or_epi64(packed_data))
            {
                // result is found
                alignas(kVectorByteSize) uint64_t vals[kWordsPackSize];
                _mm512_store_epi64(vals, packed_data);
                LOOP_FULL_UNROLL for (size_t word_i = 0; word_i < kWordsPackSize; ++word_i)
                {
                    if (vals[word_i])
                    {
                        return {  mask_i * kVectorBitSize
                                + word_i * kWordBitSize
                                + std::countr_zero(vals[word_i])};
                    }
                }
                assert(0 && "MUST not happen");
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

#endif // arch specs

}


#endif