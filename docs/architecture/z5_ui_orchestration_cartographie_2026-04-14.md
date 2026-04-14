# Z5 UI / Orchestration - cartographie opérationnelle (passe 1)

Date: 2026-04-14
Scope strict: `ui_core`, `ui_navigation`, `ui_page_manager`, `ui_template_page` + preuves runtime minimales (`ui_tasklet`, `brick6_app_init`).
Hors scope: ré-audit détaillé Z2/Z3/Z4/Z6 (uniquement appelés comme preuves de flux).

## Preuve d'entrée runtime
- Boucle UI: `ui_tasklet_poll()` -> `ui_core_init()` (once) puis `ui_core_tick()` (`Src/UI/ui_tasklet.c:47`, `Src/UI/ui_tasklet.c:50`).
- Superloop: `engine_tasklet_poll()` puis `ui_core_service_track_selection_inputs()` puis `hall_keyboard_bridge_process()` (`Src/Core/brick6_app_init.c:141`, `Src/Core/brick6_app_init.c:147`, `Src/Core/brick6_app_init.c:148`).
- Implication: les actions hall "directes" (shift/track select) sont traitées hors queue d'events, avant le bridge clavier hall.

## Flux structurants Z5

### 1) Sélection de track (hall + modificateur)
- Caller réel: `brick6_app_process()` -> `ui_core_service_track_selection_inputs()` (`Src/Core/brick6_app_init.c:147`, `Src/UI/ui_core.c:2650`).
- Autorité Z5 portée: choix de `active_track`, armement track-select, suppression note hall.
- Zones ensuite appelées: Z3 (`param_store_set_active`, `param_registry_sync_ui_for_active_track` via sync), Z4 (`seq_runtime_get_*` dans sync), Z2 (`keyboard_runtime_on_active_track_changed`), Z2 runtime track cfg indirect via setters.
- Ordre d'appels: scan boutons/hall -> `ui_core_handle_track_hall_action()` (`Src/UI/ui_core.c:1257`) -> `ui_core_set_active_track()` (`Src/UI/ui_core.c:988`) -> `ui_core_sync_active_track_cfg_params()` (`Src/UI/ui_core.c:956`). Double tap -> `ui_page_set(UI_PAGE_TEMPLATE_CFG)`.
- Etat UI possédé: `g_ui_track_state.active_track`, `track_select_armed`, `cfg_tap_ms[]`, `hall_note_suppressed[]`.
- Fragilité éventuelle: couplage fort sélection track + sync param/seq dans un seul point central (`ui_core_set_active_track`), donc ordre implicite critique.

### 2) Changement de hall mode (SHIFT+HALL)
- Caller réel: `ui_core_service_track_selection_inputs()` -> `ui_core_handle_shift_hall_action()` (`Src/UI/ui_core.c:1234`).
- Autorité Z5 portée: machine de mode hall + double-tap mode/page.
- Zones ensuite appelées: Z5 page (`ui_page_set`), Z2 keyboard runtime (`keyboard_runtime_on_hall_mode_changed` via `ui_set_hall_mode`).
- Ordre d'appels: détection front hall + shift -> trigger lookup -> `ui_set_hall_mode()` (`Src/UI/ui_core.c:3155`) -> optionnel `ui_page_set(target_page)`.
- Etat UI possédé: `hall_mode`, `mode_tap_ms[]`, `hall_note_suppressed[]`, plus états pattern/mute invalidés dans `ui_set_hall_mode`.
- Fragilité éventuelle: `ui_set_hall_mode` déclenche des side effects transverses (clear mute/pattern + callback keyboard runtime), donc API de mode non "pure".

