/*
 * fypal-role.c - named roles, inheritance and their escapes
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fypal-internal.h"

#define ROLE_DEPTH_MAX	16
#define COLOR_TEXT_MAX	64

static const struct {
	const char *name;
	unsigned int attr;
	const char *on;
} attr_desc[] = {
	{ "bold", FYPAL_ATTR_BOLD, "1" },
	{ "dim", FYPAL_ATTR_DIM, "2" },
	{ "italic", FYPAL_ATTR_ITALIC, "3" },
	{ "underline", FYPAL_ATTR_UNDERLINE, "4" },
	{ "undercurl", FYPAL_ATTR_UNDERCURL, "4:3" },
	{ "blink", FYPAL_ATTR_BLINK, "5" },
	{ "reverse", FYPAL_ATTR_REVERSE, "7" },
	{ "strike", FYPAL_ATTR_STRIKE, "9" },
	{ "overline", FYPAL_ATTR_OVERLINE, "53" },
};

static void style_unset(struct fypal_style *s)
{
	s->fg = FYPAL_COLOR_UNSET;
	s->bg = FYPAL_COLOR_UNSET;
	s->ul = FYPAL_COLOR_UNSET;
	s->attrs_set = 0;
	s->attrs_clear = 0;
}

bool fypal_path_valid_(const char *name)
{
	const char *p;
	bool start = true;

	if (!name || !*name || strlen(name) >= FYPAL_NAME_MAX)
		return false;
	for (p = name; *p; p++) {
		if (*p == '.') {
			if (start)
				return false;
			start = true;
			continue;
		}
		if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-')
			return false;
		start = false;
	}
	return !start;
}

void fypal_ctx_roles_destroy_(struct fypal_ctx *ctx)
{
	size_t i;

	for (i = 0; i < ctx->nroles; i++) {
		free(ctx->roles[i]->name);
		free(ctx->roles[i]->base);
		free(ctx->roles[i]);
	}
	free(ctx->roles);
	ctx->roles = NULL;
	ctx->nroles = ctx->aroles = 0;
	fypal_hash_release_(&ctx->role_hash);
	fypal_hash_release_(&ctx->query_hash);
}

static struct fypal_role *find_exact(const struct fypal_ctx *ctx,
				     const char *name, size_t len)
{
	return fypal_hash_find_(&ctx->role_hash, name, len);
}

/* The defined role for a name, or its nearest defined dotted ancestor. */
static struct fypal_role *find_fallback(const struct fypal_ctx *ctx,
					const char *name, size_t len)
{
	struct fypal_role *r;

	while (len) {
		r = find_exact(ctx, name, len);
		if (r)
			return r;
		while (len && name[len - 1] != '.')
			len--;
		if (len)
			len--;
	}
	return NULL;
}

static const struct fypal_role *role_parent(const struct fypal_ctx *ctx,
					    const struct fypal_role *role)
{
	const struct fypal_role *p = NULL;
	const char *dot;

	if (role->base)
		p = find_fallback(ctx, role->base, strlen(role->base));
	if (!p || p == role) {
		dot = strrchr(role->name, '.');
		p = dot ? find_fallback(ctx, role->name,
					(size_t)(dot - role->name)) : NULL;
	}
	return p == role ? NULL : p;
}

/* Inheritance applied; colour references stay references. */
static void resolve_raw(const struct fypal_ctx *ctx,
			const struct fypal_role *role, struct fypal_style *out,
			int depth)
{
	const struct fypal_style *s = &role->style;
	const struct fypal_role *parent;

	parent = depth < ROLE_DEPTH_MAX ? role_parent(ctx, role) : NULL;
	if (parent)
		resolve_raw(ctx, parent, out, depth + 1);
	else
		style_unset(out);

	if (s->fg != FYPAL_COLOR_UNSET)
		out->fg = s->fg;
	if (s->bg != FYPAL_COLOR_UNSET)
		out->bg = s->bg;
	if (s->ul != FYPAL_COLOR_UNSET)
		out->ul = s->ul;
	out->attrs_set = (out->attrs_set & ~s->attrs_clear) | s->attrs_set;
	out->attrs_clear = 0;
}

bool fypal_ctx_background_uses_(const struct fypal_ctx *ctx, size_t index)
{
	struct fypal_style style;
	size_t i;

	for (i = 0; i < ctx->nroles; i++) {
		resolve_raw(ctx, ctx->roles[i], &style, 0);
		if (FYPAL_COLOR_IS_REF(style.bg) &&
		    FYPAL_COLOR_REF_INDEX(style.bg) == index)
			return true;
	}
	return false;
}

