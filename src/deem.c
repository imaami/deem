/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gnumake.h>

#include "utf8.h"

const int plugin_is_GPL_compatible;

/** @brief String length
 */
struct len {
	size_t n_bytes; //< String length in bytes
	size_t n_chars; //< String length in Unicode characters
};

/** @brief String buffer
 */
struct buf {
	char       *ptr; //< Pointer to the data
	size_t      cap; //< Local or allocated size
	struct len  use; //< Current usage
};

struct loc256 {
	struct buf b;
	char       d[256U - sizeof(struct buf)];
};

struct loc64 {
	struct buf b;
	char       d[64U - sizeof(struct buf)];
};

static struct len
string_length (char const *str);

static force_inline struct loc256
loc256 (struct loc256 *const loc)
{
	return (struct loc256){
		.b = {
			.ptr = loc->d,
			.cap = sizeof loc->d,
			.use = {0, 0}
		},
		.d = {0}
	};
}

static force_inline struct loc64
loc64 (struct loc64 *const loc)
{
	return (struct loc64){
		.b = {
			.ptr = loc->d,
			.cap = sizeof loc->d,
			.use = {0, 0}
		},
		.d = {0}
	};
}

static force_inline void
loc256_fini (struct loc256 *const loc)
{
	if (loc->b.ptr != loc->d) {
		free(loc->b.ptr);
		*loc = loc256(loc);
	}
}

static force_inline void
loc64_fini (struct loc64 *const loc)
{
	if (loc->b.ptr != loc->d) {
		free(loc->b.ptr);
		*loc = loc64(loc);
	}
}

static force_inline void
buf_append_unsafe (struct buf *const buf,
                   char const *const str,
                   struct len *const len)
{
	memcpy(&buf->ptr[buf->use.n_bytes], str, len->n_bytes);
	buf->use.n_bytes += len->n_bytes;
	buf->use.n_chars += len->n_chars;
}

#define buf_append_literal_unsafe(buf, lit) \
	buf_append_unsafe((buf), (lit), \
		&(struct len){ \
			sizeof (lit) - 1U, \
			sizeof (lit) - 1U \
		})

static force_inline void
buf_terminate (struct buf *const buf)
{
	buf->ptr[buf->use.n_bytes] = '\0';
}

struct str {
	char *ptr;
	size_t len;
};

static char const *
strip_ws (char const *str,
          struct len *len);

static bool
buf_prealloc (struct buf *const buf,
              size_t            size)
{
	size += buf->use.n_bytes;
	if (size > buf->cap) {
		char *ptr = malloc(size);
		if (!ptr) {
			perror("malloc");
			return false;
		}
		if (buf->use.n_bytes)
			memcpy(ptr, buf->ptr, buf->use.n_bytes);
		ptr[buf->use.n_bytes] = '\0';
		buf->ptr = ptr;
		buf->cap = size;
	}
	return true;
}

static force_inline struct buf
buf_gmk_alloc (size_t size)
{
	return (struct buf){
		.ptr = gmk_alloc(size),
		.cap = size,
		.use = {0, 0}
	};
}

static void
lazy_ (struct buf *buf,
       char const *var,
       char const *val)
{
	struct len var_len;
	char const *var_ptr = strip_ws(var, &var_len);
	if (!var_ptr)
		return;

	struct len val_len;
	char const *val_ptr = strip_ws(val, &val_len);
	if (!val_ptr)
		return;

	if (!buf_prealloc(buf,
		sizeof "override "/* var */"=$(eval override "
		       /* var */":="/* val */")$("/* var */")"
		+ (3U * var_len.n_bytes)
		+ val_len.n_bytes))
		return;

	buf_append_literal_unsafe(buf, "override ");
	buf_append_unsafe(buf, var_ptr, &var_len);
	buf_append_literal_unsafe(buf, "=$(eval override ");
	buf_append_unsafe(buf, var_ptr, &var_len);
	buf_append_literal_unsafe(buf, ":=");
	buf_append_unsafe(buf, val_ptr, &val_len);
	buf_append_literal_unsafe(buf, ")$(");
	buf_append_unsafe(buf, var_ptr, &var_len);
	buf_append_literal_unsafe(buf, ")");
	buf_terminate(buf);

	gmk_eval(buf->ptr, nullptr);
}

