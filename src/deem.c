/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gnumake.h>

#include "utf8.h"

const int plugin_is_GPL_compatible;

enum flavor {
	flavor_undefined,
	flavor_simple,
	flavor_recursive
};

/** @brief String length
 */
struct len {
	size_t n_bytes; //< String length in bytes
	size_t n_chars; //< String length in Unicode characters
};

static struct len
len (char const *str);

/** @brief String pointer + length in bytes + length in code points
 */
struct ref {
	union {
		char       *mut;
		char const *imm;
	};
	struct len len;
};

static force_inline struct ref
ref (char const *const str)
{
	return (struct ref){
		.imm = str,
		.len = len(str)
	};
}

/** @brief String buffer
 */
struct buf {
	struct ref str; //< Address + current length
	size_t     cap; //< Local or allocated size
};

#define define_buf(N) \
struct buf##N { \
	struct buf b; \
	char       d[N - sizeof(struct buf)]; \
}; \
static force_inline struct buf##N \
buf##N (struct buf##N *const buf) \
{ \
	return (struct buf##N){ \
		.b = { \
			.str = { \
				.mut = buf->d, \
				.len = {0, 0} \
			}, \
			.cap = N - sizeof(struct buf) \
		}, \
		.d = {0} \
	}; \
} \
static force_inline void \
buf##N##_fini (struct buf##N *const buf) \
{ \
	if (buf->b.str.mut != buf->d) { \
		free(buf->b.str.mut); \
		*buf = buf##N(buf); \
	} \
}

define_buf(64)
define_buf(256)
define_buf(1024)

#undef define_buf

static force_inline void
buf_append_ (struct buf *const      buf,
            char const *const       str,
            struct len const *const len)
{
	__builtin_memcpy(&buf->str.mut[buf->str.len.n_bytes], str, len->n_bytes);
	buf->str.len.n_bytes += len->n_bytes;
	buf->str.len.n_chars += len->n_chars;
}

static force_inline void
buf_append (struct buf *const       buf,
            struct ref const *const ref)
{
	buf_append_(buf, ref->imm, &ref->len);
}

