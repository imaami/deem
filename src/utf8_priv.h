/* SPDX-License-Identifier: LGPL-3.0-or-later */
/** @file utf8_priv.h
 * @brief Including this private header before utf8.h prevents
 *        `UTF8_PARSER_DESCRIPTOR` from being undefined at the
 *        end of utf8.h.
 * @author Juuso Alasuutari
 */
#ifndef DEEM_SRC_UTF8_PRIV_H_
#define DEEM_SRC_UTF8_PRIV_H_

/* Cleaning up private macros from the namespace during module
 * use relies on the private macros being undefined inside the
 * guarded section but before the public header is included.
 */
#undef UTF8_PARSER_DESCRIPTOR

#include "utf8.h"

#endif /* DEEM_SRC_UTF8_PRIV_H_ */
