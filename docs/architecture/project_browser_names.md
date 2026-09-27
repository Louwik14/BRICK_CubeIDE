# Project and Patch browsers

Both browsers resolve PAGE actions and their footer labels through the same
`ui_browser_actions.h` mapping. Without SHIFT, PAGE 1–4 are NEW, LOAD,
RENAME, CLEAR (ERASE for Project). With SHIFT, PAGE 1 is SAVE; Project also
provides BLANK on SHIFT + PAGE 2. Other shifted PAGE buttons do nothing.

Project has one compact browser of existing, named Projects. Its selection
index maps to a private storage slot. `NEW` allocates a free slot in Storage;
`SAVE` replaces the focused Project without changing its name; `RENAME`
changes only the document name by transactional copy; `BLANK` creates an
unsaved working state. Invalid or unnamed files are excluded from the list.

Project files remain `0:/BRICK/PROJECT/P00.B6C` through `P15.B6C`. The slot
is the identity used by Save, Load, boot restoration, and recovery files.
Current valid Project documents remain readable without migration. The UI
never derives a display name from a slot.

Patch keeps its existing filters and visible list. `NEW` performs the former
PAGE 1 creation flow. `SAVE` replaces the focused Patch at its existing slot,
preserving its name. A family or type change may move it outside the current
filter; the browser then selects the next visible Patch and reports this.
