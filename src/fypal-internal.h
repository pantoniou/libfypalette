/*
 * fypal-internal.h - libfypalette internal structures
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FYPAL_INTERNAL_H
#define FYPAL_INTERNAL_H

#include "libfypalette.h"

#define N_ELEMENTS(a)		(sizeof(a) / sizeof((a)[0]))

#define FYPAL_SECTIONS		3
#define FYPAL_ERR_MAX		512
#define FYPAL_NAME_MAX		128
#define FYPAL_SGR_MAX		160

/* An ansi16 slot that the section does not set. */
#define FYPAL_ANSI_UNSET	(-3)

#define FYPAL_COLOR_REF(i)	(0xfe000000U | (uint32_t)(i))
#define FYPAL_COLOR_REF_INDEX(c) ((size_t)((c) & 0xffffffU))

/* Append to a growable array of pointers; evaluates to 0 or -1. */
#define FYPAL_ARRAY_PUSH(_arr, _count, _alloc, _item)				\
	({									\
		int __rc = 0;							\
		if ((_count) == (_alloc)) {					\
			size_t __a = (_alloc) ? (_alloc) * 2 : 32;		\
			__typeof__(_arr) __n = realloc((_arr),			\
						       __a * sizeof(*(_arr)));	\
			if (!__n)						\
				__rc = -1;					\
			else {							\
				(_arr) = __n;					\
				(_alloc) = __a;					\
			}							\
		}								\
		if (!__rc)							\
			(_arr)[(_count)++] = (_item);				\
		__rc;								\
	})

/*
 * A chained hash from a string key to a pointer. A key is borrowed from the
 * value unless own_key is set, in which case the entry holds a copy.
 */
struct fypal_hash_entry {
	struct fypal_hash_entry *next;
	const char *key;
	size_t len;
	uint32_t hash;
	bool own_key;
	void *value;
};

struct fypal_hash {
	struct fypal_hash_entry **buckets;
	size_t nbuckets;	/* a power of two, or 0 */
	size_t count;
};

struct fypal_hash_entry *fypal_hash_lookup_(const struct fypal_hash *h,
					    const char *key, size_t len);
void *fypal_hash_find_(const struct fypal_hash *h, const char *key, size_t len);
int fypal_hash_insert_(struct fypal_hash *h, const char *key, size_t len,
		       void *value, bool own_key);
void fypal_hash_clear_(struct fypal_hash *h);
void fypal_hash_release_(struct fypal_hash *h);

enum fypal_eval_state {
	FYPAL_EVAL_NONE,
	FYPAL_EVAL_BUSY,
	FYPAL_EVAL_DONE,
	FYPAL_EVAL_FAIL,
};

/*
 * A named definition with one expression per section. The evaluation state
 * belongs to the evaluation pass named by eval_gen; a definition of an
 * older pass is unevaluated.
 */
struct fypal_def {
	char *name;
	char *expr[FYPAL_SECTIONS];
	char *where[FYPAL_SECTIONS];
	unsigned int eval_gen;
	enum fypal_eval_state state;
};

struct fypal_param {
	struct fypal_def def;
	double value;
	bool text[FYPAL_SECTIONS];
	bool auto_text[FYPAL_SECTIONS];
};

struct fypal_color {
	struct fypal_def def;
	size_t index;		/* position in the context, for references */
	int ansi16[FYPAL_SECTIONS];
	uint32_t rgb;		/* for the active variant */
	int xterm256;
	int ansi;
};

struct fypal_glyph {
	char *name;
	char *utf;
	char *ascii;
};

struct fypal_role {
	char *name;
	char *base;
	struct fypal_style style;
	unsigned int gen;	/* context generation of on and off; 0 is none */
	char on[FYPAL_SGR_MAX];
	char off[FYPAL_SGR_MAX];
};

struct fypal_ctx {
	enum fypal_variant variant;
	struct fypal_caps caps;
	double surface_contrast;
	enum fypal_surface_scope surface_scope;

	struct fypal_param **params;
	size_t nparams;
	size_t aparams;

	struct fypal_color **colors;
	size_t ncolors;
	size_t acolors;

	struct fypal_def term16[16];
	uint32_t term16_rgb[16];

	struct fypal_role **roles;
	size_t nroles;
	size_t aroles;

	struct fypal_hash param_hash;	/* name -> struct fypal_param */
	struct fypal_hash color_hash;	/* name -> struct fypal_color */
	struct fypal_hash role_hash;	/* name -> struct fypal_role */
	struct fypal_hash query_hash;	/* queried name -> answering role or NULL */

	struct fypal_glyph **glyphs;
	size_t nglyphs;
	size_t aglyphs;
	struct fypal_hash glyph_hash;	/* name -> struct fypal_glyph */

	/* The parameters that hold the lightness, chroma and hue of the ground,
	 * owned; NULL when the theme names no ground. */
	char *ground[3];

	unsigned int gen;		/* bumped by every change */
	unsigned int eval_gen;		/* bumped by every evaluation pass */
	unsigned int derived_gen;	/* gen the colours were evaluated for */
	int derive_rc;

	char err[FYPAL_ERR_MAX];
};

struct fypal_builtin_theme {
	const char *name;
	const char *text;
};

/* Generated from themes/; ends with a NULL name. */
extern const struct fypal_builtin_theme fypal_builtin_themes_[];

void fypal_ctx_error_set_(struct fypal_ctx *ctx, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
void fypal_ctx_error_clear_(struct fypal_ctx *ctx);
void fypal_ctx_changed_(struct fypal_ctx *ctx);

/* Evaluate the colours for the active variant if a change made them stale. */
int fypal_ctx_derive_(struct fypal_ctx *ctx);

/* The colour a reference names, evaluated; NULL for a stale reference. */
struct fypal_color *fypal_ctx_color_get_(struct fypal_ctx *ctx, uint32_t ref);

/* SGR parameters of an evaluated colour for the context depth. */
int fypal_ctx_color_params_(const struct fypal_ctx *ctx,
			    const struct fypal_color *c, enum fypal_layer layer,
			    char *buf, size_t size);

int fypal_ctx_define_role_fields_(struct fypal_ctx *ctx, const char *name,
				  const char *fields, const char *where);
void fypal_ctx_roles_destroy_(struct fypal_ctx *ctx);
/* Whether a defined role uses this colour as its effective background. */
bool fypal_ctx_background_uses_(const struct fypal_ctx *ctx, size_t index);

/* A dot-separated name of roles and glyphs: components of letters, digits,
 * '_' and '-'. */
bool fypal_path_valid_(const char *name);

int fypal_ctx_define_glyph_(struct fypal_ctx *ctx, const char *name,
			    const char *utf, const char *ascii, const char *where);
void fypal_ctx_glyphs_destroy_(struct fypal_ctx *ctx);

#endif
