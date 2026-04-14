# Z6 - State / Persistence / Patterns / Projects

## 1. Perimetre

Perimetre operationnel de zone (appartient a Z6):
- `Src/Storage/pattern_live_ram.c`
- `Src/Storage/pattern_sd_bank.c`
- `Src/Storage/project_v1.c`
- `Src/Storage/project_sd_bank.c`
- `Src/Storage/boot_context_flash.c`
- `Inc/Storage/pattern_live_ram.h`
- `Inc/Storage/pattern_sd_bank.h`
- `Inc/Storage/project_v1.h`
- `Inc/Storage/project_sd_bank.h`
- `Inc/Storage/boot_context_flash.h`

Elargissements necessaires (preuve de contrats et frontieres):
- `Src/Storage/sd_access_gate.c` + `Inc/Storage/sd_access_gate.h`: arbitrage d'acces SD entre clients PATTERN/PROJECT.
- `Src/Core/brick6_app_init.c`: preuve du wiring runtime (`pattern_live_init`, `project_v1_init`, `project_v1_restore_boot_context`, `pattern_live_service`).
- `Src/UI/ui_core.c`: preuve des appels UI vers `pattern_live_capture_to_slot` et `pattern_live_queue_slot`.
- `Src/UI/pages/ui_page_settings.c`: preuve des appels UI vers `project_v1_save_slot/load_slot/delete_slot`.
- `Src/Storage/undo_v1.c`: preuve de la sous-zone undo basee sur snapshot live.

Sous-roles internes identifies:
- `pattern_live_ram.c`: capture/apply snapshot live + gestion active/queued pattern + service de bascule a boundary.
- `project_v1.c`: orchestration projet (capture/apply/save/load/delete/active slot + boot context policy).

Dependances de Z6 sans appartenance:
- Z2 `track_runtime` (classification domaine param pendant capture/apply).
- Z3 `param_registry`, `param_store`, `mod_lfo_v1` (capture/apply params + modulation).
- Z4 `seq_runtime`, `seq_model`, `seq_output_guard`, `seq_param_iface` (etat seq capture/apply, transport, playhead, panic).
- Z5 `ui_core` (track config restore, active track playhead reference, commandes utilisateur).

Exclusions explicites:
- `Src/Storage/pattern_data.c`, `pattern_utils.c`: utilitaires/legacy non sur le flux snapshot-projet observe ici.
- `Src/Storage/sd_card.c`: couche peripherique SD hors autorite de la politique pattern/project.
- Zone audio hard-RT (`Src/Audio/*`): consommateur indirect des etats restaures, pas autorite persistence.

## 2. Autorite(s) de verite

Autorite snapshot live (pattern):
- Capture: `pattern_live_capture_current()`.
- Apply: `pattern_live_apply_snapshot()`.
- Selection pattern active/queued: `pattern_live_queue_slot()`, `pattern_live_service()`, `pattern_live_set_active_state()`.
- Restore LFO en apply live: une seule voie d'autorite (`mod_lfo_v1_set_track_param` depuis `pattern_live_apply_snapshot`).

Autorite persistence pattern bank SD:
- `pattern_sd_bank_store_slot[_nosync]()`, `pattern_sd_bank_load_slot()`, `pattern_sd_bank_delete_slot()`, `pattern_sd_bank_get_slot_checksum()`.

Autorite orchestration projet:
- `project_v1_capture_current()`.
- `project_v1_apply_snapshot()`.
- `project_v1_save_slot()`, `project_v1_load_slot()`, `project_v1_store_snapshot_to_slot()`, `project_v1_delete_slot()`.
- `project_v1_restore_boot_context()`.

Autorite persistence projet SD:
- `project_sd_bank_store_slot()`, `project_sd_bank_load_slot()`, `project_sd_bank_delete_slot()`, `project_sd_bank_is_slot_equivalent_to_live()`.

Autorite boot context flash:
- `boot_context_flash_load()`, `boot_context_flash_commit()`, `boot_context_flash_clear()`.

Seconde autorite concurrente:
- Aucune seconde autorite complete equivalente n'est observee pour le couple capture/apply live + save/load project.
- Sous-zone parallele partielle: `undo_v1` reutilise `pattern_live_capture_current/apply_snapshot`, mais ne remplace pas l'autorite pattern/project.

