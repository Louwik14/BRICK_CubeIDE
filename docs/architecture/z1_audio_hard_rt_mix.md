# Z1 - Audio Hard-RT et Mix

## 1. Perimetre

Perimetre operationnel de zone (appartient a Z1):
- `Src/Audio/audio.c`
- `Src/Core/brick6_audio_runtime.c`
- `Src/Audio/mixer.c`

Elargissements necessaires (preuves de frontiere et contrats):
- `Src/Audio/audio_float.c` et `Inc/Audio/audio_float.h` : frontiere IRQ `int24 <-> float`, ownership des buffers track et callback DSP.
- `Src/Audio/audio_io.c` : preuve unpack/pack TDM et mapping slots.
- `Src/Audio/audio_io.c` repacke MAIN/CUE sans calcul de VU/peak/clip produit; la saturation TX reste locale a la conversion int24.
- `Src/Audio/dsp_engine.c` : preuve d'autorite callback DSP unique.
- `Src/Core/brick6_sampler_runtime.c` + `Inc/Core/brick6_sampler_runtime.h` : point d'insertion unique du futur moteur Sampler, sans pipeline audio parallele.
- `Src/Core/brick6_opal_runtime.cpp` + `Inc/Core/brick6_opal_runtime.h` : backend runtime mono-instance Opal rendu en blocs puis injecte via `mixer_submit_external_mono_native`, contraint localement au moteur Plaits `6OP` et branche directement `plaits::SixOpEngine` sans repasser par `plaits::Voice`.
- `Src/Core/brick6_braids_runtime.cpp` + `Inc/Core/brick6_braids_runtime.h` : runtime Braids multi-instances (une instance mono par track Braids) autour de `braids::MacroOscillator`, rendu en sous-blocs de 24 samples puis injecte via `mixer_submit_external_mono_native`.
- `Src/Core/brick6_sampler_runtime.c` + `Inc/Core/brick6_sampler_runtime.h` : backend stereo du Sampler branche sur le point d'insertion unique, en lecture via `sample_cache` RAM.
- `Src/Sampler/sample_cache.c` + `Inc/Sampler/sample_cache.h` : facade produit Sampler en RAM; `brick6_sampler_runtime` lit le cache uniquement, sans acces SD ni lecture directe `sample_desc->data`.
- `Src/Sampler/sample_page_cache.c` + `Inc/Sampler/sample_page_cache.h` : seam local du cache pagine Sampler; en phase actuelle, `READY_FULL` peut etre charge par pages float stereo contigues en SDRAM sans modifier le chemin audio stream.
- `Src/Sampler/sample_voice_reader.c` + `Inc/Sampler/sample_voice_reader.h` : helper local Sampler pour le fast path bloc RAM-only; aucune SD, aucune policy musicale globale.
- `Src/Core/brick6_clip_shifter.c` + `Inc/Core/brick6_clip_shifter.h` : pitch-shifter stereo local du mode `Sampler/Clip` `Shifter`, port C borne sans import Clouds/FxEngine.
- `Src/Core/brick6_sampler_runtime.c` + `Inc/Core/brick6_sampler_runtime.h` : slice grid v1 reconstruite hors IRQ, selection de slice par note en mode `Slice`.
- `Inc/Audio/mixer.h` : cardinalite mixer (`MIXER_MAX_TRACKS = SEQ_TRACK_COUNT`) et contrat public.
- `Src/Audio/sd_multitrack_recorder.c` + `Inc/Audio/sd_multitrack_recorder.h` : preuve des taps recorder dans le chemin audio.
- Etat produit courant `sd_multitrack_recorder` : bypass temporaire via `SD_RECORDER_PRODUCT_ENABLED=0`. Les hooks audio SD peuvent rester appeles, mais `sd_recorder_request_start/stop/arm/disarm`, `sd_recorder_audio_block_begin()` et `sd_recorder_capture_tap_block()` retournent immediatement tant que le flag reste a `0`.
- `Src/Core/brick6_master_buffer.c` + `Inc/Core/brick6_master_buffer.h` : preuve du branchement bloc debut/fin et read playback dans pipeline.
- `Src/Core/brick6_master_buffer_stretch.c` + `Inc/Core/brick6_master_buffer_stretch.h` : seam local du futur timestretch playback Master/Buffer, borne a une instance unique et sans pipeline parallele.
- `Src/Audio/fx_master_macro.c` + `Inc/Audio/fx_master_macro.h` : insert master leger pour les 4 slots `Master/FX` MacroFX, avec core delay mono statique par slot pour `COMB`, `WOBBLE`, `ECHO`, `FREEZE`, `STUTTER` et `PITCH`, et formants SVF legers pour `TALK`.
- `Src/Seq/seq_runtime.c` + `Inc/Seq/seq_runtime.h` : preuve collecte/apply des evenements audio sample-accurate.
- `Src/Core/brick6_app_init.c` : preuve du wiring `audio_set_float_callback(brick6_audio_runtime_dsp)`.

Dependances de Z1 sans appartenir a Z1:
- Engines synth/sampler (`drum`, `voice_manager`, wrappers Opal/Braids/Sampler).
- `track_runtime` (mapping track logique -> cible mix).
- `mod_lfo_v1` (modulation bloc).
- `seq_runtime` (event scheduling audio).
- `sd_multitrack_recorder` (taps et writer hors IRQ).
- `sd_multitrack_recorder` = recorder SD/stems distinct du `Master/Buffer`; en etat produit courant il reste bypasse par flag compile-time local.
- `brick6_master_buffer` (capture post-fader + lecture playback).
- `brick6_master_buffer_stretch` (etat runtime local du timestretch playback, sans ownership du recorder brut).
- `track_tone_sound_state` pour les valeurs `Master/FX` type/LVL/A/B lues par `fx_master_macro`.
- `fx_chain`, `fx_reverb`, `env_adsr`, `fx_biquad_filter`.

