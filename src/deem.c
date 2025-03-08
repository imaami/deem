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
skip_ws (char const *str);

static char *
lazy (char const    *f,
      unsigned int   c,
      char         **v)
{
	if (c < 2 || !*v || !v[1])
		return NULL;

	char const *p = skip_ws(v[0]);
	if (p)
		printf("'%s'\n", p);

	size_t var = strlen(v[0]);
	size_t val = strlen(v[1]);
	if (!var || !val)
		return NULL;

	size_t siz = sizeof "override =$(eval override :=)$()"
	           + (var << 1U) + var + val;

	char *buf = gmk_alloc(siz);
	if (!buf)
		return NULL;

	int len = snprintf(buf, siz, "override %s=$("
	                   "eval override %s:=%s)$(%s)",
	                   *v, *v, v[1], *v);
	if (len == siz - 1U)
		gmk_eval(buf, NULL);

	gmk_free(buf);
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
skip_ws (char const *str)
{
	struct utf8 u8p = utf8();
	uint8_t const *p = (uint8_t const *)str;
	for (uint8_t const *q = p; *q; q = p) {
		p = utf8_parse_next_code_point(&u8p, q);
		if (u8p.error)
			// TODO
			return NULL;

		if (*q >= (uint8_t)'\t' &&
		    (*q <= (uint8_t)'\r' ||
		     *q == (uint8_t)' '))
			continue;

		return (char const *)q;
	}

	return (char const *)p;
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