int fypal_ctx_define_role_style(struct fypal_ctx *ctx, const char *name,
				const struct fypal_style *style,
				const char *base)
{
	struct fypal_role *r;
	char *base_copy = NULL;

	if (!ctx)
		return -1;
	if (!style || !fypal_path_valid_(name) ||
	    (base && !fypal_path_valid_(base))) {
		fypal_ctx_error_set_(ctx, "api: invalid role '%s'",
				     name ? name : "");
		errno = EINVAL;
		return -1;
	}
	if (base) {
		base_copy = strdup(base);
		if (!base_copy)
			goto err_nomem;
	}

	r = find_exact(ctx, name, strlen(name));
	if (!r) {
		r = calloc(1, sizeof(*r));
		if (!r)
			goto err_nomem;
		r->name = strdup(name);
		if (!r->name ||
		    FYPAL_ARRAY_PUSH(ctx->roles, ctx->nroles, ctx->aroles, r)) {
			free(r->name);
			free(r);
			goto err_nomem;
		}
		if (fypal_hash_insert_(&ctx->role_hash, r->name,
				       strlen(r->name), r, false)) {
			ctx->nroles--;
			free(r->name);
			free(r);
			goto err_nomem;
		}
		/* a new role can answer a query that fell back before */
		fypal_hash_clear_(&ctx->query_hash);
	}
	free(r->base);
	r->base = base_copy;
	r->style = *style;
	r->gen = 0;
	fypal_ctx_changed_(ctx);
	return 0;

err_nomem:
	free(base_copy);
	fypal_ctx_error_set_(ctx, "api: out of memory");
	errno = ENOMEM;
	return -1;
}

static int parse_color(struct fypal_ctx *ctx, const char *s, size_t len,
		       uint32_t *out)
{
	char buf[COLOR_TEXT_MAX];
	uint32_t c;

	if (!len || len >= sizeof(buf))
		return -1;
	memcpy(buf, s, len);
	buf[len] = '\0';
	if (!strcmp(buf, "default")) {
		*out = FYPAL_COLOR_DEFAULT;
		return 0;
	}
	if (buf[0] == '#' || isdigit((unsigned char)buf[0]) ||
	    !strncmp(buf, "oklch(", 6)) {
		c = fypal_color_parse(buf);
	} else {
		/* a name refers to a theme colour, defined now or later */
		c = fypal_ctx_color_ref(ctx, buf);
	}
	if (c == FYPAL_RGB_INVALID || c == FYPAL_COLOR_UNSET)
		return -1;
	*out = c;
	return 0;
}

/* The next blank-separated field; blanks inside parentheses do not split. */
static const char *next_field(const char *p, size_t *len)
{
	const char *s;
	int paren = 0;

	while (*p && isspace((unsigned char)*p))
		p++;
	s = p;
	while (*p && (paren || !isspace((unsigned char)*p))) {
		if (*p == '(')
			paren++;
		else if (*p == ')' && paren)
			paren--;
		p++;
	}
	*len = (size_t)(p - s);
	return s;
}

int fypal_ctx_define_role_fields_(struct fypal_ctx *ctx, const char *name,
				  const char *fields, const char *where)
{
	char base[FYPAL_NAME_MAX];
	struct fypal_style style;
	const char *f, *p;
	uint32_t *slot;
	size_t len, i, alen;
	bool have_base = false, clear;

	if (!where)
		where = "api";
	if (!fypal_path_valid_(name) || !fields) {
		fypal_ctx_error_set_(ctx, "%s: invalid role '%s'", where,
				     name ? name : "");
		return -1;
	}
	style_unset(&style);
	for (p = fields;; p = f + len) {
		f = next_field(p, &len);
		if (!len)
			break;

		slot = NULL;
		if (len >= 3 && !strncmp(f, "fg=", 3))
			slot = &style.fg;
		else if (len >= 3 && !strncmp(f, "bg=", 3))
			slot = &style.bg;
		else if (len >= 3 && !strncmp(f, "ul=", 3))
			slot = &style.ul;
		if (slot) {
			if (parse_color(ctx, f + 3, len - 3, slot)) {
				fypal_ctx_error_set_(ctx, "%s: invalid colour in '%.*s'",
						     where, (int)len, f);
				return -1;
			}
			continue;
		}
		if (len >= 5 && !strncmp(f, "base=", 5)) {
			if (len - 5 >= sizeof(base)) {
				fypal_ctx_error_set_(ctx, "%s: base is too long", where);
				return -1;
			}
			memcpy(base, f + 5, len - 5);
			base[len - 5] = '\0';
			have_base = true;
			continue;
		}

		clear = *f == '-';
		for (i = 0; i < N_ELEMENTS(attr_desc); i++) {
			alen = strlen(attr_desc[i].name);
			if (len - clear == alen &&
			    !strncmp(f + clear, attr_desc[i].name, alen))
				break;
		}
		if (i == N_ELEMENTS(attr_desc)) {
			fypal_ctx_error_set_(ctx, "%s: unknown field '%.*s'", where,
					     (int)len, f);
			return -1;
		}
		if (clear) {
			style.attrs_clear |= attr_desc[i].attr;
			style.attrs_set &= ~attr_desc[i].attr;
		} else {
			style.attrs_set |= attr_desc[i].attr;
			style.attrs_clear &= ~attr_desc[i].attr;
		}
	}
	if (have_base && !fypal_path_valid_(base)) {
		fypal_ctx_error_set_(ctx, "%s: invalid base '%s'", where, base);
		return -1;
	}
	return fypal_ctx_define_role_style(ctx, name, &style,
					   have_base ? base : NULL);
}

