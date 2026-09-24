/*
 * fypal-theme.c - theme context: definitions, evaluation and loading
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-generic.h>

#include "fypal-internal.h"

#define EVAL_DEPTH_MAX	64

#define PARSE_FLAGS	(FYOPPF_DISABLE_DIRECTORY | FYOPPF_MODE_YAML_1_2 | \
			 FYOPPF_COLLECT_DIAG)

static const char *const variant_names[2] = { "dark", "light" };

/* The theme format version this library reads. */
#define THEME_VERSION	1

void fypal_ctx_error_set_(struct fypal_ctx *ctx, const char *fmt, ...)
{
	va_list ap;

	/* the first error is the cause; later ones follow from it */
	if (ctx->err[0])
		return;
	va_start(ap, fmt);
	vsnprintf(ctx->err, sizeof(ctx->err), fmt, ap);
	va_end(ap);
}

void fypal_ctx_error_clear_(struct fypal_ctx *ctx)
{
	ctx->err[0] = '\0';
}

const char *fypal_ctx_error(const struct fypal_ctx *ctx)
{
	return ctx ? ctx->err : "";
}

void fypal_ctx_changed_(struct fypal_ctx *ctx)
{
	/* 0 marks a role without escapes, so the generation skips it */
	if (!++ctx->gen)
		ctx->gen = 1;
}

static void caps_full(struct fypal_caps *caps)
{
	caps->depth = FYPAL_DEPTH_TRUECOLOR;
	caps->attrs = FYPAL_ATTR_ALL;
	caps->underline_color = true;
}

struct fypal_ctx *fypal_ctx_create(const struct fypal_caps *caps)
{
	struct fypal_ctx *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;
	ctx->variant = FYPAL_VARIANT_DARK;
	if (caps)
		ctx->caps = *caps;
	else
		caps_full(&ctx->caps);
	ctx->gen = 1;
	return ctx;
}

static void def_release(struct fypal_def *def)
{
	int s;

	free(def->name);
	for (s = 0; s < FYPAL_SECTIONS; s++) {
		free(def->expr[s]);
		free(def->where[s]);
	}
}

void fypal_ctx_destroy(struct fypal_ctx *ctx)
{
	size_t i;

	if (!ctx)
		return;
	for (i = 0; i < ctx->nparams; i++) {
		def_release(&ctx->params[i]->def);
		free(ctx->params[i]);
	}
	free(ctx->params);
	for (i = 0; i < ctx->ncolors; i++) {
		def_release(&ctx->colors[i]->def);
		free(ctx->colors[i]);
	}
	free(ctx->colors);
	for (i = 0; i < 16; i++)
		def_release(&ctx->term16[i]);
	fypal_hash_release_(&ctx->param_hash);
	fypal_hash_release_(&ctx->color_hash);
	for (i = 0; i < 3; i++)
		free(ctx->ground[i]);
	fypal_ctx_roles_destroy_(ctx);
	fypal_ctx_glyphs_destroy_(ctx);
	free(ctx);
}

void fypal_ctx_set_variant(struct fypal_ctx *ctx, enum fypal_variant variant)
{
	if (!ctx)
		return;
	ctx->variant = variant == FYPAL_VARIANT_LIGHT ? FYPAL_VARIANT_LIGHT :
							FYPAL_VARIANT_DARK;
	fypal_ctx_changed_(ctx);
}

enum fypal_variant fypal_ctx_variant(const struct fypal_ctx *ctx)
{
	return ctx ? ctx->variant : FYPAL_VARIANT_DARK;
}

void fypal_ctx_set_caps(struct fypal_ctx *ctx, const struct fypal_caps *caps)
{
	if (!ctx)
		return;
	if (caps)
		ctx->caps = *caps;
	else
		caps_full(&ctx->caps);
	fypal_ctx_changed_(ctx);
}

const struct fypal_caps *fypal_ctx_caps(const struct fypal_ctx *ctx)
{
	return ctx ? &ctx->caps : NULL;
}

void fypal_ctx_set_surface_contrast(struct fypal_ctx *ctx, double ratio)
{
	if (!ctx)
		return;
	if (!isfinite(ratio) || ratio < 1.0)
		ratio = 0;
	if (ratio > 21.0)
		ratio = 21.0;
	ctx->surface_contrast = ratio;
	fypal_ctx_changed_(ctx);
}

void fypal_ctx_set_surface_scope(struct fypal_ctx *ctx,
				 enum fypal_surface_scope scope)
{
	if (!ctx)
		return;
	ctx->surface_scope = scope == FYPAL_SURFACE_ALL ? FYPAL_SURFACE_ALL :
							FYPAL_SURFACE_SELECTED;
	fypal_ctx_changed_(ctx);
}

/* A parameter or colour name: an identifier that may contain dots. */
static bool def_name_valid(const char *name, size_t len)
{
	size_t i;

	if (!len || len >= FYPAL_NAME_MAX || name[len - 1] == '.')
		return false;
	if (!isalpha((unsigned char)name[0]) && name[0] != '_')
		return false;
	for (i = 1; i < len; i++) {
		if (!isalnum((unsigned char)name[i]) && name[i] != '_' &&
		    name[i] != '.')
			return false;
	}
	/* "default" names the terminal colour in a role */
	return !(len == 7 && !strncmp(name, "default", 7));
}

static struct fypal_param *param_find(const struct fypal_ctx *ctx,
				      const char *name, size_t len)
{
	return fypal_hash_find_(&ctx->param_hash, name, len);
}

static ssize_t color_index(const struct fypal_ctx *ctx, const char *name,
			   size_t len)
{
	struct fypal_color *c;

	c = fypal_hash_find_(&ctx->color_hash, name, len);
	return c ? (ssize_t)c->index : -1;
}

static struct fypal_param *param_get(struct fypal_ctx *ctx, const char *name)
{
	struct fypal_param *p;

	p = param_find(ctx, name, strlen(name));
	if (p)
		return p;
	p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	p->def.name = strdup(name);
	if (!p->def.name || FYPAL_ARRAY_PUSH(ctx->params, ctx->nparams,
					     ctx->aparams, p)) {
		free(p->def.name);
		free(p);
		return NULL;
	}
	if (fypal_hash_insert_(&ctx->param_hash, p->def.name,
			       strlen(p->def.name), p, false)) {
		ctx->nparams--;
		free(p->def.name);
		free(p);
		return NULL;
	}
	return p;
}

static ssize_t color_get(struct fypal_ctx *ctx, const char *name)
{
	struct fypal_color *c;
	ssize_t idx;
	int s;

	idx = color_index(ctx, name, strlen(name));
	if (idx >= 0)
		return idx;
	if (ctx->ncolors >= 0xffffff)
		return -1;
	c = calloc(1, sizeof(*c));
	if (!c)
		return -1;
	c->def.name = strdup(name);
	c->index = ctx->ncolors;
	if (!c->def.name || FYPAL_ARRAY_PUSH(ctx->colors, ctx->ncolors,
					     ctx->acolors, c)) {
		free(c->def.name);
		free(c);
		return -1;
	}
	if (fypal_hash_insert_(&ctx->color_hash, c->def.name,
			       strlen(c->def.name), c, false)) {
		/* the array owns it now; drop it from the array again */
		ctx->ncolors--;
		free(c->def.name);
		free(c);
		return -1;
	}
	for (s = 0; s < FYPAL_SECTIONS; s++)
		c->ansi16[s] = FYPAL_ANSI_UNSET;
	c->rgb = FYPAL_RGB_INVALID;
	fypal_ctx_changed_(ctx);
	return (ssize_t)(ctx->ncolors - 1);
}

