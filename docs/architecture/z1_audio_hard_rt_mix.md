# Z1 - Audio Hard-RT et Mix

## Addendum 2026-07-27 - pool SEND delay borne a 6 s

- Le pool SDRAM partage des delays globaux SEND `CLASSIC`/`DUAL` est dimensionne sur la capacite reellement exploitable par le contrat courant 6 s: `FX_DELAY_SHARED_CAPACITY=288008` samples par canal, soit 2 304 064 octets pour L/R float32.
- Les anciennes capacites separees `CLASSIC=288002` et `DUAL=432002` sont retirees: `CLASSIC` et `DUAL` restent mutuellement exclusifs dans `mixer_process()` et utilisent le meme pool.
- `DUAL` conserve sa reserve interne de redimensionnement `ceil(time)+8`; `CLASSIC` conserve son clamp temps a 6 s via `kMaxDelaySeconds`, malgre les quelques samples de garde supplementaires du pool commun.

## Addendum 2026-07-27 - optimisation IRQ Synth/Wave CLEAN

- `brick6_wave_runtime_render_instance()` retourne maintenant si un bloc a produit un signal Wave utile; Z1 ne soumet plus de buffer mono zero au mixer pour une instance inactive.
- Le rendu Wave prepare un contexte par oscillo au debut du bloc: resolution table, pointeur SDRAM `FLOAT32_MONO`, `frame_count`, flags `FLIP`, niveau et bornes derivees. La boucle sample ne consulte plus `sample_global_pool` ni `wavetable_pool_get_slot()`.
- Les oscillateurs a `LEVEL=0` ou table invalide sont exclus du hot path sample; leur smoothing `POS` avance en bloc pour garder l'etat local coherent sans lecture wavetable.
- Le chemin CLEAN garde l'interpolation lineaire intra-frame et inter-frames, mais saute la seconde frame quand `frame_count==1`, `frame0==frame1` ou `frame_frac==0`.
- Le wrap de phase utilise un `if` rapide avec fallback borne pour les increments extremes; la justesse pitch reste portee par le meme `phase_inc` float.

## Addendum 2026-07-27 - declick note-on Synth/Wave

- `brick6_wave_runtime` garde un anti-click local au moteur: au note-on, la nouvelle forme Wave est crossfadee depuis le dernier sample de sortie de l'instance pendant `32` samples audio.
- Ce declick ne remplace pas l'enveloppe musicale VCA mixer; il borne seulement la discontinuite creee par le reset de phase/POS/velocity propre a Wave.
- Le fast path POS stable calcule `frame0/frame1/frame_frac` et les pointeurs de frames une seule fois par bloc quand `pos_smoothed` est deja au target remappe `START/END/POS`; l'interpolation inter-frame CLEAN reste active seulement si deux frames distinctes et une fraction non nulle sont necessaires.

## Addendum 2026-07-27 - smoothing POS Synth/Wave

- `brick6_wave_runtime` lisse `POS` localement par oscillateur dans le chemin audio, apres remap `START/END` et avant selection de frame.
- Le smoothing est un one-pole borne (`WAVE_POS_SMOOTH_COEFF=0.004`) avec snap final court; il ne modifie ni `track_tone_sound_state`, ni `param_store`, ni les bases Matrix.
- L'etat `pos_smoothed` avance meme quand `LEVEL=0` ou quand la table n'est pas prete, afin qu'un oscillo reactualise ne saute pas brutalement vers une ancienne position.

## Addendum 2026-07-27 - preview wavetable hors IRQ

- `wavetable_pool` construit a la fin du chargement une preview compacte `WAVETABLE_PREVIEW_COLUMNS=124`, avec min/max par colonne et peak global.
- Cette preview est stockee avec le slot resident SDRAM et invalidee par generation; elle est calculee hors IRQ audio, jamais pendant le rendu UI ou audio.
- Z5 consomme uniquement cette preview pre-calculee pour les pages `OSC1/2 WAVE`; le scroll `TABLE` peut donc changer de table sans scan complet des frames a l'ecran.

## Addendum 2026-07-26 - Retrigger hard/soft ENV

- Le mixer porte maintenant deux flags runtime par lane, `filter_retrigger_hard` et `vca_retrigger_hard`, projetes depuis les params track-aware `PARAM_ENV_RETRIG_FILTER` et `PARAM_ENV_RETRIG_VCA`.
- `ON` conserve le comportement hard historique: l'enveloppe repart de zero au note-on. `OFF` utilise le retrigger soft de l'enveloppe existante: attaque depuis la valeur courante, sans rampe/declick/zero-cross additionnel.
- Le chemin VCA amplitude reste `adsr_daisy_c_t`; le chemin filtre reste `env_adsr_peaks_t` via `env_adsr`. Les moteurs Prism/Stack/Braids ne sont pas modifies.

## 1. Perimetre

Perimetre operationnel de zone (appartient a Z1):
- `Src/Audio/audio.c`
- `Src/Core/brick6_audio_runtime.c`
- `Src/Audio/mixer.c`

Elargissements necessaires (preuves de frontiere et contrats):
- `Src/Audio/audio_float.c` et `Inc/Audio/audio_float.h` : frontiere IRQ `int24 <-> float`, ownership des buffers track et callback DSP.
- `Src/Audio/audio_io.c` : preuve unpack/pack via `board_audio_unpack_input` / `board_audio_pack_output` et mapping slots par variante.
- `Inc/Board/board_audio*.h`, `Board/Premium/Src/board_audio_premium.c`, `Board/LowCost/Src/board_audio_lowcost.c`: frontiere codec/SAI/DMA/format physique. Premium = CS42448 SAI2 TDM8 MAIN/CUE physique; low-cost = TLV320AIC3204 SAI1 I2S stereo, MAIN physique uniquement, CUE conserve en bus interne.
- `Src/Audio/audio_io.c` repacke MAIN/CUE sans calcul de VU/peak/clip produit; la saturation TX reste locale a la conversion int24.
- `Src/Audio/metronome_runtime.c` + `Inc/Audio/metronome_runtime.h` : generateur metronome hard-RT RAM-only, declenche par event Z4, rendu MAIN monitor-only.
- `Src/Audio/dsp_engine.c` : preuve d'autorite callback DSP unique.
- `Src/Core/brick6_sampler_runtime.c` + `Inc/Core/brick6_sampler_runtime.h` : point d'insertion unique du futur moteur Sampler, sans pipeline audio parallele.
- `Src/Core/brick6_braids_runtime.cpp` + `Inc/Core/brick6_braids_runtime.h` : runtime Prism multi-instances (une instance mono par track Prism) autour de `braids::MacroOscillator`, rendu en sous-blocs de 24 samples puis injecte via `mixer_submit_external_mono_native`.
- `Src/Core/brick6_wave_runtime.c` + `Inc/Core/brick6_wave_runtime.h` : runtime `Synth/Wave` mono, deux oscillateurs wavetable utilisateur par instance, lecture SDRAM uniquement depuis `wavetable_pool`, interpolation lineaire intra-frame et inter-frames, puis injection par `mixer_submit_external_mono_native`.
- `Src/Core/brick6_sampler_runtime.c` + `Inc/Core/brick6_sampler_runtime.h` : backend stereo du Sampler branche sur le point d'insertion unique, en lecture via `sample_cache` RAM.
- `Src/Core/brick6_sampler_runtime.c` : declick minimal des stops/steals Sampler par capture du dernier echantillon rendu et tail RAM-only courte, mixee dans le buffer Sampler avant injection mixer.
- `Src/Sampler/sample_cache.c` + `Inc/Sampler/sample_cache.h` : facade produit Sampler en RAM; `brick6_sampler_runtime` lit le cache uniquement, sans acces SD ni lecture directe `sample_desc->data`.
- `Src/Sampler/multi_sample_pool.c` + `Inc/Sampler/multi_sample_pool.h` : autorite metadata RAM-only du futur `Sampler/Multi` (instruments, samples, zones, resolve note/velocity); aucun SD, aucun playback, aucun acces page-cache dans cette phase.
- `Src/Sampler/multi_sample_loader.c` + `Inc/Sampler/multi_sample_loader.h` : LOAD cooperatif du futur `Sampler/Multi`, hors IRQ, qui mappe `.brickmulti` vers `multi_sample_pool` puis prepare la ration froide 8192 frames, ou tout le sample si plus court, via le `sample_page_cache`/`sample_stream_manager` uniques.
- `Src/Sampler/sampler_ram_pool.c` + `Inc/Sampler/sampler_ram_pool.h` : backend resident RAM v1. Il charge explicitement des WAV PCM 16/24-bit mono/stereo hors IRQ vers un stockage interne `FLOAT32_INTERLEAVED` stereo alloue dans des pages permanentes du `SAMPLE_PAGE_SLOT_POOL`, conserve sample_rate/frames/cout/generation, expose `channels=2`, inscrit les slots comme `kind=RAM` dans `sample_global_pool`, et porte l'overview waveform min/max derivee du sample RAM. Le consommateur IRQ courant est `Sampler/RAM`, normal ou sliced via `Slice Count`.
- `Src/Sampler/wavetable_pool.c` + `Inc/Sampler/wavetable_pool.h` : backend resident WAVETABLE. Il charge hors IRQ un fichier `B6WT` depuis SD vers le meme `SAMPLE_PAGE_SLOT_POOL`, stocke les frames en `FLOAT32_MONO`, inscrit les slots comme `kind=WAVETABLE` dans `sample_global_pool`, et ne lit jamais la SD depuis l'audio IRQ.
- `Src/Sampler/sample_stream_manager.c` + `Inc/Sampler/sample_stream_manager.h` : seam STREAM Sampler; phase courante = proprietaire de la policy service STREAM pool, d'un pool statique de readers FatFs persistants par cle audio STREAM active, et d'un scheduler simple fair/deadline par priorite de page. Son service est cooperatif: il limite pages/operations FatFs/ticks par appel et rend le gate SD rapidement si du travail STREAM reste pending.
- `Src/Sampler/sample_stream_fatfs_map.c` + `Inc/Sampler/sample_stream_fatfs_map.h` : certification hors IRQ des WAV STREAM contigus via FatFs CLMT. Les acces aux champs internes FatFs restent confines ici. Un fichier non certifie conserve le backend FatFs historique.
- `Src/Sampler/sample_stream_backend_contiguous.c` + `Inc/Sampler/sample_stream_backend_contiguous.h` : backend V1 `STREAM_SAFE_CONTIGUOUS`; remplit une page cache float stereo depuis des secteurs SD physiques deja certifies, hors IRQ et sous l'autorite du `sample_stream_manager`.
- `Src/SD/sd_block_device.c` + `Inc/SD/sd_block_device.h` : wrapper minimal de lecture secteurs hors IRQ, utilise par le backend contigu uniquement pendant que `sd_access_gate` est deja tenu.
- `Src/Sampler/sample_page_cache.c` + `Inc/Sampler/sample_page_cache.h` : seam local du cache pagine Sampler; en phase actuelle, `READY_FULL` peut etre charge par pages float stereo contigues en SDRAM sans modifier le chemin audio stream, et le stockage/acquire/release des pages reste ici. Le lookup hot passe par un index statique borne keyed par `sample_audio_key_t {domain, object_id}` + `page_index`; les scans free/evict conservent un passage borne avec curseur.
- `Src/Sampler/sample_voice_reader.c` + `Inc/Sampler/sample_voice_reader.h` : helper local Sampler pour le fast path bloc RAM-only; aucune SD, aucune policy musicale globale.
- `Src/Core/brick6_clip_shifter.c` + `Inc/Core/brick6_clip_shifter.h` : pitch-shifter stereo local du mode `Sampler/Stream` `Shifter`, port C borne sans import Clouds/FxEngine.
- `Src/Core/brick6_sampler_runtime.c` + `Inc/Core/brick6_sampler_runtime.h` : slice grid v1 reconstruite hors IRQ, selection de slice par note en mode `Slice`.
- `Inc/Audio/mixer.h` : cardinalite mixer (`MIXER_MAX_TRACKS = SEQ_TRACK_COUNT`) et contrat public.
- `Src/Audio/fx_delay_shared_pool.c` + `Inc/Audio/fx_delay_shared_pool.h` : pool SDRAM L/R commun aux delays globaux CLASSIC et DUAL, avec ownership exclusif controle par le mixer.
- `Src/Audio/fx_master_macro.c` + `Inc/Audio/fx_master_macro.h` : insert master leger pour les 4 slots `Master/FX` MacroFX, avec core delay mono statique par slot pour `COMB`, `WOBBLE` et `FREEZE`, STUTTER stereo global unique, et coloration sombre/brillant par slot pour `COLOR`.
- `Src/Seq/seq_runtime.c` + `Inc/Seq/seq_runtime.h` : preuve collecte/apply des evenements audio sample-accurate.
- `Src/Core/brick6_app_init.c` : preuve du wiring `audio_set_float_callback(brick6_audio_runtime_dsp)`.

Dependances de Z1 sans appartenir a Z1:
- Engines synth/sampler (`drum`, `voice_manager`, wrappers Prism/Sampler).
- `track_runtime` (mapping track logique -> cible mix).
- `mod_lfo_v1` (modulation bloc).
- `seq_runtime` (event scheduling audio).
- `track_tone_sound_state` pour les valeurs `Master/FX` type/LVL/A/B lues par `fx_master_macro`.
- `fx_chain`, `fx_reverb`, `env_adsr`, `fx_biquad_filter`.

Exclusions explicites:
- UI (`Src/UI/*`) : pilote config mais n'execute pas le flux hard-RT.
- Persistence (`Src/Storage/*`) : hors chemin IRQ audio.
- Shim legacy `runtime_target` : hors autorite du pipeline hard-RT.

