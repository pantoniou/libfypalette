/*
 * libfypalette.h - programmable colour themes for terminal renderers
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LIBFYPALETTE_H
#define LIBFYPALETTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FYPAL_VERSION_MAJOR 0
#define FYPAL_VERSION_MINOR 1
#define FYPAL_VERSION_PATCH 0

#if defined(__GNUC__) && __GNUC__ >= 4
#define FYPAL_EXPORT __attribute__((visibility("default")))
#else
#define FYPAL_EXPORT
#endif

/*
 * The library holds no theme policy. It supplies colour arithmetic, palette
 * quantisation, terminal capabilities and SGR output, and it evaluates a
 * theme: a text of parameters, colour expressions and named roles. The
 * built-in themes are data files compiled into the library.
 */

/* ---------------------------------------------------------------------
 * Colours
 * ------------------------------------------------------------------ */

/* A colour is 0xRRGGBB in sRGB. FYPAL_RGB_INVALID is no colour. */
#define FYPAL_RGB_INVALID	0xffffffffU

/* OKLab, and its polar form OKLCH with the hue in degrees. */
struct fypal_lab {
	double L;
	double a;
	double b;
};

struct fypal_lch {
	double L;
	double C;
	double h;
};

FYPAL_EXPORT struct fypal_lab fypal_rgb_to_lab(uint32_t rgb);
FYPAL_EXPORT uint32_t fypal_lab_to_rgb(struct fypal_lab lab, bool *in_gamut);
FYPAL_EXPORT struct fypal_lch fypal_lab_to_lch(struct fypal_lab lab);
FYPAL_EXPORT struct fypal_lab fypal_lch_to_lab(struct fypal_lch lch);

/*
 * The sRGB colour of an OKLCH colour. A colour outside sRGB keeps its
 * lightness and hue and loses chroma until it fits.
 */
FYPAL_EXPORT uint32_t fypal_lch_to_rgb(struct fypal_lch lch);

/* Mix t of b into a in OKLab; t is clamped to [0, 1]. */
FYPAL_EXPORT uint32_t fypal_mix(uint32_t a, uint32_t b, double t);

/* The WCAG 2 contrast ratio of two colours, 1 to 21. */
FYPAL_EXPORT double fypal_contrast(uint32_t a, uint32_t b);

/* The xterm palette. Distances are measured in OKLab. */
FYPAL_EXPORT uint32_t fypal_xterm_to_rgb(int index);
/* The nearest cube or grey entry, 16 to 255: a terminal redefines 0-15. */
FYPAL_EXPORT int fypal_rgb_to_xterm256(uint32_t rgb);
FYPAL_EXPORT int fypal_rgb_to_ansi16(uint32_t rgb);

/*
 * Parse a literal colour: "#rgb", "#rrggbb", "oklch(L C h)", an xterm ANSI
 * colour name such as "brightred", or a 0-255 xterm index.
 */
FYPAL_EXPORT uint32_t fypal_color_parse(const char *s);

/* ---------------------------------------------------------------------
 * Terminal output
 * ------------------------------------------------------------------ */

/* How many colours the output can carry. */
enum fypal_depth {
	FYPAL_DEPTH_NONE,
	FYPAL_DEPTH_16,
	FYPAL_DEPTH_256,
	FYPAL_DEPTH_TRUECOLOR,
};

/* Where an SGR colour applies. */
enum fypal_layer {
	FYPAL_LAYER_FG,
	FYPAL_LAYER_BG,
	FYPAL_LAYER_UL,
};

/* Text attributes. */
#define FYPAL_ATTR_BOLD		(1U << 0)
#define FYPAL_ATTR_DIM		(1U << 1)
#define FYPAL_ATTR_ITALIC	(1U << 2)
#define FYPAL_ATTR_UNDERLINE	(1U << 3)
#define FYPAL_ATTR_UNDERCURL	(1U << 4)
#define FYPAL_ATTR_BLINK	(1U << 5)
#define FYPAL_ATTR_REVERSE	(1U << 6)
#define FYPAL_ATTR_STRIKE	(1U << 7)
#define FYPAL_ATTR_OVERLINE	(1U << 8)
#define FYPAL_ATTR_ALL		((1U << 9) - 1)

/* What a terminal can present. */
struct fypal_caps {
	enum fypal_depth depth;
	unsigned int attrs;		/* FYPAL_ATTR_* it renders */
	bool underline_color;		/* SGR 58 */
};