Exclusions explicites:
- UI (`Src/UI/*`) : pilote config mais n'execute pas le flux hard-RT.
- Persistence (`Src/Storage/*`) : hors chemin IRQ audio.
- Shim legacy `runtime_target` : hors autorite du pipeline hard-RT.

## 2. Autorite(s) de verite

Autorite d'entree hard-RT (IRQ DMA):
- `HAL_SAI_RxHalfCpltCallback()` et `HAL_SAI_RxCpltCallback()` dans `Src/Audio/audio.c`.
- Les deux callbacks appellent `process_half(0|1)`.

Autorite de decoupe demi-buffer/bloc:
- `process_half()` dans `Src/Audio/audio.c`.
- Segmente un half-buffer en sous-segments via `seq_runtime_audio_collect_block_events()` et offsets sample.
- En clock interne/externe, ce point est l'autorite effective de consommation d'avance step sequencer (domaine audio bloc).

Autorite de rendu DSP principal:
- `audio_process_block_int32()` dans `Src/Audio/audio_float.c`.
- Appelle `audio_io_unpack()` -> `dsp_engine_process_block()` -> `audio_io_pack_ramped()`.
- Le callback DSP unique est enregistre via `audio_set_float_callback()` (wiring depuis `brick6_app_init.c` vers `brick6_audio_runtime_dsp`).

Autorite de rendu runtime applicatif:
- `brick6_audio_runtime_dsp()` dans `Src/Core/brick6_audio_runtime.c`.
- Ordonne render engines externes (Drum, Sampler, Opal, Braids), modulation bloc, sampler, mixer, crossfade master/buffer, taps master.

Autorite de mixage final:
- `mixer_process()` dans `Src/Audio/mixer.c`.
- Possede la sommation tracks -> MAIN/CUE/SEND/returns et les taps post-insert/post-fader/post-send.

Autorite de flux bloc-a-bloc:
- Le flux est distribue sur 3 niveaux stricts:
1) `audio.c` (IRQ + cache DMA + segmentation eventee)
2) `audio_float.c` (frontiere conversion + callback DSP)
3) `brick6_audio_runtime.c` + `mixer.c` (rendu/mix contenu audio)

Seconde autorite concurrente:
- Aucune seconde autorite de meme niveau pour le flux IRQ->mix final n'est observee in-tree.

## 3. API entrantes

Entrees de la zone Z1:
- `audio_init()` et `audio_start()` appeles depuis boot (`brick6_app_init`).
- IRQ HAL SAI RX (`HAL_SAI_RxHalfCpltCallback`, `HAL_SAI_RxCpltCallback`) appeles par la pile HAL/DMA.
- `audio_set_float_callback(brick6_audio_runtime_dsp)` configure le coeur DSP.

Entrees de configuration runtime (hors Z1 mais consommees par Z1):
- Etat mixer (`mixer_set_track_*`, `mixer_set_send_fx_slot`, etc.) via Param/UI.
- Etat track runtime (`track_runtime_*`) lu dans `brick6_audio_runtime` et `mixer`.
- Evenements sequenceur audio (`seq_runtime_audio_collect_block_events`, `seq_runtime_audio_apply_event`).
- Etat master buffer (`brick6_master_buffer_get_xfade`, state recorder).

Contrats implicites critiques:
- `AUDIO_FRAMES_PER_HALF` dans `audio.c` doit rester coherent avec `AUDIO_BLOCK_SIZE` (`audio_float.h`).
- Les offsets d'evenements de `seq_runtime_audio_collect_block_events` sont supposes dans `[0..frames]` (code clamp a `AUDIO_FRAMES_PER_HALF`).
- Le callback DSP (`dsp_engine`) doit etre O(1) borne et sans blocage.

## 4. API sortantes

Sorties directes de Z1:
- Vers DMA TX: buffer `tx_buffer` via `HAL_SAI_Transmit_DMA` (data preparee dans `process_half`).
- Vers scheduler systeme: `engine_tasklet_notify_frames(AUDIO_FRAMES_PER_HALF)`.
- Vers runtime sequenceur: `seq_runtime_audio_apply_event()` au sample offset.
- Vers recorder taps:
  - `SD_RECORDER_TAP_TRACK_RAW` et `SD_RECORDER_TAP_MASTER` dans `brick6_audio_runtime`.
  - Les anciens taps track intermediaires `POST_INSERT` / `POST_FADER` / `POST_SEND` ne sont plus emis par `mixer`.
- Vers master buffer:
  - `brick6_master_buffer_begin_block`, `submit_track_post_fader`, `commit_block` dans `mixer`.
  - `brick6_master_buffer_read_playback` + blend final dans `brick6_audio_runtime`.

Contrats timing sortants:
- Taps et master-buffer sont synchrones du bloc audio courant (dans IRQ).
- Ecriture SD n'est pas faite dans Z1 IRQ (writer service hors IRQ), Z1 ne fait que pousser/capturer.
- Etat produit courant recorder SD: les hooks IRQ `sd_recorder_audio_block_begin()` et `sd_recorder_capture_tap_block()` peuvent rester presents au call-site, mais ils sont des no-op immediats quand `SD_RECORDER_PRODUCT_ENABLED=0`.

