# Z5 - UI / Navigation / Interaction

## 1. Perimetre

Perimetre operationnel de zone (appartient a Z5):
- `Src/UI/ui_core.c`
- `Src/UI/ui_core_runtime_bridge.c`
- `Src/UI/ui_core_navigation_bridge.c`
- `Src/UI/ui_hall_mode_contract.c`
- `Src/UI/ui_hall_mode_flow.c`
- `Src/UI/ui_hall_mode_projection.c`
- `Src/UI/ui_hall_mode_state.c`
- `Src/UI/ui_hall_input_service.c`
- `Inc/Core/track_state.h`
- `Src/Core/track_state.c`
- `Inc/UI/ui_core.h`
- `Inc/UI/ui_core_runtime_bridge.h`
- `Inc/UI/ui_core_navigation_bridge.h`
- `Inc/UI/ui_hall_mode_contract.h`
- `Inc/UI/ui_hall_mode_flow.h`
- `Inc/UI/ui_hall_mode_projection.h`
- `Inc/UI/ui_hall_mode_state.h`
- `Inc/UI/ui_hall_input_service.h`
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
- `sd_multitrack_recorder` hors integration UI/produit a date: aucun workflow UI canonique start/stop/arm n'est expose tant que le bypass compile-time `SD_RECORDER_PRODUCT_ENABLED=0` reste actif. `Master/Buffer` reste un flux distinct et demeure expose.

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
- Selectionnabilite subpage template: `ui_template_page_is_subpage_selectable`.
  - Une subpage desactivee par le resolver local ou vide (`PARAM_COUNT` sur tous les slots et titre vide/`-`/`N/A`) n'est pas selectionnable.
  - `ui_template_page_normalize_active_subpage` garantit que `active_subpage` pointe vers une subpage selectionnable; si aucune subpage ne l'est dans l'ensemble, le fallback borne reste la page `0`.

Autorite track select:
- `ui_core_service_track_selection_inputs` + `ui_core_handle_track_hall_action` + `ui_core_set_active_track`.
- Changement track actif explicite localement:
  - `ui_core_set_active_track`: selection legere (focus UI) + sync context UI explicite.
  - le pipeline `ui_system_sync_internal` n'est pas utilise pour la selection legere de track.

Autorite hall modes:
- Chemin central: `ui_set_hall_mode` (validation transition + forced clears mute/pattern + callback keyboard runtime + commit mode).
- Les transitions mute/pattern passent par `ui_set_hall_mode`; pas de chemin local direct autoritatif concurrent.
- Triggers SHIFT+HALL dans `ui_core_handle_shift_hall_action`.
- Mode hall natif `MACRO` (raw mode) expose en Z5:
  - trigger: `SHIFT + HALL 15`,
  - double tap `SHIFT + HALL 15`: ouvre la page hall mode dediee `Macro CFG` via la cible de contrat hall mode,
  - page `Macro CFG` UI-only:
    - une seule page `MODE`,
    - parametre local unique `Mode` (`Scene`/`Switch`),
    - valeur par defaut `Scene`,
    - rendu via le meme pipeline template natif (`ui_template_page` + `ui_renderer_template`) que les autres ensembles,
    - aucun write runtime/persistence/undo.
  - vitrine `Macro` = `Macro CFG` du systeme natif, sans owner local.
  - scope borne UI-only (aucune autorite runtime macro, aucune persistence macro).
- Le geste d'assignation MACRO vit dans `ui_macro_interaction`:
  - `SHIFT` absent,
  - `Scene` mode: maintien hall -> selection de la scene 0..15 -> capture encoder sans write live -> relâchement -> écriture/mise à jour d'un lock dans la scene projet,
  - chaque scene contient jusqu'a 32 locks; une scene pleine refuse l'ajout d'un nouveau lock sans parcours non borne,
  - `Scene` mode + `SHIFT` maintenu pendant un edit encodeur: suppression du lock `track+param` correspondant si present, no-op propre sinon,
  - le feedback template teste chaque parametre visible contre tous les locks de la scene maintenue; plusieurs params lockes visibles peuvent donc etre encadres simultanement,
  - pendant un maintien de scene MACRO, tourner un macro pot lie ce pot a cette scene sans appliquer le morph audio du pot,
  - `Switch` mode: maintien hall -> morph momentane base -> scene 0..15 selon la position hall; relâchement -> retrait de cette source,
  - pendant un maintien de scene MACRO, la grammaire visuelle réutilise le modèle p-lock: paramètre présent sur la page = cadre slot-lock inversé, sinon fallback LED orange sur l'ensemble cible,
  - état de capture purement transitoire, reset aux changements de hall mode, de `Mode`, ou à l'ouverture de `Macro CFG`.
