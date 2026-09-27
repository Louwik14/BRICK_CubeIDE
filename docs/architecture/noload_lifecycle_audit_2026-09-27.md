# Audit du cycle de vie des objets NOLOAD — 2026-09-27

## Périmètre et méthode

L'image Release H743 a été croisée avec le linker actif
`Board/LowCost/Generated/Linker/STM32H743IITX_FLASH.ld`, la table des symboles
ELF, les macros de `memory_layout.h`, les fonctions d'initialisation et leur
ordre d'appel depuis `brick6_app_init()`.

Le linker contient 46 sections custom `NOLOAD`, dont 35 occupées dans l'image
auditée. Elles contiennent 303 objets ELF. Les sections standard `.bss` et
`.tbss` sont également `NOLOAD`, mais appartiennent au contrat runtime C : la
startup efface explicitement l'intervalle `__bss_start__..__bss_end__`. Elles
ne sont donc pas comptées parmi les 303 objets custom. `._user_heap_stack` ne
contient aucun objet C nommé.

L'ordre réel pertinent est : reset/runtime C, init périphériques (dont FMC),
`SDRAM_Init`, `control_domain_init`, `seq_engine_control_init`,
`audio_domain_init`, `control_domain_start` (UI incluse), puis superloop et
démarrage AUDIO différé. Les initialisations ajoutées par cet audit restent
dans le domaine propriétaire et précèdent toute première lecture.

## Résultat par section et cycle de vie

Dans la colonne Objet, `reste (N)` signifie chacun des autres symboles de la
section, énumérés exhaustivement dans l'inventaire en fin de document.

