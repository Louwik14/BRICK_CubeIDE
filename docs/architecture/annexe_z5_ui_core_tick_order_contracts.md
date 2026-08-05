# ui_core_tick_order_contracts
Statut documentaire: Annexe utile (non canonique de zone).
Autorite: le document canonique de zone reste la source de verite.


Date: 2026-04-15
Scope: passe d'audit ciblee `ui_core_tick` + helpers directement appeles par `ui_core_tick`, avec lien explicite vers le flux hors queue `ui_core_service_track_selection_inputs()`.

## Preuve de flux runtime
- Superloop: `brick6_app_process()` appelle `hall_loop_process()` -> `ui_core_service_track_selection_inputs()` -> `hall_keyboard_bridge_process()`. Sur les deux variantes, l'IRQ ADC DMA a deja execute `hall_engine_process_sample()`; `hall_loop_process()` ne depile plus de FIFO et le bridge consomme la file commune d'événements Hall horodatés.
- Tasklet UI: `ui_tasklet_poll()` appelle `ui_core_tick()` (`Src/UI/ui_tasklet.c:50`).
- Scheduler: `main` ne lance plus `ui_tasklet_poll()` en 1:1 avec `engine_tick_count`; le service UI est sous-echantillonne par un diviseur explicite et le rattrapage reste borne par tour de boucle principale.
- Implication contractuelle: certaines transitions de mode/track se font hors queue d'events, avant `ui_core_tick` et avant le bridge clavier hall.

## Contrat du chemin hors queue
- Ecritures UI reelles effectuees par `ui_core_service_track_selection_inputs()`:
  - `shift_down` (miroir etat bouton SHIFT),
  - `track_select_armed` (miroir etat bouton track-mod),
  - `hall_prev_pressed[]` (memo front montant halls),
  - `hall_note_suppressed[]` (suppression note hall sur actions directes),
  - `hall_mode` via `ui_set_hall_mode` (SHIFT+HALL),
  - `active_track` via `ui_core_set_active_track` (TRACK_MOD+HALL),
  - `mode_tap_ms[]` / `cfg_tap_ms[]` (double-tap mode/track),
  - `page` via `ui_page_set` (double-tap mode/track).
