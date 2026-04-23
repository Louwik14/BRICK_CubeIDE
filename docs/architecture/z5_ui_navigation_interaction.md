# Z5 - UI / Navigation / Interaction

## 1. Perimetre

Perimetre operationnel de zone (appartient a Z5):
- `Src/UI/ui_core.c`
- `Src/UI/ui_core_runtime_bridge.c`
- `Src/UI/ui_core_navigation_bridge.c`
- `Inc/Core/track_state.h`
- `Src/Core/track_state.c`
- `Inc/UI/ui_core.h`
- `Inc/UI/ui_core_runtime_bridge.h`
- `Inc/UI/ui_core_navigation_bridge.h`
- `Src/UI/ui_track_catalog.c`
- `Inc/UI/ui_track_catalog.h`
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
- Etat UI global courant (track, hall mode, feedback, states pattern/mute).
- Orchestration centrale des interactions (track select, transport, shortcuts, pattern mode).
- Orchestration explicite des contrats de sync (selection legere, sync edit-context, reconfig runtime).
- Arbitrage UI des demandes runtime, avec delegation des effets et commits a `ui_core_runtime_bridge`.

Policy catalogue track/type/labels:
- `ui_track_catalog` porte les regles de validite/disponibilite family/type et les labels associes.
- `ui_core` conserve l'etat UI live (selection, modes, feedback) tandis que `track_state` porte l'autorite par-track pour family/type/midi.

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
  - `ui_core_set_active_track`: selection legere (focus UI) + sync context UI explicite.
  - le pipeline `ui_system_sync_internal` n'est pas utilise pour la selection legere de track.

Autorite hall modes:
- Chemin central: `ui_set_hall_mode` (validation transition + forced clears mute/pattern + callback keyboard runtime + commit mode).
- Les transitions mute/pattern passent par `ui_set_hall_mode`; pas de chemin local direct autoritatif concurrent.
- Triggers SHIFT+HALL dans `ui_core_handle_shift_hall_action`.
- `KEYBOARD` reste un mode brut normal.
- `ARP` sur track `Master/Buffer` est expose comme `ROUT_VIEW` via le resolver central `ui_hall_mode_resolve_effective_view`.
- Le mode brut persiste en `ARP`; `ROUT` n'est jamais un mode brut stocke.

Autorite navigation boutons param:
- `ui_navigation_handle_event` (table `g_ui_nav_rules` data-driven).
- Disponibilité d'ensembles template:
  - `ui_navigation_is_page_available` combine:
    - contrat structurel Z2 (`track_runtime_is_ui_ensemble_available`),
    - résolution template locale (`ui_template_family_resolve_active_track`).
  - Z5 ne redécide plus seule la présence des ensembles principaux (`COLORS/TONE/MOD/MIX/PLAY/VCA`).

Autorite raccourcis interaction:
- `ui_core_handle_global_shortcuts`, `ui_core_handle_transport_event`, `ui_core_handle_seq_mode_event`, `ui_core_handle_pattern_mode_event`, `ui_core_mute_handle_event`.

Clipboard UI:
- `Src/UI/ui_core_clipboard.c` + `Inc/UI/ui_core_clipboard.h` (etat et handlers dedies).

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
- Z2: `track_runtime_is_ui_ensemble_available` pour le gating de disponibilité d'ensembles.
- Z2: `track_runtime_resolve_track` pour la resolution explicite de cible filter depuis `ui_core`.
- Z3: `param_set`, `param_registry_apply_track_edit`, `param_registry_apply_track_structure_transition`, `param_registry_batch_*`, `param_store_set_active`.
- Z4: `seq_runtime_toggle_play_stop`, `seq_runtime_rec_toggle_arm`, `seq_runtime_set_track_div/quant/swing`, `seq_edit_*`, `seq_model_*`.
- Storage: `pattern_live_queue_slot`, `pattern_live_capture_to_slot`, `undo_v1_restore`.
- Master buffer: `brick6_master_buffer_*` (routing hall en mode ARP master-buffer, REC shortcuts).