static char *
lazy (useless char const  *f,
      unsigned int         c,
      char               **v)
{
	if (c == 2U && v[0] && v[1]) {
		struct loc256 loc = loc256(&loc);
		lazy_(&loc.b, v[0], v[1]);
		loc256_fini(&loc);
	}

	return nullptr;
}

static struct str
sgr2_ (char const *color,
       struct len  color_len,
       char const *text,
       struct len  text_len)
{
	//           "\e[" <color> "m" <text> "\e[m" <NUL>
	size_t size = 2U + color_len.n_bytes + 1U + text_len.n_bytes + 3U + 1U;
	struct str s = {
		.ptr = gmk_alloc(size),
		.len = 0
	};
	if (s.ptr) {
		s.ptr[0] = '\e';
		s.ptr[1] = '[';
		memcpy(&s.ptr[2], color, color_len.n_bytes);

		s.len = 2U + color_len.n_bytes;
		s.ptr[s.len++] = 'm';

		memcpy(&s.ptr[s.len], text, text_len.n_bytes);
		s.len += text_len.n_bytes;
		s.ptr[s.len++] = '\e';
		s.ptr[s.len++] = '[';
		s.ptr[s.len++] = 'm';

		s.ptr[s.len] = '\0';
	}
	return s;
}

static struct str
sgr2 (char const *color,
      char const *text)
{
	struct len len;
	char const *s = strip_ws(color, &len);
	if (s)
		return sgr2_(s, len, text, string_length(text));
	return (struct str){nullptr, 0U};
}

static struct buf
sgr_gmk_alloc (char const *clr,
               char const *txt)
{
	struct len clr_len;
	char const *clr_ptr = strip_ws(clr, &clr_len);
	if (!clr_ptr)
		return (struct buf){nullptr};

	struct len txt_len = string_length(txt);
	struct buf buf = buf_gmk_alloc(
		/* "\e[" <clr>         "m"  <txt>         "\e[m" <NUL> */
		2U + clr_len.n_bytes + 1U + txt_len.n_bytes + 3U + 1U);
	if (buf.ptr) {
		buf_append_literal_unsafe(&buf, "\e[");
		buf_append_unsafe(&buf, clr_ptr, &clr_len);
		buf_append_literal_unsafe(&buf, "m");
		buf_append_unsafe(&buf, txt, &txt_len);
		buf_append_literal_unsafe(&buf, "\e[m");
		buf_terminate(&buf);
	}

	return buf;
}

static void
sgr_buf (struct buf *const buf,
         char const *const clr,
         char const *const txt)
{
	struct len clr_len;
	char const *clr_ptr = strip_ws(clr, &clr_len);
	if (!clr_ptr)
		return;

	struct len txt_len = string_length(txt);
	if (!buf_prealloc(buf,
		/* "\e[" <clr>         "m"  <txt>         "\e[m" <NUL> */
		2U + clr_len.n_bytes + 1U + txt_len.n_bytes + 3U + 1U))
		return;

	buf_append_literal_unsafe(buf, "\e[");
	buf_append_unsafe(buf, clr_ptr, &clr_len);
	buf_append_literal_unsafe(buf, "m");
	buf_append_unsafe(buf, txt, &txt_len);
	buf_append_literal_unsafe(buf, "\e[m");
	buf_terminate(buf);
}


static char *
sgr (useless char const    *f,
     useless unsigned int   c,
     char                 **v)
{
	return sgr_gmk_alloc(v[0], v[1]).ptr;
}

