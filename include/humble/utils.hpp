#ifndef LIBHUMBLE_CPP_UTILS_HPP_
#define LIBHUMBLE_CPP_UTILS_HPP_

#include <cassert>
#include <cstdlib>
#include <concepts>
#include <bit>

namespace hmbl::utils
{

template <std::unsigned_integral T>
inline constexpr T div_celling(T v, T d)
{
    assert(v != 0);
    return (v + d - 1) / d;
}

template <std::unsigned_integral T, size_t Alignment>
inline constexpr T align_up(T v)
{
    assert(v != 0);
    return Alignment * div_celling(v, Alignment);
}

template <std::unsigned_integral T>
inline constexpr bool is_pow_2(T v)
{
    return std::popcount(v) == 1;
}

} // namespace hmbl::utils

#endif