/* A 16 colour index, or one of these. */
#define FYPAL_ANSI_DEFAULT	(-1)	/* the terminal default colour */
#define FYPAL_ANSI_NONE		(-2)	/* emit nothing */

/*
 * Write the SGR parameters that select a colour, without the introducer or
 * the final 'm': "38;2;r;g;b", "48;5;n" or "91". The 16 colour form is the
 * nearest ANSI colour. Returns the length snprintf() would, or 0 when the
 * depth carries no such colour; a 16 colour depth has no underline colour.
 */
FYPAL_EXPORT int fypal_sgr_params_rgb(uint32_t rgb, enum fypal_layer layer,
				      enum fypal_depth depth,
				      char *buf, size_t size);

/* The same for a 16 colour index or FYPAL_ANSI_DEFAULT / _NONE. */
FYPAL_EXPORT int fypal_sgr_params_ansi16(int index, enum fypal_layer layer,
					 char *buf, size_t size);

/*
 * The colour depth of the terminal on fd, from the environment. NO_COLOR
 * and a descriptor that is not a terminal give FYPAL_DEPTH_NONE.
 */
FYPAL_EXPORT enum fypal_depth fypal_detect_depth(int fd);

/* Capabilities from the environment: the depth, attributes by TERM. */
FYPAL_EXPORT void fypal_caps_detect(int fd, struct fypal_caps *caps);

enum fypal_variant {
	FYPAL_VARIANT_DARK,
	FYPAL_VARIANT_LIGHT,
};

/*
 * The variant that matches the terminal background: $COLORFGBG, then an OSC
 * 11 query on the controlling terminal. *known is false when neither
 * answered and the result is the dark default.
 */
FYPAL_EXPORT enum fypal_variant fypal_detect_variant(int fd, bool *known);

/* ---------------------------------------------------------------------
 * Themes
 *
 * A theme is lines of statements; '#' followed by a blank or the end of
 * the line starts a comment.
 *
 *   [all] | [dark] | [light]      the section of the statements that follow
 *   set NAME = EXPR               a number parameter
 *   color NAME = CEXPR            a colour
 *   ansi16 NAME = N|default|none  the 16 colour form of a colour
 *   terminal16 N = CEXPR          colour N of an emulated terminal's palette
 *   role NAME: FIELDS             a role (roles have no section)
 *   glyphs                        a tree of glyphs (see Glyphs)
 *
 * A definition in the section of the active variant replaces the one in
 * [all]. Names are evaluated when used, so the order of definitions does
 * not matter.
 *
 *   EXPR  := numbers, parameter names, + - * / and parentheses
 *   CEXPR := #rgb | #rrggbb | colour name
 *          | oklch(EXPR, EXPR, EXPR) | rgb(EXPR, EXPR, EXPR)
 *          | mix(CEXPR, CEXPR, EXPR) | xterm(EXPR)
 *
 * Commas between arguments are optional. A colour without an ansi16 form
 * uses its nearest ANSI colour.
 *
 * FIELDS are blank-separated: "fg=C", "bg=C", "ul=C", "base=ROLE", an
 * attribute name ("bold", "dim", "italic", "underline", "undercurl",
 * "blink", "reverse", "strike", "overline"), or an attribute name with a
 * leading '-' that clears an inherited one. C is a colour name, "default",
 * or a literal colour.
 *
 * A role name is a dot-separated path, such as "md.heading.2". A role
 * inherits each field it does not set from its base: the role named by
 * base=, or else its nearest defined dotted ancestor. A lookup of an
 * undefined name answers with its nearest defined ancestor, so a renderer
 * may ask for a precise name and a theme may define only a coarse one.
 * ------------------------------------------------------------------ */

enum fypal_section {
	FYPAL_SECTION_ALL,
	FYPAL_SECTION_DARK,
	FYPAL_SECTION_LIGHT,
};

/* The names and texts of the built-in themes; NULL past the end. */
FYPAL_EXPORT const char *fypal_builtin_theme_name(size_t index);
FYPAL_EXPORT const char *fypal_builtin_theme_text(const char *name);

/* A theme context: definitions, a variant and the output capabilities. It
 * is not safe for concurrent use. */
struct fypal_ctx;
struct fypal_role;

/* An empty context. NULL caps are truecolor with every attribute. */
FYPAL_EXPORT struct fypal_ctx *fypal_ctx_create(const struct fypal_caps *caps);
FYPAL_EXPORT void fypal_ctx_destroy(struct fypal_ctx *ctx);

/*
 * The first error since the last successful load or definition, as
 * "source:line: message"; "" when there is none.
 */
