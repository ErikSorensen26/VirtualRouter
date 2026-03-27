/**
 * @file Likely.hpp
 * @brief Branch prediction hints: likely() and unlikely() macros.
 */

#ifndef LIKELY_HPP

namespace utils
{

#define LIKELY_HPP

/// @brief Hints to the compiler that expression @p x is almost always true.
///
/// Wraps `__builtin_expect` to mark the hot path through a conditional.
/// Use on branches taken in the overwhelming majority of cases (e.g. the
/// non-error path in a packet-processing loop) to guide the code generator
/// toward keeping the likely path in the fall-through stream.
#define likely(x) __builtin_expect(!!(x), 1)

/// @brief Hints to the compiler that expression @p x is almost always false.
///
/// Use on rare branches such as error handling, resource exhaustion, or
/// protocol edge cases so the unlikely code is moved out of the hot path.
#define unlikely(x) __builtin_expect(!!(x), 0)

} // namespace utils

#endif
