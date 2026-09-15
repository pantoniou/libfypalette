/*
 * fypal-test.c - libfypalette tests
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libfypalette.h"

static int failures;

#define CHECK(cond)							\
	do {								\
		if (!(cond)) {						\
			fprintf(stderr, "%s:%d: check failed: %s\n",	\
				__FILE__, __LINE__, #cond);		\
			failures++;					\
		}							\
	} while (0)

static int channel_diff(uint32_t a, uint32_t b)
{
	int d, max = 0, s;

	for (s = 0; s <= 16; s += 8) {
		d = abs((int)((a >> s) & 0xff) - (int)((b >> s) & 0xff));
		if (d > max)
			max = d;
	}
	return max;
}

static struct fypal_ctx *ember(const struct fypal_caps *caps)
{
	struct fypal_ctx *ctx;

	ctx = fypal_ctx_create(caps);
	if (fypal_ctx_load_builtin(ctx, "ember")) {
		fprintf(stderr, "ember: %s\n", fypal_ctx_error(ctx));
		failures++;
	}
	return ctx;
}

/* A context loaded from a theme text, reporting a failure. */
static struct fypal_ctx *theme(const char *text)
{
	struct fypal_ctx *ctx;

	ctx = fypal_ctx_create(NULL);
	if (fypal_ctx_load(ctx, text, "test")) {
		fprintf(stderr, "theme: %s\n", fypal_ctx_error(ctx));
		failures++;
	}
	return ctx;
}

/* A theme text that must fail with a message that contains want. */
static void theme_fails(const char *text, const char *want, int line)
{
	struct fypal_ctx *ctx;
	const char *err;

	ctx = fypal_ctx_create(NULL);
	if (!fypal_ctx_load(ctx, text, "test")) {
		fprintf(stderr, "%s:%d: theme loaded: %s\n", __FILE__, line, text);
		failures++;
	} else {
		err = fypal_ctx_error(ctx);
		if (!strstr(err, want)) {
			fprintf(stderr, "%s:%d: error \"%s\" lacks \"%s\"\n",
				__FILE__, line, err, want);
			failures++;
		}
	}
	fypal_ctx_destroy(ctx);
}

static void test_lab_roundtrip(void)
{
	static const uint32_t samples[] = {
		0x000000, 0xffffff, 0xff0000, 0x00ff00, 0x0000ff,
		0x16150f, 0xd9a961, 0x6dbcc8, 0x808080, 0x123456,
	};
	struct fypal_lab lab;
	bool in;
	size_t i;

	for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
		lab = fypal_rgb_to_lab(samples[i]);
		CHECK(fypal_lab_to_rgb(lab, &in) == samples[i]);
		CHECK(in);
	}
	lab = fypal_rgb_to_lab(0xffffff);
	CHECK(fabs(lab.L - 1.0) < 1e-4);
	CHECK(fabs(lab.a) < 1e-4 && fabs(lab.b) < 1e-4);
}

static void test_lch_reference(void)
{
	struct fypal_lch c = { .L = 0.76, .C = 0.10, .h = 65.0 };

	/* reference values from an independent OKLCH implementation */
	CHECK(channel_diff(fypal_lch_to_rgb(c), 0xdda46b) <= 1);
	c.h = 250.0;
	CHECK(channel_diff(fypal_lch_to_rgb(c), 0x7fb6ee) <= 1);
	c.h = 25.0;
	CHECK(channel_diff(fypal_lch_to_rgb(c), 0xea9891) <= 1);
}

static void test_gamut_map(void)
{
	struct fypal_lch want = { .L = 0.70, .C = 0.30, .h = 190.0 }, got;
	uint32_t rgb;

	rgb = fypal_lch_to_rgb(want);
	got = fypal_lab_to_lch(fypal_rgb_to_lab(rgb));
	CHECK(fabs(got.L - want.L) < 0.01);
	CHECK(got.C < want.C);
	CHECK(fabs(got.h - want.h) < 3.0);
	want.C = NAN;
	CHECK(fypal_lch_to_rgb(want) <= 0xffffff);
}

static void test_xterm256(void)
{
	CHECK(fypal_rgb_to_xterm256(0x000000) == 16);
	CHECK(fypal_rgb_to_xterm256(0xffffff) == 231);
	CHECK(fypal_rgb_to_xterm256(0xff0000) == 196);
	CHECK(fypal_rgb_to_xterm256(0x808080) == 244);
	CHECK(fypal_rgb_to_xterm256(0x5f87af) == 67);
	CHECK(fypal_xterm_to_rgb(67) == 0x5f87af);
	CHECK(fypal_xterm_to_rgb(232) == 0x080808);
	CHECK(fypal_xterm_to_rgb(256) == FYPAL_RGB_INVALID);
}

static void test_ansi16(void)
{
	CHECK(fypal_rgb_to_ansi16(0xff0000) == 9);
	CHECK(fypal_rgb_to_ansi16(0x000000) == 0);
	CHECK(fypal_rgb_to_ansi16(0xffffff) == 15);
	CHECK(fypal_rgb_to_ansi16(0x00cdcd) == 6);
}

static void test_contrast(void)
{
	CHECK(fabs(fypal_contrast(0x000000, 0xffffff) - 21.0) < 0.01);
	CHECK(fabs(fypal_contrast(0x777777, 0x777777) - 1.0) < 0.01);
}

