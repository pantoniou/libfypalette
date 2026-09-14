/*
 * fypalette-show - render a theme as colour bands, text and a sample screen
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "libfypalette.h"

#define SHOW_BANDS	(1U << 0)
#define SHOW_TEXT	(1U << 1)
#define SHOW_ROLES	(1U << 2)
#define SHOW_SAMPLE	(1U << 3)

/* The role of the page: the text role on the ground of the theme. */
#define PAGE		"show.page"

static int width = 80;

/*
 * Every line is painted on a base role, normally the ground. A span turns
 * its role on and then off again, and the off can reset the background, so
 * the base is turned on again after each span.
 */
struct painter {
	struct fypal_ctx *ctx;
	const char *base;
	int col;
};

static void line_begin(struct painter *p, const char *base)
{
	p->base = base;
	p->col = 0;
	fputs(fypal_ctx_on(p->ctx, base), stdout);
}

static int text_cols(const char *s)
{
	int n = 0;

	/* every UTF-8 lead byte is one column; the sample uses no wide glyphs */
	for (; *s; s++) {
		if ((*s & 0xc0) != 0x80)
			n++;
	}
	return n;
}

static void span(struct painter *p, const char *role, const char *text)
{
	fputs(fypal_ctx_on(p->ctx, role), stdout);
	fputs(text, stdout);
	fputs(fypal_ctx_off(p->ctx, role), stdout);
	fputs(fypal_ctx_on(p->ctx, p->base), stdout);
	p->col += text_cols(text);
}

/* Pad to column col with the base role. */
static void pad_to(struct painter *p, int col)
{
	while (p->col < col && p->col < width) {
		putchar(' ');
		p->col++;
	}
}

static void line_end(struct painter *p)
{
	pad_to(p, width);
	fputs(fypal_ctx_off(p->ctx, p->base), stdout);
	fputs("\033[0m\n", stdout);
}

static void blank(struct painter *p, const char *base)
{
	line_begin(p, base);
	line_end(p);
}

static void heading(struct painter *p, const char *title)
{
	line_begin(p, PAGE);
	span(p, "chrome", "── ");
	span(p, "text", title);
	span(p, "chrome", " ");
	while (p->col < width)
		span(p, "chrome.rule", "─");
	line_end(p);
}

static uint32_t text_on(struct fypal_ctx *ctx, uint32_t bg)
{
	uint32_t ink = fypal_ctx_color(ctx, "ink");
	uint32_t ground = fypal_ctx_color(ctx, "ground");

	if (ink == FYPAL_RGB_INVALID)
		ink = 0xffffff;
	if (ground == FYPAL_RGB_INVALID)
		ground = 0x000000;
	return fypal_contrast(ink, bg) >= fypal_contrast(ground, bg) ? ink : ground;
}

static void show_bands(struct fypal_ctx *ctx)
{
	const struct fypal_caps *caps = fypal_ctx_caps(ctx);
	struct painter p = { .ctx = ctx };
	char bg[48], fg[48], line[256];
	struct fypal_lch lch;
	const char *name;
	uint32_t rgb;
	size_t i;
	int n;

	heading(&p, "bands");
	for (i = 0; (name = fypal_ctx_color_name(ctx, i)); i++) {
		rgb = fypal_ctx_color(ctx, name);
		lch = fypal_lab_to_lch(fypal_rgb_to_lab(rgb));
		fypal_ctx_color_sgr(ctx, name, FYPAL_LAYER_BG, bg, sizeof(bg));
		n = fypal_sgr_params_rgb(text_on(ctx, rgb), FYPAL_LAYER_FG,
					 caps->depth, fg, sizeof(fg));
		snprintf(line, sizeof(line),
			 "  %-14s #%06x   L %.3f  C %.3f  h %5.1f   xterm %3d",
			 name, rgb, lch.L, lch.C, lch.h, fypal_rgb_to_xterm256(rgb));
		printf("%s%s%s%s%-*s\033[0m\n", bg, n ? "\033[" : "",
		       n ? fg : "", n ? "m" : "", width, line);
	}
}

