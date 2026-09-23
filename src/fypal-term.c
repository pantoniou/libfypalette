/*
 * fypal-term.c - colour depth, capabilities and the terminal probe
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <ctype.h>
#include <errno.h>
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

/* How long to wait for the DA1 reply; $FYPAL_PROBE_TIMEOUT_MS changes it. */
#define PROBE_TIMEOUT_MS	1000

/*
 * The queries, DA1 last: a terminal answers in order, so every reply that
 * comes before the DA1 reply belongs to this exchange. XTGETTCAP asks one
 * name per request, because xterm stops a request at the first unknown name.
 */
static const char probe_query[] =
	"\033]11;?\033\\"			/* background */
	"\033]10;?\033\\"			/* foreground */
	"\033[?2026$p"				/* DECRQM synchronized output */
	"\033[?2027$p"				/* DECRQM grapheme clusters */
	"\033[?2031$p"				/* DECRQM theme change reports */
	"\033[?u"				/* kitty keyboard flags */
	"\033[>0q"				/* XTVERSION */
	"\033_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\033\\"	/* kitty graphics */
	"\033[?1;1;0S"				/* XTSMGRAPHICS sixel colours */
	"\033[16t"				/* cell size in pixels */
	"\033[14t"				/* text area size in pixels */
	"\033[?996n"				/* light or dark scheme */
	"\033P+q524742\033\\"			/* XTGETTCAP RGB */
	"\033P+q5463\033\\"			/* XTGETTCAP Tc */
	"\033P+q536d756c78\033\\"		/* XTGETTCAP Smulx */
	"\033P+q5375\033\\"			/* XTGETTCAP Su */
	"\033P+q536d6f6c\033\\"		/* XTGETTCAP Smol */
	"\033P+q4d73\033\\"			/* XTGETTCAP Ms */
	"\033[>c"				/* DA2 */
	"\033[?1004$p"				/* DECRQM focus events */
	"\033[?1006$p"				/* DECRQM SGR mouse */
	"\033[?1016$p"				/* DECRQM SGR pixel mouse */
	"\033[?2004$p"				/* DECRQM bracketed paste */
	"\033[?2048$p"				/* DECRQM in-band resize */
	"\033[?4m"				/* XTQMODKEYS modifyOtherKeys */
	"\033]99;i=fypal:p=?;\033\\"		/* kitty notifications */
	/* the 16 ANSI colours, one query each */
	"\033]4;0;?\033\\" "\033]4;1;?\033\\" "\033]4;2;?\033\\" "\033]4;3;?\033\\"
	"\033]4;4;?\033\\" "\033]4;5;?\033\\" "\033]4;6;?\033\\" "\033]4;7;?\033\\"
	"\033]4;8;?\033\\" "\033]4;9;?\033\\" "\033]4;10;?\033\\" "\033]4;11;?\033\\"
	"\033]4;12;?\033\\" "\033]4;13;?\033\\" "\033]4;14;?\033\\" "\033]4;15;?\033\\";

/* DA1 goes last. Queries added by fypal_probe_add_query() go before it. */
static const char probe_da1[] = "\033[c";

/*
 * GNU screen answers DA1 itself, and prints a DCS string it does not know.
 * It passes the content of ESC P ... ESC \ to the outer terminal. So under
 * screen each query, and DA1, is wrapped: the outer terminal then sends all
 * the replies, in order. OSC queries end in BEL, because an ST would end the
 * wrapper. Only colours are asked for: screen itself does the drawing, so
 * the other features of the outer terminal do not apply.
 */
static const char probe_screen_query[] =
	"\033P\033]11;?\a\033\\" "\033P\033]10;?\a\033\\" "\033P\033[?996n\033\\"
	"\033P\033]4;0;?\a\033\\" "\033P\033]4;1;?\a\033\\" "\033P\033]4;2;?\a\033\\"
	"\033P\033]4;3;?\a\033\\" "\033P\033]4;4;?\a\033\\" "\033P\033]4;5;?\a\033\\"
	"\033P\033]4;6;?\a\033\\" "\033P\033]4;7;?\a\033\\" "\033P\033]4;8;?\a\033\\"
	"\033P\033]4;9;?\a\033\\" "\033P\033]4;10;?\a\033\\" "\033P\033]4;11;?\a\033\\"
	"\033P\033]4;12;?\a\033\\" "\033P\033]4;13;?\a\033\\" "\033P\033]4;14;?\a\033\\"
	"\033P\033]4;15;?\a\033\\";