## 5. Etats structurants possedes

### `Src/Audio/audio.c`
- `rx_buffer[AUDIO_BUFFER_WORDS]`, `tx_buffer[AUDIO_BUFFER_WORDS]`
  - Ecriture: DMA (rx), CPU (tx dans `process_half` via `audio_process_block_int32`).
  - Lecture: CPU (rx apres invalidate), DMA (tx apres clean).
  - Role: ping-pong DMA hard-RT.
- `sai_tx`, `sai_rx`
  - Ecriture: `audio_init`.
  - Lecture: `audio_start`, callbacks IRQ.
  - Role: handles des streams SAI.

### `Src/Audio/audio_float.c`
- `tracks[MAX_TRACKS]` (`StereoTrack`)
  - Ecriture: `audio_io_unpack`, callback DSP, `audio_tracks_init`.
  - Lecture: callback DSP, `audio_io_pack_ramped`.
  - Role: buffers float de travail par track pour un bloc.
- `g_audio_block_counter`, `g_audio_dsp_frames_counter`
  - Ecriture: `audio_process_block_int32`.
  - Lecture: diagnostics/commits param hors zone.
  - Role: cadence bloc/frame.
- `master_gain`, `master_gain_target`, `master_gain_smoothed`, `postgain_recip`, `output_adjust`
  - Ecriture: APIs `audio_float_set_*`.
  - Lecture: `audio_process_block_int32`.
  - Role: gain staging et rampe sortie.

### `Src/Core/brick6_audio_runtime.c`
- `g_runtime_track_enabled`, `g_runtime_last_*`, `g_buffer_xfade_smoothed`
  - Ecriture/Lecture: `brick6_audio_runtime_dsp` et helper xfade.
  - Role: gating des engines et smoothing blend buffer.
- temporaires bloc `drum_tmp`, `plaits_tmp`, `braids_tmp`, `recL`, `recR`
  - Role: scratch per-block pour rendu engines et read playback.

### `Src/Audio/mixer.c`
- `g_tracks[MIXER_MAX_TRACKS]` (gain/pan/mute/routes/inserts/sends + smoothing)
  - Ecriture: `mixer_set_*` APIs (hors IRQ en general), lecture/evolution dans `mixer_process`.
  - Role: etat runtime mix track-aware.
- `g_track_filters[MIXER_MAX_TRACKS]`
  - Ecriture: `mixer_set_track_filter_*`, note on/off VCA/filter.
  - Lecture/update: `mixer_track_filter_process_block`.
  - Role: etat filter/EQ/VCA par track.
  - Contrat `mixer_set_track_filter_type`: idempotent sur type identique (no-op) et reconfiguration sans reset DSP brutal, pour eviter les transitoires audibles sur re-apply redondant.
- Lors d'un rebind logique->lane, la migration du state lane-bound (`g_tracks` + `g_track_filters`) doit etre faite explicitement avant re-apply des params autoritatifs; sinon le state FILTER/VCA reste attache a l'ancienne lane.
- Apres copie d'un `g_track_filters` vers une nouvelle lane, les instances DSP internes qui portent des pointeurs vers leur stockage local (notamment `EQ3` CMSIS stereo/mono) doivent etre rebindees vers le stockage de la lane destination avant tout traitement audio.
- `g_send_fx_slot[MIXER_NUM_SENDS]`, `g_reverb`
  - Ecriture: `mixer_set_send_fx_slot`, `mixer_set_reverb_*`.
  - Lecture: `mixer_process`.
  - Role: routing sends/reverb global.
- `g_external_track_l/r`, `g_external_track_mono`, `g_external_track_enabled`, `g_external_track_format`, `g_external_track_frames_valid`
  - Ecriture: `mixer_submit_external_mono`, `mixer_submit_external_mono_native` et `mixer_submit_external_stereo` (depuis `brick6_audio_runtime`).
  - Lecture+clear: `mixer_process`, `mixer_external_inputs_clear`.
  - Role: injection des sources engines externes dans les lanes mixer, avec format mono-native ou stereo explicite.
- `lane_plan` local de `mixer_process`
  - Ecriture/Lecture: calcule localement a chaque bloc par `mixer_build_lane_plan`.
  - Role: autorite locale mono/stereo par lane, sans nouveau param UI ni autorite globale parallele.
  - Discipline: une lane mono-native ne reste mono que si tous les blocs actifs de la lane ont une variante mono reelle; sinon la lane repasse localement sur le fallback stereo de reference.
- buffers bus statiques dans `mixer_process`: `bus_main_*`, `bus_cue_*`, `send_*`, `reverb_return_*`
  - Role: accumulation et rendu final du bloc.

Possession du routage main/cue/send:
- Oui, c'est porte dans `mixer.c` (routes track, sends, returns, copie vers `tracks[0]` et `tracks[1]`).

Taps recorder et master-buffer:
- Oui, implementes directement dans Z1 (`brick6_audio_runtime.c` + `mixer.c`) comme appels de service synchrones bloc.

## 6. Flux runtime

Flux nominal prouve par code:

1) Entree DMA / callback
- `HAL_SAI_RxHalfCpltCallback` ou `HAL_SAI_RxCpltCallback` (`audio.c`).

2) Decoupe half/block
- `process_half(half_index)` calcule offset half et invalidation D-cache RX.
- Recupere evenements bloc via `seq_runtime_audio_collect_block_events`.
- Ce call consomme aussi les pulses step du sequencer (interne + externes pending) en domaine sample avant extraction des events dus du bloc.
- Coupe le half en sous-segments selon offsets events, appelle `audio_process_block_int32` par segment.