static void show_text(struct fypal_ctx *ctx)
{
	struct painter p = { .ctx = ctx };
	char fg[48], ul[48];
	const char *name;
	uint32_t rgb, ground, raise;
	size_t i;
	int half = width / 2;

	ground = fypal_ctx_color(ctx, "ground");
	raise = fypal_ctx_color(ctx, "raise");
	heading(&p, "text on ground and raise");
	for (i = 0; (name = fypal_ctx_color_name(ctx, i)); i++) {
		rgb = fypal_ctx_color(ctx, name);
		fypal_ctx_color_sgr(ctx, name, FYPAL_LAYER_FG, fg, sizeof(fg));
		fypal_ctx_color_sgr(ctx, name, FYPAL_LAYER_UL, ul, sizeof(ul));

		line_begin(&p, PAGE);
		printf("%s  %-14s The quick brown fox\033[39m", fg, name);
		p.col = 36;
		if (ground != FYPAL_RGB_INVALID)
			printf(" %4.1f", fypal_contrast(rgb, ground));
		p.col += 5;
		pad_to(&p, half);
		fputs(fypal_ctx_off(ctx, PAGE), stdout);
		fputs(fypal_ctx_on(ctx, "user.card"), stdout);
		printf("%s  \033[4m%sjumps\033[24;59m over the lazy dog\033[39m",
		       fg, ul);
		p.col += 26;
		if (raise != FYPAL_RGB_INVALID)
			printf(" %4.1f", fypal_contrast(rgb, raise));
		p.col += 5;
		p.base = "user.card";
		line_end(&p);
	}
}

/* The escape made printable: ESC as \e. */
static void print_escaped(const char *s, char *buf, size_t size)
{
	size_t n = 0;

	for (; *s && n + 3 < size; s++) {
		if (*s == '\033') {
			buf[n++] = '\\';
			buf[n++] = 'e';
		} else {
			buf[n++] = *s;
		}
	}
	buf[n] = '\0';
}

static void show_roles(struct fypal_ctx *ctx)
{
	struct painter p = { .ctx = ctx };
	const struct fypal_role *r;
	char esc[256];
	const char *name;
	size_t i;

	heading(&p, "roles");
	for (i = 0; (r = fypal_ctx_role_at(ctx, i)); i++) {
		name = fypal_role_name(r);
		line_begin(&p, PAGE);
		span(&p, "chrome", "  ");
		span(&p, "chrome", name);
		pad_to(&p, 24);
		span(&p, name, "Sample text");
		pad_to(&p, 38);
		print_escaped(fypal_role_on(ctx, r), esc, sizeof(esc));
		/* the escape gets the columns after the sample, within the buffer */
		if (width - 40 < (int)sizeof(esc))
			esc[width > 40 ? width - 40 : 0] = '\0';
		span(&p, "chrome", esc);
		line_end(&p);
	}
}

struct seg {
	const char *role;
	const char *text;
};

#define SEGS(...)	((const struct seg[]){ __VA_ARGS__, { NULL, NULL } })

static void sample_line(struct painter *p, const char *base,
			const struct seg *segs)
{
	line_begin(p, base);
	for (; segs->text; segs++)
		span(p, segs->role ? segs->role : "text", segs->text);
	line_end(p);
}

