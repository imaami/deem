/* SPDX-License-Identifier: LGPL-3.0-or-later */
/** @file util.h
 *
 * @author Juuso Alasuutari
 */
#ifndef DEEM_SRC_UTIL_H_
#define DEEM_SRC_UTIL_H_

#include "compat.h"

#define maybe_parenthesize(...) arg0_if_no_args(naught,__VA_ARGS__)(__VA_ARGS__)

#ifdef NO_VA_OPT
# include "pragma.h"
diag_clang(push)
diag_clang(ignored "-Wgnu-zero-variadic-macro-arguments")
# define arg0_if_no_args(a,...) arg0_if_no_args_(, ##__VA_ARGS__ a,)
diag_clang(pop)
#else
# define arg0_if_no_args(a,...) arg0_if_no_args_(__VA_OPT__(,) a,)
#endif
#define arg0_if_no_args_(a,...) a

#define naught(...)

/** @brief Assume that the specified argument indices are not null.
 */
#define nonnull_in(...) __attribute__(( \
        nonnull maybe_parenthesize(__VA_ARGS__)))

/** @brief Assume that the return value of a function is not null.
 */
#define nonnull_out __attribute__((returns_nonnull))

/** @brief Calculate the element count of an array.
 */
#define array_size(x) (sizeof(x) / sizeof((x)[0]))

/** @brief Assume that a value is within a certain range.
 */
#if __has_builtin(__builtin_assume)
# define assume_value_bits(x, mask) \
        __builtin_assume((x) == ((x) & (typeof(x))(mask)))
#else
# define assume_value_bits(x, mask)
#endif

#ifndef __cplusplus

/**
 * @brief Check if an integer value is negative without getting warning
 *        spam if the type of the value is unsigned.
 *
 * If `x` is an unsigned integer type the macro expands to what should
 * be a compile-time constant expression 0, otherwise to `x < 0`. In
 * the latter case `x` is assumed to be a signed integer.
 *
 * @note The `_Generic` expression used to implement this macro only has
 *       explicit cases for the old school C unsigned integer types and
 *       a default case for everything else.
 *
 * @param x An integral-typed value.
 */
#define is_negative(x) (_Generic((x), \
        default:(x), unsigned char:1, \
        unsigned short:1, unsigned:1, \
        typeof(1UL):1,typeof(1ULL):1) < (typeof(x))0)

#endif /* __cplusplus */

#endif /* DEEM_SRC_UTIL_H_ */