Contrat page-cache/streamer:
- L'identite cache audio est `sample_audio_key_t {domain, object_id}` + `page_index`, pas un `sample_id` brut.
- Domaines prevus: `CLASSIC` pour les samples Sampler existants, `LOOPER` pour les transients Looper, `MULTI` reserve au futur Sampler/Multi.
- Les APIs historiques par `sample_id` restent des wrappers `CLASSIC` temporaires. En runtime courant, Stream conserve Classic STREAM; RAM ne les consomment plus.
- `sample_stream_manager` porte la meme cle pour readers, pending requests et load targets; il reste l'unique streamer FatFs et reste hors IRQ.
- `sample_stream_manager` reste l'unique streamer Sampler. Le backend `STREAM_SAFE_CONTIGUOUS` ne cree ni queue ni scheduler parallele: il remplace seulement la maniere de remplir une page `QUEUED -> LOADING -> READY` quand la metadata physique du sample est certifiee contigue.
- La metadata de streaming safe est portee par le `sample_page_cache` par `sample_audio_key_t`; Classic et Multi la partagent via le meme stream info. Looper RAW reste sur le backend existant dans cette phase.
- Le scratch du backend contigu est statique, aligne 32 octets, en SDRAM de scratch storage, et dimensionne a 9 secteurs de 512 octets pour couvrir une page source maximale actuelle plus un offset secteur.
- `Sampler/Looper` utilise `domain=LOOPER`; son `cache_id` restant est un identifiant legacy/diagnostic, pas l'autorite cache.
- Capacites logiques: `CLASSIC` garde 64 ids, `LOOPER` garde une fenetre 64 ids, `MULTI` reserve 512 ids (`object_id 0..511`) sans reserver physiquement 512 pages au boot.
- Capacite physique actuelle: `SAMPLE_PAGE_MAX_COUNT` reste le plafond de pages RAM READY/QUEUED/LOADING simultanees tous domaines confondus. Avec la config 20 MiB / pages stereo float de 2048 frames, le plafond theorique est 1280 pages; preparer 16 pages pour 512 samples Multi consommerait 8192 pages virtuelles et reste donc hors contrat produit.
- Le budget global utilisateur reste fixe a 16 MiB pour les slots sample longs; les pages stereo float font 2048 frames / 16 KiB et le pool physique total page-cache est de 1280 pages.
- Une requete `MULTI` ne peut pas evincer une page non-`MULTI`; si le pool est plein a cause de Classic/Looper, l'allocation Multi echoue proprement au lieu de degrader les comportements existants.
- Le pool produit partageable est strictement `SAMPLE_PAGE_SLOT_POOL_COUNT` pages, soit `SAMPLE_PAGE_PRODUCT_SLOT_POOL_PAGES=1024` pages de `SAMPLE_PAGE_BYTES=16384` octets dans la configuration courante: 16 777 216 octets (16 MiB). Les ranges `SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES=128` et `SAMPLE_PAGE_PRODUCT_MARGIN_PAGES=128` restent fixes et hors consommation permanente des samples RAM.
- RAM v1 utilise ce meme `SAMPLE_PAGE_SLOT_POOL` que les presocles Stream/Multi: `sampler_ram_pool` demande des runs contigus de pages via `sample_page_cache_alloc_slot_pool_bytes()`, les garde pinnees comme pages permanentes brutes, stocke le WAV converti en `FLOAT32_INTERLEAVED` stereo, construit ensuite une overview waveform slot-owned hors IRQ, et libere ces pages au clear/reset via `sample_page_cache_release_slot_pool_allocation()`.
- Le cout RAM enregistre dans `sample_global_pool.cost_bytes` est la capacite physique reelle allouee en pages SLOT_POOL (`ceil(data_bytes / SAMPLE_PAGE_BYTES) * SAMPLE_PAGE_BYTES`), pas un alignement logique de petit buffer. Aucun fallback vers `sample_cache`, `sample_voice_reader`, `sample_cache_start_voice_at` ou le streamer Classic; `Sampler/RAM` et `Sampler/RAM sliced mode` RAM lisent directement le `FLOAT32_INTERLEAVED` stereo resident.
- WAVETABLE utilise la meme politique de pool resident que RAM: pages permanentes allouees par `sample_page_cache_alloc_slot_pool_bytes()`, cout global aligne sur les pages physiques et liberees par `sample_page_cache_release_slot_pool_allocation()`. Le futur runtime `Synth/Wave` devra lire uniquement ce pointeur SDRAM resident.

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

Autorite de mixage final:
- `mixer_process()` dans `Src/Audio/mixer.c`.
- Possede la sommation tracks -> MAIN/CUE/SEND/returns et les taps post-insert/post-fader/post-send.

Autorite de monitoring final:
- `audio_io_pack_ramped()` ajoute le metronome MAIN monitor-only apres `mixer_process()`, `fx_master_macro_process_block()` et `sd_preview_render_main()`, juste avant conversion TX.
- Cette injection ne touche pas CUE et ne repasse pas par tracks, sends, Master/FX, Looper, Audio Rec ni preview/cache SD.
- Le metronome bypass le gain de sortie final deja applique au MAIN musical; son gain propre reste borne par `METRO` et `METRO_MAX_GAIN`.

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
- Evenements sequenceur audio (`seq_runtime_audio_collect_block_events`, `seq_runtime_audio_apply_event`).

Contrats implicites critiques:
- `AUDIO_FRAMES_PER_HALF` dans `audio.c` doit rester coherent avec `AUDIO_BLOCK_SIZE` (`audio_float.h`).
- Les offsets d'evenements de `seq_runtime_audio_collect_block_events`, markers boundary inclus, sont supposes dans `[0..frames]` (code clamp a `AUDIO_FRAMES_PER_HALF`).
- Le callback DSP (`dsp_engine`) doit etre O(1) borne et sans blocage.

## 4. API sortantes

Sorties directes de Z1:
- Vers DMA TX: buffer `tx_buffer` via `HAL_SAI_Transmit_DMA` (data preparee dans `process_half`).
- Vers scheduler systeme: `engine_tasklet_notify_frames(AUDIO_FRAMES_PER_HALF)`.
- Vers runtime sequenceur: `seq_runtime_audio_apply_event()` au sample offset.

Contrats timing sortants:
- Ecriture SD record future interdite dans Z1 IRQ: Z1 pourra seulement exposer des producteurs vers rings RAM prealloues.

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
  - Ecriture/Lecture: `brick6_audio_runtime_dsp`.
  - Role: gating des engines, modulation bloc et orchestration mix/master.
- temporaires bloc `drum_tmp`, `plaits_tmp`, `braids_tmp`
  - Role: scratch per-block pour rendu engines.
- `brick6_sampler_runtime` maintient un petit pool statique de tails de declick Sampler:
  - Ecriture: stops/steals Sampler apres capture du dernier signal rendu par voix.
  - Lecture/mix: `brick6_sampler_runtime_render_track`, dans le buffer Sampler pre-mixer.
  - Role: eviter les discontinuites de coupure sans garder reader/cache/streamer vivant.
  - Contraintes: RAM-only, pas de SD, pas d'allocation, pas de pression page-cache.

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
- Les lanes mixer 0..2 restent reservees aux entrees Input proto; une track moteur ne doit pas y stocker son state lane-bound, sinon un scroll CFG passant par `Input1..3` peut reinitialiser la lane d'une autre track.
- Les changements structurels locaux utilisent le rebind cible d'une seule lane logique; les lanes des autres tracks restent no-op et ne sont plus reset/recopiees.
- Apres un rebind local neuf, `mixer_snap_track_runtime_state()` aligne uniquement la lane cible sur les targets reappliquees (`gain/pan/sends`, cutoff/resonance/EQ) pour ne pas entendre les defaults internes du mixer avant la prochaine rampe.
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

- Oui, implementee directement dans Z1 (`brick6_audio_runtime.c` + `mixer.c`) comme appels de service synchrones bloc.

## 6. Flux runtime

Flux nominal prouve par code:

1) Entree DMA / callback
- `HAL_SAI_RxHalfCpltCallback` ou `HAL_SAI_RxCpltCallback` (`audio.c`).

2) Decoupe half/block
- `process_half(half_index)` calcule offset half et invalidation D-cache RX.
- Recupere evenements bloc et markers boundary via `seq_runtime_audio_collect_block_events`.
- Ce call consomme aussi les pulses step du sequencer (interne + externes pending) en domaine sample avant extraction des events dus du bloc.
- Coupe le half en sous-segments selon offsets events, appelle `audio_process_block_int32` par segment.

3) Unpack / conversion
- `audio_process_block_int32` -> `audio_io_unpack`:
  - int24 TDM slots (0/1,2/3,4/5) -> `tracks[0..2].L/R` float.
  - lane 3 (interne) est explicitement zeroee.

4) Collecte des events/sources
- Dans `brick6_audio_runtime_dsp`:
  - refresh runtime tracks
- rendu engines externes (Drum, Prism mono par instance, Sampler stereo) -> `mixer_submit_external_*`
  - `mod_lfo_v1_process_block`
  - `voice_manager_process`

5) Rendu engines/tracks
- Le callback DSP effectif est `brick6_audio_runtime_dsp` (via `dsp_engine`).
- `mixer_external_inputs_clear` puis injections engines.

6) Mixage bus / sends / master
- `mixer_process`:
  - calcule un `lane_plan` local par lane (`source mono-native`, `source stereo`, `promotion stereo requise`, `fallback stereo`)
  - per-track stereo: inserts -> filter/EQ/VCA -> gains/pan -> sends -> route MAIN/CUE
  - per-track mono-native: filtre biquad mono ou EQ3 mono -> inserts mono-compatibles -> VCA+gain dans la boucle commune -> projection vers `L/R` seulement au point utile pour taps, sends, routing MAIN/CUE et accumulation bus
  - `EQ3` mono est un bloc mono reel pris directement par le `lane_plan`; une lane mono-native avec `EQ3` actif ne doit plus etre promue stereo pour appeler `EQ3` stereo avec `L/R` dupliques
  - la projection `mono -> L/R` reste tardive et centralisee: taps `POST_INSERT`, boucle commune `VCA+gain+pan`, puis consommation `POST_FADER`, sends et bus
  - le chemin stereo reste la reference fonctionnelle et ne met plus a jour les etats mono auxiliaires (`biquad_mono`, `eq3_mono`) quand la lane execute deja en stereo
  - returns reverb/send FX
  - ecrit resultat dans `tracks[0]` (MAIN) et `tracks[1]` (CUE)

- post-mix: `fx_master_macro_process_block` applique les slots `Master/FX` legers sur `tracks[0]`, puis preview SD.
- La preview SD est un chemin d'audition UI temporaire: `sd_preview_render_main()` lit `g_sd_preview_ring` place en `AUDIO_COLD_SDRAM`; le cout SDRAM en IRQ n'existe que pendant une preview active et ne concerne pas le playback principal ni le streaming Sampler.

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
- Le profiler `cpu_load` historique entoure l'IRQ audio avec `DWT->CYCCNT`. Les IRQ audio SAI2/DMA1 Stream3/4 sont placees a priorite 1 et USB Host OTG_HS a 7 afin que l'audio reste prioritaire.

Memoire:
- Scratch bus dans `mixer_process` en statique fonction.
- Lanes externes mixer `g_external_track_l/r` dimensionnees `MIXER_MAX_TRACKS x AUDIO_BLOCK_SIZE`.

Placement memoire valide pour la reverb SEND runtime:
- `RevB` est l'unique backend reverb runtime compile.
- `g_revb_engine_buffer[32768]` et le predelay RevB restent en D1 via `AUDIO_WARM`.
- Les anciens buffers runtime Drumboy (feedback DTCM, predelay/surround RAM_D2), GVerb et Oliverb sont retires.
- Le code dormant Mutable/Inspiration non compile n'appartient pas au backend SEND runtime et n'est pas concerne par ce retrait.

Placement code ITCM:
- `ITCMRAM` est disponible comme region linker dediee au code hot.
- La macro explicite `AUDIO_CODE_HOT` cible la section `.itcm_text`.
- Aucune fonction audio n'est placee en ITCM pour l'instant; la passe RevB ITCM a ete retiree faute de gain IRQ attendu.
- Avant toute future annotation, le mecanisme de copie boot de `.itcm_text` devra etre reinstalle et valide explicitement.

Granular / fx_pool:
- Le backend granular legacy de `fx_pool` est retire du produit: plus de buffers `grain_buffer_l/r`, plus de storage granular SDRAM, et `FX_GRANULAR` reste seulement un tombstone refuse par `fx_pool_activate_slot()`.
- Les params historiques `PARAM_GRAN_*` restent des tombstones no-op pour conserver les IDs tant qu'aucune rupture explicite de layout param n'est faite.

## 8. Invariants a ne pas casser

- Entree audio hard-RT unique via IRQ DMA RX (`audio.c`).
- Ordre impose dans `process_half`:
  1) invalidate RX cache
  2) segmentation events
  3) traitement segment(s)
  4) clean TX cache