static void show_sample(struct fypal_ctx *ctx)
{
	struct painter p = { .ctx = ctx };

	heading(&p, "sample");
	blank(&p, PAGE);
	sample_line(&p, "user.card", SEGS(
		{ "user", "▌  " },
		{ "user.card", "make the peek path cheaper, and show me the diff" }));
	blank(&p, PAGE);
	sample_line(&p, PAGE, SEGS(
		{ "reasoning", "▏  " },
		{ "reasoning.text", "the scanner re-reads the token cache on every peek" }));
	blank(&p, PAGE);
	sample_line(&p, PAGE, SEGS(
		{ "tool.name", "→  read" }, { NULL, "  " },
		{ "ref", "src/lib/fy-parse.c" },
		{ "tool.elapsed", "    0.2s" }));
	sample_line(&p, PAGE, SEGS(
		{ "chrome", "⎿  " }, { "tool.output", "1,284 lines" },
		{ "chrome", "  ⋯ 1,270 elided · /history --tool-detail full last 1" }));
	sample_line(&p, PAGE, SEGS(
		{ "tool.name", "→  patch" }, { NULL, "  " },
		{ "ref", "src/lib/fy-scan.c" },
		{ "tool.fail", "  ✖ failed" }, { "tool.elapsed", "  1.1s" }));
	blank(&p, PAGE);
	sample_line(&p, PAGE, SEGS({ "md.heading.2", "Plan" }));
	blank(&p, PAGE);
	sample_line(&p, PAGE, SEGS(
		{ NULL, "The peek path is " }, { "md.strong", "quadratic" },
		{ NULL, " when " }, { "md.code", " fy_peek() " },
		{ NULL, " misses; see " }, { "md.link", "the scanner notes" },
		{ NULL, "." }));
	sample_line(&p, PAGE, SEGS(
		{ "md.bullet", "  •  " }, { NULL, "cache the last token, " },
		{ "md.emphasis", "not the last line" }));
	sample_line(&p, PAGE, SEGS(
		{ "md.task.done", "  ✔  " }, { "md.task.done.text", "profile the scanner" }));
	sample_line(&p, PAGE, SEGS(
		{ "md.task.open", "  ☐  " }, { "md.task.open.text", "land the patch" }));
	sample_line(&p, PAGE, SEGS(
		{ "md.quote.bar", "  ▎  " }, { "md.quote", "measure before you optimise" }));
	blank(&p, PAGE);

	sample_line(&p, "code.head", SEGS(
		{ "code.head", "  src/lib/fy-scan.c                                             " },
		{ "code.head.lang", "c" }));
	sample_line(&p, "code.block", SEGS(
		{ "code.lineno", " 41  " }, { "code.keyword", "static inline " },
		{ "code.type", "int " }, { "code.function", "fy_peek" },
		{ "code.punctuation", "(" }, { "code.keyword", "struct " },
		{ "code.type", "fy_scan " }, { "code.punctuation", "*" },
		{ "code.variable", "s" }, { "code.punctuation", ", " },
		{ "code.type", "int " }, { "code.variable", "n" },
		{ "code.punctuation", ")" }));
	sample_line(&p, "code.block", SEGS(
		{ "code.lineno", " 42  " }, { "code.punctuation", "{" }));
	sample_line(&p, "code.block", SEGS(
		{ "code.lineno", " 43  " },
		{ "code.comment", "    /* the cache holds one token */" }));
	sample_line(&p, "diff.del", SEGS(
		{ "diff.lineno.del", " 44 -" }, { "code.function", "    fy_log" },
		{ "code.punctuation", "(" }, { "code.variable", "s" },
		{ "code.punctuation", ", " }, { "code.string", "\"peek %d\"" },
		{ "code.punctuation", ", " }, { "code.variable", "n" },
		{ "code.punctuation", ");" }));
	sample_line(&p, "diff.add", SEGS(
		{ "diff.lineno.add", " 44 +" }, { "code.keyword", "    if " },
		{ "code.punctuation", "(" }, { "code.variable", "s" },
		{ "code.operator", "->" }, { "code.variable", "cached " },
		{ "code.operator", "== " }, { "code.number", "1" },
		{ "code.punctuation", ")" }));
	sample_line(&p, "code.block", SEGS(
		{ "code.lineno", " 45  " }, { "code.preproc", "#ifdef " },
		{ "code.constant", "FY_DEBUG" }));
	blank(&p, PAGE);

	sample_line(&p, PAGE, SEGS(
		{ "md.table.grid", "  ┌──────────┬──────┐" }));
	sample_line(&p, PAGE, SEGS(
		{ "md.table.grid", "  │" }, { "md.table.header", " test     " },
		{ "md.table.grid", "│" }, { "md.table.header", " ok   " },
		{ "md.table.grid", "│" }));
	sample_line(&p, PAGE, SEGS(
		{ "md.table.grid", "  ├──────────┼──────┤" }));
	sample_line(&p, PAGE, SEGS(
		{ "md.table.grid", "  │" }, { "md.table.cell", " parse    " },
		{ "md.table.grid", "│" }, { "ok", " ✔    " },
		{ "md.table.grid", "│" }));
	sample_line(&p, PAGE, SEGS(
		{ "md.table.grid", "  │" }, { "md.table.cell", " scan     " },
		{ "md.table.grid", "│" }, { "fail", " ✖    " },
		{ "md.table.grid", "│" }));
	sample_line(&p, PAGE, SEGS(
		{ "md.table.grid", "  └──────────┴──────┘" }));
	blank(&p, PAGE);

	sample_line(&p, PAGE, SEGS(
		{ "mermaid.stroke", "  ╭────────╮" }, { "mermaid.edge", "      " },
		{ "mermaid.stroke", "╭────────╮" }));
	sample_line(&p, PAGE, SEGS(
		{ "mermaid.stroke", "  │" }, { "mermaid.node", " parse  " },
		{ "mermaid.stroke", "│" }, { "mermaid.edge", "────▶ " },
		{ "mermaid.stroke", "│" }, { "mermaid.node", " emit   " },
		{ "mermaid.stroke", "│" }));
	sample_line(&p, PAGE, SEGS(
		{ "mermaid.stroke", "  ╰────────╯" }, { "mermaid.edge.label", " yaml " },
		{ "mermaid.stroke", "╰────────╯" }));
	blank(&p, PAGE);

	sample_line(&p, PAGE, SEGS(
		{ "notice.sigil", "!  " }, { "notice.class", "error" },
		{ "notice.message", "  patch does not apply to " },
		{ "ref", "src/lib/fy-scan.c" }));
	sample_line(&p, PAGE, SEGS(
		{ "system", "∷  model gpt-5 · branch main · compacted 214 turns" }));
	blank(&p, PAGE);

	sample_line(&p, PAGE, SEGS(
		{ "pane.cap", "──work── half · 3 tiles · 2 shown · +1 hidden ──────  ^w cycle  ^z zoom" }));
	sample_line(&p, PAGE, SEGS(
		{ "tile.sigil.work", "▌$ " }, { "tile.head.focus", "make test" },
		{ "chrome", "  running  " }, { "tool.elapsed", "12.4s" },
		{ "pane.sep", " ┃ " }, { "tile.sigil.view", " ¶ " },
		{ "tile.head", "plan  " }, { "tile.state.ok", "✔ reported" }));
	sample_line(&p, PAGE, SEGS(
		{ "tile.sigil.work", " @ " }, { "tile.head", "bench-runner" },
		{ "tile.state.asks", "  ❓ asks you" },
		{ "pane.sep", "   ┃ " }, { "tile.sigil.notice", " ! " },
		{ "tile.head", "lint  " }, { "tile.state.fail", "✖ failed" }));
	blank(&p, PAGE);

	sample_line(&p, PAGE, SEGS(
		{ "fyai", "?  fyai asks" }));
	sample_line(&p, PAGE, SEGS(
		{ NULL, "   Keep it, or fold it into the new guard?" }));
	sample_line(&p, PAGE, SEGS(
		{ "user", "   1" }, { "text.dim", "  keep both, minimal diff" }));
	sample_line(&p, PAGE, SEGS(
		{ "prompt.asking", "▸  " }, { "text.faint", "1-2, or type an answer" }));
	sample_line(&p, PAGE, SEGS(
		{ "prompt", "❯  " }, { NULL, "type here" }));
	blank(&p, PAGE);
}

