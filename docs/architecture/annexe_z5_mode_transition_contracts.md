# z5_mode_transition_contracts
Statut documentaire: Annexe utile (non canonique de zone).
Autorite: le document canonique de zone reste la source de verite.


Date: 2026-04-14
Scope: `ui_set_hall_mode`, `ui_core_handle_shift_hall_action`, `ui_core_handle_pattern_mode_event`, `ui_core_handle_seq_mode_event`, `ui_core_mute_handle_event` + helpers d'entree/sortie immediats.

## Carte courte du contrat reel
- Autorite de mode globale: `g_ui_track_state.hall_mode` via `ui_set_hall_mode` (`Src/UI/ui_core.c:3167`).
- Deux sous-machines locales superposees:
  - mute: `mute_active` + `mute_submode` + `mute_prev_mode(_valid)` (`Src/UI/ui_core.c:485`, `411`, `420`, `434`, `449`).
  - pattern: `pattern_mode` + `pattern_substate` + `pattern_prev_mode(_valid)` (`Src/UI/ui_core.c:231`, `240`, `2407`).
- Mode SEQ n'a pas de sous-etat dedie: c'est un gate de handler (`ui_core_handle_seq_mode_event` actif seulement si `hall_mode==SEQ`, `Src/UI/ui_core.c:2502`).

## Table: transition -> declencheur -> side effects -> ordre critique -> fragilite
| Transition | Declencheur | Side effects imposes | Ordre critique (handlers/priority) | Fragilite |
|---|---|---|---|---|
| `X -> MUTE (quick)` | `BTN_TRANSPOSE_UP` press avec `shift_down=1`, `track_select_armed=0`, mute inactif (`ui_core_mute_handle_event`) | Capture `mute_prev_mode`, `mute_active=1`, `mute_submode=QUICK`, transition `hall_mode=MUTE` via `ui_set_hall_mode` | `mute` est premier stage consommant dans `ui_core_tick`; l'event est exclusif ensuite | Entree mute passe par l'autorite centrale (hooks mode globaux conserves) |
| `MUTE(QUICK) -> MUTE(HOLD_QUICK)` | press `BTN_SHIFT` pendant `BTN_TRANSPOSE_UP` maintenu | `mute_submode=HOLD_QUICK`, `hall_mode=MUTE`, reset de la sequence shift latchee | Toujours absorbe par `mute`; le relache `SHIFT` arme la sequence prepare | L'ancien raccourci `prepare mute` est reinterprete ici |
| `MUTE(HOLD_QUICK) -> MUTE(PREPARE)` | `SHIFT` relache puis nouveau `BTN_SHIFT` press pendant `BTN_TRANSPOSE_UP` maintenu | Snapshot mutes runtime -> buffers prepared, `mute_submode=PREPARE`, `hall_mode=MUTE` | Toujours absorbe par `mute`; l'armement est explicite dans le sous-ետat mute | Nouvelle sequence prepare explicite et latchee |
| `MUTE(QUICK) -> prev/SEQ` | release `BTN_TRANSPOSE_UP` | `ui_core_mute_exit_to_previous_mode` -> `ui_set_hall_mode(target)` (clear mute via hook central) | Reste dans `mute` prioritaire; sortie consomme l'event | Contrat aligne sur transition centrale |
| `MUTE(HOLD_QUICK) -> prev/SEQ` | press `BTN_TRANSPOSE_UP` avec `shift_down=0` | `ui_core_mute_exit_to_previous_mode` -> `ui_set_hall_mode(target)` | Sortie explicite, distincte du momentary quick | Evite tout melange entre latched et momentary |
| `MUTE(PREPARE) -> prev/SEQ` | press `BTN_TRANSPOSE_UP` | Apply prepared mutes track par track, puis exit via `ui_set_hall_mode(target)` | `mute` masque tous handlers aval pendant l'etat | Contrat aligne sur transition centrale |
| `any -> PATTERN(RECALL)` | `BTN_TRANSPOSE_DOWN` press + `shift_down=1` (`ui_core_handle_transport_event`) | `ui_core_pattern_enter(RECALL)`: sauvegarde `pattern_prev_mode`, reset selection, `hall_mode=PATTERN` via `ui_set_hall_mode` | `transport` execute avant `pattern`/`seq`; consomme l'event | Dependance d'ordre forte: si transport bouge, contrat d'entree pattern change |
| `any -> PATTERN(STORE)` | `BTN_TRANSPOSE_DOWN` press + `track_select_armed=1` (`transport`) | idem avec `pattern_mode=STORE` | idem | idem |
| `PATTERN -> prev/SEQ` (cancel) | `BTN_TRANSPOSE_DOWN` press avec combo coherent (`shift`+recall ou `track_select`+store) dans `ui_core_handle_pattern_mode_event` | `ui_core_pattern_exit_to_previous_mode` -> `ui_set_hall_mode(target)` | `global_shortcuts` passe avant `pattern`; si shortcut consomme, ce cancel ne passe pas | Sortie explicite, mais depend d'un pipeline prioritaire externe |
| `PATTERN -> prev/SEQ` (success/fail) | `HALL_PRESS` en phase pattern-select | queue/capture pattern, feedback, puis `ui_core_pattern_exit_to_previous_mode`; en echec: feedback+exit aussi | `pattern` stage avant `seq/navigation/page` quand actif | Contrat fort: pattern monopolise les halls tant qu'actif |
| `X -> KEYBOARD/ARP/SEQ` | SHIFT+HALL trigger via `ui_core_handle_shift_hall_action` -> `ui_core_activate_hall_mode_trigger` | `hall_note_suppressed[hall]=1`; `ui_set_hall_mode(target)`; double-tap: `ui_page_set(target_page)` | Flux hors queue (`ui_core_service_track_selection_inputs`) avant tick et avant bridge clavier hall | Entree mode dual-path (hors queue vs dans queue), donc transitions dispersees |
| `PATTERN -> non-PATTERN` (forced) | tout `ui_set_hall_mode(mode!=PATTERN)` | `ui_core_pattern_abort_internal` (reset selection + invalidate prev_mode) + callback `keyboard_runtime_on_hall_mode_changed` | Hook central de sortie pattern | Contrat explicite et centralise ici |
| `MUTE -> non-MUTE` (forced) | tout `ui_set_hall_mode(mode!=MUTE)` | `ui_core_mute_clear_state` + callback `keyboard_runtime_on_hall_mode_changed` | Hook central de sortie mute | Aligne avec les transitions mute explicites (plus de bypass local) |
| `SEQ handler active/inactive` | gate `hall_mode==SEQ` (helper local `ui_core_is_seq_mode_gate_open`) dans `ui_core_handle_seq_mode_event` | Aucun changement mode, seulement edition seq | Passe apres `pattern` et apres `global_shortcuts` | SEQ est un "mode de traitement" gate, pas une sous-machine explicite |