static const char probe_screen_da1[] = "\033P\033[c\033\\";

enum probe_mux {
	MUX_NONE,
	MUX_SCREEN,
	MUX_TMUX,
};

/* tmux answers all queries itself. It can also set TERM=screen. */
static enum probe_mux probe_mux_detect(void)
{
	const char *env;

	env = getenv("TMUX");
	if (env && *env)
		return MUX_TMUX;
	env = getenv("STY");
	if (env && *env)
		return MUX_SCREEN;
	env = getenv("TERM");
	if (env && !strncmp(env, "screen", 6))
		return MUX_SCREEN;
	return MUX_NONE;
}

/* The longest reply that is kept whole; a longer one is input. */
#define PROBE_REPLY_MAX		512

/* One probe: its queries, its result, and the bytes it read. */
struct fypal_probe {
	struct fypal_term term;
	bool done;
	/* queries added with fypal_probe_add_query() */
	char *extra;
	size_t extra_len;
	/* replies that were not parsed, one after another; ends[i] is the end
	 * of reply i */
	char *unknown;
	size_t unknown_len;
	size_t *unknown_ends;
	size_t unknown_count;
	/* keys typed during the probe; input_off is how much was taken */
	char *input;
	size_t input_len;
	size_t input_off;
};

/* One component of an OSC colour answer, 1 to 4 hex digits, scaled to 8 bits. */
static int osc_component(const char *s, const char *e)
{
	unsigned long v = 0;
	int digits = 0;

	for (; s < e && isxdigit((unsigned char)*s); s++, digits++) {
		v <<= 4;
		v |= (unsigned long)(isdigit((unsigned char)*s) ? *s - '0' :
				     tolower((unsigned char)*s) - 'a' + 10);
	}
	if (!digits || digits > 4 || s != e)
		return -1;
	while (digits < 4) {
		v = (v << 4) | (v & 0xf);
		digits++;
	}
	return (int)(v >> 8);
}

/* Parse "rgb:R/G/B" in [s, e). Returns 0 with the colour in *rgbp, or -1. */
static int osc_rgb(const char *s, const char *e, uint32_t *rgbp)
{
	const char *stop;
	int rgb[3], i;

	if (e - s < 4 || memcmp(s, "rgb:", 4))
		return -1;
	s += 4;
	for (i = 0; i < 3; i++) {
		stop = i < 2 ? memchr(s, '/', (size_t)(e - s)) : e;
		if (!stop)
			return -1;
		rgb[i] = osc_component(s, stop);
		if (rgb[i] < 0)
			return -1;
		s = stop + 1;
	}
	*rgbp = ((uint32_t)rgb[0] << 16) | ((uint32_t)rgb[1] << 8) |
		(uint32_t)rgb[2];
	return 0;
}

enum reply {
	REPLY_INPUT,		/* not a reply: typed input */
	REPLY_PARTIAL,		/* the start of a sequence */
	REPLY_OTHER,		/* a reply of this exchange */
	REPLY_UNKNOWN,		/* a reply form that nothing here parses */
	REPLY_DA1,		/* the DA1 reply */
};

/* Up to max numeric parameters separated by ';' in [s, e). Returns the count. */
static int csi_params(const char *s, const char *e, unsigned int *v, int max)
{
	int n = 0;

	if (s >= e)
		return 0;
	for (;;) {
		if (n >= max)
			return n;
		v[n] = 0;
		while (s < e && isdigit((unsigned char)*s)) {
			if (v[n] < 100000000U)
				v[n] = v[n] * 10 + (unsigned int)(*s - '0');
			s++;
		}
		n++;
		if (s >= e || *s != ';')
			return n;
		s++;
	}
}

static bool starts(const char *s, const char *e, const char *prefix)
{
	size_t n = strlen(prefix);

	return (size_t)(e - s) >= n && !strncasecmp(s, prefix, n);
}

/* OSC 4: "N;rgb:R/G/B" in [s, e), one colour of the ANSI palette. */
static bool probe_ansi(const char *s, const char *e, struct fypal_term *term)
{
	unsigned int index = 0;
	uint32_t rgb;

	if (s >= e || !isdigit((unsigned char)*s))
		return false;
	while (s < e && isdigit((unsigned char)*s) && index < 256)
		index = index * 10 + (unsigned int)(*s++ - '0');
	if (s >= e || *s != ';')
		return false;
	if (index < 16 && !osc_rgb(s + 1, e, &rgb)) {
		term->ansi[index] = rgb;
		term->ansi_known |= 1U << index;
	}
	return true;
}