static void usage(FILE *fp, const char *prog)
{
	fprintf(fp,
		"usage: %s [options]\n"
		"  -t, --theme NAME|FILE  a built-in theme or a theme file (default ember)\n"
		"  -l, --light            the light variant (default: detect)\n"
		"  -d, --dark             the dark variant\n"
		"  -D, --depth DEPTH      none | 16 | 256 | truecolor (default: detect)\n"
		"  -g, --ground COLOR     make COLOR, or the background of the terminal\n"
		"                         for 'terminal', the ground of the theme\n"
		"  -p, --param NAME=EXPR  override a parameter of the active variant\n"
		"  -c, --color NAME=CEXPR override a colour\n"
		"  -r, --role NAME=FIELDS override a role\n"
		"  -b, --bands            colour bands\n"
		"  -x, --text             each colour as text on ground and raise\n"
		"  -R, --roles            every role with its escape\n"
		"  -s, --sample           a sample screen made of roles\n"
		"  -e, --sgr ROLE         print the escape of a role and exit\n"
		"  -o, --off              with --sgr, print the off escape\n"
		"  -w, --width N          line width (default: terminal)\n"
		"      --list             list the built-in themes\n"
		"  -h, --help             this help\n"
		"Without a selection, everything is shown.\n", prog);
}