static int def_set(struct fypal_ctx *ctx, struct fypal_def *def,
		   enum fypal_section section, const char *expr,
		   const char *where)
{
	char *e, *w;

	e = strdup(expr);
	w = strdup(where ? where : "api");
	if (!e || !w) {
		free(e);
		free(w);
		fypal_ctx_error_set_(ctx, "%s: out of memory", where ? where : "api");
		return -1;
	}
	free(def->expr[section]);
	free(def->where[section]);
	def->expr[section] = e;
	def->where[section] = w;
	fypal_ctx_changed_(ctx);
	return 0;
}

static bool section_valid(enum fypal_section section)
{
	return section >= FYPAL_SECTION_ALL && section <= FYPAL_SECTION_LIGHT;
}

static int define_param(struct fypal_ctx *ctx, const char *name,
			enum fypal_section section, const char *expr,
			const char *where)
{
	struct fypal_param *p;

	if (!name || !def_name_valid(name, strlen(name)) || !expr ||
	    !section_valid(section)) {
		fypal_ctx_error_set_(ctx, "%s: invalid parameter '%s'",
				     where ? where : "api", name ? name : "");
		return -1;
	}
	p = param_get(ctx, name);
	if (!p) {
		fypal_ctx_error_set_(ctx, "%s: out of memory", where ? where : "api");
		return -1;
	}
	if (def_set(ctx, &p->def, section, expr, where))
		return -1;
	p->text[section] = false;
	p->auto_text[section] = false;
	return 0;
}

static bool param_is_text(const struct fypal_ctx *ctx, const struct fypal_param *p)
{
	const char *expr, *start, *q;
	enum fypal_section s = ctx->variant == FYPAL_VARIANT_LIGHT ?
		FYPAL_SECTION_LIGHT : FYPAL_SECTION_DARK;
	if (!p->def.expr[s])
		s = FYPAL_SECTION_ALL;
	if (p->text[s])
		return true;
	if (!p->auto_text[s])
		return false;
	expr = p->def.expr[s];
	if (!expr || (!isalpha((unsigned char)*expr) && *expr != '_'))
		return false;
	/* Symbolic scalar values are strings unless they name parameters.
	 * Resolve after loading, so forward numeric references still work. */
	start = expr;
	for (q = expr; ; q++) {
		if (*q == '-' || !*q) {
			if (q == start || param_find(ctx, start, (size_t)(q - start)))
				return false;
			if (!*q)
				return true;
			start = q + 1;
		} else if (!isalnum((unsigned char)*q) && *q != '_' && *q != '.') {
			return false;
		}
	}
}

int fypal_ctx_set_param_string(struct fypal_ctx *ctx, const char *name,
			      enum fypal_section section, const char *value)
{
	int rc;

	if (!ctx)
		return -1;
	fypal_ctx_error_clear_(ctx);
	rc = define_param(ctx, name, section, value, NULL);
	if (rc)
		return rc;
	param_find(ctx, name, strlen(name))->text[section] = true;
	return 0;
}

const char *fypal_ctx_param_string(struct fypal_ctx *ctx, const char *name)
{
	struct fypal_param *p;
	enum fypal_section s;

	if (!ctx || !name)
		return NULL;
	p = param_find(ctx, name, strlen(name));
	if (!p || !param_is_text(ctx, p))
		return NULL;
	s = ctx->variant == FYPAL_VARIANT_LIGHT ? FYPAL_SECTION_LIGHT :
						FYPAL_SECTION_DARK;
	return p->def.expr[p->def.expr[s] ? s : FYPAL_SECTION_ALL];
}

static int define_color(struct fypal_ctx *ctx, const char *name,
			enum fypal_section section, const char *expr,
			const char *where)
{
	ssize_t idx;

	if (!name || !def_name_valid(name, strlen(name)) || !expr ||
	    !section_valid(section)) {
		fypal_ctx_error_set_(ctx, "%s: invalid colour '%s'",
				     where ? where : "api", name ? name : "");
		return -1;
	}
	idx = color_get(ctx, name);
	if (idx < 0) {
		fypal_ctx_error_set_(ctx, "%s: out of memory", where ? where : "api");
		return -1;
	}
	return def_set(ctx, &ctx->colors[idx]->def, section, expr, where);
}

static int set_ansi16(struct fypal_ctx *ctx, const char *name,
		      enum fypal_section section, int index, const char *where)
{
	ssize_t idx;

	if (!name || !def_name_valid(name, strlen(name)) ||
	    !section_valid(section) || index < FYPAL_ANSI_NONE || index > 15) {
		fypal_ctx_error_set_(ctx, "%s: invalid ansi16 form of '%s'",
				     where ? where : "api", name ? name : "");
		return -1;
	}
	idx = color_get(ctx, name);
	if (idx < 0) {
		fypal_ctx_error_set_(ctx, "%s: out of memory", where ? where : "api");
		return -1;
	}
	ctx->colors[idx]->ansi16[section] = index;
	fypal_ctx_changed_(ctx);
	return 0;
}

static int define_terminal16(struct fypal_ctx *ctx, int index,
			     enum fypal_section section, const char *expr,
			     const char *where)
{
	struct fypal_def *def;
	char name[32];

	if (index < 0 || index > 15 || !expr || !section_valid(section)) {
		fypal_ctx_error_set_(ctx, "%s: invalid terminal16 slot %d",
				     where ? where : "api", index);
		return -1;
	}
	def = &ctx->term16[index];
	if (!def->name) {
		snprintf(name, sizeof(name), "terminal16.%d", index);
		def->name = strdup(name);
		if (!def->name) {
			fypal_ctx_error_set_(ctx, "%s: out of memory",
					     where ? where : "api");
			return -1;
		}
	}
	return def_set(ctx, def, section, expr, where);
}

int fypal_ctx_define_param(struct fypal_ctx *ctx, const char *name,
			   enum fypal_section section, const char *expr)
{
	if (!ctx)
		return -1;
	fypal_ctx_error_clear_(ctx);
	return define_param(ctx, name, section, expr, NULL);
}

int fypal_ctx_set_param(struct fypal_ctx *ctx, const char *name,
			enum fypal_section section, double value)
{
	char buf[40];

	if (!ctx)
		return -1;
	fypal_ctx_error_clear_(ctx);
	if (!isfinite(value)) {
		fypal_ctx_error_set_(ctx, "api: parameter '%s' is not finite",
				     name ? name : "");
		return -1;
	}
	snprintf(buf, sizeof(buf), "%.17g", value);
	return define_param(ctx, name, section, buf, NULL);
}