- `AUDIO_FRAMES_PER_HALF` doit rester coherent avec `AUDIO_BLOCK_SIZE`.
- `audio_io_unpack` reserve lane 3 comme source interne (pas de mapping TDM physique direct).
- Z1 ne doit pas faire d'I/O SD bloquante; le futur record SD doit passer par des rings RAM prealloues et un writer hors IRQ.
- Le Sampler track-aware lit via `sample_cache` en RAM. `sample_pool` reste catalogue/projet/metadata; `sample_desc->data` est une compat legacy hors autorite audio principale.
- La sortie principale Sampler reste stereo de bout en bout: pas de downmix L/R->mono avant injection mixer; les samples mono restent dupliques identiquement sur L/R.
- Chemin mono-native mixer: si la source externe est mono-native et si tous les blocs track-level actifs ont une variante mono reelle, `mixer_process()` conserve le signal en mono jusqu'au dernier moment utile; le fallback stereo reste la reference fonctionnelle.
- Les blocs track-level mono reels autorises dans ce corridor sont actuellement biquad mono, `EQ3` mono, `VCA` et `gain`; l'ancien insert track `FX_SAT` lie a `COLORS/CRUNCH` n'est plus active par la policy boot produit.
- L'ordre DSP mono aligne le chemin stereo de reference: filtre/EQ puis inserts, puis `VCA+gain`, puis projection tardive `mono -> L/R`.
- Un bloc mono ne doit jamais appeler un traitement stereo avec `L/R` identiques pour simuler du mono.
- La projection mono -> stereo ne doit intervenir qu'aux frontieres qui l'exigent reellement: taps post-fader, sends stereo, routing `MAIN/CUE` et accumulation bus.
- Stabilisation actuelle `sample_cache`: le chemin Sampler track-aware supporte le playback forward simple, le pitch simple par interpolation lineaire en forward/reverse, la loop forward pitchee simple, le ping-pong pitche simple, le reverse simple, la loop forward simple, le ping-pong simple et la selection de slices v1 par note via `sample_voice_reader`. Depuis le retrait runtime RAM, ces comportements ne sont plus contractuels pour RAM; Stream garde le flux Classic provisoire.
- La memoire audio runtime Sampler reste locale au sous-systeme Sampler: `sample_page_cache` est l'owner memoire audio runtime, `sample_cache` garde la facade produit/orchestration prepare-service-compat, et `sample_voice_reader` porte la lecture musicale. `READY_FULL` est materialise par pages contigues en SDRAM; `READY_PARTIAL` signifie STREAM enregistre + pages initiales queuees, puis chargees hors audio par le `sample_stream_manager` via `sample_cache_service()`.
- Le seuil legacy `READY_FULL` Classic est borne par le cout statique d'un long-stream Classic: `SAMPLE_CACHE_STREAM_STATIC_PAGES = SAMPLE_PAGE_MIN_READY_PAGES`, soit 16 pages de 512 frames avec la configuration actuelle, donc 8192 frames stereo float decodees. Tout sample Classic au-dessus passe en `READY_PARTIAL`/STREAM pour les consommateurs Classic encore streamables (Stream), meme si l'ancien seuil 64 pages l'aurait charge en full. Le warm set STREAM initial contient le span forward 8192 calcule par le helper commun et le span reverse 8192 calcule depuis la frame tail; en reverse, un depart non aligne peut demander 17 pages physiques pour couvrir 8192 frames utiles.
- Retrigger Classic streamable (Stream/compat): le runtime coupe d'abord la voix cache du track cible, puis ne rearme qu'apres `sample_cache` juge rejouable depuis la frame de depart. Un `READY_PARTIAL` dont la frame 0 n'est plus en fenetre passe par `NEEDS_REPREPARE -> PREFILLING -> READY_PARTIAL` hors audio, sans rester coince en `PLAYING`.
- Limitations actuelles `READY_PARTIAL` pour le chemin Classic streamable restant: WAV PCM/extensible PCM, 48 kHz, mono/stereo, 16/24/32-bit, forward simple et pitch lineaire selon le mode consommateur; reverse/slices historiques ne sont plus un contrat produit RAM.
- `sample_cache_read_voice()`, `sample_cache_read_voice_frame()`, `sample_cache_peek_frame()`, `sample_cache_begin_read_block()` et `sample_cache_commit_read_block()` sont RAM-only. FatFs reste limite a `sample_cache_prepare()` et `sample_cache_service()`.
- Phase 1/2/3/4/5A/5B/6A/6B refonte locale Sampler: les modes `Shot` forward 1x (`mode=0`), `RevShot` reverse 1x (`mode=1`), `Loop` forward 1x (`mode=2`), `PingPong` 1x (`mode=3`), le `Shot` forward pitche simple (`mode=0`, `step != 1`, sans loop), le `RevShot` reverse pitche simple (`mode=1`, `step != 1`, sans loop), la `Loop` forward pitchee simple (`mode=2`, `step != 1`, sans ping-pong) et le `PingPong` pitche simple (`mode=3`, `step != 1`) ne passent plus par `sample_cache_begin_read_block()` dans l'IRQ. `brick6_sampler_runtime` construit un `play_plan` au trigger, `sample_voice_reader` porte un cursor audio local par voix, et l'IRQ consomme des segments page-bounds deja acquis via `sample_page_cache`.
- Sur ce chemin Phase 1/2/3/4/5A/5B/6A/6B, aucun `request_page` n'est emis depuis le kernel audio. Le prefetch stream est queue hors IRQ par `sample_cache_service()` a partir des voix actives, et la transition de page du cursor se limite a un acquire/release RAM-only au boundary; en reverse, les demandes se font sur une fenetre precedente bornee (`current-1..current-4`). La loop forward 1x reste un wrap de cursor local (`loop_end -> loop_begin`), le ping-pong 1x une inversion locale de direction/kernels aux bounces, et le pitch simple forward/reverse/loop/ping-pong consomme des segments prepares avec voisin d'interpolation deja acquis.
- Les samples longs en `READY_PARTIAL` gardent une preparation reverse tail legacy dans `sample_cache`, mais RAM ne la consomment plus. Le prefetch hors IRQ utilise aussi une fenetre reverse plus large que le forward pour couvrir les transitions `page N -> N-1`; les pages stream non pinnees peuvent etre reclamees avant un chargement `READY_FULL`, mais les pages de samples full deja chargees ne doivent pas etre evincees par le stream.
- Les autres modes (`slice`) ne sont plus streamables et ne demarrent plus de reader Classic: RAM slicing attend le futur sampler RAM dedie.
- Legacy restant: `voice_manager` peut encore traiter des voix anciennes et `Src/Audio/sampler.c` reste helper legacy; le chemin produit track-aware ne doit pas revenir a `sample_desc->data`.
- Les delays MacroFX restent monophoniques par slot pour `COMB`, `WOBBLE` et `FREEZE`, statiques en `AUDIO_COLD_SDRAM`, avec lecture interpolee et historique logique `delay_filled` pour eviter de nettoyer de grands buffers en IRQ lors d'un reset de type. `FREEZE` garde son historique vivant tant que `TYPE=FREEZE`: `LVL=0` ecrit seulement l'historique et laisse passer le live dry, `LVL>0` engage le freeze de boucle et dose le retour wet par la valeur `LVL`. Le mix FREEZE morphe vers un repeater: le dry est ducke par `LVL` jusqu'a etre coupe a `LVL=127`, le retour freeze monte avec gain borne, et l'input send vers la boucle passe de normal en OFF a nul au niveau maximum. Fallback dry tant que l'historique demande n'est pas encore pret. `B=HOLD` est quantifie en 4 modes DSP distincts (`SHORT/MID/LONG/INF`) qui pilotent le feedback de boucle, avec `INF` regle en quasi-maintien borne. `ECHO` est retire de `Master/FX`; les delays globaux `CLASSIC`/`DUAL` restent portes par le pool SDRAM partage du mixer.
- `DRIVE` est une saturation master legere inspiree tanh mais volontairement plus extreme: `LVL` reste le dry/wet du slot, `A=DRIVE` pilote un pre-gain fort, un etage principal de saturation polynomial sans division et un clip final leger sans division, `B=TONE` pilote une pre-emphase et une coloration sombre/brillant par filtre simple. L'algo n'utilise pas `tanhf`, pas de table et pas d'oversampling; un mode fast retire la pre-emphase et le clip final quand `A` reste bas/moyen. Le gain staging applique un post-gain dependant de `A` et un limiteur final sans division pour garder `LVL` exploitable a fort drive sans neutraliser la saturation.
- `FREEZE` est une ressource Master/FX unique dans les 4 slots: le premier slot `TYPE=FREEZE` par ordre de slot devient owner audio, les doublons sont ignores par guard runtime. Il reutilise le `g_delay[4][48000]` existant et ne partage pas l'historique stereo global de `STUTTER`.
- `STUTTER` est une ressource Master/FX globale unique: le premier slot `TYPE=STUTTER` par ordre de slot devient owner audio, les doublons sont ignores par guard runtime. Son historique stereo circulaire unique fait 24000 frames a 48 kHz, soit environ 192 KiB en float stereo, place dans `.audio_history_sdram` via `AUDIO_HISTORY_SDRAM`. `LVL` est volontairement on/off pour STUTTER: `LVL=0` coupe la sortie audible et remplit seulement l'historique L/R, `LVL>0` rend STUTTER full wet sans mix dry/wet progressif. Au passage `LVL` OFF -> ON, la lecture latch immediatement la fenetre historique `SIZE` deja remplie, mais place la tete a la fin de cette fenetre moins le micro-fade de boucle pour entendre la portion la plus recente sans phase recording audible. En lecture, `RATE` est lu en continu et `SIZE` relatch immediatement vers la portion recente avec crossfade court.
- Integration courante `Sampler/Stream`: `Stretch Mode=Off` garde une lecture 1x entre micro-corrections locales distribuees, `Stretch Mode=Speed` garde le chemin cursor varispeed legacy, et `Stretch Mode=Shifter` garde le cursor `Speed` puis applique `brick6_clip_shifter` stereo avant accumulation.
- `brick6_clip_shifter` porte un shifter deux taps delay/crossfade local; le ratio de correction est isole dans `brick6_clip_shifter_set_pitch_correction(pitch_ratio / timing_ratio)`, `Grain` pilote la taille de fenetre, `Hop` et `Search` restent sans effet dans ce mode.
- Le runtime lourd `Sampler/Stream` n'est plus porte par `SEQ_TRACK_COUNT`: il est borne a `BRICK6_MAX_CLIP_TRACKS=4` via un pool de slots locaux. Les tracks `Stream` supplementaires sont filtrees en amont par le catalogue UI; si aucun slot runtime n'est disponible au start, `Shifter` retombe explicitement sur `Speed` sans crash.
- `REC/CLEAR/stop manuel/start transport` reset uniquement l'etat du shifter et conservent l'ownership brut du runtime Looper RAW courant.
- STOP transport passe par Z4 et appelle `brick6_sampler_runtime_stop_transport_clips()` pour couper uniquement les tracks `Sampler/Stream`, y compris les streams en Launch, et remettre reader/playhead au debut du stream; RAM et Looper restent hors de ce reset.

## 9. Dependances inter-zones

- Z2 Track Runtime Authority:
  - `brick6_audio_runtime` choisit engines/mix targets via `track_runtime`.
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
- Responsabilites concentrees: `brick6_audio_runtime_dsp` cumule orchestration engines + modulation + sampler.
- Ordre d'appel tres contraint:
  - `mixer_external_inputs_clear` appele a la fois dans runtime et mixer (redondance defensive).
  - post-mix: `fx_master_macro_process_block` reste apres `mixer_process` et avant preview SD.
  - autorite source capture: bus dedie dans `mixer_process`, avec mapping `mix_track -> logical_track` via `track_runtime_get_logical_track_for_mix_track`; le routage source par track filtre la capture.
  - aucun second backend recorder concurrent observe in-tree.
- Le legacy recorder SD/stems a ete retire: aucun hook IRQ ni writer hors IRQ historique ne reste comme reference pour le futur record SD multi-client.
- Le legacy recorder RAM `live_recorder` / `recorder_transport` est retire: aucun buffer SDRAM_RECORDER dedie ni service transport historique ne reste dans le pipeline produit.
- Cout CPU variable par bloc observe:
  - segmentation en sous-segments selon nombre d'evenements seq dans `process_half`.
  - render synth conditionnel selon nombre de tracks bindees.
- Divergence doc/commentaires potentielle:
  - plusieurs commentaires evoquent "test"/"policy" locales; l'autorite runtime effective est le code courant.

Aucune double autorite concurrente du flux IRQ->mix final n'est constatee.

## 11. Impact eventuel sur la cartographie globale

- Z1 est confirmee comme zone coeur hard-RT a frontiere nette (IRQ + conversion + DSP callback + mix).
- `audio_float.c` et `audio_io.c` sont des sous-composants structurels de Z1; sans eux la cartographie de flux est incomplete.

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
- Le DSP delay CLASSIC vit dans `fx_delay_stereo.*`; ses lignes L/R utilisent le pool partage `fx_delay_shared_pool.*` place en `.audio_delay_sdram`, borne a la capacite commune 6 s.
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
- Les buffers longs DUAL utilisent le meme pool partage `fx_delay_shared_pool.*` en `.audio_delay_sdram`; le pool est dimensionne sur `ceil(6 s * 48 kHz) + 8` samples par canal. CLASSIC et DUAL restent mutuellement exclusifs dans `mixer_process()`.

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

## 16. Addendum - modele Drum final

- L'autorite des modeles Drum runtime est reduite a `DRUM_MODEL_ID_NONE`, `DRUM_MODEL_ID_TRX_BD` et `DRUM_MODEL_ID_BD_ANALOG`.
- `DRUM_MODEL_ID_TRX_BD` reste un slot produit reserve/futur; il ne selectionne pas de moteur actif et reste silencieux.
- `drum_synth` reste la facade RT-safe Drum: `DRUM_MODEL_ID_NONE` rend zero, `DRUM_MODEL_ID_BD_ANALOG` instancie directement `plaits::AnalogBassDrum` en etat statique par instance, sans `plaits::Voice`, sans CTAG et sans allocation dynamique.
- `brick6_audio_runtime` conserve le chemin track-aware Drum vers le mixer; seul `TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG` mappe vers `DRUM_MODEL_ID_BD_ANALOG`, les autres cas restent `DRUM_MODEL_ID_NONE`.
- Sortie Drum active: mono-native vers `mixer_submit_external_mono_native`, zero tant que le modele est `NONE` ou tant qu'aucun `note_on` PLAY n'a arme le moteur.
- Cout IRQ attendu: un rendu `AnalogBassDrum::Render()` par track `BD_ANALOG` active et par bloc audio, avec SVF/one-pole/sine oscillator par sample. Le point de mesure existant reste `cpu_load` autour de l'IRQ audio; il n'existe pas encore de compteur DWT local dedie Drum.
- Les anciens moteurs/types `TB3` et `DX7` ne font pas partie du runtime audio produit. Aucune compatibilite projet/config n'est conservee pour ces labels ou IDs.

## 17. Addendum - producteurs record SD multi-client

- Z1 reste l'autorite des taps/producteurs audio hard-RT pour le futur recording SD multi-client.
- Le format de capture transmis au writer est stereo `int32_t` aligne, 48 kHz, par client record.
- Le format fichier produit est WAV PCM stereo 24-bit / 48 kHz, mais le packing 24-bit appartient au writer hors IRQ, pas au pipeline audio.
- Le producteur Looper v1 ne gere que la copie IRQ vers ring RAM du writer client 0 quand l'etat writer est `RECORDING`; pas de SAVE, pas d'Overdub. Pour `LEN` fixe, START/STOP record sont armes hors IRQ mais consommes par `brick6_looper_runtime_on_boundary_edge()` au sample exact du marker audio.
- Invariant hard-RT:
  - le callback audio peut seulement copier le bloc courant vers un ring RAM prealloue,
  - aucun FatFs,
  - aucun malloc,
  - aucun lock bloquant,
  - aucun `f_open/f_write/f_sync/f_lseek/f_rename/f_unlink/f_expand`,
  - aucun formatage/header WAV.
- Le ring plein reste un diagnostic critique uniquement: Z1 ne bloque jamais l'IRQ, mais le dimensionnement produit doit rendre l'overflow non atteignable en usage supporte.
- Dimensionnement courant du producteur Looper: ring writer 4 s utiles par client a 48 kHz stereo `int32_t`; une page SD lente, une allocation FAT ou une carte busy courte doivent etre absorbees hors IRQ par le ring et le service Z0/Z6.
- Aucun module legacy recorder SD/stems ne doit servir de reference d'implementation; la cible produit reste un writer global multi-client arbitre en Z6/Z0, compatible avec `sample_cache_service`.

## 18. Addendum - playback runtime Sampler/Looper transient

