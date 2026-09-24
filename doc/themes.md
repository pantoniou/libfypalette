# Themes and colour generation

This document describes how libfypalette themes work, how a theme generates
its colours, and how the colours degrade to the capabilities of a terminal.

## 1. The model

The library has no theme policy. It supplies four things:

- colour arithmetic in OKLab and OKLCH;
- quantisation to the xterm 256 and ANSI 16 colour palettes;
- terminal capabilities and SGR escape output; and
- an evaluator for themes.

A **theme** is a YAML document. It defines numbers and strings (parameters), colours
that are expressions over the parameters, and named roles that give a style
to what a renderer draws. The built-in themes are the files in `themes/`.
The build compiles them into the library as data.

A **context** (`struct fypal_ctx`) holds the definitions of one or more
loaded themes, the active variant (dark or light), and the capabilities of
one output. A renderer asks the context for a role by name and receives the
escapes that turn the role on and off.

```text
theme YAML ──load──▶ definitions ──evaluate(variant)──▶ colours (sRGB)
                                                          │
                     role query ──resolve(inheritance)────┤
                                                          ▼
                                    caps ──quantise──▶ SGR on / off
```

## 2. Document structure

```yaml
fypalette: 1            # format version (optional; must be 1)
name: ember             # informational
description: ...        # informational

params:     {NAME: EXPR | STRING | {string: STRING}, ...}
colors:     {NAME: CEXPR, ...}
ansi16:     {NAME: 0-15 | default | none, ...}
terminal16: {0-15: CEXPR, ...}

dark:                   # overrides for the dark variant
  params: ...
  colors: ...
  ansi16: ...
  terminal16: ...
light:                  # overrides for the light variant
  ...

roles: {...}            # see section 6
glyphs: {...}           # see section 6.5
ground: {l: NAME, c: NAME, h: NAME}   # see section 5.5
```

An unknown key is an error. This catches a misspelt section, such as
`colours:`.

### 2.1 Sections

The top level is the `all` section. The `dark` and `light` mappings are
variant sections. They can contain `params`, `colors`, `ansi16` and
`terminal16`, but not `roles`.

When the context evaluates a name for a variant, it uses the definition in
the section of that variant. If that section does not define the name, it
uses the definition in `all`. Thus a theme defines a colour one time, in
`all`, as a formula, and each variant supplies different numbers for the
formula.

### 2.2 Several documents

`fypal_ctx_load()` adds to the definitions that the context already has. A
later definition of a name replaces the earlier one in the same section. An
application can thus load a built-in theme, then a user theme that changes
some parameters, then API overrides.

## 3. Parameters

Most parameters are numbers. Their values are YAML numbers or expression strings:

```yaml
params:
  ring.l: 0.76
  bright.l: ring.l + 0.08
```

A symbolic scalar made of letters, digits, dots, underscores and hyphens is a
string when none of its hyphen-separated names refers to a parameter. For
example, `none`, `top` and `bubble-rule-faint` are strings. A reference to a
defined parameter, including a forward reference, remains a numeric expression.
Use `{string: VALUE}` to state a string explicitly, including values with spaces
or values that also name a parameter. API expression definitions remain numeric;
use `fypal_ctx_set_param_string()` for API string definitions.

The expression grammar:

```text
EXPR   := TERM (('+' | '-') TERM)*
TERM   := FACTOR (('*' | '/') FACTOR)*
FACTOR := NUMBER | NAME | '(' EXPR ')' | '-' FACTOR | '+' FACTOR
NAME   := [A-Za-z_][A-Za-z0-9_.]*
```

A name can contain dots. A dot has no meaning; it only groups names for the
reader. A name cannot contain `-`, because `-` is subtraction.

## 4. Colours

A colour is an expression that gives an sRGB colour:

```text
CEXPR := '#rgb' | '#rrggbb'
       | NAME                            another colour
       | 'oklch(' EXPR, EXPR, EXPR ')'   lightness, chroma, hue in degrees
       | 'rgb(' EXPR, EXPR, EXPR ')'     0-255 per channel, clamped
       | 'mix(' CEXPR, CEXPR, EXPR ')'   OKLab interpolation, 0 to 1
       | 'xterm(' EXPR ')'               an xterm palette entry, 0-255
```

The commas between arguments are optional. Use them: without commas,
`oklch(0.5 -0.1 30)` is read as `oklch(0.5 - 0.1, 30)` and fails.