3) Unpack / conversion
- `audio_process_block_int32` -> `audio_io_unpack`:
  - int24 TDM slots (0/1,2/3,4/5) -> `tracks[0..2].L/R` float.
  - lane 3 (interne) est explicitement zeroee.

4) Collecte des events/sources
- Avant chaque sous-segment, `seq_runtime_audio_apply_event` applique les events a l'offset.
- Dans `brick6_audio_runtime_dsp`:
  - refresh runtime tracks
- rendu engines externes (Drum/Opal mono-instance, Braids mono par instance, Sampler stereo) -> `mixer_submit_external_*`
  - `mod_lfo_v1_process_block`
  - `voice_manager_process`
  - tap `SD_RECORDER_TAP_TRACK_RAW` (no-op immediat en produit tant que `SD_RECORDER_PRODUCT_ENABLED=0`)

5) Rendu engines/tracks
- Le callback DSP effectif est `brick6_audio_runtime_dsp` (via `dsp_engine`).
- `mixer_external_inputs_clear` puis injections engines.

6) Mixage bus / sends / master
- `mixer_process`:
  - begin block master-buffer
  - calcule un `lane_plan` local par lane (`source mono-native`, `source stereo`, `promotion stereo requise`, `fallback stereo`)
  - per-track stereo: inserts -> filter/EQ/VCA -> gains/pan -> sends -> route MAIN/CUE
  - per-track mono-native: filtre biquad mono ou EQ3 mono -> inserts mono-compatibles -> VCA+gain dans la boucle commune -> projection vers `L/R` seulement au point utile pour taps, sends, routing MAIN/CUE et accumulation bus
  - `EQ3` mono est un bloc mono reel pris directement par le `lane_plan`; une lane mono-native avec `EQ3` actif ne doit plus etre promue stereo pour appeler `EQ3` stereo avec `L/R` dupliques
  - la projection `mono -> L/R` reste tardive et centralisee: taps `POST_INSERT`, boucle commune `VCA+gain+pan`, puis consommation `POST_FADER`, sends et bus
  - le chemin stereo reste la reference fonctionnelle et ne met plus a jour les etats mono auxiliaires (`biquad_mono`, `eq3_mono`) quand la lane execute deja en stereo
  - returns reverb/send FX
  - ecrit resultat dans `tracks[0]` (MAIN) et `tracks[1]` (CUE)

7) Taps recorder / master-buffer
- submit post-fader vers master-buffer dans `mixer_process`.
- commit master-buffer fin de mix.
- post-mix: `brick6_audio_runtime_dsp` lit playback buffer et applique xfade live/recorded sur `tracks[0]`.
- post-mix: `fx_master_macro_process_block` applique les slots `Master/FX` legers sur `tracks[0]` avant preview SD, playback `Master/Buffer` et tap master final.
- tap final `SD_RECORDER_TAP_MASTER` (hook conserve, no-op immediat en produit tant que `SD_RECORDER_PRODUCT_ENABLED=0`).
- Distinction structurante:
  - `sd_multitrack_recorder` = recorder SD/stems actuellement bypasse cote produit.
  - `brick6_master_buffer` = recorder/buffer interne distinct, conserve actif.

8) Pack / sortie
- `audio_process_block_int32` -> `audio_io_pack_ramped`:
  - MAIN -> slots TX 0/1
  - CUE -> slots TX 2/3
  - copie MAIN -> slots TX 4/5
  - slots 6/7 a zero
- `process_half` nettoie D-cache TX puis DMA consomme.

## 7. Contraintes RT/CPU/memoire

Contraintes hard-RT observees:
- Audio execute en IRQ DMA RX (callbacks HAL).
- Pas d'allocation dynamique dans le chemin `audio.c` / `audio_float.c` / `brick6_audio_runtime.c` / `mixer.c`.
- Buffers critiques statiques (`rx_buffer`, `tx_buffer`, tracks, bus temporaires).
- Cohérence cache explicite sur buffers DMA cacheables:
  - `dcache_invalidate_by_addr_aligned` avant lecture RX CPU
  - `dcache_clean_by_addr_aligned` avant lecture TX DMA

Contraintes CPU/worst-case:
- Bloc fixe `AUDIO_BLOCK_SIZE=64`, `AUDIO_FRAMES_PER_HALF=64`.
- `mixer_process` fait des boucles `MIXER_MAX_TRACKS * frames`; `MIXER_MAX_TRACKS = SEQ_TRACK_COUNT = 14`.
- Decoupe en sous-segments peut multiplier les appels `audio_process_block_int32` par half selon densite d'evenements seq.
- Sends/reverb et inserts sont conditionnels mais dans le chemin IRQ.
- Aucun calcul VU/peak meter produit n'est conserve dans le chemin IRQ (`mixer_process` ni `audio_io_pack_ramped`).

Memoire:
- Scratch bus dans `mixer_process` en statique fonction.
- Lanes externes mixer `g_external_track_l/r` dimensionnees `MIXER_MAX_TRACKS x AUDIO_BLOCK_SIZE`.

Placement memoire valide pour la reverb SEND runtime:
- `RevB` est l'unique backend reverb runtime compile.
- `g_revb_engine_buffer[32768]` et le predelay RevB restent en D1 via `AUDIO_WARM`.
- Les anciens buffers runtime Drumboy (feedback DTCM, predelay/surround RAM_D2), GVerb et Oliverb sont retires.
- Le code dormant Mutable/Inspiration non compile n'appartient pas au backend SEND runtime et n'est pas concerne par ce retrait.