/*
 * The reply to the OSC 99 query: "i=fypal:p=?;key=value:key=value" in
 * [s, e). The key s lists the sounds a notification can play. Any value
 * other than "none" means notifications can play a sound.
 */
static bool probe_notify(const char *s, const char *e, struct fypal_term *term)
{
	const char *keys, *v, *stop;

	keys = memchr(s, ';', (size_t)(e - s));
	if (!starts(s, e, "i=fypal") || !keys)
		return false;
	term->flags |= FYPAL_TERM_NOTIFY;
	for (v = keys + 1; v < e; v = stop + 1) {
		stop = memchr(v, ':', (size_t)(e - v));
		if (!stop)
			stop = e;
		if (starts(v, stop, "s=") && stop - v > 2 &&
		    !(stop - v == 6 && !strncmp(v + 2, "none", 4)))
			term->flags |= FYPAL_TERM_NOTIFY_SOUND;
	}
	return true;
}

/* An OSC, DCS or APC body in [body, end): whether it is a reply. */
static bool probe_string(char intro, const char *body, const char *end,
			 struct fypal_term *term)
{
	uint32_t rgb;
	size_t n;

	switch (intro) {
	case ']':
		if (starts(body, end, "4;"))
			return probe_ansi(body + 2, end, term);
		if (starts(body, end, "99;"))
			return probe_notify(body + 3, end, term);
		if (!starts(body, end, "11;") && !starts(body, end, "10;"))
			return false;
		if (osc_rgb(body + 3, end, &rgb))
			return true;
		if (body[1] == '1') {
			term->background = rgb;
			term->flags |= FYPAL_TERM_BACKGROUND;
		} else {
			term->foreground = rgb;
			term->flags |= FYPAL_TERM_FOREGROUND;
		}
		return true;
	case 'P':
		if (starts(body, end, ">|")) {
			n = (size_t)(end - body - 2);
			if (n >= sizeof(term->name))
				n = sizeof(term->name) - 1;
			memcpy(term->name, body + 2, n);
			term->name[n] = '\0';
			return true;
		}
		if (starts(body, end, "0+r"))
			return true;
		if (!starts(body, end, "1+r"))
			return false;
		body += 3;
		if (starts(body, end, "524742") || starts(body, end, "5463"))
			term->flags |= FYPAL_TERM_TRUECOLOR;
		else if (starts(body, end, "536d756c78") ||
			 starts(body, end, "5375"))
			term->flags |= FYPAL_TERM_STYLED_UL;
		else if (starts(body, end, "536d6f6c"))
			term->flags |= FYPAL_TERM_OVERLINE;
		else if (starts(body, end, "4d73"))
			term->flags |= FYPAL_TERM_CLIPBOARD;
		else
			return false;
		return true;
	case '_':
		if (!starts(body, end, "Gi=31;"))
			return false;
		if (starts(body + 6, end, "OK"))
			term->flags |= FYPAL_TERM_KITTY_GRAPHICS;
		return true;
	}
	return false;
}

