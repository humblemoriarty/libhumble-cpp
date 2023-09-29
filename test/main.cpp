#include "humble/bitset.hpp"
#include "humble/sparse_dynamic_bitset.hpp"
#include "humble/posix/aligned_allocator.h"

#include <iostream>
#include <bitset>
#include <cassert>
#include <cstdlib>

#include <vector>

template <hmbl::posix::CAlignedAllocator TAlloc>
class AlignedAllocatorUser
{
};

int main()
{
    hmbl::Bitset<999> hmbl_b;

    assert(hmbl_b.size() == 999);
    assert(hmbl_b.count() == 0);
    assert(!hmbl_b.any());
    assert(!hmbl_b.all());
    assert(hmbl_b.none());

    assert(hmbl_b.set(100).test(100));
    assert(hmbl_b.count() == 1);
    assert(hmbl_b.any());
    assert(!hmbl_b.none());
    assert(!hmbl_b.flip().test(100));
    assert(hmbl_b.count() == 998);

    assert(hmbl_b.reset().count() == 0);
    assert(!hmbl_b.any());
    assert(!hmbl_b.all());
    assert(hmbl_b.none());
    assert(hmbl_b.set(125).set(126).set(127).count() == 3);

    hmbl::Bitset<999> hmbl_b1;
    assert(hmbl_b1.set(125).set(126).set(127).count() == 3);
    assert(hmbl_b == hmbl_b1);
    assert((hmbl_b1 >>= 2).count() == 3);
    assert((hmbl_b1 & hmbl_b).count() == 1);
    assert((hmbl_b1 | hmbl_b).count() == 5);

    using DBitset = hmbl::SparseDynamicBitset<hmbl::posix::AlignedAllocator<uint64_t, 64>>;
    size_t bits1[] = {65, 111, 555, 1'000'000};
    size_t bits2[] = {10, 132, 792, 5555, 1'000'000};
    size_t bits3[] = {1, 554, 8190};
    DBitset db1(bits1, 2'000'000);
    DBitset db2(bits2, 2'000'000);
    DBitset db3(bits3, 2'000'000);
    DBitset db4(bits3, 2'000'000);
    DBitset db5(bits3, 2'000'000);
    DBitset db6(bits3, 2'000'000);
    DBitset db7(bits3, 2'000'000);
    DBitset db8(bits3, 2'000'000);
    // DBitset db3(bits2, 8192);
    // DBitset db4(bits2, 8192);
    // DBitset db5(bits2, 8192);
    // DBitset db6(bits2, 8192);
    // DBitset db7(bits2, 8192);
    // DBitset db8(bits2, 8192);
    // DBitset db9(bits2, 8192);
    // DBitset db10(bits2, 8192);

    // using DBitset2 = hmbl::SparseDynamicBitset<hmbl::posix::AlignedAllocator<uint64_t, 64>>;
    // DBitset2 db2_1(bits1, 8192);
    // DBitset2 db2_2(bits2, 8192);

    DBitset  const *dyn_bitsets[]  = {&db1, &db2, &db3, &db4, &db5, &db6, &db7, &db8}; //&db3, &db4, &db5, &db6, &db7, &db8, &db9, &db10};
    // DBitset2 const *dyn_bitsets2[] = {&db2_2, &db2_1};

    auto res = DBitset::and_any(dyn_bitsets);
    // auto res2 = DBitset::and_any(dyn_bitsets2);

    printf("res = %lu sizeof(__m512i) = %lu bitset<128> = %lu\n", res.value_or(0), sizeof(__m512i), sizeof(std::bitset<128>));
    // printf("res = %lu sizeof(__m512i) = %lu bitset<128> = %lu\n", res2.value_or(0), sizeof(__m512i), sizeof(std::bitset<128>));

    // hmbl::posix::AlignedAllocator<int, 64> alloc;
    // AlignedAllocatorUser<hmbl::posix::AlignedAllocator<int, 32>> u;
    // int *I = std::assume_aligned<hmbl::posix::AlignedAllocator<int, 64>::alignment()>(alloc.allocate(100));

    // hmbl::posix::AlignedAllocator<unsigned, 32> alloc;

    // size_t nn;
    // scanf("%lu", &nn);
    // unsigned *a = alloc.allocate(nn);
    // unsigned *b = alloc.allocate(nn);

    // for (unsigned i = 0; i < nn; ++i)
    // {
    //     a[i] = rand();
    //     b[i] = a[i];//rand();
    // }

    // std::vector<int> q;

    // // size_t n = nn;
    // // const std::byte *p1 = std::assume_aligned<32>(reinterpret_cast<const std::byte*>(a));
    // // const std::byte *p2 = std::assume_aligned<32>(reinterpret_cast<const std::byte*>(b));
    // // for (; n > 0; --n, ++p1, ++p2)
    // // {
    // //     if (*p1 != *p2) return 1;
    // // }

    // printf("equal = %i\n", hmbl::detail::simd_mem_equal<32,32>(a, b, nn));
    return 0;
}