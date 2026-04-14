# Z5 - UI / Navigation / Interaction

## 1. Perimetre

Perimetre operationnel de zone (appartient a Z5):
- `Src/UI/ui_core.c`
- `Inc/UI/ui_core.h`
- `Src/UI/ui_navigation.c`
- `Inc/UI/ui_navigation.h`
- `Src/UI/ui_template_page.c`
- `Inc/UI/ui_template_page.h`
- `Src/UI/pages/` (toutes les pages enregistrees par `ui_core_init`)

Elargissements necessaires (preuves de frontieres et contrats):
- `Src/UI/ui_page_manager.c` + `Inc/UI/ui_page_manager.h`: autorite page active et changement de page.
- `Src/UI/ui_event.c` + `Inc/UI/ui_event.h`: pipeline input bouton/hall -> queue events UI.
- `Src/App/Hall/hall_keyboard_bridge.c`: consommation de `ui_get_hall_mode` et suppression notes hall (`ui_core_hall_note_is_suppressed`).
- `Src/Core/brick6_app_init.c`: ordre d'appel runtime (`ui_core_service_track_selection_inputs` puis `hall_keyboard_bridge_process`).

Dependances de Z5 sans appartenir a Z5:
- Z2 `track_runtime` (track validity/bind pour mute, routing, labels).
- Z3 `param_registry/param_store` (UI cfg, copy/paste, edits params).
- Z4 `seq_runtime/seq_edit/seq_model` (transport, pattern mode, clipboard seq).
- Storage (`pattern_live_ram`, `undo_v1`) pour recall/store/undo.
- Keyboard runtime/hall engine pour comportements mode hall.

Exclusions explicites:
- Rendu audio hard-RT (Z1) hors possession UI.
- Autorite param/modele seq hors UI (Z3/Z4), seulement pilotees depuis Z5.

Sous-roles concentres dans `ui_core.c`:
- Etat UI global courant (track, hall mode, feedback, states pattern/mute/clipboard).
- Orchestration des interactions (track select, transport, shortcuts, clipboard, pattern mode).
- Synchronisation contexte track actif vers param/runtime.

## 2. Autorite(s) de verite

Autorite etat UI courant:
- `g_ui_track_state` dans `ui_core.c`.
- APIs autoritatives: `ui_get_active_track`, `ui_set_hall_mode`, `ui_get_hall_mode`, `ui_get_*` state helpers.

Autorite page/ensemble actif:
- Page active: `ui_page_manager` (`g_ui_current_page_id`, `ui_page_set`, `ui_page_get`).
- Ensemble/subpage template active: `ui_template_page` (`ui_template_page_state_t.active_subpage`, `ui_template_family_resolve*`, `ui_template_page_select_subpage`).

Autorite track select:
- `ui_core_service_track_selection_inputs` + `ui_core_handle_track_hall_action` + `ui_core_set_active_track`.

Autorite hall modes:
- `ui_set_hall_mode` (validation transition + side effects mute/pattern cleanup + notification keyboard runtime).
- Triggers SHIFT+HALL dans `ui_core_handle_shift_hall_action`.

Autorite navigation boutons param:
- `ui_navigation_handle_event` (table `g_ui_nav_rules` data-driven).

Autorite raccourcis interaction:
- `ui_core_handle_global_shortcuts`, `ui_core_handle_transport_event`, `ui_core_handle_seq_mode_event`, `ui_core_handle_pattern_mode_event`, `ui_core_mute_handle_event`.

Clipboard UI:
- `g_ui_clipboard` dans `ui_core.c` + handlers `ui_core_handle_*_clipboard_event`.

Seconde autorite concurrente:
- Pas de seconde autorite concurrente sur l'etat UI courant; `ui_core` reste le point central.
- `ui_page_manager` est sous-autorite dediee a l'etat page, invoquee par `ui_core`/pages.

## 3. API entrantes

Entrees directes:
- `ui_core_init`, `ui_core_tick`, `ui_core_service_track_selection_inputs`.
- Appelants reels:
  - `ui_tasklet_poll` -> `ui_core_init/ui_core_tick`.
  - `brick6_app_process` -> `ui_core_service_track_selection_inputs`.