FYPAL_EXPORT const char *fypal_ctx_error(const struct fypal_ctx *ctx);

/*
 * Add a theme text to the context. Every colour is then evaluated for both
 * variants. Returns 0, or -1 with the cause in fypal_ctx_error(); the
 * statements before the failing one stay defined. source names the text in
 * error messages and may be NULL.
 */
FYPAL_EXPORT int fypal_ctx_load(struct fypal_ctx *ctx, const char *text,
				const char *source);
FYPAL_EXPORT int fypal_ctx_load_file(struct fypal_ctx *ctx, const char *path);
FYPAL_EXPORT int fypal_ctx_load_builtin(struct fypal_ctx *ctx, const char *name);

FYPAL_EXPORT void fypal_ctx_set_variant(struct fypal_ctx *ctx,
					enum fypal_variant variant);
FYPAL_EXPORT enum fypal_variant fypal_ctx_variant(const struct fypal_ctx *ctx);
FYPAL_EXPORT void fypal_ctx_set_caps(struct fypal_ctx *ctx,
				     const struct fypal_caps *caps);
FYPAL_EXPORT const struct fypal_caps *fypal_ctx_caps(const struct fypal_ctx *ctx);

/*
 * Define statements through the API, with the meaning of the theme
 * statements. Each returns 0, or -1 with the cause in fypal_ctx_error().
 * A definition is checked when it is evaluated.
 */
FYPAL_EXPORT int fypal_ctx_define_param(struct fypal_ctx *ctx, const char *name,
					enum fypal_section section,
					const char *expr);
FYPAL_EXPORT int fypal_ctx_set_param(struct fypal_ctx *ctx, const char *name,
				     enum fypal_section section, double value);
FYPAL_EXPORT int fypal_ctx_set_param_string(struct fypal_ctx *ctx, const char *name,
					  enum fypal_section section, const char *value);
FYPAL_EXPORT int fypal_ctx_define_color(struct fypal_ctx *ctx, const char *name,
					enum fypal_section section,
					const char *expr);
FYPAL_EXPORT int fypal_ctx_set_ansi16(struct fypal_ctx *ctx, const char *name,
				      enum fypal_section section, int index);
FYPAL_EXPORT int fypal_ctx_define_terminal16(struct fypal_ctx *ctx, int index,
					     enum fypal_section section,
					     const char *expr);
FYPAL_EXPORT int fypal_ctx_define_role(struct fypal_ctx *ctx, const char *name,
				       const char *fields);

/* Evaluate every definition for the active variant: 0, or -1 and the cause. */
FYPAL_EXPORT int fypal_ctx_check(struct fypal_ctx *ctx);

/* Parameters and colours, in definition order, for the active variant. */
FYPAL_EXPORT size_t fypal_ctx_param_count(const struct fypal_ctx *ctx);
FYPAL_EXPORT const char *fypal_ctx_param_name(const struct fypal_ctx *ctx,
					      size_t index);
FYPAL_EXPORT int fypal_ctx_param(struct fypal_ctx *ctx, const char *name,
				 double *value);
/* Borrowed string for the active variant; NULL for numeric or absent values. */
FYPAL_EXPORT const char *fypal_ctx_param_string(struct fypal_ctx *ctx,
					      const char *name);

FYPAL_EXPORT size_t fypal_ctx_color_count(const struct fypal_ctx *ctx);
FYPAL_EXPORT const char *fypal_ctx_color_name(const struct fypal_ctx *ctx,
					      size_t index);
/* The colour, or FYPAL_RGB_INVALID when it is undefined or does not evaluate. */
FYPAL_EXPORT uint32_t fypal_ctx_color(struct fypal_ctx *ctx, const char *name);

/* The escape that selects a colour for the context capabilities; "" is none. */
FYPAL_EXPORT int fypal_ctx_color_sgr(struct fypal_ctx *ctx, const char *name,
				     enum fypal_layer layer,
				     char *buf, size_t size);

/* The emulated terminal palette; false when the theme leaves a slot out. */
FYPAL_EXPORT bool fypal_ctx_terminal16(struct fypal_ctx *ctx, uint32_t out[16]);

/* ---------------------------------------------------------------------
 * Glyphs
 *
 * A theme names the glyphs its application draws, such as the gutter marks
 * of a transcript, with a UTF-8 form and an ASCII form for a terminal or a
 * charset without the UTF-8 one. A glyph name is a dot-separated path, and a
 * lookup of an undefined name answers with its nearest defined ancestor.
 * ------------------------------------------------------------------ */