static void test_sgr_rgb(void)
{
	char buf[64];
	int n;

	fypal_sgr_params_rgb(0x0a0b0c, FYPAL_LAYER_BG, FYPAL_DEPTH_TRUECOLOR,
			     buf, sizeof(buf));
	CHECK(!strcmp(buf, "48;2;10;11;12"));
	fypal_sgr_params_rgb(0xff0000, FYPAL_LAYER_FG, FYPAL_DEPTH_256,
			     buf, sizeof(buf));
	CHECK(!strcmp(buf, "38;5;196"));
	fypal_sgr_params_rgb(0xff0000, FYPAL_LAYER_UL, FYPAL_DEPTH_256,
			     buf, sizeof(buf));
	CHECK(!strcmp(buf, "58;5;196"));
	fypal_sgr_params_rgb(0xff0000, FYPAL_LAYER_FG, FYPAL_DEPTH_16,
			     buf, sizeof(buf));
	CHECK(!strcmp(buf, "91"));
	n = fypal_sgr_params_rgb(0xff0000, FYPAL_LAYER_UL, FYPAL_DEPTH_16,
				 buf, sizeof(buf));
	CHECK(n == 0 && !buf[0]);
	n = fypal_sgr_params_rgb(0xff0000, FYPAL_LAYER_FG, FYPAL_DEPTH_NONE,
				 buf, sizeof(buf));
	CHECK(n == 0 && !buf[0]);
	fypal_sgr_params_ansi16(FYPAL_ANSI_DEFAULT, FYPAL_LAYER_BG, buf,
				sizeof(buf));
	CHECK(!strcmp(buf, "49"));
	fypal_sgr_params_ansi16(3, FYPAL_LAYER_BG, buf, sizeof(buf));
	CHECK(!strcmp(buf, "43"));
	n = fypal_sgr_params_ansi16(FYPAL_ANSI_NONE, FYPAL_LAYER_FG, buf,
				    sizeof(buf));
	CHECK(n == 0);
}

static void test_color_parse(void)
{
	CHECK(fypal_color_parse("#f0a") == 0xff00aa);
	CHECK(fypal_color_parse(" #16150F ") == 0x16150f);
	CHECK(fypal_color_parse("196") == 0xff0000);
	CHECK(fypal_color_parse("brightred") == 0xff0000);
	CHECK(fypal_color_parse("green") == 0x00cd00);
	CHECK(channel_diff(fypal_color_parse("oklch(0.76 0.10 65)"), 0xdda46b) <= 1);
	CHECK(channel_diff(fypal_color_parse("oklch(76% 0.10 65)"), 0xdda46b) <= 1);
	CHECK(fypal_color_parse("oklch(0.76 0.10)") == FYPAL_RGB_INVALID);
	CHECK(fypal_color_parse("#12345") == FYPAL_RGB_INVALID);
	CHECK(fypal_color_parse("256") == FYPAL_RGB_INVALID);
	CHECK(fypal_color_parse("ink") == FYPAL_RGB_INVALID);
	CHECK(fypal_color_parse("") == FYPAL_RGB_INVALID);
}

static void test_theme_builtin(void)
{
	struct fypal_ctx *ctx;
	bool found = false;
	const char *name;
	size_t i;

	for (i = 0; (name = fypal_builtin_theme_name(i)); i++)
		found |= !strcmp(name, "ember");
	CHECK(found);
	CHECK(fypal_builtin_theme_text("ember") != NULL);
	CHECK(fypal_builtin_theme_text("nope") == NULL);

	ctx = ember(NULL);
	CHECK(!*fypal_ctx_error(ctx));
	CHECK(fypal_ctx_color_count(ctx) >= 22);
	CHECK(fypal_ctx_role_count(ctx) > 50);
	CHECK(fypal_ctx_load_builtin(ctx, "nope") == -1);
	CHECK(strstr(fypal_ctx_error(ctx), "nope") != NULL);
	fypal_ctx_destroy(ctx);
}

static void test_theme_expr(void)
{
	struct fypal_ctx *ctx;
	double v;

	ctx = theme("params: {a: 2, b: 'a * 3 + (1 - 0.5)', neg: '-a', "
		    "div: 'b / 4'}\n"
		    "colors: {c1: '#102030', c2: 'oklch(0.5, 0, 0)', "
		    "c3: 'mix(#000000 #ffffff 0.5)', c4: 'rgb(1, 2, 300)', "
		    "c5: 'xterm(196)', c6: c1, c7: 'oklch(a / 4 0 0)'}\n");
	CHECK(!fypal_ctx_param(ctx, "b", &v) && fabs(v - 6.5) < 1e-9);
	CHECK(!fypal_ctx_param(ctx, "neg", &v) && v == -2.0);
	CHECK(!fypal_ctx_param(ctx, "div", &v) && fabs(v - 1.625) < 1e-9);
	CHECK(fypal_ctx_param(ctx, "nope", &v) == -1);
	CHECK(fypal_ctx_color(ctx, "c1") == 0x102030);
	CHECK(fypal_ctx_color(ctx, "c4") == 0x0102ff);
	CHECK(fypal_ctx_color(ctx, "c5") == 0xff0000);
	CHECK(fypal_ctx_color(ctx, "c6") == 0x102030);
	CHECK(channel_diff(fypal_ctx_color(ctx, "c3"), 0x636363) <= 2);
	CHECK(channel_diff(fypal_ctx_color(ctx, "c2"), 0x636363) <= 2);
	CHECK(channel_diff(fypal_ctx_color(ctx, "c7"), 0x636363) <= 2);
	CHECK(fypal_ctx_color(ctx, "nope") == FYPAL_RGB_INVALID);
	fypal_ctx_destroy(ctx);
}

static void test_theme_sections(void)
{
	struct fypal_ctx *ctx;
	double v;

	ctx = theme("params: {a: 1, b: 'a + 10'}\n"
		    "dark: {params: {a: 2}, colors: {c: '#000000'}}\n"
		    "light: {params: {a: 3}}\n"
		    "colors: {c: '#ffffff'}\n");
	CHECK(fypal_ctx_variant(ctx) == FYPAL_VARIANT_DARK);
	CHECK(!fypal_ctx_param(ctx, "b", &v) && v == 12.0);
	CHECK(fypal_ctx_color(ctx, "c") == 0x000000);
	fypal_ctx_set_variant(ctx, FYPAL_VARIANT_LIGHT);
	CHECK(!fypal_ctx_param(ctx, "b", &v) && v == 13.0);
	CHECK(fypal_ctx_color(ctx, "c") == 0xffffff);
	fypal_ctx_destroy(ctx);
}

