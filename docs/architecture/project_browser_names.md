# Project, Patch and asset browsers

`ui_browser_actions.h` resolves PAGE and SHIFT actions and supplies the footer
labels for these browsers. Blank positions display `-`.

| Browser | PAGE 1 | PAGE 2 | PAGE 3 | PAGE 4 |
| --- | --- | --- | --- | --- |
| Patch | RETURN / - | LOAD / RENAME | SAVE / DELETE | PREVIEW / - |
| Project | RETURN / - | LOAD / RENAME | SAVE / DELETE | - / - |
| Asset SD | RETURN / - | LOAD / REFRESH | RENAME / DELETE | PREVIEW / - |
| Asset pool | RETURN / - | UNLOAD / REFRESH | RENAME / - | PREVIEW / - |

Patch and Project lists begin with a virtual `[ + NEW ... ]` destination. LOAD
on it initializes the existing product default/blank path; SAVE opens naming and
creates a physical entry. On an existing entry SAVE overwrites it. RENAME,
DELETE and Patch PREVIEW are disabled for the virtual destination.

The Patch catalog selector is `ALL -> FOCUS -> SYN -> SMP -> DRM`, defaults to
FOCUS, and never filters out the virtual entry. FOCUS uses the current track
family. Patch PREVIEW is momentary (C4, velocity 100) and stops on release,
selection change, LOAD, or browser exit.

Patch and Project footers share the framed up-arrow indicator. Its inverted
state means the SHIFT labels are currently displayed; it consumes no extra row.

Each cell lists the action without SHIFT, then with SHIFT. Unavailable actions
are dimmed. Wavetable PREVIEW is unavailable because its browser has no
preview path. PAGE 4 starts RAM, Stream or Multi preview while held; release,
focus changes and leaving the browser stop it. Multi resolves C4 with velocity
100 and applies its root note pitch. The keyboard continues to play the active
track while the browser is open.

RETURN leaves a browser at its root. In hierarchical asset browsers it first
moves from the current directory to its direct parent, one level per press;
only RETURN at the asset root leaves the browser.

Project has one compact browser of existing, named Projects. Its selection
index maps to a private storage slot. NEW allocates a free slot in Storage;
SAVE replaces the focused Project without changing its name; RENAME changes
only the document name by transactional copy; BLANK creates an unsaved working
state. Discovery validates the canonical `PROJECT.B6C` envelope (current magic,
version, header and payload CRC), its normalized non-empty name and the sibling
`PATTERNS/` directory. Invalid or unnamed files are excluded from the list and
the per-slot scan status records the rejection reason.

Each slot is a visible directory, from `0:/PROJECTS/P00/` through `P15/`,
containing `PROJECT.B6C` and `PATTERNS/`. The slot is the identity used by
Save, Load and boot restoration. There is no legacy fallback or migration;
the UI never derives a display name from a slot.

Patch keeps its existing filters and visible list. NEW performs the former
PAGE 1 creation flow. SAVE replaces the focused Patch at its existing slot,
preserving its name. A family or type change may move it outside the current
filter; the browser then selects the next visible Patch and reports this. INIT
clears the target track to its default state.

Asset LOAD is unavailable when that source is already loaded. Removing an
asset from the runtime Pool
first publishes OFF to every matching Track selector, clears the canonical
references used by persistence, then retires the resource. SD RENAME and
DELETE refuse assets currently loaded or referenced by an active track, a saved Patch, a
`PROJECT.B6C`, or one of a Project's canonical Pattern files. They
also refuse while transport, recording or SD asset work is active. DELETE
requires confirmation. Multi rename updates its directory, index filename and
instrument name. Wavetable rename/delete purges derived caches. REFRESH rescans
the browser and restores the focused item where it still exists.
Pool RENAME remains dimmed while the asset is loaded because changing its
source path would require a coordinated update of runtime and saved references.
Multi DELETE checks that the folder is flat and bounded before removal; an SD
error during removal reports failure and can leave part of that folder behind.

## Cohérence du Sample Browser

Le pool global est la source canonique de la projection runtime affichée dans
la colonne Slots. Sa révision monotone change uniquement lors de la publication
ou de l'invalidation effective d'un slot. Le Sample Browser mémorise la révision
ayant produit son snapshot et reconstruit ce snapshot sur changement : un index
conservé par l'UI ne constitue jamais une référence durable vers le pool.

LOAD et CLEAR sont des requêtes, pas des résultats. Avec le transport actif,
STORAGE peut terminer la préparation, le retrait des leases STREAM et la
publication plusieurs tours coopératifs plus tard. L'UI conserve alors le type
d'opération et le slot attendu, affiche `LOADING` ou `CLEARING`, et ne publie
`LOAD OK`/`CLEAR OK` qu'après l'état terminal canonique. À ce moment la liste,
la sélection et les compteurs sont reconstruits ensemble. Une erreur réelle de
chargement reste issue de `sample_global_pool` et n'est pas convertie en succès.

Les listes de fichiers WAV restent la projection du catalogue STORAGE. Une
mutation SD invalide/reconstruit ce catalogue puis publie sa vue paginée ; la
vue UI n'est remplacée qu'après `WAV_LOADER_CATALOG_VIEW_PUBLISHED`. Wavetable
et Multi gardent leurs complétions dédiées, mais partagent la règle : aucune
admission asynchrone ne doit être présentée comme une terminaison.