- `Sampler/Looper` est maintenant une source audio track-aware via `brick6_looper_runtime`, rendue dans `brick6_audio_runtime_dsp` puis injectee dans le mixer avec `mixer_submit_external_stereo`.
- Le mixer reste l'unique autorite de sommation; aucun second mixer ni chemin master special n'est ajoute.
- Le playback Looper lit uniquement les pages RAM pretes du `sample_page_cache`; l'IRQ audio ne fait aucun FatFs, aucun `f_read`, aucune allocation et aucun lock bloquant.
- La preparation du backing RAW Looper et le refill des pages sont faits hors IRQ par `brick6_looper_runtime_service`, apres notification de prise RAW finalisee. Le premier depart live post-REC peut toutefois etre arme avant `TAKE_READY` via START_RAM; le runtime attache ensuite le reservoir RAW via `sample_page_cache` sans parsing WAV.
- Le refill Looper utilise une plage `sample_page_cache` separee des ids `sample_pool`; `sample_cache_service` ne charge pas les pages Looper avant le writer record.
- Pour une prise RAW, `sample_page_cache` enregistre un stream PCM24 stereo interleaved 48 kHz sans header: offset disque `frame * 6`, decode signed little-endian vers float, et longueur unique `recorded_frames`.
- Si une page manque pendant le render, le runtime produit un silence local pour le reste du bloc disponible et attend le refill hors IRQ; il ne bloque jamais l'audio.
- Une page Looper manquante n'arrete pas le curseur audio: le playhead avance sur le silence local, et le refill hors IRQ prefetch une fenetre de pages en avance, avec wrap modulo vers le debut avant le retour a zero.
- `multi_record_writer_push_audio_block_from_irq` reste limite au producteur Looper existant dans `mixer_process`.
- Un hook interne `sample_capture` existe dans `mixer_process` pour le backend `SAMPLE_WAV`, mais il est desactive hors record Audio Rec. Lorsqu'il est active, il somme les tracks routees par le modele Audio Rec, y compris le playback Looper route, convertit ce bus en PCM24 stereo `int32_t`, calcule des buckets min/max signes pour l'overview waveform RAM, et pousse uniquement vers le ring RAM dedie du writer client `SAMPLE_CAPTURE_RECORD_CLIENT_ID`; aucun FatFs, malloc ni lock bloquant n'est ajoute en IRQ.
- `PLAY=Off` prepare la prise mais la garde muette; `PLAY=Auto` lance la lecture sur transport running depuis START_RAM post-REC quand disponible, puis depuis RAW/page-cache. STOP transport arrete la lecture.
- Apres une prise LEN fixe, `PLAY=Auto` ne demarre plus sur disponibilite flottante du cache: Z5 transmet une intention, puis Z1 consomme START/STOP REC sur `SEQ_RUNTIME_AUDIO_EVENT_BOUNDARY_EDGE`. Au STOP boundary, `brick6_looper_runtime` arme START_RAM a `playhead=0` sur le meme sample, Z1 segmente le half-buffer a cet offset et appelle `brick6_looper_runtime_on_scheduled_start()` avant le rendu du segment suivant. La notification `TAKE_READY` rattache ensuite le RAW/backing storage sans rattrapage par avance de playhead.
- SAVE RAW export est branche hors IRQ: Z5 refuse transport running, Z6 lit uniquement `recorded_frames` du reservoir RAW et ecrit un WAV final par chunks budgetes apres les services sample/writer/refill Looper. Aucun chemin Looper actif ne depend d'un fichier intermediaire ni d'un `f_rename` de prise.
- En `ARM=Rec`, le demarrage d'une nouvelle prise Looper est un replace: le reader playback precedent est detache, les pages transient du cache Looper sont invalidees et les metadonnees de prise sont remises a zero avant le passage writer en `RECORDING`.
- Le playback IRQ Looper conserve une reference de page courante acquise et ne rappelle plus `sample_page_cache_begin_read_block()` a chaque bloc audio; les requetes de prefetch/lookahead restent hors IRQ dans `brick6_looper_runtime_service()`. Le chemin normal est `RAW/page-cache`; une page manquante sur ce chemin rend du silence local, avance le playhead et n'emet aucune requete page depuis l'audio.
- Pour le premier playback post-REC RAW, le Looper dispose d'un tampon START_RAM/preroll statique de 0.25 s stereo `int32_t` en `SDRAM_RECORDER`, alimente par le meme bloc PCM24 interleaved pousse au writer depuis `mixer_process`. Ce tampon commence au premier segment capture apres `REC_START` boundary et s'arrete avant le premier segment apres `REC_STOP` boundary. Il permet de demarrer `PLAY=Auto` a `playhead=0` sans attendre `TAKE_READY` ni la premiere page RAW; le RAW/page-cache reste le backing complet. Apres sortie du tampon, si le backing RAW n'est pas encore attache, le playback attend sans reutiliser START_RAM; si le backing est attache mais qu'une page manque, le chemin normal produit du silence local et avance le playhead comme les autres miss page-cache.
- Le tampon `PREROLL_RAM` est une source de demarrage uniquement. Le runtime tente d'entrer dans le chemin normal `RAW/page-cache` avant d'utiliser START_RAM; START_RAM ne sert que de bridge post-REC quand la page RAW courante n'est pas encore disponible. Des que le playback Looper a lu un bloc depuis `RAW_PAGE_CACHE`, le take marque le relais RAW comme effectue et le preroll est consomme: les wraps suivants doivent repartir en `RAW_PAGE_CACHE` a la frame 0, jamais en `PREROLL_RAM`.
- L'etat hot Looper `g_looper_tracks` est place en DTCM via `AUDIO_HOT` uniquement pour les metadonnees par track lues/ecrites par l'IRQ audio; les gros buffers restent hors DTCM (`g_looper_preroll_pcm` en `SDRAM_RECORDER`, rings writer en `SDRAM_RECORDER`, page-cache/buffers audio dans leurs sections existantes). Aucun DMA ne cible `g_looper_tracks`.

## 19. Addendum - Prism Phase Reset

- `Synth/Prism` conserve le comportement historique par defaut: `PHASE RESET=Off` laisse le `sync_block` nul et `MacroOscillator::Strike()` garde son contrat Mutable courant.
- `PHASE RESET=On` arme un reset one-shot au `note_on` Prism, puis le premier sous-bloc rendu par `brick6_braids_runtime_render_instance()` pose `sync_block[0]=1` avant de consommer le flag.
- Le reset reste track-aware par instance Prism et ne reset aucun generateur random.
- Les moteurs Prism qui consomment deja `sync_block` reset leur phase au premier sample rendu; les moteurs sans entree sync pertinente restent des no-op implicites.
- Le rendu reste borne en IRQ: buffers locaux de 24 samples, pas de malloc, pas de FatFs, pas de reset brutal du moteur Mutable.

## Addendum 2026-05-13 - retrait du buffer master

- Le backend audio buffer master est retire du pipeline hard-RT.
- Z1 ne possede plus de capture/playback buffer dedie: `audio.c` ne route plus les boundaries vers ce backend, `brick6_audio_runtime_dsp()` appelle directement `mixer_process()`, et `mixer_process()` ne pousse plus de bus post-fader vers ce chemin.
- `audio_xfade` reste le seam neutre de courbe/smoothing utilise par l'ecoute `Sampler/Looper`; aucun appel audio runtime a l'ancien backend n'est conserve.
- Le chemin Looper dans `mixer_process()` garde ROUT et sortie separes: ROUT alimente seulement la capture REC, tandis que le playback Looper est rendu sur la lane Looper puis retenu hors bus live normal.
- `PARAM_LOOPER_XFADE` agit sur le bus final apres accumulation live/sends/returns et avant copie vers `tracks[0]` / `tracks[1]`: `0%` conserve le bus live MAIN, `100%` conserve seulement le bus playback Looper disponible sur MAIN, et les valeurs intermediaires font un crossfade live/playback.
- CUE n'est traite par XFade que si le playback Looper est effectivement route vers CUE; sinon le bus CUE live reste hors cout et hors attenuation XFade.
- Si aucun playback Looper ne sort et que `XFade > 0`, la cible playback MAIN est le silence: le live MAIN est attenue selon la meme courbe. Si `XFade=0`, aucun blend n'est applique.
- Le cout idle reste borne: avec `XFade=0`, les buffers bus Looper ne sont pas nettoyes, la copie playback Looper vers le bus XFADE est sautee, et `audio_xfade_smooth_next()` n'est appele que si la cible ou le smoothing courant peut encore modifier la sortie.
- Quand `XFade>0`, la lane playback Looper est accumulee directement dans les bus XFade MAIN/CUE utiles pendant le passage mixer; il n'existe pas de cache intermediaire par track Looper a nettoyer/copier avant le blend final.
- Les etats stables ont des fast paths: `100%` stable remplace/mute par `memcpy`/`memset`, et les valeurs intermediaires stables calculent les gains une seule fois par bloc.
- Les diagnostics temporaires Looper RAW ne sont plus appeles depuis l'IRQ audio ni depuis les transitions writer/UI; les compteurs CPU/perf existants restent conserves.
- Le recorder legacy dormant `live_recorder` / `recorder_transport` est retire avec son buffer SDRAM_RECORDER; le record actif reste uniquement Looper RAW via `multi_record_writer`.

## Addendum 2026-05-13 - metadata musicale Looper REC

- `brick6_looper_runtime` memorise maintenant une metadata musicale minimale par prise RAW transient: `recorded_frames`, `recorded_steps_q16`, `source_samples_per_step_q16`, `source_bpm_milli`, `record_start_sample` et `record_stop_sample`.
- `REC_START` reste consomme sur marker boundary audio; le runtime capture alors la cadence source via `seq_runtime_get_samples_per_step_q16()` et le BPM projet courant.
- `REC_STOP` reste consomme sur marker boundary audio: pour `LEN` fixe, `recorded_steps_q16` vient du mode LEN; pour `LEN=Free`, il est mesure depuis le span sample exact `REC_STOP - REC_START` et la cadence source capturee au start.
- Cette metadata ne branche pas encore le stretch/pitchshifter Looper et ne modifie pas le rendu PLAY/WRAP/RAW/page-cache/XFade/SAVE.

## Addendum 2026-05-13 - Looper STRETCH runtime

- `Sampler/Looper` consomme maintenant `PARAM_LOOPER_STRETCH`, `PARAM_LOOPER_PITCH` et `PARAM_LOOPER_GRAIN` dans `brick6_looper_runtime`, sans lookup `param_registry` depuis l'IRQ audio.
- `Off + Pitch=0` utilise le chemin bloc normal `RAW/page-cache`; START_RAM/preroll reste limite au bridge post-REC avant relais RAW. Wrap entier, avance playhead entiere et XFade/ROUT/SAVE restent inchanges.
- `Off + Pitch != 0` utilise le lecteur Looper varispeed Q16/fractionnaire sur le chemin normal `RAW/page-cache`, avec interpolation lineaire stereo; il transpose la boucle sans time-stretch.
- `Speed` utilise la metadata de prise: `timing_ratio = source_samples_per_step_q16 / current_samples_per_step_q16`, clamp `0.5..2.0`, puis increment de lecture `timing_ratio * pitch_ratio`.
- `Shifter` rend d'abord le Looper en varispeed `timing_ratio` depuis le chemin normal `RAW/page-cache` dans un scratch stereo bloc, puis applique `brick6_clip_shifter_process_stereo()` avec correction `pitch_ratio / timing_ratio` et fenetre `Grain`.
- Le pool shifter Looper est dedie et separe du pool Stream prive: 4 instances bornees en RAM_D1, avec reset explicite au start playback et au changement mode/grain. Le scratch stereo bloc Looper Shifter (`g_looper_stretch_l/r`) reste aussi en RAM_D1 pour eviter les lectures/ecritures shifter par sample en SDRAM cold.
- Si la metadata musicale est invalide (`recorded_frames`, `recorded_steps_q16`, `source_samples_per_step_q16` ou cadence courante nuls), `Speed/Shifter` retombent sur `Off`.
- Si `Shifter` est demande mais qu'aucune instance Looper dediee n'est disponible, le runtime retombe sur `Speed` si la metadata est valide, sinon `Off`.
- Quand `Pitch` revient sur un point stable `-12`, `0` ou `+12` apres une phase varispeed libre `Off` ou `Speed`, le runtime Looper peut armer un resync one-shot du playhead; il n'y a pas de correction permanente entre ces points.
- Le resync est consomme cote audio uniquement apres une courte stabilite du ratio effectif stable: `Off` utilise le ratio pitch stable (`0.5`, `1.0`, `2.0`), `Speed` multiplie ce ratio par le `timing_ratio` courant.
- La position attendue vient de la timeline audio depuis `playback_start_sample`, multipliee par le ratio stable effectif puis modulo `recorded_frames`; le jump relache la page courante, demande les pages autour du nouveau playhead et applique un mini crossfade local de 96 frames.
- `Shifter`, Stream, REC/PLAY/WRAP/SAVE/XFADE/ROUT restent hors comportement de resync dans cette passe.

## Addendum 2026-05-15 - Sampler/Multi playback sans UI

- `brick6_sampler_runtime` expose un hook interne `brick6_sampler_runtime_trigger_multi_note_velocity()` pour declencher un instrument Multi deja `READY`, sans UI ni persistence projet.
- Le playback Multi reutilise `sample_voice_reader` et le `sample_page_cache` key-based avec `domain=MULTI`; aucun second reader/cache/streamer n'est ajoute.
- La capacite produit Multi finale est 512 samples max. Un instrument `READY` garantit maintenant la ration froide 8192 frames pour tous les samples reels, ou toutes les pages d'un sample plus court. Le prefetch runtime commun entretient ensuite la fenetre active au-dela de cette ration.
- La voix Multi est forward simple, sans reverse/pingpong/stretch, avec varispeed lineaire derive de l'ecart note/root retourne par `multi_sample_pool_resolve()`.
- `PARAM_SAMPLER_MULTI_LOOP=OFF` garde le comportement one-shot existant. `ON` active `SAMPLE_PLAY_LOOP_FORWARD` dans le `sample_play_plan_t`: points `smpl` WAV valides en frames source absolues, sinon fallback `[region_begin, region_end)`. `note_off` garde le chemin release actuel; stop/steal/mute/unload/reset restent prioritaires.
- L'IRQ audio lit uniquement les pages RAM READY. Une page manquante stoppe localement la voix et incremente le diagnostic underrun; aucun FatFs n'est appele depuis le rendu.
- Le prefetch continu Multi est entretenu hors IRQ par le seam explicite `brick6_sampler_runtime_queue_stream_pages()`, appele tot dans la superloop avant `sample_cache_service()` et avant le writer SD; `sample_cache_service()` sert ensuite le `sample_stream_manager` avant les clients SD moins critiques. `brick6_sampler_runtime_service()` continue de refaire la queue Multi et de traiter les releases/diagnostics hors IRQ. Chaque voix Multi active expose sa position au noyau `sample_stream_manager_queue_active_pages()` avec une cle `domain=MULTI`, `object_id=multi_sample_id`, et demande jusqu'a `current_page + 27`.
- `sample_stream_manager_has_pending_sd_work()` tient compte des pending key-based, donc les requetes `MULTI` restent visibles au service SD/cache et ne dependent plus du scan legacy `CLASSIC`.
- Le modele de voix Multi est borne a `SAMPLER_MULTI_MAX_VOICES_PER_TRACK = 4` par track. Une cinquieme note Multi sur la meme track vole la plus ancienne voix Multi de cette track.
- Le stockage hot des voix Multi est un pool global DTCM de `SAMPLER_MULTI_MAX_GLOBAL_VOICES = 16` entrees, chaque entree portant son `owner_track_id`; le rendu parcourt le pool global et route chaque voix vers sa track proprietaire.
- La limite globale volable du Sampler/Multi est `SAMPLER_MULTI_MAX_GLOBAL_VOICES = 16`: si elle est atteinte, Multi vole d'abord la plus ancienne voix Multi globale, puis la plus ancienne voix RAM volable. Stream et Looper restent proteges.
- L'etat produit minimal par track Multi est porte par `brick6_sampler_runtime`: `multi_instrument_id`, `gain` et `loop_enabled`. `multi_sample_pool` reste l'autorite globale des instruments/samples/zones et porte seulement les metadonnees sample/zone, dont `has_loop/loop_begin/loop_end`.
- En absence d'UI/persistence Multi, le controle passe par les APIs runtime `set/get_multi_instrument`, `set/get_multi_gain`, `multi_instrument_is_ready` et le trigger track-aware qui utilise l'instrument assigne quand l'appelant passe `MULTI_SAMPLE_POOL_INVALID_ID`.
- `Sampler/Multi` consomme maintenant le note-off clavier via `brick6_sampler_runtime_note_off_multi_track_note(track,note)`: les voix Multi actives du meme couple track/note passent en release pending et continuent a fournir du signal RAM/page-cache jusqu'a extinction du VCA mixer existant, puis sont stoppees/reset avec diagnostic `REL_DONE`. Aucun FatFs, malloc ni UART n'est ajoute en IRQ; les logs UART Multi sont desactives par defaut.
- Le prefetch continu Multi reste hors IRQ et devient monotone par fenetre: chaque voix memorise la derniere page de lookahead demandee via le noyau STREAM commun, demande seulement les nouvelles pages jusqu'a `current_page + 27`, et reset cet etat au trigger/steal/stop/release done. Pour une voix loopee, une seconde fenetre active `owner_kind=MULTI_LOOP` maintient les pages autour de `loop_begin`; au wrap reel, la fenetre courante est reset pour autoriser une prochaine page source plus basse sans supposer une progression monotone.
- La page d'ancrage Multi `page0` n'est plus candidate a l'eviction du `sample_page_cache`: le contrat `READY` reste donc stable pendant les prefetchs actifs page1+ et les refus `PAGE0` loguent hors IRQ une ligne ciblee par sample (`inst/zone/smp/obj/root/vel/state0/path`).
- Le STOP transport/panic coupe toutes les voix Sampler runtime, pas seulement les Streams, afin de liberer les locks de fenetre STREAM et d'arreter les prefetchs SD devenus inutiles apres arret du sequenceur.