/* Name the parameters of the ground: @names holds l, c and h. */
static int define_ground(struct fypal_ctx *ctx, const char *const *names,
			 const char *where)
{
	char *copy[3] = { NULL, NULL, NULL };
	int i;

	for (i = 0; i < 3; i++) {
		if (!names[i] || !def_name_valid(names[i], strlen(names[i]))) {
			fypal_ctx_error_set_(ctx, "%s: ground: must name l, c and h",
					     where);
			goto err;
		}
	}
	for (i = 0; i < 3; i++) {
		copy[i] = strdup(names[i]);
		if (!copy[i]) {
			fypal_ctx_error_set_(ctx, "%s: out of memory", where);
			goto err;
		}
	}
	for (i = 0; i < 3; i++) {
		free(ctx->ground[i]);
		ctx->ground[i] = copy[i];
	}
	fypal_ctx_changed_(ctx);
	return 0;
err:
	for (i = 0; i < 3; i++)
		free(copy[i]);
	return -1;
}

int fypal_ctx_define_ground(struct fypal_ctx *ctx, const char *l,
			    const char *c, const char *h)
{
	const char *names[3] = { l, c, h };

	if (!ctx)
		return -1;
	fypal_ctx_error_clear_(ctx);
	return define_ground(ctx, names, "api");
}

int fypal_ctx_set_ground(struct fypal_ctx *ctx, uint32_t rgb)
{
	enum fypal_section section;
	struct fypal_lch lch;
	double v[3];
	char buf[40];
	int i;

	if (!ctx)
		return -1;
	fypal_ctx_error_clear_(ctx);
	if (!ctx->ground[0]) {
		fypal_ctx_error_set_(ctx, "api: the theme names no ground");
		return -1;
	}
	if (rgb & 0xff000000U) {
		fypal_ctx_error_set_(ctx, "api: invalid ground colour 0x%08x", rgb);
		return -1;
	}
	lch = fypal_lab_to_lch(fypal_rgb_to_lab(rgb));
	v[0] = lch.L;
	v[1] = lch.C;
	v[2] = lch.h;
	section = ctx->variant == FYPAL_VARIANT_LIGHT ? FYPAL_SECTION_LIGHT :
							FYPAL_SECTION_DARK;
	for (i = 0; i < 3; i++) {
		/* a grey has no hue */
		snprintf(buf, sizeof(buf), "%.17g", isfinite(v[i]) ? v[i] : 0.0);
		if (define_param(ctx, ctx->ground[i], section, buf, NULL))
			return -1;
	}
	return 0;
}

int fypal_ctx_define_color(struct fypal_ctx *ctx, const char *name,
			   enum fypal_section section, const char *expr)
{
	if (!ctx)
		return -1;
	fypal_ctx_error_clear_(ctx);
	return define_color(ctx, name, section, expr, NULL);
}

int fypal_ctx_set_ansi16(struct fypal_ctx *ctx, const char *name,
			 enum fypal_section section, int index)
{
	if (!ctx)
		return -1;
	fypal_ctx_error_clear_(ctx);
	return set_ansi16(ctx, name, section, index, NULL);
}

int fypal_ctx_define_terminal16(struct fypal_ctx *ctx, int index,
				enum fypal_section section, const char *expr)
{
	if (!ctx)
		return -1;
	fypal_ctx_error_clear_(ctx);
	return define_terminal16(ctx, index, section, expr, NULL);
}

int fypal_ctx_define_role(struct fypal_ctx *ctx, const char *name,
			  const char *fields)
{
	if (!ctx)
		return -1;
	fypal_ctx_error_clear_(ctx);
	return fypal_ctx_define_role_fields_(ctx, name, fields, NULL);
}

uint32_t fypal_ctx_color_ref(struct fypal_ctx *ctx, const char *name)
{
	ssize_t idx;

	if (!ctx || !name || !def_name_valid(name, strlen(name)))
		return FYPAL_COLOR_UNSET;
	/* a reference to a colour not yet defined declares it */
	idx = color_get(ctx, name);
	return idx < 0 ? FYPAL_COLOR_UNSET : FYPAL_COLOR_REF(idx);
}

/* ---------------------------------------------------------------------
 * Expressions
 * ------------------------------------------------------------------ */

struct eval {
	struct fypal_ctx *ctx;
	const char *p;
	const char *where;
	int depth;
	bool failed;
};

static void eval_init(struct eval *e, struct fypal_ctx *ctx, const char *text,
		      const char *where, int depth)
{
	e->ctx = ctx;
	e->p = text;
	e->where = where ? where : "api";
	e->depth = depth;
	e->failed = false;
}

