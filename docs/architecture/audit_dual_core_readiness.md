# Audit dual-core readiness M7/M4

## 1. Verdict global

Mise a jour apres passe H743 mono-coeur `audio_control_snapshot`: la premiere frontiere locale controle/audio est en place pour `track_runtime`, `track_sound_state` et `track_tone_sound_state`. L'audio consomme maintenant une projection double-buffer appliquee au debut de `brick6_audio_runtime_dsp()`. Les constats d'audit ci-dessous restent valides pour les autres domaines non migres: scheduler mutable, queues MIDI/control, sampler/page-cache, Looper/Recorder et preview SD.

Le firmware est partiellement pret a une migration STM32H7 dual-core. La separation conceptuelle annoncee par la documentation est verifiee dans le code pour les trois grands niveaux:

- etat canonique et controle: `track_state`, `track_sound_state`, `track_tone_sound_state`, `seq_model`, `param_store`, persistence;
- projection runtime track-aware: `track_runtime`, `seq_runtime_exec`, `seq_play_scheduler`, `param_registry`/backends;
- execution bornee: IRQ audio, mixer, engines, LFO, sampler readers, Looper transient.

La trajectoire "seams explicites sans bus central ni IPC premature" est donc credible. En revanche, le code n'est pas encore directement transplantable sur M7/M4: plusieurs etats mutables sont lus/ecrits des deux domaines actuels sans contrat inter-coeur, et certains chemins audio appliquent encore directement des commandes vers des modules qui deviendraient naturellement M4-owned.

Blockers principaux:

- l'IRQ audio appelle encore des APIs qui mutent des etats globaux partages: scheduler, engines, mixer, sampler voices, Looper, LFO et parfois sorties MIDI;
- `track_runtime_refresh_if_dirty()` est appele depuis l'IRQ audio, meme s'il refuse le refresh en IRQ; le dirty runtime reste donc une fuite directe du domaine controle vers le domaine audio;
- le sampler/page-cache a de bons concepts de generation, refs, window locks et page READY, mais les metadata/refcounts ne sont pas protegees pour deux caches CPU et deux coeurs;
- les services SD/writer/preview sont hors IRQ, mais leur etat est encore partage en RAM avec le rendu audio sans protocole de publication versionnee.

Verdict: preparation moyenne-haute sur les seams, faible sur les garanties de concurrence bicoueur. La prochaine etape ne doit pas etre un projet CM4, mais une phase H743 mono-core qui remplace les acces croises directs par des contrats locaux simules.

## 2. Architecture actuelle observee

### Contextes d'execution

| Contexte | Entrees code | Responsabilites observees | Etat readiness |
|---|---|---|---|
| IRQ audio SAI/DMA | `HAL_SAI_RxHalfCpltCallback()` et `HAL_SAI_RxCpltCallback()` dans `Src/Audio/audio.c:388` et `Src/Audio/audio.c:426` | decoupe half-buffer, cache maintenance, collecte/apply seq, rendu DSP, notification tasklet | hard-RT clair, mais consomme encore beaucoup d'etats globaux |
| IRQ SAI HAL | `Board/*/Generated/Src/stm32h7xx_it.c` appelle `HAL_SAI_IRQHandler` | dispatch HAL vers callbacks audio | restera M7 |
| IRQ TIM12 | `Src/MIDI/midi.c:969` appelle `seq_runtime_time_adapter_process_internal_from_irq()` | tick interne auxiliaire seq, sans avance step finale | cible discutable: M4 si MIDI/clock externe, M7 si uniquement cadence audio |
| IRQ TIM5 | callbacks MIDI clock dans `Src/MIDI/midi.c` | clock MIDI out/timer | plutot M4, sauf horodatage audio a projeter |
| IRQ USB Host/Device | `OTG_*_IRQHandler` dans generated + USB stack | RX/TX USB, MIDI host/device | M4 cible |
| PendSV MIDI | `stm32h7xx_it.c` + `midi_usb_tx_deferred_service_from_isr` | flush USB MIDI differe | M4 cible |
| Superloop | `Board/Premium/Generated/Src/main.c:192`, `Board/LowCost/Generated/Src/main.c:195` | `brick6_app_process`, MIDI host, UI tasklet, render OLED, flush display | M4 majoritaire a terme |
| Tasklet engine | `engine_tasklet_notify_frames()` depuis audio, `engine_tasklet_poll()` dans superloop | pont IRQ audio -> ticks systeme bornes | contrat IRQ->M4 a definir |
| UI tasklet | `ui_tasklet_poll()` depuis `main` | UI/OLED/input/navigation | M4 |
| Services SD/cache/writers | `brick6_app_process()` dans `Src/Core/brick6_app_init.c:177` | sample cache, writer, looper export, waveform, preview, pattern load | M4 pour SD/writer, avec publication vers M7 |
| Callbacks MIDI/USB | `midi.c`, `midi_host.c`, `usbh_midi.c` | dispatch MIDI, clock, live rec, output | M4, avec evenement timestamp vers M7 |

La separation hard-RT/systeme est reelle: les FatFs/SD services restent dans `brick6_app_process()` (`Src/Core/brick6_app_init.c:184-206`), pas dans l'IRQ audio. Le chemin audio est toutefois encore un gros consommateur direct de projections et de runtime mutable.

### Chemin audio observe

1. `HAL_SAI_RxHalfCpltCallback()` / `HAL_SAI_RxCpltCallback()` entrent dans `process_half()` (`Src/Audio/audio.c:388`, `Src/Audio/audio.c:426`).
2. `process_half()` invalide le D-cache RX, segmente le half-buffer et appelle `seq_runtime_audio_collect_block_events()` (`Src/Audio/audio.c:251-269`).
3. Les events sont appliques a l'offset via `audio_apply_seq_event_at_sample()` (`Src/Audio/audio.c:83-109`):
   - boundary Looper: `brick6_looper_runtime_on_boundary_edge`;
   - metronome: `metronome_runtime_trigger_at`;
   - events PLAY: `seq_runtime_audio_apply_event`.