static void test_theme_errors(void)
{
	theme_fails("colors: {x: 'oklch(nope, 0, 0)'}", "undefined parameter 'nope'",
		    __LINE__);
	theme_fails("params: {a: b, b: a}\ncolors: {x: 'oklch(a 0 0)'}",
		    "depends on itself", __LINE__);
	theme_fails("colors: {x: 'oklch(0.5, 0.1, 30'}", "expected ')'", __LINE__);
	theme_fails("colors: {x: 'oklch(0.5 0.1'}", "expected a number", __LINE__);
	theme_fails("params: {a: '1 2'}", "unexpected", __LINE__);
	theme_fails("params: {a: '1 / 0'}", "division by zero", __LINE__);
	theme_fails("colors: {x: 'hsl(1 2 3)'}", "unknown function", __LINE__);
	theme_fails("colors: {x: '#12'}", "invalid colour", __LINE__);
	theme_fails("colors: {x: 'y'}", "undefined colour 'y'", __LINE__);
	theme_fails("colors: [", "test", __LINE__);
	theme_fails("colours: {}", "unknown key 'colours'", __LINE__);
	theme_fails("roles: {r: {fg: nocolor}}", "nocolor", __LINE__);
	theme_fails("roles: {r: {fg: '#000', sparkle: 1}}",
		    "roles/r.sparkle: must be a mapping or a string", __LINE__);
	theme_fails("roles: {r: {fg: '#000', sparkle: yes}}",
		    "unknown field 'yes'", __LINE__);
	theme_fails("roles: {r: {fg: [a]}}", "invalid value of 'fg'", __LINE__);
	theme_fails("roles: {r: 'bold sparkly'}", "sparkly", __LINE__);
	theme_fails("colors: {x: '#000'}\nansi16: {x: 20}", "0-15", __LINE__);
	theme_fails("dark: {colors: {x: '#000'}}", "light variant", __LINE__);
	theme_fails("terminal16: {16: '#000'}", "slot", __LINE__);
	theme_fails("fypalette: 2", "version", __LINE__);
	theme_fails("dark: {roles: {}}", "unknown key 'roles'", __LINE__);
	theme_fails("params: {'bad-name': 1}", "invalid parameter", __LINE__);
}

static void test_theme_api(void)
{
	struct fypal_ctx *ctx;
	uint32_t pal[16];

	ctx = fypal_ctx_create(NULL);
	CHECK(!fypal_ctx_set_param(ctx, "l", FYPAL_SECTION_ALL, 0.5));
	CHECK(!fypal_ctx_define_param(ctx, "l2", FYPAL_SECTION_LIGHT, "l + 0.1"));
	CHECK(!fypal_ctx_define_color(ctx, "g", FYPAL_SECTION_ALL,
				      "oklch(l, 0, 0)"));
	CHECK(!fypal_ctx_define_color(ctx, "g", FYPAL_SECTION_LIGHT,
				      "oklch(l2, 0, 0)"));
	CHECK(!fypal_ctx_define_role(ctx, "r", "fg=g bold"));
	CHECK(!fypal_ctx_check(ctx));
	CHECK(channel_diff(fypal_ctx_color(ctx, "g"), 0x636363) <= 2);
	fypal_ctx_set_variant(ctx, FYPAL_VARIANT_LIGHT);
	CHECK(fypal_ctx_color(ctx, "g") > 0x636363);
	CHECK(!fypal_ctx_define_terminal16(ctx, 3, FYPAL_SECTION_ALL, "g"));
	CHECK(!fypal_ctx_terminal16(ctx, pal));
	CHECK(pal[3] == fypal_ctx_color(ctx, "g"));
	CHECK(pal[4] == FYPAL_RGB_INVALID);
	CHECK(fypal_ctx_set_param(ctx, "x", FYPAL_SECTION_ALL, NAN) == -1);
	CHECK(fypal_ctx_define_color(ctx, "default", FYPAL_SECTION_ALL, "#000") == -1);
	CHECK(fypal_ctx_define_terminal16(ctx, 16, FYPAL_SECTION_ALL, "g") == -1);
	CHECK(fypal_ctx_set_ansi16(ctx, "g", FYPAL_SECTION_ALL, 16) == -1);

	/* a role that names an undefined colour fails the check */
	CHECK(!fypal_ctx_define_role(ctx, "s", "fg=later"));
	CHECK(fypal_ctx_check(ctx) == -1);
	CHECK(strstr(fypal_ctx_error(ctx), "later") != NULL);
	CHECK(!fypal_ctx_define_color(ctx, "later", FYPAL_SECTION_ALL, "#010203"));
	CHECK(!fypal_ctx_check(ctx));
	fypal_ctx_destroy(ctx);
}

static void test_param_strings(void)
{
	struct fypal_ctx *ctx = fypal_ctx_create(NULL);
	double value;
	const char *text;

	CHECK(!fypal_ctx_load(ctx,
		"params: {md.code.rules: none, n: 2, label: {string: 'hello world'}, alias: n}\n"
		"dark: {params: {md.code.rules: bubble-rule-faint}}\n"
		"light: {params: {md.code.rules: top}}\n", "strings"));
	text = fypal_ctx_param_string(ctx, "md.code.rules");
	CHECK(text && !strcmp(text, "bubble-rule-faint"));
	CHECK(fypal_ctx_param(ctx, "md.code.rules", &value) == -1);
	CHECK(fypal_ctx_param_string(ctx, "n") == NULL);
	CHECK(fypal_ctx_param_string(ctx, "missing") == NULL);
	text = fypal_ctx_param_string(ctx, "label");
	CHECK(text && !strcmp(text, "hello world"));
	CHECK(!fypal_ctx_param(ctx, "alias", &value) && value == 2);
	CHECK(!fypal_ctx_set_param(ctx, "alias", FYPAL_SECTION_ALL, 2));
	fypal_ctx_set_variant(ctx, FYPAL_VARIANT_LIGHT);
	text = fypal_ctx_param_string(ctx, "md.code.rules");
	CHECK(text && !strcmp(text, "top"));
	CHECK(!fypal_ctx_set_param_string(ctx, "n", FYPAL_SECTION_LIGHT, "both"));
	CHECK(!fypal_ctx_check(ctx));
	CHECK(fypal_ctx_param(ctx, "n", &value) == -1);
	CHECK(!fypal_ctx_set_param(ctx, "n", FYPAL_SECTION_LIGHT, 3));
	CHECK(!fypal_ctx_param(ctx, "n", &value) && value == 3);
	CHECK(fypal_ctx_param_string(ctx, "n") == NULL);
	CHECK(!fypal_ctx_define_param(ctx, "bad", FYPAL_SECTION_ALL, "md.code.rules"));
	CHECK(fypal_ctx_check(ctx) == -1);
	CHECK(strstr(fypal_ctx_error(ctx), "is a string") != NULL);
	fypal_ctx_destroy(ctx);
}

