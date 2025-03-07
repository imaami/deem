/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>

#include <gnumake.h>

#include "utf8.h"

const int plugin_is_GPL_compatible;

static char *
lazy (char const    *f,
      unsigned int   c,
      char         **v)
{
	if (c < 2 || !*v || !v[1])
		return NULL;

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
