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

/** @brief String pointer + length in bytes and Unicode characters
 */
struct ref {
	union {
		char       *mut;
		char const *imm;
	};
	struct len len;
};

/** @brief String buffer
 */
struct buf {
	struct ref str; //< Address + current length
	size_t     cap; //< Local or allocated size
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
			.str = {
				.mut = loc->d,
				.len = {0, 0}
			},
			.cap = sizeof loc->d,
		},
		.d = {0}
	};
}

static force_inline struct loc64
loc64 (struct loc64 *const loc)
{
	return (struct loc64){
		.b = {
			.str = {
				.mut = loc->d,
				.len = {0, 0}
			},
			.cap = sizeof loc->d,
		},
		.d = {0}
	};
}

static force_inline void
loc256_fini (struct loc256 *const loc)
{
	if (loc->b.str.mut != loc->d) {
		free(loc->b.str.mut);
		*loc = loc256(loc);
	}
}

static force_inline void
loc64_fini (struct loc64 *const loc)
{
	if (loc->b.str.mut != loc->d) {
		free(loc->b.str.mut);
		*loc = loc64(loc);
	}
}

static force_inline void
buf_append (struct buf *const buf,
            char const *const str,
            struct len *const len)
{
	__builtin_memcpy(&buf->str.mut[buf->str.len.n_bytes], str, len->n_bytes);
	buf->str.len.n_bytes += len->n_bytes;
	buf->str.len.n_chars += len->n_chars;
}

#define buf_append_literal(buf, lit) \
	buf_append((buf), (lit), \
		&(struct len){ \
			sizeof (lit) - 1U, \
			sizeof (lit) - 1U \
		})

static force_inline void
buf_terminate (struct buf *const buf)
{
	buf->str.mut[buf->str.len.n_bytes] = '\0';
}

struct str {
	char *ptr;
	size_t len;
};

static bool
deem_debug (void)
{
	static int debug_mk = 0;

	if (!debug_mk) {
		int debug_mk_ = -1;
		char *str = gmk_expand("$(DEBUG_MK)");
		if (str) {
			char const *p = str;
			while (*p >= '\t' && (*p <= '\r' || *p == ' '))
				++p;
			if (*p == '1') do {
				if (!*++p) {
					debug_mk_ = 1;
					break;
				}
			} while (*p >= '\t' && (*p <= '\r' || *p == ' '));
			gmk_free(str);
		}

		debug_mk = debug_mk_;
	}

	return debug_mk == 1;
}

static void
deem_eval (char const *const str)
{
	if (deem_debug()) {
		static unsigned c = 0;
		unsigned c_ = (c++ & 0x7fU) + 0x60U;
		char const *begin = str;
		for (size_t i = 0; *begin; ++i) {
			char const *end = strpbrk(begin, "\n\r");
			int line = end ? (int)(end - begin) : -1;
			bool last = !end ||
			            ((*end == '\n' ||
			              *++end == '\n') && !*++end);
			(void)fprintf(stderr, "\e[38;5;%um%s\e[m", c_,
			              i ? (last ? " └─" : " ├─")
			                : (last ? "───" : "─┬─"));
			if (line < 0) {
				(void)fprintf(stderr, " %s\n", begin);
				break;
			}
			(void)fprintf(stderr, " %.*s\n", line, begin);
			if (last)
				break;
			begin = end;
		}
	}
	gmk_eval(str, nullptr);
}

static struct ref
trim (char const *str);

static inline char const *
strip_ws (char const *str,
          struct len *len)
{
	struct ref ret = trim(str);
	*len = ret.len;
	return ret.imm;
}

static bool
buf_reserve (struct buf *const buf,
             size_t            size)
{
	size += buf->str.len.n_bytes;
	if (size > buf->cap) {
		char *ptr = malloc(size);
		if (!ptr) {
			perror("malloc");
			return false;
		}
		if (buf->str.len.n_bytes)
			__builtin_memcpy(ptr, buf->str.mut, buf->str.len.n_bytes);
		ptr[buf->str.len.n_bytes] = '\0';
		buf->str.mut = ptr;
		buf->cap = size;
	}
	return true;
}

