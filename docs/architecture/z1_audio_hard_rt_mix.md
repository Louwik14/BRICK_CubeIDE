# Z1 - Audio Hard-RT et Mix

## 1. Perimetre

Perimetre operationnel de zone (appartient a Z1):
- `Src/Audio/audio.c`
- `Src/Core/brick6_audio_runtime.c`
- `Src/Audio/mixer.c`

Elargissements necessaires (preuves de frontiere et contrats):
- `Src/Audio/audio_float.c` et `Inc/Audio/audio_float.h` : frontiere IRQ `int24 <-> float`, ownership des buffers track et callback DSP.
- `Src/Audio/audio_io.c` : preuve unpack/pack TDM et mapping slots.
- `Src/Audio/dsp_engine.c` : preuve d'autorite callback DSP unique.
- `Src/Core/brick6_sampler_runtime.c` + `Inc/Core/brick6_sampler_runtime.h` : point d'insertion unique du futur moteur Sampler, sans pipeline audio parallele.
- `Src/Core/brick6_sampler_runtime.c` + `Inc/Core/brick6_sampler_runtime.h` : backend mono minimal du Sampler branche sur le point d'insertion unique, en lecture via `sample_cache` RAM.
- `Src/Sampler/sample_cache.c` + `Inc/Sampler/sample_cache.h` : owner de la memoire audio runtime Sampler; `brick6_sampler_runtime` lit le cache uniquement, sans acces SD ni lecture directe `sample_desc->data`.
- `Src/Core/brick6_sampler_runtime.c` + `Inc/Core/brick6_sampler_runtime.h` : slice grid v1 reconstruite hors IRQ, selection de slice par note en mode `Slice`.
- `Inc/Audio/mixer.h` : cardinalite mixer (`MIXER_MAX_TRACKS = SEQ_TRACK_COUNT`) et contrat public.
- `Src/Audio/sd_multitrack_recorder.c` + `Inc/Audio/sd_multitrack_recorder.h` : preuve des taps recorder dans le chemin audio.
- `Src/Core/brick6_master_buffer.c` + `Inc/Core/brick6_master_buffer.h` : preuve du branchement bloc debut/fin et read playback dans pipeline.
- `Src/Core/brick6_master_buffer_stretch.c` + `Inc/Core/brick6_master_buffer_stretch.h` : seam local du futur timestretch playback Master/Buffer, borne a une instance unique et sans pipeline parallele.
- `Src/Seq/seq_runtime.c` + `Inc/Seq/seq_runtime.h` : preuve collecte/apply des evenements audio sample-accurate.
- `Src/Core/brick6_app_init.c` : preuve du wiring `audio_set_float_callback(brick6_audio_runtime_dsp)`.

Dependances de Z1 sans appartenir a Z1:
- Engines synth/sampler (`drum`, `voice_manager`).
- `track_runtime` (mapping track logique -> cible mix).
- `mod_lfo_v1` (modulation bloc).
- `seq_runtime` (event scheduling audio).
- `sd_multitrack_recorder` (taps et writer hors IRQ).
- `brick6_master_buffer` (capture post-fader + lecture playback).
- `brick6_master_buffer_stretch` (etat runtime local du timestretch playback, sans ownership du recorder brut).
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
- Ordonne render engines externes, modulation bloc, sampler, mixer, crossfade master/buffer, taps master.

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
  - `SD_RECORDER_TAP_TRACK_POST_INSERT`, `POST_FADER`, `POST_SEND` dans `mixer`.
- Vers master buffer:
  - `brick6_master_buffer_begin_block`, `submit_track_post_fader`, `commit_block` dans `mixer`.
  - `brick6_master_buffer_read_playback` + blend final dans `brick6_audio_runtime`.

Contrats timing sortants:
- Taps et master-buffer sont synchrones du bloc audio courant (dans IRQ).
- Ecriture SD n'est pas faite dans Z1 IRQ (writer service hors IRQ), Z1 ne fait que pousser/capturer.

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
- temporaires bloc `drum_tmp`, `recL`, `recR`
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
- `g_send_fx_slot[MIXER_NUM_SENDS]`, `g_reverb`
  - Ecriture: `mixer_set_send_fx_slot`, `mixer_set_reverb_*`.
  - Lecture: `mixer_process`.
  - Role: routing sends/reverb global.
- `g_external_track_l/r`, `g_external_track_enabled`
  - Ecriture: `mixer_submit_external_mono` (depuis `brick6_audio_runtime`).
  - Lecture+clear: `mixer_process`, `mixer_external_inputs_clear`.
  - Role: injection sources engines externes dans lanes mixer.
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
  - rendu engines externes (Drum) -> `mixer_submit_external_mono`
  - `mod_lfo_v1_process_block`
  - `voice_manager_process`
  - tap `SD_RECORDER_TAP_TRACK_RAW`

5) Rendu engines/tracks
- Le callback DSP effectif est `brick6_audio_runtime_dsp` (via `dsp_engine`).
- `mixer_external_inputs_clear` puis injections engines.

6) Mixage bus / sends / master
- `mixer_process`:
  - begin block master-buffer
  - per-track: inserts -> filter/EQ/VCA -> gains/pan -> sends -> route MAIN/CUE
  - returns reverb/send FX
  - ecrit resultat dans `tracks[0]` (MAIN) et `tracks[1]` (CUE)

7) Taps recorder / master-buffer
- Taps post-insert/fader/send dans `mixer_process`.
- submit post-fader vers master-buffer dans `mixer_process`.
- commit master-buffer fin de mix.
- post-mix: `brick6_audio_runtime_dsp` lit playback buffer et applique xfade live/recorded sur `tracks[0]`.
- tap final `SD_RECORDER_TAP_MASTER`.

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

Memoire:
- Scratch bus dans `mixer_process` en statique fonction.
- Lanes externes mixer `g_external_track_l/r` dimensionnees `MIXER_MAX_TRACKS x AUDIO_BLOCK_SIZE`.

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
- Le Sampler track-aware lit via `sample_cache` en RAM. `sample_pool` reste catalogue/projet/metadata; `sample_desc->data` est une compat legacy hors autorite audio principale.
- Stabilisation actuelle `sample_cache`: le chemin Sampler track-aware supporte le playback forward simple; reverse, loop, slice avance et pitch/resampling temps reel restent hors chemin cache stabilise.
- Master-buffer est dans le pipeline de bloc (`begin -> submit -> commit`) et son playback est blend apres mixer dans `brick6_audio_runtime_dsp`.
- Le futur stretch Master/Buffer reste un seam local du playback buffer: `brick6_master_buffer` garde l'ownership du buffer et `live_recorder` garde l'ownership du stockage/lecture brute.
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