Getters non-mutants vs mutables:
- Non-mutants: `ui_get_active_track`, `ui_get_track_*`, `ui_get_hall_mode`, `ui_get_pattern_stub_state`, `ui_get_mute_state`, `ui_get_mute_hall_led`.
- Mutables: `ui_set_track_family/type`, `ui_set_track_midi_channel/source`, `ui_set_hall_mode`, clipboard/pattern/mute handlers.

## 5. Etats structurants possedes

Etat global UI:
- `g_ui_track_state` (`ui_track_state_t`) dans `ui_core.c` pour l'etat UI pur.
- `track_state` dans `Src/Core/track_state.c` pour l'autorite par-track.
- Champs structurants: active track, shift, track_select_armed, hall_mode, mode/cfg tap timers, hall suppression, pattern state, feedback, mute state/buffers, armement prepare du hold quick mute.
- Etat track autoritatif externe: family/type/midi channel/source dans `track_state`.
- Ecritures: init + handlers d'events/selection.
- Lectures: getters UI, renderer template, hall keyboard bridge, logique shortcuts.

Etat clipboard:
- `g_ui_clipboard` (`ui_clipboard_state_t`) dans `ui_core_clipboard.c` avec sous-etats track/ensemble/page.
- Ecriture: copy handlers dans `ui_core_clipboard.c`.
- Lecture: paste/clear handlers dans `ui_core_clipboard.c`.

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

Frontiere commit runtime UI:
- `ui_core_runtime_bridge` porte les effets runtime, la transition track structurelle et les callbacks de commit explicitement appeles depuis `ui_core`.
- `ui_core_runtime_bridge` porte aussi le post-commit visible UI: miroir active-track, sync edit-context, mirror MIDI actif et reconfiguration post-structure.
- `ui_core_runtime_bridge` porte aussi les lectures runtime encore consommees par l'UI centrale (`seq_edit_step_hold_update`, `track_runtime_resolve_track`, `seq_edit_get_page`, `keyboard_runtime_get_octave_shift`, `pattern_live_*`) afin de garder `ui_core.c` du cote arbitrage.
- `ui_core.c` conserve l'arbitrage et la decision; le bridge porte l'execution des actions runtime et la sync post-commit.

Frontiere navigation UI:
- `ui_core_navigation_bridge` porte les requetes de page et le dispatch navigation depuis `ui_core`/`ui_edit_context_sync`.
- `ui_core.c` ne porte plus les details des requetes de page cible ni le sync contextualise de navigation.

## 6. Flux runtime

Flux nominal prouve:
1. Entree input UI
- Buttons/hall lus dans `ui_event_from_inputs` + `ui_core_service_track_selection_inputs` (path direct track-select/shift/hall).

2. Resolution navigation / raccourci
- Dans `ui_core_service_track_selection_inputs`: SHIFT+HALL => mode trigger; TRACK_MOD+HALL => active track.
- Dans `ui_core_tick`: resolution priorisee mute/transport/shortcuts/pattern/seq/navigation.

3. Mutation etat UI
- Mutations de `g_ui_track_state` (hall_mode, track_select_armed, active_track, pattern/mute/feedback).
- Noyau de sync systeme track/config factorise dans `ui_system_sync_internal` (profils `ui_system_sync_make_request_*` + apply unique) pour 3 chemins runtime: family change, type change, restore bulk.
- Durcissement du module prive: une requete de sync invalide (callbacks adapteur requis manquants) est rejetee sans execution partielle.
- Les edits `CFG_TRACK` / `CFG_TRACK_TYPE` passent maintenant par le meme corridor structurel complet que `restore bulk`:
  capture des mix targets -> mutation dans le pipeline -> rebind lanes -> reapply lane-bound -> neutralisation runtime -> sync UI structurelle.
- Pour une mutation structurelle `CFG_TRACK` / `CFG_TRACK_TYPE` / restore bulk, Z5 delegue le corridor a Z3 via `param_registry_apply_track_structure_transition(...)`; la resync UI active-track reste explicite cote Z5 (`ui_param_sync_active_track_mirror_from_runtime` puis `ui_param_sync_active_bank_values`) apres finalisation runtime Z3.
- Restore bulk track config:
  - validation snapshot all-or-nothing,
  - ecriture de `track_state` comme autorite par-track,
  - pipeline post-apply localise: `ui_core_post_restore_global_sync()`
    (`ui_system_sync_make_request_restore_bulk` -> `ui_system_sync_apply_track_context_change` -> `ui_active_track_sync_after_track_structure_change(1)`).