| Objet | Section | Première lecture | Initialisation | Verdict | Risque/correction |
| --- | --- | --- | --- | --- | --- |
| `g_import_async` et 27 autres états CONTROL | `.ram_d3_ctrl` | services import/pools/cache/audio/UI | init domaine (`multi_sample_import_init`, pools, cache, executor) avant services | SAFE | Le correctif antérieur de `g_import_async` est bien ordonné. |
| `g_ui_renderer_template_wavetable_cache`, `g_ui_template_stack_wave_cache` | `.ram_d3_ctrl` | premier rendu template | `ui_renderer_template_init` depuis `ui_bootstrap_init` | BUG corrigé | `valid` résiduel pouvait accepter un cache d'un boot précédent. |
| `g_sample_voice_loop_cache` | `.sdram_stream_service` | premier bind de voix Sampler | `sample_voice_reader_init` au début de `brick6_sampler_runtime_init` | BUG corrigé | Un pointeur `reader` résiduel pouvait être passé à la routine de release. |
| reste (7) | `.sdram_stream_service` | service Stream/SD | `sample_stream_io_init`, transport init/reset, pool reset; buffers écrits avant lecture | SAFE | Aucun état de queue résiduel consommé. |
| `g_waveform_tiles`, `g_waveform_build`, `g_waveform_local`, `g_waveform_local_line_hot` | `.storage_state_sdram` | service/rendu waveform | `waveform_service_init` depuis `control_domain_init` | BUG corrigé | `active`/`valid` résiduels pouvaient lancer un faux job ou réutiliser un cache invalide. |
| reste (31) | `.storage_state_sdram` | services storage/pools/project | init propriétaire avant superloop, ou scratch entièrement écrit | SAFE | Jobs save/load, recorder, preview, catalogues et pools sont remis à zéro/état explicite. |
| reste (6) | `.sdram_recorder` | publications IPC/audio ou allocation clip | init projection/FIFO/preview, ou effacement par `brick6_clip_shifter_init` | SAFE | Publication après construction; historiques clip effacés avant rendu. |
| 19 objets contrôle | `.control_state_sdram` | CONTROL, paramètres, projet, Hall | `control_rt_publication_init`, `param_registry_init`, `project_control_init`, init Hall | SAFE | État complet réinitialisé; scratch publication rempli avant publication. |
| 63 objets AUDIO hot | `.dtcm_audio` | init/rendu AUDIO | init engine/fx/mixer/synth avant `audio_start`; scratch écrit avant lecture | SAFE | Aucun IRQ AUDIO avant fin de l'init AUDIO. |
| 6 objets AUDIO warm | `.ram_d1_audio` | moteurs FX/wavetable | init FX/voice; IO wavetable rempli avant décodage | SAFE | Historiques explicitement effacés. |
| 2 buffers delay | `.audio_delay_sdram` | delay actif | acquisition puis clear dans init/clear du delay | SAFE | Propriétaire explicite et effacement avant lecture. |
| 2 historiques AUDIO | `.audio_history_sdram` | FX drift/clip shifter | `audio_fx_runtime_init` / `brick6_clip_shifter_init` | SAFE | Entièrement effacés avant rendu. |
| 5 buffers DMA/UI | `.ram_d2_dma` | ADC/display/LED | matériel producteur ou init/encodage complet | SAFE | TX/LED initialisés; ADC écrit par DMA avant consommation. |
| 2 buffers SAI | `.ram_d2_dma_audio` | DMA SAI | `audio_init` les efface avant `board_audio_start_stream` | SAFE | TX déterministe avant activation DMA. |
| framebuffer OLED | `.sdram` | rendu/flush | `drv_display_clear`; controller RAM aussi effacée | SAFE | Snapshot/transfer sont remplis avant émission. |
| 11 objets NoteFX/SEQ/CONTROL | `.ram_control_m4_sram2` | runtime SEQ | init NoteFX, live-rec, transport, music output | SAFE | Structures, compteurs et flags réinitialisés. |
| 35 objets SEQ/CONTROL | `.ram_d2_m4_sram1`, `.ram_d2_m4_sram2`, `.ram_d2_m4_sram3` | runtime SEQ/track | `seq_runtime_init`, `seq_engine_irq_init`, `track_state_init`, init paramètres | SAFE | Publication désactivée jusqu'à construction des snapshots. |
| 11 objets calendrier/SEQ | `.seq_state_sdram` | compilation/exécution SEQ | init timing/calendar/core avant publication | SAFE | Têtes utilisent leur sentinelle explicite `0xFFFF`. |
| 5 objets IPC D2 | `.ram_d2_ipc` | AUDIO/CONTROL après init | init leases/projections/cache avant `audio_start` | SAFE | Protocoles de publication ordonnés, pas de pointeur résiduel. |
| 10 objets IPC D3 | `.ram_d3_ipc` | producteurs/consommateurs IPC | init de chaque layout/producteur avant IRQ/USB | SAFE | Layouts réinitialisés avant publication; locks écrits sous compte publié. |
| 4 scratch recorder | `.recorder_scratch_sdram` | storage recorder/waveform | buffers entièrement remplis avant lecture | SAFE | Aucun champ de contrôle dans ces buffers. |
| 4 caches audio éditeur | `.editor_audio_cache_sdram` | waveform local | métadonnées invalidées par `waveform_service_init`, payload écrit avant `ready` | SAFE | Le payload n'est jamais interprété sans métadonnée READY. |
| 2 états UI | `.ui_state_sdram` | bootstrap/entrée Settings | registre entièrement rempli; Settings remis à zéro à l'entrée | SAFE | `return_page_id` est posé par l'API d'ouverture avant l'entrée. |
| 5 arènes UI | `.sdram_ui` | clipboard/undo/catalogue | init clipboard/undo/catalogue avant UI interactive | SAFE | Undo neutralise d'abord le count avant de libérer l'historique. |
| 2 scratch storage | `.storage_scratch_sdram` | IO/conversion | écriture complète ou `wav_convert_init` | SAFE | Aucun état implicite du buffer partagé. |
| 13 objets import Multi | `.sdram_multi_import` | import coopératif | compteurs/état init; tableaux remplis avant lecture | SAFE | Scratch non lu hors bornes publiées. |
| 6 objets index/load Multi | `.sdram_multi_load` | index/loader | loader init ou reconstruction complète avant publication | SAFE | Files/plans remis à zéro. |
| 2 pools Multi | `.sdram_multi_pool` | résolution Multi | `multi_sample_pool_init` avant utilisation | SAFE | Compteurs publiés après remplissage. |
| cache Classic | `.sdram_classic_pool` | résolution Classic | `sample_cache_init` | SAFE | Chaque descriptor placé explicitement EMPTY. |
| payload pages | `.sdram_sample_page_pool` | lecture après descriptor READY | payload rempli puis publié | SAFE | Contenu opaque tant que descriptor non READY. |
| 2 métadonnées pages | `.sdram_page_meta` | cache page CONTROL/AUDIO | `sample_page_cache_init/reset`; descriptors partagés publiés ensuite | SAFE | États et index reconstruits. |
| index pages | `.sdram_page_index` | lookup cache | reset/reconstruction cache | SAFE | Accès borné par métadonnées publiées. |
| scratch CLMT | `.sdram_stream_scratch` | création map FatFs | memset puis FatFs remplit la table | SAFE | Jamais lu avant création réussie. |
| 4 snapshots AUDIO | `.sdram_audio_state_snapshot` | publication/consommation AUDIO | FIFO/projections initialisés avant démarrage AUDIO | SAFE | Publication explicite après remplissage. |
| 2 rings recorder | `.sdram_recorder_ring` | recorder/clip AUDIO | transport recorder init; clip shifter efface son historique | SAFE | Curseurs initialisés avant capture. |
| IO preview | `.sdram_audio_cold` | preview | écrit par lecture SD avant décodage | SAFE | Scratch sans métadonnée autonome. |

