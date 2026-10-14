# NeoGrace

*English · [Русский](README.ru.md)*

A modern editor for **Grace (.agr)** plots: a replacement for xmgrace/QtGrace that runs
natively on Apple Silicon Macs (arm64), with no Rosetta needed. Written in C++17 and Qt 6.
The interface is available in English and Russian.

![](packaging/NeoGrace.png)

## Features

**Files**
- Opens and saves `.agr` files, plus the older `.par`/`.xmgr` formats. Commands that
  NeoGrace does not understand (regions, `stack world`, `timestamp`, `link`…) are kept
  as they are, so a file saved by NeoGrace still opens in xmgrace with nothing lost.
- Reads `.xvg` (GROMACS) files and text tables (`.dat`, `.txt`, `.csv`, `.tsv`). Columns
  can be separated by spaces, tabs, commas or `;`. Blocks are separated by `&`, and column
  headers are recognized.
- Exports PDF and SVG (vector, with fonts kept as text) and PNG, JPEG or TIFF at any dpi,
  optionally with a transparent background. Also prints, and has **Copy as Image** for
  pasting into Keynote or Word.
- Batch mode without a window, like `gracebat`:
  `NeoGrace --export figure.pdf project.agr` (with `--dpi 600`, `--transparent`).

**Rendering.** Sizes are taken from the Grace source code (fonts, symbols, ticks, line
widths, legend and label layout), so projects look the same as in xmgrace, but with
antialiasing, modern fonts and a real minus sign “−” instead of a hyphen.
- XY, Chart (side-by-side and stacked bars), Polar, Pie and Fixed graphs. Several graphs
  per page, and insets.
- All set types: xy, xydx, xydy, xydxdx, xydydy, xydxdy, xydxdxdydy, bar, bardy, bardydy,
  xyhilo, xyz, xyr, xysize, xycolor, xycolpat, xyvmap, xyboxplot, xystring.
- Symbols, 8 line styles, step lines and fills (to a baseline or as a polygon), with
  patterns. Error bars (including arrow-clipped ones), drop lines and value labels.
- Scales: linear, logarithmic, reciprocal (1/x) and logit. Inverted axes, top and right
  axes, axes through zero, and special ticks with your own labels.
- Tick label formats: general, decimal, power (10ⁿ), scientific, engineering (k/M/µ),
  computing (K/M), dates and degrees. Tick labels can also be recalculated with a formula
  (`$t - 273.15`).
- Grace text markup: `\S \s \N`, `\x` (Greek letters), `\f{…}`, `\+ \-`, `\u \o`,
  `\R{color}`, `\#{code}`. Greek letters can also be typed directly from the keyboard.
- Text labels, arrows, rectangles and ellipses, placed in page or graph coordinates.

**Editing**
- **Canvas.** Select by clicking. Drag the legend, text labels and shapes. Move and
  resize graphs with handles. Zoom to a rectangle (click: ×2, ⌥-click: ÷2), pan with the
  hand tool, and zoom with ⌘ + scroll wheel or a trackpad pinch.
- **Project panel.** Lists graphs, sets and objects, with visibility checkboxes and
  context menus.
- **Group editing.** Select several sets (⌘- or ⇧-click on the graph or in the project
  panel; ⌘A selects all sets of a graph). Color, line width, symbols, fill and any other
  property in the inspector then change for all of them at once, as one undo step.
  **Distinct colors in order** gives the selected sets easy-to-tell-apart colors.
- **Double-click** any element (set, axis, title, legend, text label) to open its
  properties, with the cursor in the main field.
- **Fill under a curve.** Use **Quick → Fill under curve** or the context menu. The fill
  goes down to the X axis (y = 0) or to the bottom or top edge of the graph, in a light
  shade of the line color.
- **Layer order.**
  - Sets, graphs and text labels move forward or backward (⌘] ⌘[), or to the front or
    back (⇧⌘] ⇧⌘[).
  - Use **Edit → Layer Order**, the context menu, or drag and drop in the project panel.
    Dragging also moves sets into another graph.
  - As in Grace, the order is defined by set numbers, so files stay compatible with
    xmgrace.
