# libfypalette

Programmable colour themes for terminal renderers.

libfypalette has no theme policy of its own. It supplies:

- OKLab and OKLCH colour arithmetic, with gamut mapping to sRGB;
- quantisation to the xterm 256 and ANSI 16 colour palettes;
- detection of terminal capabilities and background; and
- an evaluator for YAML themes: parameters, colour expressions, per-variant
  sections, a tree of named roles that give SGR escapes, and a tree of named
  glyphs with UTF-8 and ASCII forms.

The built-in theme is Ember, the display design language of fyai. Its colours
are formulas over a small set of parameters; see `themes/ember.yaml`.

`doc/themes.md` describes the theme format and how colours are generated.

## Build

```sh
cmake -S . -B build -G Ninja
ninja -C build
ctest --test-dir build
```

libfypalette requires libfyaml.

## Try a theme

```sh
./build/bin/fypalette-show                    # everything, detected variant and depth
./build/bin/fypalette-show -l --sample        # the light variant
./build/bin/fypalette-show -D 256 --bands     # as a 256 colour terminal sees it
./build/bin/fypalette-show -p ramp.hue=250 -s # a cool ground
./build/bin/fypalette-probe                   # what this terminal answers
```