static void test_theme_ground(void)
{
	struct fypal_ctx *ctx;
	struct fypal_lch lch;
	double v;

	ctx = theme("ground: {l: gl, c: gc, h: gh}\n"
		    "params: {gc: 0, gh: 0}\n"
		    "dark: {params: {gl: 0.2}}\n"
		    "light: {params: {gl: 0.9}}\n"
		    "colors: {g: 'oklch(gl, gc, gh)', "
		    "r: 'oklch(gl + 0.05, gc, gh)'}\n");
	/* the colour of the terminal becomes the ground of the active variant */
	CHECK(!fypal_ctx_set_ground(ctx, 0x1e1e2e));
	CHECK(channel_diff(fypal_ctx_color(ctx, "g"), 0x1e1e2e) <= 1);
	lch = fypal_lab_to_lch(fypal_rgb_to_lab(0x1e1e2e));
	CHECK(!fypal_ctx_param(ctx, "gl", &v) && fabs(v - lch.L) < 1e-9);
	CHECK(!fypal_ctx_param(ctx, "gc", &v) && fabs(v - lch.C) < 1e-9);
	CHECK(fabs(fypal_rgb_to_lab(fypal_ctx_color(ctx, "r")).L -
		   (lch.L + 0.05)) < 0.01);
	/* the other variant keeps the ground of the theme */
	fypal_ctx_set_variant(ctx, FYPAL_VARIANT_LIGHT);
	CHECK(!fypal_ctx_param(ctx, "gl", &v) && v == 0.9);
	CHECK(!fypal_ctx_param(ctx, "gc", &v) && v == 0.0);
	fypal_ctx_destroy(ctx);

	/* a theme that names no ground cannot take one */
	ctx = theme("params: {a: 1}\n");
	CHECK(fypal_ctx_set_ground(ctx, 0x000000) == -1);
	CHECK(strstr(fypal_ctx_error(ctx), "names no ground") != NULL);
	CHECK(fypal_ctx_define_ground(ctx, "a", "b", NULL) == -1);
	CHECK(strstr(fypal_ctx_error(ctx), "ground") != NULL);
	CHECK(!fypal_ctx_define_ground(ctx, "a", "b", "c"));
	CHECK(!fypal_ctx_set_ground(ctx, 0x000000));
	CHECK(!fypal_ctx_param(ctx, "a", &v) && fabs(v) < 1e-9);
	fypal_ctx_destroy(ctx);

	theme_fails("ground: {l: gl, c: gc}", "ground: must name l, c and h",
		    __LINE__);
	theme_fails("ground: {l: gl, c: gc, h: gh, x: gx}",
		    "ground: must name l, c and h", __LINE__);
	theme_fails("dark: {ground: {l: a, c: b, h: c}}", "unknown key 'ground'",
		    __LINE__);
}

static void test_theme_ansi16(void)
{
	struct fypal_caps caps = {
		.depth = FYPAL_DEPTH_16,
		.attrs = FYPAL_ATTR_ALL,
	};
	struct fypal_ctx *ctx;
	char buf[32];

	ctx = ember(&caps);
	fypal_ctx_color_sgr(ctx, "gold", FYPAL_LAYER_FG, buf, sizeof(buf));
	CHECK(!strcmp(buf, "\033[93m"));
	fypal_ctx_color_sgr(ctx, "raise", FYPAL_LAYER_BG, buf, sizeof(buf));
	CHECK(!strcmp(buf, ""));
	fypal_ctx_color_sgr(ctx, "ground", FYPAL_LAYER_BG, buf, sizeof(buf));
	CHECK(!strcmp(buf, "\033[49m"));
	fypal_ctx_set_variant(ctx, FYPAL_VARIANT_LIGHT);
	fypal_ctx_color_sgr(ctx, "gold", FYPAL_LAYER_FG, buf, sizeof(buf));
	CHECK(!strcmp(buf, "\033[33m"));
	fypal_ctx_destroy(ctx);

	/* a colour with no ansi16 form takes its nearest ANSI colour */
	ctx = fypal_ctx_create(&caps);
	CHECK(!fypal_ctx_load(ctx, "colors: {red: '#ff0000'}", "test"));
	fypal_ctx_color_sgr(ctx, "red", FYPAL_LAYER_FG, buf, sizeof(buf));
	CHECK(!strcmp(buf, "\033[91m"));
	fypal_ctx_destroy(ctx);
}

static void test_theme_terminal16(void)
{
	struct fypal_ctx *ctx;
	uint32_t pal[16];
	int i;

	ctx = ember(NULL);
	CHECK(fypal_ctx_terminal16(ctx, pal));
	CHECK(pal[15] == fypal_ctx_color(ctx, "ink"));
	/* a bright hue sits further from the ground than its normal form */
	for (i = 1; i <= 6; i++)
		CHECK(fypal_rgb_to_lab(pal[i + 8]).L > fypal_rgb_to_lab(pal[i]).L);
	fypal_ctx_set_variant(ctx, FYPAL_VARIANT_LIGHT);
	CHECK(fypal_ctx_terminal16(ctx, pal));
	CHECK(pal[0] == fypal_ctx_color(ctx, "ink"));
	fypal_ctx_destroy(ctx);
}

static void test_theme_file(void)
{
	struct fypal_ctx *ctx;
	const char *dir = getenv("FYPAL_THEME_DIR");
	char path[1024];

	if (!dir)
		return;
	snprintf(path, sizeof(path), "%s/ember.yaml", dir);
	ctx = fypal_ctx_create(NULL);
	CHECK(!fypal_ctx_load_file(ctx, path));
	CHECK(fypal_ctx_color(ctx, "ink") != FYPAL_RGB_INVALID);
	CHECK(fypal_ctx_load_file(ctx, "/nonexistent/theme.yaml") == -1);
	CHECK(*fypal_ctx_error(ctx));
	fypal_ctx_destroy(ctx);
}