- Invariants supposes:
  - chemin suspendu si `mute_active!=0`,
  - actions halls directes declenchees uniquement sur front montant (`hall_prev_pressed`),
  - precedence SHIFT+HALL sur TRACK_MOD+HALL quand les deux modificateurs seraient actifs, resolue explicitement par `ui_core_resolve_hall_direct_action()` (et non plus seulement par ordre implicite d'`if`).
- Dependances implicites cote `ui_core_tick`:
  - `transport`, `global_shortcuts`, `pattern`, `seq`, `mute` lisent `shift_down`/`track_select_armed` et dependent du pre-traitement hors queue pour des flags frais,
  - `seq`/`pattern` gates lisent `hall_mode` potentiellement bascule juste avant depilement de queue,
  - `ui_core_is_track_hall_event_consumed` depend de `track_select_armed` pre-mis a jour pour bloquer/laisser passer les halls.
  - `ui_navigation_handle_event` peut changer la page active juste avant le dispatch final; `active_page->handle_event` recoit donc l'event sur la page active apres navigation.
- Les deltas encodeur sont resolus sur un snapshot local du contexte actif pris au debut du tick UI; un edit qui reconfigure bank ou track pendant la passe ne rebind pas le sens des deltas suivants dans le meme tick.
- Le drain encodeur du tick est encadre par `param_registry_batch_begin/end` et un refresh runtime unique en amont, afin de coalescer les applies track-aware du meme tick.

## Contrat post-commit UI
- Les reconfigurations structurelles `track family/type` et `restore bulk` passent par `ui_core_runtime_bridge` puis sur un post-commit unique `ui_core_runtime_bridge_post_track_structure_change()`.
- Le miroir de track active au moment du focus courant est egalement porte par `ui_core_runtime_bridge` (`sync_active_track_context` / `sync_active_track_mirror` / mirror MIDI), afin de garder `ui_core` dans le role d'arbitre.
- Les lectures runtime encore exposees au rendu UI (`seq_edit_step_hold_update`, `track_runtime_resolve_track`, `seq_edit_get_page`, `keyboard_runtime_get_octave_shift`, `pattern_live_*`) passent aussi par `ui_core_runtime_bridge`, pour eviter une dependance directe de `ui_core.c` a ces sous-systemes.
- La metadonnee hall mode (trigger, page cible, label brut) est centralisee dans `ui_hall_mode_contract`.
- La politique d'activation hall (double-tap, trigger->page, transition) est centralisee dans `ui_hall_mode_flow`.
- La projection visible hall (effective view, labels, lecture/injection) est centralisee dans `ui_hall_mode_projection`.
- L'autorite brute du hall_mode est portee par `ui_hall_mode_state`, hors de `ui_core`.
- Le chemin hors queue hall/transpose est porte par `ui_hall_input_service`, avec `ui_core` reduit a l'orchestration superloop et au track-select residuel.

## Contrat navigation UI
- `ui_core` ne requiert plus directement de page via `ui_navigation_*`; les demandes de page passent par `ui_core_navigation_bridge`.
- La synchronisation d'ensemble actif au post-commit de contexte passe aussi par le bridge navigation.

## Ordre reel des stages dans `ui_core_tick`
Source: `Src/UI/ui_core.c:743-805`.

1. `ui_core_handle_track_selection_event(&ev)` (toujours execute, non bloquant)
2. `ui_core_mute_handle_event(&ev)` (consume+block)
3. `ui_core_is_track_hall_event_consumed(&ev)` (consume+block)
5. `ui_core_handle_transport_event(&ev)` (consume+block)
6. `ui_page_settings_handle_event(&ev)` (consume+block si settings ouverte)
7. `ui_core_handle_global_shortcuts(&ev)` (consume+block)
8. `ui_core_handle_pattern_mode_event(&ev)` (consume+block)
9. `ui_core_handle_seq_mode_event(&ev)` (consume+block)
10. `ui_navigation_handle_event(&ev)` (non bloquant)
11. `ui_page_get()->handle_event(&ev)` (non bloquant, page active potentiellement deja changee par navigation)

## Table operative des invariants de stage
| Stage | Preconditions d'entree | Etat lu/ecrit | Consommation/exclusivite | Handlers aval masques si consume | Side effects inter-zones |
|---|---|---|---|---|---|
| `track_selection_event` | `ev!=0`, mute inactif pour agir | lit `ev`; ecrit `shift_down`, `track_select_armed` | Non (void) | Aucun | Aucun direct; prepare les flags utilises par stages suivants |
| `mute` | voir gestures mute; entree quick exige `shift_down=1`, `track_select_armed=0`, `BTN_TRANSPOSE_UP` press; hold quick et prepare sont reconnus localement dans le meme handler | lit/ecrit `mute_active/submode/prev_mode`, `mute_hold_quick_prepare_armed`, `hall_mode`, `hall_note_suppressed` | Oui si match (`return 1`) | Tous (stages 3..11) | appels runtime mute (`track_runtime_refresh_track`, `mixer_set_track_mute`, `param_set`) |
| `track_hall_consume` | `mute_active=0`, `track_select_armed=1`, `hall_mode!=PATTERN`, event hall press/release valide | lit flags mode/modifier | Oui (predicat de gate) | Tous (stages 4..11) | Aucun; role de barriere d'orchestration |
| `settings_gate` | settings ouverte (`ui_page_settings_is_open()!=0`) | lit open-state; delegue event settings interne | Oui (tous events) | Tous (stages 7..11) | Z5 settings workflow |
| `global_shortcuts` | `ev!=0` | lit combos/clipboard flags; ecrit clipboard/feedback | Oui si match | Tous (stages 8..11) | Z3/Z4/Z6 via copy/paste/undo + open settings |
| `pattern_mode` | `hall_mode==PATTERN` | lit/ecrit pattern substate/prev_mode, `hall_mode` via exit | Oui sur cancel et hall actions pattern | Tous (stages 9..11) | Z6 pattern capture/queue; sortie via `ui_set_hall_mode` |
| `seq_mode` | gate SEQ ouvert (`ui_core_is_seq_mode_gate_open` -> `hall_mode==SEQ`) | lit events halls/boutons; ecrit etat edition seq | Oui si event seq reconnu | Stages 10..11 | Z4 seq edit/copy/paste/clear |
| `navigation` | button press + regle valide | lit page courante/track context; peut changer page | Non | Aucun | Z5 page switch (`ui_page_set`) |
| `active_page->handle_event` | page active et handler non nul | propre a la page | N/A | N/A | Recoit l'event sur la page active apres `ui_navigation_handle_event` (contrat volontaire) |

## Verifications ciblees demandees
- `mute` vs `ui_set_hall_mode`: aligne. Les transitions mute (entree quick/prepare, sortie vers mode precedent) passent par `ui_set_hall_mode`, donc conservent le bridge de notification hall-mode et les hooks de sortie centralises (`Src/UI/ui_core.c:1055-1090`, `Src/UI/ui_core_runtime_bridge.c:498-505`).
- Entree/sortie `pattern`: entree uniquement depuis stage transport sur `BTN_TRANSPOSE_DOWN` combos (`ui_core_pattern_enter`), sorties via handler pattern (cancel/success/fail) et via abort force lors de `ui_set_hall_mode(mode!=PATTERN)` (`Src/UI/ui_core.c:569-574`, `Src/UI/ui_core.c:1055-1090`, `Src/UI/ui_core_runtime_bridge.c:457-470`).
- Gate reel de `seq mode`: strict `hall_mode==SEQ` via helper local `ui_hall_is_seq_context`; aucun sous-etat dedie SEQ. Events consommes: `COPY/PASTE` (si steps tenus), `HALL_PRESS/RELEASE` sur step, `TRANSPOSE_UP/DOWN` page seq, avec court-circuit si `shift_down!=0` hors bloc copy/paste (`Src/UI/ui_core.c:639-805`, `Src/UI/ui_core_runtime_bridge.c:492-500`).
- Priorite `global_shortcuts` avant `pattern/seq`: confirmee par table de stages (`Src/UI/ui_core.c:779-787`).
- Events/combos consommes par `global_shortcuts`:
  - clipboard track (`track_select_armed=1` + `COPY/PASTE`),
  - clipboard ensemble (param button tenue + `COPY/PASTE`),
  - clipboard page (button page active tenue + `COPY/PASTE`),
  - clipboard seq scope (`hall_mode==SEQ`, pas de track-select ni button param/page tenue, `COPY/PASTE`),
  - `BTN_SETTINGS` press,
  - `SHIFT+COPY` (undo).
- Masquage effectif si consume `global_shortcuts`: `pattern_mode`, `seq_mode`, `navigation`, puis `active_page->handle_event` sur le meme event.
- Interaction hors queue `ui_core_service_track_selection_inputs()`: confirmee en superloop avant bridge hall; peut changer `hall_mode` via `ui_core_handle_shift_hall_action()` et track actif avant tout traitement event de `ui_core_tick` (`Src/Core/brick6_app_init.c:64-66`, `Src/UI/ui_core.c:675-742`, `Src/UI/ui_core.c:743-805`).
- Post-commit structurel: `ui_core_runtime_bridge_post_track_structure_change()` centralise la sync UI visible apres mutation structurelle, au lieu de laisser `ui_core` rebrancher lui-meme le miroir et l'edit-context.
- Contrat `navigation -> active_page->handle_event`: coherent et volontaire. `ui_navigation_handle_event` peut appeler `ui_page_set`; le `ui_page_get()` fait juste apres determine la page qui traite effectivement le meme event.

## Fragilites structurelles prouvees (courtes)
- Dual-path principal reduit: mute n'a plus de transition directe de `hall_mode`; `ui_set_hall_mode` reste l'autorite unique pour les bascules de mode.
- Pipeline `continue` tres prioritaire: un stage amont peut masquer totalement navigation + page handler sans trace explicite.
- Contrat hors queue/hors tick: changements mode/track en superloop peuvent devancer la semantique attendue "event queue first".
- SEQ reste un mode de handler (gate), pas une sous-machine explicite; dependance forte a l'ordre relatif pattern/shortcuts/seq.

## Statut de cloture
- Sous-chantier local "contrats d'ordre Z5" clos pour l'instant.
- Dernier point local explicite: double ecriture volontaire des modificateurs (`shift_down`, `track_select_armed`) entre chemin hors queue et chemin queue, maintenue comme mecanisme de coherence.