- **Interface in English and Russian.** Switch with **View → Язык / Language**; the default
  is the system language. The dictionary is `tools/i18n_en.json`, and `tools/gen_i18n.py`
  checks that every string is translated.
- **Property inspector** for the page, each graph, each axis, set, legend and object:
  every Grace setting, grouped into sections.
- **Data table.** Edit cells, add or delete rows, paste from Excel or Numbers.
- **Full undo history** (⌘Z / ⇧⌘Z). A whole drag counts as one undo step.
- **10 plot styles:** Modern, For papers, Classic xmgrace, Journal (like Nature or Science),
  Presentation (large type, thick lines), Dark, Black & White, Grid, Minimal, Soft
  Background.
  - Black & White is for print: curves differ by line style and symbols instead of
    colors.
  - **Style Gallery…** shows your own project in every style; click one to apply it. Open
    it from the toolbar, the **Graph** menu, or the page inspector.
- **Palettes:** Modern, Classic Grace, Okabe–Ito (colorblind-safe), Bright (for dark
  backgrounds), Pastel and Deep. **Color by Gradient** (viridis, warm or blue) colors the
  selected sets along a scale, which works well for a series of curves.
- Arrange graphs in a grid, and fit margins to labels.

**Data analysis** (**Data** menu)
- **Set from Text…** (⌥⌘N).
  - Each line is a point: `20.0 -10.0` and `20.0 10.0` give a line segment.
  - A blank line or `&` starts a new set. A third column is an error bar or another Y.
  - A line such as `y = 0.5*x` or `y = sin(x)` adds a function over the visible X range.
    You can change the range and the number of points.
  - Numbers copied from Excel or a text file can be pasted onto the canvas as a new set
    (⌘V).
  - To edit an existing set this way, use **Edit Data as Text…** in its context menu.
- **Formula of sets** (⇧⌘M, **Data → Set Operations…**). Creates a new set from any
  expression of other sets:
  - Examples: `s1.y + s2.y + s3.y`, `s0 - s1 - s2`, `(s0 + s1) / 2`,
    `s0.y - 0.5*s1.y + x`, and `g1.s0` for a set in another graph.
  - `sN` with no column means `sN.y`, and you can type × ÷ −.
  - The window starts with the sum of the selected sets already filled in. Buttons
    insert set references and operators, and **Sum / Difference / Mean of selected** write
    the formula for you.
  - If the sets have different X points, they are resampled onto the X points of the
    first set in the formula (or of a set you choose). Points outside the common range
    are skipped.