## 3. API entrantes

Entrees depuis init/runtime global:
- `brick6_app_init.c` appelle `pattern_live_init()`, `project_v1_init()`, `project_v1_restore_boot_context()`.
- Boucle superloop appelle `pattern_live_service()`.

Entrees depuis UI:
- `ui_core.c` appelle `pattern_live_capture_to_slot()` et `pattern_live_queue_slot()`.
- `ui_page_settings.c` appelle `project_v1_refresh_slots()`, `project_v1_list_slots()`, `project_v1_slot_has_data()`, `project_v1_load_slot()`, `project_v1_save_slot()`, `project_v1_delete_slot()`.

Entrees depuis undo:
- `undo_v1.c` appelle `pattern_live_capture_current()` et `pattern_live_apply_snapshot()`.

Contrats implicites observes:
- APIs `project_v1_*` mutantes et `undo_v1_*` refusent contexte ISR via `__get_IPSR()`.
- `pattern_live_service()` suppose appel periodique hors IRQ audio, avec seq runtime deja actif si queue presente.
- L'autorite stop/panic/reprise transport du restore live est centralisee dans `pattern_live_apply_snapshot()`.
- `pattern_live_queue_slot()` applique immediatement si transport stoppe, sinon differe au boundary.

Getters non-mutants:
- `pattern_live_get_active()`, `pattern_live_get_queued()`, `pattern_live_is_apply_in_progress()`.
- `project_v1_get_active_slot()`, `project_v1_get_last_error()`, `project_v1_get_last_sd_error_code()`.

## 4. API sortantes

Z6 appelle les zones suivantes:
- Z5 UI:
  - `ui_get_track_family/type/midi_*`, `ui_restore_track_config_bulk()`, `ui_get_active_track()`.
- Z2 Track runtime:
  - `track_runtime_get_param_rule()`, `track_runtime_refresh_all()`.
- Z3 Param/Mod:
  - `param_get()`, `param_set()`, `param_registry_get_track_value()`, `param_registry_apply_track_value()`, `param_registry_batch_begin/end()`, `param_registry_sync_ui_for_active_track()`.
  - `mod_lfo_v1_get_track_param()`, `mod_lfo_v1_set_track_param()`.
- Z4 Seq:
  - `seq_model_*` (capture/apply trig/plocks/pages/length).
  - `seq_runtime_*` (tempo/clock/rec/div/quant/swing/playhead/start/stop).
  - `seq_output_guard_panic()`.
- Infrastructure storage:
  - `sd_access_gate_try_acquire/release()`, `sd_access_fs_mount_if_needed()`, FatFs (`f_open/f_read/f_write/f_sync/f_unlink/f_lseek/f_mkdir`).
  - HAL flash in `boot_context_flash.c` (`HAL_FLASHEx_Erase`, `HAL_FLASH_Program`).

Contrats timing:
- SD/flash operations sont hors hard-RT et potentiellement longues.
- Apply snapshot/projet stoppe transport avant mutation d'etat globale.
- Load projet est separe en 3 phases: load+validation, live apply, puis commit pattern-bank SD.

## 5. Etats structurants possedes

### `pattern_live_ram.c`
- `g_current_pattern` (`PatternSaveV1`): dernier snapshot live capture/applique.
- `g_next_pattern` (`PatternSaveV1`): snapshot pattern queue pour prochaine bascule.
- `g_boot_pattern` (`PatternSaveV1`): fallback snapshot boot si slot absent.
- `g_pattern_slot_meta[16][16]` (`pattern_slot_meta_t {has_snapshot, dirty_pending_persist}`): meta locale des slots.
- `g_active_bank`, `g_active_pattern`: pattern actif.
- `g_queued_valid`, `g_queued_bank`, `g_queued_pattern`: pattern queue.
- `g_apply_in_progress`: garde anti re-entrance apply.
- `g_last_playhead_valid`, `g_last_playhead_step`: detection de wrap pour apply queue.

Points d'ecriture principaux:
- capture: `pattern_live_capture_current`, `pattern_live_capture_to_slot`, `pattern_live_capture_boot_snapshot`.
- apply: `pattern_live_apply_snapshot`, `pattern_live_queue_slot`, `pattern_live_service`, `pattern_live_set_active_state`, `pattern_live_init`.