### 4.1 OKLCH

OKLCH is the polar form of OKLab. It has three axes:

- **L**, lightness from 0 (black) to 1 (white). Equal steps of L look like
  equal steps of lightness.
- **C**, chroma, the distance from grey. 0 is grey. Most sRGB colours are
  below 0.32.
- **h**, the hue angle in degrees.

Hues with the same L and C have approximately the same perceived lightness
and saturation. RGB and HSL do not have this property: in HSL, a yellow and a
blue at the same "lightness" look very different. A theme that changes only h
thus gets colours that are equally loud.

The conversion is Björn Ottosson's OKLab with the sRGB transfer function.
`fypal_rgb_to_lab()` and `fypal_lab_to_rgb()` are the forward and inverse
conversions.

### 4.2 Gamut mapping

Some OKLCH colours are outside sRGB. For example, a saturated cyan at
L 0.70 needs a negative red channel. `oklch()` then keeps L and h and
decreases C until the colour fits. The search is a bisection on C with
32 steps; a linear channel within 1e-5 of the range counts as inside.

This mapping changes chroma only. The colour keeps its lightness and its
hue, which are the two properties that a theme uses to give meaning.

### 4.3 Mixing

`mix(a, b, t)` interpolates in OKLab: `a + (b - a) * t` for each of L, a and b.
An OKLab mix does not go through the grey or dark intermediate colours that an
sRGB mix of two hues goes through.

### 4.4 Evaluation

The context evaluates a name only when it needs the value. Thus the order of
definitions in the document has no effect. A load evaluates every parameter
and colour for both variants, so a theme with an error does not load. The
errors are:

| cause | example message |
|---|---|
| unknown name | `ember: colors/gold: undefined parameter 'ring.x' (dark variant)` |
| cycle | `test: params/a: parameter 'b' depends on itself (dark variant)` |
| syntax | `test: colors/x: expected ')' at "" (dark variant)` |
| missing variant | `test: colour 'x' is not defined (light variant)` |
| YAML | `test:3:7: <parser message>` |

The first error is the cause and `fypal_ctx_error()` returns it. Later errors
follow from it and are not recorded.

## 5. How Ember generates its colours

Ember (`themes/ember.yaml`) has a small set of parameters. Every colour is a
formula over them.

### 5.1 The neutral ramp

```yaml
ground: oklch(l.ground, ramp.chroma, ramp.hue)
...
ink:    oklch(l.ink, ramp.chroma, ramp.hue)
```

Six tokens share one hue (`ramp.hue`, 85, a warm yellow) and one very low
chroma (`ramp.chroma`, 0.008). Only the lightness changes:

| token | dark L | light L | job |
|---|---|---|---|
| ground | 0.16 | 0.98 | the background |
| raise | 0.20 | 0.95 | fenced blocks, node fills |
| card | 0.24 | 0.90 | user message cards |
| rule | 0.26 | 0.88 | borders, table grid |
| faint | 0.48 | 0.62 | what the reader may skip |
| dim | 0.68 | 0.44 | working text: arguments, output |
| ink | 0.92 | 0.21 | what the reader must read |

The light variant inverts the order of lightness and keeps the hue and the
chroma. Every step is written from `l.ground`, such as
`l.raise: l.ground + 0.04`, so a different ground moves the whole ramp and
keeps the steps between the tokens.

### 5.2 The hue ring

```yaml
gold: oklch(ring.l, ring.c, hue.gold)
```

The six identity hues use the same L and C (`ring.l`, `ring.c`). Only the
angle changes, so no hue is louder than another. Each hue has one job:

| hue | angle | job |
|---|---|---|
| gold | 65 | fyai itself: tool calls, the prompt |
| blue | 250 | the user, and what the user acts on |
| violet | 295 | reasoning |
| cyan | 190 | references: paths, symbols, links |
| green | 145 | added, passed |
| coral | 25 | removed, failed |

### 5.3 Consequence, syntax and washes

- `green_strong` and `coral_strong` use `strong.l` and `strong.c`: less
  lightness and more chroma than the ring. Saturation is the signal that the
  result matters.
- The syntax tokens use `syntax.l` and `syntax.c`, one chroma step below the
  ring. Code is thus quieter than the transcript around it. Each syntax token
  takes the angle of a ring hue: keywords violet, types cyan, strings green,
  numbers gold, functions blue, preprocessor coral.
