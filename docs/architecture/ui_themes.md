# UI themes and display preferences

`ui_theme` owns the active UI theme and the `CPU LOAD` visibility preference.
Both values are loaded by `ui_bootstrap_init()` and stored atomically in
`0:/BRICK/UI_PREFS.BIN` (`.TMP`/`.BAK`, versioned header and CRC32). A missing,
old, corrupt or out-of-range file selects `CLASSIC` and enables CPU load, so
existing installations retain the historical rendering.

The renderer reads one static `ui_theme_t` descriptor. It contains frame,
focus, header, typography, separator and small geometry choices; there is no
allocation or theme-specific page copy. Shared primitives render card/page
frames, focused elements, page titles and the template top-information model.
The top model keeps the existing product sources for active track number/name,
Hall mode/suffix, focused ensemble, BPM/clock state, pattern and conditional CPU
load. Theme or CPU visibility changes invalidate the OLED generation
immediately, including the cached template header/footer.

The five descriptors are:

- `CLASSIC`: existing 5x7/4x6 typography, open-corner chrome, cut-corner
  inverted focus and the historical structured header.
- `MINIMAL`: 5x7/4x6 typography, top-line cards, sparse header and underlined
  focus markers.
- `GRID`: 5x7/4x6 typography, closed cells with label separators, tabular
  header and solid rectangular focus.
- `TERMINAL`: compact existing font for title/header, bracket-only frames,
  command-line header and bracket focus.
- `MODERN`: 5x7/4x6 typography, asymmetric rails, split header and flag-shaped
  focus.

`CPU LOAD = OFF` omits the CPU string from the shared model. Each header style
then naturally leaves the BPM/pattern group expanded or uses the freed cell;
no placeholder is rendered. Settings, Project lists, Patch Browser, sample
browsers, template/synth/sequence pages and their focused ensemble/footer use
the shared theme primitives. Browser action mappings and the SHIFT footer icon
remain unchanged.

The persistent runtime state costs two bytes plus normal alignment; descriptors
and drawing code live in Flash. No framebuffer, heap object, extra periodic
redraw or dirty region is added. `GRID` draws the most primitives (closed cells
and separators), but its work remains bounded to the existing invalidated
frame.