4. Chaque segment appelle `audio_process_block_int32()` (`Src/Audio/audio.c:115`).
5. `audio_process_block_int32()` depile jusqu'a 8 `control_event_t`, unpacke les entrees, appelle le DSP, puis packe la sortie (`Src/Audio/audio_float.c:561-590`).
6. `audio_io_unpack()` delegue au backend Board (`Src/Audio/audio_io.c:11`).
7. `dsp_engine_process_block()` appelle `brick6_audio_runtime_dsp()` via le callback pose dans `brick6_app_init()` (`Src/Core/brick6_app_init.c:130`).
8. `brick6_audio_runtime_dsp()` rafraichit/consulte `track_runtime`, execute LFO, synths, sampler, Looper, Wave, voice manager, mixer, MasterFX et preview SD (`Src/Core/brick6_audio_runtime.c:197-259`).
9. `mixer_process()` somme les lanes et lit la projection inverse `mix -> logical track` (`Src/Audio/mixer.c:2188`, `Src/Audio/mixer.c:2431`).
10. `fx_master_macro_process_block()` lit `track_tone_sound_state` (`Src/Audio/fx_master_macro.c:1098`, `Src/Audio/fx_master_macro.c:1110`).
11. `sd_preview_render_main()` mixe le ring preview RAM dans MAIN (`Src/Storage/sd_preview.c:709`).
12. `audio_io_pack_ramped()` ajoute le metronome monitor puis delegue `board_audio_pack_output()` (`Src/Audio/audio_io.c:37-70`).
13. `process_half()` nettoie le D-cache TX (`Src/Audio/audio.c:286`) puis les callbacks notent `engine_tasklet_notify_frames()` (`Src/Audio/audio.c:397`, `Src/Audio/audio.c:435`).

### Appels audio vers etats ou modules M4-cibles

| Appel depuis audio | Fichier | Probleme dual-core |
|---|---|---|
| `control_event_pop()` | `Src/Audio/audio_float.c:568` | consomme une queue controle dans l'IRQ; le producteur serait M4, il faut une file SPSC M4->M7 explicite ou retirer ce chemin |
| `track_runtime_refresh_if_dirty()` | `Src/Core/brick6_audio_runtime.c:202` | lecture dirty controle depuis M7; refresh refuse en IRQ, mais dependance directe au dirty M4 |
| `seq_runtime_audio_collect_block_events()` | `Src/Audio/audio.c:262` | collecte avance timeline et scheduler dans IRQ; a garder M7 ou remplacer par evenements prepares M4 + apply M7 |
| `seq_runtime_audio_apply_event()` | `Src/Audio/audio.c:109` | applique note/program vers engines/MIDI; MIDI out est M4-cible, engine apply M7-cible |
| `midi_note_on/off()` via scheduler | `Src/Seq/seq_play_scheduler.c:404-415`, `Src/Seq/seq_play_scheduler.c:872-923` | sortie MIDI depuis apply audio: doit devenir notification M7->M4 ou etre sortie par M4 avant bloc selon timestamp |
| `mod_lfo_v1_process_block()` | `Src/Core/brick6_audio_runtime.c:213` | bon candidat M7, mais lit config canonique Z3 et appelle setters mixer/sampler/engine directs |
| `sd_preview_render_main()` | `Src/Core/brick6_audio_runtime.c:257` | rendu RAM-only acceptable M7, mais etat ring produit par service SD M4 doit etre publie proprement |
| `track_tone_sound_state_get_const()` par MasterFX | `Src/Audio/fx_master_macro.c:1110` | lecture directe d'etat canonique M4-cible depuis M7; a remplacer par snapshot MasterFX M7 |
| `sample_page_cache_try_acquire/release` via readers | `Src/Sampler/sample_page_cache.c:1024-1136` | metadata/refcounts partages M4/M7 sans atomiques/cache protocol |
| `multi_record_writer_push_audio_block_from_irq()` / Looper preroll | `Inc/Storage/multi_record_writer.h:101`, `Inc/Core/brick6_looper_runtime.h:90` | bon pattern ring RAM, mais devra etre une publication M7->M4 cache-safe |

## 3. Separation M7/M4 recommandee

### M7 recommande

- IRQ/DMA audio, `audio.c`, `audio_float.c`, `audio_io.c`.
- `brick6_audio_runtime_dsp`, mixer, MasterFX, metronome render, XFade.
- Engines audio: Drum/Plaits, Wave/Braids, Sampler voices, Looper playback.
- Application sample-accurate des events deja prepares.
- LFO/audio-rate ou window-rate effectif.
- Etat runtime strictement audio: voices, enveloppes, phase, playheads, readers actifs, tails, filters, VCA, delay/reverb histories.
- Producteurs RAM vers M4: record rings, Looper/Audio Rec capture, telemetry.

### M4 recommande

- UI, controles, Hall/STEP, keyboard input, OLED, LEDs.
- MIDI USB host/device et dispatch MIDI externe.
- SDMMC/FatFs, `sd_access_gate`, sample loaders, page fill, preview decode, waveform cache, persistence.
- Preparation musicale lente: editions params, snapshots, Kit/Patch/Project/Pattern.
- Preparation des evenements de sequence par bloc ou par horizon borne, puis publication M4->M7.
- Publication de pages sample READY et de snapshots de parametres.

### Corrections a la repartition cible initiale

- Le sequenceur ne doit pas etre deplace entierement sur M4. Le code actuel montre que la progression sample-domain et la collecte/apply sont dans le domaine audio (`seq_runtime_exec_collect_block_events`, `Src/Seq/seq_runtime_exec.c:644`). A terme, M4 peut preparer les pas et p-locks, mais M7 doit rester proprietaire de l'application sample-accurate dans le bloc.
- Le MIDI out ne doit plus etre appele directement depuis l'audio si USB/MIDI est M4. Les events MIDI doivent etre publies M7->M4 avec timestamp/ordre.
- `track_runtime` est aujourd'hui une projection controle->audio lue partout. La source canonique reste M4, mais une copie runtime immutable par generation doit etre publiee au M7.
- `mod_lfo_v1` doit rester M7 pour les destinations audio continues, mais sa configuration/base doit etre publiee par snapshot M4->M7.