- `wash_add` and `wash_del` mix `wash` (6% dark, 4% light) of a consequence
  colour into the ground. A diff row takes the wash as its background, so its
  tokens keep their syntax colours.
- `wash_focus` mixes `wash.focus` (14% dark, 6% light) of blue into the
  ground. Blue is what you act on, and the wash is the ground of what holds the
  keys: the prompt, a picker and a focused tile take the same colour. Every
  colour that carries text reads on it at 4.5:1. At 16 colours it has no form,
  so a renderer marks focus in another way.

### 5.4 Changing Ember

A change to one parameter changes every colour that depends on it:

| change | effect |
|---|---|
| `ramp.hue: 250` | a cool, blue-grey neutral ramp |
| `ramp.chroma: 0` | pure greys |
| `dark/params/ring.c: 0.06` | quieter identity hues in dark mode only |
| `hue.cyan: 210` | references move toward blue, in text and in code types |
| `dark/params/l.ground: 0.10` | a darker ground; the other ramp steps stay |

To see a change without writing a file:

```sh
fypalette-show -p ramp.hue=250 -p ring.c=0.06 --sample
```

### 5.5 The ground of a terminal

A terminal draws its own background, and a theme cannot know it. The
`ground` key names the parameters that hold the OKLCH lightness, chroma and
hue of the background of the theme:

```yaml
ground: {l: l.ground, c: ramp.chroma, h: ramp.hue}
```

A host that knows the background of its terminal gives it to
`fypal_ctx_set_ground()`. The library sets the three parameters, in the
section of the active variant, to the lightness, chroma and hue of that
colour; a grey gets the hue 0. `fypal_detect_background()` gives the
background colour that the terminal probe read (section 7.1).

The library then does nothing more: the theme decides what follows the
ground. Ember writes its neutral ramp from the ground, so the ground becomes
the colour of the terminal, and raise, rule, faint, dim and ink keep their
steps above (dark) or below (light) it. The hue ring, the consequence
colours and the syntax colours do not use the ground and do not change. A
theme without `ground` refuses the call.

`ground` names parameters only: it is in the `all` section, and a variant
section cannot redefine it. `fypal_ctx_define_ground()` names them through
the API.

To see a theme over a background without changing a terminal:

```sh
fypalette-show --dark --ground '#1e1e2e' --sample
fypalette-show --ground terminal --sample
```

## 6. Roles

A role is a style with a name. It has these fields:

| field | value |
|---|---|
| `fg`, `bg`, `ul` | a colour name, `default` (the terminal colour), or a literal colour (`#rrggbb`, `oklch(...)`) |
| `attrs` | a list or a blank-separated string of `bold`, `dim`, `italic`, `underline`, `undercurl`, `blink`, `reverse`, `strike`, `overline`; a leading `-` removes an inherited attribute |
| `base` | the role to inherit from, in place of the dotted parent |

A colour name in a role can refer to a colour that a later document defines.
A load fails if the name is still not defined when the context evaluates it.

### 6.1 The role tree

In a mapping under `roles:`, the keys `fg`, `bg`, `ul`, `attrs` and `base`
are the fields of that node. Every other key is a child role, and its name is
the name of the node, a dot, and the key:

```yaml
roles:
  md:                       # a group: no fields, not a role
    heading:                # md.heading
      fg: ink
      attrs: [bold]
      2: {fg: gold}         # md.heading.2
    link:                   # md.link
      fg: cyan
      ul: rule
      attrs: [underline]
      url: {fg: faint, attrs: [-underline]}   # md.link.url
  code.keyword: {fg: syn_keyword}             # a dotted key works too
  tool.name: 'fg=gold bold'                   # the text form of the fields
```

Because the field names are reserved, a child role cannot be named `fg`, `bg`,
`ul`, `attrs` or `base`.

### 6.2 Inheritance

A role inherits every field that it does not set from its parent. The parent
is the role that `base` names. Without `base`, the parent is the nearest
defined dotted ancestor: the parent of `md.task.done.text` is `md.task.done`
if that role exists, else `md.task`, else `md`.

In the tree above, `md.heading.2` sets `fg: gold` and inherits `bold` from
`md.heading`. `md.link.url` sets `fg: faint`, inherits `ul: rule` from
`md.link`, and removes the inherited underline.