- En mode brut `MACRO`, l'UI standard reste intacte; la surcouche MACRO ne prend pas le contrôle global des encodeurs ni du track-select.
- La capture MACRO ne s'active que pendant un maintien de scene en `Mode=Scene`, puis se finalise au relâchement.
- `ui_macro_ui` n'est plus un owner de fait; les call-sites UI passent par `project_v1` pour le modele MACRO.
- `KEYBOARD` reste un mode brut normal.
- `ARP` sur track `Master/Buffer` est expose comme `ROUT_VIEW` via le resolver central `ui_hall_mode_resolve_effective_view`.
- Les contextes `ROUT` visibles sont resolus par `ui_hall_mode_resolve_rout_context`: `Master/Buffer` et `Master/FX` partagent le label `ROUT`, mais gardent des etats/actions/renderers LED distincts.
- Le mode brut persiste en `ARP`; `ROUT` n'est jamais un mode brut stocke.
- Le feedback LED MACRO lit directement `project_v1` pour l'etat vide/non vide des scenes et `ui_macro_interaction` pour la scene maintenue; la couleur de base vient d'une table stable scene -> couleur dans `led_rgb.c`, sans encoder les locks ni revenir au mapping pot/slot.

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
  - `main` ne pilote plus `ui_tasklet_poll` en 1:1 avec `engine_tick_count`: la cadence UI est sous-echantillonnee par un diviseur explicite et le rattrapage par boucle principale reste borne.

Entrees evenementielles:
- `ui_event_from_inputs` (buttons + hall) alimente queue lue par `ui_core_tick`.
- Les deltas encodeur sont pris sur un snapshot local du bank/track actif au debut du tick UI, pour que plusieurs edits arrives dans la meme passe restent deterministes si un edit reconfigure le contexte.
- Control All relatif:
  - quand `TRACK` est maintenu, un edit encodeur de base sur un parametre track-aware applique le meme delta demande aux autres tracks compatibles,
  - le point d'insertion reste `ui_param_handle_encoder_with_context` apres resolution du parametre/bank actif; Z3 reste l'autorite d'apply via `param_registry_apply_track_edit`,
  - le delta est relatif (`delta * step`) et chaque track clamp individuellement selon ses bornes effectives; une track active deja en butee n'empeche pas les autres tracks de bouger,
  - seules les tracks ou le meme `param_id` est autorise par `track_runtime_get_effective_param_status` et, pour `COLORS`/`TONE`/`PLAY`/`MOD`, resolu par `seq_param_iface` sont touchees,
  - les tracks `Master`, les params globaux, les params `BUFFER`, les edits CFG structurels, les p-locks de steps et le live-rec p-lock restent exclus.
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
- Ecriture: register families, `ui_template_page_select_subpage`, `ui_template_page_normalize_active_subpage`, enter handlers.
- Lecture: renderer template, clipboard active page.

Etat queue events UI:
- `g_ui_evt_q`, `g_ui_evt_w`, `g_ui_evt_r`, `g_ui_hall_prev_pressed` dans `ui_event.c`.
- Ecriture: `ui_event_from_inputs`.
- Lecture: `ui_event_pop` dans `ui_core_tick`.