/* A CSI reply: private marker, parameters in [p, e), and the final byte. */
static enum reply probe_csi(char mark, const char *p, const char *e,
			    char final, struct fypal_term *term)
{
	unsigned int v[16];
	int n, i;

	n = csi_params(p, e, v, 16);
	if (mark == '?' && final == 'c') {
		if (n)
			term->da1_class = v[0];
		for (i = 1; i < n; i++)
			if (v[i] == 4)
				term->flags |= FYPAL_TERM_SIXEL;
		return REPLY_DA1;
	}
	if (mark == '?' && final == 'u') {
		term->flags |= FYPAL_TERM_KITTY_KEYS;
		return REPLY_OTHER;
	}
	/* DECRPM: CSI ? mode ; Ps $ y, where Ps 0 is an unknown mode */
	if (mark == '?' && final == 'y' && e > p && e[-1] == '$' && n == 2) {
		if (v[1] >= 1 && v[1] <= 4) {
			if (v[0] == 2026)
				term->flags |= FYPAL_TERM_SYNC;
			else if (v[0] == 2027)
				/* Ps 1 and 3 are set, 2 and 4 reset */
				term->flags |= FYPAL_TERM_GRAPHEMES |
					       (v[1] & 1 ? FYPAL_TERM_GRAPHEMES_SET : 0);
			else if (v[0] == 2031)
				term->flags |= FYPAL_TERM_THEME_REPORT;
			else if (v[0] == 1004)
				term->flags |= FYPAL_TERM_FOCUS_EVENTS;
			else if (v[0] == 1006)
				term->flags |= FYPAL_TERM_SGR_MOUSE;
			else if (v[0] == 1016)
				term->flags |= FYPAL_TERM_SGR_PIXEL_MOUSE;
			else if (v[0] == 2004)
				term->flags |= FYPAL_TERM_BRACKETED_PASTE;
			else if (v[0] == 2048)
				term->flags |= FYPAL_TERM_INBAND_RESIZE;
		}
		return REPLY_OTHER;
	}
	/* DA2: CSI > type ; version ; ROM c */
	if (mark == '>' && final == 'c') {
		if (n >= 2) {
			term->da2_type = v[0];
			term->da2_version = v[1];
			term->flags |= FYPAL_TERM_DA2;
		}
		return REPLY_OTHER;
	}
	/* XTQMODKEYS: CSI > 4 ; level m */
	if (mark == '>' && final == 'm' && n >= 1 && v[0] == 4) {
		term->modify_other_keys = n >= 2 ? v[1] : 0;
		term->flags |= FYPAL_TERM_MODIFY_KEYS;
		return REPLY_OTHER;
	}
	/* the scheme: CSI ? 997 ; 1 n dark, 2 light */
	if (mark == '?' && final == 'n' && n == 2 && v[0] == 997) {
		if (v[1] == 1 || v[1] == 2)
			term->flags |= FYPAL_TERM_SCHEME |
				       (v[1] == 2 ? FYPAL_TERM_SCHEME_LIGHT : 0);
		return REPLY_OTHER;
	}
	/* XTSMGRAPHICS: CSI ? item ; status ; value S */
	if (mark == '?' && final == 'S' && n >= 2) {
		if (v[0] == 1 && v[1] == 0 && n >= 3)
			term->sixel_colors = v[2];
		return REPLY_OTHER;
	}
	if (!mark && final == 't' && n == 3 && (v[0] == 6 || v[0] == 4)) {
		if (v[0] == 6) {
			term->cell_height = v[1];
			term->cell_width = v[2];
			term->flags |= FYPAL_TERM_CELL_PIXELS;
		} else {
			term->window_height = v[1];
			term->window_width = v[2];
			term->flags |= FYPAL_TERM_WINDOW_PIXELS;
		}
		return REPLY_OTHER;
	}
	return REPLY_INPUT;
}

/* Whether the CSI in buf[0, final) has an intermediate byte, 0x20-0x2f. */
static bool csi_intermediate(const char *buf, size_t final)
{
	size_t i;

	for (i = 2; i < final; i++)
		if ((unsigned char)buf[i] >= 0x20 && (unsigned char)buf[i] <= 0x2f)
			return true;
	return false;
}

/*
 * Classify the sequence at buf[0] of len bytes and set *used to its length.
 * A reply that this exchange asked for is kept in *term. Only a complete
 * reply form is a reply: a key such as ESC or an arrow is input.
 */
static enum reply probe_classify(const char *buf, size_t len, size_t *used,
				 struct fypal_term *term)
{
	enum reply r;
	size_t i;
	char mark = 0, final;