static void eval_fail(struct eval *e, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static void eval_fail(struct eval *e, const char *fmt, ...)
{
	char msg[256];
	va_list ap;

	if (e->failed)
		return;
	e->failed = true;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	fypal_ctx_error_set_(e->ctx, "%s: %s (%s variant)", e->where, msg,
			     variant_names[e->ctx->variant]);
}

static void skip_ws(struct eval *e)
{
	while (*e->p && isspace((unsigned char)*e->p))
		e->p++;
}

static bool eval_accept(struct eval *e, char c)
{
	skip_ws(e);
	if (*e->p != c)
		return false;
	e->p++;
	return true;
}

static void eval_expect(struct eval *e, char c)
{
	if (!e->failed && !eval_accept(e, c))
		eval_fail(e, "expected '%c' at \"%s\"", c, e->p);
}

static size_t eval_ident(struct eval *e, const char **name)
{
	skip_ws(e);
	*name = e->p;
	if (!isalpha((unsigned char)*e->p) && *e->p != '_')
		return 0;
	while (isalnum((unsigned char)*e->p) || *e->p == '_' || *e->p == '.')
		e->p++;
	return (size_t)(e->p - *name);
}

static const char *def_expr(const struct fypal_def *def,
			    enum fypal_variant variant, const char **where)
{
	int s = variant == FYPAL_VARIANT_LIGHT ? FYPAL_SECTION_LIGHT :
						 FYPAL_SECTION_DARK;

	if (!def->expr[s])
		s = FYPAL_SECTION_ALL;
	*where = def->where[s];
	return def->expr[s];
}

/* Start evaluating a definition; false when its value is not to be computed. */
static bool def_enter(struct eval *e, struct fypal_def *def, const char *kind)
{
	if (def->eval_gen != e->ctx->eval_gen) {
		def->eval_gen = e->ctx->eval_gen;
		def->state = FYPAL_EVAL_NONE;
	}
	switch (def->state) {
	case FYPAL_EVAL_BUSY:
		eval_fail(e, "%s '%s' depends on itself", kind, def->name);
		return false;
	case FYPAL_EVAL_FAIL:
		e->failed = true;
		return false;
	case FYPAL_EVAL_DONE:
		return false;
	default:
		break;
	}
	if (e->depth >= EVAL_DEPTH_MAX) {
		eval_fail(e, "%s '%s' is nested too deeply", kind, def->name);
		return false;
	}
	return true;
}

static double eval_expr(struct eval *e);
static uint32_t eval_color(struct eval *e);

static double param_value(struct eval *e, struct fypal_param *p)
{
	const char *expr, *where;
	struct eval sub;
	double v;

	if (param_is_text(e->ctx, p)) {
		eval_fail(e, "parameter '%s' is a string", p->def.name);
		return NAN;
	}

	if (!def_enter(e, &p->def, "parameter"))
		return p->def.state == FYPAL_EVAL_DONE ? p->value : NAN;
	expr = def_expr(&p->def, e->ctx->variant, &where);
	if (!expr) {
		p->def.state = FYPAL_EVAL_FAIL;
		eval_fail(e, "parameter '%s' has no value", p->def.name);
		return NAN;
	}
	p->def.state = FYPAL_EVAL_BUSY;
	eval_init(&sub, e->ctx, expr, where, e->depth + 1);
	v = eval_expr(&sub);
	skip_ws(&sub);
	if (!sub.failed && *sub.p)
		eval_fail(&sub, "unexpected \"%s\"", sub.p);
	if (!sub.failed && !isfinite(v))
		eval_fail(&sub, "parameter '%s' is not finite", p->def.name);
	if (sub.failed) {
		p->def.state = FYPAL_EVAL_FAIL;
		e->failed = true;
		return NAN;
	}
	p->value = v;
	p->def.state = FYPAL_EVAL_DONE;
	return v;
}

static uint32_t color_value(struct eval *e, struct fypal_color *c)
{
	const char *expr, *where;
	struct eval sub;
	uint32_t rgb;

	if (!def_enter(e, &c->def, "colour"))
		return c->def.state == FYPAL_EVAL_DONE ? c->rgb : FYPAL_RGB_INVALID;
	expr = def_expr(&c->def, e->ctx->variant, &where);
	if (!expr) {
		c->def.state = FYPAL_EVAL_FAIL;
		eval_fail(e, "colour '%s' is not defined", c->def.name);
		return FYPAL_RGB_INVALID;
	}
	c->def.state = FYPAL_EVAL_BUSY;
	eval_init(&sub, e->ctx, expr, where, e->depth + 1);
	rgb = eval_color(&sub);
	skip_ws(&sub);
	if (!sub.failed && *sub.p)
		eval_fail(&sub, "unexpected \"%s\"", sub.p);
	if (sub.failed) {
		c->def.state = FYPAL_EVAL_FAIL;
		e->failed = true;
		return FYPAL_RGB_INVALID;
	}
	c->rgb = rgb;
	c->def.state = FYPAL_EVAL_DONE;
	return rgb;
}

static double eval_factor(struct eval *e)
{
	struct fypal_param *p;
	const char *name;
	size_t len;
	char *end;
	double v;

	if (e->failed)
		return NAN;
	if (eval_accept(e, '-'))
		return -eval_factor(e);
	if (eval_accept(e, '+'))
		return eval_factor(e);
	if (eval_accept(e, '(')) {
		v = eval_expr(e);
		eval_expect(e, ')');
		return v;
	}
	if (isdigit((unsigned char)*e->p) ||
	    (*e->p == '.' && isdigit((unsigned char)e->p[1]))) {
		v = strtod(e->p, &end);
		e->p = end;
		return v;
	}
	len = eval_ident(e, &name);
	if (!len) {
		eval_fail(e, "expected a number at \"%s\"", e->p);
		return NAN;
	}
	p = param_find(e->ctx, name, len);
	if (!p) {
		eval_fail(e, "undefined parameter '%.*s'", (int)len, name);
		return NAN;
	}
	return param_value(e, p);
}

static double eval_term(struct eval *e)
{
	double v, r;

	v = eval_factor(e);
	for (;;) {
		if (eval_accept(e, '*')) {
			v *= eval_factor(e);
		} else if (eval_accept(e, '/')) {
			r = eval_factor(e);
			if (r == 0.0 && !e->failed)
				eval_fail(e, "division by zero");
			v /= r;
		} else {
			return v;
		}
	}
}

static double eval_expr(struct eval *e)
{
	double v;

	v = eval_term(e);
	for (;;) {
		if (eval_accept(e, '+'))
			v += eval_term(e);
		else if (eval_accept(e, '-'))
			v -= eval_term(e);
		else
			return v;
	}
}

static void eval_args(struct eval *e, double *v, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (i)
			eval_accept(e, ',');
		v[i] = eval_expr(e);
	}
	eval_expect(e, ')');
}

static uint32_t eval_color(struct eval *e)
{
	struct fypal_lch lch;
	const char *name, *s;
	uint32_t c1, c2, rgb;
	char buf[16];
	double v[3];
	ssize_t idx;
	size_t len;
	long x;
	int i;

	if (e->failed)
		return FYPAL_RGB_INVALID;
	skip_ws(e);
	if (*e->p == '#') {
		s = e->p++;
		while (isxdigit((unsigned char)*e->p))
			e->p++;
		len = (size_t)(e->p - s);
		rgb = FYPAL_RGB_INVALID;
		if (len < sizeof(buf)) {
			memcpy(buf, s, len);
			buf[len] = '\0';
			rgb = fypal_color_parse(buf);
		}
		if (rgb == FYPAL_RGB_INVALID)
			eval_fail(e, "invalid colour '%.*s'", (int)len, s);
		return rgb;
	}

	len = eval_ident(e, &name);
	if (!len) {
		eval_fail(e, "expected a colour at \"%s\"", e->p);
		return FYPAL_RGB_INVALID;
	}
	if (!eval_accept(e, '(')) {
		idx = color_index(e->ctx, name, len);
		if (idx < 0) {
			eval_fail(e, "undefined colour '%.*s'", (int)len, name);
			return FYPAL_RGB_INVALID;
		}
		return color_value(e, e->ctx->colors[idx]);
	}

	if (len == 5 && !strncmp(name, "oklch", 5)) {
		eval_args(e, v, 3);
		if (e->failed)
			return FYPAL_RGB_INVALID;
		lch.L = v[0];
		lch.C = v[1];
		lch.h = v[2];
		return fypal_lch_to_rgb(lch);
	}
	if (len == 3 && !strncmp(name, "rgb", 3)) {
		eval_args(e, v, 3);
		if (e->failed)
			return FYPAL_RGB_INVALID;
		rgb = 0;
		for (i = 0; i < 3; i++) {
			x = lround(v[i]);
			x = x < 0 ? 0 : x > 255 ? 255 : x;
			rgb = (rgb << 8) | (uint32_t)x;
		}
		return rgb;
	}
	if (len == 3 && !strncmp(name, "mix", 3)) {
		c1 = eval_color(e);
		eval_accept(e, ',');
		c2 = eval_color(e);
		eval_accept(e, ',');
		v[0] = eval_expr(e);
		eval_expect(e, ')');
		return e->failed ? FYPAL_RGB_INVALID : fypal_mix(c1, c2, v[0]);
	}
	if (len == 5 && !strncmp(name, "xterm", 5)) {
		eval_args(e, v, 1);
		if (e->failed)
			return FYPAL_RGB_INVALID;
		x = lround(v[0]);
		if (x < 0 || x > 255) {
			eval_fail(e, "xterm index %ld is out of range", x);
			return FYPAL_RGB_INVALID;
		}
		return fypal_xterm_to_rgb((int)x);
	}
	eval_fail(e, "unknown function '%.*s'", (int)len, name);
	return FYPAL_RGB_INVALID;
}