static void test_ember_gamut(void)
{
	struct fypal_ctx *ctx;
	const char *name;
	size_t i;
	int v;

	ctx = ember(NULL);
	for (v = FYPAL_VARIANT_DARK; v <= FYPAL_VARIANT_LIGHT; v++) {
		fypal_ctx_set_variant(ctx, (enum fypal_variant)v);
		for (i = 0; (name = fypal_ctx_color_name(ctx, i)); i++)
			CHECK(fypal_ctx_color(ctx, name) <= 0xffffff);
	}
	fypal_ctx_destroy(ctx);
}

static void test_ember_ramp(void)
{
	static const char *const ramp[] = {
		"ground", "raise", "rule", "faint", "dim", "ink",
	};
	struct fypal_ctx *ctx;
	double prev, L;
	size_t i;

	ctx = ember(NULL);
	prev = -1.0;
	for (i = 0; i < 6; i++) {
		L = fypal_rgb_to_lab(fypal_ctx_color(ctx, ramp[i])).L;
		CHECK(L > prev);
		prev = L;
	}
	fypal_ctx_set_variant(ctx, FYPAL_VARIANT_LIGHT);
	prev = 2.0;
	for (i = 0; i < 6; i++) {
		L = fypal_rgb_to_lab(fypal_ctx_color(ctx, ramp[i])).L;
		CHECK(L < prev);
		prev = L;
	}
	fypal_ctx_destroy(ctx);
}

static void test_ember_contrast(void)
{
	static const char *const text[] = {
		"dim", "ink", "gold", "blue", "violet", "cyan", "green",
		"coral", "green_strong", "coral_strong",
	};
	struct fypal_ctx *ctx;
	uint32_t ground, c;
	size_t i;
	int v;

	/* every colour that carries text reads on the ground at 4.5:1 */
	ctx = ember(NULL);
	for (v = FYPAL_VARIANT_DARK; v <= FYPAL_VARIANT_LIGHT; v++) {
		fypal_ctx_set_variant(ctx, (enum fypal_variant)v);
		ground = fypal_ctx_color(ctx, "ground");
		for (i = 0; i < sizeof(text) / sizeof(text[0]); i++) {
			c = fypal_ctx_color(ctx, text[i]);
			if (fypal_contrast(c, ground) < 4.5) {
				fprintf(stderr, "%s on ground: %.2f (variant %d)\n",
					text[i], fypal_contrast(c, ground), v);
				failures++;
			}
		}
	}
	fypal_ctx_destroy(ctx);
}

static void test_ember_focus(void)
{
	static const char *const text[] = {
		"dim", "ink", "gold", "blue", "violet", "cyan", "green",
		"coral", "green_strong", "coral_strong",
	};
	const struct fypal_style *style;
	const struct fypal_role *role;
	struct fypal_ctx *ctx;
	uint32_t wash, ground, c;
	size_t i;
	int v;

	/* the ground of what holds the keys is a wash, and text reads on it */
	ctx = ember(NULL);
	for (v = FYPAL_VARIANT_DARK; v <= FYPAL_VARIANT_LIGHT; v++) {
		fypal_ctx_set_variant(ctx, (enum fypal_variant)v);
		wash = fypal_ctx_color(ctx, "wash_focus");
		ground = fypal_ctx_color(ctx, "ground");
		CHECK(wash != FYPAL_RGB_INVALID);
		CHECK(wash != ground);
		for (i = 0; i < sizeof(text) / sizeof(text[0]); i++) {
			c = fypal_ctx_color(ctx, text[i]);
			if (fypal_contrast(c, wash) < 4.5) {
				fprintf(stderr, "%s on wash_focus: %.2f (variant %d)\n",
					text[i], fypal_contrast(c, wash), v);
				failures++;
			}
		}
	}
	/* the work pane names it for a renderer */
	role = fypal_ctx_role(ctx, "pane.focus");
	CHECK(role != NULL);
	style = role ? fypal_role_style(role) : NULL;
	CHECK(style && style->bg == fypal_ctx_color_ref(ctx, "wash_focus"));
	fypal_ctx_destroy(ctx);
}

/* Ember over the ground of a terminal: dark and light grounds of real
 * terminal themes. */
static void ember_ground_check(struct fypal_ctx *ctx, enum fypal_variant variant,
			       uint32_t rgb, double raise_step)
{
	static const char *const ramp[] = {
		"ground", "raise", "rule", "faint", "dim", "ink",
	};
	static const char *const text[] = { "dim", "ink" };
	uint32_t ground;
	double prev, L;
	size_t i;

	fypal_ctx_set_variant(ctx, variant);
	CHECK(!fypal_ctx_set_ground(ctx, rgb));
	ground = fypal_ctx_color(ctx, "ground");
	CHECK(channel_diff(ground, rgb) <= 1);
	/* the ramp keeps its steps over the new ground */
	CHECK(fabs(fypal_rgb_to_lab(fypal_ctx_color(ctx, "raise")).L -
		   fypal_rgb_to_lab(ground).L - raise_step) < 0.01);
	prev = variant == FYPAL_VARIANT_DARK ? -1.0 : 2.0;
	for (i = 0; i < sizeof(ramp) / sizeof(ramp[0]); i++) {
		L = fypal_rgb_to_lab(fypal_ctx_color(ctx, ramp[i])).L;
		CHECK(variant == FYPAL_VARIANT_DARK ? L >= prev : L <= prev);
		prev = L;
	}
	for (i = 0; i < sizeof(text) / sizeof(text[0]); i++) {
		if (fypal_contrast(fypal_ctx_color(ctx, text[i]), ground) < 4.5) {
			fprintf(stderr, "%s on ground #%06x: %.2f\n", text[i],
				rgb, fypal_contrast(fypal_ctx_color(ctx, text[i]),
						    ground));
			failures++;
		}
	}
}