Granular / fx_pool:
- `g_granular_state_storage` n'est plus en DTCM; il est place hors D1 via `AUDIO_COLD_SDRAM`.
- Granular reste hors chemin critique prioritaire du produit.

## 8. Invariants a ne pas casser

- Entree audio hard-RT unique via IRQ DMA RX (`audio.c`).
- Ordre impose dans `process_half`:
  1) invalidate RX cache
  2) segmentation events
  3) traitement segment(s)
  4) clean TX cache
- `AUDIO_FRAMES_PER_HALF` doit rester coherent avec `AUDIO_BLOCK_SIZE`.
- `audio_io_unpack` reserve lane 3 comme source interne (pas de mapping TDM physique direct).
- Z1 ne doit pas faire d'I/O SD bloquante: seulement captures/taps; writer hors IRQ.
- Tant que `SD_RECORDER_PRODUCT_ENABLED=0`, le recorder SD ne doit pas redevenir actif par simple presence des call-sites IRQ: la reactivation impose explicitement integration UI/produit, remise du flag a `1`, validation start/stop/arm et revalidation audio/IRQ.
- Le Sampler track-aware lit via `sample_cache` en RAM. `sample_pool` reste catalogue/projet/metadata; `sample_desc->data` est une compat legacy hors autorite audio principale.
- La sortie principale Sampler reste stereo de bout en bout: pas de downmix L/R->mono avant injection mixer; les samples mono restent dupliques identiquement sur L/R.
- Chemin mono-native mixer: si la source externe est mono-native et si tous les blocs track-level actifs ont une variante mono reelle, `mixer_process()` conserve le signal en mono jusqu'au dernier moment utile; le fallback stereo reste la reference fonctionnelle.
- Les blocs track-level mono reels autorises dans ce corridor sont actuellement biquad mono, `EQ3` mono, `VCA` et `gain`; l'ancien insert track `FX_SAT` lie a `COLORS/CRUNCH` n'est plus active par la policy boot produit.
- L'ordre DSP mono aligne le chemin stereo de reference: filtre/EQ puis inserts, puis `VCA+gain`, puis projection tardive `mono -> L/R`.
- Un bloc mono ne doit jamais appeler un traitement stereo avec `L/R` identiques pour simuler du mono.
- La projection mono -> stereo ne doit intervenir qu'aux frontieres qui l'exigent reellement: taps post-fader, sends stereo, routing `MAIN/CUE` et accumulation bus.
- Stabilisation actuelle `sample_cache`: le chemin Sampler track-aware supporte le playback forward simple, le pitch simple par interpolation lineaire en forward/reverse, la loop forward pitchee simple, le ping-pong pitche simple, le reverse simple, la loop forward simple, le ping-pong simple et la selection de slices v1 par note via `sample_voice_reader`.
- La memoire audio runtime Sampler reste locale au sous-systeme Sampler: `sample_page_cache` est l'owner memoire audio runtime, `sample_cache` garde la facade produit/orchestration prepare-service-compat, et `sample_voice_reader` porte la lecture musicale. `READY_FULL` est materialise par pages contigues en SDRAM; `READY_PARTIAL` forward simple est maintenant servi par pages queuees/chargees hors audio via `sample_page_cache_service()` appele depuis `sample_cache_service()`.
- Retrigger Sampler track-aware: `brick6_sampler_runtime_trigger()` coupe d'abord la voix cache du track cible, puis ne rearme qu'apres `sample_cache` jugé rejouable depuis la frame de depart. Un `READY_PARTIAL` dont la frame 0 n'est plus en fenetre passe par `NEEDS_REPREPARE -> PREFILLING -> READY_PARTIAL` hors audio, sans rester coince en `PLAYING`.
- Limitations actuelles `READY_PARTIAL`: WAV PCM/extensible PCM, 48 kHz, mono/stereo, 16/24-bit, forward simple, pitch lineaire, reverse simple, loop forward simple, ping-pong simple, slices v1 par note, partage multi-voix meme sample autorise en phase actuelle.
- `sample_cache_read_voice()`, `sample_cache_read_voice_frame()`, `sample_cache_peek_frame()`, `sample_cache_begin_read_block()` et `sample_cache_commit_read_block()` sont RAM-only. FatFs reste limite a `sample_cache_prepare()` et `sample_cache_service()`.
- Phase 1/2/3/4/5A/5B/6A/6B refonte locale Sampler: les modes `Shot` forward 1x (`mode=0`), `RevShot` reverse 1x (`mode=1`), `Loop` forward 1x (`mode=2`), `PingPong` 1x (`mode=3`), le `Shot` forward pitche simple (`mode=0`, `step != 1`, sans loop), le `RevShot` reverse pitche simple (`mode=1`, `step != 1`, sans loop), la `Loop` forward pitchee simple (`mode=2`, `step != 1`, sans ping-pong) et le `PingPong` pitche simple (`mode=3`, `step != 1`) ne passent plus par `sample_cache_begin_read_block()` dans l'IRQ. `brick6_sampler_runtime` construit un `play_plan` au trigger, `sample_voice_reader` porte un cursor audio local par voix, et l'IRQ consomme des segments page-bounds deja acquis via `sample_page_cache`.
- Sur ce chemin Phase 1/2/3/4/5A/5B/6A/6B, aucun `request_page` n'est emis depuis le kernel audio. Le prefetch stream est queue hors IRQ par `sample_cache_service()` a partir des voix actives, et la transition de page du cursor se limite a un acquire/release RAM-only au boundary; en reverse, les demandes se font sur `current-1/current-2`. La loop forward 1x reste un wrap de cursor local (`loop_end -> loop_begin`), le ping-pong 1x une inversion locale de direction/kernels aux bounces, et le pitch simple forward/reverse/loop/ping-pong consomme des segments prepares avec voisin d'interpolation deja acquis.
- Les autres modes (`slice`) restent provisoirement sur les chemins legacy `sample_cache_begin_read_block()` et `sample_voice_reader_render_pitch_forward()` jusqu'aux phases suivantes.
- Legacy restant: `voice_manager` peut encore traiter des voix anciennes et `Src/Audio/sampler.c` reste helper legacy; le chemin produit track-aware ne doit pas revenir a `sample_desc->data`.
- Master-buffer est dans le pipeline de bloc (`begin -> submit -> commit`) et son playback est blend apres mixer dans `brick6_audio_runtime_dsp`.
- Master/FX MacroFX est un insert master apres `mixer_process` et avant le blend playback `Master/Buffer`; `DRIVE`, `CRUSH`, `RING`, `CHOP`, `PUMP`, `COMB`, `WOBBLE`, `ECHO`, `FREEZE`, `STUTTER`, `TALK` et `PITCH` ont un traitement DSP. `OFF` et tout type inconnu restent no-op exacts. `RING` et `CRUSH` ne lisent pas la mesure/position transport; `STUTTER` lit seulement le BPM courant pour dimensionner sa fenetre rythmique.
- Les delays MacroFX sont monophoniques par slot, statiques en `AUDIO_COLD_SDRAM`, avec lecture interpolee et historique logique `delay_filled` pour eviter de nettoyer de grands buffers en IRQ lors d'un reset de type. `STUTTER` et `PITCH` reutilisent ce core mono: `STUTTER` capture une fenetre recente bornee avec crossfade court de boucle, `PITCH` utilise deux lectures delay/grain simples. `TALK` utilise des formants fixes/morphables bornes, sans FFT ni analyse vocale.
- Le futur stretch Master/Buffer reste un seam local du playback buffer: `brick6_master_buffer` garde l'ownership du buffer et `live_recorder` garde l'ownership du stockage/lecture brute.
- Integration courante `Sampler/Clip`: `Stretch Mode=Off` garde une lecture 1x entre micro-corrections locales distribuees, `Stretch Mode=Speed` garde le chemin cursor varispeed legacy, et `Stretch Mode=Shifter` garde le cursor `Speed` puis applique `brick6_clip_shifter` stereo avant accumulation.
- `brick6_clip_shifter` porte un shifter deux taps delay/crossfade local; le ratio de correction est isole dans `brick6_clip_shifter_set_pitch_correction(pitch_ratio / timing_ratio)`, `Grain` pilote la taille de fenetre, `Hop` et `Search` restent sans effet dans ce mode.
- Le runtime lourd `Sampler/Clip` n'est plus porte par `SEQ_TRACK_COUNT`: il est borne a `BRICK6_MAX_CLIP_TRACKS=4` via un pool de slots locaux. Les tracks `Clip` supplementaires sont filtrees en amont par le catalogue UI; si aucun slot runtime n'est disponible au start, `Shifter` retombe explicitement sur `Speed` sans crash.
- Le dispatch playback reste local a `brick6_master_buffer_read_playback()`: lecture brute `live_recorder_read()` en bypass/fallback, moteur stretch local uniquement quand il est explicitement pret.
- Le lifecycle du stretch buffer reste pilote par `brick6_master_buffer`: invalidation sur clear/debut de record, republication explicite de la source sur fin auto ou stop manuel.
- L'analyse stretch (metadata/transients/anchors) reste hors IRQ et est servicee depuis la superloop via `brick6_app_process()`, par slices bornees et reliees a une `source_generation` explicite.
- Le moteur stretch playback `Master/Buffer` reste local a `brick6_master_buffer_stretch`, mais il n'utilise plus de phase-vocoder spectral: il repose sur une lecture time-domain par grains fenetres, OLA et ring de sortie statique.
- Les profils de qualite visibles ont disparu; le moteur est maintenant regle explicitement par `Grain` et `Hop` dans le seam local.
- `Pitch=Off` force un fallback varispeed local tres leger; `Pitch=On` active l'OLA time-domain borne.
- Criteres de validation manuelle a conserver pour `Master/Buffer` timestretch:
  - `OFF` doit retomber sur la lecture brute existante,
  - `NORMAL` doit conserver le pitch en variation de `SYNC_LEN`/`Src BPM`,
  - les changements `Grain/Hop` ne doivent pas casser la stabilite du seam local,
  - `REC/CLEAR/stop manuel/start transport` doivent invalider ou republier explicitement la source stretch,
  - `XFade`, `Fade In/Out`, `Q Rec`, `Q Play` et restore pattern/project ne doivent pas contourner le seam local `brick6_master_buffer_read_playback()`.

