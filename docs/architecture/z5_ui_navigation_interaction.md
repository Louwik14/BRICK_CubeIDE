# Z5 - Navigation et interaction UI

STEP 1..8 selectionnent les top-level. En GROUP, STEP 9..16 selectionnent les children 8..15. CFG, ENV, TONE, MIX et PLAY utilisent l'entite selectionnee; MOD derive son owner par `entity_topology`.

`SHIFT + STEP 16` ouvre le Master global sans changer la selection. Master expose reverb, delay et compresseur et ne possede ni sequence, mute, clipboard Track, Undo ni slot persistant.

`ui_set_hall_mode` est l'autorite de transition. MUTE et PATTERN possedent leurs sous-etats; SEQ est un gate de handler. Sortir force MUTE/PATTERN nettoie leur etat via ce point central.

Le chemin Hall direct met a jour modifiers, selection, mode et double-tap avant le drain de la queue UI. SHIFT+HALL precede TRACK_MOD+HALL. Un evenement consomme par un stage masque les suivants.

Ordre contractuel du tick:

```text
track selection -> mute -> track hall gate -> transport -> settings
-> global shortcuts -> pattern -> sequence -> navigation -> page active
```

Les deltas encodeur utilisent un snapshot du contexte pris au debut du tick. Les modifications structurelles et restores appellent directement les owners Track; la mise a jour de contexte UI reste explicite via `ui_active_track_sync` et `ui_edit_context_sync`.

Les clipboards transportent uniquement des etats logiques. Un collage MIDI FX applique MODEL avant ses parametres; un collage External conserve l'entree demandee et echoue sur conflit.