### 3) Raccourcis globaux
- Caller réel: boucle `ui_core_tick()` (`Src/UI/ui_core.c:2709`) via `ui_core_handle_global_shortcuts()` (`Src/UI/ui_core.c:2438`).
- Autorité Z5 portée: arbitrage prioritaire de copy/paste/clear/undo/settings.
- Zones ensuite appelées: Z3 (`param_registry_apply_track_value` via clipboard handlers), Z4 (`seq_edit_*` via seq clipboard), Z6 (`undo_v1_restore`), Z5 settings page.
- Ordre d'appels: après transport/settings gate, `ui_core_handle_global_shortcuts()` est testé avant pattern/seq/navigation/page handler.
- Etat UI possédé: `g_ui_clipboard`, `shift_down`, `track_select_armed`, feedback message.
- Fragilité éventuelle: orchestration très centralisée avec forte priorité par `continue`; un nouveau shortcut peut masquer des handlers aval sans contrat explicite.

### 4) Transport / pattern mode / mute
- Caller réel: `ui_core_tick()` event loop.
- Autorité Z5 portée: routage des boutons transport vers runtime seq/master-buffer/pattern/mute.
- Zones ensuite appelées:
  - transport: Z4 `seq_runtime_toggle_play_stop`, `seq_runtime_rec_toggle_arm`; Z2 `brick6_master_buffer_request_*` (`Src/UI/ui_core.c:1428`).
  - pattern: Z6 `pattern_live_capture_to_slot`, `pattern_live_queue_slot` (`Src/UI/ui_core.c:2390`).
  - mute: Z2/Z3 via `track_runtime_refresh_track`, `mixer_*`, `param_set` (`Src/UI/ui_core.c:485`).
- Ordre d'appels dans tick: `mute` -> consume track-hall -> master-buffer routing -> `transport` -> settings -> `global shortcuts` -> `pattern mode` -> `seq mode` -> navigation -> page handler (`Src/UI/ui_core.c:2731-2778` logique).
- Etat UI possédé: `mute_active`, `mute_submode`, `mute_prev_mode`, buffers mute prepared/initial; `pattern_mode`, `pattern_substate`, `pattern_selected_bank`, `pattern_prev_mode`.
- Fragilité éventuelle: double orchestration de modes (hall mode global + sous-états mute/pattern) avec transitions implicites et priorités dépendantes de l'ordre de handlers.

### 5) Sync UI -> param/runtime
- Caller réel: `ui_core_set_active_track`, `ui_restore_track_config_bulk`, `ui_set_track_family/type` (via `ui_core_sync_*`).
- Autorité Z5 portée: publication des valeurs "UI active" vers Z3 et activation runtime track lanes.
- Zones ensuite appelées: Z3 `param_store_set_active`, `param_registry_sync_ui_for_active_track`; Z2 `track_enable`, `track_runtime_invalidate_all`; Z4 lecture état seq pour exposer rec/tempo/sync.
- Ordre d'appels clé:
  - active track: set active -> `ui_core_sync_active_track_cfg_params`.
  - restore bulk: validation -> write arrays UI -> `track_runtime_invalidate_all` -> `ui_core_sync_audio_runtime_enables` -> `keyboard_runtime_on_active_track_changed` -> `ui_core_sync_active_track_cfg_params` (`Src/UI/ui_core.c:1062+`).
- Etat UI possédé: `track_configs[]`, `track_midi_channel[]`, `track_midi_source[]`, `active_track`.
- Fragilité éventuelle: UI porte une responsabilité de synchronisation système (pas seulement vue/commande), avec dépendance forte à l'ordre exact des appels.

### 6) Page active / template family / sous-page
- Caller réel:
  - navigation: `ui_navigation_handle_event()` (`Src/UI/ui_navigation.c:42`) depuis `ui_core_tick`.
  - page switch effectif: `ui_page_set()` (`Src/UI/ui_page_manager.c:91`).
  - template: `ui_template_page_enter/handle_event/select_subpage` (`Src/UI/ui_template_page.c:175`, `204`, `149`).
- Autorité Z5 portée:
  - `ui_page_manager`: page active + hooks leave/enter.
  - `ui_navigation`: règles bouton->page conditionnées par track family/type.
  - `ui_template_page`: résolution family active + sous-page + bank param active.