## 9. Dependances inter-zones

- Z2 Track Runtime Authority:
  - `brick6_audio_runtime` choisit engines/mix targets via `track_runtime`.
  - `mixer` resolve `logical_track <- mix_track` pour capture master-buffer.
- Z3 Param/Mod:
  - Param configure mixer/fx/gains; `mod_lfo_v1_process_block` est appele dans DSP.
- Z4 Seq Clock Scheduler:
  - `seq_runtime_audio_collect_block_events` et `audio_apply_event` pilotent la segmentation sample-accurate.
- Z5 UI:
  - configure indirectement families/types/params mais hors chemin IRQ.
- Z6 Persistence:
  - hors pipeline IRQ; impacte etat charge mais pas l'ordonnancement hard-RT direct.

## 10. Dette technique observee

Points factuels:
- Responsabilites concentrees: `brick6_audio_runtime_dsp` cumule orchestration engines + modulation + sampler + taps + blend buffer.
- Ordre d'appel tres contraint:
  - `mixer_external_inputs_clear` appele a la fois dans runtime et mixer (redondance defensive).
  - Le blend master-buffer est applique apres `mixer_process`, donc hors logique de bus mixer.
- Branche speciale Master/Buffer presente dans Z1 mais bornee:
  - capture: `mixer_process` (`brick6_master_buffer_begin_block` -> `brick6_master_buffer_submit_track_post_fader` -> `brick6_master_buffer_commit_block`).
  - ownership runtime capture/playback: `brick6_master_buffer.c` (etat recorder, sources, quantize, loop/read).
  - lecture playback + point de blend: `brick6_audio_runtime_dsp` apres `mixer_process` et avant `SD_RECORDER_TAP_MASTER`.
  - autorite routage source capture: mapping `mix_track -> logical_track` via `track_runtime_get_logical_track_for_mix_track` dans `mixer_process`.
  - aucun second backend recorder concurrent observe in-tree.