## Addendum 2026-05-15 - noyau STREAM actif commun

- `sample_stream_manager_queue_active_pages()` est le noyau commun minimal d'entretien des voix streamées actives, basé sur `sample_audio_key_t {domain, object_id}`, `current_frame`, `end_frame`, direction et lookahead.
- `sample_cache_queue_active_stream_pages()` conserve son role legacy Classic/Stream, mais delegue maintenant le calcul page courante/lookahead/priorite au noyau commun sans dependance nouvelle a `sample_cache_voice_t`; RAM ne creent plus de voix Classic STREAM.
- `Sampler/Multi` expose ses voix actives au même noyau via `domain=MULTI`, `object_id=multi_sample_id`, `current_frame=reader.position`, `end_frame=region_end`, direction forward et `SAMPLE_PAGE_MULTI_LOOKAHEAD_PAGES`; la policy locale page2/urgent séparée est retirée.
- L'anti-spam monotone par voix Multi vit dans `sample_stream_active_state_t` et reste hors IRQ. READY Multi n'est plus limite a page0: le LOAD prepare la ration froide 8192 frames avant de declarer l'instrument pret.
- L'IRQ audio continue de lire uniquement RAM/page-cache via `sample_voice_reader`; aucune SD/FatFs/malloc/UART n'est ajoutée au rendu.
- Quand une voix Multi s'arrête, la fermeture du reader STREAM et le nettoyage des pending associés sont différés vers `brick6_sampler_runtime_service()` hors IRQ; le rendu ne ferme jamais de `FIL` et ne touche pas FatFs.

## Addendum 2026-05-15 - Sampler/Multi velocity single-layer

- `multi_sample_pool_resolve()` reste l'autorite de selection note/velocity du Sampler/Multi et retourne maintenant le nombre borne de layers velocity pour le couple note/root retenu.
- Si une note/root ne possede qu'un seul layer velocity, ce layer est resolu meme si sa plage metadata ne contient pas la velocity du NOTE ON; le sample couvre donc implicitement `1..127`.
- Le gain de voix Multi single-layer est applique au demarrage par `brick6_sampler_runtime_velocity_gain(velocity)`, soit `velocity / 127.0f`, dans `brick6_sampler_runtime` uniquement.
- Les instruments multi-layer gardent la selection de zone par velocity existante et ne recoivent pas de gain velocity additionnel.
- `velocity=0` sur le trigger Multi track-aware est traite comme un note-off local, sans demarrer de voix a gain nul.
- Aucun changement de format `.brickmulti`, de parsing filename, de streaming/cache, de persistence ou de chemin SD n'est introduit.

## Addendum 2026-05-18 - Sampler fenetre de depart protegee

- Un note-on Classic streamable restant (Stream) ou Multi doit reserver une fenetre minimale de pages protegee avant acceptation.
- La protection de fenetre voix est distincte du `pin_count` de socle slot: `sample_page_cache` porte `window_pin_count` et des locks owner/generation separes.
- Une page READY deja chaude comptee dans la fenetre est aussi protegee jusqu'au release de son owner; elle ne reste plus une garantie implicite de cache global.
- `sample_stream_manager_release_owner()` libere uniquement les locks de fenetre et les pending de l'owner/generation; il ne libere pas les pins de socle slot.
- L'eviction du page-cache doit refuser toute page avec `window_pin_count != 0`.
- Cette passe ne cree pas encore de `VOICE_WINDOW_POOL` complet, ne change pas la taille de page et ne remplace pas le scheduler par deadline.

## Addendum 2026-05-18 - Sampler fenetre active protegee

- Les voix Classic streamables restantes (Stream) et Multi actives entretiennent maintenant leur fenetre courante via `sample_stream_manager_queue_active_pages()`: chaque page de la fenetre est verrouillee par owner/generation avant d'etre demandee ou consideree comme garantie.
- Les locks de fenetre sont idempotents par page/owner/generation et les pages sorties de la fenetre courante sont liberees explicitement par owner, sans toucher aux `pin_count` de socle slot.
- Le chemin Classic ne demande plus de page lookahead depuis le cursor/read path; les demandes de streaming actif passent par le service STREAM hors IRQ.
- Les pages READY deja presentes dans la fenetre active sont verrouillees elles aussi; le cache chaud peut aider la latence mais ne constitue plus une garantie implicite non protegee.
- Cette passe garde le cache opportuniste pour les pages hors fenetre et ne remplace pas encore l'arbitrage urgent/normal/prefetch par un scheduler a deadline.

## Addendum 2026-05-18 - Sampler scheduler deadline

- Les pending STREAM issus de voix actives portent maintenant une deadline audio en frames, calculee depuis la distance entre la position courante de la voix et la page demandee.
- `sample_stream_manager_pick_next()` choisit d'abord la plus petite deadline, puis applique un tie-break stable par owner/voice et anciennete.
- Les priorites historiques urgent/normal/prefetch restent seulement comme metadata secondaire et compteurs de service pendant la migration; elles ne sont plus l'autorite principale pour les voix actives.
- Les requetes legacy sans owner conservent une deadline infinie et restent servies apres le travail a deadline reelle.
- Aucun changement de driver SD, taille de page, FatFs ou budget de service n'est introduit dans cette passe.

## Addendum 2026-05-18 - Sampler SD streaming critique

- Les locks de fenetre voix Sampler activent une policy `streaming_critical` dans `sd_access_gate`.
- Tant que cette policy est active, seul `SD_ACCESS_CLIENT_SAMPLE_STREAM` peut demarrer une nouvelle possession SD; les clients non essentiels sont differes hors IRQ.
- La policy est mise a jour lors des reservations/releases owner et par `sample_cache_service()` apres publication des fenetres actives.
- Cette passe ne preempte pas un client SD deja proprietaire du gate et ne modifie pas le driver SD/FatFs.

## Addendum 2026-05-18 - Sampler nettoyage legacy STREAM

- Le streamer ne sert plus les pages `QUEUED` Classic trouvees par fallback global sans pending explicite; toute lecture STREAM servie par `sample_stream_manager` doit avoir une demande en queue.
- Les wrappers publics urgent/normal et la classification par position de lecteur FatFs sont retires: les voix actives utilisent deadline audio, les demandes legacy explicites gardent une deadline infinie.
- Le cursor Classic ne conserve plus de slot lookahead opportuniste; les transitions de page restent RAM-only via acquire direct de la page READY courante.
- Les anciennes pages d'entree RAM slicing via `sample_stream_manager_request_page()` sont retirees du runtime STREAM: RAM sliced mode-only ne cree plus de pending STREAM.

## Addendum 2026-05-19 - Sampler pages 512 frames

- Configuration actuelle: `SAMPLE_PAGE_FRAMES = 2048`, `SAMPLE_PAGE_BYTES = 16384`, `SAMPLE_PAGE_MAX_COUNT = 1280`; le pool audio decode total fait 20 MiB, dont 16 MiB de slot-pool produit.
- Les fenetres temporelles suivent la ration produit actuelle: Classic forward = span 8192 frames, Classic reverse = span 8192 frames depuis la position reverse reelle (16 ou 17 petites pages selon alignement), Multi = 28 petites pages total (`current + 27`).
- `SAMPLE_STREAM_SERVICE_MAX_PAGES` passe a 16 pour ne plus plafonner artificiellement le nombre de petites pages servies sous le budget existant; les caps FatFs ops (16), byte budget appelant et max 2 ms restent actifs.
- Le pool de locks de fenetre suit la plus grande fenetre active (`SAMPLE_PAGE_CACHE_MAX_VOICES * SAMPLE_PAGE_MULTI_WINDOW_PAGES * 2`) pour couvrir 16 voix Multi x 28 pages courantes plus 16 fenetres loop-begin optionnelles.
- Le grand index hash page-cache (`g_sample_page_index`, 8192 entrees / 96 KiB) est place en SDRAM storage-state: les lecteurs audio conservent deja une reference de slot/page courante et ne consultent l'index qu'a l'acquisition initiale, aux transitions de page ou aux lookups de service hors IRQ.
- Les ecritures de `sample_audio_key_t` dans cet index restent champ-par-champ: `sample_page_index_entry_t` place `key` a l'offset 2, donc une affectation de struct peut generer un store 32-bit non aligne et trapper en Debug si `UNALIGN_TRP` est actif.
- Les structs SDRAM du streamer (`sample_stream_pending_t`, `sample_stream_reader_t`) gardent `sample_audio_key_t` et les champs `uint32_t` sur offsets multiples de 4; les tableaux readers/pending restent en SDRAM sans acces 32-bit non aligne.
- Les sections SDRAM `NOLOAD` ne sont pas zero-initialisees par le startup (`_sbss.._ebss` couvre la BSS interne uniquement). `sample_stream_manager_init()` initialise donc explicitement `g_sample_stream_readers`, `g_sample_stream_reader_paths` et `g_sample_stream_pending` avant tout reset/clear.
- Un `FIL` de reader STREAM n'est ferme que si l'etat ouvert a ete pose par `sample_stream_manager_open_reader()` (`file_open` + cookie interne); apres close ou invalidation, le handle `FIL` est remis a zero pour eviter un `obj.fs` stale vers FatFs.

## Addendum 2026-05-19 - nettoyage mesures STREAM

- L'instrumentation comparative temporaire du chemin Sampler STREAM et les modes experimentaux de livraison groupee sont retires du build produit.
- Les mesures terrain conservent seulement la conclusion d'architecture: les mini-pages servies separement defavorisent le streaming SD; les pistes restantes sont les livraisons logiques plus grosses ou les pages physiques plus grosses, a trancher dans une passe dediee.
- Les correctifs permanents conserves sont l'initialisation explicite des objets STREAM en SDRAM `NOLOAD`, le guard `file_open` + cookie avant `f_close()`, la remise a zero des `FIL`, le placement SDRAM des readers/pending/index page-cache, et la discipline d'alignement des structures STREAM/page-index.
- Aucune commande GDB temporaire, compteur de profiling, option compile-time experimentale ni chemin de test de livraison n'appartient a l'architecture runtime active.

## Addendum 2026-05-19 - contrats Sampler communs non branches

- `Inc/Sampler/sample_play_plan.h` porte les contrats cibles `sample_resolved_source_t` et `sample_play_plan_t` pour converger vers `Classic/Multi resolve -> resolved_source -> play_plan -> reader/window`.
- Cette passe ne branche ni Classic ni Multi sur une nouvelle resolution: le runtime existant continue d'utiliser les champs historiques de `sample_play_plan_t` via `sample_voice_reader`.
- Les champs contractuels ajoutes restent preparatoires: ils n'imposent pas encore de nouvelle start gate, ration, fenetre, loop/reverse Multi, cache policy ou driver bas niveau.

## Addendum 2026-05-19 - adaptateur Classic resolved_source non branche

- `sample_cache_resolve_classic_source()` construit un `sample_resolved_source_t` depuis le descripteur Classic existant `sample_cache_desc_t`, sans acces SD, allocation, prefetch, start gate ni changement de reader.
- L'adaptateur expose seulement le contrat source: key Classic, path, format WAV, frames totales et region complete. Les informations musicales portees par le runtime Classic actif restent neutres tant que la passe play-plan commune n'est pas branchee.

## Addendum 2026-05-19 - adaptateur Multi resolved_source non branche

- `multi_sample_pool_resolve_source()` construit un `sample_resolved_source_t` depuis la resolution note/velocite/zone Multi existante, sans changer le trigger Multi, le reader, le lookahead ni le cache.
- Le pool Multi conserve maintenant les metadonnees format issues de l'index (`data_offset`, `data_size`, sample-rate, channels, bits-per-sample, block-align) afin que le contrat source puisse decrire le sample sans acces SD.
- Loop/reverse Multi restent neutres dans ce contrat preparatoire: aucune feature musicale nouvelle n'est branchee dans cette passe.

## Addendum 2026-05-19 - builder play-plan commun non branche

- `sample_play_plan_build_from_source()` convertit un `sample_resolved_source_t` en `sample_play_plan_t` avec validation pure de source, region, boucle et rate; il ne fait aucun acces SD et n'est pas branche aux chemins Classic/Multi.
- Le `sample_play_plan_t` porte maintenant les metadonnees contractuelles preparatoires `min_ready_frames`, `target_window_frames`, owner/generation, start-gate flags et diagnostic minimal. Ces champs restent neutres tant que le start gate et la fenetre commune ne sont pas migres.
- Le builder derive seulement kernel/direction/loop/rate de maniere deterministe; il ne change pas les implementations runtime existantes de reverse, loop, ration, lookahead ou cache.

## Addendum 2026-05-19 - validation start-gate/ration non branchee

- `sample_play_plan_check_ready_requirements()` verifie un `sample_play_plan_t` contre le page-cache RAM et classe la ration minimale et la fenetre cible en `COMPLETE`, `PARTIAL`, `PENDING`, `MISSING` ou `INVALID`.
- Seul `SAMPLE_PAGE_READY` compte comme audio disponible; `SAMPLE_PAGE_QUEUED` et `SAMPLE_PAGE_LOADING` sont reportes comme pending mais ne valident pas la ration minimale.
- Le helper reste preparatoire: il ne refuse aucun trigger, ne modifie pas Classic/Multi, ne queue aucune page, ne touche pas le reader et ne change pas la policy de cache opportuniste.

## Addendum 2026-05-19 - conversion ration/fenetre frames vers pages non branchee

- `sample_play_plan_frames_to_page_span()` et `sample_play_plan_required_pages_for_frames()` expriment la conversion commune du contrat produit en frames vers un span de pages interne base sur `SAMPLE_PAGE_FRAMES`.
- La conversion est directionnelle, bornee par `region_begin/region_end`, couvre les samples courts et retourne un span invalide si la demande en frames est nulle.
- Ces helpers ne lisent pas la SD, ne modifient pas le cache, ne changent aucune constante runtime et ne tranchent pas le futur modele B/C.

## Addendum 2026-05-19 - cible ration minimale 8192 frames

- La cible produit pour `min_ready_frames` est maintenant 8192 frames, soit environ 170,7 ms a 48 kHz.
- `target_window_frames` reste a definir dans une passe ulterieure si une fenetre de confort distincte de la ration minimale est retenue.
- Les helpers frames->pages restent generiques et peuvent convertir n'importe quelle valeur; les anciennes valeurs comme 6144 frames ne sont plus une cible produit et ne doivent servir que d'exemples historiques/diagnostic si elles apparaissent dans de vieux documents.
- Cette clarification ne branche pas Classic/Multi, ne modifie pas le start gate, ne change pas `SAMPLE_PAGE_FRAMES` et ne touche pas au cache/streamer.

## Addendum 2026-05-19 - plan commun autorite playback Sampler