Points de lecture principaux:
- UI via `pattern_live_get_active/get_queued`.
- project via `project_v1_capture_current` et `project_v1_apply_snapshot`.
- undo via `undo_v1_*`.

### `pattern_sd_bank.c`
- `g_slot_has_data[16][16]`, `g_slot_meta_cache_valid[16][16]`, `g_slot_checksum_cache[16][16]`: cache presence/checksum pattern slots.
- `g_boot_pattern` + `g_boot_pattern_valid`: fallback de chargement si fichier slot absent.
- `g_pattern_write_chunk[4096]`: tampon chunk write pour payload pattern.

### `project_v1.c`
- `g_project_work` (`ProjectSaveV1`): buffer travail pour save/load/apply.
- `g_project_active_slot_valid`, `g_project_active_slot`: slot projet actif logique.
- `g_project_save_counter`: compteur de version save.
- `g_project_last_error`, `g_project_last_sd_error`: etat erreur expose API.

### `project_sd_bank.c`
- `g_project_slot_has_data[16]`: presence des slots projet.
- `g_project_slot_buffer` (`PatternSaveV1`): buffer temporaire records pattern lors save/load.
- `g_project_sd_last_error`: erreur SD detaillee.

### `boot_context_flash.c`
- `g_boot_ctx_cache` (`boot_context_flash_data_t {version, valid, crc, active_project_slot}`): cache dernier contexte valide.
- `g_boot_ctx_cache_valid`: validite cache RAM.

### `undo_v1.c` (sous-zone dependante)
- `g_undo_v1` (`undo_snapshot_v1_t {valid, source_slot, snapshot}`): snapshot undo.
- `g_undo_capture_suspended`: garde anti-recursion pendant restore undo.

## 6. Flux runtime

1. Capture d'etat live:
- Source: `pattern_live_capture_current()`.
- Collecte:
  - track cfg UI (`ui_get_track_*`),
  - seq model (trigs/plocks/pages/length),
  - params globaux + track values,
  - mod LFO,
  - tempo/clock/rec/div/quant/swing.

2. Apply snapshot / restore pattern:
- `pattern_live_apply_snapshot()`:
  - valide budget plocks,
  - stop transport + panic,
  - restore track config UI,
  - `track_runtime_refresh_all()`,
  - apply seq block (plus tot),
  - batch apply params + globals,
  - restore LFO,
  - restore tempo/clock/rec/div/quant/swing,
  - reset playheads, sync UI param,
  - restart transport si autorise.

3. Save pattern:
- `pattern_live_capture_to_slot()` capture `g_current_pattern` puis `pattern_sd_bank_store_slot()`.
- Maj meta locale `has_snapshot/dirty_pending_persist`.

4. Load/queue pattern:
- `pattern_live_queue_slot()` lit presence slot via `pattern_sd_bank_slot_has_data()`.
- Charge `g_next_pattern` depuis SD ou fallback `g_boot_pattern`.
- Si transport stop: apply immediate + maj active.
- Si transport run: queue (`g_queued_*`) pour apply differe.

5. Bascule queue au boundary:
- `pattern_live_service()`:
  - active seulement si queued + transport run + pas d'apply en cours,
  - lit playhead track active,
  - applique sur transition non-zero -> zero.

6. Save project:
- `project_v1_save_slot()`:
  - capture current project (`project_v1_capture_current`),
  - force active slot dans snapshot,
  - stocke via `project_v1_store_snapshot_to_slot` -> `project_sd_bank_store_slot`,
  - incremente save_counter,
  - commit boot context si slot actif valide.

7. Load project:
- `project_v1_load_slot()`:
  - charge depuis SD via `project_sd_bank_load_slot` (lecture + validation header/checksum + records, sans commit pattern-bank),
  - applique snapshot (`project_v1_apply_snapshot` -> `pattern_live_apply_snapshot`),
  - commit ensuite le pattern-bank SD via `project_sd_bank_commit_slot_patterns`,
  - met a jour slot actif/counter,
  - commit boot context.

8. Restore boot context:
- `project_v1_restore_boot_context()`:
  - lit flash (`boot_context_flash_load`),
  - valide slot, puis appelle `project_v1_load_slot(slot)`.