Frontiere commit runtime UI:
- `ui_core_runtime_bridge` porte les effets runtime, la transition track structurelle et les callbacks de commit explicitement appeles depuis `ui_core`.
- `ui_core_runtime_bridge` porte aussi le post-commit visible UI: miroir active-track, sync edit-context, mirror MIDI actif et reconfiguration post-structure.
- `ui_core_runtime_bridge` porte aussi les lectures runtime encore consommees par l'UI centrale (`seq_edit_step_hold_update`, `track_runtime_resolve_track`, `seq_edit_get_page`, `keyboard_runtime_get_octave_shift`, `pattern_live_*`) afin de garder `ui_core.c` du cote arbitrage.
- `ui_hall_mode_contract` porte la table de contrat hall mode centrale (trigger, page cible, label brut).
- `ui_hall_mode_flow` porte la politique d'activation hall et le double-tap/ciblage associe.
- `ui_hall_mode_projection` porte la vue effective, les labels et les helpers de lecture/injection du domaine hall.
- `ui_hall_mode_state` porte l'autorite brute du hall_mode et les transitions d'ecriture.
- `ui_hall_input_service` porte le chemin hors queue hall/transpose et delegue les effets aux helpers de domaine.
- `ui_core.c` conserve l'arbitrage et la decision; le bridge porte l'execution des actions runtime et la sync post-commit.

Frontiere navigation UI:
- `ui_core_navigation_bridge` porte les requetes de page et le dispatch navigation depuis `ui_core`/`ui_edit_context_sync`.
- `ui_core.c` ne porte plus les details des requetes de page cible ni le sync contextualise de navigation.
- `ui_hall_mode_contract.c` centralise la metadonnee de mode hall et les helpers de contrat associes.
- `ui_hall_mode_flow.c` centralise la transition/activation des hall modes, en gardant `ui_core` au niveau orchestration.
- `ui_hall_mode_projection.c` centralise la projection visible hall, sans couture dans `ui_core`.

## 6. Flux runtime

Flux nominal prouve:
1. Entree input UI
- Buttons/hall lus dans `ui_event_from_inputs` + `ui_core_service_track_selection_inputs` (path direct track-select/shift/hall).

2. Resolution navigation / raccourci
- Dans `ui_core_service_track_selection_inputs`: SHIFT+HALL => mode trigger; TRACK_MOD+HALL => active track.
- Contrat explicite track halls sous `TRACK`:
  - `TRACK` + tap track seul = focus/select track-aware historique uniquement.
  - `TRACK` + simple tap ne demande jamais `CFG`; l'ouverture `CFG` sous `TRACK` reste reservee au double tap explicite sur la track deja focus.
  - la mutation de chaine de voix ne s'arme que si une autre hall track est deja maintenue au moment du nouvel appui:
    `TRACK` maintenu + master candidate maintenue + target press.
  - sans seconde hall maintenue, aucun role `Solo/Master/Slave` ne change.
  - le geste groupe conserve les validations locales existantes: ajout contigu a droite, retrait uniquement sur la derniere slave, refus sans auto-fill ni mutation.
  - ajout special sur target `Off`: avant de devenir `Slave`, la target recoit une copie ponctuelle de l'etat instrument/per-track de la master candidate (family/type, config MIDI et params track-aware hors domaine `PLAY`), sans copie de sequence/trigs/steps/plocks `PLAY`.
- Contrat specifique `MACRO`: `SHIFT+HALL15` arme le raw mode `MACRO`; double tap ouvre la page dediee `Macro` (cible de contrat), sans effet runtime cache.
- Grammaire visuelle halls en `MACRO`:
  - les 16 halls adressent les 16 scenes et chaque scene a une couleur stable dediee,
  - `Mode = Scene`: scene vide=couleur dediee faible, scene non vide=couleur dediee normale, scene maintenue=couleur dediee forte,
  - `Mode = Switch`: scene vide/non vide garde la meme identite couleur, avec intensite liee a la pression hall quand disponible,
  - les Hall LEDs n'affichent jamais les 32 locks d'une scene, ni leur nombre, ni leur type.
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
- Contrat subpage selectionnable:
  - visible dans une famille/template ne signifie pas selectionnable si la subpage est vide ou desactivee,
  - les boutons de page ne peuvent pas selectionner une subpage vide,
  - l'entree/reload d'ecran, le tick template, la sync active-track et les changements de famille/resolver normalisent le focus vers une subpage selectionnable,
  - les contextes dynamiques (`COLORS` EQ3/ADSR, `MIX 2/2` delay CLASSIC/DUAL, pages moteur/type track-aware) passent par cette normalisation centrale apres recomposition de leurs familles.