## 4. Blockers classes par gravite

### Critique

1. Etats mutables audio et controle partages sans protocole inter-coeur.
   - Exemples: `g_track_runtime_ctx` (`Src/Core/track_runtime.c:22`), `g_tracks` mixer (`Src/Audio/mixer.c:122`), `g_seq_runtime_exec_state` (`Src/Seq/seq_runtime_exec.c:36`), `g_sample_page_desc` (`Src/Sampler/sample_page_cache.c:59`).
   - Risque: race M4/M7, cache incoherent, update partiel observe par l'IRQ.

2. Sortie MIDI appelee depuis l'application audio des events.
   - `seq_play_scheduler_audio_apply_event()` appelle `seq_play_scheduler_emit_midi_note_raw()` puis engines (`Src/Seq/seq_play_scheduler.c:872-923`).
   - Risque: M7 depend d'un peripherique M4 et d'une pile potentiellement non hard-RT.

3. Page-cache pas encore bicore-safe.
   - Acquire/release valident generation (`Src/Sampler/sample_page_cache.c:1033-1097`) mais modifient `last_touch`, state et metadata globales.
   - Risque: refcount implicite/eviction/cache CPU non synchronises.

4. Snapshots param/audio absents.
   - MasterFX lit directement `track_tone_sound_state` (`Src/Audio/fx_master_macro.c:1110`).
   - LFO lit `track_sound_state` pour bases/config (`Src/Mod/mod_lfo_v1.c:736`, `Src/Mod/mod_lfo_v1.c:748`).
   - Risque: M7 lit une structure canonique pendant write M4.

### Eleve

5. `brick6_app_init()` centralise init board, storage, audio, UI, seq et persistence (`Src/Core/brick6_app_init.c:86-159`).
   - Risque: difficile de couper boot CM7/CM4 sans detourer l'ordre des dependances.

6. `main.c` CubeMX initialise tous les peripheriques dans un seul firmware (`Board/Premium/Generated/Src/main.c:160-183`, `Board/LowCost/Generated/Src/main.c:160-186`).
   - Risque: ownership peripherique dual-core non exprime.

7. Events seq collectes avec mutation de timeline dans l'IRQ.
   - C'est correct mono-core, mais la preparation M4/M7 demandera un split net entre build d'events et apply sample-accurate.

8. Rings record/previews doivent etre cache-safe.
   - Le pattern RAM-only est bon, mais pas encore specifie pour D-cache CM7/CM4.

### Moyen

9. `track_runtime_refresh_if_dirty()` en IRQ refuse le refresh mais laisse l'audio lire l'ancien runtime (`Src/Core/track_runtime.c:926-957`).
   - C'est une degradation acceptable mono-core; en dual-core il faut un contrat de publication de revision.

10. `control_event_pop()` dans `audio_process_block_int32()` (`Src/Audio/audio_float.c:568`) n'a pas de role clair dans le chemin audio cible.

11. Board abstraction progresse, mais CubeMX/generated et handles HAL restent tres centralises.

## 5. Tableau des etats et proprietaires

| Etat | Proprietaire actuel | Lecteurs | Ecrivains | Contextes actuels | Proprietaire cible | Echange requis |
|---|---|---|---|---|---|---|
| `track_state` | Z2/Z5 canonique | Z2, Z3, Z5, Z6 | UI, restore, Kit/Patch | superloop/UI/storage | M4 | snapshot structure M4->M7 |
| `track_runtime` | Z2 projection | Z1, Z3, Z4, Z5 | refresh explicite Z2 | superloop + lecture IRQ | M7 copie publiee, M4 source | snapshot versionne immutable |
| `track_sound_state` | Z3 canonique commun | Z3, Z6, LFO | param writes, restore | superloop + LFO audio | M4 canonique, M7 snapshot | snapshot param audio |
| `track_tone_sound_state` | Z3 canonique TONE | Z3, Z6, MasterFX | param writes, restore | superloop + audio | M4 canonique, M7 snapshot | snapshot TONE/MasterFX |
| `param_store.active` | Z3 UI/global mirror | UI, wrappers, persistence | param_set/wrappers | superloop/UI | M4 | aucun direct M7 |
| `mod_lfo_v1` runtime | Z3/Z1 execution | audio | config writes + audio tick | superloop + IRQ audio | M7 runtime, M4 config | config snapshot + note trigger events |
| `seq_model` | Z4 canonical pattern | scheduler, UI, storage | UI/live-rec/restore | superloop + scheduling | M4 canonical | events/p-lock materialises M4->M7 |
| `seq_runtime_exec` | Z4 execution | audio, UI queries | audio collect, control commands | IRQ + superloop | M7 execution | commandes transport M4->M7, telemetry M7->M4 |
| `seq_play_scheduler` queue | Z4 scheduler | audio collect/apply | schedule step/audio | IRQ audio | split: build M4, apply M7 | event queue audio offset |
| Mixer `g_tracks`/filters | Z1 runtime DSP | mixer audio, UI diag | param/LFO/note gates | IRQ + superloop | M7 | commandes/snapshot params |
| Drum/Wave states | Z1 engines | audio | note events, param/LFO | IRQ + superloop | M7 | note/param event queue |
| Sampler voices/readers | Z1 sampler runtime | audio | note, param, service | IRQ + superloop | M7 | commands + page publication |
| `sample_page_cache` pages/data | Sampler/Z6 seam | audio readers, SD service | SD service, readers | IRQ + superloop | data shared, metadata split | page publication immutable + release telemetry |
| `sample_stream_manager` | Z6/Sampler service | service, runtime queue | service | superloop | M4 | page requests M7->M4, READY M4->M7 |
| `sampler_ram_pool` | Z6/Sampler RAM | audio RAM voices, UI | loaders/clear | superloop + IRQ | M4 load, M7 read snapshot | immutable slot publication |
| Looper runtime | Z1/Z6 seam | audio render/service | UI/control/audio boundary/service | IRQ + superloop | M7 playback/capture, M4 storage | record ring + page publication |
| `multi_record_writer` | Z6 writer | service/diag | audio push, UI stop | IRQ + superloop | M4 writer, M7 producer | M7->M4 ring |
| `sd_preview` | Z6 preview | audio render, service/UI | SD service/UI | IRQ + superloop | M4 decode, M7 render ring | ring publication |
| UI state | Z5 | UI/render/input | UI | superloop | M4 | telemetry only from M7 |
| Persistence/Project/Pattern | Z6 | UI/storage | UI/storage | superloop | M4 | no direct M7; applies via snapshots/commands |