A chain of `base` references that makes a cycle stops at a depth of 16.

### 6.3 Lookup

`fypal_ctx_role(ctx, name)` returns the role for the name. If the name is not
defined, it returns the nearest defined dotted ancestor. If no ancestor is
defined, it returns NULL. A renderer thus asks for the most precise name it
knows, and a theme defines only the precision that it needs:

| query | Ember answers with |
|---|---|
| `md.heading.2` | `md.heading.2` |
| `md.heading.5` | `md.heading` |
| `code.keyword.control.return` | `code.keyword` |
| `code.label` | `code` |
| `nothing.here` | NULL |

The context keeps every name in a hash table. It also keeps the answer to
each query, a miss included, in a query cache. A repeated query is one hash
lookup. A definition of a new role clears the query cache, because the new
role can answer a query that fell back before.

### 6.4 Names for renderers

These prefixes are the convention of the fyai renderers:

| prefix | renderer |
|---|---|
| `text`, `chrome` | plain text levels |
| `user`, `fyai`, `reasoning`, `ref`, `ok`, `fail`, `system`, `prompt` | identity |
| `tool.*`, `notice.*` | transcript fragments |
| `md.*` | Markdown elements |
| `code.<capture>` | syntax highlighting: `<capture>` is the tree-sitter capture name, for example `code.keyword.control` |
| `diff.*` | diff rows and line numbers |
| `mermaid.*` | diagrams |
| `pane.*`, `tile.*` | the work pane; `pane.focus` is the ground of what holds the keys, and `pane.edge` marks its edge |

Tree-sitter capture names are already dot-separated hierarchies
(`keyword.control.conditional`), so the lookup fallback maps a precise capture
to the most precise role that the theme defines. A theme does not need a table
of regular expressions.

### 6.5 Glyphs

A theme also names the glyphs that its renderers draw: the gutter marks of a
transcript, the list bullet, the task marks, the frames of an indicator. Each
glyph has a UTF-8 form and an ASCII form. A renderer uses the ASCII form when
the terminal or the user does not accept the UTF-8 form.

```yaml
glyphs:
  gutter:
    tool: {utf: "→", ascii: "->"}   # both forms
    ask: "?"                        # one string is both forms
  tool:
    pending:
      utf: "→"
      ascii: "->"
      1: " "                        # a child: tool.pending.1
```

The glyph tree has the same shape as the role tree. A key other than `utf` and
`ascii` is a child glyph, and the name of a child extends the name of its parent
with a dot. A glyph with no `ascii` form uses its `utf` form for both.

`fypal_ctx_glyph(ctx, name, ascii)` returns a glyph. An undefined name returns
its nearest defined ancestor, as a role lookup does, and a name with no defined
ancestor returns `NULL`. A sequence of glyphs, such as the frames of an
indicator, uses numbered children. The first undefined child returns the same
string as the parent, so a renderer stops there.

An application gives a glyph a column count that does not change with the form.
Ember keeps the gutter three columns wide with the `gutter.cols` parameter, and
the ASCII form of a gutter mark is never wider than that.

A theme sets fenced-code layout with the string parameter `md.code.rules`:

```yaml
params:
  md.code.rules: bubble-raise-faint # Ember's default
  # md.code.rules: none            # no rule rows
  # md.code.rules: top             # rule above only
  # md.code.rules: both            # rules above and below
  # md.code.rules: top-bottom      # alias for both
```

`bubble-<bg>-<legend>` fills the fence with the named palette colour `<bg>`
and draws its uppercase `── LANGUAGE ──` label using `<legend>` (ASCII:
`-- LANGUAGE --`). A blank bubble row separates the label from the code.
Ember uses `bubble-raise-faint`: `raise` behind the code and `faint` for the label.
For example, `bubble-rule-faint`
uses `rule` as the background and `faint` for the label. Content keeps its
syntax foreground colours. Bubble rows extend to the rendering width and have
no full-width rule rows. A missing parameter keeps the renderer's own decorations;
numeric `0` and `1` remain aliases for `none` and `both`.

`fypal_ctx_param_string()` returns the borrowed value for the active variant,
or `NULL` for a numeric or absent parameter. `fypal_ctx_set_param_string()`
defines a string through the API, including variant overrides. Numeric lookups
and expressions cannot use string values.

## 7. Terminal output

### 7.1 Capabilities

