# Z5 - Navigation et interaction UI

STEP 1..8 selectionnent les top-level. En GROUP, STEP 9..16 selectionnent les children 8..15. CFG, ENV, TONE, MIX et PLAY utilisent l'entite selectionnee; MOD derive son owner par `entity_topology`.

`SHIFT + STEP 16` ouvre le Master global sans changer la selection. Master expose reverb, delay et compresseur et ne possede ni sequence, mute, clipboard Track, Undo ni slot persistant.

`ui_set_hall_mode` est l'autorite de transition. MUTE et PATTERN possedent leurs sous-etats; SEQ est un gate de handler. Sortir force MUTE/PATTERN nettoie leur etat via ce point central.

Le chemin Hall direct met a jour modifiers, selection, mode et double-tap avant le drain de la queue UI. SHIFT+HALL precede TRACK_MOD+HALL. Un evenement consomme par un stage masque les suivants.

La page PATCH conserve une grammaire locale PAGE 1 SAVE, PAGE 2 LOAD, PAGE 3 RENAME,
PAGE 4 CLEAR. Cette passe branche uniquement SAVE et RENAME via le Name Editor
generique; LOAD et CLEAR gardent leur comportement existant. Le double-tap Hall
n'est plus une commande Save: il ouvre le contexte PATCH, dont le Save est porte
par PAGE 1.

Le contexte temporaire TRACK est resolu par `ui_hall_mode_track_overlay_active`,
utilise par le dispatch Hall et la projection LED. Il prime sur la page active
pour la selection des tracks, puis disparait au relachement de TRACK; la page
reprend alors sa projection propre. Les overlays MUTE, MACRO et SHIFT restent
prioritaires selon leur contrat.

Audio REC est une page modale Low-Cost, mais les boutons d'ensemble restent
navigables lorsqu'une destination est disponible. La navigation standard ferme
alors la page REC, restaure le mode/page de retour et ouvre l'ensemble demande;
le changement n'est pas traite comme une capture de bouton par REC. REC CFG
reste une page dediee au reglage de l'enregistrement.

Ordre contractuel du tick:

```text
track selection -> mute -> track hall gate -> transport -> settings
-> global shortcuts -> pattern -> sequence -> navigation -> page active
```

Les deltas encodeur utilisent un snapshot du contexte pris au debut du tick. Les modifications structurelles et restores appellent directement les owners Track; la mise a jour de contexte UI reste explicite via `ui_active_track_sync` et `ui_edit_context_sync`.

Les clipboards transportent uniquement des etats logiques. Un collage MIDI FX applique MODEL avant ses parametres; un collage External conserve l'entree demandee et echoue sur conflit.