/** @brief Wrap the return value of `gmk_alloc()` in a `struct buf`.
 *
 * The allocated buffer must be freed with `gmk_free(buf->ptr)`.
 *
 * @param size The size of the buffer to allocate.
 * @return A `struct buf` containing the allocated buffer and its size.
 */
static force_inline struct buf
buf_gmk_alloc (size_t size)
{
	return (struct buf){
		.str = {
			.mut = gmk_alloc(size),
			.len = {0, 0}
		},
		.cap = size,
	};
}

static void
lazy_ (struct buf *buf,
       char const *var,
       char const *val)
{
	struct ref var_ref = trim(var);
	if (!var_ref.imm)
		return;

	struct ref val_ref = trim(val);
	if (!val_ref.imm)
		return;

	if (!buf_reserve(buf,
		sizeof "override "/* var */"=$(eval override "
		       /* var */":="/* val */")$("/* var */")"
		+ (3U * var_ref.len.n_bytes)
		+ val_ref.len.n_bytes))
		return;

	buf_append_literal(buf, "override ");
	buf_append(buf, var_ref.imm, &var_ref.len);
	buf_append_literal(buf, "=$(eval override ");
	buf_append(buf, var_ref.imm, &var_ref.len);
	buf_append_literal(buf, ":=");
	buf_append(buf, val_ref.imm, &val_ref.len);
	buf_append_literal(buf, ")$(");
	buf_append(buf, var_ref.imm, &var_ref.len);
	buf_append_literal(buf, ")");
	buf_terminate(buf);

	deem_eval(buf->str.mut);
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
sgr2_ (char const *clr,
       struct len  clr_len,
       char const *txt,
       struct len  txt_len)
{
	//           "\e[" <color>           "m"  <text>         "\e[m" <NUL>
	size_t size = 2U + clr_len.n_bytes + 1U + txt_len.n_bytes + 3U + 1U;
	struct str s = {
		.ptr = gmk_alloc(size),
		.len = 0
	};
	if (s.ptr) {
		s.ptr[0] = '\e';
		s.ptr[1] = '[';
		__builtin_memcpy(&s.ptr[2], clr, clr_len.n_bytes);

		s.len = 2U + clr_len.n_bytes;
		s.ptr[s.len++] = 'm';

		__builtin_memcpy(&s.ptr[s.len], txt, txt_len.n_bytes);
		s.len += txt_len.n_bytes;
		s.ptr[s.len++] = '\e';
		s.ptr[s.len++] = '[';
		s.ptr[s.len++] = 'm';

		s.ptr[s.len] = '\0';
	}
	return s;
}

static struct str
sgr2 (char const *clr,
      char const *txt)
{
	struct ref clr_ref = trim(clr);
	if (clr_ref.imm)
		return sgr2_(clr_ref.imm, clr_ref.len, txt, string_length(txt));
	return (struct str){nullptr, 0U};
}

static struct buf
sgr_gmk_alloc (char const *clr,
               char const *txt)
{
	struct ref clr_ref = trim(clr);
	if (!clr_ref.imm)
		return (struct buf){nullptr};

	struct len txt_len = string_length(txt);
		return (struct buf){nullptr};

	struct buf buf = buf_gmk_alloc(
		/* "\e[" <clr>         "m"  <txt>         "\e[m" <NUL> */
		2U + clr_ref.len.n_bytes + 1U + txt_len.n_bytes + 3U + 1U);
	if (buf.str.mut) {
		buf_append_literal(&buf, "\e[");
		buf_append(&buf, clr_ref.imm, &clr_ref.len);
		buf_append_literal(&buf, "m");
		buf_append(&buf, txt, &txt_len);
		buf_append_literal(&buf, "\e[m");
		buf_terminate(&buf);
	}

	return buf;
}

static void
sgr_buf (struct buf *const buf,
         char const *const clr,
         char const *const txt)
{
	struct ref clr_ref = trim(clr);
	if (!clr_ref.imm)
		return;

	struct len txt_len = string_length(txt);
	if (!buf_reserve(buf,
		/* "\e[" <clr>         "m"  <txt>         "\e[m" <NUL> */
		2U + clr_ref.len.n_bytes + 1U + txt_len.n_bytes + 3U + 1U))
		return;

	buf_append_literal(buf, "\e[");
	buf_append(buf, clr_ref.imm, &clr_ref.len);
	buf_append_literal(buf, "m");
	buf_append(buf, txt, &txt_len);
	buf_append_literal(buf, "\e[m");
	buf_terminate(buf);
}


static char *
sgr (useless char const    *f,
     useless unsigned int   c,
     char                 **v)
{
	return sgr_gmk_alloc(v[0], v[1]).str.mut;
}

static char *
msg (useless char const  *f,
     unsigned int         c,
     char               **v)
{
	if (c != 2U || !v[0] || !v[1])
		return nullptr;

	struct ref pfx_ref = trim(v[0]);
	if (!pfx_ref.imm)
		return nullptr;

	struct len txt_len = string_length(v[1]);
	struct loc64 loc = loc64(&loc);
	if (!buf_reserve(&loc.b,
		sizeof "$(info $(" /* pfx */ "_pfx)" /* txt */ ")"
		+ pfx_ref.len.n_bytes + txt_len.n_bytes))
		return nullptr;

	buf_append_literal(&loc.b, "$(info $(");
	buf_append(&loc.b, pfx_ref.imm, &pfx_ref.len);
	buf_append_literal(&loc.b, "_pfx)");
	buf_append(&loc.b, v[1], &txt_len);
	buf_append_literal(&loc.b, ")");
	buf_terminate(&loc.b);

	deem_eval(loc.b.str.mut);
	loc64_fini(&loc);

	return nullptr;
}

static char *
register_msg (useless char const    *f,
              useless unsigned int   c,
              char                 **v)
{
	struct loc64 sgr = loc64(&sgr);
	sgr_buf(&sgr.b, v[1], v[0]);
	if (!sgr.b.str.mut)
		return nullptr;

	char buf[256];

	struct len pfx_len;
	char const *pfx_ptr = strip_ws(v[0], &pfx_len);
	if (!pfx_ptr ||
	    pfx_len.n_bytes > sizeof buf - sizeof "_pfx")
		goto done;

	__builtin_memcpy(buf, pfx_ptr, pfx_len.n_bytes);
	__builtin_memcpy(&buf[pfx_len.n_bytes], "_pfx", sizeof "_pfx");

	char *arr[] = {buf, sgr.b.str.mut};
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
	char *rhs_str = gmk_expand(v[1]);
	if (!rhs_str)
		return nullptr;

	struct ref rhs = trim(rhs_str);
	if (!rhs.imm) {
		gmk_free(rhs_str);
		return nullptr;
	}

	char *lhs_str = gmk_expand(v[0]);
	if (lhs_str) do {
		struct ref lhs = trim(lhs_str);
		if (!lhs.imm) {
			gmk_free(lhs_str);
			break;
		}

		size_t len = lhs.len.n_bytes + rhs.len.n_bytes;
		char *ret = gmk_alloc(len + 1U);
		if (ret) {
			__builtin_memcpy(ret, lhs.imm, lhs.len.n_bytes);
			__builtin_memcpy(&ret[lhs.len.n_bytes], rhs.imm, rhs.len.n_bytes);
			ret[len] = '\0';
		}

		gmk_free(lhs_str);
		gmk_free(rhs_str);
		return ret;
	} while (0);

	if (rhs.imm != (char const *)rhs_str)
		(void)memmove(rhs_str, rhs.imm, rhs.len.n_bytes);
	rhs_str[rhs.len.n_bytes] = '\0';

	return rhs_str;
}

static char *
library (useless char const    *f,
         useless unsigned int   c,
         char                 **v)
{
	if (c < 2U || !v[0] || !v[1])
		return nullptr;

	struct ref name_ref = trim(v[0]);
	if (!name_ref.imm)
		return nullptr;

	struct ref src_ref = trim(v[1]);
	if (!src_ref.imm)
		return nullptr;

	struct loc256 loc = loc256(&loc);
	if (!buf_reserve(&loc.b,
		sizeof ".PHONY: "/* name */" clean-"/* name */" install-"/* name */"\n"
		       "all:| "/* name */"\n"
		       "clean:| clean-"/* name */"\n"
		       "install:| install-"/* name */"\n"
		       "override SRC_"/* name */":="/* src */"\n"
		       "override OBJ_"/* name */":=$(SRC_"/* name */":%=%.o-fpic)\n"
		       "ifneq (,$(filter all clean install "/* name */" clean-"/* name */" install-"/* name */",$(or $(MAKECMDGOALS),all)))\n"
		       /* name */": $(THIS_DIR)"/* name */"\n"
		       "$(THIS_DIR)"/* name */": $(OBJ_"/* name */":%=$(THIS_DIR)%)\n"
		       "\t@+$(CC) $(CFLAGS) $(CFLAGS_$(@F)) -fPIC -shared -o $@ -MMD $^\n"
		       "%.c.o-fpic: %.c\n"
		       "\t@+$(CC) $(CFLAGS) $(CFLAGS_$(@F)) -fPIC -c"   " -o $@ -MMD $<\n"
		       "clean-"/* name */":\n"
		       "\t@rm -f $(@:clean-%=$(THIS_DIR)%) $(OBJ_$(@:clean-%=%):%=$(THIS_DIR)%)\n"
		       "endif"
		       + (17U * name_ref.len.n_bytes) + src_ref.len.n_bytes))
		return nullptr;

	buf_append_literal(&loc.b, ".PHONY: ");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, " clean-");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, " install-");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, "\nall:| ");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, "\nclean:| clean-");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, "\ninstall:| install-");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, "\noverride SRC_");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, ":=");
	buf_append(&loc.b, src_ref.imm, &src_ref.len);
	buf_append_literal(&loc.b, "\noverride OBJ_");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, ":=$(SRC_");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, ":%=%.o-fpic)\nifneq (,$(filter all clean install ");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, " clean-");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, " install-");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, ",$(or $(MAKECMDGOALS),all)))\n");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, ": $(THIS_DIR)");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, "\n$(THIS_DIR)");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b, ": $(OBJ_");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b,
		":%=$(THIS_DIR)%)\n"
		"\t@+$(CC) $(CFLAGS) $(CFLAGS_$(@F)) -fPIC -shared -o $@ -MMD $^\n"
		"%.c.o-fpic: %.c\n"
		"\t@+$(CC) $(CFLAGS) $(CFLAGS_$(@F)) -fPIC -c -o $@ -MMD $<\n"
		"clean-");
	buf_append(&loc.b, name_ref.imm, &name_ref.len);
	buf_append_literal(&loc.b,
		":\n"
		"\t@rm -f $(@:clean-%=$(THIS_DIR)%) $(OBJ_$(@:clean-%=%):%=$(THIS_DIR)%)\n"
		"endif");
	buf_terminate(&loc.b);
	deem_eval(loc.b.str.mut);
	loc256_fini(&loc);

	return nullptr;
}