#define buf_append_literal(buf, lit) \
	buf_append_((buf), (lit), \
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

/**
 * @brief Check if the buffer has enough capacity, and allocate more
 *        if it does not.
 *
 * This function does not resize an existing heap allocation. Trying
 * to do so will result in the old allocation being leaked.
 *
 * The only valid use case is when `buf` is embedded in a size-typed
 * stack buffer and `buf->str.mut` points at the attached storage of
 * that stack buffer. See the `define_buf()` macro etc. for details.
 *
 * @param buf The buffer to check.
 * @param size The required capacity.
 * @return `true` if the buffer had enough capacity or an allocation
 *         was made successfully, `false` otherwise.
 */
static bool
buf_reserve (struct buf *const buf,
             size_t            size)
{
	assert(!buf->str.mut || buf->str.mut == (char *)&buf[1]);

	// FIXME: needs overflow check
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
buf_gmk_alloc (size_t const size)
{
	return (struct buf){
		.str = {
			.mut = gmk_alloc(size),
			.len = {0U, 0U}
		},
		.cap = size,
	};
}

static void
lazy_ (struct buf *buf,
       char const *lhs,
       char const *rhs)
{
	struct ref var = trim(lhs);
	if (!var.imm)
		return;

	struct ref val = trim(rhs);
	if (!val.imm)
		return;

	#define XLAZY(L, V, N) L("override ") V(var) \
	L("=$(eval override ") V(var) L(":=") V(val) \
	L(")$(") V(var) L(")") N()

	#define lit_(x) (sizeof x - 1U) +
	#define var_(x) (x).len.n_bytes +
	#define nul_()  1U

	if (!buf_reserve(buf, XLAZY(lit_, var_, nul_)))
		return;

	#undef nul_
	#undef var_
	#undef lit_

	#define lit_(x) buf_append_literal(buf, x);
	#define var_(x) buf_append(buf, &x);
	#define nul_()  buf_terminate(buf)

	XLAZY(lit_, var_, nul_);

	#undef nul_
	#undef var_
	#undef lit_

	#undef XLAZY

	deem_eval(buf->str.mut);
}

static char *
lazy (useless char const  *f,
      unsigned int         c,
      char               **v)
{
	if (c == 2U && v[0] && v[1]) {
		struct buf256 loc = buf256(&loc);
		lazy_(&loc.b, v[0], v[1]);
		buf256_fini(&loc);
	}

	return nullptr;
}

static char *
sgr_buf (char const *const color,
         char const *const text,
         struct buf *const loc_buf)
{
	struct ref clr = trim(color);
	if (!clr.imm)
		return nullptr;

	struct ref txt = ref(text);
	struct buf gmk_buf, *buf;

	#define XSGR(CLR, TXT, L, V, N) \
	L("\e[") V(CLR) L("m") V(TXT) L("\e[m") N()

	#define lit_(x) (sizeof x - 1U) +
	#define var_(x) (x).len.n_bytes +
	#define nul_()  1U

	if (loc_buf) {
		if (!buf_reserve(loc_buf, XSGR(clr, txt, lit_, var_, nul_)))
			return nullptr;
		buf = loc_buf;
	} else {
		gmk_buf = buf_gmk_alloc(XSGR(clr, txt, lit_, var_, nul_));
		if (!gmk_buf.str.mut)
			return nullptr;
		buf = &gmk_buf;
	}

	#undef nul_
	#undef var_
	#undef lit_

	#define lit_(x) buf_append_literal(buf, x);
	#define var_(x) buf_append(buf, &x);
	#define nul_()  buf_terminate(buf)

	XSGR(clr, txt, lit_, var_, nul_);

	#undef nul_
	#undef var_
	#undef lit_

	#undef XSGR

	return buf->str.mut;
}

static char *
sgr (useless char const    *f,
     useless unsigned int   c,
     char                 **v)
{
	return sgr_buf(v[0], v[1], nullptr);
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

	struct ref txt_ref = ref(v[1]);
	struct buf64 loc = buf64(&loc);
	if (!buf_reserve(&loc.b,
		sizeof "$(info $(" /* pfx */ "_pfx)" /* txt */ ")"
		+ pfx_ref.len.n_bytes + txt_ref.len.n_bytes))
		return nullptr;

	buf_append_literal(&loc.b, "$(info $(");
	buf_append(&loc.b, &pfx_ref);
	buf_append_literal(&loc.b, "_pfx)");
	buf_append_(&loc.b, txt_ref.imm, &txt_ref.len);
	buf_append_literal(&loc.b, ")");
	buf_terminate(&loc.b);

	deem_eval(loc.b.str.mut);
	buf64_fini(&loc);

	return nullptr;
}

static char *
register_msg (useless char const    *f,
              useless unsigned int   c,
              char                 **v)
{
	struct buf64 sgr = buf64(&sgr);
	if (!sgr_buf(v[1], v[0], &sgr.b))
		return nullptr;

	char buf[256];

	struct ref pfx_ref = trim(v[0]);
	if (!pfx_ref.imm ||
	    pfx_ref.len.n_bytes > sizeof buf - sizeof "_pfx")
		goto done;

	__builtin_memcpy(buf, pfx_ref.imm, pfx_ref.len.n_bytes);
	__builtin_memcpy(&buf[pfx_ref.len.n_bytes], "_pfx", sizeof "_pfx");

	char *arr[] = {buf, sgr.b.str.mut};
	lazy(nullptr, 2U, arr);

done:
	buf64_fini(&sgr);
	return nullptr;
}

enum side {
	left_side,
	right_side
};

/**
 * @brief Expand, trim, and concatenate two strings if one of them is
 *        non-empty after expansion and trimming.
 *
 * This is the underlying implementation for the `$(pfx-if a,b)` and
 * `$(sfx-if a,b)` functions. If `side` is `left_side`, concatenation
 * is done iff `argv[0]` is non-empty after expansion and trimming;
 * otherwise if `side` is `right_side`, concatenation is done iff
 * `argv[1]` is non-empty after expansion and trimming. The opposite
 * side argument isn't required to be non-empty.
 *
 * If the argument indicated by `side` is empty after expansion and
 * trimming, the result is `nullptr`. If the argument indicated by
 * `side` is non-empty, but the other argument is empty, the result
 * is simply the non-empty argument after expansion and trimming.
 *
 * If the result is not null, the caller is responsible for freeing
 * it with `gmk_free()`.
 *
 * @param argv The arguments to the `cat-if` function.
 * @param side The side to concatenate.
 * @return A pointer to the concatenated string, or `nullptr` if the
 *         argument indicated by `side` is empty after expansion and
 *         trimming.
 */
static char *
cat_if (char      **argv,
        enum side   side)
{
	// Expand the argument indicated by `side`.
	char *str[] = {nullptr, nullptr};
	str[side] = gmk_expand(argv[side]);
	if (!str[side])
		return nullptr;

	// Trim the expanded argument indicated by `side`.
	struct ref ref[] = {{nullptr}, {nullptr}};
	ref[side] = trim(str[side]);
	if (!ref[side].imm) {
		gmk_free(str[side]);
		return nullptr;
	}

	str[!side] = gmk_expand(argv[!side]);
	if (str[!side]) do {
		ref[!side] = trim(str[!side]);
		if (!ref[!side].imm) {
			gmk_free(str[!side]);
			break;
		}

		struct ref const *const lhs = &ref[left_side];
		struct ref const *const rhs = &ref[right_side];
		size_t n = lhs->len.n_bytes + rhs->len.n_bytes;
		char *r = gmk_alloc(n + 1U);
		if (r) {
			(void)__builtin_memcpy(r, lhs->imm, lhs->len.n_bytes);
			(void)__builtin_memcpy(&r[lhs->len.n_bytes], rhs->imm,
			                       rhs->len.n_bytes);
			r[n] = '\0';
		}

		gmk_free(str[left_side]);
		gmk_free(str[right_side]);
		return r;
	} while (0);

	/* If the other argument was empty, and the argument indicated by
	 * `side` was non-empty, we simply return the buffer allocated by
	 * `gmk_expand()` for the non-empty argument. This requires us to
	 * deal with possible leading whitespace.
	 *
	 * Because we return the trimmed result, the non-whitespace bytes
	 * must start at the beginning of the buffer for the caller to be
	 * able to call `gmk_free()` on it. So, first we have to check if
	 * the trimmed content bytes start at an offset, and move them to
	 * the beginning if they do.
	 */

	if (ref[side].imm != (char const *)str[side])
		(void)__builtin_memmove(str[side], ref[side].imm,
		                        ref[side].len.n_bytes);
	str[side][ref[side].len.n_bytes] = '\0';

	return str[side];
}

static char *
pfx_if (useless char const    *f,
        useless unsigned int   c,
        char                 **v)
{
	return cat_if(v, right_side);
}

static char *
sfx_if (useless char const    *f,
        useless unsigned int   c,
        char                 **v)
{
	return cat_if(v, left_side);
}

static char *
library (useless char const    *f,
         useless unsigned int   c,
         char                 **v)
{
	if (c < 2U || !v[0] || !v[1])
		return nullptr;

	struct library_args {
		struct ref pos[2];
		bool install;
	} args = {0};

	struct ref name = trim(v[0]);
	if (!name.imm)
		return nullptr;

	struct ref src = trim(v[1]);
	if (!src.imm)
		return nullptr;

	#define XLIBRARY(NAME, SRC, L, V, N) \
	L(".PHONY: ") V(NAME) L(" clean-") V(NAME) L(" install-") V(NAME) L("\n" \
	"all:| ") V(NAME) L("\n" \
	"clean:| clean-") V(NAME) L("\n" \
	"install:| install-") V(NAME) L("\n" \
	"\n" \
	"override SRC_") V(NAME) L(":=") V(SRC) L("\n" \
	"override OBJ_") V(NAME) L(":=$(SRC_") V(NAME) L(":%=$O%.o-fpic)\n" \
	"override DEP_") V(NAME) L(":=$(SRC_") V(NAME) L(":%=$O%.d)\n" \
	"\n" \
	"ifneq (,$(filter all ") V(NAME) L(",$(or $(MAKECMDGOALS),all)))\n") \
	V(NAME) L(": $O") V(NAME) L("\n" \
	"endif\n" \
	"\n" \
	"ifneq (,$(filter all install ") V(NAME) L(" install-") V(NAME) L(",$(or $(MAKECMDGOALS),all)))\n" \
	"$O") V(NAME) L(": $(OBJ_") V(NAME) L(")\n" \
	"\t$(msg LINK,") V(NAME) L(")\n" \
	"\t@+$(CC) $(CFLAGS) $(CFLAGS_") V(NAME) L(") -fPIC -shared -o $@ -MMD $^\n" \
	"\n" \
	"%.c.o-fpic: %.c\n" \
	"\t$(msg CC,$(@F))\n" \
	"\t@+$(CC) $(CFLAGS) $(CFLAGS_$(@F)) -fPIC -c -o $@ -MMD $<\n" \
	"\n" \
	"-include $(DEP_") V(NAME) L(")\n" \
	"endif\n" \
	"\n" \
	"ifneq (,$(filter clean clean-") V(NAME) L(",$(MAKECMDGOALS)))\n" \
	"clean-") V(NAME) L(": $(eval override private WHAT_") V(NAME) L("=$$(eval clean-") V(NAME) L(": override private WHAT_") V(NAME) L(":=$$$$(sort $$$$(wildcard " \
	"$O") V(NAME) L(" $(OBJ_") V(NAME) L(") $(DEP_") V(NAME) L(")))))$(WHAT_") V(NAME) L(")\n" \
	"clean-") V(NAME) L(":;$(if $(WHAT_") V(NAME) L("),$(info \e[38;5;191mYEET\e[m    \e[38;5;119m(╯°□°)╯︵ ┻━┻\e[m $(WHAT_") V(NAME) L(":$O%=%))" \
	"\t@$(RM) $(WHAT_") V(NAME) L("),@:)\n" \
	"endif\n" \
	"\n" \
	"ifneq (,$(filter install install-") V(NAME) L(",$(MAKECMDGOALS)))\n" \
	"$(eval install-") V(NAME) L(": override private DST_") V(NAME) L("=$(eval override private DST_") V(NAME) L(":=$$(if $$(DESTDIR),$$(DESTDIR:/=)/)$$(if $$(libdir),$$(libdir:/=)/)") V(NAME) L(".0)$(DST_") V(NAME) L("))\n" \
	"install-") V(NAME) L(": $O") V(NAME) L("\n" \
	"\t$(msg INSTALL,$(DST_") V(NAME) L("))\n" \
	"\t@install -DTsm 0644 $O") V(NAME) L(" $(DST_") V(NAME) L(")\n" \
	"endif") N()

	#define lit_(x) (sizeof x - 1U) +
	#define var_(x) (x).len.n_bytes +
	#define nul_()  1U

	struct buf1024 loc = buf1024(&loc);
	if (!buf_reserve(&loc.b, XLIBRARY(name, src, lit_, var_, nul_)))
		return nullptr;

	#undef nul_
	#undef var_
	#undef lit_

	#define lit_(x) buf_append_literal(&loc.b, x);
	#define var_(x) buf_append(&loc.b, &x);
	#define nul_()  buf_terminate(&loc.b)

	XLIBRARY(name, src, lit_, var_, nul_);

	#undef nul_
	#undef var_
	#undef lit_

	#undef XLIBRARY

	deem_eval(loc.b.str.mut);
	buf1024_fini(&loc);

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

	struct buf256 loc = buf256(&loc);
	lazy_(&loc.b, "THIS_DIR",
	      "$(dir $(realpath $(lastword $(MAKEFILE_LIST))))");
	deem_eval("override O=$(eval override O:=$(THIS_DIR))$(O)");
	buf256_fini(&loc);

	deem_eval(".PHONY: all clean install\n"
	          "all:; @:\n"
	          "clean:; @:\n"
	          "install:; @:\n");

	gmk_add_function("library", library, 2, 0, GMK_FUNC_NOEXPAND);
	gmk_add_function("lazy", lazy, 2, 2, GMK_FUNC_NOEXPAND);
	gmk_add_function("SGR", sgr, 2, 2, GMK_FUNC_NOEXPAND);
	gmk_add_function("msg", msg, 2, 2, GMK_FUNC_DEFAULT);
	gmk_add_function("register-msg", register_msg, 2, 2, GMK_FUNC_DEFAULT);
	gmk_add_function("pfx-if", pfx_if, 2, 2, GMK_FUNC_NOEXPAND);
	gmk_add_function("sfx-if", sfx_if, 2, 2, GMK_FUNC_NOEXPAND);

	register_msg(nullptr, 2U, (char *[]){"CC      ", "0;36"});
	register_msg(nullptr, 2U, (char *[]){"CLEAN   ", "0;35"});
	register_msg(nullptr, 2U, (char *[]){"CXX     ", "0;36"});
	register_msg(nullptr, 2U, (char *[]){"INFO", "38;5;213"});
	register_msg(nullptr, 2U, (char *[]){"INSTALL ", "1;36"});
	register_msg(nullptr, 2U, (char *[]){"LINK    ", "1;34"});
	register_msg(nullptr, 2U, (char *[]){"STRIP   ", "0;33"});
	register_msg(nullptr, 2U, (char *[]){"SYMLINK ", "0;32"});

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
len (char const *const str)
{
	if (str) {
		struct len r = {0, 0};
		struct utf8 u8p = utf8();
		uint8_t const *p = (uint8_t const *)str;
		for (uint8_t const *q = p; *q; q = p) {
			p = utf8_parse_next_code_point(&u8p, q);
			if (u8p.error) {
				(void)fprintf(stderr, "UTF-8 error: %s\n",
				              strerror(u8p.error));
				goto fail;
			}
			r.n_bytes += utf8_size(&u8p);
			r.n_chars++;
		}
		return r;
	}
fail:
	return (struct len){0, 0};
}

static char *
arg_var (useless char const  *f,
         unsigned int         c,
         char               **v)
{
	struct ref var = trim(v[0]);
	if (!var.imm)
		return nullptr;

	struct ref def = c > 1U ? trim(v[1]) : (struct ref){
		.imm = nullptr,
		.len = {0U, 0U}
	};

/*
override define arg_var
  $(eval
    ifeq (environment,$$(origin __default_$1))
      # Unset the __default variable if it originates from the environment
      # (because it's probably not shadowing a file variable).
      $$(call warn-origin,__default_$1)
      $$(if $$(DEBUG_MK),$$(info override undefine __default_$1 [$$(origin __default_$1)]))
      override undefine __default_$1
    else ifeq (,$$(filter file override undefined,$$(firstword $$(origin __default_$1))))
      # Abort if __default might be a command line or environment override
      # variable, as they can be used to mess with the build by clobbering
      # non-override file vars.
      $$(call fail-origin,__default_$1)
    endif

    ifeq (environment,$$(firstword $$(origin $1)))
      # Unset the target variable if it originates from the environment.
      $$(if $$(DEBUG_MK),$$(info override undefine $1 [$$(origin $1)]))
      override undefine $1
    else ifeq (default,$$(origin $1))
      # The target variable is a make builtin default.
      ifdef 2
        # Explicit fallback takes precedence over a builtin default value.
        $$(if $$(DEBUG_MK),$$(info override undefine $1 [default]))
        override undefine $1
      else ifneq (undefined,$$(origin __default_$1))
        # The __default variable takes precedence over the builtin default.
        $$(if $$(DEBUG_MK),$$(info override undefine $1 [$$(origin $1)]))
        override undefine $1
      endif
    else ifneq (undefined,$$(origin $1))
      # The target variable was set in the makefile or on the command line.
      # Unset if it's empty, as we only accept nonempty argument variables
      # from the user. To allow empty command line variables, __default_$1
      # must be defined as empty (not undefined), and there must not be an
      # explicit fallback.
      ifeq (simple,$$(flavor $1))
        ifeq (,$$(strip $$($1)))
          $$(if $$(DEBUG_MK),$$(info override undefine $1 [$$(origin $1)]))
          override undefine $1
        endif
      else ifeq (,$$(strip $$(value $1)))
        $$(if $$(DEBUG_MK),$$(info override undefine $1 [$$(origin $1)]))
        override undefine $1
      endif
    endif

    ifeq (undefined,$$(origin $1))
      # The target variable is undefined, i.e. we can modify it.
      ifdef 2
        $$(if $$(DEBUG_MK),$$(info override $1 = $$$$(eval override $1 := $$(value 2))$$$$($1)))
        override $1 = $$(eval override $1 := $(value 2))$$($1)
      else ifneq (undefined,$$(origin __default_$1))
        $$(if $$(DEBUG_MK),$$(info override $1 = $$$$(eval override $1 := $$(value __default_$1))$$$$($1)))
        override $1 = $$(eval override $1 := $$(value __default_$1))$$($1)
      endif
    endif
  )
endef
*/
	return nullptr;
}