const struct fypal_role *fypal_ctx_role(struct fypal_ctx *ctx, const char *name)
{
	struct fypal_hash_entry *e;
	struct fypal_role *r;
	size_t len;

	if (!ctx || !name)
		return NULL;
	len = strlen(name);
	/* the answer of a query is remembered, a miss included */
	e = fypal_hash_lookup_(&ctx->query_hash, name, len);
	if (e)
		return e->value;
	r = find_fallback(ctx, name, len);
	/* a failed insert only costs the next query its walk */
	(void)fypal_hash_insert_(&ctx->query_hash, name, len, r, true);
	return r;
}

size_t fypal_ctx_role_count(const struct fypal_ctx *ctx)
{
	return ctx ? ctx->nroles : 0;
}

const struct fypal_role *fypal_ctx_role_at(const struct fypal_ctx *ctx,
					   size_t index)
{
	return ctx && index < ctx->nroles ? ctx->roles[index] : NULL;
}

const char *fypal_role_name(const struct fypal_role *role)
{
	return role ? role->name : NULL;
}

const char *fypal_role_base(const struct fypal_role *role)
{
	return role ? role->base : NULL;
}

const struct fypal_style *fypal_role_style(const struct fypal_role *role)
{
	return role ? &role->style : NULL;
}

static uint32_t color_rgb(struct fypal_ctx *ctx, uint32_t c)
{
	const struct fypal_color *col;

	if (!FYPAL_COLOR_IS_REF(c))
		return c;
	col = fypal_ctx_color_get_(ctx, c);
	return col && col->rgb != FYPAL_RGB_INVALID ? col->rgb : FYPAL_COLOR_UNSET;
}

void fypal_ctx_resolve(struct fypal_ctx *ctx, const struct fypal_role *role,
		       struct fypal_style *out)
{
	style_unset(out);
	if (!ctx || !role)
		return;
	resolve_raw(ctx, role, out, 0);
	out->fg = color_rgb(ctx, out->fg);
	out->bg = color_rgb(ctx, out->bg);
	out->ul = color_rgb(ctx, out->ul);
}

/* The parameters leave room for the "\033[" and "m" that enclose them. */
struct sgr_buf {
	char s[FYPAL_SGR_MAX - 3];
	size_t len;
};

static void sgr_add(struct sgr_buf *b, const char *param)
{
	int n;

	if (!*param || b->len >= sizeof(b->s) - 1)
		return;
	n = snprintf(b->s + b->len, sizeof(b->s) - b->len, "%s%s",
		     b->len ? ";" : "", param);
	if (n > 0)
		b->len += (size_t)n;
	if (b->len >= sizeof(b->s))
		b->len = sizeof(b->s) - 1;
}

static void sgr_color(struct fypal_ctx *ctx, uint32_t c,
		      enum fypal_layer layer, struct sgr_buf *on,
		      struct sgr_buf *off)
{
	static const char *const reset[] = {
		[FYPAL_LAYER_FG] = "39", [FYPAL_LAYER_BG] = "49",
		[FYPAL_LAYER_UL] = "59",
	};
	const struct fypal_caps *caps = &ctx->caps;
	char p[40];

	p[0] = '\0';
	if (c == FYPAL_COLOR_UNSET || caps->depth == FYPAL_DEPTH_NONE)
		return;
	if (layer == FYPAL_LAYER_UL && !caps->underline_color)
		return;
	if (c == FYPAL_COLOR_DEFAULT)
		snprintf(p, sizeof(p), "%s", reset[layer]);
	else if (FYPAL_COLOR_IS_REF(c))
		fypal_ctx_color_params_(ctx, fypal_ctx_color_get_(ctx, c), layer,
					p, sizeof(p));
	else if (FYPAL_COLOR_IS_RGB(c))
		fypal_sgr_params_rgb(c, layer, caps->depth, p, sizeof(p));
	if (!*p)
		return;
	sgr_add(on, p);
	sgr_add(off, reset[layer]);
}