4. Resolution contextuelle page/ensemble
- `ui_navigation_handle_event` mappe boutons param -> page cible selon disponibilite track-family/type.
- `ui_template_page` resout family/subpage active et banque param associee.
- Resolution contextuelle `Master/Buffer -> ROUT` (propre):
  - page/template ARP: `ui_page_template_arp_resolve_family` lit `ui_hall_mode_resolve_effective_view(...)` pour choisir ARP vs ROUT,
  - label mode hall: `ui_get_hall_mode_short_label` et suffixe s'appuient sur `effective_view`.

5. Appels vers param/runtime/seq/storage
- Selon handler: apply params, track config, seq edits, pattern queue/store, undo, settings, master buffer routing/record.
- Cas speciaux locaux `ui_core` (non resolver):
  - `ui_core_handle_master_buffer_routing_event`: en `ARP` + `MASTER/BUFFER`, les `HALL_PRESS` togglent `brick6_master_buffer_set_source_enabled` et sont consommes,
  - `ui_core_handle_transport_event`: shortcut REC/CLEAR buffer sous conditions (`track_select_armed` + track buffer unique).

6. Feedback / consommation aval
- Feedback texte via `ui_core_set_feedback` visible dans header track.
- `hall_keyboard_bridge` consomme hall mode + flags suppression pour autoriser/bloquer emission note hall.
- Gardes runtime explicites en contexte `ROUT`:
  - `hall_keyboard_bridge_process` utilise `ui_hall_allows_injection(...)`,
  - `keyboard_runtime_tick` utilise `ui_hall_uses_arp_engine(...)`,
  - `keyboard_input_note_on/off/all_notes_off` s'aligne sur `ui_hall_uses_arp_engine(...)` + `effective_view`.

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
- Autorite unique etat track: `track_state` centralise family/type/midi en dehors de `ui_core.c`.
- Mutation du mode brut: `ui_set_hall_mode` est l'unique mutateur de `hall_mode`.
- `effective_view` reste une projection read-only; aucune ecriture persistante.
- Resolution contextuelle track-aware: disponibilite pages/types depend de family/type de la track active (`ui_navigation_is_page_available`, `ui_template_family_resolve_active_track`).
- Priorite SHIFT/HALL sur track-select en service input: condition explicite `shift_down !=0` et `track_select_armed==0` avant trigger mode hall.
- Changement hall mode: chemin central via `ui_set_hall_mode` avec side effects explicites (mute/pattern inclus).
- Contrat d'ordre `ui_core_tick` explicite en code: `track_selection` amont, stages consommants ordonnes, puis `navigation` -> `active_page->handle_event`.
- `active_page->tick()` reste un tick de page local, pas un mecanisme de resync active-track.
- Contrat d'ordre stabilise: `global_shortcuts` est prioritaire sur `pattern/seq/navigation/page` pour un event consomme.
- Navigation et logique runtime ne sont pas separees strictement: `ui_core` appelle directement seq/param/track_runtime/storage.
- Cohabitation hall modes / ensembles UI maintenue par resolver family/subpage et label suffix selon mode.
- Deviation `Master/Buffer` en ARP est centralisee par `effective_view`:
  - page/label/template lisent la meme projection,
  - gardes runtime keyboard/hall consomment les helpers centraux.
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
- `ui_core.c` reste central pour l'UI, mais ne porte plus l'autorite par-track.
- `ui_core_runtime_bridge.c` concentre la couche d'execution UI -> runtime pour les mutations track, transport, shortcuts et remaps de commit.
- `ui_core_runtime_bridge.c` concentre aussi le post-commit UI associe aux changements structurels, au miroir de la track active, et aux lectures runtime encore utilisees par l'UI centrale.
- `ui_core_navigation_bridge.c` concentre la policy de requete/navigation appliquee depuis l'arbitrage UI.
- Logique de navigation distribuee entre `ui_navigation`, `ui_template_page`, et cas speciaux dans `ui_core`.
- Couplage fort a `param_registry`, `seq_*`, `pattern_live_*` depuis Z5; les lectures runtime restantes passent maintenant par `ui_core_runtime_bridge`.
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


