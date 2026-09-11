/*
 * fypal-term.c - colour depth and background detection
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#define FYPAL_HAVE_TTY 1
#endif

#include "libfypalette.h"

#define N_ELEMENTS(a)	(sizeof(a) / sizeof((a)[0]))

/* How long a terminal has to answer a query, in milliseconds. */
#define QUERY_TIMEOUT_MS	100

/* TERM prefixes of terminals that take 24 bit colour. */
static const char *const truecolor_terms[] = {
	"xterm-kitty", "xterm-ghostty", "ghostty", "wezterm", "alacritty",
	"foot", "contour", "rio", "vte", "konsole", "iterm", "kitty",
};

/* Variables that only a 24 bit terminal sets. */
static const char *const truecolor_env[] = {
	"KITTY_WINDOW_ID", "GHOSTTY_RESOURCES_DIR", "GHOSTTY_BIN_DIR",
	"WEZTERM_EXECUTABLE", "WEZTERM_PANE", "ALACRITTY_WINDOW_ID",
	"ALACRITTY_SOCKET", "KONSOLE_VERSION", "ITERM_SESSION_ID",
	"WT_SESSION", "VSCODE_INJECTION",
};

/* TERM_PROGRAM values of 24 bit terminals. */
static const char *const truecolor_programs[] = {
	"iTerm.app", "WezTerm", "ghostty", "vscode", "Hyper", "Tabby",
	"rio", "warp", "WarpTerminal",
};

static bool env_any_set(const char *const *names, size_t count)
{
	const char *v;
	size_t i;

	for (i = 0; i < count; i++) {
		v = getenv(names[i]);
		if (v && *v)
			return true;
	}
	return false;
}

static bool str_in(const char *value, const char *const *list, size_t count,
		   bool prefix)
{
	size_t i;

	if (!value || !*value)
		return false;
	for (i = 0; i < count; i++) {
		if (prefix ? !strncasecmp(value, list[i], strlen(list[i])) :
			     !strcasecmp(value, list[i]))
			return true;
	}
	return false;
}

/* TERM prefixes of terminals that draw a curly and a coloured underline. */
static const char *const styled_underline_terms[] = {
	"xterm-kitty", "kitty", "xterm-ghostty", "ghostty", "wezterm", "foot",
	"contour",
};

static const char *const styled_underline_env[] = {
	"KITTY_WINDOW_ID", "GHOSTTY_RESOURCES_DIR", "WEZTERM_PANE",
};

static const char *const styled_underline_programs[] = {
	"WezTerm", "ghostty", "iTerm.app",
};

enum fypal_depth fypal_detect_depth(int fd)
{
	const char *env;

	/* https://no-color.org: any non-empty value disables colour */
	env = getenv("NO_COLOR");
	if (env && *env)
		return FYPAL_DEPTH_NONE;

	env = getenv("CLICOLOR_FORCE");
	if (!(env && *env && strcmp(env, "0")) && fd >= 0 && !isatty(fd))
		return FYPAL_DEPTH_NONE;

	env = getenv("COLORTERM");
	if (env && (!strcmp(env, "truecolor") || !strcmp(env, "24bit")))
		return FYPAL_DEPTH_TRUECOLOR;

	env = getenv("TERM");
	if (env && (!*env || !strcmp(env, "dumb")))
		return FYPAL_DEPTH_NONE;
	/* a terminfo name ending in -direct is the 24 bit form */
	if (env && (strstr(env, "-direct") || strstr(env, "truecolor")))
		return FYPAL_DEPTH_TRUECOLOR;
	if (str_in(env, truecolor_terms, N_ELEMENTS(truecolor_terms), true))
		return FYPAL_DEPTH_TRUECOLOR;
	if (str_in(getenv("TERM_PROGRAM"), truecolor_programs,
		   N_ELEMENTS(truecolor_programs), false))
		return FYPAL_DEPTH_TRUECOLOR;
	if (env_any_set(truecolor_env, N_ELEMENTS(truecolor_env)))
		return FYPAL_DEPTH_TRUECOLOR;

	if (!env || !*env)
		return FYPAL_DEPTH_NONE;
	if (strstr(env, "256"))
		return FYPAL_DEPTH_256;
	return FYPAL_DEPTH_16;
}

/*
 * $COLORFGBG is "<fg>;<bg>", sometimes with a field between them. The last
 * field is the background as an ANSI index; 7 and 9 up are light.
 */
static int variant_from_colorfgbg(void)
{
	const char *env = getenv("COLORFGBG");
	const char *last;
	char *end;
	long bg;

	if (!env || !*env)
		return -1;
	last = strrchr(env, ';');
	last = last ? last + 1 : env;
	if (!strcasecmp(last, "default"))
		return FYPAL_VARIANT_DARK;
	bg = strtol(last, &end, 10);
	if (end == last || *end)
		return -1;
	return (bg == 7 || bg >= 9) ? FYPAL_VARIANT_LIGHT : FYPAL_VARIANT_DARK;
}

#ifdef FYPAL_HAVE_TTY

/* One component of an OSC 11 answer, 1 to 4 hex digits, scaled to 8 bits. */
static int osc_component(const char *s, const char *e)
{
	unsigned long v = 0;
	int digits = 0;

	for (; s < e && isxdigit((unsigned char)*s); s++, digits++) {
		v <<= 4;
		v |= (unsigned long)(isdigit((unsigned char)*s) ? *s - '0' :
				     tolower((unsigned char)*s) - 'a' + 10);
	}
	if (!digits || digits > 4)
		return -1;
	while (digits < 4) {
		v = (v << 4) | (v & 0xf);
		digits++;
	}
	return (int)(v >> 8);
}

