# Z0 - Plateforme / Cadence

## 1. Perimetre

Perimetre operationnel de zone (appartient a Z0):
- `Src/main.c`
- `Src/Core/brick6_app_init.c`
- `Inc/Core/brick6_app_init.h`

Elargissements necessaires (preuve de cadence et points periodiques):
- `Src/Core/engine_tasklet.c` + `Inc/Core/engine_tasklet.h`: base de cadence non-RTOS derivee des frames audio.
- `Src/UI/ui_tasklet.c` + `Inc/UI/ui_tasklet.h`: init lazy UI et tick UI dans superloop.
- `Src/UI/display_flush_service.c`: service periodique display (16 ms) appele en superloop.
- `Src/MIDI/midi.c`: callback IRQ TIM12 et callback TIM5 utilises par la cadence globale.
- `Src/stm32h7xx_it.c`: branchement IRQ TIM12/TIM5 vers HAL, plus service `PendSV` pour flush TX USB MIDI differe.
- `Src/tim.c`: configuration frequence TIM12 (1500 Hz) et activation IRQ associee.
- `Src/Core/brick6_app_init.c`: service superloop preview SD (`sd_preview_process()`) hors IRQ.
- `Src/Core/brick6_app_init.c`: init et service cooperatif du squelette `multi_record_writer`, hors IRQ et sans client actif par defaut.
- `Src/Core/brick6_app_init.c`: validation boot des reservoirs RAW systeme Looper via `looper_storage_raw_validate()`, hors IRQ et sans creation de fichier.

Sous-roles internes dans `brick6_app_init.c`:
- Orchestrateur boot produit: initialisation ordered des sous-systemes applicatifs.
- Wiring inter-zones: enregistrement callback audio DSP et chaines runtime.
- Service loop applicative: aggregation des services hors IRQ audio.

Dependances de Z0 sans appartenance:
- HAL/CubeMX (`MX_*`, `HAL_*`, clocks, peripherals).
- Z1 audio (`audio_*`, `brick6_audio_runtime_*`, mixer/synth).
- Z2/Z3/Z4/Z5/Z6 via appels d'init/service delegues.
- USB host/device et MIDI host.

Exclusions explicites:
- Tout pipeline audio IRQ (`Src/Audio/audio.c` callbacks DMA) appartient a Z1, pas a Z0.
- Logiques metier UI/Seq/Param/Storage ne font pas partie de Z0, elles sont seulement orchestrees.
- Drivers CubeMX individuels (`adc.c`, `sai.c`, etc.) exclus du perimetre de gouvernance Z0.

## 2. Autorite(s) de verite

Autorite boot principal:
- `main()` dans `Src/main.c` est le point d'entree unique observe.

Autorite init systeme MCU/HAL:
- Dans `main()`:
  - `MPU_Config()` (region DMA non-cacheable),
  - `SCB_EnableICache()`, `SCB_EnableDCache()`,
  - `HAL_Init()`,
  - `SystemClock_Config()`, `PeriphCommonClock_Config()`,
  - puis `MX_*_Init()`.

Autorite init sous-systemes projet:
- `brick6_app_init()` dans `Src/Core/brick6_app_init.c`.

Autorite wiring global inter-zones:
- `brick6_app_init()`:
  - `audio_init(&hsai_BlockA2, &hsai_BlockB2)`
  - `audio_set_float_callback(brick6_audio_runtime_dsp)`
  - ordre d'init runtime (drum/sampler/Opal/Braids, puis param/seq/storage/undo/control/hall/etc.).
- `brick6_audio_runtime_dsp()`:
  - point d'injection MAIN pour la preview SD via le buffer de lecture pre-resample.

Autorite cadence/service loop hors audio IRQ:
- `while(1)` dans `main()` + `brick6_app_process()`.
- `engine_tasklet_poll()` est l'autorite de consommation de la cadence derivee audio cote non-IRQ.

Tasklets/timers periodiques observes:
- TIM12 IRQ -> `HAL_TIM_PeriodElapsedCallback` (dans `midi.c`) -> `seq_runtime_time_adapter_process_internal_from_irq()`.
- TIM5 OC IRQ -> `HAL_TIM_OC_DelayElapsedCallback` -> `midi_clock_on_timer_tick()`.
- `engine_tasklet_notify_frames()` (alimente depuis IRQ audio Z1) -> `engine_tasklet_poll()` en superloop.
- `PendSV` -> `midi_usb_tx_deferred_service_from_isr()` pour lancer un premier flush TX USB MIDI hors superloop apres enqueue ISR.