- Etat courant recorder SD/stems:
  - bypass produit local par `SD_RECORDER_PRODUCT_ENABLED=0` dans `sd_multitrack_recorder.c`,
  - call-sites IRQ conserves pour `sd_recorder_audio_block_begin`, `SD_RECORDER_TAP_TRACK_RAW` et `SD_RECORDER_TAP_MASTER`,
  - retour immediat des hooks publics principaux tant que le flag reste a `0`,
  - reactivation future conditionnee a une integration UI/produit et a une validation IRQ/audio dediee.
- Cout CPU variable par bloc observe:
  - segmentation en sous-segments selon nombre d'evenements seq dans `process_half`.
  - render synth conditionnel selon nombre de tracks bindees.
- Divergence doc/commentaires potentielle:
  - plusieurs commentaires evoquent "test"/"policy" locales; l'autorite runtime effective est le code courant.

Aucune double autorite concurrente du flux IRQ->mix final n'est constatee.

## 11. Impact eventuel sur la cartographie globale

- Z1 est confirmee comme zone coeur hard-RT a frontiere nette (IRQ + conversion + DSP callback + mix).
- `audio_float.c` et `audio_io.c` sont des sous-composants structurels de Z1; sans eux la cartographie de flux est incomplete.
- `Master/Buffer` reste un transverse (Z1/Z2/Z4/Z5), pas une zone primaire autonome.

## 12. Conclusion stricte

`cause trouvee`

## 13. Addendum - send2 delay stereo global

- `PARAM_MIX_SEND2` reste le send amount track-aware vers `send index 1`.
- `send index 1` est maintenant reserve au delay stereo global dedie: il ne passe pas par `fx_pool` et ne s'additionne pas avec `g_send_fx_slot[1]`.
- Le flux produit est:
  - tracks dry -> master,
  - send1 -> reverb globale -> master,
  - send2 -> delay stereo global -> master,
  - delay wet -> reverb globale si `REV > 0`, sans retour reverb -> delay.
- L'autorite d'execution reste `mixer_process()`: accumulation `send_l/r[1]`, appel `fx_delay_stereo_global_process_block()`, ajout du wet `VOL` au bus MAIN et addition du wet `REV` dans l'entree reverb avant traitement reverb.
- La reverb globale est processee uniquement selon l'autorite `Wet`: `fx_reverb_global_is_active()` retourne vrai si `Wet > 0`, et `mixer_process()` appelle alors `fx_reverb_global_process_block()` a chaque bloc audio, meme si `send_l/r[0]` est silencieux.
- `Wet=0` coupe immediatement le cout reverb; aucun gate local base sur l'entree et aucun tail mixer local ne participent a la decision.
- L'entree de la reverb globale est filtree en place par les params globaux `PARAM_MIX_REVERB_HPF` / `PARAM_MIX_REVERB_LPF` dans `mixer_process()`, apres l'eventuel wet delay `REV` et juste avant `fx_reverb_global_process_block()`.
- Le DSP delay vit dans `fx_delay_stereo.*`; ses buffers L/R sont statiques, alignes et places en `AUDIO_COLD_SDRAM`, dimensionnes pour le `1 bar` a 40 BPM.
- V1 expose le contrat 8 params `TIME`, `X`, `WID`, `FDBK`, `HPF`, `LPF`, `REV`, `VOL`; `TIME` est une division musicale sync BPM stockee comme enum et convertie en secondes via l'autorite tempo `seq_runtime`, tandis que le smoothing/interpolation reste dans le DSP delay.
- `X` est un bool ping-pong, `HPF/LPF` filtrent la boucle feedback, `WID` est bipolaire et agit uniquement sur le retour wet hors boucle feedback.
- `VOL=0` garde le retour master inaudible; le delay reste traite si `REV>0` afin d'alimenter la reverb globale.

## 14. Addendum - send2 delay TYPE CLASSIC/DUAL

- `PARAM_MIX_DELAY_TYPE` choisit le backend global send2:
  - `CLASSIC` garde le moteur existant `fx_delay_stereo.*` et reste le default.
  - `DUAL` route le meme bus send2 vers le moteur dedie `fx_delay_dual.*`.
- `send index 1` reste reserve au delay global dedie; `fx_pool` ne redevient pas autorite de send2.
- L'autorite d'execution reste `mixer_process()`:
  - accumulation `send_l/r[1]`,
  - dispatch exclusif CLASSIC ou DUAL,
  - ajout wet vers MAIN via `VOL`,
  - ajout wet vers reverb globale via `REV`.