static void style_escapes(struct fypal_ctx *ctx, const struct fypal_style *style,
			  char *on, size_t on_size, char *off, size_t off_size)
{
	const struct fypal_caps *caps = &ctx->caps;
	struct sgr_buf bon = { .len = 0 }, boff = { .len = 0 };
	unsigned int want, attrs;
	size_t i;

	want = style->attrs_set & ~style->attrs_clear;
	attrs = want & caps->attrs;
	/* a terminal without the curl still has the underline */
	if ((want & FYPAL_ATTR_UNDERCURL) && !(caps->attrs & FYPAL_ATTR_UNDERCURL))
		attrs |= FYPAL_ATTR_UNDERLINE & caps->attrs;

	for (i = 0; i < N_ELEMENTS(attr_desc); i++) {
		if (!(attrs & attr_desc[i].attr))
			continue;
		if (attr_desc[i].attr == FYPAL_ATTR_UNDERLINE &&
		    (attrs & FYPAL_ATTR_UNDERCURL))
			continue;
		sgr_add(&bon, attr_desc[i].on);
	}
	if (attrs & (FYPAL_ATTR_BOLD | FYPAL_ATTR_DIM))
		sgr_add(&boff, "22");
	if (attrs & FYPAL_ATTR_ITALIC)
		sgr_add(&boff, "23");
	if (attrs & (FYPAL_ATTR_UNDERLINE | FYPAL_ATTR_UNDERCURL))
		sgr_add(&boff, "24");
	if (attrs & FYPAL_ATTR_BLINK)
		sgr_add(&boff, "25");
	if (attrs & FYPAL_ATTR_REVERSE)
		sgr_add(&boff, "27");
	if (attrs & FYPAL_ATTR_STRIKE)
		sgr_add(&boff, "29");
	if (attrs & FYPAL_ATTR_OVERLINE)
		sgr_add(&boff, "55");

	sgr_color(ctx, style->fg, FYPAL_LAYER_FG, &bon, &boff);
	sgr_color(ctx, style->bg, FYPAL_LAYER_BG, &bon, &boff);
	sgr_color(ctx, style->ul, FYPAL_LAYER_UL, &bon, &boff);

	if (on && on_size) {
		if (bon.len)
			snprintf(on, on_size, "\033[%sm", bon.s);
		else
			on[0] = '\0';
	}
	if (off && off_size) {
		if (boff.len)
			snprintf(off, off_size, "\033[%sm", boff.s);
		else
			off[0] = '\0';
	}
}

/*
 * The escapes are cached in the role. The cache is not part of the role's
 * value, so it is written through the const pointer callers hold.
 */
static struct fypal_role *role_escapes(struct fypal_ctx *ctx,
				       const struct fypal_role *role)
{
	struct fypal_role *r = (struct fypal_role *)role;
	struct fypal_style style;

	fypal_ctx_derive_(ctx);
	if (r->gen != ctx->gen) {
		resolve_raw(ctx, r, &style, 0);
		style_escapes(ctx, &style, r->on, sizeof(r->on), r->off,
			      sizeof(r->off));
		r->gen = ctx->gen;
	}
	return r;
}

const char *fypal_role_on(struct fypal_ctx *ctx, const struct fypal_role *role)
{
	if (!ctx || !role)
		return "";
	return role_escapes(ctx, role)->on;
}

const char *fypal_role_off(struct fypal_ctx *ctx, const struct fypal_role *role)
{
	if (!ctx || !role)
		return "";
	return role_escapes(ctx, role)->off;
}

const char *fypal_ctx_on(struct fypal_ctx *ctx, const char *name)
{
	return fypal_role_on(ctx, fypal_ctx_role(ctx, name));
}

const char *fypal_ctx_off(struct fypal_ctx *ctx, const char *name)
{
	return fypal_role_off(ctx, fypal_ctx_role(ctx, name));
}

int fypal_ctx_style_sgr(struct fypal_ctx *ctx, const struct fypal_style *style,
			char *on, size_t on_size, char *off, size_t off_size)
{
	char tmp[FYPAL_SGR_MAX];

	if (!ctx || !style) {
		if (on && on_size)
			on[0] = '\0';
		if (off && off_size)
			off[0] = '\0';
		return 0;
	}
	fypal_ctx_derive_(ctx);
	style_escapes(ctx, style, tmp, sizeof(tmp), off, off_size);
	return snprintf(on, on_size, "%s", tmp);
}