- Zones ensuite appelées: Z3 `ui_param_set_bank`; rendu Z5 (`ui_renderer_template_draw`).
- Ordre d'appels: event -> navigation éventuelle (`ui_page_set` reset bank+leave+enter) -> page `handle_event`; dans template, BTN_PAGE_n -> `ui_template_page_select_subpage` -> `ui_param_set_bank`.
- Etat UI possédé: `g_ui_current_page_id` (`ui_page_manager`), `ui_template_page_state_t.active_subpage/has_visited/resolved_family` (context page).
- Fragilité éventuelle: dépendance implicite forte à l'ordre d'enregistrement des pages (IDs stables), et à la résolution dynamique family track-aware au moment d'entrer/mettre à jour.

### 7) Restore UI track config vs runtime refresh
- Caller réel: API `ui_restore_track_config_bulk()` (appelée hors fichier audité, mais autorité définie dans Z5) (`Src/UI/ui_core.c:1062`).
- Autorité Z5 portée: validation snapshot UI, compat transform (DX7 extras -> MONOB), application bulk.
- Zones ensuite appelées: Z2 runtime invalidate/enable, Z2 keyboard runtime callback, Z3 sync params actifs.
- Ordre d'appels: validate constraints -> write state UI -> `track_runtime_invalidate_all` -> `ui_core_sync_audio_runtime_enables` -> `keyboard_runtime_on_active_track_changed` -> `ui_core_sync_active_track_cfg_params`.
- Etat UI possédé: totalité config track/midi dans `g_ui_track_state`.
- Fragilité éventuelle: Z5 décide des règles de compatibilité de restore (politique système) et pousse un refresh runtime global.

## Autorités réelles
- Autorité centrale Z5: `ui_core` (état global UI + orchestration d'events + transitions de modes + sync inter-zones).
- Sous-autorité navigation/page: `ui_navigation` (règles), `ui_page_manager` (page active et cycle leave/enter).
- Sous-autorité template contextuelle: `ui_template_page` (family active, sous-page active, bank param active).
- Autorité runtime-call-order: 
  - hors queue: `ui_core_service_track_selection_inputs` (dans superloop) avant `hall_keyboard_bridge_process`.
  - dans queue: ordre prioritaire fixe des handlers dans `ui_core_tick`.

## Points de fragilité
### Fragilités structurelles (actives)
- Centralisation excessive dans `ui_core`: état UI + policy + orchestration + appels directs Z2/Z3/Z4/Z6.
- Ordre implicite critique dans `ui_core_tick` (`continue`): le comportement dépend d'une priorité non contractualisée.
- Double orchestration de modes: `hall_mode` global et sous-machines `mute/pattern` partagent des transitions croisées.
- UI comme autorité de synchronisation système: `ui_core_sync_active_track_cfg_params` et `ui_core_sync_audio_runtime_enables` portent plus que du pilotage UI.
- Couplage page IDs / ordre d'enregistrement: risque de régression silencieuse si ordre modifié.

### Reliquats legacy (non structurants)
- Compat restore DX7->MONOB dans `ui_restore_track_config_bulk`: politique de backward compatibility localisée.
- Commentaires proto Input4/lane 3: dette matériel/proto, mais bornée et explicite.

## Prochaine passe recommandée
1. Extraire une carte "contracts d'ordre" de `ui_core_tick` (handlers, préconditions, exclusivité) pour figer les invariants avant refactor.
2. Isoler les autorités "sync système" de `ui_core` (track enable/runtime invalidate/param sync) derrière un orchestrateur dédié, sans changer le comportement.
3. Encadrer les transitions de modes (SEQ/KEYBOARD/ARP/PATTERN/MUTE) par une table de transition explicite et testable.
4. Formaliser la dépendance page-ID/registration (assertions de boot + tests de non-régression navigation).
