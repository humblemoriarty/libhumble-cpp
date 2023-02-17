#include "humble/bitset.hpp"

#include <iostream>
#include <bitset>

int main()
{
    std::bitset<10> a;

    a.test(1);
    a.all();
    a.any();
    a.none();

    a.count();
    a.flip();
    a.set();
    a.reset();
    a.set(1);
    a.reset(1);

    hmbl::bitset<62, uint32_t> b1{}; // non-optimized version
    hmbl::bitset<62, uint64_t> b2{}; // optimized version
    hmbl::bitset<30, uint32_t> b3{}; // optimized
    b3.set();
    printf("res = %d\n", int(b2 == b1));
    b2.flip();
    printf("res = %d\n", int(b2 == b1));
    b1.flip();
    printf("b2.size() = %lu\n", b1.size());
    b3 != b3;
    // b2 == b2;
    // b2 == b2;
    return 0;
}