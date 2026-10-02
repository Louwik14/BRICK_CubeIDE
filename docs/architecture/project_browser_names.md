# Project, Patch and asset browsers

`ui_browser_actions.h` resolves PAGE and SHIFT actions and supplies the footer
labels for these browsers. Blank positions display `-`.

| Browser | PAGE 1 | PAGE 2 | PAGE 3 | PAGE 4 |
| --- | --- | --- | --- | --- |
| Patch | NEW / SAVE | LOAD / INIT | CLEAR / RENAME | RETURN / - |
| Project | NEW / SAVE | LOAD / BLANK | ERASE / RENAME | RETURN / - |
| Asset SD | PREVIEW / - | LOAD / REFRESH | RENAME / DELETE | RETURN / - |
| Asset pool | PREVIEW / - | UNLOAD / REFRESH | RENAME / - | RETURN / - |

Each cell lists the action without SHIFT, then with SHIFT. Unavailable actions
are dimmed. Wavetable PREVIEW is unavailable because its browser has no
preview path. PAGE 1 starts RAM, Stream or Multi preview while held; release,
focus changes and leaving the browser stop it. Multi resolves C4 with velocity
100 and applies its root note pitch. The keyboard continues to play the active
track while the browser is open.

Project has one compact browser of existing, named Projects. Its selection
index maps to a private storage slot. NEW allocates a free slot in Storage;
SAVE replaces the focused Project without changing its name; RENAME changes
only the document name by transactional copy; BLANK creates an unsaved working
state. Invalid or unnamed files are excluded from the list.

Each slot is a visible directory, from `0:/PROJECTS/P00/` through `P15/`,
containing `PROJECT.B6C` and `PATTERNS/`. The slot is the identity used by
Save, Load and boot restoration. There is no legacy fallback or migration;
the UI never derives a display name from a slot.

Patch keeps its existing filters and visible list. NEW performs the former
PAGE 1 creation flow. SAVE replaces the focused Patch at its existing slot,
preserving its name. A family or type change may move it outside the current
filter; the browser then selects the next visible Patch and reports this. INIT
clears the target track to its default state.

Asset LOAD is unavailable when that source is already loaded. UNLOAD refuses
an asset referenced by an active track. SD RENAME and DELETE refuse assets
currently loaded or referenced by an active track, a saved Patch, a
`PROJECT.B6C`, or one of a Project's canonical Pattern files. They
also refuse while transport, recording or SD asset work is active. DELETE
requires confirmation. Multi rename updates its directory, index filename and
instrument name. Wavetable rename/delete purges derived caches. REFRESH rescans
the browser and restores the focused item where it still exists.
Pool RENAME remains dimmed while the asset is loaded because changing its
source path would require a coordinated update of runtime and saved references.
Multi DELETE checks that the folder is flat and bounded before removal; an SD
error during removal reports failure and can leave part of that folder behind.