Ambiguites a eliminer:

- `track_runtime_get_ctx()` retourne un pointeur interne mutable (`Src/Core/track_runtime.c:1179`); ne pas partager directement entre coeurs.
- `track_sound_state_get()` et `track_tone_sound_state_get()` retournent des pointeurs mutables (`Src/Core/track_sound_state.c:75`, `Src/Core/track_tone_sound_state.c:107`); a garder M4-only.
- Les setters mixer/engine sont appeles depuis Z3/Z4/Z5 et depuis LFO/audio; ils doivent devenir commandes M7 ou fonctions M7-only.
- Les getters stricts sont globalement purs, mais certains appels de maintenance (`track_runtime_refresh_track`, `track_runtime_refresh_all`) restent proches des queries dans les call-sites.

## 6. Contrats inter-coeurs necessaires

| Type | Producteur | Consommateur | Frequence | Donnees | Retard/plein |
|---|---|---|---|---|---|
| Commande structure track | M4 | M7 | rare, edits/restore | generation, track, family/type/midi, mix target calcule ou snapshot runtime complet | derniere commande gagne; si plein, drop ancienne non appliquee avant commit generation |
| Snapshot runtime tracks | M4 | M7 | rare a moderee | tableau `track_runtime` immutable + revision | double-buffer; M7 garde ancienne revision si nouvelle incomplete |
| Snapshot params debut bloc | M4 | M7 | moderee | valeurs MIX/COLORS/TONE/VCA/engine, MasterFX, LFO config/base par track | coalescing par param; derniere valeur gagne |
| Events audio sample-accurate | M4 ou M7 prepare | M7 apply | par bloc/horizon court | note on/off, p-lock apply/release, program, boundary, metro, offset intra-bloc ou sample absolu | si plein: drop selon priorite diagnostique; notes off/panic prioritaires |
| Commandes transport | M4 | M7 | faible | start/stop/continue, tempo, clock source, count-in, sample anchor si connu | start/stop atomiques; si plein, stop/panic prioritaire |
| MIDI out audio-aligne | M7 | M4 | par event | status/data/channel, sample_time/order | M4 peut etre en retard; horodatage USB best effort, ne bloque jamais M7 |
| Page request | M7 | M4 | selon voices | `sample_audio_key`, page_index, priority, owner/generation, deadline | si plein: M7 garde silence/underrun local; requetes coalescables |
| Page publication | M4 | M7 | selon streaming | slot/page, key, generation, frame_count, data address immutable, cache clean marker | publication double phase `LOADING -> READY`; M7 ignore generation incoherente |
| Page release/telemetry | M7 | M4 | selon voice windows | owner/generation/page release, low-water, underruns | si plein: utiliser compteur saturant; eviction conservatrice cote M4 |
| Record ring | M7 | M4 | audio bloc | PCM24/stereo frames, client, generation, start/stop markers | si plein: compteur overrun + stop/refus; jamais blocage IRQ |
| Telemetrie | M7 | M4 | 10-60 Hz | CPU max, underruns, queue high-water, audio revision, playhead snapshots | ecrasement autorise |
| Notification | M4->M7/M7->M4 | autre coeur | evenementielle | SD busy, project load done, panic, recorder finalized | flags atomiques/versionnes; pas de RPC sync |

Regles:

- aucun RPC synchrone depuis l'IRQ audio;
- aucun lock pris dans l'IRQ audio;
- aucun pointeur mutable non versionne traverse la frontiere;
- toute publication de buffer partage doit preciser ownership, generation, cache clean/invalidate et etat terminal.

## 7. Notes de preparation par zone

| Zone | Note | Justification code |
|---|---:|---|
| Z0 plateforme/cadence | 2 | superloop et tasklets sont clairs (`Src/Core/brick6_app_init.c:177`, `engine_tasklet_notify_frames` dans `Src/Audio/audio.c:397`), mais init/peripheriques restent monolithiques dans `main.c` et `brick6_app_init.c`. |
| Z1 audio | 3 | chemin IRQ/DMA net (`Src/Audio/audio.c:235`, `Src/Audio/audio_float.c:561`), Board audio abstraite, mais appels directs vers seq/MIDI/preview/track state restent a contractualiser. |
| Z2 runtime track-aware | 3 | projection explicite et getters purs (`Src/Core/track_runtime.c:926-957`, `Src/Core/track_runtime.c:1179-1360`), mais pointeurs internes et dirty flags partages ne sont pas bicore-safe. |
| Z3 param/modulation | 2 | bases canoniques separees (`track_sound_state`, `track_tone_sound_state`) et LFO direct M7-compatible, mais Z3 appelle directement mixer/engines et ses bases sont lues en audio. |
| Z4 sequenceur | 3 | separation exec/scheduler visible et offsets sample reels (`Src/Seq/seq_runtime_exec.c:644`, `Src/Seq/seq_play_scheduler.c:735`), mais preparation/apply restent dans un meme domaine global et MIDI out part de l'audio. |
| Z5 UI | 3 | UI est deja hors audio et Board surface progresse; les commandes passent par Z2/Z3/Z4/Z6. Reste a transformer les appels directs en commandes M4->M7. |
| Z6 persistence | 3 | SD/writer/cache services sont hors IRQ (`Src/Core/brick6_app_init.c:184-206`) et gates existent; la publication audio des pages/rings n'est pas encore cache-safe bicore. |
| Sampler/cache | 2 | acquire/release, generations et keys existent (`Src/Sampler/sample_page_cache.c:1033-1136`), mais metadata/refcounts/statuts sont partages et modifies par les deux domaines. |
| Looper/Recorder | 2 | bon pattern RAM ring/writer (`Inc/Storage/multi_record_writer.h:101`, `Src/Storage/multi_record_writer.c:822`), mais runtime playback/service/storage restent couples par globals. |
| Mutable Instruments | 3 | wrappers BRICK statiques (`Src/Core/brick6_braids_runtime.cpp:88`, `Src/Audio/drum_synth.cpp:32`), pas d'allocation observee dans wrappers; dependances C++/tables MI a garder M7, avec isolation platform/sections. |
| Abstraction Board | 2 | `board_audio_*`, `board_controls_*`, `board_surface_*` existent, mais generated `main.c`, clocks, IRQ et handles HAL restent single-core centralises. |

