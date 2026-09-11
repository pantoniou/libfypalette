/*
 * fypal-color.c - OKLab conversion, quantisation, parsing and SGR colours
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "fypal-internal.h"

/* A linear channel this far outside [0, 1] is still in gamut. */
#define GAMUT_EPS	1e-5

/* Bisection steps of the gamut map; chroma resolves to C / 2^steps. */
#define GAMUT_STEPS	32

/* The canonical xterm values of the sixteen ANSI colours. */
static const struct {
	uint32_t rgb;
	const char *name;
} ansi16[16] = {
	{ 0x000000, "black" },
	{ 0xcd0000, "red" },
	{ 0x00cd00, "green" },
	{ 0xcdcd00, "yellow" },
	{ 0x0000ee, "blue" },
	{ 0xcd00cd, "magenta" },
	{ 0x00cdcd, "cyan" },
	{ 0xe5e5e5, "white" },
	{ 0x7f7f7f, "brightblack" },
	{ 0xff0000, "brightred" },
	{ 0x00ff00, "brightgreen" },
	{ 0xffff00, "brightyellow" },
	{ 0x5c5cff, "brightblue" },
	{ 0xff00ff, "brightmagenta" },
	{ 0x00ffff, "brightcyan" },
	{ 0xffffff, "brightwhite" },
};

static const int cube_level[6] = { 0, 95, 135, 175, 215, 255 };