- Les triggers Multi utilisent maintenant le `sample_play_plan_t` commun comme autorite de bind reader/playback; RAM sont neutralises cote runtime jusqu'au futur sampler RAM dedie.
- L'echafaudage de migration shadow/compare/fallback est retire: plus de flag CMake shadow, plus de compteurs GDB-only shadow, plus de comparaison stricte runtime/legacy, plus de fallback legacy de playback.
- Les diagnostics conserves sont les echecs de construction du plan commun via `common_plan_classic_build_fail`, `common_plan_multi_build_fail` et `common_plan_last_reason`, ainsi que les diagnostics produit existants de reject, underrun, miss et stop.
- Les anciens champs/structures legacy encore presents restent utilises pour calculer l'etat musical, construire la source resolue commune ou maintenir les chemins non concernes; ils ne sont plus un fallback de playback RAM/Multi.
- Cette passe ne modifie ni start gate strict READY, ni cache opportuniste, ni streamer/fenetre, ni loop/reverse Multi, ni parametres UI.

## Addendum 2026-05-19 - preparation froide 8192 frames Sampler

- La ration minimale produit reste `SAMPLE_PREP_MIN_READY_FRAMES = 8192` frames. Avec l'implementation actuelle `SAMPLE_PAGE_FRAMES = 512`, cela donne 16 pages, mais la taille de page reste un detail interne.
- `SAMPLE_PAGE_MIN_READY_PAGES` est seulement la conversion de la ration logique vers les pages internes actuelles.
- Classic STREAM prepare encore la base forward correspondant a 8192 frames depuis le debut du sample, ou tout le sample s'il est plus court, pour les consommateurs Classic streamables restants; RAM refusent `READY_PARTIAL`.
- Multi LOAD ne se limite plus a page0: chaque sample du preset demande la ration logique 8192 frames convertie en pages internes, ou toutes ses pages si le sample est plus court, avant de passer l'instrument en `READY`.
- Cette passe ne branche pas encore le start gate strict: `sample_play_plan_check_ready_requirements()` reste non autoritaire, et `QUEUED/LOADING` ne doivent toujours pas etre comptes comme audio disponible dans le futur gate.
- Reverse Classic STREAM reste une dette legacy de `sample_cache`; il n'est plus consomme par RAM.
- Les demandes d'entree de slice RAM slicing sont retirees du runtime STREAM; le futur traitement par slice appartient au sampler RAM dedie.
- Le cache opportuniste n'est pas encore supprime dans cette passe.

## Addendum 2026-05-19 - profils de preparation Sampler

- Le moteur playback reste commun (`sample_play_plan_t`, reader, page-cache, streamer), mais la policy de preparation est explicite par profil.
- `SAMPLE_PREP_PROFILE_CLASSIC` reste une dette de nommage/preparation Classic; il couvre les consommateurs Classic streamables restants et ne doit plus etre interprete comme un contrat stream RAM.
- `SAMPLE_PREP_PROFILE_MULTI` couvre l'instrument Multi: preparation predictable depuis frame 0, sans start/end/reverse utilisateur, avec ration 8192 frames ou sample court complet.
- Option B est le modele privilegie: ration logique 8192 frames, implementee par plusieurs pages internes et potentiellement lisible/servie de facon groupee si les pages sont contigues.
- Option C reste testable plus tard: une page physique/logique de 8192 frames ne doit pas changer le contrat produit, seulement la conversion interne.
- Le budget Multi explicite est `SAMPLE_PREP_MULTI_BUDGET_BYTES = 8 MiB`, converti en `SAMPLE_PREP_MULTI_BUDGET_PAGES` selon `SAMPLE_PAGE_BYTES`; avec les pages actuelles de 4096 B, cela donne 2048 pages.
- Le LOAD Multi additionne `ceil(min(total_frames, 8192) / SAMPLE_PAGE_FRAMES)` pour tous les samples du preset. Si le total depasse le budget, le preset est refuse avec `MULTI_SAMPLE_LOAD_PREP_BUDGET_EXCEEDED`; il n'y a pas de fallback silencieux a page0.
- Les diagnostics de load exposent les pages requises, le budget pages et le nombre de samples preparables.

## Addendum 2026-05-19 - start-gate strict READY 8192

- Le start gate strict reste branche sur Multi apres construction du `sample_play_plan_t` commun et avant demarrage/bind de voix; RAM refusent avant toute construction de reader Classic.
- `sample_play_plan_check_ready_requirements()` est autoritaire pour la ration minimale: seul `SAMPLE_PAGE_READY` valide le depart; `QUEUED`, `LOADING`, missing ou plan invalide refusent proprement le trigger.
- Les refus incrementent les diagnostics runtime `start_gate_reject_*`, avec dernier statut, premiere page missing, premiere page pending et compteurs par raison invalid/missing/pending/partial.
- L'echafaudage shadow start-gate est retire apres validation terrain du gate reel.
- Cette passe ne modifie ni reader, ni cache opportuniste, ni streamer/fenetre, ni budget Multi, ni loop/reverse Multi.

## Addendum 2026-05-19 - eviction cache opportuniste Sampler

- Une page READY est contractuelle si elle est en cours d'usage (`use_count`), protegee par socle/ration (`pin_count`), protegee par une fenetre active (`window_pin_count`) ou appartient a un FULL explicite (`fully_loaded`).
- Les pages READY hors contrat ne sont plus protegees par un cas special historique: elles restent seulement candidates LRU immediates pour la prochaine allocation compatible.
- La ration Multi preparee au LOAD est maintenant pinnee page par page; page0 Multi n'a plus de protection speciale hors contrat distincte.
- Le reclaim avant FULL load respecte aussi les locks de fenetre active et les pins contractuels.
- Cette passe ne purge pas proactivement les pages hors contrat, ne modifie pas le reader, ne change pas le streamer FatFs et ne touche pas au start gate strict READY 8192.

## Addendum 2026-05-21 - cleanup lifecycle window-locks Multi

- Les locks de fenetre Multi restent indexes par `(owner_kind=MULTI_VOICE, owner_id=voice_index, owner_generation=trigger_order)`.
- Le rendu IRQ peut terminer une voix pendant que la superloop entretient la fenetre STREAM; le cleanup final des owners inactifs est donc repasse hors IRQ dans `brick6_sampler_runtime_queue_stream_pages()` et `brick6_sampler_runtime_service()`, avant/apres l'entretien des fenetres.
- Ce cleanup libere les window locks/pending de la generation de voix inactive, reset l'etat stream local et differe la liberation du reader/key via le chemin Multi existant; il ne clear pas globalement le page-cache et ne touche pas aux pins de ration Multi chargee.
- Un unload/remplacement d'instrument Multi stoppe d'abord les voix de l'instrument, puis libere les readers/pending et clear les pages de chaque `sample_audio_key_multi(sample_id)`, ce qui retire les pins contractuels de la ration chargee.
- Le rendu IRQ Multi ne parcourt pas la table des locks: lorsqu'une voix finit en IRQ, il conserve seulement en RAM le triplet owner `voice_index/generation` a liberer. La superloop libere ensuite les locks avec cette generation capturee, meme si la voix a ete reutilisee entre-temps avec un nouveau `trigger_order`.
- Les descripteurs physiques du page-cache `g_sample_page_desc` restent en SDRAM dans une section dediee `.page_desc_sdram`; les pages audio dynamiques `g_sample_page_data` sont seules dans `.sdram_sample_page_pool`; l'index hash `g_sample_page_index`, les readers/pending STREAM et les scratch SD restent dans leurs sections SDRAM dediees hors pool audio dynamique.

## Addendum 2026-05-21 - page-cache allocator type par ranges

- Le page-cache conserve une seule table de donnees `g_sample_page_data` et une seule table de descripteurs `g_sample_page_desc`, indexees 1:1.
- Les nouveaux slots peuvent maintenant etre demandes avec un type d'allocation: `SLOT_PERMANENT`, `VOICE_WINDOW`, `MARGIN` ou `LEGACY_DEFAULT`.
- `SLOT_PERMANENT` scanne uniquement le range slot produit, `VOICE_WINDOW` uniquement le range de fenetres voix, `MARGIN` uniquement le range marge/cache/transitions; `LEGACY_DEFAULT` conserve le scan historique global pour les chemins non migres.
- Les presocles Multi passent par `SLOT_PERMANENT`; les reservations de fenetres voix actives passent par `VOICE_WINDOW`; les pages Classic STREAM cold base restantes et les prefetchs Looper RAW passent par `MARGIN`; Classic FULL passe par `SLOT_PERMANENT`. Les anciennes requetes opportunistes RAM slicing sont retirees du runtime actif. Les wrappers historiques restent en `LEGACY_DEFAULT` seulement comme compat API, sans appel in-tree non migre observe.

## Addendum 2026-05-21 - retrait runtime Classic RAM

- `Sampler/RAM` et `Sampler/RAM sliced mode` ne demarrent plus via le runtime Classic, meme si le sample est complet en RAM.
- Les params RAM existants restent stockes/exposes pour le futur sampler RAM dedie, mais `Start`, `End`, `Mode`/reverse et `Slice Count` ne pilotent plus un reader stream.
- Le prefetch opportuniste des entrees de slices est retire: RAM slicing ne queue plus de pages via `sample_stream_manager_request_page_key_alloc`.

## Addendum 2026-05-24 - playback RAM minimal

- `Sampler/RAM` resout maintenant `PARAM_SAMPLER_SAMPLE` comme slot global produit et demarre uniquement si ce slot est `kind=RAM`, `READY`, avec un `backend_index` valide dans `sampler_ram_pool`.
- La voix RAM est integree dans `g_sampler_voice[track]`: une voix max par track, donc 14 voix logiques actuelles sous le plafond produit de 16 voix RAM globales. Un nouveau trigger sur la meme track remplace l'ancienne voix avec declick court.
- Le rendu IRQ RAM lit seulement le pointeur resident `FLOAT32_INTERLEAVED` stereo du slot RAM valide. La conversion PCM 16/24-bit vers float et la duplication mono vers stereo sont faites au load WAV hors IRQ. Il ne touche ni FatFs, ni malloc, ni decode, ni `sample_cache`, ni `sample_voice_reader`, ni `sample_stream_manager`.
- Les voix RAM stockent la generation du slot RAM; clear/replace/reset invalident cette generation avant liberation physique. Au rendu suivant, une generation invalide stoppe la voix avec declick au lieu de lire un pointeur stale.
- `Start`/`End` sont des pourcentages normalises `[0..1]` convertis au trigger en frames bornees. Si la region resolue est vide (`Start >= End` apres conversion), RAM retombe sur le sample complet. `Mode=RevShot` (`1`) lance la region en reverse; `Loop`/`PingPong` sont rendus par le moteur RAM resident.
- Note-off RAM pilote le gate VCA mixer: relacher une note declenche la release VCA sans repasser par Classic Stream. Stop/mute/reset coupent la voix RAM avec declick. P-lock start/end/reverse sont captures au trigger; persistence/autoload RAM est portee hors IRQ par Z6 via chemins WAV et slots globaux.
- Quand la release VCA RAM RAM est terminee avant la fin de la region, le runtime clear la voix RAM au bloc suivant pour eviter de rendre une loop/pingpong inaudible. Si le sample finit avant la release, la voix s'arrete par la fin de region normale.
- `Sampler/Stream` conserve le chemin Classic `sample_cache`/`sample_voice_reader`; `Sampler/Multi` conserve `domain=MULTI`; `Sampler/Looper` RAW conserve `domain=LOOPER`.

## Addendum 2026-05-24 - modes RAM

- `Sampler/RAM` resout `PARAM_SAMPLER_SAMPLE` comme slot global produit et demarre uniquement si ce slot est `kind=RAM`, `READY`, avec un `backend_index` valide dans `sampler_ram_pool`; `Slice Count != Off` active le slicing grille du meme moteur RAM.
- `Start`/`End` sont des pourcentages normalises `[0..1]` et definissent la region RAM de base a slicer. Si la region est vide, RAM sliced mode retombe sur le sample complet.
- `PARAM_SAMPLER_SLICE_COUNT` reste borne aux valeurs UI `Off, 2, 4, 8, 16, 32, 64`. `Off` est traite comme une seule region sur la fenetre `Start..End`; sinon les slices sont regulieres dans cette meme fenetre.
- La table runtime `slice_begin/end[64]` reste reconstruite hors IRQ au changement de sample/slice count/start/end et par le service runtime non IRQ si le slot RAM change de generation. Le trigger RAM sliced mode ne depend pas de cette table pour les p-locks: il calcule directement la slice du trig a partir des valeurs effectives `Start`/`End`/`Slice Count`, avec quelques divisions bornees et sans scan.
- Mapping note v1: `note 60 -> slice 0`, puis les notes suivantes selectionnent `slice 1`, `slice 2`, etc.; les notes sous 60 ou au-dessus du nombre de slices sont clampes aux bornes. `Slice Count=Off` garde le chemin RAM chromatique, `Slice Count!=Off` desactive l'offset chromatique par note et garde seulement sample-rate + `TUNE` pour le pitch.
- `brick6_sampler_runtime_get_ram_playhead()` expose un snapshot UI leger de la voix RAM active du track demande: slot global, slot RAM, generation, frame courant, direction et ordre de trigger. Il lit l'etat runtime sans allocation, sans acces SD, sans invalidation UI et sans blocage audio; l'UI l'utilise seulement a sa cadence de rendu.
- Le rendu RAM sliced mode reutilise le renderer RAM resident: lecture `FLOAT32_INTERLEAVED` stereo, declick sur stop/steal/generation mismatch. Aucun `sample_cache_start_voice_at`, `sample_voice_reader_bind_play_plan`, pending stream ou page-cache Classic n'est utilise.
- `MODE=Shot` lit RAM ou slice en forward et stoppe a la fin de region. `MODE=RevShot` lit RAM ou slice en reverse depuis `end-1` et stoppe au debut de region.
- `Start`, `End`, `Mode` et `Loop` sont projetes en live sur la voix RAM active du track via une reconciliation centrale des bornes: `Start/End` recalculent seulement les bornes de region, `Loop` recalcule seulement le point de retour, et les trois valeurs restent independantes.
- En `MODE=Shot`/`RevShot`, une tete sortie de region peut encore finir/stopper proprement. En `MODE=Loop`, si la tete sort de `[Start, End)`, elle est repliee vers `Start` ou vers le `Loop` effectif; en `MODE=PingPong`, elle est clampée dans la region et la direction est corrigee au bord touche.
- `MODE=Loop` en mode non-slice lit d'abord depuis `Start`, puis wrappe sur le marqueur `Loop` au prochain passage de `End`; si `Loop` est hors plage fonctionnelle `Start..End`, le runtime utilise `Start` comme point de retour effectif sans muter la valeur parametre. En mode slice (`brick6_sampler_runtime_ram_slice_mode_active(track) != 0`), le `Loop` global est ignore: `MODE=Loop` wrappe au debut de la slice selectionnee et la valeur `Loop` ne modifie ni le debut de boucle ni les bornes des slices. `MODE=PingPong` continue d'utiliser la region ou slice courante; le marqueur `Loop` n'est pas un clamp de `Start/End`.
- Une voix `Loop`/`PingPong` n'est stoppee par un changement live de bornes que si la plage runtime devient invalide ou trop courte.
- Note-off RAM sliced mode suit le meme contrat VCA que RAM: release musicale via le gate mixer, sans stop Classic. Les params globaux `Start`/`End`/`Mode`/`Tune`/`Gain` sont captures au trigger; `Slice Count` reste non p-lockable et aucun etat par-slice utilisateur n'est introduit.

