/*
 * fypalette-probe.c - print what the terminal answered to the probe
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "libfypalette.h"

static const struct {
	unsigned int flag;
	const char *name;
} flags[] = {
	{ FYPAL_TERM_PROBED, "probed" },
	{ FYPAL_TERM_ANSWERED, "answered" },
	{ FYPAL_TERM_BACKGROUND, "background" },
	{ FYPAL_TERM_FOREGROUND, "foreground" },
	{ FYPAL_TERM_SYNC, "sync" },
	{ FYPAL_TERM_KITTY_KEYS, "kitty-keys" },
	{ FYPAL_TERM_GRAPHEMES, "graphemes" },
	{ FYPAL_TERM_GRAPHEMES_SET, "graphemes-on" },
	{ FYPAL_TERM_MULTIPLEXER, "multiplexer" },
	{ FYPAL_TERM_THEME_REPORT, "theme-report" },
	{ FYPAL_TERM_SIXEL, "sixel" },
	{ FYPAL_TERM_KITTY_GRAPHICS, "kitty-graphics" },
	{ FYPAL_TERM_TRUECOLOR, "truecolor" },
	{ FYPAL_TERM_STYLED_UL, "styled-underline" },
	{ FYPAL_TERM_CELL_PIXELS, "cell-pixels" },
	{ FYPAL_TERM_WINDOW_PIXELS, "window-pixels" },
	{ FYPAL_TERM_SCHEME, "scheme" },
	{ FYPAL_TERM_FOCUS_EVENTS, "focus-events" },
	{ FYPAL_TERM_SGR_MOUSE, "sgr-mouse" },
	{ FYPAL_TERM_SGR_PIXEL_MOUSE, "sgr-pixel-mouse" },
	{ FYPAL_TERM_BRACKETED_PASTE, "bracketed-paste" },
	{ FYPAL_TERM_INBAND_RESIZE, "inband-resize" },
	{ FYPAL_TERM_CLIPBOARD, "clipboard" },
	{ FYPAL_TERM_OVERLINE, "overline" },
	{ FYPAL_TERM_NOTIFY, "notify" },
	{ FYPAL_TERM_NOTIFY_SOUND, "notify-sound" },
};

/* Each argument is a query to add, with \e for ESC. */
static void add_queries(struct fypal_probe *pr, int argc, char *argv[])
{
	char buf[256];
	const char *p;
	size_t n;
	int i;

	for (i = 1; i < argc; i++) {
		n = 0;
		for (p = argv[i]; *p && n < sizeof(buf); p++) {
			if (p[0] == '\\' && p[1] == 'e') {
				buf[n++] = '\033';
				p++;
			} else {
				buf[n++] = *p;
			}
		}
		if (fypal_probe_add_query(pr, buf, n))
			fprintf(stderr, "cannot add query %s\n", argv[i]);
	}
}

int main(int argc, char *argv[])
{
	struct fypal_probe *pr;
	const struct fypal_term *t;
	struct timespec t0, t1;
	char input[256];
	const char *u;
	size_t i, j, n;
	long ms;
	int rc;

	pr = fypal_probe_create();
	if (!pr) {
		fprintf(stderr, "cannot create a probe\n");
		return EXIT_FAILURE;
	}
	add_queries(pr, argc, argv);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	fypal_probe_run(pr, STDOUT_FILENO);
	t = fypal_probe_result(pr);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	ms = (t1.tv_sec - t0.tv_sec) * 1000 +
	     (t1.tv_nsec - t0.tv_nsec) / 1000000;

	printf("time: %ld ms\n", ms);
	printf("name: %s\n", t->name[0] ? t->name : "(unknown)");
	printf("da1-class: %u\n", t->da1_class);
	if (t->flags & FYPAL_TERM_DA2)
		printf("da2: type %u version %u\n", t->da2_type,
		       t->da2_version);
	if (t->flags & FYPAL_TERM_MODIFY_KEYS)
		printf("modify-other-keys: %u\n", t->modify_other_keys);
	printf("flags:");
	for (i = 0; i < sizeof(flags) / sizeof(flags[0]); i++)
		if (t->flags & flags[i].flag)
			printf(" %s", flags[i].name);
	printf("\n");
	if (t->flags & FYPAL_TERM_BACKGROUND)
		printf("background: #%06x\n", (unsigned int)t->background);
	if (t->flags & FYPAL_TERM_FOREGROUND)
		printf("foreground: #%06x\n", (unsigned int)t->foreground);
	if (t->flags & FYPAL_TERM_CELL_PIXELS)
		printf("cell: %ux%u px\n", t->cell_width, t->cell_height);
	if (t->flags & FYPAL_TERM_WINDOW_PIXELS)
		printf("window: %ux%u px\n", t->window_width, t->window_height);
	if (t->flags & FYPAL_TERM_SCHEME)
		printf("scheme: %s\n", t->flags & FYPAL_TERM_SCHEME_LIGHT ?
		       "light" : "dark");
	if (t->ansi_known) {
		printf("ansi:");
		for (i = 0; i < 16; i++)
			if (t->ansi_known & (1U << i))
				printf(" %zu=#%06x", i, (unsigned int)t->ansi[i]);
		printf("\n");
	}
	if (t->sixel_colors)
		printf("sixel-colors: %u\n", t->sixel_colors);
	for (i = 0; i < fypal_probe_unknown_count(pr); i++) {
		u = fypal_probe_unknown(pr, i, &n);
		printf("unknown reply:");
		for (j = 0; j < n; j++)
			printf((unsigned char)u[j] < 0x20 ? " ^%c" : "%c",
			       (unsigned char)u[j] < 0x20 ? u[j] + '@' : u[j]);
		printf("\n");
	}
	n = fypal_probe_take_input(pr, input, sizeof(input));
	if (n)
		printf("typed during the probe: %zu bytes\n", n);
	rc = t->flags & FYPAL_TERM_ANSWERED ? EXIT_SUCCESS : EXIT_FAILURE;
	fypal_probe_destroy(pr);
	return rc;
}
