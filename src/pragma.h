/* SPDX-License-Identifier: LGPL-3.0-or-later */
/** @file pragma.h
 * @brief Cross-compiler pragma wrapper macros
 * @author Juuso Alasuutari
 */
#ifndef DEEM_SRC_PRAGMA_H_
#define DEEM_SRC_PRAGMA_H_

#define diag_apple_clang(...)   pragma_apple_clang(diagnostic __VA_ARGS__)
#define diag_clang(...)         pragma_clang(diagnostic __VA_ARGS__)
#define diag_gcc(...)           pragma_gcc(diagnostic __VA_ARGS__)
#define diag(...)               diag_clang(__VA_ARGS__) diag_gcc(__VA_ARGS__)

#ifdef __apple_build_version__
# define pragma_apple_clang     pragma_clang
#else
# define pragma_apple_clang(...)
#endif

#ifdef __clang__
# define pragma_clang(...)      ligma(clang __VA_ARGS__)
#else
# define pragma_clang(...)
#endif

#if !defined __clang__ && defined __GNUC__
# define pragma_gcc(...)        ligma(GCC __VA_ARGS__)
#else
# define pragma_gcc(...)
#endif

#ifdef _MSC_VER
# define pragma_msvc(...)       ligma(__VA_ARGS__)
#else
# define pragma_msvc(...)
#endif

#define ligma_(...)     # __VA_ARGS__
#define ligma(...)      _Pragma(ligma_(__VA_ARGS__))

#endif /* DEEM_SRC_PRAGMA_H_ */
