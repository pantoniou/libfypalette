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
 * Probe the terminal and return the variant that matches it, as
 * fypal_term_variant() decides. Each call probes again, and keys typed
 * during the probe are lost: a program that reads keys uses a
 * struct fypal_probe instead.
 */
FYPAL_EXPORT enum fypal_variant fypal_detect_variant(int fd, bool *known);

/*
 * Probe the terminal and return its background colour in *rgb. Returns
 * false, and leaves *rgb unchanged, if the terminal did not report it. Each
 * call probes again, and keys typed during the probe are lost.
 */
FYPAL_EXPORT bool fypal_detect_background(int fd, uint32_t *rgb);

/*
 * The terminal probe sends all its queries in one write and ends them with
 * DA1. A terminal answers queries in order, and every terminal answers DA1.
 * So when the DA1 reply arrives, all the other replies have arrived too, and
 * a query with no reply is not supported. The probe waits for the DA1 reply
 * for at most 1000 ms; $FYPAL_PROBE_TIMEOUT_MS changes this limit.
 *
 * The queries: OSC 10 and 11 (colours), OSC 4 (the 16 ANSI colours),
 * CSI ? 996 n (light or dark scheme), DECRQM for modes 1004, 1006, 1016,
 * 2004, 2026, 2027, 2031 and 2048, CSI ? u (kitty keyboard), XTVERSION, DA2,
 * the kitty graphics query, XTSMGRAPHICS (sixel colours), CSI 14 t and
 * CSI 16 t (sizes in pixels), XTGETTCAP (RGB, Tc, Smulx, Su, Smol, Ms),
 * XTQMODKEYS and the OSC 99 query (kitty notifications). Queries added with
 * fypal_probe_add_query() follow, then DA1.
 *
 * tmux answers all the queries itself. GNU screen answers DA1 itself, so
 * under screen the probe sends only the colour and scheme queries, and DA1,
 * each wrapped for screen to pass to the outer terminal.
 */
#define FYPAL_TERM_PROBED	(1U << 0)	/* the exchange was sent */
#define FYPAL_TERM_ANSWERED	(1U << 1)	/* DA1 reply arrived: results are complete */
#define FYPAL_TERM_BACKGROUND	(1U << 2)	/* background is valid */
#define FYPAL_TERM_FOREGROUND	(1U << 3)	/* foreground is valid */
#define FYPAL_TERM_SYNC		(1U << 4)	/* synchronized output, mode 2026 */
#define FYPAL_TERM_KITTY_KEYS	(1U << 5)	/* the kitty keyboard protocol */
#define FYPAL_TERM_GRAPHEMES	(1U << 6)	/* grapheme clusters, mode 2027 */
#define FYPAL_TERM_THEME_REPORT	(1U << 7)	/* light/dark change reports, 2031 */
#define FYPAL_TERM_SIXEL	(1U << 8)	/* DA1 attribute 4 */
#define FYPAL_TERM_KITTY_GRAPHICS (1U << 9)	/* the kitty graphics protocol */
#define FYPAL_TERM_TRUECOLOR	(1U << 10)	/* XTGETTCAP RGB or Tc */
#define FYPAL_TERM_STYLED_UL	(1U << 11)	/* XTGETTCAP Smulx */
#define FYPAL_TERM_CELL_PIXELS	(1U << 12)	/* cell_width, cell_height valid */
#define FYPAL_TERM_WINDOW_PIXELS (1U << 13)	/* window_width, _height valid */
#define FYPAL_TERM_SCHEME	(1U << 14)	/* the terminal reported its scheme */
#define FYPAL_TERM_SCHEME_LIGHT	(1U << 15)	/* ... and it is light */
#define FYPAL_TERM_DA2		(1U << 16)	/* da2_type, da2_version valid */
#define FYPAL_TERM_FOCUS_EVENTS	(1U << 17)	/* mode 1004 */
#define FYPAL_TERM_SGR_MOUSE	(1U << 18)	/* mode 1006 */
#define FYPAL_TERM_SGR_PIXEL_MOUSE (1U << 19)	/* mode 1016 */
#define FYPAL_TERM_BRACKETED_PASTE (1U << 20)	/* mode 2004 */
#define FYPAL_TERM_INBAND_RESIZE (1U << 21)	/* resize reports, mode 2048 */
#define FYPAL_TERM_CLIPBOARD	(1U << 22)	/* XTGETTCAP Ms: OSC 52 */
#define FYPAL_TERM_OVERLINE	(1U << 23)	/* XTGETTCAP Smol */
#define FYPAL_TERM_MODIFY_KEYS	(1U << 24)	/* modify_other_keys valid */
#define FYPAL_TERM_NOTIFY	(1U << 25)	/* kitty notifications, OSC 99 */
#define FYPAL_TERM_NOTIFY_SOUND	(1U << 26)	/* ... that can play a sound */
#define FYPAL_TERM_GRAPHEMES_SET (1U << 27)	/* mode 2027 is on already */
#define FYPAL_TERM_MULTIPLEXER	(1U << 28)	/* running under screen or tmux */