static int parse_depth(const char *s, enum fypal_depth *depth)
{
	if (!strcmp(s, "none"))
		*depth = FYPAL_DEPTH_NONE;
	else if (!strcmp(s, "16"))
		*depth = FYPAL_DEPTH_16;
	else if (!strcmp(s, "256"))
		*depth = FYPAL_DEPTH_256;
	else if (!strcmp(s, "truecolor") || !strcmp(s, "24bit"))
		*depth = FYPAL_DEPTH_TRUECOLOR;
	else
		return -1;
	return 0;
}

struct override {
	int kind;
	const char *arg;
};

static int apply_override(struct fypal_ctx *ctx, const struct override *o)
{
	enum fypal_section section;
	char name[128];
	const char *eq;
	int rc;

	eq = strchr(o->arg, '=');
	if (!eq || eq == o->arg || (size_t)(eq - o->arg) >= sizeof(name)) {
		fprintf(stderr, "expected NAME=VALUE: %s\n", o->arg);
		return -1;
	}
	memcpy(name, o->arg, (size_t)(eq - o->arg));
	name[eq - o->arg] = '\0';
	/* an override must win over the variant section of the theme */
	section = fypal_ctx_variant(ctx) == FYPAL_VARIANT_LIGHT ?
		  FYPAL_SECTION_LIGHT : FYPAL_SECTION_DARK;
	if (o->kind == 'p')
		rc = fypal_ctx_define_param(ctx, name, section, eq + 1);
	else if (o->kind == 'c')
		rc = fypal_ctx_define_color(ctx, name, section, eq + 1);
	else
		rc = fypal_ctx_define_role(ctx, name, eq + 1);
	if (!rc)
		rc = fypal_ctx_check(ctx);
	if (rc)
		fprintf(stderr, "%s\n", fypal_ctx_error(ctx));
	return rc;
}

