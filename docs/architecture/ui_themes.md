# UI themes and display preferences

`ui_theme` owns the active theme and the independent `CPU LOAD` visibility
preference. Both are loaded by `ui_bootstrap_init()` and stored atomically in
`0:/BRICK/UI_PREFS.BIN` (`.TMP`/`.BAK`, versioned header and CRC32).

## Theme model

The renderer has two immutable body styles and six selectable themes. A theme
descriptor contains only a name, a `base_style` and a `header_layout`:

| Theme | Body base | Global-information composition |
|---|---|---|
| `CLASSIC` | `CLASSIC` | Historical open-corner track badge, centered ensemble block and right-side BPM/CPU. |
| `DECK` | `CLASSIC` | Four instrument-like cells: track badge, track/Hall, dominant ensemble, BPM/CPU/pattern. |
| `HALO` | `CLASSIC` | Ensemble centered between split rules, with track metadata left and tempo/load right. |
| `MINIMALIST` | `MINIMALIST` | Historical sparse two-line header with the existing top-line cards and underline focus. |
| `STRIP` | `MINIMALIST` | Compact aligned strips separated by one horizontal rule; no decorative box. |
| `AXIS` | `MINIMALIST` | Asymmetric split composition: ensemble/track block left, metadata axis right. |

The two `ui_theme_base_t` records are the only owners of page/card frames,
focus, page-title typography, label typography, separators and body spacing.
Consequently `DECK` and `HALO` render every non-header element exactly like
`CLASSIC`; `STRIP` and `AXIS` render them exactly like `MINIMALIST`. Header
layouts receive one shared product model containing active track number/name,
Hall mode/suffix, ensemble, BPM/clock state, pattern and conditional CPU load.
Each non-reference layout measures and truncates long fields within its own
regions before drawing.

`CPU LOAD = OFF` removes the CPU string from that model. Layouts do not draw a
placeholder: tempo, pattern and whitespace retain a balanced composition.
No dynamic allocation, additional framebuffer or periodic redraw is used.

## Theme browser

Selecting `THEME` in Settings opens the existing `FAKE / THEME` preview page.
It uses the normal template renderer, cards, widgets, global header model and
page-button chrome. Encoder 1 walks `UI_THEME_COUNT` with wrap-around and calls
`ui_theme_preview()`, which changes only volatile UI state and invalidates the
frame. No persistence write occurs while scrolling.

`P1 RETURN` restores the theme captured on entry and returns to the `THEME` row.
`P2 LOAD` calls `ui_theme_commit_preview()` once, then returns to Settings. P3
and P4 have no label or action. Leaving through any other route also restores
the captured theme. The FAKE page exposes the same live track, track name, Hall,
ensemble, BPM and optional CPU information as ordinary template pages, plus
representative cards, selected value/focus and page labels.

## Persistence migration

Preference format version 2 stores the six-theme ID space. Version-1 files keep
ID 0 as `CLASSIC` and ID 1 as `MINIMALIST`; every removed legacy ID maps to
`CLASSIC`. Missing, corrupt, unsupported or out-of-range data also defaults to
`CLASSIC`, with CPU load enabled. The migrated value is written only on the next
explicit preference change or `LOAD`.

## Adding a theme

Add a stable ID and one `{name, base_style, header_layout}` entry. Reuse one of
the two body bases and add only a bounded header-layout case when a new
composition is needed. The Settings label and FAKE browser both derive names
and iteration from the same descriptor table; there is no parallel theme list.