/*
 * The glyph for @name, or for its nearest defined ancestor: the ASCII form
 * when @ascii, else the UTF-8 form. NULL when no glyph answers. The string is
 * valid until the glyph is defined again or the context is destroyed.
 */
FYPAL_EXPORT const char *fypal_ctx_glyph(struct fypal_ctx *ctx, const char *name,
					 bool ascii);

/*
 * Define or replace a glyph. A NULL @ascii uses @utf for both forms. Returns
 * 0, or -1 with the cause in fypal_ctx_error().
 */
FYPAL_EXPORT int fypal_ctx_define_glyph(struct fypal_ctx *ctx, const char *name,
					const char *utf, const char *ascii);

FYPAL_EXPORT size_t fypal_ctx_glyph_count(const struct fypal_ctx *ctx);
FYPAL_EXPORT const char *fypal_ctx_glyph_name(const struct fypal_ctx *ctx,
					      size_t index);

/* ---------------------------------------------------------------------
 * Roles
 * ------------------------------------------------------------------ */

/*
 * A style colour: 0xRRGGBB, the terminal default, unset (inherited), or a
 * reference to a named colour of the context.
 */
#define FYPAL_COLOR_UNSET	0xff000000U
#define FYPAL_COLOR_DEFAULT	0xfd000000U
#define FYPAL_COLOR_IS_RGB(c)	(((c) & 0xff000000U) == 0)
#define FYPAL_COLOR_IS_REF(c)	(((c) & 0xff000000U) == 0xfe000000U)

/*
 * A style. attrs_set turns attributes on and attrs_clear turns inherited
 * ones off; a resolved style has no clear bits.
 */
struct fypal_style {
	uint32_t fg;
	uint32_t bg;
	uint32_t ul;
	unsigned int attrs_set;
	unsigned int attrs_clear;
};

/* A reference to a named colour, for a style; FYPAL_COLOR_UNSET on failure. */
FYPAL_EXPORT uint32_t fypal_ctx_color_ref(struct fypal_ctx *ctx,
					  const char *name);

FYPAL_EXPORT int fypal_ctx_define_role_style(struct fypal_ctx *ctx,
					     const char *name,
					     const struct fypal_style *style,
					     const char *base);

/*
 * The role for a name, or its nearest defined ancestor; NULL when none is
 * defined. The pointer is valid until the context is destroyed.
 */
FYPAL_EXPORT const struct fypal_role *fypal_ctx_role(struct fypal_ctx *ctx,
						     const char *name);
FYPAL_EXPORT size_t fypal_ctx_role_count(const struct fypal_ctx *ctx);
FYPAL_EXPORT const struct fypal_role *fypal_ctx_role_at(const struct fypal_ctx *ctx,
							size_t index);
FYPAL_EXPORT const char *fypal_role_name(const struct fypal_role *role);
FYPAL_EXPORT const char *fypal_role_base(const struct fypal_role *role);

/* The fields of the role as defined, without inheritance. */
FYPAL_EXPORT const struct fypal_style *fypal_role_style(const struct fypal_role *role);

/*
 * The role with inheritance applied and colours made RGB for the active
 * variant. A colour that no ancestor sets stays FYPAL_COLOR_UNSET.
 */
FYPAL_EXPORT void fypal_ctx_resolve(struct fypal_ctx *ctx,
				    const struct fypal_role *role,
				    struct fypal_style *out);

/*
 * The escape that turns the role on, and the one that turns off exactly
 * what it turned on, for the context capabilities. Both are "" for a NULL
 * role. They are valid until the next change to the context.
 */
FYPAL_EXPORT const char *fypal_role_on(struct fypal_ctx *ctx,
				       const struct fypal_role *role);
FYPAL_EXPORT const char *fypal_role_off(struct fypal_ctx *ctx,
					const struct fypal_role *role);

/* Lookup and escape in one call; "" when no role answers. */
FYPAL_EXPORT const char *fypal_ctx_on(struct fypal_ctx *ctx, const char *name);
FYPAL_EXPORT const char *fypal_ctx_off(struct fypal_ctx *ctx, const char *name);

/*
 * The on and off escapes of an arbitrary style for the context. Returns the
 * length of on as snprintf() would.
 */
FYPAL_EXPORT int fypal_ctx_style_sgr(struct fypal_ctx *ctx,
				     const struct fypal_style *style,
				     char *on, size_t on_size,
				     char *off, size_t off_size);

#ifdef __cplusplus
}
#endif

#endif