int main(int argc, char *argv[])
{
	static const struct option lopts[] = {
		{ "theme", required_argument, NULL, 't' },
		{ "light", no_argument, NULL, 'l' },
		{ "dark", no_argument, NULL, 'd' },
		{ "depth", required_argument, NULL, 'D' },
		{ "ground", required_argument, NULL, 'g' },
		{ "param", required_argument, NULL, 'p' },
		{ "color", required_argument, NULL, 'c' },
		{ "role", required_argument, NULL, 'r' },
		{ "bands", no_argument, NULL, 'b' },
		{ "text", no_argument, NULL, 'x' },
		{ "roles", no_argument, NULL, 'R' },
		{ "sample", no_argument, NULL, 's' },
		{ "sgr", required_argument, NULL, 'e' },
		{ "off", no_argument, NULL, 'o' },
		{ "width", required_argument, NULL, 'w' },
		{ "list", no_argument, NULL, 'L' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	struct override overrides[64];
	const char *theme = "ember", *sgr_role = NULL, *name, *ground = NULL;
	enum fypal_variant variant = FYPAL_VARIANT_DARK;
	uint32_t ground_rgb = FYPAL_RGB_INVALID;
	struct fypal_caps caps;
	struct fypal_ctx *ctx;
	struct winsize ws;
	enum fypal_depth depth = FYPAL_DEPTH_TRUECOLOR;
	bool have_variant = false, have_depth = false, off = false;
	unsigned int show = 0;
	size_t noverrides = 0, i;
	int opt, rc;

	while ((opt = getopt_long(argc, argv, "t:ldD:g:p:c:r:bxRse:ow:h", lopts,
				  NULL)) != -1) {
		switch (opt) {
		case 't':
			theme = optarg;
			break;
		case 'g':
			ground = optarg;
			break;
		case 'l':
		case 'd':
			variant = opt == 'l' ? FYPAL_VARIANT_LIGHT : FYPAL_VARIANT_DARK;
			have_variant = true;
			break;
		case 'D':
			if (parse_depth(optarg, &depth)) {
				fprintf(stderr, "%s: bad depth: %s\n", argv[0], optarg);
				return EXIT_FAILURE;
			}
			have_depth = true;
			break;
		case 'p':
		case 'c':
		case 'r':
			if (noverrides == sizeof(overrides) / sizeof(overrides[0])) {
				fprintf(stderr, "%s: too many overrides\n", argv[0]);
				return EXIT_FAILURE;
			}
			overrides[noverrides].kind = opt;
			overrides[noverrides++].arg = optarg;
			break;
		case 'b':
			show |= SHOW_BANDS;
			break;
		case 'x':
			show |= SHOW_TEXT;
			break;
		case 'R':
			show |= SHOW_ROLES;
			break;
		case 's':
			show |= SHOW_SAMPLE;
			break;
		case 'e':
			sgr_role = optarg;
			break;
		case 'o':
			off = true;
			break;
		case 'w':
			width = atoi(optarg);
			break;
		case 'L':
			for (i = 0; (name = fypal_builtin_theme_name(i)); i++)
				printf("%s\n", name);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout, argv[0]);
			return EXIT_SUCCESS;
		default:
			usage(stderr, argv[0]);
			return EXIT_FAILURE;
		}
	}

	fypal_caps_detect(STDOUT_FILENO, &caps);
	if (have_depth) {
		caps.depth = depth;
		/* an explicit depth draws even into a pipe */
		if (depth != FYPAL_DEPTH_NONE && !caps.attrs)
			caps.attrs = FYPAL_ATTR_ALL & ~FYPAL_ATTR_UNDERCURL;
		if (depth < FYPAL_DEPTH_256)
			caps.underline_color = false;
	}
	if (ground) {
		if (!strcmp(ground, "terminal")) {
			if (!fypal_detect_background(STDOUT_FILENO, &ground_rgb)) {
				fprintf(stderr, "%s: the terminal did not report its "
					"background\n", argv[0]);
				return EXIT_FAILURE;
			}
		} else {
			ground_rgb = fypal_color_parse(ground);
			if (ground_rgb == FYPAL_RGB_INVALID) {
				fprintf(stderr, "%s: bad ground colour: %s\n", argv[0],
					ground);
				return EXIT_FAILURE;
			}
		}
	}
	if (!have_variant)
		variant = fypal_detect_variant(STDOUT_FILENO, NULL);
	if (width <= 0) {
		width = 80;
		if (!ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) && ws.ws_col)
			width = ws.ws_col;
	}
	if (width == 80 && !ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) && ws.ws_col)
		width = ws.ws_col;

	ctx = fypal_ctx_create(&caps);
	if (!ctx) {
		fprintf(stderr, "%s: cannot create a context\n", argv[0]);
		return EXIT_FAILURE;
	}
	fypal_ctx_set_variant(ctx, variant);
	if (strchr(theme, '/') || strstr(theme, ".yaml"))
		rc = fypal_ctx_load_file(ctx, theme);
	else
		rc = fypal_ctx_load_builtin(ctx, theme);
	if (rc) {
		fprintf(stderr, "%s\n", fypal_ctx_error(ctx));
		fypal_ctx_destroy(ctx);
		return EXIT_FAILURE;
	}
	/* the ground goes before the overrides, which can then adjust it */
	if (ground_rgb != FYPAL_RGB_INVALID &&
	    fypal_ctx_set_ground(ctx, ground_rgb)) {
		fprintf(stderr, "%s\n", fypal_ctx_error(ctx));
		fypal_ctx_destroy(ctx);
		return EXIT_FAILURE;
	}
	/* paint on the ground of the theme, not on the terminal's */
	rc = fypal_ctx_define_role(ctx, PAGE,
				   fypal_ctx_color(ctx, "ground") != FYPAL_RGB_INVALID ?
				   "base=text bg=ground" : "base=text");
	if (rc) {
		fprintf(stderr, "%s\n", fypal_ctx_error(ctx));
		fypal_ctx_destroy(ctx);
		return EXIT_FAILURE;
	}
	for (i = 0; i < noverrides; i++) {
		if (apply_override(ctx, &overrides[i])) {
			fypal_ctx_destroy(ctx);
			return EXIT_FAILURE;
		}
	}

	if (sgr_role) {
		fputs(off ? fypal_ctx_off(ctx, sgr_role) : fypal_ctx_on(ctx, sgr_role),
		      stdout);
		fypal_ctx_destroy(ctx);
		return EXIT_SUCCESS;
	}

	if (!show)
		show = SHOW_BANDS | SHOW_TEXT | SHOW_ROLES | SHOW_SAMPLE;
	if (show & SHOW_BANDS)
		show_bands(ctx);
	if (show & SHOW_TEXT)
		show_text(ctx);
	if (show & SHOW_ROLES)
		show_roles(ctx);
	if (show & SHOW_SAMPLE)
		show_sample(ctx);
	fypal_ctx_destroy(ctx);
	return EXIT_SUCCESS;
}