Seconde autorite concurrente:
- Aucune seconde autorite concurrente complete pour la sequence boot/system init/superloop.
- Cadence seq a deux chemins mais avec ownership exclusif par source clock:
  - interne: Z1 audio bloc (`seq_runtime_audio_collect_block_events` -> drive internal steps).
  - externe: superloop recoit les pulses MIDI puis les met en pending; la consommation d'avance step reste en Z1 audio bloc (`seq_runtime_audio_collect_block_events`).
  TIM12 reste un ticker auxiliaire interne, non autorite d'avance step.

## 3. API entrantes

Qui appelle Z0:
- Boot CPU/reset appelle `main()`.
- Superloop appelle `brick6_app_process()` depuis `main()`.
- HAL IRQ appelle:
  - `TIM8_BRK_TIM12_IRQHandler()` -> `HAL_TIM_IRQHandler(&htim12)` -> `HAL_TIM_PeriodElapsedCallback`.
  - `TIM5_IRQHandler()` -> `HAL_TIM_IRQHandler(&htim5)` -> `HAL_TIM_OC_DelayElapsedCallback`.

Contrats implicites d'ordre:
- MPU region DMA doit etre configuree avant activation cache CPU (verifie dans `main()`).
- Horloges/system clocks avant `MX_*_Init()`.
- `audio_set_float_callback()` doit etre pose avant `audio_start()`.
- `engine_tasklet_init()` doit preceder l'usage de `engine_tick_count` pour UI cadence.
- `ui_tasklet_poll()` initialise UI au premier tick engine, pas pendant boot `brick6_app_init()`.

## 4. API sortantes

Z0 appelle principalement:
- System/HAL/Cube:
  - `HAL_Init`, clocks config, `MX_*_Init`, timers start (`HAL_TIM_Base_Start`, `HAL_TIM_OC_Start`, `HAL_TIM_Base_Start_IT`).
- Boot produit via `brick6_app_init()`:
  - init audio runtime, synths, sampler, sequencer, storage, undo, controls, hall, MIDI.
- Runtime via `brick6_app_process()`:
  - tasklets/services metier (`engine_tasklet_poll`, `seq_runtime_time_adapter_process`, `pattern_live_service`, `hall_loop_process`, `voice_manager_service`, etc.).
- Boucle `main()` appelle en plus:
  - `MX_USB_HOST_Process`, `usb_host_tasklet_poll_bounded(4)`, `midi_host_poll_bounded(8)`,
  - `ui_tasklet_poll` conditionne par `engine_tick_count`,
  - `ui_renderer_oled_service_poll`, `display_flush_service_poll` apres init UI.

## 5. Etats structurants possedes

### `Src/main.c`
- `last_tick` (`uint32_t`, local `main`):
  - Ecriture: dans la boucle quand `engine_tick_count` change.
  - Lecture: compare avec `engine_tick_count`.
  - Role: cadence de `ui_tasklet_poll` (1 appel par tick engine detecte).

### `Src/Core/brick6_app_init.c`
- `g_live_recorder_buffer[LIVE_RECORDER_MAX_FRAMES * 2]` (AUDIO_COLD_SDRAM float array):
  - Ecriture/lecture: modules recorder/master_buffer/audio runtime apres init.
  - Role: buffer global de recorder/master buffer branche au boot.
- `g_sample_pool_data[...]` (SDRAM_SAMPLES float array):
  - Ecriture: `sample_pool` lors du chargement WAV ou du restore projet.
  - Role: arena residante des samples projet, dimensionnee pour absorber le reste disponible de la SDRAM apres les reserves fixes; le boot n'injecte plus de sample par defaut.
- `g_rec_ring_storage[...]` + `g_live_recorder_buffer[...]` (SDRAM_RECORDER float arrays):
  - Ecriture/lecture: recorder/master buffer.
  - Role: historique/loop recorder partage.
- `g_live_recorder` (`live_recorder_t`):
  - Ecriture: `brick6_recorder_runtime_boot_init`.
  - Lecture/mutation: runtime recorder et audio runtime.
  - Role: instance recorder partagee.

### `Src/Core/engine_tasklet.c` (cadence non-RTOS rattachee Z0)
- `volatile uint32_t engine_tick_count`:
  - Ecriture: `engine_tick()` dans `engine_tasklet_poll`.
  - Lecture: `main()` (cadence UI), `seq_runtime`, recorder transport, autres modules.
  - Role: horloge logique derivee audio cote superloop.
- `static volatile uint32_t engine_frames_accum`:
  - Ecriture: IRQ audio via `engine_tasklet_notify_frames`.
  - Lecture/ecriture: superloop via `engine_tasklet_poll`.
  - Role: passerelle IRQ->main loop pour ticks.
- `engine_frames_per_tick` (32), `engine_last_poll_ms`:
  - Role: quantification et dt_ms des ticks.