static void test_ember_ground(void)
{
	/* not black: 8 bit steps near black are coarser than a ramp step */
	static const uint32_t dark[] = { 0x0d1117, 0x1e1e2e, 0x282c34, 0x002b36 };
	static const uint32_t light[] = { 0xffffff, 0xfdf6e3, 0xeff1f5 };
	struct fypal_ctx *ctx;
	struct fypal_lch lch;
	size_t i;

	/* without a terminal ground, Ember keeps the ramp it always had */
	ctx = ember(NULL);
	lch.L = 0.16;
	lch.C = 0.008;
	lch.h = 85;
	CHECK(fypal_ctx_color(ctx, "ground") == fypal_lch_to_rgb(lch));
	lch.L = 0.20;
	CHECK(fypal_ctx_color(ctx, "raise") == fypal_lch_to_rgb(lch));
	lch.L = 0.92;
	CHECK(fypal_ctx_color(ctx, "ink") == fypal_lch_to_rgb(lch));
	fypal_ctx_set_variant(ctx, FYPAL_VARIANT_LIGHT);
	lch.L = 0.95;
	CHECK(fypal_ctx_color(ctx, "raise") == fypal_lch_to_rgb(lch));
	lch.L = 0.21;
	CHECK(fypal_ctx_color(ctx, "ink") == fypal_lch_to_rgb(lch));

	for (i = 0; i < sizeof(dark) / sizeof(dark[0]); i++)
		ember_ground_check(ctx, FYPAL_VARIANT_DARK, dark[i], 0.04);
	for (i = 0; i < sizeof(light) / sizeof(light[0]); i++)
		ember_ground_check(ctx, FYPAL_VARIANT_LIGHT, light[i], -0.03);
	fypal_ctx_destroy(ctx);
}

static void test_role_lookup(void)
{
	struct fypal_ctx *ctx;
	const struct fypal_role *r;
	struct fypal_style s;
	uint32_t gold;
	char want[64];

	ctx = ember(NULL);
	gold = fypal_ctx_color(ctx, "gold");
	r = fypal_ctx_role(ctx, "md.heading.2");
	CHECK(r && !strcmp(fypal_role_name(r), "md.heading.2"));
	fypal_ctx_resolve(ctx, r, &s);
	CHECK(s.fg == gold);
	CHECK(s.attrs_set == FYPAL_ATTR_BOLD);
	CHECK(s.bg == FYPAL_COLOR_UNSET);

	snprintf(want, sizeof(want), "\033[1;38;2;%u;%u;%um",
		 (gold >> 16) & 0xff, (gold >> 8) & 0xff, gold & 0xff);
	CHECK(!strcmp(fypal_role_on(ctx, r), want));
	CHECK(!strcmp(fypal_role_off(ctx, r), "\033[22;39m"));
	CHECK(!strcmp(fypal_ctx_on(ctx, "nonexistent"), ""));
	CHECK(!strcmp(fypal_role_on(ctx, NULL), ""));
	fypal_ctx_destroy(ctx);
}

static void test_role_fallback(void)
{
	struct fypal_ctx *ctx;
	const struct fypal_role *r;

	ctx = ember(NULL);
	r = fypal_ctx_role(ctx, "md.heading.7");
	CHECK(r && !strcmp(fypal_role_name(r), "md.heading"));
	r = fypal_ctx_role(ctx, "md.heading.2.extra.deep");
	CHECK(r && !strcmp(fypal_role_name(r), "md.heading.2"));
	/* a capture name falls back through its own hierarchy */
	r = fypal_ctx_role(ctx, "code.keyword.control.return");
	CHECK(r && !strcmp(fypal_role_name(r), "code.keyword"));
	CHECK(!fypal_ctx_role(ctx, "nothing.here"));
	CHECK(!fypal_ctx_role(ctx, ""));

	/* a cached miss is answered once the role exists */
	CHECK(!fypal_ctx_role(ctx, "late.role"));
	CHECK(!fypal_ctx_define_role(ctx, "late", "bold"));
	r = fypal_ctx_role(ctx, "late.role");
	CHECK(r && !strcmp(fypal_role_name(r), "late"));
	fypal_ctx_destroy(ctx);
}

static void test_role_inherit(void)
{
	struct fypal_ctx *ctx;
	struct fypal_style s;

	ctx = ember(NULL);
	CHECK(!fypal_ctx_define_role(ctx, "a", "fg=gold bold italic"));
	CHECK(!fypal_ctx_define_role(ctx, "a.b", "bg=raise -bold"));
	fypal_ctx_resolve(ctx, fypal_ctx_role(ctx, "a.b"), &s);
	CHECK(s.fg == fypal_ctx_color(ctx, "gold"));
	CHECK(s.bg == fypal_ctx_color(ctx, "raise"));
	CHECK(s.attrs_set == FYPAL_ATTR_ITALIC);

	/* base= replaces the dotted parent */
	CHECK(!fypal_ctx_define_role(ctx, "c.d", "base=ok underline"));
	fypal_ctx_resolve(ctx, fypal_ctx_role(ctx, "c.d"), &s);
	CHECK(s.fg == fypal_ctx_color(ctx, "green_strong"));
	CHECK(s.attrs_set == FYPAL_ATTR_UNDERLINE);
	CHECK(!strcmp(fypal_role_base(fypal_ctx_role(ctx, "c.d")), "ok"));

	/* a cycle ends at the depth limit instead of recursing forever */
	CHECK(!fypal_ctx_define_role(ctx, "x", "base=y fg=cyan"));
	CHECK(!fypal_ctx_define_role(ctx, "y", "base=x bold"));
	fypal_ctx_resolve(ctx, fypal_ctx_role(ctx, "x"), &s);
	CHECK(s.fg == fypal_ctx_color(ctx, "cyan"));
	CHECK(fypal_ctx_on(ctx, "y")[0] == '\033');

	/* literal colours, with blanks inside the parentheses */
	CHECK(!fypal_ctx_define_role(ctx, "lit", "fg=#102030 bg=oklch(0.5 0 0) "
				     "ul=default"));
	fypal_ctx_resolve(ctx, fypal_ctx_role(ctx, "lit"), &s);
	CHECK(s.fg == 0x102030);
	CHECK(FYPAL_COLOR_IS_RGB(s.bg));
	CHECK(s.ul == FYPAL_COLOR_DEFAULT);
	fypal_ctx_destroy(ctx);
}