static long elapsed_ms(const struct timespec *t0)
{
	struct timespec t1;

	clock_gettime(CLOCK_MONOTONIC, &t1);
	return (t1.tv_sec - t0->tv_sec) * 1000 +
	       (t1.tv_nsec - t0->tv_nsec) / 1000000;
}

/*
 * Ask the terminal for its background with OSC 11; the answer is
 * "ESC ] 11 ; rgb:RRRR/GGGG/BBBB" ended by BEL or ST. The query goes to the
 * controlling terminal so that redirected output never carries it, the read
 * has a deadline so that a silent terminal cannot hang the caller, and the
 * terminal mode is restored on every path.
 */
static int variant_from_query(int fd)
{
	static const char query[] = "\033]11;?\033\\";
	struct termios saved, raw;
	struct timespec t0;
	struct pollfd pfd;
	char buf[128];
	const char *p, *e, *slash, *stop;
	size_t used = 0;
	ssize_t n;
	long remain;
	int tty, flags, rgb[3], i, out = -1;
	bool own_tty = true;

	tty = open("/dev/tty", O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
	if (tty >= 0) {
		flags = fcntl(tty, F_GETFL);
		if (flags >= 0)
			fcntl(tty, F_SETFL, flags & ~O_NONBLOCK);
	} else {
		if (fd < 0 || !isatty(fd))
			return -1;
		tty = fd;
		own_tty = false;
	}
	if (!isatty(tty) || tcgetattr(tty, &saved))
		goto out_close;

	raw = saved;
	raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(tty, TCSANOW, &raw))
		goto out_close;
	if (write(tty, query, sizeof(query) - 1) != (ssize_t)(sizeof(query) - 1))
		goto out_restore;

	/* a read of 0 after the mode switch is not the end of the answer */
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (;;) {
		remain = QUERY_TIMEOUT_MS - elapsed_ms(&t0);
		if (remain <= 0)
			break;
		pfd.fd = tty;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, (int)remain) <= 0)
			break;
		n = read(tty, buf + used, sizeof(buf) - 1 - used);
		if (n < 0)
			break;
		used += (size_t)n;
		buf[used] = '\0';
		if (memchr(buf, '\a', used) || strstr(buf, "\033\\") ||
		    used + 1 >= sizeof(buf))
			break;
	}
	if (!used)
		goto out_restore;

	p = strstr(buf, "rgb:");
	if (!p)
		goto out_restore;
	p += 4;
	e = buf + used;
	for (i = 0; i < 3; i++) {
		slash = memchr(p, '/', (size_t)(e - p));
		stop = slash ? slash : e;
		/* the last component ends at the terminator */
		if (!slash) {
			for (stop = p; stop < e && isxdigit((unsigned char)*stop); stop++)
				;
		}
		rgb[i] = osc_component(p, stop);
		if (rgb[i] < 0 || (!slash && i < 2))
			goto out_restore;
		if (slash)
			p = slash + 1;
	}
	/* Rec. 601 luma; halfway divides a light terminal from a dark one */
	out = (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) / 1000 >= 128 ?
	      FYPAL_VARIANT_LIGHT : FYPAL_VARIANT_DARK;

out_restore:
	tcsetattr(tty, TCSANOW, &saved);
out_close:
	if (own_tty)
		close(tty);
	return out;
}

#else

static int variant_from_query(int fd)
{
	(void)fd;
	return -1;
}

#endif

void fypal_caps_detect(int fd, struct fypal_caps *caps)
{
	const char *term = getenv("TERM");
	bool styled;

	caps->depth = fypal_detect_depth(fd);
	caps->attrs = 0;
	caps->underline_color = false;
	if (caps->depth == FYPAL_DEPTH_NONE)
		return;

	/* the Linux console has no italic, no dim and no strike */
	if (term && !strcmp(term, "linux")) {
		caps->attrs = FYPAL_ATTR_BOLD | FYPAL_ATTR_UNDERLINE |
			      FYPAL_ATTR_BLINK | FYPAL_ATTR_REVERSE;
		return;
	}
	caps->attrs = FYPAL_ATTR_ALL & ~FYPAL_ATTR_UNDERCURL;

	/* a multiplexer drops what the outer terminal could draw */
	if (term && (!strncmp(term, "screen", 6) || !strncmp(term, "tmux", 4)))
		return;
	styled = str_in(term, styled_underline_terms,
			N_ELEMENTS(styled_underline_terms), true) ||
		 str_in(getenv("TERM_PROGRAM"), styled_underline_programs,
			N_ELEMENTS(styled_underline_programs), false) ||
		 env_any_set(styled_underline_env,
			     N_ELEMENTS(styled_underline_env));
	if (styled) {
		caps->attrs |= FYPAL_ATTR_UNDERCURL;
		caps->underline_color = true;
	}
}

enum fypal_variant fypal_detect_variant(int fd, bool *known)
{
	int v;

	v = variant_from_colorfgbg();
	if (v < 0)
		v = variant_from_query(fd);
	if (known)
		*known = v >= 0;
	return v == FYPAL_VARIANT_LIGHT ? FYPAL_VARIANT_LIGHT : FYPAL_VARIANT_DARK;
}