- **Ready-made operations** (same window, or **Operations** in a set's context menu):
  - A + B, A − B, A × B, A ÷ B, A ^ B, where B is another set or a number. A number can
    apply to Y or to X, for example to convert units.
  - Sum, and mean ± σ, of several selected sets.
  - −y, |y|, 1/y, normalize to the maximum or to the area, subtract the minimum, swap X
    and Y.
  - If the X points of two sets differ, the second set is linearly interpolated onto the
    first set's X. Y error bars are propagated.
  - The result is a new set (labelled like “S0 + S1”), or the source sets are changed,
    including all selected sets at once. The window shows what you will get before you run
    it.
- **Set formulas:** `y = y*2; x = x/1000`. They can reference other sets (`s1.y`) and use
  functions such as `sin exp ln log10 sqrt erf gamma j0 rnorm…`.
- **New set from a formula.**
- **Regression:** linear, polynomials up to degree 10, exponential, power and logarithmic,
  with R² and the fitted equation.
- **Nonlinear curve fitting** to your own formula with parameters `a0…a9`
  (Levenberg–Marquardt). Templates include Gaussian, Lorentzian, exponential and logistic
  curves.
- Moving average, derivative, integral, Fourier spectrum, histogram, interpolation (linear
  and spline), sorting and set statistics.

## Building on a Mac (Apple Silicon)

You need the Xcode Command Line Tools (`xcode-select --install`) and
[Homebrew](https://brew.sh).

```bash
./packaging/build_macos.sh
```

The script installs Qt 6 and CMake with Homebrew, builds NeoGrace for arm64, runs the
tests, and creates `dist/NeoGrace.app` and `dist/NeoGrace.dmg`. Open the dmg and drag
NeoGrace to Applications.

The first time you launch it, right-click NeoGrace and choose **Open**. The app is signed
locally, without a Developer ID, so macOS asks once.

Once installed, NeoGrace opens `.agr` files when you double-click them in Finder.

**Without building it yourself.** The repository includes a GitHub Actions workflow
(`.github/workflows/build-macos.yml`). Push the folder to GitHub and every commit builds
`NeoGrace.dmg` for Apple Silicon (Actions tab → Artifacts). A tag such as `v1.0.0` also
creates a release.

### Manually

```bash
brew install qt cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j
open build/NeoGrace.app
```

### Linux

```bash
sudo apt install qt6-base-dev qt6-svg-dev cmake g++    # Ubuntu 24.04
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/NeoGrace
```

## Keyboard shortcuts

| Keys | Action |
|---|---|
| ⌘O / ⌘S / ⇧⌘S | open / save / save as |
| ⌘I | import data |
| ⌥⌘N / ⌘V on the canvas | set from text / paste numbers from the clipboard as a set |
| ⇧⌘M | set operations and formula of sets |
| ⌘E | export image |
| ⌘Z / ⇧⌘Z | undo / redo |
| ⌘D / ⌫ | duplicate / delete selection |
| ⌘-click, ⇧-click / ⌘A | add a set to the selection / select all sets of a graph |
| ⌘] ⌘[ / ⇧⌘] ⇧⌘[ | layer forward / backward, to front / to back |
| ⌘T | data table |
| ⇧⌘A | autoscale |
| ⌘F / ⌘K | set formula / nonlinear curve fitting |
| V Z H | select / zoom to rectangle / pan |
| T L R O | text / line / rectangle / ellipse |
| ⌘+ ⌘− ⌘0 | zoom the canvas / fit to window |
| arrow keys (⇧) | nudge the selected object, legend or graph |
| double-click | element properties |

## Project layout

```
src/core   — no UI: project model, .agr reading and writing, text markup, scales and
             ticks, QPainter rendering, formulas, set operations, fitting, export, styles
src/ui     — main window, canvas, inspector, data table, dialogs, undo history
tests      — core and GUI tests, plus 38 sample files from the Grace distribution
tools      — English dictionary (i18n_en.json) and its generator / coverage check
packaging  — macOS build script, icon, Info.plist
```

Properties are described by schemas in `model.cpp`. One entry defines the `.agr` keyword,
the type and the default value, and the parser, the file writer and the inspector all use
it.

Run the tests with `cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure`.

**Compatibility with Grace.** `tests/grace_compat.sh build/NeoGrace` re-saves every sample
through NeoGrace and renders it with `gracebat` 5.1.25. Currently 31 of the 38 samples
match pixel for pixel. The rest differ by a few pixels (time stamp, antialiasing of old
fills, two-digit years in `times.agr`).

Files from older versions (Xmgr, ACE/gr, Grace 5.0) are converted to the current semantics
when opened, just as Grace itself does, and are saved as version 5.1.25.

`build/neograce_tests some_folder/` also saves rendered images of all the samples.

## Limitations

- Smith charts are drawn as ordinary XY graphs.
- Grace text transformations (`\t{…}`, slant `\l`) are ignored; the text itself is shown.
- Regions, the world stack and graph links are kept in the file but cannot be edited.
- Grace's Type 1 fonts are replaced by system fonts: Times → Times New Roman,
  Helvetica → Helvetica, Courier → Courier New, Symbol → Unicode Greek letters.

## License

GNU GPL version 2 or later, the same as Grace. NeoGrace uses Grace's fill patterns, and
the samples in `tests/data` come from the Grace distribution.