static void test_role_caps(void)
{
	struct fypal_caps caps = {
		.depth = FYPAL_DEPTH_16,
		.attrs = FYPAL_ATTR_BOLD | FYPAL_ATTR_UNDERLINE,
		.underline_color = false,
	};
	struct fypal_ctx *ctx;

	ctx = ember(&caps);
	/* the underline colour and the italic are dropped, the rest stays */
	CHECK(!strcmp(fypal_ctx_on(ctx, "md.link"), "\033[4;96m"));
	CHECK(!strcmp(fypal_ctx_off(ctx, "md.link"), "\033[24;39m"));
	CHECK(!strcmp(fypal_ctx_on(ctx, "md.quote"), "\033[39m"));
	/* a wash has no 16 colour form */
	CHECK(!strcmp(fypal_ctx_on(ctx, "diff.add"), ""));

	caps.depth = FYPAL_DEPTH_NONE;
	caps.attrs = 0;
	fypal_ctx_set_caps(ctx, &caps);
	CHECK(!strcmp(fypal_ctx_on(ctx, "md.heading.2"), ""));
	CHECK(!strcmp(fypal_ctx_off(ctx, "md.heading.2"), ""));

	caps.depth = FYPAL_DEPTH_256;
	caps.attrs = FYPAL_ATTR_ALL;
	caps.underline_color = true;
	fypal_ctx_set_caps(ctx, &caps);
	CHECK(strstr(fypal_ctx_on(ctx, "md.link"), ";58;5;") != NULL);
	CHECK(strstr(fypal_ctx_off(ctx, "md.link"), "59") != NULL);
	fypal_ctx_destroy(ctx);
}

static void test_role_yaml(void)
{
	struct fypal_ctx *ctx;
	struct fypal_style s;

	ctx = theme("colors: {a: '#aabbcc', b: '#010101'}\n"
		    "roles:\n"
		    "  one:\n"
		    "    fg: a\n"
		    "    attrs: [bold, italic]\n"
		    "    two: {bg: b, attrs: '-bold underline'}\n"
		    "  three: 'fg=b base=one'\n"
		    "  group:\n"
		    "    leaf: {fg: b}\n"
		    "    deep.er: {fg: a}\n"
		    "    2: {fg: a}\n");
	fypal_ctx_resolve(ctx, fypal_ctx_role(ctx, "one.two"), &s);
	CHECK(s.fg == 0xaabbcc && s.bg == 0x010101);
	CHECK(s.attrs_set == (FYPAL_ATTR_ITALIC | FYPAL_ATTR_UNDERLINE));
	fypal_ctx_resolve(ctx, fypal_ctx_role(ctx, "three"), &s);
	CHECK(s.fg == 0x010101);
	CHECK(s.attrs_set == (FYPAL_ATTR_BOLD | FYPAL_ATTR_ITALIC));
	/* a node without fields groups roles and is not one itself */
	CHECK(fypal_ctx_role(ctx, "group") == NULL);
	CHECK(fypal_ctx_role(ctx, "group.leaf") != NULL);
	CHECK(fypal_ctx_role(ctx, "group.deep.er") != NULL);
	CHECK(fypal_ctx_role(ctx, "group.2") != NULL);
	fypal_ctx_destroy(ctx);
}

static void test_role_invalidate(void)
{
	struct fypal_ctx *ctx;
	char dark[64];

	ctx = ember(NULL);
	snprintf(dark, sizeof(dark), "%s", fypal_ctx_on(ctx, "ref"));
	fypal_ctx_set_variant(ctx, FYPAL_VARIANT_LIGHT);
	CHECK(strcmp(dark, fypal_ctx_on(ctx, "ref")));
	fypal_ctx_set_variant(ctx, FYPAL_VARIANT_DARK);
	CHECK(!strcmp(dark, fypal_ctx_on(ctx, "ref")));

	/* redefining a parent changes the child */
	CHECK(!fypal_ctx_define_role(ctx, "md.heading", "fg=ink underline"));
	CHECK(strstr(fypal_ctx_on(ctx, "md.heading.2"), "4;") != NULL);

	/* redefining a colour changes the roles that use it */
	CHECK(!fypal_ctx_define_color(ctx, "cyan", FYPAL_SECTION_ALL, "#000001"));
	CHECK(strstr(fypal_ctx_on(ctx, "ref"), "38;2;0;0;1") != NULL);
	fypal_ctx_destroy(ctx);
}

static void test_role_style_sgr(void)
{
	struct fypal_caps caps = {
		.depth = FYPAL_DEPTH_TRUECOLOR,
		.attrs = FYPAL_ATTR_ALL & ~FYPAL_ATTR_UNDERCURL,
		.underline_color = true,
	};
	struct fypal_style s = {
		.fg = 0x010203,
		.bg = FYPAL_COLOR_DEFAULT,
		.ul = FYPAL_COLOR_UNSET,
		.attrs_set = FYPAL_ATTR_UNDERCURL,
	};
	struct fypal_ctx *ctx;
	char on[64], off[64];

	ctx = fypal_ctx_create(&caps);
	CHECK(fypal_ctx_style_sgr(ctx, &s, on, sizeof(on), off, sizeof(off)) > 0);
	CHECK(!strcmp(on, "\033[4;38;2;1;2;3;49m"));
	CHECK(!strcmp(off, "\033[24;39;49m"));

	caps.attrs = FYPAL_ATTR_ALL;
	fypal_ctx_set_caps(ctx, &caps);
	fypal_ctx_style_sgr(ctx, &s, on, sizeof(on), off, sizeof(off));
	CHECK(!strcmp(on, "\033[4:3;38;2;1;2;3;49m"));
	fypal_ctx_destroy(ctx);
}