- Resolution contextuelle `Master/Buffer -> ROUT` (propre):
  - page/template ARP: `ui_page_template_arp_resolve_family` lit `ui_hall_mode_resolve_effective_view(...)` pour choisir ARP vs ROUT,
  - label mode hall: `ui_get_hall_mode_short_label` et suffixe s'appuient sur `effective_view`.
  - renderer LED: `led_rgb` lit le contexte ROUT explicite, puis garde le renderer existant `Master/Buffer` base sur `brick6_master_buffer_get_source_enabled`.
  - le hall de la track `Master/Buffer` active est affiche en vert fonce comme destination courante et son toggle est ignore.
- Resolution contextuelle `Master/Buffer -> TONE`:
  - la famille template buffer garde `REC` et `FADE`,
  - les controles stretch restent dans `TONE` via sous-pages buffer-specifiques `STR` et `SYNC`,
  - `STR` expose `TStr`, `Grain` et `Hop` pour `Master/Buffer`,
  - `SYNC` expose `Sync Len` et `Src BPM` uniquement pour `Master/Buffer`,
  - aucun nouveau hall mode ni deplacement de `ROUT`.

5. Appels vers param/runtime/seq/storage
- Selon handler: apply params, track config, seq edits, pattern queue/store, undo, settings, master buffer routing/record.
- Cas speciaux locaux `ui_core` (non resolver):
  - `ui_core_handle_master_buffer_routing_event`: en `ARP` + `MASTER/BUFFER`, les `HALL_PRESS` togglent `brick6_master_buffer_set_source_enabled` et sont consommes,
  - `ui_core_handle_transport_event`: shortcut REC/CLEAR buffer sous conditions (`track_select_armed` + track buffer unique).

6. Feedback / consommation aval
- Feedback texte via `ui_core_set_feedback` visible dans header track.
- Le header template affiche la charge CPU moyenne uniquement; il ne lit plus de VU/peak/clip meter audio.
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
- Le service `ui_core_tick` n'est plus cadence a chaque tick moteur audio: la boucle principale agrège les ticks moteur et ne laisse passer qu'un tick UI sur N, avec budget de rattrapage borne par tour de superloop, afin de garantir du temps CPU au rendu/flush OLED.

Dependances de cadence:
- `ui_core_service_track_selection_inputs` doit tourner regulierement pour detection taps/double taps/armed states.
- `ui_core_tick` doit etre appele regulierement pour drainer queue events et tick page active.

## 8. Invariants a ne pas casser

Invariants prouves:
- Autorite unique etat UI courant: `g_ui_track_state` centralise dans `ui_core.c`.
- Autorite unique etat track: `track_state` centralise family/type/midi en dehors de `ui_core.c`.
- Une subpage template vide ne doit pas rester focus si une subpage selectionnable existe dans le meme ensemble.
- Tout changement de contexte track/type/mode qui peut modifier les subpages selectionnables doit repasser par `ui_template_page_normalize_active_subpage` via enter/tick/sync active-context ou appel equivalent.
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
- Le recorder SD/stems n'est pas un cas special UI actif dans l'etat produit courant: sa reactivation devra passer par une integration UI/produit explicite, puis par validation start/stop/arm et revalidation IRQ/audio.
- Le comportement `ROUT` n'est pas une sous-machine dediee: c'est une interpretation contextuelle de `ARP` avec contexte explicite (`MASTER/BUFFER` ou `MASTER/FX`) et gardes locaux separes.
- Fragilites restantes prouvees:
  - priorites de consommation toujours tres centralisees dans `ui_core_tick` (desormais explicites via table locale),
  - contrat hors queue (`ui_core_service_track_selection_inputs`) restant critique pour la coherence mode/track avant bridge hall.

## 11. Impact eventuel sur la cartographie globale

- Z5 est confirmee comme zone d'orchestration interactionnelle centrale, avec sous-composants internes: event queue, page manager, template resolver.
- `ui_page_manager` et `ui_event` doivent etre rattaches explicitement a Z5 dans la carte globale (pas des utilitaires neutres).
- Le cas Master/Buffer reste transverse Z5<->Z1/Z2/Z4 mais ne justifie pas une zone UI separee.