Echelle: 0 fortement couple, 1 embryonnaire, 2 frontiere visible mais fuites nombreuses, 3 largement pret avec adaptations localisees, 4 transplantable directement.

## 8. Plan de migration ordonne

### Phase 0 - Mesure H743 mono-core

- Objectif: etablir worst-case audio et files.
- Zones: `audio.c`, `cpu_load`, `seq_play_scheduler`, `sample_page_cache`, `multi_record_writer`.
- Resultat: compteurs max segments/events, duree IRQ max, underruns, queue high-water, SD contention.
- Tests: transport + p-locks + LFO + Stream/Multi + Looper record + USB MIDI + UI stress.
- Risques: instrumentation trop lourde en IRQ.
- Passage suivant: mesures stables sans regression audible.

### Phase 1 - Classer les APIs par domaine

- Objectif: marquer M7-only, M4-only, shared snapshot.
- Zones: headers Z1/Z2/Z3/Z4/Sampler/Z6.
- Resultat: liste compile-time ou doc locale des APIs interdites en IRQ/audio.
- Tests: build + grep/static checks simples.
- Risques: classification incomplete.
- Passage suivant: plus aucun nouvel appel audio vers API M4-only.

### Phase 2 - Contrats locaux sans IPC

- Objectif: introduire des structures de commandes/snapshots mono-core, consommees par appels directs.
- Zones: track runtime snapshot, param snapshot, audio event queue, page publication.
- Resultat: M7 consomme une copie immutable meme sur H743.
- Tests: comparaison sortie/etat avant-apres, stress edits pendant audio.
- Risques: duplication temporaire.
- Passage suivant: audio ne lit plus `track_state`, `track_sound_state`, `track_tone_sound_state` directement.

### Phase 3 - Sequenceur split preparation/apply

- Objectif: separer preparation des steps/p-locks et application sample-accurate.
- Zones: `seq_runtime_exec`, `seq_play_scheduler`, `seq_param_iface`.
- Resultat: events bornes `{type, track, note/param, value, offset}` disponibles par bloc.
- Tests: microtiming negatif, p-locks, note-off forced, start atomique, clock externe.
- Risques: doublons note-off, events en retard.
- Passage suivant: queue bornee avec politique plein prouvee.

### Phase 4 - Param snapshots M7

- Objectif: M4 garde canonique, M7 recoit snapshots/commandes.
- Zones: `param_registry_backends`, `mod_lfo_v1`, mixer setters, sampler setters, MasterFX.
- Resultat: LFO et engines lisent un etat M7 local.
- Tests: edits rapides, restore Kit/Patch/Project, LFO release/base, MasterFX.
- Risques: divergence UI/base/runtime.
- Passage suivant: revision param M7 observable et coherente.

### Phase 5 - Sampler/page-cache publication

- Objectif: M4 charge, M7 lit uniquement READY immutable.
- Zones: `sample_page_cache`, `sample_stream_manager`, `sample_voice_reader`, `sampler_ram_pool`.
- Resultat: metadata M4-owner, table publiee M7-owner, releases telemetry.
- Tests: Stream under load, Multi 512 page0, RAM clear pendant voix, cache miss.
- Risques: cache CPU, eviction trop agressive.
- Passage suivant: aucune mutation M4 d'une page READY lue par M7.

### Phase 6 - Recorder/Looper ring propre

- Objectif: M7 produit audio, M4 ecrit/finalise.
- Zones: `brick6_looper_runtime`, `multi_record_writer`, `sample_capture`.
- Resultat: ring SPSC cache-safe + markers start/stop/version.
- Tests: record long, stop sous SD busy, pattern load differe, export Looper.
- Risques: overrun ring, perte marker stop.
- Passage suivant: writer n'a plus besoin de lire un etat runtime M7 mutable.

### Phase 7 - Tests desktop ciblables

- Objectif: tester moteurs sans HAL.
- Zones: mixer, seq event apply, sampler reader, LFO, Braids/Drum wrappers si possible.
- Resultat: harness C/C++ host pour events blocs, params, page ready/miss.
- Tests: CI/local desktop, golden counters.
- Risques: divergences float/flags ARM.
- Passage suivant: scenarios critiques couverts hors cible.

### Phase 8 - Boot minimal CM7/CM4

- Objectif: creer deux firmwares minimaux sans deplacer toute la logique.
- Zones: Board/generated, linker, clocks, HSEM/boot, MPU/cache.
- Resultat: CM7 audio silence + CM4 heartbeat/UI minimal ou inverse selon board.
- Tests: boot order, clocks, SDRAM init unique, cache coherency scratch.
- Risques: double init peripherique, cache SDRAM.
- Passage suivant: deux coeurs stables avec shared memory test.

### Phase 9 - Deplacement progressif services M4