static char *
msg (useless char const  *f,
     unsigned int         c,
     char               **v)
{
	if (c != 2U || !v[0] || !v[1])
		return nullptr;

	struct len pfx_len;
	char const *pfx_ptr = strip_ws(v[0], &pfx_len);
	if (!pfx_ptr)
		return nullptr;

	struct len txt_len = string_length(v[1]);
	struct loc256 loc = loc256(&loc);
	if (!buf_prealloc(&loc.b,
		sizeof "$(info $(" /* pfx */ "_pfx)" /* txt */ ")"
		+ pfx_len.n_bytes + txt_len.n_bytes))
		return nullptr;

	buf_append_literal_unsafe(&loc.b, "$(info $(");
	buf_append_unsafe(&loc.b, pfx_ptr, &pfx_len);
	buf_append_literal_unsafe(&loc.b, "_pfx)");
	buf_append_unsafe(&loc.b, v[1], &txt_len);
	buf_append_literal_unsafe(&loc.b, ")");
	buf_terminate(&loc.b);

	gmk_eval(loc.b.ptr, nullptr);
	loc256_fini(&loc);

	return nullptr;
}

static char *
register_msg (useless char const    *f,
              useless unsigned int   c,
              char                 **v)
{
	struct loc64 sgr = loc64(&sgr);
	sgr_buf(&sgr.b, v[1], v[0]);
	if (!sgr.b.ptr)
		return nullptr;

	char buf[256];

	struct len pfx_len;
	char const *pfx_ptr = strip_ws(v[0], &pfx_len);
	if (!pfx_ptr ||
	    pfx_len.n_bytes > sizeof buf - sizeof "_pfx")
		goto done;

	memcpy(buf, pfx_ptr, pfx_len.n_bytes);
	memcpy(&buf[pfx_len.n_bytes], "_pfx", sizeof "_pfx");

	char *arr[] = {buf, sgr.b.ptr};
	lazy(nullptr, 2U, arr);

done:
	loc64_fini(&sgr);
	return nullptr;
}

static char *
pfx_if (useless char const    *f,
        useless unsigned int   c,
        char                 **v)
{
	char *y = gmk_expand(v[1]);
	if (!y)
		return nullptr;

	struct len n;
	char const *rhs = strip_ws(y, &n);
	if (!rhs) {
		gmk_free(y);
		return nullptr;
	}

	char *x = gmk_expand(v[0]);
	if (x) do {
		struct len m;
		char const *lhs = strip_ws(x, &m);
		if (!lhs) {
			gmk_free(x);
			break;
		}

		size_t len = m.n_bytes + n.n_bytes;
		char *ret = gmk_alloc(len + 1U);
		if (ret) {
			memcpy(ret, lhs, m.n_bytes);
			memcpy(&ret[m.n_bytes], rhs, n.n_bytes);
			ret[len] = '\0';
		}

		gmk_free(x);
		gmk_free(y);
		return ret;
	} while (0);

	if (rhs != y)
		(void)memmove(y, rhs, n.n_bytes);
	y[n.n_bytes] = '\0';

	return y;
}

static char *
library (useless char const    *f,
         useless unsigned int   c,
         char                 **v)
{
	if (c < 2U || !v[0] || !v[1])
		return nullptr;

	struct len name_len;
	char const *name_ptr = strip_ws(v[0], &name_len);
	if (!name_ptr)
		return nullptr;

	struct len src_len;
	char const *src_ptr = strip_ws(v[1], &src_len);
	if (!src_ptr)
		return nullptr;

	size_t size = sizeof "all:| "/* name */"\n"
	                   "clean:| clean-"/* name */"\n"
	                 "install:| install-"/* name */"\n"
	                 "PHONY: "/* name */" clean-"/* name */" install-"/* name */"\n"
	                 "override PUBLISH+="/* name */" clean-"/* name */" install-"/* name */"\n"
	                 "override SRC_"/* name */":="/* src */"\n" + (10U * name_len.n_bytes) + src_len.n_bytes + 1U;
	char *buf = malloc(size);
	if (!buf) {
		perror("malloc");
		return nullptr;
	}
	(void)snprintf(buf, size, "all:| %s\n"
	               "clean:| clean-%s\n"
	               "install:| install-%s\n"
	               "PHONY: %s clean-%s install-%s\n"
	               "override PUBLISH+=%s clean-%s install-%s\n"
	               "override SRC_%s:=%s\n", name_ptr, name_ptr, name_ptr, name_ptr, name_ptr, name_ptr, name_ptr, name_ptr, name_ptr, name_ptr, src_ptr);
	gmk_eval(buf, nullptr);
	free(buf);
	return nullptr;
}