## 12. Contrat MIDI UI v1 (canonique)
- Nouvelle source UI explicite: `UI_TRACK_FAMILY_MIDI` + `UI_TRACK_TYPE_MIDI` dans `ui_core`.
- Exposition navigation borne pour une track MIDI:
  - exposes: `PLAY`, `MOD`, `TONE`, `CFG`,
  - non exposes: `COLORS`, `MIX`, `VCA`.
- `CFG` reste l'autorite UI pour le channel MIDI (`PARAM_CFG_MIDI_CH`) ; aucun deplacement vers `TONE`.
- Resolution template contextuelle MIDI:
  - `TONE` utilise une famille template MIDI dediee (`PROG`, `CC1`, `CC2`, `CC3`) sans fallback audio Synth,
  - page `PROG`: `PARAM_MIDI_PROGRAM`,
  - pages `CCX`: `PARAM_MIDI_CC1_1..PARAM_MIDI_CC3_4`.
- Contrat runtime/UI associe:
  - track MIDI non audio-routable (pas de cible mix/filter audio),
  - Z5 reste source family/type; Z2 reste autorite unique du bind runtime.

## 13. Contrat Hybrid UI v1 (borne)
- `Hybrid` n'est pas une nouvelle family: `family=Input1..4`, `type=Hybrid`.
- Exposition UI pour `Input/Hybrid`:
  - exposes: `PLAY`, `MOD`, `TONE`, `VCA`, `COLORS`, `MIX`, `CFG`.
- `TONE` Hybrid utilise une famille template dediee (pas fallback Synth):
  - page `PROG`: `Gate` + `Program`,
  - pages suivantes: `CC1`, `CC2`, `CC3`.
- `PLAY` est explicitement navigable pour `Input/Hybrid`.

## 14. Contrat Sampler v0
- `UI_TRACK_TYPE_SAMPLER` est reconnu comme type de track synth pour le socle d'integration.
- Le rendu UI complet du Sampler expose maintenant deux pages Tone de base:
  - `PLAY`: `Sample`, `Gain`, `Start`, `End`,
  - `FX`: `Mode`, `Tune`, `Fade In`, `Fade Out`.
- La rotation du parametre `Sample` dans `TONE` met seulement a jour l'etat runtime, sans preview audio implicite.
- Un sous-onglet `SLICE` borne `Slice Count` sans editeur de slices.
- Cette passe ne cree pas de nouvelle navigation produit ni d'editor Slice.
- `Settings > SAMPLER` porte la preecoute SD manuelle via le flux `PREVIEW / STOP`.
- La preecoute s'arrete au changement de selection, au retour/back, et avant `Load/Replace`.
- `Load/Replace` reste l'autorite d'import vers le pool projet; la preview reste hors slots projet.
- Invariants conserves:
  - pas de seconde autorite UI,
  - pas de refonte de page,
  - pas de nouveau flux de navigation autonome.

## 15. Contrat UI Settings - Load Project
- `PROJECT > LOAD` expose une entree explicite `BLANK PROJECT` (index 0), distincte des slots SD.
- Action associee: appel direct `project_v1_load_blank()`.
- Les slots SD restent listes apres cette entree (index decales de +1).
- Le flux `PROJECT > MANAGE` reste reserve aux slots reels.

## 16. Contrat boot UI
- Etat boot UI voulu:
  - track active logique = track 1 (index 0),
  - ensemble/page active = `CFG` (`UI_PAGE_TEMPLATE_CFG`) en boot normal.
- Priorite hall/bootstrap:
  - `ui_bootstrap_init` pose l'etat initial `CALIBRATION`,
  - `brick6_app_init` decide ensuite selon `hall_calibration_load()`:
    - succes -> bascule vers `CFG`,
    - echec -> conserve `CALIBRATION`.
- Aucun fallback renderer n'est utilise pour masquer un etat UI invalide.