static double srgb_to_linear(double c)
{
	return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static double linear_to_srgb(double c)
{
	return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

static double clamp01(double v)
{
	return v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
}

static void rgb_to_linear(uint32_t rgb, double lin[3])
{
	lin[0] = srgb_to_linear(((rgb >> 16) & 0xff) / 255.0);
	lin[1] = srgb_to_linear(((rgb >> 8) & 0xff) / 255.0);
	lin[2] = srgb_to_linear((rgb & 0xff) / 255.0);
}

static void lab_to_linear(struct fypal_lab lab, double lin[3])
{
	double l, m, s;

	l = lab.L + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
	m = lab.L - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
	s = lab.L - 0.0894841775 * lab.a - 1.2914855480 * lab.b;
	l = l * l * l;
	m = m * m * m;
	s = s * s * s;
	lin[0] = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
	lin[1] = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
	lin[2] = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
}

static bool linear_in_gamut(const double lin[3])
{
	int i;

	for (i = 0; i < 3; i++) {
		if (!(lin[i] >= -GAMUT_EPS && lin[i] <= 1.0 + GAMUT_EPS))
			return false;
	}
	return true;
}

static uint32_t linear_to_rgb(const double lin[3])
{
	uint32_t v = 0;
	int i;

	for (i = 0; i < 3; i++)
		v = (v << 8) | (uint32_t)lround(linear_to_srgb(clamp01(lin[i])) * 255.0);
	return v;
}

struct fypal_lab fypal_rgb_to_lab(uint32_t rgb)
{
	struct fypal_lab lab;
	double lin[3], l, m, s;

	rgb_to_linear(rgb, lin);
	l = cbrt(0.4122214708 * lin[0] + 0.5363325363 * lin[1] + 0.0514459929 * lin[2]);
	m = cbrt(0.2119034982 * lin[0] + 0.6806995451 * lin[1] + 0.1073969566 * lin[2]);
	s = cbrt(0.0883024619 * lin[0] + 0.2817188376 * lin[1] + 0.6299787005 * lin[2]);
	lab.L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
	lab.a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
	lab.b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
	return lab;
}

uint32_t fypal_lab_to_rgb(struct fypal_lab lab, bool *in_gamut)
{
	double lin[3];

	lab_to_linear(lab, lin);
	if (in_gamut)
		*in_gamut = linear_in_gamut(lin);
	return linear_to_rgb(lin);
}

struct fypal_lch fypal_lab_to_lch(struct fypal_lab lab)
{
	struct fypal_lch lch;

	lch.L = lab.L;
	lch.C = sqrt(lab.a * lab.a + lab.b * lab.b);
	lch.h = atan2(lab.b, lab.a) * 180.0 / M_PI;
	if (lch.h < 0.0)
		lch.h += 360.0;
	return lch;
}

struct fypal_lab fypal_lch_to_lab(struct fypal_lch lch)
{
	struct fypal_lab lab;
	double rad = lch.h * M_PI / 180.0;

	lab.L = lch.L;
	lab.a = lch.C * cos(rad);
	lab.b = lch.C * sin(rad);
	return lab;
}

uint32_t fypal_lch_to_rgb(struct fypal_lch lch)
{
	struct fypal_lch t = lch;
	double lin[3], lo, hi;
	int i;

	t.L = clamp01(lch.L);
	if (!(t.C > 0.0))
		t.C = 0.0;
	lab_to_linear(fypal_lch_to_lab(t), lin);
	if (linear_in_gamut(lin))
		return linear_to_rgb(lin);

	/* keep lightness and hue, give up chroma until the colour fits */
	lo = 0.0;
	hi = t.C;
	for (i = 0; i < GAMUT_STEPS; i++) {
		t.C = (lo + hi) / 2.0;
		lab_to_linear(fypal_lch_to_lab(t), lin);
		if (linear_in_gamut(lin))
			lo = t.C;
		else
			hi = t.C;
	}
	t.C = lo;
	lab_to_linear(fypal_lch_to_lab(t), lin);
	return linear_to_rgb(lin);
}

uint32_t fypal_mix(uint32_t a, uint32_t b, double t)
{
	struct fypal_lab la, lb, m;

	t = clamp01(t);
	la = fypal_rgb_to_lab(a);
	lb = fypal_rgb_to_lab(b);
	m.L = la.L + (lb.L - la.L) * t;
	m.a = la.a + (lb.a - la.a) * t;
	m.b = la.b + (lb.b - la.b) * t;
	return fypal_lab_to_rgb(m, NULL);
}

static double luminance(uint32_t rgb)
{
	double lin[3];

	rgb_to_linear(rgb, lin);
	return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2];
}

double fypal_contrast(uint32_t a, uint32_t b)
{
	double la = luminance(a), lb = luminance(b);

	return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

uint32_t fypal_xterm_to_rgb(int index)
{
	int r, g, b;

	if (index < 0 || index > 255)
		return FYPAL_RGB_INVALID;
	if (index < 16)
		return ansi16[index].rgb;
	if (index < 232) {
		index -= 16;
		r = cube_level[(index / 36) % 6];
		g = cube_level[(index / 6) % 6];
		b = cube_level[index % 6];
	} else {
		r = g = b = 8 + (index - 232) * 10;
	}
	return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static int nearest_index(uint32_t rgb, int first, int last)
{
	struct fypal_lab lab = fypal_rgb_to_lab(rgb), x;
	double best = -1.0, d;
	int i, bi = first;

	for (i = first; i <= last; i++) {
		x = fypal_rgb_to_lab(fypal_xterm_to_rgb(i));
		d = (lab.L - x.L) * (lab.L - x.L) + (lab.a - x.a) * (lab.a - x.a) +
		    (lab.b - x.b) * (lab.b - x.b);
		if (best < 0.0 || d < best) {
			best = d;
			bi = i;
		}
	}
	return bi;
}

int fypal_rgb_to_xterm256(uint32_t rgb)
{
	return nearest_index(rgb & 0xffffff, 16, 255);
}

int fypal_rgb_to_ansi16(uint32_t rgb)
{
	return nearest_index(rgb & 0xffffff, 0, 15);
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static uint32_t parse_hex(const char *s, size_t len)
{
	uint32_t v = 0;
	size_t i;
	int d;

	if (len != 3 && len != 6)
		return FYPAL_RGB_INVALID;
	for (i = 0; i < len; i++) {
		d = hexval(s[i]);
		if (d < 0)
			return FYPAL_RGB_INVALID;
		v = (v << 4) | (uint32_t)d;
	}
	if (len == 6)
		return v;
	/* #rgb repeats each digit: #f0a is #ff00aa */
	return ((v & 0xf00) << 12) | ((v & 0xf00) << 8) |
	       ((v & 0x0f0) << 8) | ((v & 0x0f0) << 4) |
	       ((v & 0x00f) << 4) | (v & 0x00f);
}

static const char *skip_sep(const char *p)
{
	while (*p && (isspace((unsigned char)*p) || *p == ','))
		p++;
	return p;
}

/* "oklch(L C h)", with an optional % on L; separators are blanks or commas. */
static uint32_t parse_oklch(const char *s, size_t len)
{
	struct fypal_lch lch;
	const char *p, *e = s + len;
	double v[3];
	char *end;
	int i;

	if (len < 7 || strncasecmp(s, "oklch(", 6) || s[len - 1] != ')')
		return FYPAL_RGB_INVALID;
	p = s + 6;
	for (i = 0; i < 3; i++) {
		p = skip_sep(p);
		v[i] = strtod(p, &end);
		if (end == p || end > e)
			return FYPAL_RGB_INVALID;
		p = end;
		if (i == 0 && *p == '%') {
			v[0] /= 100.0;
			p++;
		}
	}
	p = skip_sep(p);
	if (p != e - 1)
		return FYPAL_RGB_INVALID;
	lch.L = v[0];
	lch.C = v[1];
	lch.h = v[2];
	return fypal_lch_to_rgb(lch);
}

uint32_t fypal_color_parse(const char *s)
{
	size_t len, i;
	char *end;
	long idx;

	if (!s)
		return FYPAL_RGB_INVALID;
	while (isspace((unsigned char)*s))
		s++;
	len = strlen(s);
	while (len && isspace((unsigned char)s[len - 1]))
		len--;
	if (!len)
		return FYPAL_RGB_INVALID;

	if (*s == '#')
		return parse_hex(s + 1, len - 1);
	if (!strncasecmp(s, "oklch(", 6))
		return parse_oklch(s, len);
	if (len <= 3 && isdigit((unsigned char)*s)) {
		idx = strtol(s, &end, 10);
		if (end != s + len)
			return FYPAL_RGB_INVALID;
		return fypal_xterm_to_rgb((int)idx);
	}
	for (i = 0; i < 16; i++) {
		if (strlen(ansi16[i].name) == len &&
		    !strncasecmp(s, ansi16[i].name, len))
			return ansi16[i].rgb;
	}
	return FYPAL_RGB_INVALID;
}

static int empty_result(char *buf, size_t size)
{
	if (buf && size)
		buf[0] = '\0';
	return 0;
}

static int sgr_base(enum fypal_layer layer)
{
	return layer == FYPAL_LAYER_BG ? 48 : layer == FYPAL_LAYER_UL ? 58 : 38;
}

int fypal_sgr_params_ansi16(int index, enum fypal_layer layer,
			    char *buf, size_t size)
{
	int base;

	if (index < FYPAL_ANSI_DEFAULT || index > 15 || layer == FYPAL_LAYER_UL)
		return empty_result(buf, size);
	base = layer == FYPAL_LAYER_BG ? 40 : 30;
	if (index == FYPAL_ANSI_DEFAULT)
		return snprintf(buf, size, "%d", base + 9);
	if (index >= 8)
		return snprintf(buf, size, "%d", base + 60 + index - 8);
	return snprintf(buf, size, "%d", base + index);
}

static int sgr_params_xterm(int index, enum fypal_layer layer,
			    char *buf, size_t size)
{
	return snprintf(buf, size, "%d;5;%d", sgr_base(layer), index);
}

int fypal_sgr_params_rgb(uint32_t rgb, enum fypal_layer layer,
			 enum fypal_depth depth, char *buf, size_t size)
{
	if (rgb & 0xff000000U)
		return empty_result(buf, size);
	switch (depth) {
	case FYPAL_DEPTH_TRUECOLOR:
		return snprintf(buf, size, "%d;2;%u;%u;%u", sgr_base(layer),
				(rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
	case FYPAL_DEPTH_256:
		return sgr_params_xterm(fypal_rgb_to_xterm256(rgb), layer,
					buf, size);
	case FYPAL_DEPTH_16:
		return fypal_sgr_params_ansi16(fypal_rgb_to_ansi16(rgb), layer,
					       buf, size);
	default:
		return empty_result(buf, size);
	}
}

int fypal_ctx_color_params_(const struct fypal_ctx *ctx,
			    const struct fypal_color *c, enum fypal_layer layer,
			    char *buf, size_t size)
{
	if (!c || c->rgb == FYPAL_RGB_INVALID)
		return empty_result(buf, size);
	switch (ctx->caps.depth) {
	case FYPAL_DEPTH_TRUECOLOR:
		return fypal_sgr_params_rgb(c->rgb, layer, ctx->caps.depth,
					    buf, size);
	case FYPAL_DEPTH_256:
		return sgr_params_xterm(c->xterm256, layer, buf, size);
	case FYPAL_DEPTH_16:
		return fypal_sgr_params_ansi16(c->ansi, layer, buf, size);
	default:
		return empty_result(buf, size);
	}
}