- Objectif: basculer UI/MIDI/SD/writer sur M4 derriere contrats locaux deja existants.
- Zones: Z5, Z6, MIDI, Board USB/SD/display/controls.
- Resultat: M7 audio autonome consomme snapshots/events/pages.
- Tests: charge SD + USB + UI pendant audio.
- Risques: latence commandes, files pleines.
- Passage suivant: M7 ne depend plus d'appel direct FatFs/USB/UI.

### Phase 10 - Validation complete sous charge

- Objectif: valider produit.
- Zones: toutes.
- Resultat: profil worst-case avec SD streaming, USB MIDI, UI OLED/LED, record, project load refused/deferred.
- Tests: soak, transport start/stop, clock externe, multi record, sampler streaming, Kit/Patch.
- Risques: contention SDRAM/FMC, D-cache bugs, pertes events.
- Passage suivant: criteres audio stables et diagnostics propres.

## 9. Risques techniques principaux

- Coherence cache CM7/CM4 sur SDRAM/FMC: pages sample, record rings, DMA buffers, preview rings et metadata doivent avoir des regions MPU ou maintenance explicite.
- Contention SDRAM/FMC: M7 audio lit pages/FX histories pendant M4 SD/cache/UI; mesurer worst-case, pas seulement charge moyenne.
- Events sample-accurate en retard: une queue M4->M7 pleine ne doit jamais bloquer l'IRQ; il faut une politique de drop/priority.
- Divergence param UI/runtime: snapshots coalesces peuvent masquer des transitions structurelles; chaque snapshot doit porter generation track/runtime.
- Pointeurs directs: `get_ctx`, `track_sound_state_get`, `sample_page_cache_get_page_desc` ne doivent pas traverser la frontiere.
- MIDI out depuis audio: a convertir en telemetry/event out, sinon M7 depend d'un service M4.
- Boot/peripheriques: SDRAM, clocks, USB, SDMMC, SAI et DMA ne peuvent pas etre initialises deux fois.
- Mutable/C++: wrappers sont statiques et portables, mais les sources MI compilees via globs CMake doivent etre isolees dans le firmware M7 et leurs sections memoire explicitees.

## 10. Premiere etape concrete recommandee

Premiere passe utile: sur H743 mono-core, introduire une "frontiere audio locale" sans IPC reel:

1. definir un snapshot M7 local pour `track_runtime` + params audio par generation;
2. remplacer dans le chemin audio les lectures directes de `track_sound_state` / `track_tone_sound_state` par ce snapshot;
3. transformer les param/track writes superloop en publication de snapshot, consommee au debut de bloc audio;
4. ajouter diagnostics: revision appliquee, snapshots rates, queue high-water, dirty observe en IRQ.

Cette passe garde le MCU actuel, evite le cumul refactor + dual-core, et force les bons contrats avant toute creation CM7/CM4.

## Addendum 2026-07-18 - phase commandes controle -> audio H743

- Une file locale bornee `audio_control_command` est ajoutee sur H743 mono-coeur pour transformer les writes controle vers les principaux runtimes audio en commandes consommees a la frontiere de bloc audio.
- Migré dans cette phase: setters mixer track/global les plus utilises, FILTER/VCA via Z3, params moteurs Wave/Drum/Sampler/Looper, XFade Looper, notes clavier hors sample-accurate, rebind/snap/neutralisation mixer de transition structurelle.
- Non migre volontairement: events PLAY sample-accurate, LFO audio-owned, etats DSP internes, page-cache, scheduler complet, reset/storage sampler/pool et certaines commandes UI settings directes encore ambigues.
- Politique plein: coalescable = derniere valeur gagne puis rejet diagnostique si tous les slots sont pleins; imperatif = ordre conserve, jamais drop silencieux, echec compte dans `critical_failures`.
## Addendum 2026-07-18 - phase bloc events Z4 et MIDI out

- Z4 ajoute un contrat local de bloc prepare `seq_runtime_audio_event_block_t`: buffer statique borne, generation, debut/longueur de bloc, offsets intra-bloc et donnees d'application. Sur H743 mono-coeur, la collecte reste locale et sans IPC reel.
- La separation obtenue est partielle mais explicite: la preparation logique step/p-lock/boundary reste dans `seq_boundary_engine`/`seq_play_scheduler`; l'avance timeline et l'application sample-accurate restent audio-owned dans `seq_runtime_exec` + `audio.c`.
- Les appels MIDI/USB non hard-RT sortent du chemin audio Z4: note-on/off, Program, MIDI clock, transport realtime et panic publient vers `audio_midi_out`, file M7->controle locale bornee, drainee dans la superloop.
- Saturation MIDI out: note-off, stop, panic et all-notes-off sont critiques; un critique peut remplacer un evenement normal, sinon l'echec critique est diagnostique. Aucune attente ni croissance non bornee dans l'IRQ.
- Diagnostics ajoutes: events prepares par bloc, maximum observe, blocs satures/echecs critiques, cout max collecte/application, profondeur/high-water/rejets/echecs critiques de la sortie MIDI out.
## Addendum 2026-07-18 - phase actions PLAY preparees

- La frontiere locale Z4 progresse: les notes PLAY et Program Change sont preparees hors IRQ depuis `seq_model`/p-locks PLAY dans un lot double-buffer statique, puis consommees par l'audio via generation active.
- Le scheduler audio ne parcourt plus les steps ni les p-locks PLAY pour construire les actions musicales; il garde l'autorite sample-domain: placement exact, microtiming positif/negatif, quant, note-off, tokens anti double-off, queue due-sample et apply engine.
- Le format prepare est volontairement plus large que le peuplement actuel: types `NOTE`, `PROGRAM_CHANGE`, `PLOCK_APPLY`, `PLOCK_RELEASE`, `CONTROL`, sans pointeurs mutables et avec track/type/valeurs explicites. Les p-locks non-PLAY restent dans `seq_boundary_engine` tant que leur capture base/release exige l'etat runtime exact au boundary.
- Le lot est place en `CONTROL_STATE_SDRAM`; l'ancien buffer reste actif jusqu'au publish court, ce qui evite une lecture partielle par l'IRQ. En cible dual-core, cette section devra recevoir le protocole cache/MPU de shared memory.
- Saturation/diagnostics: capacite fixe par step, rejets normaux/critique, lots publies/consommes/manquants/stale, maximum d'actions preparees, cout max de preparation et de placement.