static void test_role_many(void)
{
	struct fypal_ctx *ctx;
	const struct fypal_role *r;
	char name[64];
	int i;

	/* enough roles to grow the hash several times */
	ctx = fypal_ctx_create(NULL);
	for (i = 0; i < 2000; i++) {
		snprintf(name, sizeof(name), "grp%d.role%d", i % 37, i);
		CHECK(!fypal_ctx_define_role(ctx, name, i & 1 ? "bold" : "italic"));
	}
	CHECK(fypal_ctx_role_count(ctx) == 2000);
	for (i = 0; i < 2000; i += 97) {
		snprintf(name, sizeof(name), "grp%d.role%d.leaf", i % 37, i);
		r = fypal_ctx_role(ctx, name);
		snprintf(name, sizeof(name), "grp%d.role%d", i % 37, i);
		CHECK(r && !strcmp(fypal_role_name(r), name));
	}
	fypal_ctx_destroy(ctx);
}

static void test_glyph_yaml(void)
{
	struct fypal_ctx *ctx;

	ctx = theme("glyphs:\n"
		    "  mark: \"?\"\n"
		    "  gutter:\n"
		    "    utf: \"|\"\n"
		    "    tool: {utf: \"\u2192\", ascii: \"->\"}\n"
		    "    deep.er: {utf: \"x\"}\n");
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "mark", false), "?"));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "mark", true), "?"));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "gutter.tool", false), "\u2192"));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "gutter.tool", true), "->"));
	/* an undefined name answers with its nearest ancestor */
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "gutter.tool.pending", true), "->"));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "gutter.other", false), "|"));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "gutter.deep.er", true), "x"));
	CHECK(fypal_ctx_glyph(ctx, "nothing", false) == NULL);
	CHECK(fypal_ctx_glyph_count(ctx) == 4);
	fypal_ctx_destroy(ctx);
}

static void test_glyph_api(void)
{
	struct fypal_ctx *ctx;
	bool found = false;
	const char *name;
	size_t i;

	ctx = fypal_ctx_create(NULL);
	CHECK(!fypal_ctx_define_glyph(ctx, "a.b", "\u25cf", "*"));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "a.b", false), "\u25cf"));
	CHECK(!fypal_ctx_define_glyph(ctx, "a.b", "o", NULL));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "a.b", true), "o"));
	CHECK(fypal_ctx_glyph_count(ctx) == 1);
	for (i = 0; (name = fypal_ctx_glyph_name(ctx, i)); i++)
		found |= !strcmp(name, "a.b");
	CHECK(found);
	CHECK(fypal_ctx_define_glyph(ctx, "bad..name", "x", NULL) == -1);
	CHECK(fypal_ctx_define_glyph(ctx, "ok", NULL, "x") == -1);
	CHECK(strstr(fypal_ctx_error(ctx), "invalid glyph") != NULL);
	fypal_ctx_destroy(ctx);
}

static void test_glyph_errors(void)
{
	theme_fails("glyphs: [a]", "glyphs: must be a mapping", __LINE__);
	theme_fails("glyphs: {a: 1}", "glyphs/a: must be a mapping or a string",
		    __LINE__);
	theme_fails("glyphs: {a: {ascii: x}}", "utf must be a string", __LINE__);
	theme_fails("glyphs: {a: {utf: x, ascii: [y]}}", "utf must be a string",
		    __LINE__);
}

static void test_ember_glyphs(void)
{
	struct fypal_ctx *ctx;
	double cols;

	ctx = ember(NULL);
	CHECK(!fypal_ctx_param(ctx, "gutter.cols", &cols) && cols == 3.0);
	CHECK(!strcmp(fypal_ctx_param_string(ctx, "md.code.rules"), "bubble-raise-faint"));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "gutter.tool", false), "\u2192"));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "gutter.tool", true), "->"));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "gutter.result", true), "`-"));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "md.bullet", false), "\u2022"));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "md.task.open", true), "[ ]"));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "tool.pending", false), "\u2192"));
	CHECK(!strcmp(fypal_ctx_glyph(ctx, "tool.pending.1", true), " "));
	/* a frame the theme does not define answers with frame 0 */
	CHECK(fypal_ctx_glyph(ctx, "tool.pending.2", false) ==
	      fypal_ctx_glyph(ctx, "tool.pending", false));
	fypal_ctx_destroy(ctx);
}

static const struct {
	const char *name;
	void (*fn)(void);
} tests[] = {
	{ "glyph_yaml", test_glyph_yaml },
	{ "glyph_api", test_glyph_api },
	{ "glyph_errors", test_glyph_errors },
	{ "ember_glyphs", test_ember_glyphs },
	{ "lab_roundtrip", test_lab_roundtrip },
	{ "lch_reference", test_lch_reference },
	{ "gamut_map", test_gamut_map },
	{ "xterm256", test_xterm256 },
	{ "ansi16", test_ansi16 },
	{ "contrast", test_contrast },
	{ "sgr_rgb", test_sgr_rgb },
	{ "color_parse", test_color_parse },
	{ "theme_builtin", test_theme_builtin },
	{ "theme_expr", test_theme_expr },
	{ "theme_sections", test_theme_sections },
	{ "theme_errors", test_theme_errors },
	{ "theme_api", test_theme_api },
	{ "param_strings", test_param_strings },
	{ "theme_ground", test_theme_ground },
	{ "theme_ansi16", test_theme_ansi16 },
	{ "theme_terminal16", test_theme_terminal16 },
	{ "theme_file", test_theme_file },
	{ "ember_gamut", test_ember_gamut },
	{ "ember_ramp", test_ember_ramp },
	{ "ember_contrast", test_ember_contrast },
	{ "ember_focus", test_ember_focus },
	{ "ember_ground", test_ember_ground },
	{ "role_lookup", test_role_lookup },
	{ "role_fallback", test_role_fallback },
	{ "role_inherit", test_role_inherit },
	{ "role_caps", test_role_caps },
	{ "role_yaml", test_role_yaml },
	{ "role_invalidate", test_role_invalidate },
	{ "role_style_sgr", test_role_style_sgr },
	{ "role_many", test_role_many },
};

int main(int argc, char *argv[])
{
	size_t i;
	int ran = 0;

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		if (argc > 1 && strcmp(argv[1], tests[i].name))
			continue;
		tests[i].fn();
		ran++;
	}
	if (!ran) {
		fprintf(stderr, "no such test: %s\n", argv[1]);
		return 2;
	}
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