## Verification explicite demandee
- Entree/sortie mute: alignee sur `ui_set_hall_mode` (plus d'assign direct de `hall_mode` dans les transitions mute), avec un sous-ensemble local `QUICK -> HOLD_QUICK -> PREPARE`.
- Entree/sortie pattern: entree via transport->`pattern_enter`; sorties via `pattern_exit` (cancel/success/fail) ou abort force dans `ui_set_hall_mode`.
- Relation hall_mode <-> seq mode: `seq mode` est seulement un gate handler (`hall_mode==SEQ`), pas une machine d'etat dediee.
- Priorite/masquage `global_shortcuts` vs `pattern/seq`: explicite et volontaire. Tout event consomme par `ui_core_handle_global_shortcuts` bloque `pattern`, `seq`, `navigation` et `page->handle_event` pour ce tour d'event.
- Side effects `ui_set_hall_mode`: guards, clear mute si sortie MUTE, abort pattern si sortie PATTERN, callback keyboard runtime, puis mutation `hall_mode`.
- Clears implicites entre modes: oui, via `ui_set_hall_mode`; mais mute a aussi ses clears propres hors `ui_set_hall_mode`.
- Ecrasement de mode sans contrat explicite: non pour mute (entree/sortie mute passent par `ui_set_hall_mode`).

## Vrais contrats vs reliquats
### Vrais contrats de transition
- `ui_set_hall_mode` est le point central contractuel de sortie forcee de PATTERN/MUTE.
- Priorites `ui_core_tick` imposent l'exclusivite mute puis pattern/seq.
- Entrees pattern par transport (combos transpose_down) sont contractuelles via ordre de stages.

### Reliquats / dette de structure
- SEQ reste un mode implicite de handler, non encode comme sous-machine explicite.

## Plus petit point de clarification recommande
- Garder la section de commentaire de reference dans `ui_set_hall_mode` comme source d'autorite unique de transition `hall_mode`.