/* The derived colours remain the sole source for roles and SGR output. */
static bool surface_color_selected(const struct fypal_ctx *ctx, size_t index)
{
	static const char *const names[] = { "card", "wash_add", "wash_del" };
	size_t i;

	if (ctx->surface_scope == FYPAL_SURFACE_SELECTED) {
		for (i = 0; i < N_ELEMENTS(names); i++)
			if (!strcmp(ctx->colors[index]->def.name, names[i]))
				return true;
		return false;
	}
	return fypal_ctx_background_uses_(ctx, index);
}

static void surface_contrast_adjust(struct fypal_ctx *ctx)
{
	struct fypal_color *ground, *c;
	struct fypal_lch lch;
	uint32_t rgb, base;
	ssize_t index;
	double low, high, mid, ratio;
	size_t i;
	int step;

	if (!ctx->surface_contrast || ctx->caps.depth < FYPAL_DEPTH_256)
		return;
	index = color_index(ctx, "ground", 6);
	if (index < 0)
		return;
	ground = ctx->colors[index];
	base = ground->rgb;
	if (ctx->caps.depth == FYPAL_DEPTH_256)
		base = fypal_xterm_to_rgb(ground->xterm256);
	for (i = 0; i < ctx->ncolors; i++) {
		if ((ssize_t)i == index || !surface_color_selected(ctx, i))
			continue;
		c = ctx->colors[i];
		if (c->rgb == FYPAL_RGB_INVALID)
			continue;
		rgb = c->rgb;
		if (ctx->caps.depth == FYPAL_DEPTH_256)
			rgb = fypal_xterm_to_rgb(c->xterm256);
		if (fypal_contrast(base, rgb) >= ctx->surface_contrast)
			continue;
		lch = fypal_lab_to_lch(fypal_rgb_to_lab(c->rgb));
		low = lch.L;
		high = ctx->variant == FYPAL_VARIANT_LIGHT ? 0.0 : 1.0;
		/* Find the first representable lightness that meets the ratio. */
		for (step = 0; step < 24; step++) {
			mid = (low + high) / 2.0;
			lch.L = mid;
			rgb = fypal_lch_to_rgb(lch);
			if (ctx->caps.depth == FYPAL_DEPTH_256)
				rgb = fypal_xterm_to_rgb(fypal_rgb_to_xterm256(rgb));
			ratio = fypal_contrast(base, rgb);
			if (ratio >= ctx->surface_contrast)
				high = mid;
			else
				low = mid;
		}
		lch.L = high;
		c->rgb = fypal_lch_to_rgb(lch);
		c->xterm256 = fypal_rgb_to_xterm256(c->rgb);
	}
}

static int ctx_derive(struct fypal_ctx *ctx, bool force)
{
	const char *expr, *where;
	struct fypal_color *c;
	struct eval e;
	size_t i;
	int s, rc = 0;

	if (!force && ctx->derived_gen == ctx->gen)
		return ctx->derive_rc;
	if (!++ctx->eval_gen)
		ctx->eval_gen = 1;
	s = ctx->variant == FYPAL_VARIANT_LIGHT ? FYPAL_SECTION_LIGHT :
						  FYPAL_SECTION_DARK;

	/* a parameter without a value for this variant is not an error
	 * until something uses it */
	for (i = 0; i < ctx->nparams; i++) {
		if (param_is_text(ctx, ctx->params[i]))
			continue;
		if (!def_expr(&ctx->params[i]->def, ctx->variant, &where))
			continue;
		eval_init(&e, ctx, "", where, 0);
		param_value(&e, ctx->params[i]);
		if (e.failed)
			rc = -1;
	}

	for (i = 0; i < ctx->ncolors; i++) {
		c = ctx->colors[i];
		eval_init(&e, ctx, "", c->def.where[FYPAL_SECTION_ALL], 0);
		if (!def_expr(&c->def, ctx->variant, &where))
			e.where = "theme";
		color_value(&e, c);
		if (e.failed)
			rc = -1;
	}
	for (i = 0; i < ctx->ncolors; i++) {
		c = ctx->colors[i];
		if (c->def.state != FYPAL_EVAL_DONE) {
			c->rgb = FYPAL_RGB_INVALID;
			continue;
		}
		c->xterm256 = fypal_rgb_to_xterm256(c->rgb);
		if (c->ansi16[s] != FYPAL_ANSI_UNSET)
			c->ansi = c->ansi16[s];
		else if (c->ansi16[FYPAL_SECTION_ALL] != FYPAL_ANSI_UNSET)
			c->ansi = c->ansi16[FYPAL_SECTION_ALL];
		else
			c->ansi = fypal_rgb_to_ansi16(c->rgb);
	}

	surface_contrast_adjust(ctx);

	for (i = 0; i < 16; i++) {
		ctx->term16_rgb[i] = FYPAL_RGB_INVALID;
		expr = def_expr(&ctx->term16[i], ctx->variant, &where);
		if (!expr)
			continue;
		eval_init(&e, ctx, expr, where, 0);
		ctx->term16_rgb[i] = eval_color(&e);
		skip_ws(&e);
		if (!e.failed && *e.p)
			eval_fail(&e, "unexpected \"%s\"", e.p);
		if (e.failed) {
			ctx->term16_rgb[i] = FYPAL_RGB_INVALID;
			rc = -1;
		}
	}

	ctx->derived_gen = ctx->gen;
	ctx->derive_rc = rc;
	return rc;
}

int fypal_ctx_derive_(struct fypal_ctx *ctx)
{
	return ctx_derive(ctx, false);
}

int fypal_ctx_check(struct fypal_ctx *ctx)
{
	if (!ctx)
		return -1;
	fypal_ctx_error_clear_(ctx);
	return ctx_derive(ctx, true);
}

struct fypal_color *fypal_ctx_color_get_(struct fypal_ctx *ctx, uint32_t ref)
{
	size_t idx = FYPAL_COLOR_REF_INDEX(ref);

	if (!FYPAL_COLOR_IS_REF(ref) || idx >= ctx->ncolors)
		return NULL;
	fypal_ctx_derive_(ctx);
	return ctx->colors[idx];
}

size_t fypal_ctx_param_count(const struct fypal_ctx *ctx)
{
	return ctx ? ctx->nparams : 0;
}

const char *fypal_ctx_param_name(const struct fypal_ctx *ctx, size_t index)
{
	return ctx && index < ctx->nparams ? ctx->params[index]->def.name : NULL;
}

int fypal_ctx_param(struct fypal_ctx *ctx, const char *name, double *value)
{
	struct fypal_param *p;

	if (!ctx || !name)
		return -1;
	p = param_find(ctx, name, strlen(name));
	if (!p || param_is_text(ctx, p))
		return -1;
	fypal_ctx_derive_(ctx);
	if (p->def.eval_gen != ctx->eval_gen || p->def.state != FYPAL_EVAL_DONE)
		return -1;
	if (value)
		*value = p->value;
	return 0;
}

