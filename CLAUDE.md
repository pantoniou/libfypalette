# CLAUDE.md

Use this file when you change this repository.

## Project

libfypalette makes the colours of terminal renderers from themes. It is
part of the fyai family: libfyts, libfymd4c, libfymermaid, libfyvterm,
libfytimui and fyai use it so that they draw with one design language.

```sh
cmake -S . -B build -G Ninja
ninja -C build
ctest --test-dir build

cmake -S . -B build-asan -G Ninja -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
ninja -C build-asan
ctest --test-dir build-asan
```

`doc/themes.md` is the reference for the theme format and for colour
generation. Keep it correct when you change behaviour.

## The library holds no theme policy

This is the rule that the rest of the design follows.

- Put no colour, hue, role name or 16 colour mapping in C. A theme states
  them. Ember is data in `themes/ember.yaml`, not code.
- The C code supplies mechanism only: colour arithmetic, quantisation,
  capabilities, SGR output, and the evaluation of a theme.
- A choice that a theme could want to make differently belongs in the theme
  format. Examples: the 16 colour form of a colour, the palette of an
  emulated terminal, the attributes of a role.
- The xterm palette values, the SGR codes and the capability heuristics are
  facts about terminals, not policy. They stay in C.
- When the format gets a new statement, update `load_section()`, the
  API definition functions, `doc/themes.md`, and the tests together.

## Source layout

- `include/libfypalette.h`: the one public header.
- `src/fypal-color.c`: OKLab/OKLCH, gamut mapping, mixing, contrast, xterm
  and ANSI quantisation, literal colour parsing, SGR colour parameters.
- `src/fypal-theme.c`: the context, parameter and colour definitions, the
  expression evaluator, and YAML loading.
- `src/fypal-role.c`: roles, inheritance, lookup, and SGR on/off escapes.
- `src/fypal-glyph.c`: named glyphs with UTF-8 and ASCII forms.
- `src/fypal-hash.c`: the string hash for every name lookup.
- `src/fypal-term.c`: depth, capability and background detection.
- `src/fypal-internal.h`: internal structures.
- `themes/*.yaml`: built-in themes. CMake compiles each file into
  `fypal-themes.c`; the file name without `.yaml` is the theme name.
- `bin/fypalette-show.c`: draws a theme as bands, text, roles and a sample
  screen, for a person to check.
- `t/fypal-test.c`: the tests.

## Data model

- A theme is a libfyaml generic. Parse with `FYOPPF_COLLECT_DIAG` and report
  the parser diagnostic through `fypal_ctx_error()`, never to stderr.
- A definition copies the strings it keeps. The document and its builder are
  destroyed at the end of a load. Do not keep a generic or a cast pointer
  past the load.
- Prefer `fy_castp()` on the address of a generic to `fy_cast()` for strings.
  A short string can be stored in the generic word itself.
- `libfyaml.h` does not include the generic API. Include
  `<libfyaml/libfyaml-generic.h>` where generics are used.

## Lookups use hashes

- Find a parameter, a colour or a role by name through its `struct
  fypal_hash`, never through a loop over the arrays. The arrays keep the
  definition order for enumeration only.
- A role query (`fypal_ctx_role()`) caches its answer, a miss included, in
  `query_hash`. A new role must clear that cache, because it can answer a
  query that fell back before. Redefining an existing role does not change
  which role answers, so it does not clear it.
- A hash entry borrows its key from the value it names unless `own_key` is
  set. Insert only after the value is stored, and remove the value from its
  array again if the insert fails.
- A caller that asks for the same role often, such as a highlighter, keeps
  the `struct fypal_role` pointer. The pointer stays valid until the context
  is destroyed.

## Evaluation and invalidation

- `ctx->gen` changes on every change that affects output: a definition, a
  load, the variant, the capabilities. Call `fypal_ctx_changed_()` for a new
  kind of change.
- Colours are evaluated lazily for `gen` by `fypal_ctx_derive_()`. Role
  escapes are cached in the role for `gen`. Anything that reads a colour or an
  escape goes through those two.
- The evaluation state of a definition belongs to one pass, named by
  `eval_gen`. `BUSY` detects a cycle. Keep the evaluator free of recursion
  that a theme can make unbounded: cycles fail, and depth is limited.
- A load validates both variants before it returns. A theme that fails in
  one variant must not load.
- The first error is the cause. `fypal_ctx_error_set_()` keeps the first and
  ignores later ones. A public entry point clears the error before it works.
- An error message names where it came from: `source: section/group/name`,
  and the variant when evaluation failed.

## Escapes

- An off escape undoes exactly what its on escape set (`22`, `23`, `24`,
  `39`, `49`, `59`, ...). Never emit `0` from a role: a role must be able to
  close inside another role.
- Filter by the capabilities: remove attributes the terminal does not draw,
  degrade `undercurl` to `underline`, drop `ul` without underline colour and
  at 16 colours, and emit no colour at `FYPAL_DEPTH_NONE`.
- The 256 colour search covers entries 16-255 only.

## Tests

- Each test is a function in `t/fypal-test.c` and a name in the list in
  `t/CMakeLists.txt`, so CTest reports and runs it on its own.
- A test of an error checks the message text, not only the return value.
- Theme properties are tests too: Ember colours in gamut, a monotonic ramp,
  and 4.5:1 contrast of text colours on the ground in both variants. A theme
  change that breaks one is a defect in the theme.
- Run the normal and the ASAN suites before a commit.
- Check the output by eye with `fypalette-show` in both variants and at
  truecolor, 256 and 16 colours when you change colour generation,
  quantisation or a built-in theme.

## Exported symbols

The build uses `-fvisibility=hidden`. A public function has `FYPAL_EXPORT`
in the header. An internal function shared between source files ends in `_`
and is not exported. Only `fypal_` symbols leave the shared object:

```sh
nm -D --defined-only build/src/libfypalette.so | awk '$2 ~ /^[A-Z]$/ {print $3}' | grep -v '^fypal_'
```

The output must be empty.

## C style

- Linux kernel style: hard tabs, kernel braces, `lower_snake_case`.
- GNU C2x with `-Wall -Wextra -Wdeclaration-after-statement`. Declare local
  variables at the start of a function.
- Four spaces in CMake files.
- An SPDX header (MIT) in each new source file.
- Do not put an operation inside an error check; store the result and test
  it.
- Comments state an invariant, ownership, or a non-obvious reason, in
  concise technical English. Do not narrate history or restate the code.

## Documentation and commits

- Write documentation and commit messages in ASD-STE100 Simplified Technical
  English.
- Commit subject: imperative with a subsystem prefix, such as
  `theme: evaluate both variants on load`. Two or three short body lines on
  what changed and why, wrapped at 80 columns. End with:

  ```text
  Signed-off-by: Pantelis Antoniou <pantelis.antoniou@konsulko.com>
  ```

- One logical change per patch, in the order implementation, tests,
  documentation. Every patch builds. Fold a fix into the patch that
  introduced the defect.
