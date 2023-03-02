#include "humble/bitset.hpp"

#include <iostream>
#include <bitset>
#include <cassert>

int main()
{
    hmbl::bitset<999> hmbl_b;

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

    hmbl::bitset<999> hmbl_b1;
    assert(hmbl_b1.set(125).set(126).set(127).count() == 3);
    assert(hmbl_b == hmbl_b1);
    assert((hmbl_b1 >>= 2).count() == 3);
    assert((hmbl_b1 & hmbl_b).count() == 1);
    assert((hmbl_b1 | hmbl_b).count() == 5);

    return 0;
}