size_t fypal_ctx_color_count(const struct fypal_ctx *ctx)
{
	return ctx ? ctx->ncolors : 0;
}

const char *fypal_ctx_color_name(const struct fypal_ctx *ctx, size_t index)
{
	return ctx && index < ctx->ncolors ? ctx->colors[index]->def.name : NULL;
}

uint32_t fypal_ctx_color(struct fypal_ctx *ctx, const char *name)
{
	ssize_t idx;

	if (!ctx || !name)
		return FYPAL_RGB_INVALID;
	idx = color_index(ctx, name, strlen(name));
	if (idx < 0)
		return FYPAL_RGB_INVALID;
	fypal_ctx_derive_(ctx);
	return ctx->colors[idx]->rgb;
}

int fypal_ctx_color_sgr(struct fypal_ctx *ctx, const char *name,
			enum fypal_layer layer, char *buf, size_t size)
{
	char params[40];
	ssize_t idx;
	int n = 0;

	if (buf && size)
		buf[0] = '\0';
	if (!ctx || !name)
		return 0;
	idx = color_index(ctx, name, strlen(name));
	if (idx < 0)
		return 0;
	fypal_ctx_derive_(ctx);
	if (layer == FYPAL_LAYER_UL && !ctx->caps.underline_color)
		return 0;
	n = fypal_ctx_color_params_(ctx, ctx->colors[idx], layer, params,
				    sizeof(params));
	if (n <= 0)
		return 0;
	return snprintf(buf, size, "\033[%sm", params);
}

bool fypal_ctx_terminal16(struct fypal_ctx *ctx, uint32_t out[16])
{
	bool all = true;
	int i;

	if (!ctx)
		return false;
	fypal_ctx_derive_(ctx);
	for (i = 0; i < 16; i++) {
		if (out)
			out[i] = ctx->term16_rgb[i];
		if (ctx->term16_rgb[i] == FYPAL_RGB_INVALID)
			all = false;
	}
	return all;
}

/* ---------------------------------------------------------------------
 * Loading
 * ------------------------------------------------------------------ */

static const char *section_prefix(enum fypal_section section)
{
	return section == FYPAL_SECTION_DARK ? "dark/" :
	       section == FYPAL_SECTION_LIGHT ? "light/" : "";
}

/* A mapping key as a C string; NULL when it is not a string or a number. */
static const char *key_text(fy_generic *k, char *buf, size_t size)
{
	if (fy_is_string(*k))
		return fy_castp(k, "");
	if (fy_is_int(*k)) {
		snprintf(buf, size, "%lld", fy_cast(*k, 0LL));
		return buf;
	}
	return NULL;
}

static int load_defs(struct fypal_ctx *ctx, fy_generic map,
		     enum fypal_section section, const char *source,
		     const char *group)
{
	char where[FYPAL_NAME_MAX * 2], kbuf[32], expr[40];
	const char *key, *text;
	fy_generic k, v;
	char *end;
	long idx;
	int rc;

	if (!fy_is_mapping(map)) {
		fypal_ctx_error_set_(ctx, "%s: %s%s: must be a mapping", source,
				     section_prefix(section), group);
		return -1;
	}
	fy_foreach_key_value(k, v, map) {
		key = key_text(&k, kbuf, sizeof(kbuf));
		snprintf(where, sizeof(where), "%s: %s%s/%s", source,
			 section_prefix(section), group, key ? key : "?");
		if (!key) {
			fypal_ctx_error_set_(ctx, "%s: key must be a name", where);
			return -1;
		}

		/* An explicit string avoids ambiguity with a numeric reference. */
		if (!strcmp(group, "params") && fy_is_mapping(v) && fy_len(v) == 1 &&
		    fy_is_string(fy_get(v, "string", fy_invalid))) {
			fy_generic str = fy_get(v, "string", fy_invalid);
			text = fy_castp(&str, "");
			rc = define_param(ctx, key, section, text, where);
			if (rc)
				return rc;
			param_find(ctx, key, strlen(key))->text[section] = true;
			continue;
		}
		text = NULL;
		if (fy_is_string(v)) {
			text = fy_castp(&v, "");
		} else if (fy_is_int(v) || fy_is_float(v)) {
			snprintf(expr, sizeof(expr), "%.17g", fy_number(v, 0.0));
			text = expr;
		}

		if (!strcmp(group, "ansi16")) {
			if (text && !strcmp(text, "default"))
				idx = FYPAL_ANSI_DEFAULT;
			else if (text && !strcmp(text, "none"))
				idx = FYPAL_ANSI_NONE;
			else if (fy_is_int(v))
				idx = (long)fy_cast(v, -100LL);
			else
				idx = -100;
			if (idx < FYPAL_ANSI_NONE || idx > 15) {
				fypal_ctx_error_set_(ctx, "%s: must be 0-15, default or none",
						     where);
				return -1;
			}
			rc = set_ansi16(ctx, key, section, (int)idx, where);
		} else if (!text) {
			fypal_ctx_error_set_(ctx, "%s: must be a number or an expression",
					     where);
			return -1;
		} else if (!strcmp(group, "params")) {
			rc = define_param(ctx, key, section, text, where);
			if (!rc && fy_is_string(v))
				param_find(ctx, key, strlen(key))->auto_text[section] = true;
		} else if (!strcmp(group, "colors")) {
			rc = define_color(ctx, key, section, text, where);
		} else {
			idx = strtol(key, &end, 10);
			if (*end || end == key) {
				fypal_ctx_error_set_(ctx, "%s: slot must be 0-15", where);
				return -1;
			}
			rc = define_terminal16(ctx, (int)idx, section, text, where);
		}
		if (rc)
			return rc;
	}
	return 0;
}

/* The keys of a role mapping that are fields; every other key is a child. */
static bool role_field_key(const char *key)
{
	return !strcmp(key, "fg") || !strcmp(key, "bg") || !strcmp(key, "ul") ||
	       !strcmp(key, "base") || !strcmp(key, "attrs");
}

/*
 * The fields of a role mapping in the text form. *have is set when the
 * mapping has a field, which is what makes the node a role and not only a
 * group of roles.
 */