## Addendum 2026-07-18 - phase page-cache Sampler publie

- Le premier contrat local sampler/page-cache est en place sur H743 mono-coeur: `sample_voice_reader` consomme une projection `sample_audio_page_t` versionnee au lieu d'utiliser directement `sample_page_desc_t` pour le chemin segment cursor Stream/Multi/Classic migre.
- Ownership obtenu: le systeme possede allocation, chargement, etats `QUEUED/LOADING/ERROR`, pins/window locks/LRU et publication READY; l'audio acquiert seulement des pages READY immutables par cle/page/generation et release un lease local borne.
- Les metadata systeme `use_count` et `last_touch` ne sont plus modifiees par le fast path migre. Les anciens refcounts restent pour les chemins non migres et pour la compat `sample_cache`.
- Publication: page invisible pendant `LOADING`; publication READY avec hook local de coherence cache; ancienne generation acquise non reutilisee avant release; generation stale rejetee.
- Saturation/miss: acquire refuse si lease plein ou page non READY; le rendu conserve silence/underrun/fallback sans blocage ni attente SD. L'eviction garde une page avec lease audio plutot que de la reutiliser.
- Acces directs restant a migrer: `sample_cache` legacy Classic et ses spans/blocs, Looper RAW playback, RAM resident `sampler_ram_pool` (deja immutable par slot/generation mais hors handle page-cache), overview waveform hors IRQ et APIs de diagnostic `sample_page_cache_get_page_desc`.
- Readiness sampler/cache passe de 2 a 3 pour le chemin reader migre, mais reste non transplantable tel quel: les hooks clean/invalidate/barrier sont encore no-op locaux et aucune coherence CM7/CM4 reelle n'est traitee.

## Addendum 2026-07-18 - phase Classic legacy audio sur projection

- Les chemins Classic audio restants dans `sample_cache` ne lisent plus les descripteurs mutables du page-cache: voix cursor, blocs, spans, read-frame et peek-frame consomment maintenant `sample_audio_page_t` et releasent par handle versionne.
- La couche d'autorite est clarifiee: `sample_page_cache` reste l'autorite physique READY/slot/generation/eviction; `sample_stream_manager` reste l'unique service SD STREAM; `brick6_sampler_runtime` et `sample_cache` ne gardent que l'etat voix/lecture et les wrappers Classic.
- Les spans multi-pages restent bornes a deux leases dans les chemins couverts: page courante puis voisin necessaire, ordre deterministe, rollback si le voisin manque, pas de lecture partielle declaree complete.
- Diagnostics ajoutes pour la suite dual-core: spans Classic OK/refuses/rollback, max pages par operation, misses, appels legacy observes en IRQ, acquire/release/stale via le diagnostic page-cache.
- Restent hors migration: Looper RAW playback, `sampler_ram_pool` resident, overview waveform, loaders/diagnostics hors IRQ et `sample_cache_get_legacy_data()` utilise par la compat pool hors audio.
## Addendum 2026-07-18 - phase Looper RAW playback sur projection

- Le playback RAW Looper est migre vers la meme frontiere locale que Classic/Stream/Multi: l'audio acquiert uniquement des pages READY immutables par cle `LOOPER`, slot et generation, puis release explicitement le lease versionne.
- Le domaine systeme garde les reservoirs RAW, le writer, la finalisation, les metadonnees et les demandes de prefetch/refill. Le rendu audio ne demande plus de pages et ne touche plus aux compteurs mutables `use_count`/`last_touch` du page-cache.
- La generation de contenu Looper est incrementee a chaque nouvelle prise, clear/replace ou re-record; la publication normale est refusee tant que le RAW finalise et les metadonnees minimales ne sont pas coherents. Le preroll RAM reste une projection locale transitoire pour conserver le demarrage Auto existant pendant finalisation.
- Diagnostics Looper ajoutes: generations publiees/appliquees, acquire/release, stale generation, page misses, publication incomplete rejetee, max pages acquises par rendu et placeholders explicites pour transitions refusees, pages gardees par lease actif et acces legacy IRQ.
- Readiness Looper/Recorder passe de 2 a 3 pour le playback RAW. Restent non transplantables directement: ring writer cache-safe CM7/CM4 reel, preroll RAM shared-memory, maintenance cache/barrieres bicore et service SD M4 effectif.

## Addendum 2026-07-18 - phase Sampler/RAM resident sur projection

- `sampler_ram_pool` expose une projection audio locale `sampler_ram_audio_sample_t`, construite uniquement quand le slot RAM est READY et complet. Le rendu `Sampler/RAM` normal et sliced ne lit plus `sampler_ram_slot_t` mutable.
- Le domaine systeme garde import, allocation `SAMPLE_PAGE_SLOT_POOL`, conversion, registration `sample_global_pool`, overview, clear/reload et erreurs. Le domaine audio acquiert une generation immutable au trigger et release a l'arret, au steal, au reset track ou a la fin de lecture.
- Le caractere resident/pinne permet un lease borne par voix plutot qu'un acquire par bloc: les anciennes generations sont retirees et gardees hors free-list tant que leur compteur audio n'est pas revenu a zero. Aucun lock, malloc, attente ou release physique n'est requis en IRQ.
- Diagnostics ajoutes: generations publiees, acquire/release OK/refus/stale, clear deferres, allocations conservees pour voix actives, publications incompletes refusees, maximum d'utilisateurs audio et acces legacy IRQ via `sampler_ram_pool_get_data()`.
- Readiness Sampler/cache progresse pour RAM resident: Classic/Stream/Multi, Looper RAW et RAM resident ont maintenant des projections audio locales. Restent non transplantables directement: coherence D-cache CM7/CM4 reelle, table de retiree partagee cache-safe et ownership des overviews hors audio.