### `Src/UI/ui_tasklet.c` (cadence UI rattachee Z0)
- `g_ui_tasklet_init`:
  - Ecriture: premier `ui_tasklet_poll()`.
  - Lecture: `ui_tasklet_poll`, `ui_tasklet_is_initialized`.
  - Role: init lazy display+ui_core et gate des services render/flush.

## 6. Flux runtime

1. Reset / entry:
- CPU entre dans `main()`.

2. Init platforme MCU/HAL:
- Verification bornes region DMA D2.
- `MPU_Config()` (region non-cacheable DMA), activation I/D cache.
- `HAL_Init()`, clocks system/periph.
- Initialisation peripheriques `MX_*` (GPIO, DMA, USART, FMC, SDMMC, SPI, I2C, ADC, SAI, TIM...).

3. Lancement services bas niveau:
- Start timers dans `main()`:
  - TIM5 base + OC ch1,
  - TIM12 base IT,
  - FATFS init.

4. Init sous-systemes projet:
- `brick6_app_init()`:
  - SDRAM, USB device, codec,
  - mixer/fx policy,
  - audio float tracks,
  - gate SD,
  - validation reservoirs RAW Looper systeme,
  - sampler/recorder/master buffer,
  - synths/hall bridge,
  - runtime audio + wiring callback DSP,
  - engine_tasklet/param defaults,
  - seq runtime,
  - pattern/project/boot-context/undo,
  - control events + hall loop,
  - politique UI boot explicite:
    - calibration hall invalide/absente -> page `CALIBRATION`,
    - calibration hall valide -> page `CFG` (`UI_PAGE_TEMPLATE_CFG`),
  - start audio,
  - delay 200 ms,
  - reset peak CPU,
  - init MIDI.

5. Runtime continu superloop:
- Dans `main while(1)` ordre observe:
  1) `brick6_app_process()`
  2) `MX_USB_HOST_Process()`
  3) `usb_host_tasklet_poll_bounded(4)`
  4) `midi_host_poll_bounded(8)`
  5) si tick engine avance: `ui_tasklet_poll()`
  6) si UI init: `ui_renderer_oled_service_poll()` puis `display_flush_service_poll()`.

6. Runtime continu `brick6_app_process()` ordre observe:
- `engine_tasklet_poll()`
- `seq_runtime_time_adapter_process()`
- `sample_cache_service(32768U)`
- `multi_record_writer_service(16384U)`
- `pattern_load_service(4096U)`
- `pattern_live_service()`
- `sd_preview_process()`
- `brick6_master_control_process()`
- `hall_loop_process()`
- `ui_core_service_track_selection_inputs()`
- `hall_keyboard_bridge_process()`
- `brick6_recorder_runtime_process_transport()`
- `voice_manager_service()`
- `midi_poll()`

7. Points critiques periodiques hors superloop direct:
- IRQ TIM12 appele en parallele de la superloop (adaptateur temps seq depuis IRQ).
- IRQ TIM5 OC pour clock MIDI.
- IRQ audio (Z1) alimente `engine_tasklet_notify_frames`; superloop consomme ensuite.
- IRQ audio (Z1) porte aussi la progression step du sequencer (interne + consommation pulses externes) en domaine sample.
- `PendSV` sert de contexte differe basse priorite pour demarrer le TX USB MIDI quand des messages sont enfiles depuis ISR.

## 7. Contraintes RT/CPU/memoire

- Pas de RTOS observe: ordonnanceur cooperatif superloop + IRQ HAL.
- Separation hard-RT audio vs services non-IRQ:
  - audio IRQ traite pipeline hard-RT,
  - Z0 orchestre services bornes/bounded hors IRQ (USB host poll bounded, services SD hors IRQ).
- Dependance forte a l'ordre d'init et au hardware clock/timer configure.
- `engine_tasklet_poll()` utilise section critique IRQ courte pour transfert frames->ticks.
- Buffers globaux statiques (`g_live_recorder_buffer`, `g_live_recorder`), pas de malloc dans Z0 observe.

## 8. Invariants a ne pas casser

- Point d'entree boot unique: `main()`.
- Ordre impose:
  - MPU/cache/HAL/clocks avant peripheriques,
  - peripheriques avant `brick6_app_init`,
  - callback DSP enregistre avant `audio_start`.
- Absence de RTOS: cadence basee sur superloop + IRQ.
- Separation init vs runtime continu:
  - init dans `main` + `brick6_app_init`,
  - services periodiques dans `main while` + `brick6_app_process`.
- Cadence UI alignee sur `engine_tick_count` (pas sur boucle brute).
- Cadence seq split stricte:
  - interne: drive step en domaine audio bloc (Z1),
  - externe: pulses MIDI recueillies hors IRQ audio puis consommees en domaine audio bloc,
  - TIM12: ticker auxiliaire interne uniquement.