## Inventaire ELF exhaustif

Le nombre entre parenthèses est le nombre d'objets de la section. Les noms
suffixés `.lto_priv.0` sont ceux émis par LTO.

- `.audio_delay_sdram` (2): `g_delay_shared_l`, `g_delay_shared_r`.
- `.audio_history_sdram` (2): `g_audio_fx_drift_history`, `g_sampler_clip_shifter_delay_general`.
- `.control_state_sdram` (19): `g_control_audio_horizon`, `g_fm_control_state`, `g_macros`, `g_max_buffer`, `g_min_buffer`, `g_mixer_control`, `g_mod_env3_control`, `g_multi_bank`, `g_music_publish_scratch`, `g_param_filter_control`, `g_param_macro_collected_resolutions`, `g_param_macro_sources`, `g_ram_load`, `g_sample_bank`, `g_track_assets`, `g_unavailable_assets`, `g_vca_control`, `g_wavetable_bank`, `g_wavetable_load`.
- `.dtcm_audio` (63): `g_fm_voice`, `g_fm_modern`, `g_braids_poly_d2`, `g_braids_runtime`, `g_braids_render_scratch`, `g_dual`, `g_haas_l`, `g_haas_r`, `audio_rec_bus_i32`, `audio_rec_bus_l`, `audio_rec_bus_r`, `bus_group_fx_l`, `bus_group_fx_r`, `bus_group_l`, `bus_group_r`, `bus_main_l`, `bus_main_r`, `delay_reverb_l`, `delay_reverb_r`, `g_audio_fx_plan`, `g_audio_fx_runtime`, `g_audio_physical_inputs`, `g_audio_state`, `g_audio_tracks_enabled_mask`, `g_capture`, `g_external_track_l`, `g_external_track_mono`, `g_external_track_r`, `g_fast_refresh`, `g_groove_flashword`, `g_poly_cutoff_override`, `g_poly_filters_hot`, `g_requested_entity`, `g_reverb`, `g_sampler_declick_tail`, `g_sampler_multi_voice`, `g_sampler_render_track_mask`, `g_sampler_voice`, `g_stack_acc_scratch`, `g_stack_native_scratch`, `g_stack_runtime`, `g_synth_poly`, `g_synth_slot_owner`, `g_synth_voice`, `g_tb303`, `g_track_filters`, `g_track_sat`, `g_wave_poly_runtime`, `g_wave_runtime`, `master_gain`, `master_gain_smoothed`, `master_gain_target`, `mono_pan_l`, `mono_pan_r`, `output_adjust`, `postgain_recip`, `send_l`, `send_r`, `tracks`, `xfade_carrier_l`, `xfade_carrier_r`, `xfade_track_tap_l`, `xfade_track_tap_r`.
- `.editor_audio_cache_sdram` (4): `g_waveform_local_pcm`, `g_waveform_local_level0`, `g_waveform_local_level1`, `g_waveform_local_level2`.
- `.ram_control_m4_sram2` (11): `g_control_music_multi_instrument`, `g_control_music_outputs`, `g_control_music_outputs_staged`, `g_family`, `g_held`, `g_held_active_mask`, `g_locks_a_sram2`, `g_seq_context`, `g_seq_live_rec_pending`, `g_seq_runtime_live_rec_queue`, `g_state`.
- `.ram_d1_audio` (6): `g_revb_engine_buffer`, `g_audio_fx_vibe_history`, `g_multi_voice_dsp_pool`, `g_wavetable_pool_io`, `history`, `state`.
- `.ram_d2_dma` (5): `adc1_dma`, `adc2_dma`, `flush_snapshot`, `flush_transfer`, `pwm_buffer`.
- `.ram_d2_dma_audio` (2): `rx_buffer`, `tx_buffer`.
- `.ram_d2_ipc` (5): `g_multi_audio_instruments`, `g_sample_page_leases`, `g_sample_page_shared_last_slot`, `g_sampler_ram_audio_global_to_slot`, `g_sampler_ram_playhead`.
- `.ram_d2_m4_sram1` (28): `g_chain_runtime`, `g_core`, `g_generated_until`, `g_live_parameter_audio_poly_spread`, `g_live_parameter_audio_poly_voices`, `g_locks_b`, `g_mod_lfo_control_state`, `g_pattern_slot_b`, `g_seq_clock_bridge`, `g_seq_hold_state`, `g_seq_length_flash`, `g_seq_live_rec_armed`, `g_seq_live_rec_len_mode`, `g_seq_live_rec_pattern_active`, `g_seq_live_rec_pattern_pending_start`, `g_seq_live_rec_pattern_steps_remaining`, `g_seq_live_rec_pattern_target_track`, `g_seq_live_rec_pattern_track`, `g_seq_live_rec_start_mode`, `g_seq_live_rec_waiting_trigger_start`, `g_seq_param_runtime_locked_bits`, `g_seq_param_runtime_state`, `g_seq_runtime_control`, `g_seq_track_loop_generation`, `g_seq_transport_fsm`, `g_tone_program`, `g_track_runtime_ctx`, `g_track_sound_state`.
- `.ram_d2_m4_sram2` (1): `g_seq_project`.
- `.ram_d2_m4_sram3` (6): `g_control_music_output_staged_death`, `g_control_music_window_external`, `g_control_music_window_internal`, `g_seq_fx_a`, `g_seq_live_lane`, `g_seq_live_source_count`.
- `.ram_d3_ctrl` (28): `g_apply_sample_map`, `g_audio_seq_output`, `g_import_async`, `g_import_last_result`, `g_import_sample_count`, `g_import_zone_count`, `g_multi_clear_active`, `g_multi_instruments`, `g_multi_retire_fence_head`, `g_multi_retire_invariant_failed`, `g_multi_retire_stop_committed`, `g_multi_sample_count`, `g_multi_zone_count`, `g_sample_cache_last_fresult`, `g_sample_page_cache_state`, `g_sample_page_evict_cursor`, `g_sample_page_free_cursor`, `g_sample_page_reserved_count`, `g_sampler_multi_page0_reject_logged`, `g_sampler_multi_stream_release_pending`, `g_sampler_ram_retire_invariant_failed`, `g_sampler_ram_retire_not_before_sample`, `g_sampler_ram_retire_stop_committed`, `g_ui_renderer_template_wavetable_cache`, `g_ui_template_stack_wave_cache`, `g_wavetable_retire_invariant_failed`, `g_wavetable_retire_not_before_sample`, `g_wavetable_retire_stop_committed`.
- `.ram_d3_ipc` (10): `g_audio_boot_diag_layout`, `g_audio_rec_level_layout`, `g_audio_recorder_capture`, `g_audio_waveform_buffers`, `g_audio_waveform_layout`, `g_control_audio_fifo_layout`, `g_locks_a_d3`, `g_sd_preview_ring_layout`, `g_synth_waveform_layout`, `g_usb_audio_float_rings`.
- `.recorder_scratch_sdram` (4): `g_audio_recorder_write_buffers`, `g_sample_capture_copy_buf`, `g_waveform_cache_io`, `g_waveform_read`.
- `.sdram` (1): `buffer` (framebuffer OLED).
- `.sdram_audio_cold` (1): `g_sd_preview_io`.
- `.sdram_audio_state_snapshot` (4): `g_audio_prepared_state`, `g_audio_wavetable_registry`, `g_rec_source_projection`, `g_sample_classic_audio_source`.
- `.sdram_classic_pool` (1): `g_sample_cache`.
- `.sdram_multi_import` (13): `g_auto_loop_begin_candidates`, `g_auto_loop_begin_frames`, `g_auto_loop_end_candidates`, `g_auto_loop_end_frames`, `g_auto_loop_io`, `g_import_index_path`, `g_import_last_diag`, `g_import_paths`, `g_import_samples`, `g_import_scan_dir`, `g_import_source_samples`, `g_import_work_path`, `g_import_zones`.
- `.sdram_multi_load` (6): `g_index_io`, `g_index_samples`, `g_index_strings`, `g_index_zones`, `g_multi_bulk_plans`, `g_multi_load_queue`.
- `.sdram_multi_pool` (2): `g_multi_samples`, `g_multi_zones`.
- `.sdram_page_index` (1): `g_sample_page_shared_index`.
- `.sdram_page_meta` (2): `g_sample_page_sample_desc`, `g_sample_page_shared_descriptor`.
- `.sdram_recorder` (6): `g_control_audio_fifo_commands`, `g_multi_audio_samples`, `g_multi_audio_zones`, `g_sampler_clip_shifter_delay_recorder`, `g_sampler_ram_audio_slots`, `g_sd_preview_ring`.
- `.sdram_recorder_ring` (2): `g_audio_recorder_capture_ring`, `g_sampler_clip_shifter_delay_recorder_ring`.
- `.sdram_sample_page_pool` (1): `g_sample_page_shared_data`.
- `.sdram_stream_scratch` (1): `g_sample_stream_clmt_scratch`.
- `.sdram_stream_service` (8): `g_sample_stream_io_async`, `g_sample_stream_io_rec_decode`, `g_sample_stream_manager_pending_io`, `g_sample_stream_physical_pool`, `g_sample_stream_transport_mailbox`, `g_sample_stream_transport_release_queue`, `g_sample_voice_loop_cache`, `g_sd_block_device_async_fifo`.
- `.sdram_ui` (5): `g_seq_clipboard`, `g_ui_clipboard`, `g_undo_v2`, `g_wav_catalog_scratch_view`, `g_wav_catalog_views`.
- `.seq_state_sdram` (11): `g_blocks`, `g_chain`, `g_deferred_calendar`, `g_deferred_calendar_head`, `g_deferred_calendar_tail`, `g_final_calendar`, `g_final_calendar_head`, `g_final_calendar_tail`, `g_seq_source_cohort`, `g_seq_sources`, `g_terminal`.
- `.storage_scratch_sdram` (2): `g_storage_shared_io`, `g_wav_convert`.
- `.storage_state_sdram` (35): `g_audio_recorder_storage`, `g_groove`, `g_meta`, `g_patch_io`, `g_patch_transaction_backup`, `g_persistence_workspace`, `g_project_load`, `g_project_save`, `g_rec_source_generations`, `g_rec_source_waveform`, `g_sample_global_pool`, `g_sampler_ram_load_job`, `g_sampler_ram_pool`, `g_sd_fs`, `g_sd_preview`, `g_seq_param_patch_transaction_locked_bits`, `g_seq_param_patch_transaction_state`, `g_wav_catalog_view_load`, `g_wav_parser_crc_io`, `g_waveform_build`, `g_waveform_cache`, `g_waveform_cache_ram_tiles`, `g_waveform_local`, `g_waveform_local_line_hot`, `g_waveform_tiles`, `g_wavetable_candidate`, `g_wavetable_fft_imag`, `g_wavetable_fft_real`, `g_wavetable_fft_work_imag`, `g_wavetable_fft_work_real`, `g_wavetable_load_job`, `g_wavetable_old_commit_snapshot`, `g_wavetable_pool`, `g_wavetable_transaction_files`, `g_wavetable_transaction_paths`.
- `.ui_state_sdram` (2): `g_ui_settings`, `g_ui_template_family_registry`.

Les onze sections custom vides sont `.ram_d1`, `.ram_d1_ui`, `.dtcm_fault`,
`.ram_d2_dma_cacheable`, `.ram_d2_local_cacheable`, `.ram_d2_m7`,
`.ram_d3_restore_ipc`, `.backup_sram`, `.multi_load_sdram`, `.sdram_samples` et
`.page_desc_sdram`.

## Totaux

- Objets custom NOLOAD audités : **303**
- SAFE dès l'origine : **293**
- SUSPICIOUS : **0**
- BUG démontrés puis corrigés : **10**

