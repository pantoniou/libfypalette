/*
 * fypal-glyph.c - named glyphs with UTF-8 and ASCII forms
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "fypal-internal.h"

void fypal_ctx_glyphs_destroy_(struct fypal_ctx *ctx)
{
	size_t i;

	for (i = 0; i < ctx->nglyphs; i++) {
		free(ctx->glyphs[i]->name);
		free(ctx->glyphs[i]->utf);
		free(ctx->glyphs[i]->ascii);
		free(ctx->glyphs[i]);
	}
	free(ctx->glyphs);
	ctx->glyphs = NULL;
	ctx->nglyphs = ctx->aglyphs = 0;
	fypal_hash_release_(&ctx->glyph_hash);
}

int fypal_ctx_define_glyph_(struct fypal_ctx *ctx, const char *name,
			    const char *utf, const char *ascii, const char *where)
{
	struct fypal_glyph *g;
	char *u, *a;

	if (!where)
		where = "api";
	if (!fypal_path_valid_(name) || !utf) {
		fypal_ctx_error_set_(ctx, "%s: invalid glyph '%s'", where,
				     name ? name : "");
		return -1;
	}
	u = strdup(utf);
	a = strdup(ascii ? ascii : utf);
	if (!u || !a)
		goto err_nomem;

	g = fypal_hash_find_(&ctx->glyph_hash, name, strlen(name));
	if (g) {
		/* a redefinition replaces the strings a caller may hold */
		free(g->utf);
		free(g->ascii);
		g->utf = u;
		g->ascii = a;
		fypal_ctx_changed_(ctx);
		return 0;
	}
	g = calloc(1, sizeof(*g));
	if (!g)
		goto err_nomem;
	g->name = strdup(name);
	g->utf = u;
	g->ascii = a;
	if (!g->name ||
	    FYPAL_ARRAY_PUSH(ctx->glyphs, ctx->nglyphs, ctx->aglyphs, g)) {
		free(g->name);
		free(g);
		goto err_nomem;
	}
	if (fypal_hash_insert_(&ctx->glyph_hash, g->name, strlen(g->name), g,
			       false)) {
		ctx->nglyphs--;
		free(g->name);
		free(g);
		goto err_nomem;
	}
	fypal_ctx_changed_(ctx);
	return 0;

err_nomem:
	free(u);
	free(a);
	fypal_ctx_error_set_(ctx, "%s: out of memory", where);
	return -1;
}

int fypal_ctx_define_glyph(struct fypal_ctx *ctx, const char *name,
			   const char *utf, const char *ascii)
{
	if (!ctx)
		return -1;
	fypal_ctx_error_clear_(ctx);
	return fypal_ctx_define_glyph_(ctx, name, utf, ascii, NULL);
}

const char *fypal_ctx_glyph(struct fypal_ctx *ctx, const char *name, bool ascii)
{
	struct fypal_glyph *g;
	size_t len;

	if (!ctx || !name)
		return NULL;
	/* the nearest defined ancestor answers an undefined name */
	for (len = strlen(name); len; ) {
		g = fypal_hash_find_(&ctx->glyph_hash, name, len);
		if (g)
			return ascii ? g->ascii : g->utf;
		while (len && name[len - 1] != '.')
			len--;
		if (len)
			len--;
	}
	return NULL;
}

size_t fypal_ctx_glyph_count(const struct fypal_ctx *ctx)
{
	return ctx ? ctx->nglyphs : 0;
}

const char *fypal_ctx_glyph_name(const struct fypal_ctx *ctx, size_t index)
{
	return ctx && index < ctx->nglyphs ? ctx->glyphs[index]->name : NULL;
}