## 17. Contrat UI Master/FX
- `Master/FX` est un type de la family `Master` expose en `CFG`.
- `TONE` expose 4 pages de 4 slots:
  - `FX1`: `FX1`, `LVL`, macro A, macro B
  - `FX2`: `FX2`, `LVL`, macro A, macro B
  - `FX3`: `FX3`, `LVL`, macro A, macro B
  - `FX4`: `FX4`, `LVL`, macro A, macro B
- Les labels visibles des macros A/B changent selon le type FX: OFF `---/---`, DRIVE `TONE/SHAPE`, CRUSH `BITS/RATE`, PUMP `RATE/REL`, CHOP `RATE/SHAPE`, ECHO `TIME/FB`, WOBBLE `RATE/DEPTH`, COMB `TUNE/FB`, RING `FREQ/COLOR`, PITCH `SEMI/FINE`, TALK `VOWL/TONE`, STUTTER `SIZE/RATE`, FREEZE `TIME/HOLD`.
- `RING COLOR` expose quatre positions nettes `SIN/TRI/SQR/DIRT`.
- Les valeurs visibles de `LVL` et des macros A/B sont formatees par la page TONE selon le type FX courant et le mapping DSP reel, sans modifier le stockage `0..127` ni les plages DSP.
- Les macros Master/FX labelisees discretes sont editees par steps UI locaux dans `ui_param`: l'encodeur convertit step discret vers valeur raw canonique `0..127`, puis le chemin param track-aware standard applique la valeur.
- `LVL` et les macros A/B gardent le rendu parametre standard du template, avec widget potard normal; la contextualisation ne doit remplacer que le nom et le texte de valeur.
- `ARP` brut est projete en `ROUT` pour Master/FX. L'etat ROUT Master/FX est UI-only local et ne modifie pas le routing audio.
- Le renderer LED Master/FX ROUT utilise l'etat UI-only `g_master_fx_route_enabled[]` via le runtime bridge; il n'utilise pas le renderer ARP ni l'etat `Master/Buffer`.
- Le hall de la track `Master/FX` active est affiche en vert fonce comme destination courante et son toggle est ignore.
- Les series DSP Master/FX cablees en Z1 sont `DRIVE`, `CRUSH`, `RING`, `CHOP`, `PUMP`, `COMB`, `WOBBLE`, `ECHO`, `FREEZE`, `STUTTER`, `TALK` et `PITCH`. Les labels UI existants restent l'autorite visible de mapping A/B; `FILTER`, `REVERB` et `REVERSE` restent absents de la grammaire MacroFX.


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
- Contrat KBD/canal partage:
  - le KBD interne emet sur le canal MIDI de la track focus/play-owner,
  - le routage note interne est canal-aware: toutes les tracks moteur ou `Input/Hybrid` en source `INT`/`ALL` qui partagent ce canal recoivent note-on/note-off,
  - ce routage reutilise le meme dispatch track-aware que l'entree MIDI externe, avec dedoublonnage par owner de voice group pour eviter un double trigger de la source.

## 13. Contrat Hybrid UI v1 (borne)
- `Hybrid` n'est pas une nouvelle family: `family=Input1..4`, `type=Hybrid`.
- Exposition UI pour `Input/Hybrid`:
  - exposes: `PLAY`, `MOD`, `TONE`, `VCA`, `COLORS`, `MIX`, `CFG`.
- `TONE` Hybrid utilise une famille template dediee (pas fallback Synth):
  - page `PROG`: `Gate` + `Program`,
  - pages suivantes: `CC1`, `CC2`, `CC3`.
- `PLAY` est explicitement navigable pour `Input/Hybrid`.

## 13.b Contrat COLORS UI
- L'ensemble `COLORS` conserve uniquement les pages filtre utiles:
  - `MAIN`: `F Type`, cutoff/low, resonance/mid, `EG Amt`/high selon type de filtre,
  - `ADSR`: `Atk`, `Dec`, `Sus`, `Rel` uniquement pour les filtres biquad avec envelope,
  - les slots 3 et 4 restent vides (`-`).
- L'ancienne page 4 `CRUNCH` est retiree et ne doit plus exposer `Drive`, `Bits`, `Rate` ni `Rate2`.
- Aucun fallback de page ou preset CRUNCH n'est conserve.