int
deem_gmk_setup (useless gmk_floc const *floc)
{

	if (deem_debug()) {
		puts("\e[0;36m┌───────╮\e[m\n"
		     "\e[0;36m│"
		     "\e[1;34md\e[m "
		     "\e[1;36me\e[m "
		     "\e[1;32me\e[m "
		     "\e[1;33mm"
		     "\e[0;36m│\e[m\n"
		     "\e[0;36m╰───────┘\e[m");
	}

	struct loc256 loc = loc256(&loc);
	lazy_(&loc.b, "THIS_DIR",
	      "$(dir $(realpath $(lastword $(MAKEFILE_LIST))))");
	loc256_fini(&loc);

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

	deem_eval(".PHONY: all clean install\n"
	          "ifneq (.DEFAULT,$(MAKECMDGOALS))\n"
	          ".PHONY: yeet\n"
	          "clean:| yeet\n"
	          "yeet:\n"
	          "\t$(msg YEET,    \e[38;5;119m(╯°□°)╯︵ ┻━┻\e[m)@:\n"
	          "endif");

	return 1;
}

static struct ref
trim (char const *str)
{
	struct ref ret = {
		.imm = nullptr,
		.len = {0U, 0U}
	};

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
			goto end;
		}
		ret.len.n_bytes += utf8_size(&u8p);
		ret.len.n_chars++;

		size_t ws_count = 0;
		for (; *p >= (uint8_t)'\t' && (*p <= (uint8_t)'\r' ||
		                               *p == (uint8_t)' ')
		     ; ++p)
			++ws_count;

		if (!*p)
			break;

		ret.len.n_bytes += ws_count;
		ret.len.n_chars += ws_count;
	}

	if (ret.len.n_bytes)
		ret.imm = str;
end:
	return ret;
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