`struct fypal_caps` describes one output:

- `depth`: none, 16, 256 or truecolor;
- `attrs`: the attributes that the terminal draws; and
- `underline_color`: true if the terminal accepts SGR 58.

`fypal_caps_detect()` fills the structure from the environment. It reads
`NO_COLOR`, `CLICOLOR_FORCE`, `COLORTERM`, `TERM` and `TERM_PROGRAM`, and
checks that the descriptor is a terminal. The Linux console gets no italic,
dim or strike. Terminals that are known to draw a curly or coloured underline
get `undercurl` and `underline_color`. A terminal multiplexer gets neither.

`fypal_term_variant()` gives the variant for a probe result: the scheme that
the terminal reported, then `COLORFGBG`, then the background colour. The
terminal's own report is exact. `COLORFGBG` can come from a different
terminal, and deciding light or dark from a mid-grey background is a guess.
`fypal_detect_variant()` and `fypal_detect_background()` probe the terminal
on each call; keys typed during that probe are lost.

#### The terminal probe

A `struct fypal_probe` asks the terminal what it supports. The caller makes
it with `fypal_probe_create()`, runs it one time with `fypal_probe_run()`,
reads the `struct fypal_term` from `fypal_probe_result()`, and destroys it
with `fypal_probe_destroy()`. The library keeps no state of its own. The
probe sends all its queries in one write, with DA1 last:

| Query | Result |
|---|---|
| OSC 11, OSC 10 | background and foreground colours |
| OSC 4 `N ; ?`, N 0 to 15 | the 16 ANSI colours |
| `CSI ? 996 n` | light or dark scheme |
| DECRQM 2026 | synchronized output |
| DECRQM 2027 | grapheme clusters, and whether the mode is on |
| DECRQM 2031 | light/dark change reports |
| DECRQM 1004, 1006, 1016, 2004, 2048 | focus events, SGR mouse, SGR pixel mouse, bracketed paste, resize reports |
| `CSI ? u` | kitty keyboard protocol |
| XTQMODKEYS `CSI ? 4 m` | modifyOtherKeys level |
| XTVERSION `CSI > 0 q` | terminal name and version |
| DA2 `CSI > c` | terminal type and version |
| APC `G` query | kitty graphics protocol |
| XTSMGRAPHICS `CSI ? 1 ; 1 ; 0 S` | number of sixel colour registers |
| `CSI 16 t`, `CSI 14 t` | cell size and text area size in pixels |
| XTGETTCAP `RGB`, `Tc` | truecolor |
| XTGETTCAP `Smulx`, `Su` | styled underline |
| XTGETTCAP `Smol` | overline |
| XTGETTCAP `Ms` | OSC 52 clipboard |
| OSC 99 query | kitty notifications, and whether they can play a sound |
| DA1 `CSI c` | terminal class; attribute 4 means sixel |

A terminal answers queries in order, and every terminal answers DA1. So when
the DA1 reply arrives, all the other replies have arrived too, and a query
with no reply is not supported. The probe does not decide anything from how
long a reply takes.

The probe waits for the DA1 reply for at most 1000 ms.
`$FYPAL_PROBE_TIMEOUT_MS` changes this limit. A test suite on a slow machine
can set a long limit: a terminal that answers ends the probe at once. If the
DA1 reply does not arrive, `FYPAL_TERM_ANSWERED` is clear and the result may
be incomplete.

The probe sends nothing if there is no terminal, if `TERM` is `dumb`, or if
the process is in a background process group. Echo is off during the probe.
Bytes that arrive and are not replies are keys that the user typed;
`fypal_probe_take_input()` returns them, and the program must read them
before it reads the terminal.

A process whose terminal was already probed by another process, such as a
child that draws on a terminal emulated by its parent, uses the result of
that probe and does not probe again.

#### Terminal multiplexers

tmux answers all the queries itself, in order. Its answers are the right
ones, because tmux does the drawing.