## 14. Contrat Sampler v0
- `UI_TRACK_FAMILY_SAMPLER` est exposee en `CFG`.
- `UI_TRACK_TYPE_ONE_SHOT` est le type canonique de cette famille; `UI_TRACK_TYPE_SAMPLER` reste un alias de compat snapshot.
- `UI_TRACK_TYPE_CLIP` est expose comme type produit distinct dans la meme famille.
- `UI_TRACK_TYPE_CLIP` est borne a `BRICK6_MAX_CLIP_TRACKS=4` tracks simultanees: si 4 tracks sont deja `Clip`, le catalogue `CFG` cesse de le proposer aux autres tracks, tout en le laissant visible/editable pour une track deja `Clip`.
- Le rendu UI complet du Sampler expose maintenant deux pages Tone de base:
  - `PLAY`: `Sample`, `Gain`, `Start`, `End`,
  - `FX`: `Mode`, `Tune`, `Fade In`, `Fade Out`.
- Les modes produits exposes pour `OneShot` sont bornes a `Shot`, `RevShot`, `Loop`, `PingPong`.
- Le rendu UI `Clip` expose quatre pages Tone dediees:
  - `PLAY`: `Sample`, `Gain`, `Src BPM`,
  - `CLIP`: `Play Mode`, `Loop`, `Stretch`,
  - `SYNC`: `Sync Len`,
  - `STR`: `Grain`; `Hop` et `Search` restent reserves/non exposes produit.
- `Stretch=Off` lit le clip a vitesse/pitch d'origine sans tempo-sync ni moteur stretch.
- `Stretch=Speed` conserve le varispeed courant; `Stretch=Shifter` conserve le cursor Speed puis applique le shifter stereo local.
- En `Shifter`, `Grain` pilote la taille de fenetre; `Hop` et `Search` restent stockes mais non exposes et sans effet DSP.
- `STR` utilise les valeurs bornees `Grain = 384/512/768/1024/1536/2048`, avec default `Grain=1536`.
- La rotation du parametre `Sample` dans `TONE` met seulement a jour l'etat runtime, sans preview audio implicite.
- `Slice` / `RevSlice` restent en compat legacy interne uniquement, hors navigation produit `OneShot`.
- `Settings > SAMPLER` porte la preecoute SD manuelle via le flux `PREVIEW / STOP`.
- La preecoute s'arrete au changement de selection, au retour/back, et avant `Load/Replace`.
- `Load/Replace` reste l'autorite d'import vers le pool projet; la preview reste hors slots projet.
- Les etats visibles de slot Sampler suivent `sample_pool_get_state()`:
  - `LOADED` pour `READY_FULL`, `READY_PARTIAL` jouable et `PLAYING`,
  - `PREP` pour `PREPARING`, `PREFILLING`, `DONE`, `UNDERRUN`, `NEEDS_REPREPARE`,
  - `ERROR` pour une faute cache/SD/format,
  - `MISSING` reserve au catalogue/path absent ou fichier manquant au load/restore.
- Invariants conserves:
  - pas de seconde autorite UI,
  - pas de refonte de page,
  - pas de nouveau flux de navigation autonome.
- Compat UI/restore:
  - un ancien couple `Synth/Sampler` est remappe vers `Sampler/OneShot`,
  - la famille `Synth` propose `Opal` et `Braids`,
  - `Braids` peut être sélectionné sur plusieurs tracks `Synth` (dans la limite runtime `BRICK6_BRAIDS_MAX_INSTANCES`).



## 14.b Contrat UI Braids
- `Synth/Braids` reste dans l'ensemble `TONE`, sans UI Mutable originale ni mode global dédié.
- La famille template `TONE` Braids expose exactement 7 params dans l'ordre runtime:
  - `EDIT`, `FINE`, `COARSE`, `FM`, `TIMBRE`, `MODULATION`, `COLOR`.
- Le layout UI suit deux sous-pages:
  - `EDIT`: `EDIT`, `FINE`, `COARSE`, `FM`
  - `TONE`: `TIMBRE`, `MODULATION`, `COLOR`