Clarification START/END/LOOP live:
- La reconciliation live construit des bornes effectives runtime-only; elle ne mute jamais les valeurs `Start`, `End` ou `Loop` stockees/affichees.
- Hors `MODE=Loop`, `Start` est prioritaire et `End <= Start` devient seulement `effective_end = effective_start + 1` quand le sample le permet; une tete sortie de region est repliee dans cette region effective au lieu de stopper.
- En `MODE=Loop`, `Loop` est prioritaire apres `Start`: `Loop < effective_start` donne `effective_loop = effective_start`, et `End <= effective_loop` donne `effective_end = effective_loop + 1` quand le sample le permet.
- En `MODE=PingPong`, la region effective suit la regle hors loop; une tete apres `effective_end` est clampee sur `effective_end - 1` avec direction reverse, une tete avant `effective_start` est clampee sur `effective_start` avec direction forward.
- En mode slice, le `Loop` global reste ignore; le debut de boucle effectif est le debut de la slice active.

## Addendum 2026-05-24 - pitch RAM

- Les voix RAM capturent maintenant un pas de lecture Q16 au trigger. La formule est `step = sample_rate / 48000 * 2^((note - 60 + Tune) / 12)`, bornee par le clamp runtime existant de ratio Q16.
- `PARAM_SAMPLER_TUNE` reste en demi-tons et les p-locks Tune passent par le meme chemin de param effectif que les autres params Sampler avant le trigger.
- Le rendu IRQ RAM garde uniquement le pointeur resident `FLOAT32_INTERLEAVED` stereo, avance le playhead fractionnaire Q16 selon ce pas et applique une interpolation lineaire float RAM-only. Le cas forward pitché non-PingPong est rendu par segments jusqu'aux bornes pour sortir les checks wrap/terminal de la boucle sample. Aucun chemin Classic Stream, page-cache, FatFs, malloc ou decode n'est appele par RAM.
- Quand `step_q16 == 0x00010000`, que la phase Q16 est entiere et que le mode n'est pas PingPong, le rendu RAM reprend un fast path entier sans interpolation pour Shot, RevShot et Loop.
- Les bornes `Start/End` et `slice_begin/end` restent capturees au trigger. Shot/RevShot stoppent a la limite de region, Loop wrappe dans la region, PingPong reflete le playhead dans la region meme avec un pas superieur a une frame.

## Addendum 2026-05-25 - LFO MIX direct simple

- Les targets mixer `gain`, `pan` et `send_level[0..1]` restent les seules valeurs ecrites par le chemin LFO direct pour `PARAM_MIX_LEVEL`, `PARAM_MIX_PAN`, `PARAM_MIX_SEND1` et `PARAM_MIX_SEND2`.
- Le mixer conserve le smoothing existant: le LFO ecrit les targets, puis `mixer_process()` rampe `gain_current`, `pan_current` et `send_level_current` vers ces targets sur le bloc courant.

## Addendum 2026-05-25 - LFO FILTER/VCA direct bornes

- `mod_lfo_v1` applique maintenant directement `PARAM_FILTER_CUTOFF`, `PARAM_FILTER_RESONANCE`, `PARAM_FILTER_EG_AMT`, `PARAM_FILTER_ATTACK`, `PARAM_FILTER_DECAY`, `PARAM_FILTER_SUSTAIN`, `PARAM_FILTER_RELEASE` et `PARAM_VCA_ATTACK`, `PARAM_VCA_DECAY`, `PARAM_VCA_SUSTAIN`, `PARAM_VCA_RELEASE` sur la lane mixer resolue, sans detour par `param_registry_apply_track_value_rt_fast`.
- Les conversions restent celles de `param_filter`: cutoff via LUT `exp2`, resonance quadratique, EG/sustain lineaire 0..1, temps A/D/R via LUT `exp2`.
- Le chemin direct est une projection runtime temporaire: il ne modifie pas les bases track-aware ni les miroirs UI, et la release LFO restaure la base capturee si aucune autre LFO de la track ne cible la meme destination.
- Aucune ecriture directe de `*_current`, aucun recalcul filtre/EQ/VCA et aucune nouvelle autorite mixer ne sont introduits.

## Addendum 2026-05-25 - LFO Sampler/Stream/Multi/Looper direct bornes

- `mod_lfo_v1` applique directement les cibles Sampler continues ou deja projetees runtime (`Gain`, `Start`, `End`, `Tune`, fades), les cibles Stream runtime (`Src BPM`, `Sync Len`, `Pitch`, `PlayMode`, `Loop`, `Stretch`, `Grain`), `PARAM_SAMPLER_MULTI_LOOP` et `PARAM_LOOPER_XFADE`.
- `PARAM_SAMPLER_SAMPLE` / selection instrument Multi restent exclus du chemin direct: aucune selection sample, import, load, SD/FatFs ou mutation de pool n'est ajoutee au chemin LFO.
- `PARAM_SAMPLER_CLIP_HOP` et `PARAM_SAMPLER_CLIP_SEARCH` restent des no-op runtime comme dans le fallback RT-fast actuel quand `update_base_state=0`.

## Addendum 2026-05-26 - TUNE RAM live

- `brick6_sampler_runtime_set_tune()` reprojette maintenant `PARAM_SAMPLER_TUNE` sur la voix `Sampler/RAM` active du track en recalculant uniquement le pas de lecture RAM (`ram_step_q16` et miroir `step_signed`), sans deplacer la tete, retrigger, reset VCA/enveloppe, stop, SD/FatFs ou allocation.
- En mode RAM chromatique (`Slice Count=Off`), le pas effectif reste `sample_rate / 48000 * 2^((note - 60 + Tune) / 12)`.
- En mode RAM slice (`Slice Count!=Off`), la note conserve son role de selection de slice et ne transpose pas; le pas effectif reste `sample_rate / 48000 * 2^(Tune / 12)`.
- `Start`/`End`/`Loop` gardent leur reconciliation live separee; le changement de `Tune` ne revalide pas les bornes et ne modifie pas la region active.

## Addendum 2026-05-26 - grille audio conservee, offsets sequenceur restaures

- `BRICK6_AUDIO_EVENT_GRID_FRAMES` dans `Inc/Core/brick6_audio_event_grid.h` reste disponible pour les traitements audio/modulation qui en dependent, notamment le LFO window-rate.
- Z1 ne force plus les evenements sequenceur PLAY au debut d'une fenetre 64: `audio.c` collecte les events avec offsets relatifs reels, rend le segment avant event, applique les events au sample de segment, puis rend la suite.
- Quand aucun event ni boundary interne ne tombe dans le demi-buffer, le rendu peut rester un demi-buffer complet de 64 frames.
- Pour eviter que les p-locks non-PLAY de boundary soient appliques trop tot, `audio.c` coupe aussi la collecte avant le prochain pulse interne connu, puis collecte/applique le boundary au segment suivant.
- Les clears mixer/sends restent complets; le delay global CLASSIC/DUAL partage maintenant un pool SDRAM unique et le reset du pool se fait lors du changement de type via l'API de controle, pas dans la boucle sample IRQ.
- Les filtres track biquad LP/HP/BP stereo et mono restent traites par chunks de `MIXER_FILTER_UPDATE_PERIOD`: l'enveloppe ADSR avance toujours a la cadence sample, mais les appels `fx_biquad_filter*_process_block()` ne sont plus relances sample par sample.

## Addendum 2026-05-26 - LFO window-rate experimental

- `mod_lfo_v1_process_block(frames)` est appele apres le clear des entrees externes mixer et avant le rendu des engines, afin que les destinations moteur voient la valeur LFO de la fenetre courante et que les destinations mixer restent appliquees avant `mixer_process()`.
- En mode experimental, le LFO traite une valeur par fenetre audio recue par `mod_lfo_v1_process_block(frames)`, typiquement la fenetre `BRICK6_AUDIO_EVENT_GRID_FRAMES`; une valeur de grille 32 ou 64 permet de tester le cout pire cas de 28 LFO actifs sans changer les destinations.
- Si le cache Matrix signale qu'aucune route n'est configuree, `mod_lfo_v1_process_block(frames)` sort avant le parcours des 14 tracks, des sources ENV/LFO et de `mod_matrix_process_track`. Une track sans route configuree est egalement sautee avant refresh runtime et scan Matrix complet.
- La phase LFO avance par nombre de frames ecoulees, pas par nombre fixe de ticks 3000 Hz; un LFO rapide ne declenche donc pas plus d'updates qu'un LFO lent.
- La valeur LFO est tenue sur la fenetre de controle; aucun ramp supplementaire ni instrumentation IRQ n'est ajoute dans cette passe.
- Correction 2026-05-26: le mode experimental n'attend plus un accumulateur `MOD_LFO_WINDOW_RATE_FRAMES`; il applique immediatement un tick LFO avec le `frames` du bloc courant pour eviter un retard d'une fenetre au demarrage/reset et respecter les sous-fenetres eventuelles.
- Correction 2026-05-26: le tick LFO est avance avant le rendu Drum/Sampler/Looper/Prism; les modulations moteur ne restent plus decalees d'une fenetre audio par rapport au rendu.

## Addendum 2026-05-27 - fast path Sampler/RAM actif

- Dans `brick6_sampler_runtime_render_track()`, une voix `BRICK6_SAMPLER_VOICE_RAM` active est rendue directement par `brick6_sampler_runtime_render_ram()` avant le scan des voix Multi globales et avant les fallbacks Classic/Stream.
- Le gate VCA RAM reste teste avant ce fast path: si la release VCA est terminee, la voix RAM est toujours clear avant rendu.
- Le rendu RAM resident, les bornes, le loop/reverse/pingpong, les fades de declick, le diagnostic first-output et le mix des tails restent inchanges.
- Le chemin `Sampler/Multi` conserve son scan global; le fast path RAM ne s'applique pas aux tracks de type `TRACK_RUNTIME_TYPE_MULTI`.

## Addendum 2026-05-27 - no-copy Sampler/RAM actif vers mixer

- `mixer_begin_external_stereo()` / `mixer_commit_external_stereo()` exposent un chemin limite de reservation des buffers externes stereo mixer, sans copie intermediaire.
- `brick6_render_sampler_tracks()` utilise ce chemin seulement pour une track Sampler non-Clip/non-Multi dont `g_sampler_voice[track]` contient une voix `BRICK6_SAMPLER_VOICE_RAM` active.
- Les buffers externes mixer sont clear avant rendu, car `brick6_sampler_runtime_render_track()` et les tails de declick accumulent avec `+=`.
- Les chemins Stream, Multi, Clip et les cas sans voix RAM active gardent le chemin historique `sampler_tmp_l/r` + `mixer_submit_external_stereo()`.
- Mono-native RAM reste hors patch: les slots RAM residentiels conservent le format `SAMPLER_RAM_FORMAT_FLOAT32_INTERLEAVED` stereo, y compris pour les WAV mono dupliques au load.

## Addendum 2026-05-27 - linear pitch Sampler optimise

- Le pitch Sampler conserve strictement l'interpolation CLEAN lineaire `a + (b - a) * frac`; aucun mode nearest/stochastic/sinc ni parametre utilisateur n'est ajoute.
- Le rendu RAM forward pitch segmente toujours les boundaries comme avant, mais la boucle hot sans boundary reutilise localement la paire stereo `N+1` quand elle devient `N` a la frame suivante. Les cas boundary/loop/end/reverse/pingpong/fade gardent leurs guards locaux.
- Le mixer Stream/Multi pitch lineaire garde les acquisitions spans/pages existantes et ne modifie pas la policy `sample_page_cache`, window-lock ou `STREAM_SAFE`; les spans entierement internes utilisent une boucle sans checks page/loop par sample, sinon le fallback boundary existant reste actif.

## Addendum 2026-05-27 - pitch stochastic nearest experimental

- Le pitch Sampler experimental remplace l'interpolation lineaire des chemins pitch couverts par un nearest distribue deterministe: pour chaque position Q16, le code choisit `N` ou le voisin selon `frac > threshold`, puis lit une seule frame source stereo.
- Le seuil est produit par un hash entier borne et sans etat global, base sur la cellule source et un seed stable. RAM melange frame source, track/note/slot/trigger; Stream/Multi reader melange frame source et seed de segment/sample. La fraction Q16 ne participe pas au hash et sert uniquement a comparer `frac > threshold`.
- Les chemins non pitch 1x et les fast paths RAM/Stream entiers restent inchanges. Le patch n'ajoute aucun mode utilisateur et ne pretend pas etre CLEAN.
- Les guards de region, loop, reverse, pingpong et page boundary restent conserves: si le voisin stochastic n'est pas dans le span/page deja acquis, le rendu retombe sur la frame de base sans modifier `sample_page_cache`, window-lock ou `STREAM_SAFE`.

## Addendum 2026-05-27 - pitch nearest brut experimental

- Le pitch Sampler experimental courant remplace le nearest distribue par un nearest brut minimal: l'index lu est toujours `position_q16 >> 16` / `floor(position)` pour les chemins pitch couverts.
- Aucun hash, seuil, dither, smoothing de sortie ou interpolation lineaire n'est utilise dans ces chemins. Le rendu lit une seule frame source stereo par frame audio, applique le gain et accumule.
- Les chemins non pitch 1x restent inchanges. Les bounds region/page/span et les politiques `sample_page_cache`, window-lock et `STREAM_SAFE` restent inchanges.

## Addendum 2026-05-27 - extraction renderer Sampler/RAM

- `brick6_sampler_runtime_render_ram_track()` isole le chemin IRQ `Sampler/RAM`: validation RAM, gate VCA RAM, rendu resident, diagnostic first-output et tails de declick.
- `brick6_sampler_runtime_render_track()` reste le point d'entree generique et route une voix `BRICK6_SAMPLER_VOICE_RAM` active vers ce renderer dedie avant le scan Multi et les fallbacks Classic/Stream.
- `brick6_render_sampler_tracks()` conserve le no-copy stereo mixer pour RAM actif, mais appelle maintenant le renderer RAM dedie apres `mixer_begin_external_stereo()`.
- Stream/Clip, Multi, page-cache/window-lock et policy SD restent inchanges dans cette extraction.

## Addendum 2026-05-27 - extraction renderer Sampler/Stream

- `brick6_sampler_runtime_render_stream_track()` isole le chemin IRQ `Sampler/Stream` encore nomme `Clip` en interne: etat clip/stream, shifter/stretch, `brick6_sampler_render_sample()`, reader Stream et tails de declick.
- `brick6_sampler_runtime_render_track()` route maintenant `TRACK_RUNTIME_TYPE_CLIP` vers ce renderer dedie avant les chemins RAM/Multi/fallback.
- Les noms legacy `CLIP`, la policy page-cache/window-lock/STREAM_SAFE, Multi, RAM et Looper restent inchanges dans cette extraction.

## Addendum 2026-05-27 - extraction renderer Sampler/Multi

- `brick6_sampler_runtime_render_multi_track()` isole le chemin IRQ `Sampler/Multi`: scan borne de `g_sampler_multi_voice[]`, filtrage `owner_track_id`, rendu des voix Multi, diagnostic first-output et tails de declick.
- `brick6_sampler_runtime_render_track()` devient un dispatcher court: `Clip/Stream` vers `brick6_sampler_runtime_render_stream_track()`, `Multi` vers `brick6_sampler_runtime_render_multi_track()`, voix RAM active vers `brick6_sampler_runtime_render_ram_track()`, puis fallback legacy Classic si present.
- Les regles de voice steal/release, page-cache/window-lock/STREAM_SAFE, RAM, Stream/Clip, Looper et LFO restent inchanges.