## Addendum 2026-07-18 - phase recorder/Looper ring audio -> systeme

- `multi_record_writer` separe maintenant la production audio de l'ownership fichier: l'audio publie frames et markers START/STOP/ABORT versionnes, le systeme consomme, ecrit, draine et finalise.
- Le chemin IRQ ne lit plus l'etat mutable writer pour autoriser la capture Looper; il s'appuie sur l'etat boundary Looper local et sur une projection writer minimale `{generation, client, backend, active, accepts, frame_limit}`.
- START/STOP Looper au boundary ne mutent plus directement le writer quand l'appel vient de l'IRQ: ils deviennent des markers critiques SPSC. Le service consomme les markers avant le drain afin de garder les premiers blocs de la session.
- Saturation: l'IRQ ne bloque pas. `SAMPLE_WAV` coupe l'acceptation et demande STOP pour finaliser degrade; `LOOPER_RAW` coupe la session et demande ABORT pour eviter une boucle RAW partielle non signalee. Les echecs de markers critiques et les sessions abandonnees sont diagnostiques.
- Les seams mono-coeur pour future coherence memoire sont explicites (`publish producer`, `acquire consumer`, `release consumer`, barrieres locales), sans IPC materiel ni projet CM7/CM4.
- Readiness Looper/Recorder passe a 3 pour le flux record: le fichier/finalisation sont system-owned et l'audio ne modifie plus que les rings, la projection producteur et les compteurs producteurs. Restent non transplantables directement: cache/MPU CM7/CM4 reels et placement shared-memory definitif.## Addendum 2026-07-19 - audit acces directs systeme/audio restants

- Supprimes dans cette passe: panic/all-notes-off Z4 vers engines et `midi_*`, reset Wave instance depuis Z2, reset Sampler Kit/Patch/Project, set/stop/gain Multi depuis UI/storage, reset global Project blank, queries UI/Z3 vers Multi runtime et query UI vers playhead RAM mutable.
- Telemetry ajoutee: `audio_telemetry` publie depuis l'audio le playhead RAM utile UI avec generation et compteurs published/overwritten; publication ecrasable autorisee pour cette donnee non critique.
- Commandes critiques ajoutees ou reutilisees: `AUDIO_CONTROL_WAVE_RESET_INSTANCE`, `AUDIO_CONTROL_COMMAND_AUDIO_RUNTIME_RESET_ALL`, stop clips transport, VCA/Wave/Drum all-notes-off, reset track Sampler, set/stop/gain Multi.
- API legacy traitees: `brick6_sampler_runtime_get_ram_playhead`, `brick6_sampler_runtime_get_multi_instrument`, `brick6_sampler_runtime_get_multi_gain`, `brick6_sampler_runtime_set_multi_instrument`, `brick6_sampler_runtime_stop_multi_instrument`, `brick6_sampler_runtime_reset_track`, `brick6_braids_runtime_reset_instance` n'ont plus de call-site systeme actif important; leurs definitions restent pour le domaine audio et compat.
- Restants legitimes audio-owned: executant `audio_control_command` vers mixer/engines/sampler/looper, DSP internes, voix, phases, filtres, enveloppes, LFO audio, mixer interne, timeline sample, compteurs audio, et `brick6_audio_runtime` lors du toggle runtime audio.
- Restants init/control-owned: `audio_control_snapshot_build()` lit `track_runtime_get_revision()` et les etats controle pour publier le snapshot; les submitters de commande capturent la revision runtime controle pour stale generation.
- Restants legacy hors chemin actif ou diagnostic: declarations publiques des anciennes APIs runtime, `sample_cache_get_legacy_data`, overviews/waveforms, loaders, diagnostics page-cache/RAM et protections d'appel IRQ.
- A reporter au vrai CM7/CM4: coherence D-cache/MPU/barrieres reelles, placement shared-memory des rings/snapshots/telemetry, boot peripheriques unifie, et politiques IPC pour saturation inter-coeurs.
## Addendum 2026-07-20 - split Z0 en domaines mono-coeur

- Z0 passe de l'orchestrateur indistinct `brick6_app_init()`/`main()` a des entry points explicites: shared platform, domaine audio init/start, domaine systeme init/start/process. Les wrappers historiques restent pour compatibilite mono-coeur.
- Ordre audite et implemente: `main()` garde MPU/cache, HAL, clocks, peripheriques CubeMX, TIM5/TIM12 et FatFs; `brick6_app_init()` enchaine SDRAM shared, init audio sans DMA, init systeme/storage/controle, publication + force-apply du snapshot initial, demarrage DMA audio, puis LED/MIDI systeme.
- Le demarrage DMA audio est maintenant un point unique retardable (`brick6_audio_domain_start()`), appele uniquement apres `audio_control_snapshot_publish_from_control()` et `audio_control_snapshot_force_apply_latest()`.
- La superloop est systeme-owned: services engine/seq/storage/UI/Hall/MIDI/audio-metadata, USB host Board et poll MIDI host borne vivent dans `brick6_system_domain_process()`. Z1 ne gagne aucun service cooperatif nouveau.
- Ownership futur documente: shared unique = power/clocks/MPU/cache/FMC-SDRAM; M7 = SAI/I2S, DMA audio, codec, mixer/engines/audio runtime; M4 = USB/MIDI, SD/FatFs, UI/OLED/LED/Hall/controls, loaders/cache/writer/persistence. TIM5/TIM12 restent a classer finement avec le split clock final.
- Dependances restantes non corrigees volontairement: les `main.c` generes initialisent encore tous les peripheriques dans un seul firmware; FatFs reste initialise cote generated avant l'init storage; les handles/IRQ CubeMX restent centralises; pas de memoire partagee, cache protocol inter-coeur, HSEM ou projet CM7/CM4 introduit.
- Readiness Z0 passe de 2 a 3: la frontiere de boot et de service est visible et utilisable pour le futur deplacement, mais la transplantation directe attend encore le vrai decoupage CubeMX/peripheriques et la policy shared-memory.