Entrees evenementielles:
- `ui_event_from_inputs` (buttons + hall) alimente queue lue par `ui_core_tick`.
- `ui_core_tick` enchaine handlers dans cet ordre: track-selection -> mute -> consommations hall track-select -> master-buffer routing -> transport -> settings -> global shortcuts -> pattern mode -> seq mode -> navigation -> page handler.

Contrats implicites d'ordre:
- L'ordre `ui_core_service_track_selection_inputs()` puis `hall_keyboard_bridge_process()` (dans `brick6_app_process`) garantit que suppression hall est fixee avant emission notes clavier.
- Dans `ui_core_tick`, les `continue` imposent priorite des couches de shortcuts/navigation.

## 4. API sortantes

Sorties vers UI interne:
- `ui_page_set`, `ui_page_get`, `ui_navigation_handle_event`, `ui_template_page_*`, `ui_param_set_bank`, `ui_param_handle_encoder`.

Sorties vers autres zones:
- Z2: `track_runtime_refresh_*`, `track_runtime_get_ctx`, `track_runtime_invalidate_all`, etc.
- Z3: `param_set`, `param_registry_apply_track_value`, `param_registry_batch_*`, `param_registry_sync_ui_for_active_track`, `param_store_set_active`.
- Z4: `seq_runtime_toggle_play_stop`, `seq_runtime_rec_toggle_arm`, `seq_runtime_set_track_div/quant/swing`, `seq_edit_*`, `seq_model_*`.
- Storage: `pattern_live_queue_slot`, `pattern_live_capture_to_slot`, `undo_v1_restore`.
- Master buffer: `brick6_master_buffer_*` (routing hall en mode ARP master-buffer, REC shortcuts).

Getters non-mutants vs mutables:
- Non-mutants: `ui_get_active_track`, `ui_get_track_*`, `ui_get_hall_mode`, `ui_get_pattern_stub_state`, `ui_get_mute_state`, `ui_get_mute_hall_led`.
- Mutables: `ui_set_track_family/type`, `ui_set_track_midi_channel/source`, `ui_set_hall_mode`, clipboard/pattern/mute handlers.

## 5. Etats structurants possedes

Etat global UI:
- `g_ui_track_state` (`ui_track_state_t`) dans `ui_core.c`.
- Champs structurants: active track, shift, track_select_armed, hall_mode, mode/cfg tap timers, track_configs[14], midi channel/source, hall suppression, pattern state, feedback, mute state/buffers.
- Ecritures: init + handlers d'events/selection.
- Lectures: getters UI, renderer template, hall keyboard bridge, logique shortcuts.

Etat clipboard:
- `g_ui_clipboard` (`ui_clipboard_state_t`) avec sous-etats track/ensemble/page.
- Ecriture: copy handlers (`ui_core_clipboard_copy_*`).
- Lecture: paste/clear handlers.

Etat page active:
- `g_ui_current_page_id` + `g_ui_pages[]` dans `ui_page_manager.c`.
- Ecriture: `ui_page_manager_init`, `ui_page_manager_register`, `ui_page_set`.
- Lecture: `ui_page_get`, `ui_page_get_id`, navigation/settings.

Etat template families/subpages:
- `g_ui_template_family_registry[...]` dans `ui_template_page.c`.
- Etat courant par page template: `ui_template_page_state_t` (dans chaque page concrete `Src/UI/pages/*`).
- Ecriture: register families, `ui_template_page_select_subpage`, enter handlers.
- Lecture: renderer template, clipboard active page.

Etat queue events UI:
- `g_ui_evt_q`, `g_ui_evt_w`, `g_ui_evt_r`, `g_ui_hall_prev_pressed` dans `ui_event.c`.
- Ecriture: `ui_event_from_inputs`.
- Lecture: `ui_event_pop` dans `ui_core_tick`.

## 6. Flux runtime

Flux nominal prouve:
1. Entree input UI
- Buttons/hall lus dans `ui_event_from_inputs` + `ui_core_service_track_selection_inputs` (path direct track-select/shift/hall).

