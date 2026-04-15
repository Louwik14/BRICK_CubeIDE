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
- `Src/UI/ui_system_sync_internal.c` + `Src/UI/ui_system_sync_internal.h`: noyau interne de sync systeme track/config (module prive Z5).
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
- Changement track actif explicite localement:
  - `ui_core_set_active_track`: same-track => resync only.
  - changement reel => callback keyboard runtime -> mutation `active_track` -> sync systeme via `ui_system_sync_apply_track_context_change`.

Autorite hall modes:
- Chemin central: `ui_set_hall_mode` (validation transition + forced clears mute/pattern + callback keyboard runtime + commit mode).
- Les transitions mute/pattern passent par `ui_set_hall_mode`; pas de chemin local direct autoritatif concurrent.
- Triggers SHIFT+HALL dans `ui_core_handle_shift_hall_action`.
- `KEYBOARD` reste un mode normal (pas de remap `Master/Buffer -> ROUT` sur ce mode).
- `ARP` sur track `Master/Buffer` devient contextuellement `ROUT` (resolution page/label + gardes runtime).

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
- `ui_core_tick` materialise la politique des stages consommants via `k_event_stages[]` (ordre stabilise): mute -> consommations hall track-select -> master-buffer routing -> transport -> settings -> global shortcuts -> pattern mode -> seq mode.
- `track_selection` reste hors table et execute en amont.
- `navigation` puis `active_page->handle_event` restent en fin de chaine.

Contrats implicites d'ordre:
- L'ordre `ui_core_service_track_selection_inputs()` puis `hall_keyboard_bridge_process()` (dans `brick6_app_process`) garantit que suppression hall est fixee avant emission notes clavier.
- Dans `ui_core_tick`, la table de stages + blocage aval (equivalent `continue`) impose la priorite de consommation.
- `ui_navigation_handle_event` est volontairement execute avant `ui_page_get()->handle_event` pour que le meme event soit traite par la page active apres navigation.
- Le contrat `Master/Buffer -> ROUT` repose aussi sur cette mise a jour mode/track avant bridge (etat lu dans le meme tour par `hall_keyboard_bridge_process`).

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
- Noyau de sync systeme track/config factorise dans `ui_system_sync_internal` (profils `ui_system_sync_make_request_*` + apply unique) pour 4 chemins: active-track, family change, type change, restore bulk.
- Durcissement du module prive: une requete de sync invalide (callbacks adapteur requis manquants) est rejetee sans execution partielle.
- Restore bulk track config:
  - validation snapshot all-or-nothing,
  - ecriture `g_ui_track_state.*`,
  - pipeline post-apply localise: `ui_core_restore_post_apply_sync_and_notify()`
    (`track_runtime_invalidate_all` -> `ui_core_sync_audio_runtime_enables` -> `keyboard_runtime_on_active_track_changed` -> `ui_core_sync_active_track_cfg_params`).

4. Resolution contextuelle page/ensemble
- `ui_navigation_handle_event` mappe boutons param -> page cible selon disponibilite track-family/type.
- `ui_template_page` resout family/subpage active et banque param associee.
- Resolution contextuelle `Master/Buffer -> ROUT` (propre):
  - page ARP: `ui_page_template_arp_register_families` associe `MASTER+BUFFER` a une famille template `ROUT`,
  - label mode hall: `ui_get_hall_mode_short_label` affiche `ROUT` en `hall_mode==ARP` sur `MASTER+BUFFER`.

5. Appels vers param/runtime/seq/storage
- Selon handler: apply params, track config, seq edits, pattern queue/store, undo, settings, master buffer routing/record.
- Cas speciaux locaux `ui_core` (non resolver):
  - `ui_core_handle_master_buffer_routing_event`: en `ARP` + `MASTER/BUFFER`, les `HALL_PRESS` togglent `brick6_master_buffer_set_source_enabled` et sont consommes,
  - `ui_core_handle_transport_event`: shortcut REC/CLEAR buffer sous conditions (`track_select_armed` + track buffer unique).

6. Feedback / consommation aval
- Feedback texte via `ui_core_set_feedback` visible dans header track.
- `hall_keyboard_bridge` consomme hall mode + flags suppression pour autoriser/bloquer emission note hall.
- Gardes runtime explicites en contexte `ROUT`:
  - `hall_keyboard_bridge_process` bloque l'injection hall->keyboard en `ARP` si `keyboard_runtime_is_master_buffer_route_context()!=0`,
  - `keyboard_runtime_tick` n'active pas l'arp engine via `keyboard_runtime_hall_mode_uses_arp_engine` sur `MASTER/BUFFER`,
  - `keyboard_input_note_on/off/all_notes_off` neutralisent les sinks ARP en route context.

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
- Changement hall mode: chemin central via `ui_set_hall_mode` avec side effects explicites (mute/pattern inclus).
- Contrat d'ordre `ui_core_tick` explicite en code: `track_selection` amont, stages consommants ordonnes, puis `navigation` -> `active_page->handle_event`.
- Contrat d'ordre stabilise: `global_shortcuts` est prioritaire sur `pattern/seq/navigation/page` pour un event consomme.
- Navigation et logique runtime ne sont pas separees strictement: `ui_core` appelle directement seq/param/track_runtime/storage.
- Cohabitation hall modes / ensembles UI maintenue par resolver family/subpage et label suffix selon mode.
- Deviation `Master/Buffer` en ARP est fonctionnellement coherente mais distribuee:
  - resolution contextuelle propre (page/label/template),
  - cas speciaux locaux UI (`ui_core`),
  - gardes runtime explicites (`hall_keyboard_bridge` / `keyboard_runtime` / `keyboard_input`).
- Contrat d'ordre utile confirme: `ui_core_service_track_selection_inputs` precede `hall_keyboard_bridge_process`; sur les cas frontiere audites, pas de fuite hall/note/arp prouvee.

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
- Le comportement `ROUT` n'est pas une sous-machine dediee: c'est une interpretation contextuelle de `ARP` sur `MASTER/BUFFER`, renforcee par des gardes locaux.
- Fragilites restantes prouvees:
  - priorites de consommation toujours tres centralisees dans `ui_core_tick` (desormais explicites via table locale),
  - contrat hors queue (`ui_core_service_track_selection_inputs`) restant critique pour la coherence mode/track avant bridge hall.

## 11. Impact eventuel sur la cartographie globale

- Z5 est confirmee comme zone d'orchestration interactionnelle centrale, avec sous-composants internes: event queue, page manager, template resolver.
- `ui_page_manager` et `ui_event` doivent etre rattaches explicitement a Z5 dans la carte globale (pas des utilitaires neutres).
- Le cas Master/Buffer reste transverse Z5<->Z1/Z2/Z4 mais ne justifie pas une zone UI separee.

