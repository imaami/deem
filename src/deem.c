/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>

#include <gnumake.h>

const int plugin_is_GPL_compatible;

static char *
argc (char const    *f,
      unsigned int   c,
      char         **v)
{
	char *b = gmk_alloc(8);
	if (c > 9999999U)
		c = 9999999U;
	(void)snprintf(b, sizeof b, "%u", c);
	return b;
}

int
deem_gmk_setup (gmk_floc const *floc)
{
	puts("\e[1;34md\e[m \e[1;36me\e[m \e[1;32me\e[m \e[1;33mm\e[m");
	gmk_add_function("argc", argc, 0, 0, GMK_FUNC_NOEXPAND);
	return 1;
}
