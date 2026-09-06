# Z5 - Navigation et interaction UI

## Entrees physiques

Le panneau expose 24 Hall et 16 STEP. Hall et STEP sont deux familles
physiques distinctes: un Hall ne devient jamais un STEP par remappage logiciel.
Les STEP 1..8 ciblent les top-level et les STEP 9..16 ciblent les children
`8..15` lorsque le top-level 7 est GROUP master.

L'ISR Hall effectue uniquement l'acquisition ADC, le seuil/hysteresis, le
debounce borne et la mise en file d'un `live_event_t` de 16 octets. Les bits
modifier `SHIFT` et `TRACK` sont captures dans l'evenement. La queue est SPSC;
CONTROL interprete les evenements et possede la selection, le mode et le
contexte logique. Aucun handler UI ne lit un GPIO Hall comme un STEP.

KBD utilise les touches Hall pour les commandes clavier; SEQ utilise les STEP
pour les lanes et les fonctions de sequence. Les pages PLAY, TRACK CFG, MOD,
TONE, ENV, FILTER et MIX utilisent leurs renderers et handlers propres; aucun
renderer generique ne remplace leur etat produit.

STEP 1..8 selectionnent les top-level. En GROUP, STEP 9..16 selectionnent les children 8..15. CFG, ENV, TONE, MIX et PLAY utilisent l'entite selectionnee; MOD derive son owner par `entity_topology`.

`SHIFT + STEP 16` ouvre le Master global sans changer la selection. Master expose reverb, delay et compresseur et ne possede ni sequence, mute, clipboard Track, Undo ni slot persistant.

`ui_set_hall_mode` est l'autorite de transition. MUTE et PATTERN possedent leurs sous-etats; SEQ est un gate de handler. Sortir force MUTE/PATTERN nettoie leur etat via ce point central.

Le chemin Hall direct met a jour modifiers, selection, mode et double-tap avant le drain de la queue UI. SHIFT+HALL precede TRACK_MOD+HALL. Un evenement consomme par un stage masque les suivants.

En overlay Macro Switch, l'UI signale l'evenement Hall; CONTROL possede le
seuil/hysteresis de pression et le calcul de grandeur qui commande la source
de scene. L'UI ne decide pas la valeur produit.

Ordre contractuel du tick:

```text
track selection -> mute -> track hall gate -> transport -> settings
-> global shortcuts -> pattern -> sequence -> navigation -> page active
```

Les deltas encodeur utilisent un snapshot du contexte pris au debut du tick. Les modifications structurelles et restores appellent directement les owners Track; la mise a jour de contexte UI reste explicite via `ui_active_track_sync` et `ui_edit_context_sync`.

Les clipboards transportent uniquement des etats logiques. Un collage MIDI FX applique MODEL avant ses parametres; un collage External conserve l'entree demandee et echoue sur conflit.