int
deem_gmk_setup (useless gmk_floc const *floc)
{
	puts("\e[1;34md\e[m \e[1;36me\e[m \e[1;32me\e[m \e[1;33mm\e[m");

	gmk_eval("override THIS_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))\n"
	         "    all:| $(TARGETS)\n"
	         "  clean:| $(TARGETS:%=clean-%)\n"
	         "install:| $(TARGETS:%=install-%)\n"
	         "override PUBLISH :=            \\\n"
	         "      all $(TARGETS)           \\\n"
	         "    clean $(TARGETS:%=clean-%) \\\n"
	         "  install $(TARGETS:%=install-%)\n"
	         ".PHONY: $(PUBLISH)\n"
	         "override LIB_TGT := $(filter %.so,$(TARGETS))\n"
	         "override EXE_TGT := $(TARGETS:%.so=)", nullptr);

	gmk_add_function("library", library, 2, 0, GMK_FUNC_NOEXPAND);
	gmk_add_function("lazy", lazy, 2, 2, GMK_FUNC_NOEXPAND);
	gmk_add_function("SGR", sgr, 2, 2, GMK_FUNC_NOEXPAND);
	gmk_add_function("msg", msg, 2, 2, GMK_FUNC_DEFAULT);
	gmk_add_function("register-msg", register_msg, 2, 2, GMK_FUNC_DEFAULT);
	gmk_add_function("pfx-if", pfx_if, 2, 2, GMK_FUNC_NOEXPAND);

	register_msg(nullptr, 2U, (char *[]){"CC      ", "0;36"});
	register_msg(nullptr, 2U, (char *[]){"CLEAN   ", "0;35"});
	register_msg(nullptr, 2U, (char *[]){"CXX     ", "0;36"});
	register_msg(nullptr, 2U, (char *[]){"INFO", "38;5;213"});
	register_msg(nullptr, 2U, (char *[]){"INSTALL ", "1;36"});
	register_msg(nullptr, 2U, (char *[]){"LINK    ", "1;34"});
	register_msg(nullptr, 2U, (char *[]){"STRIP   ", "0;33"});
	register_msg(nullptr, 2U, (char *[]){"SYMLINK ", "0;32"});
	register_msg(nullptr, 2U, (char *[]){"YEET", "38;5;191"});

	gmk_eval(".PHONY: yeet\nyeet:\n"
	         "\t$(msg YEET,    \e[38;5;119m(╯°□°)╯︵ ┻━┻\e[m)@:\n"
	         "clean:| yeet", nullptr);

	return 1;
}

static char const *
strip_ws (char const *str,
          struct len *len)
{
	struct len len_ = {0, 0};
	while (*str >= '\t' && (*str <= '\r' || *str == ' '))
		++str;
	if (!*str)
		goto end;

	struct utf8 u8p = utf8();

	for (uint8_t const *p = (uint8_t const *)str;;) {
		p = utf8_parse_next_code_point(&u8p, p);
		if (u8p.error) {
			(void)fprintf(stderr, "UTF-8 error: %s\n",
			              strerror(u8p.error));
			str = nullptr;
			goto end;
		}
		len_.n_bytes += utf8_size(&u8p);
		len_.n_chars++;

		size_t ws_count = 0;
		for (; *p >= (uint8_t)'\t' && (*p <= (uint8_t)'\r' ||
		                               *p == (uint8_t)' ')
		     ; ++p)
			++ws_count;

		if (!*p)
			break;

		len_.n_bytes += ws_count;
		len_.n_chars += ws_count;
	}

end:
	*len = len_;
	return len_.n_bytes ? str : nullptr;
}

static struct len
string_length (char const *str)
{
	struct len r = {0, 0};
	struct utf8 u8p = utf8();
	uint8_t const *p = (uint8_t const *)str;
	for (uint8_t const *q = p; *q; q = p) {
		p = utf8_parse_next_code_point(&u8p, q);
		if (u8p.error) {
			(void)fprintf(stderr, "UTF-8 error: %s\n",
			              strerror(u8p.error));
			return (struct len){0, 0};
		}
		r.n_bytes += utf8_size(&u8p);
		r.n_chars++;
	}
	return r;
}