- Service MIDI host autoritatif unique: `midi_host_poll_bounded(8)` appele dans `main()`; `midi_host_poll()` reste un wrapper API sans second scheduler.

## 9. Dependances inter-zones

- Vers Z1: wiring callback DSP (`audio_set_float_callback(brick6_audio_runtime_dsp)`), start audio, source de ticks via IRQ audio->engine tasklet.
- Vers Z4: init seq runtime + service superloop transport/bridge; l'avance step (interne/externe) est consommee cote Z1 audio bloc.
- Vers Z6: init pattern/project/undo, service `multi_record_writer_service` et appel `pattern_live_service` en runtime.
- Vers Z5: init/tick UI via `ui_tasklet_poll`, service selection inputs dans app process.
- Vers Z3/Z2: init param defaults et effets indirects via init/runtime des autres zones.

## 10. Dette technique observee

- Centralisation elevee dans `brick6_app_init()` (wiring de nombreuses zones dans une seule fonction).
- Dependances d'ordre implicites fortes (pas de garde explicite de prerequis inter-modules).
- Service MIDI host autoritatif unique en superloop `main()`:
  - `midi_host_poll_bounded(8)` appele une fois par boucle,
  - `midi_host_poll()` reste un wrapper API de `midi_host_poll_bounded(8)` (budget effectif inchange).
- Contrat split seq mis a jour:
  - progression step interne en domaine audio bloc (deterministe sample),
  - progression step externe consommee en domaine audio bloc a partir de pulses MIDI pending,
  - TIM12 conserve un role de ticker auxiliaire.
- API `brick6_app_get_stats()` retiree de `brick6_app_init.h` (reliquat sans call site ni implementation).
- Reliquat supprime et contrat fige: aucune API stats Z0 exposee tant qu'une autorite de metriques n'est pas definie.
- Melange plateforme et logique produit dans `main.c`/`brick6_app_init.c` (timers HAL, USB host loop, UI cadence, orchestration metier).

## 11. Impact eventuel sur la cartographie globale

- Z0 est confirme comme zone d'orchestration (boot + wiring + cadence non-RTOS), pas zone de logique metier.
- Frontiere Z0/Z1 reste nette: Z0 demarre et cadence, Z1 execute hard-RT audio IRQ.
- Z0 met en evidence une sous-frontiere "cadence" (engine tasklet + timers TIM12/TIM5 + superloop gating UI) utile pour maintenance future.

## 12. Addendum - ordre de service SD recording produit

- Z0 porte uniquement l'ordre de service cooperatif hors IRQ; il ne devient pas l'autorite SD metier.
- Implementation courante: `brick6_app_init()` appelle `multi_record_writer_init()`; `brick6_app_process()` appelle `multi_record_writer_service(16384U)` juste apres `sample_cache_service(32768U)`, puis `brick6_looper_runtime_service(8192U)`, puis `looper_storage_raw_export_service(8192U)` seulement si le refill Looper n'a pas de travail SD pending, avant `pattern_load_service(4096U)`.
- Ordre cible pour la cohabitation SD audio:
  1. `sample_cache_service(...)` prioritaire pour playback sample streaming.
  2. `multi_record_writer_service(...)` pour drainer les rings record, priorite par watermark.
  3. `brick6_looper_runtime_service(...)` pour refill transient RAW/WAV.
  4. `looper_storage_raw_export_service(...)` opportuniste pour SAVE RAW -> WAV, apres les refills sample/Looper.
  5. `pattern_save_service(...)` opportuniste, seulement si les rings record ne sont pas critiques.
  6. `pattern_live_service()` / apply pattern uniquement apres que les preconditions Z6/Z4 soient satisfaites.
- Les operations project save/load, preset load, preview SD, scan/import restent refusees ou differees pendant active recording/finalizing.
- Le service writer global doit rester hors IRQ et budgete; aucune attente longue ne doit etre deplacee dans Z1.
- Dimensionnement Looper record produit: le ring writer reste a 4 s utiles par client a 48 kHz stereo `int32_t` (`192001` frames allouees, une frame sentinel), soit environ 1.536 MiB par client et 6.144 MiB pour les 4 clients statiques. Le budget writer de 16 KiB par service conserve `sample_cache_service(32768U)` prioritaire et limite la possession du gate SD a une tranche courte; le writer execute au plus un `f_write` audio par passage et abandonne son passage si le sample cache expose du travail SD pending. Les prises Looper utilisent le reservoir RAW systeme sans preallocation de prise intermediaire; les prises LEN fixe conservent seulement la borne dure `expected_frames` / `frame_limit`.