GNU screen answers DA1 itself but passes OSC queries to the outer terminal.
The outer terminal's replies then arrive after screen's DA1 reply, too late.
Under screen (`$STY` is set, or `TERM` starts with `screen` and `$TMUX` is
not set), the probe sends only the colour and scheme queries, and DA1, each
wrapped in `ESC P ... ESC \` so that screen passes it to the outer terminal.
OSC queries end in BEL inside the wrapper. No XTGETTCAP query is sent,
because screen prints it.

`FYPAL_TERM_MULTIPLEXER` is set under screen and under tmux.

#### Your own queries

`fypal_probe_add_query()` adds a query to the probe. Added queries are sent
after the built-in ones and before DA1. The library keeps each reply it does
not parse, for `fypal_probe_unknown()`, if no key can produce it: an OSC, DCS
or APC string, or a CSI sequence with a private marker or an intermediate
byte. The program decides what the reply means. A reply that is a plain CSI
sequence looks like a key and is treated as typed input.

#### Checking a terminal

`fypalette-probe` prints the probe result for the terminal it runs in. Each
argument is an extra query, with `\e` for ESC; the tool also prints the
replies it did not parse. To record the raw replies, run it under
`script --log-in FILE`.

#### Sound

No query asks a terminal whether it can play sound. BEL works everywhere.
kitty's OSC 99 notifications can play a sound, and the probe reports this
(`FYPAL_TERM_NOTIFY_SOUND`). Other terminals have sound sequences, such as
DECPS, that cannot be queried; use the name from XTVERSION to decide.

### 7.2 Escapes

`fypal_role_on()` and `fypal_role_off()` return two escapes. The off escape
turns off exactly what the on escape turned on: `22` for bold or dim, `23` for
italic, `24` for underline, `39`, `49` and `59` for the colours. It does not
use `0`, so a role can close inside another role and keep the outer
attributes that it did not change.

The context keeps both escapes in the role. A change to the context (a load,
a definition, the variant, or the capabilities) makes them stale, and the next
query makes them again.

### 7.3 Degradation

| depth | a colour becomes |
|---|---|
| truecolor | `38;2;r;g;b` |
| 256 | `38;5;n`: the nearest of entries 16-255 by OKLab distance |
| 16 | the `ansi16` value of the colour; without one, the nearest of the 16 xterm colours |
| none | nothing |

The 256 colour search skips entries 0-15. A terminal theme changes those
entries, so their real values are not known.

The 16 colour form is policy, so the theme states it in `ansi16`. Ember maps
each hue to its bright ANSI colour on a dark ground and to its normal colour
on a light ground. Green and coral use the normal pair on both, because
without the chroma step they must stay different from the bright hues.
`default` gives `39` or `49`; `none` gives no escape, which is right for a
background that has no 16 colour form, such as a diff wash.

An attribute that the terminal does not draw is removed. `undercurl` becomes
`underline` on a terminal without the curl. `ul` is removed without
`underline_color`, and at 16 colours.

### 7.4 An emulated terminal

`terminal16` gives the 16 colour palette of a terminal that the application
emulates, such as a shell session in a tile. A program in that terminal that
uses ANSI colours then draws with the colours of the theme.
`fypal_ctx_terminal16()` returns false if the theme leaves a slot undefined.

## 8. Using a context

```c
struct fypal_caps caps;
struct fypal_ctx *ctx;

fypal_caps_detect(STDOUT_FILENO, &caps);
ctx = fypal_ctx_create(&caps);
fypal_ctx_set_variant(ctx, fypal_detect_variant(STDOUT_FILENO, NULL));
if (fypal_ctx_load_builtin(ctx, "ember") ||
    fypal_ctx_load_file(ctx, user_theme_path)) {
	fprintf(stderr, "%s\n", fypal_ctx_error(ctx));
	...
}

printf("%s%s%s\n", fypal_ctx_on(ctx, "md.heading.2"), "Plan",
       fypal_ctx_off(ctx, "md.heading.2"));

fypal_ctx_destroy(ctx);
```

A renderer that draws cells instead of escapes uses `fypal_ctx_resolve()`,
which gives the colours as sRGB values.

## 9. fypalette-show

`fypalette-show` draws a theme so that a person can check it:

| option | output |
|---|---|
| `--bands` | a band per colour with its hex, L, C, h and xterm index |
| `--text` | each colour as text on `ground` and on `raise`, with the contrast ratio |
| `--roles` | each role with a sample and its escape |
| `--sample` | a transcript, code, a diff, a table, a diagram and the work pane, made of roles |
| `--sgr ROLE [--off]` | the escape of one role, for scripts |

`-l`/`-d` select the variant and `-D` the depth. `-p`, `-c` and `-r` override
a parameter, a colour or a role for the active variant. With no selection the
tool draws everything.