	*used = 1;
	if (buf[0] != '\033')
		return REPLY_INPUT;
	if (len < 2)
		return REPLY_PARTIAL;
	if (buf[1] == ']' || buf[1] == 'P' || buf[1] == '_') {
		for (i = 2; i < len; i++) {
			if ((buf[i] == '\a' && buf[1] == ']') ||
			    (buf[i] == '\033' && i + 1 < len && buf[i + 1] == '\\'))
				break;
		}
		if (i >= len)
			return len < PROBE_REPLY_MAX ? REPLY_PARTIAL : REPLY_INPUT;
		*used = i + (buf[i] == '\a' ? 1 : 2);
		/* no key produces a string, so this is a reply */
		return probe_string(buf[1], buf + 2, buf + i, term) ?
		       REPLY_OTHER : REPLY_UNKNOWN;
	}
	if (buf[1] != '[')
		return REPLY_INPUT;
	/* CSI: parameter bytes, intermediate bytes, one final byte */
	for (i = 2; i < len && (unsigned char)buf[i] >= 0x20 &&
		    (unsigned char)buf[i] <= 0x3f; i++)
		;
	if (i >= len)
		return len < PROBE_REPLY_MAX ? REPLY_PARTIAL : REPLY_INPUT;
	final = buf[i];
	if ((unsigned char)final < 0x40 || (unsigned char)final > 0x7e)
		return REPLY_INPUT;
	if (i > 2 && strchr("<=>?", buf[2]))
		mark = buf[2];
	r = probe_csi(mark, buf + 2 + (mark ? 1 : 0), buf + i, final, term);
	/* no key has a private marker or an intermediate byte */
	if (r == REPLY_INPUT &&
	    (mark || csi_intermediate(buf, i)))
		r = REPLY_UNKNOWN;
	if (r != REPLY_INPUT)
		*used = i + 1;
	return r;
}

/* Keep one reply that was not parsed, for fypal_probe_unknown(). */
static void probe_unknown_add(struct fypal_probe *pr, const char *buf,
			      size_t len)
{
	size_t *ends;
	char *p;

	/* if an allocation fails, the reply is dropped */
	p = realloc(pr->unknown, pr->unknown_len + len);
	if (!p)
		return;
	pr->unknown = p;
	ends = realloc(pr->unknown_ends,
		       (pr->unknown_count + 1) * sizeof(*ends));
	if (!ends)
		return;
	pr->unknown_ends = ends;
	memcpy(pr->unknown + pr->unknown_len, buf, len);
	pr->unknown_len += len;
	pr->unknown_ends[pr->unknown_count++] = pr->unknown_len;
}

static void probe_input_add(struct fypal_probe *pr, const char *buf,
			    size_t len)
{
	char *p;

	if (!len)
		return;
	p = realloc(pr->input, pr->input_len + len);
	/* if the allocation fails, these typed keys are lost */
	if (!p)
		return;
	memcpy(p + pr->input_len, buf, len);
	pr->input = p;
	pr->input_len += len;
}

/*
 * Take the replies out of buf; keep everything else as input. Returns the
 * count of bytes consumed: a partial sequence at the end stays for the next
 * read unless final is set. *da1 is set when the DA1 reply was seen, and the
 * bytes after it are input.
 */
static size_t probe_scan(struct fypal_probe *pr, const char *buf, size_t len,
			 bool final, bool *da1)
{
	size_t i = 0, start = 0, used;
	enum reply r;

	while (i < len && !*da1) {
		r = probe_classify(buf + i, len - i, &used, &pr->term);
		if (r == REPLY_PARTIAL) {
			if (!final)
				break;
			r = REPLY_INPUT;
			used = 1;
		}
		if (r == REPLY_INPUT) {
			i += used;
			continue;
		}
		probe_input_add(pr, buf + start, i - start);
		if (r == REPLY_UNKNOWN)
			probe_unknown_add(pr, buf + i, used);
		i += used;
		start = i;
		if (r == REPLY_DA1) {
			*da1 = true;
			i = len;
		}
	}
	probe_input_add(pr, buf + start, i - start);
	return i;
}

static long probe_timeout_ms(void)
{
	const char *env = getenv("FYPAL_PROBE_TIMEOUT_MS");
	char *end;
	long v;

	if (!env || !*env)
		return PROBE_TIMEOUT_MS;
	v = strtol(env, &end, 10);
	if (*end || v <= 0 || v > 3600 * 1000)
		return PROBE_TIMEOUT_MS;
	return v;
}

#ifdef FYPAL_HAVE_TTY

static long elapsed_ms(const struct timespec *t0)
{
	struct timespec t1;

	clock_gettime(CLOCK_MONOTONIC, &t1);
	return (t1.tv_sec - t0->tv_sec) * 1000 +
	       (t1.tv_nsec - t0->tv_nsec) / 1000000;
}

/*
 * Write all of buf before the time limit. EINTR retries; EAGAIN, from a
 * descriptor in non-blocking mode, waits until the terminal takes more.
 */