- `fx_delay_dual.*` porte un dual delay L/R permanent inspire QDelay:
  - lignes separees delay L/R et Haas width L/R,
  - modes `Normal`, `PingPong`, `Tap`, `ClassicPingPong`,
  - interpolation lineaire sur lecture temps modulee,
  - modulation LFO bornee sur temps de lecture,
  - HPF/LPF simplifie dans le feedback.
- `Tap` suit le contrat QDelay: `TIME` sert de tap/predelay, `TIME_R` sert de temps principal.
- `FBW` mappe le croisement/largeur de feedback; `WID` reste la largeur wet/haas/pingpong selon le mode.
- `SWING` et `ACCENT` sont retires du backend DUAL produit V1; les IDs param restent reserves pour ne pas renumeroter le stockage indexe par `PARAM_COUNT`.
- Fonctions explicitement hors scope du backend DUAL: pitch, shimmer, reverse, diffusion, drive, ducking, phaser, EQ param complete, lo-fi.
- Les buffers longs DUAL sont statiques en `AUDIO_COLD_SDRAM`; aucune allocation runtime audio n'est introduite.

## 14.b Addendum - reverb send RevB unique

- `RevB` est l'unique backend global send1 runtime compile; Drumboy, GVerb et Oliverb runtime sont retires.
- `PARAM_MIX_REVERB_TYPE` reste un tombstone de stockage (`0/RevB`) pour ne pas renumeroter `PARAM_COUNT`; il n'est plus expose dans la page MIX active.
- La reverb reste un SEND global wet-only: `mixer_process()` accumule `send index 0`, applique HPF/LPF d'entree, appelle `fx_reverb_global_process_block()`, puis additionne uniquement le wet stereo au MAIN.
- `RevB` utilise une API locale stable dans `fx_reverb_revb.*`: init/reset, setters, puis `process_send_mono_to_stereo_wet()`.
- `RevB` downmixe l'entree send stereo en mono avant tank, puis sort un wet stereo decorrele; `Wet=0` conserve le bypass cout nul cote mixer.
- Params mappes:
  - `Wet` -> gain d'entree wet-only,
  - `Size` -> diffusion + modulation lente,
  - `Decay` -> feedback/time,
  - `PreD` -> predelay local,
  - `LPF` -> damping interne en plus du prefiltre d'entree global.
- `Surr` reste reserve/tombstone et n'a pas d'effet runtime RevB.
- RAM conservee pour RevB: `g_revb_engine_buffer[32768]` en D1 via `AUDIO_WARM` soit environ 128 KiB, plus predelay environ 17 KiB en D1 et scratch bloc DTCM.
- RAM liberee estimee par retrait runtime: Drumboy environ 60 KiB DTCM + 21 KiB RAM_D2, GVerb environ 1.28 MiB SDRAM + petit etat DTCM, Oliverb environ 128 KiB SDRAM + scratch/etat DTCM.
- Cout IRQ attendu: environ 3% avec le buffer RevB en `AUDIO_WARM`.
- Un point de mesure DWT local est expose par `fx_reverb_global_get_last_cycles()` / `fx_reverb_global_get_max_cycles()` autour du process reverb global.
- Les sources Mutable/Inspiration dormantes (`clouds/*`, `rings/*`, `braids/*`, `plaits/*`, `stmlib/*`, `Inspiration/*`) ne sont pas supprimees par ce retrait car elles peuvent servir d'autres ports ou references non-runtime.

## 15. Addendum - retrait COLORS/CRUNCH

- La page `COLORS/CRUNCH` est retiree du produit.
- Les params track-aware `PARAM_FILTER_DRIVE`, `PARAM_FILTER_DECIMATOR_BITS`, `PARAM_FILTER_DECIMATOR_RATE` et `PARAM_FILTER_DECIMATOR_RATE2` ne sont plus exposes par COLORS, ne sont plus p-lockables/macro-assignables, et ne reappliquent plus de runtime track insert.
- La policy boot ne pre-active plus le slot `FX_SAT` en slot 1.
- `fx_saturation.*` reste present comme code legacy/global non expose par COLORS; il n'est plus branche par le runtime COLORS track-aware.

## 16. Addendum - retrait moteurs Drum legacy

- Les moteurs DSP Drum legacy `TRX_*` et `FM_*` sont retires du build.
- `drum_synth` reste la facade RT-safe Drum: `DRUM_MODEL_ID_NONE` rend zero, `DRUM_MODEL_ID_BD_ANALOG` instancie directement `plaits::AnalogBassDrum` en etat statique par instance, sans `plaits::Voice`, sans CTAG et sans allocation dynamique.
- `brick6_audio_runtime` conserve le chemin track-aware Drum vers le mixer; les types legacy restent mappes vers `DRUM_MODEL_ID_NONE`, tandis que `TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG` mappe vers `DRUM_MODEL_ID_BD_ANALOG`.
- Sortie Drum active: mono-native vers `mixer_submit_external_mono_native`, zero tant que le modele est `NONE` ou tant qu'aucun `note_on` PLAY n'a arme le moteur.
- Cout IRQ attendu: un rendu `AnalogBassDrum::Render()` par track `BD_ANALOG` active et par bloc audio, avec SVF/one-pole/sine oscillator par sample. Le point de mesure existant reste `cpu_load` autour de l'IRQ audio; il n'existe pas encore de compteur DWT local dedie Drum.