9. Flux interne project SD (important):
- `project_sd_bank_load_slot`: phase de lecture/validation uniquement (payload projet + records + checksum global).
- `project_sd_bank_commit_slot_patterns`: phase de commit pattern-bank (revalidation puis application des deltas vers `pattern_sd_bank_*`).

Effets aval:
- Z5: UI track config et sync param active track sont mis a jour.
- Z3: runtime params/LFO sont remutes via registry/mod.
- Z4: transport/clock/seq state est restaure et repositionne.
- Z2: binding runtime rafraichi avant apply param track.

## 7. Contraintes RT/CPU/memoire

- Z6 n'est pas hard-RT: SD et flash bloquants autorises, mais exclus du chemin IRQ audio.
- APIs projet mutantes protegent contre ISR (`__get_IPSR`).
- `pattern_live_apply_snapshot` fait des boucles `PARAM_COUNT x SEQ_TRACK_COUNT` + seq full copy, cout variable mais hors IRQ audio.
- Pas de malloc observe dans les fichiers Z6; buffers statiques (`UI_SDRAM`, `DMA_BUFFER`, globals).
- Coordination SD via `sd_access_gate` evite collisions clients heterogenes (pattern/project vs autres clients non project-pattern).

## 8. Invariants a ne pas casser

- Autorite unique de capture/apply live: `pattern_live_capture_current` / `pattern_live_apply_snapshot`.
- Separation explicite:
  - live RAM (`pattern_live_*`) vs persistence pattern SD (`pattern_sd_bank_*`) vs persistence project SD (`project_sd_bank_*`) vs boot pointer flash (`boot_context_flash_*`).
- Apply snapshot/projet impose stop transport avant mutation et reset playhead apres apply.
- Ordre durci de restore live: stop/panic -> track config -> `track_runtime_refresh_all` -> seq block -> params/LFO/runtime globals.
- Queue pattern appliquee uniquement sur boundary detecte (wrap playhead) quand transport tourne.
- `project_v1_load_slot` ne commit le pattern-bank SD qu'apres succes du live apply.
- Getters de statut actif/queued et erreurs ne mutent pas l'etat metier (hors eventual set error pour API invalides cote project_v1).

## 9. Dependances inter-zones

- Depend de Z5 pour source et restauration config track UI, et pour les commandes utilisateur save/load/capture/queue.
- Depend de Z2 pour classification param domains et refresh binding runtime avant apply.
- Depend de Z3 pour application/lecture parametres et modulation.
- Depend de Z4 pour transport/tempo/clock/playhead et donnees sequenceur.
- Expose a Z5/Z7 (settings/ops) les APIs projet/pattern de persistence.

## 10. Dette technique observee

- Couplage inter-zone eleve dans `pattern_live_apply_snapshot` (UI + Z2 + Z3 + Z4 dans une seule routine).
- Dependance implicite a l'ordre d'appel superloop pour `pattern_live_service` (si non appele, queue pattern ne commute pas).
- `dirty_pending_persist` est ecrit mais non exploite dans le flux observe (meta partiellement orpheline).
- Parametres mix legacy `PARAM_MIX_TRACK0..3_*` encore whitelistes en global dans `pattern_live_is_global_param_useful` alors que l'architecture est track-aware 14 pistes.
- `project_v1_apply_snapshot` est un orchestrateur mince: delegation du live apply a `pattern_live_apply_snapshot` + restauration etat actif/queued/slot projet.
- Couplage UI implicite dans la condition de boundary (`seq_runtime_get_playhead_step(ui_get_active_track(), ...)`) au lieu d'une reference transport neutre.

Aucune double autorite complete concurrente de save/load projet n'est observee.

## 11. Impact eventuel sur la cartographie globale

- Z6 est confirmee comme zone composite avec 4 sous-domaines stables: live snapshot, pattern SD bank, project orchestration, boot context flash.
- `undo_v1` doit etre rattache en sous-zone dependante de Z6 (pas une zone principale separee).
- Frontiere Z6/Z5 est plus forte que prevu: la mutation persistence est pilotee par UI, mais l'autorite d'etat reste dans Z6.

## 12. Conclusion stricte

`cause trouvee`