- L'ordre visible doit rester aligné avec `track_runtime_tone_slot_to_param()` / `track_runtime_tone_param_to_slot()` pour le type runtime `Braids`.
- Le clavier live réutilise le même seam track-aware que le scheduler:
  - `note on/off` Braids passent par `keyboard_engine` puis `brick6_braids_runtime`
  - aucun chemin UI local parallèle n'est autorisé pour le jeu de notes.


## 14.c Contrat MIX send2 delay
- La page template `MIX` conserve la page track-aware `MIX`: `Level`, `Pan`, `Send1`, `Send2`.
- L'ensemble `MIX` est scinde en deux sous-ensembles locaux UI-only:
  - appui `MIX` depuis un autre ensemble: ouvre `MIX 1/2`, subpage `MIX`,
  - appui `MIX` depuis `MIX`: alterne `MIX 1/2` / `MIX 2/2`,
  - le changement de sous-ensemble conserve la subpage active si elle reste disponible, sinon revient a `0`.
- `MIX 1/2` expose:
  - `MIX`: `Level`, `Pan`, `Send1`, `Send2`,
  - `REVB`: `Wet`, `Size`, `Decay`, `PreD`,
  - `REV2`: `Type`, `Surr`,
  - `REV3`: `HPF`, `LPF`.
- Le champ `REV2/Type` expose `DRUMBOY`, `RevB`, `GVERB` et `OLIVERB`; `DRUMBOY` reste le default, `RevB`, `GVERB` et `OLIVERB` sont experimentaux et ne changent pas la surface UI.
- Les params delay globaux sont exposes dans `MIX 2/2`, sans nouveau mode UI.
- `Send2` reste le niveau par track vers le delay global; `VOL` reste le niveau global de retour wet master et `REV` le send wet delay vers la reverb globale.
- Le delay global expose une surface `MIX 2/2` contextuelle selon `TYPE`:
  - CLASSIC `DLY1`: `TYPE`, `TIME`, `X`, `VOL`,
  - CLASSIC `DLY2`: `HPF`, `LPF`, `REV`, `FDBK`,
  - DUAL `DLY1`: `TYPE`, `TIME`, `MODE`, `VOL`,
  - DUAL `DLY2`: `HPF`, `LPF`, `REV`, `FDBK`,
  - DUAL `DLY3`: `TIME_R`, `WID`, `FBW`, `MOD`,
  - DUAL `DLY4`: `M.RATE`.
- `TYPE=CLASSIC` expose 2 pages delay; `TYPE=DUAL` expose 4 pages delay.
- `TYPE=CLASSIC` reste le default visible et conserve l'ancien controle `X`.
- `TYPE=DUAL` substitue `MODE` au slot de `X`; `MODE` propose `Normal`, `PingPong`, `Tap`, `ClassicPP`.
- `TIME_R` et `WID` sont visibles uniquement en DUAL; en `Tap`, `TIME_R` sert de temps principal.
- `SWING` et `ACCENT` sont retires de la surface delay produit V1.

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


## 9. Contrat query stricte - filter target
- `ui_resolve_filter_target_track` porte l'explicitation du refresh avant la query.
- `ui_core_runtime_bridge_resolve_filter_target_track` reste une query pure et ne refresh plus au passage.

## 10. Contrat transport explicite
- Le seam transport du runtime bridge est d�sormais d�coup� en commandes explicites PLAY / REC / pattern shortcut.
- L'arbritrage reste c�t� UI; le bridge porte seulement l'ex�cution des commandes et leurs feedbacks associ�s.

## 18. Contrat UI Drum Plaits direct

- Les types Drum legacy restent presents dans `CFG` pour compatibilite avec les tracks/projets existants.
- La page TONE Drum est neutralisee: elle garde une sous-page vide pour ne pas casser la navigation, mais n'expose plus les params `TRX_*`/`FM_*` comme controles produit actifs.
- `BD Analog` est le premier type Drum propre et experimental; il est ajoute apres les tombstones dans le catalogue `Drum`, donc il n'est pas le default tant que les projets existants gardent leur premier type legacy.
- Pour `BD Analog`, `TONE` expose une sous-page legere `BD`: `Pitch`, `Decay`, `Tone`, `FM`.
- `PLAY`, `COLORS`, `MIX`, `MOD` et `VCA` restent les ensembles communs existants; aucune page UI lourde ni chemin de modulation local n'est ajoute.
