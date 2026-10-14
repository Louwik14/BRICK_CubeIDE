# Commandes physiques

## Statut

CONFIRME — validation materielle requise pour les libelles de facade, le sens mecanique des encodeurs et les seuils Hall.

## Surface logique

Le vocabulaire commun declare 44 IDs, mais la carte LowCost Release ne cable que 32 boutons : TRACK, PLAY, REC, SHIFT, TRANSPOSE UP/DOWN, COPY, PASTE, PAGE 1..4, poussoirs des quatre encodeurs et STEP 1..16. PARAM 1..7 et SETTINGS sont des IDs logiques synthetises par les raccourcis Hall, pas des boutons physiques de cette carte. Quatre encodeurs rotatifs produisent `UI_EVENT_ENCODER`; les boutons produisent PRESS/RELEASE; la surface STEP produit STEP_PRESS/STEP_RELEASE. Le clavier Hall 24 touches publie des `live_event_t`.

Sources : `Drivers/Drv_app/Inc/buttons_ids.h::button_id_t`, `Src/UI/ui_event.c::ui_event_from_inputs`, `Src/App/Hall/hall_surface.c`, `Src/Keyboard/keyboard_input.c`.

## Grammaire globale

| Manipulation | Effet prouve |
|---|---|
| raccourci Hall TONE/ENV/MOD/MIX/PLAY | synthetise PARAM 2/1/3/4/5 et ouvre l'ensemble |
| SHIFT + touche Hall B1 (ID 6) | alterne MIDI FX/Audio FX si aucun contexte ROUT ne prend la touche |
| SHIFT + touche Hall F1 / F#1 | mode KBD / SEQ |
| SHIFT + touche Hall G#1 / A1 / A#1 | Patch / browser Sample / Audio REC |
| SHIFT + touche Hall C2 | mode MACRO/Scene |
| SHIFT + touche Hall G#2 (ID 15) | Master global ; depuis TONE Master, alterne ses subsets |
| PAGE 1..4 | choisit la sous-page correspondante ; les cases indisponibles sont ignorees |
| Encodeur 1..4 | edite le slot P1..P4 de la page ; SHIFT emploie le pas fin pour les continus |
| STEP 1..8 avec TRACK maintenu | selection top-level 1..8 |
| STEP 9..15 avec TRACK maintenu, piste 8 en GROUP | selection child 1..7 |
| touche noire Hall 10 | ouvre Settings ; dans Settings, SETTINGS logique/remontee est gere par la page |
| PLAY | demande start/stop au FSM transport ; SHIFT+PLAY entre dans le ROM DFU |
| REC | arme/desarme la capture de notes ; SHIFT+REC ouvre REC CFG ; TRACK+REC bascule la capture audio et ouvre Audio REC |
| SHIFT+TRANSPOSE UP | entre en Quick Mute ; la surface Hall bascule les mutes disponibles |
| SHIFT+TRANSPOSE DOWN | entre en Pattern Recall ; TRACK+TRANSPOSE DOWN entre en Pattern Store |
| TRANSPOSE UP/DOWN en SEQ | avec step tenu, change Roll ; sinon change de page sequence |
| TRANSPOSE UP/DOWN en jeu Hall | change l'octave si aucun modifier |
| COPY/PASTE | scope resolu par les touches maintenues : Track, ensemble, page ou sequence/steps |
| SHIFT + COPY en contexte sequence | efface steps tenus ou sequence complete |
| TRACK + SHIFT + PASTE | efface la piste cible |
| SHIFT + PAGE n | selectionne la page sequence n ; deux PAGE tenues forment un masque ; double tap isole une page |

Les touches noires du Hall ajoutent, en SEQ/MACRO (ou SHIFT en Keyboard) : noires 1..5 = TONE/ENV/PLAY/MOD/MIX, 6 = Undo, 7 = Redo, 9 = REC CFG, 10 = Settings.

Sources : `Board/LowCost/Src/board_controls_lowcost.c::g_button_physical_to_logical`, `Src/UI/ui_hall_mode_flow.c::ui_hall_mode_flow_handle_lowcost_shift_step`, `Src/Keyboard/keyboard_input.c::keyboard_input_lowcost_trigger_black_shortcut`, `Src/UI/ui_navigation.c::g_ui_nav_rules`, `Src/UI/ui_core.c::ui_core_handle_seq_page_chord`, `Src/UI/ui_core_clipboard.c`, `Src/UI/ui_core_seq_transport.c`.

## Modes Hall et priorites

Modes : SEQ, KEYBOARD, MACRO/Scene, AUDIO_REC, PATCH, PATTERN et MUTE. Le pipeline direct traite SHIFT+Hall avant TRACK+Hall, puis la file UI suit : selection piste -> mute -> gate TRACK -> transport -> Settings -> raccourcis globaux -> Pattern -> sequence -> navigation -> page active. Une etape qui consomme l'evenement masque les suivantes.

MUTE et PATTERN ont des sous-etats propres. TRACK est un overlay momentane qui survit aux changements de page et disparait uniquement au relachement physique. MACRO memorise et restaure exactement SEQ ou KBD. `SHIFT + C2` (touche Hall ID 7) entre dans MACRO. Un tap en LIVE change PRESSURE/TOGGLE apres expiration de la fenetre de double tap ; un double tap ouvre ASSIGN sans appliquer la premiere bascule. Un tap depuis ASSIGN revient au dernier LIVE.

Sources : `Src/UI/ui_core.c::ui_core_tick`, `Src/UI/ui_hall_mode_flow.c`, `Src/UI/ui_hall_mode_state.c`, `Src/UI/ui_macro_interaction.c`, `docs/architecture/z5_ui_navigation_interaction.md` confronte aux symboles precedents.

## Appuis longs et doubles

Le type d'evenement generique ne contient aucun LONG_PRESS : les maintiens sont derives de `button_down()` ou de l'etat held. Les doubles taps explicitement prouves concernent les modes Hall/Scene et PAGE sequence (fenetre 300 ms). Ne pas generaliser un double appui a d'autres boutons.

## Clipboard

- TRACK maintenu + COPY : copie Track logique ; TRACK + PASTE : colle ; TRACK + SHIFT + PASTE : clear.
- touche PARAM d'ensemble maintenue + COPY/PASTE : copie/colle l'ensemble ; SHIFT+PASTE remet les valeurs admissibles au minimum.
- touche PAGE active maintenue + COPY/PASTE : meme logique sur les quatre controles de page.
- en SEQ sans autre scope : steps tenus, sinon sequence entiere ; le collage peut signaler TRUNC/PARTIAL/INCOMP.

La compatibilite est logique ; External refuse notamment un conflit d'entree physique.