## Addendum 2026-05-27 - micro-optimisations hot path

- `brick6_audio_runtime_dsp()` lit le cache `track_runtime_get_cached_synth_usage()` au lieu de rescanner les tracks pour le comptage Drum.
- Le chemin Drum IRQ ne lit plus `ui_get_active_track()`; le diagnostic local suit seulement le nombre de drums effectivement rendus.
- `mixer_process()` conserve l'appel de projection `mix_track -> logical_track`, mais celui-ci est maintenant une lecture O(1) d'une table Z2 reconstruite hors IRQ.

## Addendum 2026-05-29 - Prism/Braids rendu bloc fixe

- `brick6_braids_runtime_render_instance()` ne transmet plus de taille partielle a `braids::MacroOscillator::Render()`: le wrapper genere uniquement des blocs complets de 24 samples (`kBraidsRenderBlockSize`).
- Chaque instance Prism/Braids porte un cache de sortie statique de 24 samples avec offset/count pending. Le rendu IRQ consomme d'abord les samples pending, genere un nouveau bloc complet seulement si necessaire, puis conserve le surplus pour le prochain appel.
- Ce cache reste local a l'instance (`BRICK6_BRAIDS_MAX_INSTANCES`), sans allocation dynamique ni etat global partage. Reset/all-notes-off vident le cache; un changement de shape Prism l'invalide pour eviter de rejouer des samples de l'ancien moteur.
- Les sous-segments audio Z1 peuvent rester non multiples de 24 a cause des evenements sample-accurate; l'adaptation bloc-fixe appartient au wrapper Prism, pas a `audio.c` ni aux sources Mutable/Braids.

## Addendum 2026-07-17 - frontiere Board audio premium

- `audio.c` conserve l'autorite IRQ, les buffers DMA, la maintenance cache, le decoupage en sous-segments et les callbacks HAL half/full; l'identification du handle RX et le demarrage SAI DMA passent par `board_audio_*`.
- Le backend premium `Board/Premium/Src/board_audio_premium.c` porte CS42448, SAI2, demarrage RX/TX DMA et mapping physique TDM8.
- `audio_io.c` garde le monitoring metronome MAIN et delegue le pack/unpack physique a `board_audio_unpack_input()` / `board_audio_pack_output()`. MAIN/CUE restent des bus mixer communs; CUE physique premium reste mappe sur les slots TX 2/3.

## Addendum 2026-07-17 - lot 4B low-cost ressources audio

- La variante low-cost compile avec `UI_AUDIO_INPUT_RESOURCE_COUNT=1` et `UI_AUDIO_INPUT_PROTO_WIRED_COUNT=1`: seule la ressource logique `Input1` reste exposable/routable comme entree physique produit.
- Le backend low-cost conserve le flux TLV320AIC3204 SAI1 I2S stereo: slots RX 0/1 -> `Input1` L/R, slots TX 0/1 <- MAIN L/R. Le codec reste configure en 24-bit / 48 kHz, STM32 maitre MCLK/BCLK/FS, TLV esclave.
- Les tracks d'entree inexistantes low-cost `Input2..4` ne sont plus activees par le bridge runtime audio et ne sont plus reservees par Z2; leurs buffers bloc sont zeroes par le backend board au lieu d'etre parcourus comme entrees physiques.
- Low-cost compile `MIXER_HAS_CUE_BUS=0`: les buffers bloc CUE et Looper-CUE ne sont pas instancies, les accumulations CUE/XFade CUE sont compilees hors chemin IRQ, et `board_audio_pack_output()` ignore toute donnee CUE. Premium garde MAIN/CUE physique et bus CUE inchanges.
- Gain mesure apres build Release: low-cost DTCMRAM 83584 B contre premium 84608 B, soit 1024 B de buffers CUE bloc retires dans Z1. Les buffers DMA low-cost restent reduits par `BOARD_AUDIO_TDM_SLOTS=2` contre TDM8 premium.

## Addendum 2026-07-23 - sortie casque TLV320AIC3204 low-cost

- Le flux low-cost transporte du PCM signe 24 bits dans deux slots SAI de 32 bits; le codec recoit BCLK/WCLK depuis `SAI1_A`.
- L'initialisation codec active les DAC gauche/droit avec leurs donnees serie respectives, route LDAC vers HPL et RDAC vers HPR, puis alimente et de-mute uniquement les sorties casque.
- Les gains HPL/HPR sont maintenus mutes pendant le routage et la mise sous tension, puis ouverts a 0 dB apres le delai de stabilisation.
- Aucun registre reserve de la page 1 ne fait partie de la sequence d'initialisation.

## Addendum 2026-07-25 - fondation runtime Stack

- `Src/Core/brick6_stack_runtime.c` porte le runtime Stack v0: trois slots, niveaux Q15, noise, note on/off/all-notes-off/reset, pending mono 24 samples par instance et trim nominal Q15 applique apres somme.
- Le chemin Z1 rend les tracks bindees `TRACK_RUNTIME_ENGINE_STACK` via `brick6_stack_runtime_render_instance()` puis les injecte en mono-native avec `mixer_submit_external_mono_native()`, comme source externe separee de Prism.
- Le kernel audible actuel est volontairement provisoire et local a Stack; aucune instance `MacroOscillator` Braids n'est creee par slot et le runtime Prism historique reste rendu par `brick6_braids_runtime`.
- La file de commandes Stack est dimensionnee pour absorber un refresh/reapply complet de toutes les instances Stack (`reset + 16 params` par track) sans overflow silencieux avant drainage audio.

## Addendum 2026-07-25 - catalogue et dispatch Stack

- Stack expose maintenant un catalogue constant `BRICK6_STACK_MODEL_*` couvrant le socle cible, avec mapping modele -> famille -> kernel -> renderer et taille d'etat kernel indicative.
- Chaque slot stocke le modele, la famille, le kernel et le renderer resolus; le rendu appelle un renderer par slot et par chunk 24, sans switch modele dans la boucle sample.
- Les renderers lourds non encore implementes restent explicitement silencieux dans cette etape; le placeholder phase local conserve seulement le chemin audible minimal Stack.

## Addendum 2026-07-25 - kernels analogiques Stack

- Les modeles analogiques simples Stack actifs sont regroupes en `SINFD`, `TRIFD` et `SHAPE`; l'ancien modele `SOFT` prototype n'est plus expose.
- `SINFD` utilise une base sine propre et `TRIFD` une base triangle, chacun avec `FOLD/SYM/SHAPE`; `FOLD=0` conserve strictement la forme non coloree et aucun oversampling local n'est utilise.
- `SHAPE` reprend l'ancien comportement `SAW/SQUARE`: `TIMBRE` garde la forme/largeur d'impulsion et `COLOR` morphe `SAW -> SQUARE`; les extremites saw/square retournent directement la forme cible.
- La math pure des formes analogiques Stack est factorisee dans `brick6_stack_waveform`, consommee par le runtime Stack et par la preview UI, sans instance `MacroOscillator` et sans toucher Prism/Braids.

## Addendum 2026-07-25 - wavetable Stack

- Le modele Stack `WAVETABLE` utilise les donnees Braids `wt_waves` via un adaptateur C++ local `brick6_stack_braids_resources`, sans copier la banque et sans instancier `MacroOscillator`.
- `TIMBRE` scanne les 16 waves d'une banque et `COLOR` selectionne une des 16 banques derivees de `wt_waves`; `wt_map` et `wt_code` ne sont pas consommes par ce kernel Stack v1.
- Prism/Braids garde son runtime et ses ressources historiques inchanges; l'adaptateur expose seulement l'echantillonnage table necessaire au kernel Stack.

## Addendum 2026-07-25 - modeles complexes Stack

- Les modeles Stack `SUB`, `FM`, `FEEDBACK FM`, `RING`, `TRIPLE SAW`, `TRIPLE SQUARE` et `SWARM` ont maintenant des renderers locaux jouables, avec phases auxiliaires et feedback portes par `stack_osc_slot_t`.
- Ces modeles utilisent le dispatch Stack resolu par slot et restent sans instance complete `MacroOscillator` ou `DigitalOscillator`; Prism/Braids conserve son runtime historique separe.
- `SUB` mixe un principal saw/square avec un sub divise, `FM` et `FEEDBACK FM` utilisent `TIMBRE` pour l'index et `COLOR` pour le ratio, `RING` emploie deux modulateurs detunes, `TRIPLE SAW/SQUARE` utilisent `TIMBRE` et `COLOR` comme detunes osc 2/3, et `SWARM` reste un ensemble saw detune simple.

## Addendum 2026-07-25 - VOICE Stack osc detune/reset

- Le systeme d'unison interne Stack ajoute par erreur est retire: pas de compteur `VOICE`, pas de duplication de renderer par voix, pas de `SPREAD`/`DRIFT` Stack, pas de normalisation `1/N` et retour a l'injection mono-native.
- `OSC DETUNE` agit uniquement sur les trois slots Stack existants. En `RESET=RESET`, trois offsets pseudo-aleatoires deterministes sont generes au note-on hors boucle sample, recentres autour de zero, puis appliques au `phase_inc` des slots tant que la note dure; en `RESET=FREE`, les offsets courants sont conserves.
- `RESET` ne gere plus que la phase des trois slots: `FREE` garde les phases libres; `RESET` fixe au note-on des phases deterministes par slot (`phase=slot*0x55555555`, `phase2=0x55555555+slot*0x11111111`, `phase3=0xAAAAAAAA-slot*0x11111111`).

## Addendum 2026-07-25 - declick retrigger Stack FREE

- Stack ne pre-rend plus de surplus au-dela des frames demandees par le segment audio courant: le cache 24 samples reste local au chunk en cours et ne laisse plus l'etat phase/feedback avancer en avance sur les samples deja sortis.
- En `RESET=FREE`, le note-on Stack conserve les phases principales, phases auxiliaires, feedback et offsets `OSC DETUNE`; aucun de ces etats n'est regenere ou nettoye au retrigger.
- En `RESET=RESET`, le note-on conserve le comportement volontaire de redemarrage: phases par slot remises aux valeurs deterministes, feedback clear et offsets `OSC DETUNE` regeneres.
- Le changement de `MODEL` Stack est idempotent sur meme modele, afin qu'un reapply/p-lock identique ne reset pas phase/etats de kernel inutilement.

## Addendum 2026-07-26 - Stack FREE free-running

- En `RESET=FREE`, Stack avance les phases principales et auxiliaires des trois slots pendant `gate=off`, sans rendu modele complet, sans bruit, sans feedback FM et sans audio mixer utile.
- Chaque slot utilise le meme increment que son renderer actif: `phase_inc` du slot pour les phases principales, et les increments derives locaux pour wavetable, sub, FM, ring, triple et swarm. Les offsets `OSC DETUNE` deja projetes dans `phase_inc` continuent donc a faire deriver les slots pendant le silence.
- En `RESET=RESET`, ce chemin free-running est coupe; le note-on continue de reset phases, feedback et offsets `OSC DETUNE` comme avant.

## Addendum 2026-07-26 - Stack SINFD/TRIFD

- Les modeles Stack `SINFD` et `TRIFD` ajoutent deux renderers locaux dedies, bases respectivement sur la sine Stack propre et la triangle Stack existante.
- Leur wavefolder est factorise dans `brick6_stack_waveform`: `FOLD=0` retourne strictement la base clean; `SYM` ajoute un offset de fold dependant de `FOLD`; `SHAPE` arrondit le repli via la LUT sine Stack apres un miroir borne.
- Aucun `sinf`, `tanhf`, `powf`, oversampling, filtre correctif, allocation ou acces Prism/Braids n'est ajoute dans le chemin sample Stack.
- `SOFT` n'est plus un modele Stack actif: l'ancien slot enum 0 est remplace par `SINFD`. Les autres modeles Stack gardent leurs renderers et mappings sonores.

## Addendum 2026-07-26 - Stack renderers simples faible CPU

- Les calibrations loudness/Braids-like propres a Stack sont retirees des renderers locaux non-Prism: `TRIPLE SAW`, `TRIPLE SQUARE` et `SWARM` utilisent des moyennes simples de trois oscillateurs; `SWARM` n'a plus de filtre local ni de cinq lectures saw.
- `WAVETABLE` Stack lit une seule table selectionnee par `TIMBRE` dans la banque choisie par `COLOR`: plus de double demi-increment, moyenne ou crossfade de waves dans le chemin sample.
- `SUB`, `FM`, `FEEDBACK FM`, `RING` et `SHAPE` gardent des formules Stack directes: balance sub lineaire, profondeur FM sans boost de phase, feedback FM borne par multiplication simple du sample precedent, ring en multiplication Q15, square/PWM sans gain Braids.
- Le mix energie/levels, le soft clip de sortie Stack, la sine interpolee en `int64_t`, `SINFD/TRIFD`, `PARAM3` et la surface UI TONE Stack restent les autorites courantes; Prism/Braids historique reste separe.

## Addendum 2026-07-27 - identite Wave sans rendu audio

- `TRACK_RUNTIME_ENGINE_WAVE` est reserve au futur moteur wavetable utilisateur.
- A cette etape d'identite, aucun renderer Z1 Wave n'etait branche: aucune lecture SDRAM, aucun acces SD et aucun appel au runtime Prism/Braids n'etaient introduits pour `Synth/Wave`.

## Addendum 2026-07-27 - asset WAVETABLE resident SDRAM

- `wavetable_pool` ajoute le chargement resident SDRAM pour les futurs assets `Synth/Wave`.
- Format courant charge: fichier `B6WT` little-endian, header 32 octets, `frame_sample_count=2048`, `frame_count>0`, samples source mono `S16` ou `F32`, conversion runtime en `FLOAT32_MONO`.
- Le pool ne cree pas de streamer concurrent: il reutilise `SAMPLE_PAGE_SLOT_POOL` pour l'audio resident et `sample_global_pool` pour les slots, le budget et l'affichage memoire global.
- Cette passe asset ne branchait pas encore le chemin audio; l'addendum runtime ci-dessous precise le branchement audio SDRAM-only de `TRACK_RUNTIME_ENGINE_WAVE`.

## Addendum 2026-07-27 - runtime audio Synth/Wave

- `brick6_wave_runtime` branche maintenant `TRACK_RUNTIME_ENGINE_WAVE` dans le rendu Z1.
- Chaque instance suit `instance_id == track_id`, porte deux oscillateurs mono et lit les tables via `sample_global_pool`/`wavetable_pool`; le rendu audio consomme uniquement les pointeurs SDRAM `FLOAT32_MONO` deja charges.
- Le rendu fait l'interpolation lineaire dans la frame 2048 samples et l'interpolation lineaire entre frames selon `POS` remappe dans `START/END`.
- `POS` est smooth localement par oscillateur apres remap START/END. `LEVEL=0` coupe l'oscillateur. `TUNE` recalcule l'increment de phase en demi-tons. `PHASE` applique le depart 0/90/180/270 au note-on. `FLIP` applique inversion X et/ou lecture inverse Y.
- Le moteur sort un mono externe vers `mixer_submit_external_mono_native`; aucune lecture SD, FatFs, allocation ou scan de table n'est introduit dans l'IRQ audio.
- Hors passe: pages TONE, destinations Matrix et widget preview.