struct fypal_term {
	unsigned int flags;		/* FYPAL_TERM_* */
	uint32_t background;		/* 0xRRGGBB */
	uint32_t foreground;		/* 0xRRGGBB */
	unsigned int cell_width;	/* pixels */
	unsigned int cell_height;
	unsigned int window_width;	/* pixels of the text area */
	unsigned int window_height;
	unsigned int sixel_colors;	/* colour registers; 0 when unknown */
	unsigned int da1_class;		/* first DA1 parameter, e.g. 62, 64 */
	unsigned int da2_type;		/* DA2: terminal type */
	unsigned int da2_version;	/* DA2: firmware version */
	unsigned int modify_other_keys;	/* XTQMODKEYS level, 0 to 3 */
	char name[64];			/* XTVERSION, "" when unknown */
	uint32_t ansi[16];		/* the ANSI palette, 0xRRGGBB */
	unsigned int ansi_known;	/* bit N: ansi[N] is valid */
};

/*
 * The variant that matches a probe result. The order is: the scheme that the
 * terminal reported, then $COLORFGBG, then the background colour. If none is
 * known, *known is false and the result is dark. term may be NULL.
 */
FYPAL_EXPORT enum fypal_variant fypal_term_variant(const struct fypal_term *term,
						   bool *known);

/* One probe of a terminal. The caller owns it. Not thread-safe. */
struct fypal_probe;

/* A new probe, or NULL if out of memory. */
FYPAL_EXPORT struct fypal_probe *fypal_probe_create(void);

FYPAL_EXPORT void fypal_probe_destroy(struct fypal_probe *probe);

/*
 * Add a query of your own. It is sent after the built-in queries and before
 * DA1. Returns -1 if the probe already ran, or if the added queries exceed
 * 4 KiB.
 *
 * A reply that the library does not parse is kept, for
 * fypal_probe_unknown(), if no key can produce it: an OSC, DCS or APC
 * string, or a CSI sequence with a private marker (? > < =) or an
 * intermediate byte. A reply that is a plain CSI sequence, such as the reply
 * to CSI 18 t, looks like a key and is treated as typed input.
 *
 * Under GNU screen the added queries are wrapped for screen to pass to the
 * outer terminal. The wrapper ends at the first ST, so end an OSC query with
 * BEL.
 */
FYPAL_EXPORT int fypal_probe_add_query(struct fypal_probe *probe,
				       const char *seq, size_t len);

/*
 * Probe the controlling terminal, or fd if there is none. Blocks until the
 * DA1 reply or the time limit. Sends nothing if there is no terminal, if
 * $TERM is "dumb", or if the process is in a background process group; the
 * result then has no flags. A probe runs one time: returns -1 if it already
 * ran, else 0.
 */
FYPAL_EXPORT int fypal_probe_run(struct fypal_probe *probe, int fd);

/* The result. Valid until the probe is destroyed. */
FYPAL_EXPORT const struct fypal_term *
fypal_probe_result(const struct fypal_probe *probe);

/*
 * Bytes that arrived during the probe and are not replies are keys that the
 * user typed. Copy up to size of them into buf, in the order they were typed,
 * and remove them. Returns the number of bytes; 0 when none remain. Read all
 * of them before reading the terminal.
 */
FYPAL_EXPORT size_t fypal_probe_take_input(struct fypal_probe *probe,
					   char *buf, size_t size);

/* The number of replies that the library did not parse. */
FYPAL_EXPORT size_t fypal_probe_unknown_count(const struct fypal_probe *probe);

/*
 * An unparsed reply, in the order the replies arrived: the whole sequence
 * from its ESC, with its length in *len. NULL if index is out of range.
 * Valid until the probe is destroyed.
 */
FYPAL_EXPORT const char *fypal_probe_unknown(const struct fypal_probe *probe,
					     size_t index, size_t *len);

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

enum fypal_surface_scope {
	FYPAL_SURFACE_SELECTED,	/* card and diff washes */
	FYPAL_SURFACE_ALL,	/* every colour used as a role background */
};

/* The ratio is WCAG contrast against the theme ground; zero disables it.
 * The selected scope adjusts card and diff washes only. */
FYPAL_EXPORT void fypal_ctx_set_surface_contrast(struct fypal_ctx *ctx,
						 double ratio);
FYPAL_EXPORT void fypal_ctx_set_surface_scope(struct fypal_ctx *ctx,
					      enum fypal_surface_scope scope);

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

/*
 * The ground of a theme: the parameters that hold the OKLCH lightness,
 * chroma and hue of its background, as the `ground` key of a theme names
 * them. Returns 0, or -1 with the cause in fypal_ctx_error().
 */
FYPAL_EXPORT int fypal_ctx_define_ground(struct fypal_ctx *ctx, const char *l,
					 const char *c, const char *h);

/*
 * Make @rgb the ground of the active variant: set the ground parameters in
 * the section of that variant to its lightness, chroma and hue. A theme that
 * derives its neutral colours from those parameters then follows the
 * background of the terminal. Returns 0, or -1 with the cause in
 * fypal_ctx_error() when the theme names no ground.
 */
FYPAL_EXPORT int fypal_ctx_set_ground(struct fypal_ctx *ctx, uint32_t rgb);

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