static int role_fields_text(struct fypal_ctx *ctx, fy_generic map,
			    const char *where, char *buf, size_t size,
			    bool *have)
{
	char kbuf[32];
	const char *key, *s;
	fy_generic k, v, a;
	size_t len = 0;
	int n;

	buf[0] = '\0';
	*have = false;
	fy_foreach_key_value(k, v, map) {
		key = key_text(&k, kbuf, sizeof(kbuf));
		if (!key || !role_field_key(key))
			continue;
		*have = true;
		if (strcmp(key, "attrs")) {
			if (!fy_is_string(v))
				goto err_value;
			s = fy_castp(&v, "");
			n = snprintf(buf + len, size - len, "%s%s=%s",
				     len ? " " : "", key, s);
		} else if (fy_is_string(v)) {
			s = fy_castp(&v, "");
			n = snprintf(buf + len, size - len, "%s%s",
				     len ? " " : "", s);
		} else if (fy_is_sequence(v)) {
			fy_foreach(a, v) {
				if (!fy_is_string(a))
					goto err_value;
				s = fy_castp(&a, "");
				n = snprintf(buf + len, size - len, "%s%s",
					     len ? " " : "", s);
				if (n < 0 || (size_t)n >= size - len)
					goto err_long;
				len += (size_t)n;
			}
			continue;
		} else {
			goto err_value;
		}
		if (n < 0 || (size_t)n >= size - len)
			goto err_long;
		len += (size_t)n;
	}
	return 0;

err_value:
	fypal_ctx_error_set_(ctx, "%s: invalid value of '%s'", where, key);
	return -1;
err_long:
	fypal_ctx_error_set_(ctx, "%s: role is too long", where);
	return -1;
}

/*
 * One node of the role tree. A string is the fields of the role; a mapping
 * holds the fields of the role and its children, whose names extend this
 * one with a dot.
 */
static int load_role_node(struct fypal_ctx *ctx, const char *name,
			  fy_generic node, const char *source)
{
	char where[FYPAL_NAME_MAX * 2], fields[1024], child[FYPAL_NAME_MAX];
	char kbuf[32];
	const char *key;
	fy_generic k, v;
	bool have;

	snprintf(where, sizeof(where), "%s: roles/%s", source, name);
	if (fy_is_string(node))
		return fypal_ctx_define_role_fields_(ctx, name,
						     fy_castp(&node, ""), where);
	if (!fy_is_mapping(node)) {
		fypal_ctx_error_set_(ctx, "%s: must be a mapping or a string",
				     where);
		return -1;
	}
	if (role_fields_text(ctx, node, where, fields, sizeof(fields), &have))
		return -1;
	if (have && fypal_ctx_define_role_fields_(ctx, name, fields, where))
		return -1;

	fy_foreach_key_value(k, v, node) {
		key = key_text(&k, kbuf, sizeof(kbuf));
		if (!key) {
			fypal_ctx_error_set_(ctx, "%s: role names must be scalars",
					     where);
			return -1;
		}
		if (role_field_key(key))
			continue;
		if (snprintf(child, sizeof(child), "%s.%s", name, key) >=
		    (int)sizeof(child)) {
			fypal_ctx_error_set_(ctx, "%s: role name is too long", where);
			return -1;
		}
		if (load_role_node(ctx, child, v, source))
			return -1;
	}
	return 0;
}

/*
 * One node of the glyph tree. A string is both forms of the glyph; a mapping
 * holds the utf and ascii forms and the children, whose names extend this one
 * with a dot.
 */
static int load_glyph_node(struct fypal_ctx *ctx, const char *name,
			   fy_generic node, const char *source)
{
	char where[FYPAL_NAME_MAX * 2], child[FYPAL_NAME_MAX], kbuf[32];
	const char *key, *utf, *ascii;
	fy_generic k, v, gu, ga;

	snprintf(where, sizeof(where), "%s: glyphs/%s", source, name);
	if (fy_is_string(node))
		return fypal_ctx_define_glyph_(ctx, name, fy_castp(&node, ""),
					       NULL, where);
	if (!fy_is_mapping(node)) {
		fypal_ctx_error_set_(ctx, "%s: must be a mapping or a string",
				     where);
		return -1;
	}
	gu = fy_get(node, "utf");
	ga = fy_get(node, "ascii");
	if (fy_is_valid(gu) || fy_is_valid(ga)) {
		if (!fy_is_string(gu) || (fy_is_valid(ga) && !fy_is_string(ga))) {
			fypal_ctx_error_set_(ctx, "%s: utf must be a string, and ascii too when it is given",
					     where);
			return -1;
		}
		utf = fy_castp(&gu, "");
		ascii = fy_is_valid(ga) ? fy_castp(&ga, "") : NULL;
		if (fypal_ctx_define_glyph_(ctx, name, utf, ascii, where))
			return -1;
	}
	fy_foreach_key_value(k, v, node) {
		key = key_text(&k, kbuf, sizeof(kbuf));
		if (!key) {
			fypal_ctx_error_set_(ctx, "%s: glyph names must be scalars",
					     where);
			return -1;
		}
		if (!strcmp(key, "utf") || !strcmp(key, "ascii"))
			continue;
		if (snprintf(child, sizeof(child), "%s.%s", name, key) >=
		    (int)sizeof(child)) {
			fypal_ctx_error_set_(ctx, "%s: glyph name is too long", where);
			return -1;
		}
		if (load_glyph_node(ctx, child, v, source))
			return -1;
	}
	return 0;
}

static int load_glyphs(struct fypal_ctx *ctx, fy_generic map, const char *source)
{
	char kbuf[32];
	const char *name;
	fy_generic k, v;

	if (!fy_is_mapping(map)) {
		fypal_ctx_error_set_(ctx, "%s: glyphs: must be a mapping", source);
		return -1;
	}
	fy_foreach_key_value(k, v, map) {
		name = key_text(&k, kbuf, sizeof(kbuf));
		if (!name) {
			fypal_ctx_error_set_(ctx, "%s: glyphs: names must be scalars",
					     source);
			return -1;
		}
		if (load_glyph_node(ctx, name, v, source))
			return -1;
	}
	return 0;
}

static int load_roles(struct fypal_ctx *ctx, fy_generic map, const char *source)
{
	char kbuf[32];
	const char *name;
	fy_generic k, v;

	if (!fy_is_mapping(map)) {
		fypal_ctx_error_set_(ctx, "%s: roles: must be a mapping", source);
		return -1;
	}
	fy_foreach_key_value(k, v, map) {
		name = key_text(&k, kbuf, sizeof(kbuf));
		if (!name) {
			fypal_ctx_error_set_(ctx, "%s: roles: names must be scalars",
					     source);
			return -1;
		}
		if (load_role_node(ctx, name, v, source))
			return -1;
	}
	return 0;
}

/* The ground of a theme: {l: NAME, c: NAME, h: NAME}. */
static int load_ground(struct fypal_ctx *ctx, fy_generic map, const char *source)
{
	static const char *const keys[3] = { "l", "c", "h" };
	const char *names[3] = { NULL, NULL, NULL };
	/* A short name is stored in the generic word: each name keeps its own
	 * generic until the names are copied. */
	fy_generic v[3];
	int i;

	if (!fy_is_mapping(map) || fy_len(map) != 3) {
		fypal_ctx_error_set_(ctx, "%s: ground: must name l, c and h", source);
		return -1;
	}
	for (i = 0; i < 3; i++) {
		v[i] = fy_get(map, keys[i], fy_invalid);
		if (fy_is_string(v[i]))
			names[i] = fy_castp(&v[i], "");
	}
	return define_ground(ctx, names, source);
}

static int load_section(struct fypal_ctx *ctx, fy_generic map,
			enum fypal_section section, const char *source)
{
	const char *key;
	fy_generic k, v;
	int rc;

