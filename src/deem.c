/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gnumake.h>

#include "utf8.h"

const int plugin_is_GPL_compatible;

struct length {
	size_t n_bytes;
	size_t n_chars;
};

struct str {
	char *ptr;
	size_t len;
};

static char const *
strip_ws (char const  *str,
          char const **end);

static char *
lazy (useless char const  *f,
      unsigned int         c,
      char               **v)
{
	if (c < 2 || !*v || !v[1])
		return nullptr;

	char const *end[2] = {
		v[0], v[1]
	};
	char const *str[2] = {
		strip_ws(v[0], &end[0]),
		strip_ws(v[1], &end[1])
	};
	if (!str[0] || !str[1])
		return nullptr;

	size_t var = (size_t)(end[0] - str[0]);
	size_t val = (size_t)(end[1] - str[1]);
	if (!var || !val)
		return nullptr;

	size_t siz = sizeof "override =$(eval override :=)$()"
	           + (var << 1U) + var + val;

	char buf[1024];
	char *ptr = siz > sizeof buf ? gmk_alloc(siz) : buf;
	if (!ptr)
		return nullptr;

	int len = snprintf(ptr, siz, "override %.*s=$("
	                   "eval override %.*s:=%.*s)$(%.*s)",
	                   (int)var, str[0], (int)var, str[0],
	                   (int)val, str[1], (int)var, str[0]);
	if (len > 0 && (size_t)len == siz - 1U)
		gmk_eval(buf, nullptr);

	if (ptr != buf)
		gmk_free(ptr);

	return nullptr;
}

static struct str
sgr2_ (char const *color,
       size_t      color_len,
       char const *text,
       size_t      text_len)
{
	//           "\e[" <color> "m" <text> "\e[m" <NUL>
	size_t size = 2U + color_len + 1U + text_len + 3U + 1U;
	struct str s = {
		.ptr = gmk_alloc(size),
		.len = 0
	};
	if (s.ptr) {
		s.ptr[0] = '\e';
		s.ptr[1] = '[';
		memcpy(&s.ptr[2], color, color_len);

		s.len = 2U + color_len;
		s.ptr[s.len++] = 'm';

		memcpy(&s.ptr[s.len], text, text_len);
		s.len += text_len;
		s.ptr[s.len++] = '\e';
		s.ptr[s.len++] = '[';
		s.ptr[s.len++] = 'm';

		s.ptr[s.len] = '\0';
	}
	return s;
}

static char *
sgr (useless char const  *f,
     unsigned int         c,
     char               **v)
{
	if (c != 2U || !v[0] || !v[1])
		return nullptr;

	char const *end = v[0];
	char const *str = strip_ws(v[0], &end);
	if (!str)
		return nullptr;

	return sgr2_(
		str, (size_t)(end - str),
		v[1], strlen(v[1])
	).ptr;
}

static char *
msg (useless char const  *f,
     unsigned int         c,
     char               **v)
{
	if (c != 2U || !v[0] || !v[1])
		return nullptr;

	char const *pfx_end = v[0];
	char const *pfx_ptr = strip_ws(v[0], &pfx_end);
	if (!pfx_ptr)
		return nullptr;

	size_t pfx_len = (size_t)(pfx_end - pfx_ptr);
	if (!pfx_len)
		return nullptr;

	size_t msg_len = strlen(v[1]);
	size_t size = sizeof "$(info $(_pfx))" + pfx_len + msg_len;

	char buf[1024];
	char *ptr = size > sizeof buf ? gmk_alloc(size) : buf;
	if (ptr) {
		int len = snprintf(ptr, size, "$(info $(%.*s_pfx)%s)",
		                  (int)pfx_len, pfx_ptr, v[1]);
		if (len > 0 && (size_t)len == size - 1U)
			gmk_eval(ptr, nullptr);

		if (ptr != buf)
			gmk_free(ptr);
	}

	return nullptr;
}

static char *
register_msg (useless char const  *f,
              unsigned int         c,
              char               **v)
{
	if (c != 2U || !v[0] || !v[1])
		return nullptr;

	char const *pfx_end = v[0];
	char const *pfx_ptr = strip_ws(v[0], &pfx_end);
	if (!pfx_ptr)
		return nullptr;

	size_t pfx_len = (size_t)(pfx_end - pfx_ptr);
	if (!pfx_len)
		return nullptr;

	size_t msg_len = strlen(v[1]);
	size_t size = sizeof "$(info $(_pfx))" + pfx_len + msg_len;

	char *ptr = gmk_alloc(size);
	if (ptr) {
		int len = snprintf(ptr, size, "$(info $(%.*s_pfx)%s)",
		                  (int)pfx_len, pfx_ptr, v[1]);
		if (len > 0 && (size_t)len == size - 1U)
			gmk_eval(ptr, nullptr);
		gmk_free(ptr);
	}

	return nullptr;
}

int
deem_gmk_setup (useless gmk_floc const *floc)
{
	puts("\e[1;34md\e[m \e[1;36me\e[m \e[1;32me\e[m \e[1;33mm\e[m");
	gmk_add_function("lazy", lazy, 2, 2, GMK_FUNC_NOEXPAND);
	gmk_add_function("SGR", sgr, 1, 2, GMK_FUNC_NOEXPAND);
	gmk_add_function("_SGR", sgr, 1, 2, GMK_FUNC_DEFAULT);
	gmk_add_function("msg", msg, 2, 2, GMK_FUNC_DEFAULT);
	gmk_add_function("register-msg", register_msg, 2, 2, GMK_FUNC_DEFAULT);
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
			(void)fprintf(stderr, "UTF-8 error: %s\n",
			              strerror(u8p.error));
			return nullptr;
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
