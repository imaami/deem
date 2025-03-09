/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>

#include <gnumake.h>

#include "utf8.h"

const int plugin_is_GPL_compatible;

struct length {
	size_t n_bytes;
	size_t n_chars;
};

static char const *
strip_ws (char const  *str,
          char const **end);

static char *
lazy (char const    *f,
      unsigned int   c,
      char         **v)
{
	if (c < 2 || !*v || !v[1])
		return NULL;

	char const *end[2] = {
		NULL,
		NULL
	};
	char const *str[2] = {
		strip_ws(v[0], &end[0]),
		strip_ws(v[1], &end[1])
	};
	if (!str[0] || !str[1])
		return NULL;

	size_t var = (size_t)(end[0] - str[0]);
	size_t val = (size_t)(end[1] - str[1]);
	if (!var || !val)
		return NULL;

	size_t siz = sizeof "override =$(eval override :=)$()"
	           + (var << 1U) + var + val;

	char buf[1024];
	char *ptr = siz > sizeof buf ? gmk_alloc(siz) : buf;
	if (!ptr)
		return NULL;

	int len = snprintf(ptr, siz, "override %.*s=$("
	                   "eval override %.*s:=%.*s)$(%.*s)",
	                   (int)var, str[0], (int)var, str[0],
	                   (int)val, str[1], (int)var, str[0]);
	if (len > 0 && (size_t)len == siz - 1U)
		gmk_eval(buf, NULL);

	if (ptr != buf)
		gmk_free(ptr);

	return NULL;
}

int
deem_gmk_setup (gmk_floc const *floc)
{
	puts("\e[1;34md\e[m \e[1;36me\e[m \e[1;32me\e[m \e[1;33mm\e[m");
	gmk_add_function("lazy", lazy, 2, 2, GMK_FUNC_NOEXPAND);
	return 1;
}

static char const *
strip_ws (char const  *str,
          char const **end)
{
	while (*str >= '\t' && (*str <= '\r' || *str == ' ')) {
		++str;
	}

	uint8_t const *p = (uint8_t const *)str;
	size_t ws_count = 0;
	struct utf8 u8p = utf8();

	for (uint8_t const *q = p; *q; q = p) {
		p = utf8_parse_next_code_point(&u8p, q);
		if (u8p.error) {
			fprintf(stderr, "UTF-8 error: %s\n",
			        strerror(u8p.error));
			return NULL;
		}

		for (ws_count = 0U;
		     *p >= (uint8_t)'\t' && (*p <= (uint8_t)'\r' ||
		                             *p == (uint8_t)' '); ++p)
			++ws_count;
	}

	*end = (char const *)p - ws_count;
	return str;
}

static struct length
string_length (char const *str)
{
	struct length r = {0, 0};
	struct utf8 u8p = utf8();
	uint8_t const *p = (uint8_t const *)str;
	for (uint8_t const *q = p; *q; q = p) {
		p = utf8_parse_next_code_point(&u8p, q);
		if (u8p.error)
			// TODO
			return (struct length){0};
		r.n_bytes += utf8_size(&u8p);
		r.n_chars++;
	}
	return r;
}