	if (!fy_is_mapping(map)) {
		fypal_ctx_error_set_(ctx, "%s: %s must be a mapping", source,
				     section == FYPAL_SECTION_ALL ? "a theme" :
				     section_prefix(section));
		return -1;
	}
	fy_foreach_key_value(k, v, map) {
		key = fy_is_string(k) ? fy_castp(&k, "") : NULL;
		if (!key) {
			fypal_ctx_error_set_(ctx, "%s: %skeys must be strings", source,
					     section_prefix(section));
			return -1;
		}
		if (!strcmp(key, "params") || !strcmp(key, "colors") ||
		    !strcmp(key, "ansi16") || !strcmp(key, "terminal16")) {
			rc = load_defs(ctx, v, section, source, key);
		} else if (section == FYPAL_SECTION_ALL && !strcmp(key, "dark")) {
			rc = load_section(ctx, v, FYPAL_SECTION_DARK, source);
		} else if (section == FYPAL_SECTION_ALL && !strcmp(key, "light")) {
			rc = load_section(ctx, v, FYPAL_SECTION_LIGHT, source);
		} else if (section == FYPAL_SECTION_ALL && !strcmp(key, "roles")) {
			rc = load_roles(ctx, v, source);
		} else if (section == FYPAL_SECTION_ALL && !strcmp(key, "glyphs")) {
			rc = load_glyphs(ctx, v, source);
		} else if (section == FYPAL_SECTION_ALL && !strcmp(key, "ground")) {
			rc = load_ground(ctx, v, source);
		} else if (section == FYPAL_SECTION_ALL && !strcmp(key, "fypalette")) {
			rc = 0;
			if (fy_cast(v, 0LL) != THEME_VERSION) {
				fypal_ctx_error_set_(ctx, "%s: fypalette: version %d is required",
						     source, THEME_VERSION);
				rc = -1;
			}
		} else if (section == FYPAL_SECTION_ALL &&
			   (!strcmp(key, "name") || !strcmp(key, "description"))) {
			rc = 0;
		} else {
			fypal_ctx_error_set_(ctx, "%s: %sunknown key '%s'", source,
					     section_prefix(section), key);
			rc = -1;
		}
		if (rc)
			return rc;
	}
	return 0;
}

/* Evaluate both variants, then leave the context on its own. */
static int validate(struct fypal_ctx *ctx)
{
	enum fypal_variant saved = ctx->variant;
	int v, rc = 0;

	for (v = FYPAL_VARIANT_DARK; v <= FYPAL_VARIANT_LIGHT && !rc; v++) {
		ctx->variant = (enum fypal_variant)v;
		fypal_ctx_changed_(ctx);
		rc = ctx_derive(ctx, true);
	}
	ctx->variant = saved;
	fypal_ctx_changed_(ctx);
	return rc;
}

int fypal_ctx_load_generic_(struct fypal_ctx *ctx, fy_generic doc,
			    const char *source);

int fypal_ctx_load_generic_(struct fypal_ctx *ctx, fy_generic doc,
			    const char *source)
{
	fypal_ctx_error_clear_(ctx);
	if (!source)
		source = "theme";
	if (load_section(ctx, doc, FYPAL_SECTION_ALL, source))
		return -1;
	return validate(ctx);
}

/* The first parser diagnostic of a failed parse, without the input address. */
static void parse_error(struct fypal_ctx *ctx, fy_generic v, const char *source)
{
	fy_generic diag, rec, gmsg;
	const char *msg = "cannot parse";
	long long line = 0, column = 0;

	diag = fy_generic_get_diag(v);
	if (fy_is_valid(diag) && !fy_is_null(diag) && fy_len(diag)) {
		rec = fy_is_sequence(diag) ? fy_get_at(diag, 0) : diag;
		gmsg = fy_get(rec, "message");
		if (fy_is_string(gmsg))
			msg = fy_castp(&gmsg, "cannot parse");
		line = fy_get(rec, "line", 0LL);
		column = fy_get(rec, "column", 0LL);
		fypal_ctx_error_set_(ctx, "%s:%lld:%lld: %s", source, line, column,
				     msg);
		return;
	}
	fypal_ctx_error_set_(ctx, "%s: %s", source, msg);
}

static struct fy_generic_builder *builder_create(struct fypal_ctx *ctx,
						 const char *source)
{
	struct fy_generic_builder_cfg cfg = {
		.flags = FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED,
	};
	struct fy_generic_builder *gb;

	gb = fy_generic_builder_create(&cfg);
	if (!gb)
		fypal_ctx_error_set_(ctx, "%s: cannot create a generic builder",
				     source);
	return gb;
}

int fypal_ctx_load(struct fypal_ctx *ctx, const char *text, const char *source)
{
	struct fy_generic_builder *gb;
	fy_generic_sized_string input;
	fy_generic doc;
	int rc = -1;

	if (!ctx || !text)
		return -1;
	fypal_ctx_error_clear_(ctx);
	if (!source)
		source = "theme";
	gb = builder_create(ctx, source);
	if (!gb)
		return -1;
	input.data = text;
	input.size = strlen(text);
	/* the definitions copy what they keep, so the document dies with gb */
	doc = fy_parse(gb, input, PARSE_FLAGS | FYOPPF_INPUT_TYPE_STRING, NULL);
	if (!fy_is_valid(doc))
		parse_error(ctx, doc, source);
	else
		rc = fypal_ctx_load_generic_(ctx, doc, source);
	fy_generic_builder_destroy(gb);
	return rc;
}

int fypal_ctx_load_file(struct fypal_ctx *ctx, const char *path)
{
	struct fy_generic_builder *gb;
	fy_generic doc;
	int rc = -1;

	if (!ctx || !path)
		return -1;
	fypal_ctx_error_clear_(ctx);
	gb = builder_create(ctx, path);
	if (!gb)
		return -1;
	doc = fy_gb_parse_file(gb, PARSE_FLAGS, path);
	if (!fy_is_valid(doc))
		parse_error(ctx, doc, path);
	else
		rc = fypal_ctx_load_generic_(ctx, doc, path);
	fy_generic_builder_destroy(gb);
	return rc;
}

const char *fypal_builtin_theme_name(size_t index)
{
	size_t i;

	for (i = 0; fypal_builtin_themes_[i].name; i++) {
		if (i == index)
			return fypal_builtin_themes_[i].name;
	}
	return NULL;
}

const char *fypal_builtin_theme_text(const char *name)
{
	size_t i;

	if (!name)
		return NULL;
	for (i = 0; fypal_builtin_themes_[i].name; i++) {
		if (!strcmp(fypal_builtin_themes_[i].name, name))
			return fypal_builtin_themes_[i].text;
	}
	return NULL;
}

int fypal_ctx_load_builtin(struct fypal_ctx *ctx, const char *name)
{
	const char *text;

	if (!ctx)
		return -1;
	text = fypal_builtin_theme_text(name);
	if (!text) {
		fypal_ctx_error_clear_(ctx);
		fypal_ctx_error_set_(ctx, "no built-in theme '%s'",
				     name ? name : "");
		return -1;
	}
	return fypal_ctx_load(ctx, text, name);
}