2. Resolution navigation / raccourci
- Dans `ui_core_service_track_selection_inputs`: SHIFT+HALL => mode trigger; TRACK_MOD+HALL => active track.
- Dans `ui_core_tick`: resolution priorisee mute/transport/shortcuts/pattern/seq/navigation.

3. Mutation etat UI
- Mutations de `g_ui_track_state` (hall_mode, track_select_armed, active_track, pattern/mute/feedback/clipboard states).

4. Resolution contextuelle page/ensemble
- `ui_navigation_handle_event` mappe boutons param -> page cible selon disponibilite track-family/type.
- `ui_template_page` resout family/subpage active et banque param associee.

5. Appels vers param/runtime/seq/storage
- Selon handler: apply params, track config, seq edits, pattern queue/store, undo, settings, master buffer routing/record.

6. Feedback / consommation aval
- Feedback texte via `ui_core_set_feedback` visible dans header track.
- `hall_keyboard_bridge` consomme hall mode + flags suppression pour autoriser/bloquer emission note hall.

## 7. Contraintes RT/CPU/memoire

Contraintes observees:
- Aucun malloc dans Z5; etats statiques.
- Traitement UI event-driven/control-rate (main loop/tasklet), pas hard-RT IRQ audio.
- File events taille fixe (`UI_EVENT_Q_LEN=32`) avec drop silencieux si pleine.

Dependances de cadence:
- `ui_core_service_track_selection_inputs` doit tourner regulierement pour detection taps/double taps/armed states.
- `ui_core_tick` doit etre appele regulierement pour drainer queue events et tick page active.

## 8. Invariants a ne pas casser

Invariants prouves:
- Autorite unique etat UI courant: `g_ui_track_state` centralise dans `ui_core.c`.
- Resolution contextuelle track-aware: disponibilite pages/types depend de family/type de la track active (`ui_navigation_is_page_available`, `ui_template_family_resolve_active_track`).
- Priorite SHIFT/HALL sur track-select en service input: condition explicite `shift_down !=0` et `track_select_armed==0` avant trigger mode hall.
- Changement hall mode passe par `ui_set_hall_mode` avec nettoyage mute/pattern en sortie de mode.
- Navigation et logique runtime ne sont pas separees strictement: `ui_core` appelle directement seq/param/track_runtime/storage.
- Cohabitation hall modes / ensembles UI maintenue par resolver family/subpage et label suffix selon mode.

## 9. Dependances inter-zones

Entrees vers Z5:
- Hall/buttons/encoders hardware via `ui_event` + polling boutons/hall.

Sorties de Z5:
- Z2 Track Runtime: invalidation/refresh/routing info.
- Z3 Param/Control: ecritures globales et track-aware, sync UI active.
- Z4 Seq/Clock: transport, edit seq, pattern operations.
- Z6 Storage: recall/store pattern, undo.
- Z1 indirect via commandes runtime (ex: master buffer routing/record).

## 10. Dette technique observee

Points factuels:
- `ui_core.c` tres central (etat global + orchestration shortcuts + clipboard + pattern/mute + ponts inter-zones).
- Logique de navigation distribuee entre `ui_navigation`, `ui_template_page`, et cas speciaux dans `ui_core`.
- Couplage fort a `track_runtime`, `param_registry`, `seq_*`, `pattern_live_*` depuis Z5.
- Dependance implicite a l'ordre d'appel superloop (`service_track_selection_inputs` avant `hall_keyboard_bridge_process`) pour suppression hall coherent.
- Cas speciaux Master/Buffer reels dans UI (routing hall en mode ARP + shortcuts REC), transverse mais restant dans frontiere Z5 comme logique d'interaction.

## 11. Impact eventuel sur la cartographie globale

- Z5 est confirmee comme zone d'orchestration interactionnelle centrale, avec sous-composants internes: event queue, page manager, template resolver.
- `ui_page_manager` et `ui_event` doivent etre rattaches explicitement a Z5 dans la carte globale (pas des utilitaires neutres).
- Le cas Master/Buffer reste transverse Z5<->Z1/Z2/Z4 mais ne justifie pas une zone UI separee.