static bool probe_write(int tty, const char *buf, size_t len,
			const struct timespec *t0, long timeout)
{
	struct pollfd pfd;
	ssize_t n;
	long remain;
	int rc;

	while (len) {
		n = write(tty, buf, len);
		if (n > 0) {
			buf += n;
			len -= (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
			return false;
		remain = timeout - elapsed_ms(t0);
		if (remain <= 0)
			return false;
		pfd.fd = tty;
		pfd.events = POLLOUT;
		rc = poll(&pfd, 1, (int)remain);
		if (rc < 0 && errno != EINTR)
			return false;
	}
	return true;
}

/*
 * Send the queries to the controlling terminal, not to fd, so that they
 * never go into redirected output. Read until the DA1 reply or the time
 * limit. Echo is off while reading, so typed keys are not shown. The
 * terminal mode is restored on every path.
 */
static void probe_run(struct fypal_probe *pr, int fd)
{
	struct fypal_term *term = &pr->term;
	struct termios saved, raw;
	struct timespec t0;
	struct pollfd pfd;
	char buf[4 * PROBE_REPLY_MAX];
	const char *env;
	size_t used = 0, done;
	ssize_t n;
	long remain, timeout;
	enum probe_mux mux;
	int tty, flags, rc;
	bool own_tty = true, da1 = false, ok;

	mux = probe_mux_detect();
	env = getenv("TERM");
	if (env && !strcmp(env, "dumb"))
		return;
	tty = open("/dev/tty", O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
	if (tty >= 0) {
		flags = fcntl(tty, F_GETFL);
		if (flags >= 0)
			fcntl(tty, F_SETFL, flags & ~O_NONBLOCK);
	} else {
		if (fd < 0 || !isatty(fd))
			return;
		tty = fd;
		own_tty = false;
	}
	/* a background process group stops on the mode change and the read */
	if (!isatty(tty) || tcgetpgrp(tty) != getpgrp() ||
	    tcgetattr(tty, &saved))
		goto out_close;

	raw = saved;
	raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(tty, TCSANOW, &raw))
		goto out_close;
	/* one time limit for writing the queries and reading the replies */
	timeout = probe_timeout_ms();
	clock_gettime(CLOCK_MONOTONIC, &t0);
	if (mux == MUX_SCREEN) {
		ok = probe_write(tty, probe_screen_query,
				 sizeof(probe_screen_query) - 1, &t0, timeout) &&
		     (!pr->extra_len ||
		      (probe_write(tty, "\033P", 2, &t0, timeout) &&
		       probe_write(tty, pr->extra, pr->extra_len, &t0,
				   timeout) &&
		       probe_write(tty, "\033\\", 2, &t0, timeout))) &&
		     probe_write(tty, probe_screen_da1,
				 sizeof(probe_screen_da1) - 1, &t0, timeout);
	} else {
		ok = probe_write(tty, probe_query, sizeof(probe_query) - 1,
				 &t0, timeout) &&
		     probe_write(tty, pr->extra, pr->extra_len, &t0,
				 timeout) &&
		     probe_write(tty, probe_da1, sizeof(probe_da1) - 1, &t0,
				 timeout);
	}
	if (!ok)
		goto out_restore;
	if (mux != MUX_NONE)
		term->flags |= FYPAL_TERM_MULTIPLEXER;
	term->flags |= FYPAL_TERM_PROBED;

	while (!da1) {
		remain = timeout - elapsed_ms(&t0);
		if (remain <= 0)
			break;
		pfd.fd = tty;
		pfd.events = POLLIN;
		rc = poll(&pfd, 1, (int)remain);
		if (rc < 0 && errno == EINTR)
			continue;
		if (rc <= 0)
			break;
		/* a read of 0 after the mode switch is not the end of the answer */
		n = read(tty, buf + used, sizeof(buf) - used);
		if (n < 0 && (errno == EINTR || errno == EAGAIN ||
			      errno == EWOULDBLOCK))
			continue;
		if (n < 0)
			break;
		used += (size_t)n;
		done = probe_scan(pr, buf, used, used == sizeof(buf), &da1);
		memmove(buf, buf + done, used - done);
		used -= done;
	}
	probe_scan(pr, buf, used, true, &da1);
	if (da1)
		term->flags |= FYPAL_TERM_ANSWERED;
	if (mux == MUX_SCREEN && !term->name[0])
		strcpy(term->name, "screen");

out_restore:
	tcsetattr(tty, TCSANOW, &saved);
out_close:
	if (own_tty)
		close(tty);
}

#else

static void probe_run(struct fypal_probe *pr, int fd)
{
	(void)pr;
	(void)fd;
}

#endif

struct fypal_probe *fypal_probe_create(void)
{
	return calloc(1, sizeof(struct fypal_probe));
}

void fypal_probe_destroy(struct fypal_probe *pr)
{
	if (!pr)
		return;
	free(pr->extra);
	free(pr->unknown);
	free(pr->unknown_ends);
	free(pr->input);
	free(pr);
}

int fypal_probe_add_query(struct fypal_probe *pr, const char *seq, size_t len)
{
	char *p;

	if (!pr || pr->done || !seq || !len || pr->extra_len + len > 4096)
		return -1;
	p = realloc(pr->extra, pr->extra_len + len);
	if (!p)
		return -1;
	memcpy(p + pr->extra_len, seq, len);
	pr->extra = p;
	pr->extra_len += len;
	return 0;
}

int fypal_probe_run(struct fypal_probe *pr, int fd)
{
	if (!pr || pr->done)
		return -1;
	pr->done = true;
	probe_run(pr, fd);
	return 0;
}

const struct fypal_term *fypal_probe_result(const struct fypal_probe *pr)
{
	return pr ? &pr->term : NULL;
}

size_t fypal_probe_take_input(struct fypal_probe *pr, char *buf, size_t size)
{
	size_t n;

	if (!pr || !buf)
		return 0;
	n = pr->input_len - pr->input_off;
	if (n > size)
		n = size;
	/* with no keys, pr->input is NULL, which memcpy() must not get */
	if (!n)
		return 0;
	memcpy(buf, pr->input + pr->input_off, n);
	pr->input_off += n;
	return n;
}

size_t fypal_probe_unknown_count(const struct fypal_probe *pr)
{
	return pr ? pr->unknown_count : 0;
}

const char *fypal_probe_unknown(const struct fypal_probe *pr, size_t index,
				size_t *len)
{
	size_t start;

	if (!pr || index >= pr->unknown_count)
		return NULL;
	start = index ? pr->unknown_ends[index - 1] : 0;
	if (len)
		*len = pr->unknown_ends[index] - start;
	return pr->unknown + start;
}

/* Probe once for a caller that has no probe of its own. */
static bool probe_once(int fd, struct fypal_term *term)
{
	struct fypal_probe *pr;

	pr = fypal_probe_create();
	if (!pr)
		return false;
	fypal_probe_run(pr, fd);
	*term = pr->term;
	fypal_probe_destroy(pr);
	return true;
}

/* Light or dark from a background colour. */
static int variant_from_rgb(uint32_t rgb)
{
	int r, g, b;

	r = (int)(rgb >> 16) & 0xff;
	g = (int)(rgb >> 8) & 0xff;
	b = (int)rgb & 0xff;
	/* Rec. 601 luma; halfway divides a light terminal from a dark one */
	return (r * 299 + g * 587 + b * 114) / 1000 >= 128 ?
	       FYPAL_VARIANT_LIGHT : FYPAL_VARIANT_DARK;
}

bool fypal_detect_background(int fd, uint32_t *rgb)
{
	struct fypal_term term;

	if (!rgb || !probe_once(fd, &term) ||
	    !(term.flags & FYPAL_TERM_BACKGROUND))
		return false;
	*rgb = term.background;
	return true;
}

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

enum fypal_variant fypal_term_variant(const struct fypal_term *term,
				      bool *known)
{
	int v = -1;

	/* the terminal's own report wins over $COLORFGBG, which can be stale */
	if (term && (term->flags & FYPAL_TERM_SCHEME))
		v = term->flags & FYPAL_TERM_SCHEME_LIGHT ?
		    FYPAL_VARIANT_LIGHT : FYPAL_VARIANT_DARK;
	if (v < 0)
		v = variant_from_colorfgbg();
	if (v < 0 && term && (term->flags & FYPAL_TERM_BACKGROUND))
		v = variant_from_rgb(term->background);
	if (known)
		*known = v >= 0;
	return v == FYPAL_VARIANT_LIGHT ? FYPAL_VARIANT_LIGHT : FYPAL_VARIANT_DARK;
}

enum fypal_variant fypal_detect_variant(int fd, bool *known)
{
	struct fypal_term term;

	if (!probe_once(fd, &term))
		memset(&term, 0, sizeof(term));
	return fypal_term_variant(&term, known);
}
