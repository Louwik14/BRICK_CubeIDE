# Audit mémoire interne — DTCM / RAM_D1 / RAM_D2 / RAM_D3

## 1. Rôle du document

Ce document regroupe les observations des audits ciblés réalisés sur les RAM internes STM32H743 du projet BRICK6 :

- `DTCMRAM`
- `RAM_D1`
- `RAM_D2`
- `RAM_D3`

Il consolide :

- les régions et sections linker ;
- les macros de placement mémoire ;
- les symboles observés dans les map/ELF disponibles ;
- les rôles runtime réels ;
- les contraintes DMA / cache / IRQ audio ;
- les possibilités de déplacement vers SDRAM, D1, D2, D3 ou DTCM ;
- les gains théoriques et réalistes ;
- les questions restantes pour une dissection mémoire complète.

Ce document est un état d'audit. Il ne prescrit aucun déplacement automatique et ne remplace pas la preuve par profiling/audio worst-case.

---

## 2. Source observée et écart avec l'état link récent

Les chiffres de référence fournis pour l'état link récent étaient :

| Région | État link récent fourni |
|---|---:|
| DTCMRAM | 123 776 B / 128 KiB = 94,43% |
| RAM_D1 | 499 296 B / 512 KiB = 95,23% |
| RAM_D2 | 284 128 B / 288 KiB = 96,34% |
| RAM_D3 | 14 240 B / 64 KiB = 21,73% |
| SDRAM | 26 046 144 B / 32 MiB = 77,62% |
| ITCM | 0 B / 64 KiB |

Les audits ont été réalisés sur les artefacts existants disponibles dans le dépôt, principalement :

- `build/Release/BRICK6_CUBE.map`
- `build/Release/BRICK6_CUBE.elf`
- `build/Debug/BRICK6_CUBE.map`
- `build/Debug/BRICK6_CUBE.elf`

Aucun build n'a été lancé pendant les audits.

### Écarts observés entre le map Release disponible et l'état link récent fourni

| Région | État link récent fourni | Map Release disponible | Écart |
|---|---:|---:|---:|
| DTCMRAM | 123 776 B | 72 512 B | -51 264 B |
| RAM_D1 | 499 296 B | 498 880 B high-water observé | -416 B |
| RAM_D2 | 284 128 B | 262 528 B | -21 600 B |
| RAM_D3 | 14 240 B | 14 240 B | 0 B |

Conclusion importante :

- `RAM_D3` correspond exactement à l'état récent fourni.
- `RAM_D1` est presque identique.
- `RAM_D2` diffère significativement.
- `DTCMRAM` diffère très fortement.

Toute décision finale sur `DTCMRAM` et `RAM_D2` doit être revalidée avec le map exact correspondant aux chiffres récents.

---

## 3. Linker script actif et régions mémoire

Le linker actif côté CMake est :

- `STM32H743IITX_FLASH.ld`

Il définit les régions utiles suivantes :

| Région linker | Origine | Taille | Attributs |
|---|---:|---:|---|
| `DTCMRAM` | `0x20000000` | `128K` | `xrw` |
| `RAM_D1` | `0x24000000` | `512K` | `xrw` |
| `RAM_D2` | `0x30000000` | `288K` | `xrw` |
| `RAM_D3` | `0x38000000` | `64K` | `xrw` |
| `SDRAM` | `0xC0000000` | `32M` | `xrw` |
| `ITCMRAM` | `0x00000000` | `64K` | `xrw` |

La stack top `_estack` est placée à la fin de `RAM_D1`, pas en DTCM.

---

## 4. Macros de placement mémoire

Les macros principales dans `Inc/Storage/memory_layout.h` sont :

| Macro | Section cible | Rôle déclaré | Alignement macro |
|---|---|---|---:|
| `AUDIO_HOT` | `.dtcm_audio` | IRQ critical data/state | aucun |
| `AUDIO_CODE_HOT` | `.itcm_text` | opt-in code hot audio futur en ITCM | aucun |
| `AUDIO_WARM` | `.ram_d1_audio` | block DSP state not directly DMA-owned | aucun |
| `DMA_BUFFER` | `.ram_d2_dma` | CPU↔DMA shared payload | `ALIGN32` |
| `AUDIO_DMA_BUFFER_CACHEABLE` | `.ram_d2_lut` | buffers audio RX/TX DMA cacheables | `ALIGN32` |
| `AUDIO_LUT_D2` | `.ram_d2_lut` | read-mostly audio LUTs hors D1 | aucun |
| `SEQ_STATE_D2` | `.ram_d2_lut` | état séquenceur/runtime interne non-SDRAM | aucun |
| `CTRL_STATE` | `.ram_d3_ctrl` | low-rate control/flags | aucun |
| `UI_SDRAM` | `.sdram_ui` | UI / bulk non real-time | `ALIGN32` |
| `SDRAM_SAMPLES` | `.sdram_samples` | arena samples résidents | `ALIGN32` |
| `SDRAM_RECORDER` | `.sdram_recorder` | recorder/master-buffer history | `ALIGN32` |
| `AUDIO_COLD_SDRAM` | `.sdram_audio_cold` | large cold audio history | `ALIGN32` |

Observation linker importante :

- `AUDIO_WARM` cible `.ram_d1_audio`.
- `AUDIO_CODE_HOT` cible `.itcm_text`.
- `.itcm_text` est cablee vers `ITCMRAM` dans les scripts linker maintenus, avec une LMA en image de chargement (`FLASH` ou `RAM_EXEC` selon le script).
- `.RamFunc` reste inchangee et ne cible pas `ITCMRAM`.
- Aucune fonction n'est annotee avec `AUDIO_CODE_HOT`; la pose RevB ITCM a ete retiree faute de gain IRQ attendu.
- Mais la section de sortie `.ram_d1` capture `*(.ram_d1*)` avant la section `.ram_d1_audio`.
- Les objets `.ram_d1_audio` sont donc placés dans la section de sortie `.ram_d1`.
- La section de sortie `.ram_d1_audio` apparaît vide dans le map Release disponible.

---

## 5. Politique cache / DMA / MPU observée

### D2 DMA non-cacheable

Le code configure une région MPU non-cacheable en D2 :

- base : `0x30000000`
- taille MPU : 32 KiB
- subregions désactivées : `0xF8`
- couverture effective prévue : 12 KiB
- section contrôlée : `.ram_d2_dma`

`main()` vérifie explicitement que `__ram_d2_dma_start__` / `__ram_d2_dma_end__` restent dans cette fenêtre avant activation des caches.

### D2 audio DMA cacheable

Les buffers audio SAI RX/TX sont en D2 cacheable via `AUDIO_DMA_BUFFER_CACHEABLE` / `.ram_d2_lut`.

La cohérence cache est assurée explicitement en IRQ audio :

- invalidate RX avant lecture CPU ;
- clean TX après écriture CPU et avant lecture DMA.

### D1 / D3 / DTCM

Aucune région MPU dédiée D1, D3 ou DTCM n'a été observée.

Conséquences :

- D1 est cacheable par défaut après activation D-Cache.
- D3 est cacheable par défaut, sauf attribut matériel particulier non exprimé dans le projet.
- DTCM est utilisé comme mémoire CPU très rapide pour `AUDIO_HOT`, pas comme zone DMA.
- Les payloads DMA explicites doivent rester D2 ou être accompagnés d'une preuve DMA/cache complète.

---

## 6. Vue globale par région — map Release disponible

| Région | Sections observées | Taille observée | Occupation observée | Rôle dominant |
|---|---|---:|---:|---|
| `DTCMRAM` | `.dtcm_audio` | 72 512 B | 55,32% | audio hot IRQ / engines / mix |
| `RAM_D1` | `.data`, `.bss`, `.ram_d1`, heap/stack reserve | 498 880 B high-water | 95,15% | audio warm + gros états runtime/UI/storage |
| `RAM_D2` | `.ram_d2_dma`, `.ram_d2_lut` | 262 528 B | 89,02% | DMA + séquenceur/param runtime |
| `RAM_D3` | `.ram_d3_ctrl` | 14 240 B | 21,73% | metadata/control Sampler |

---

# 7. RAM_D3

## 7.1 Verdict RAM_D3

`RAM_D3` est sous-utilisée en taille mais contient actuellement presque uniquement du metadata/control Sampler potentiellement touché par des chemins audio RAM-only.

- Taille totale : 65 536 B
- Utilisé Release observé : 14 240 B
- Libre observé : 51 296 B
- Section unique : `.ram_d3_ctrl`
- Section `NOLOAD`
- Alignement début/fin : 32 B
- Symboles start/end : absents

Le contenu D3 n'est pas DMA-owned et n'a pas de contrainte MPU non-cacheable explicite.

## 7.2 Sections et objets

`.ram_d3_ctrl` Release :

| Objet | Taille |
|---|---:|
| `Src/Sampler/sample_cache.c.obj` | 128 B |
| `Src/Sampler/sample_page_cache.c.obj` | 14 092 B |
| `Src/Sampler/sample_pool.c.obj` | 2 B |
| fill final | 18 B |
| Total | 14 240 B |

## 7.3 Symboles D3 Release

| Symbole | Taille | Fichier | Rôle | Statut | Justification synthétique |
|---|---:|---|---|---|---|
| `g_sample_cache_last_fresult` | 64 B | `sample_cache.c` | dernier `FRESULT` par sample | déplaçable immédiatement | statut SD/FatFs, non DMA, non IRQ audio prouvé |
| `g_sample_cache_file_open` | 64 B | `sample_cache.c` | flags fichiers ouverts | déplaçable immédiatement | cycle de vie FatFs, non DMA, non IRQ audio prouvé |
| `g_sample_page_last_slot` | 256 B | `sample_page_cache.c` | accélérateur lookup sample→slot | garder pour l'instant | lookup page-cache potentiellement traversé par rendu Sampler |
| `g_sample_page_cache_state` | 12 B | `sample_page_cache.c` | compteurs génération/touch | garder pour l'instant | acquire/release page-cache peut toucher ces compteurs |
| `g_sample_page_sample_desc` | 13 824 B | `sample_page_cache.c` | metadata sample/page-cache | déplaçable seulement avec vérification IRQ/perf | plus gros bloc D3, pas DMA, mais lu par chemins page-cache Sampler |
| `g_sample_pool_last_sd_error_code` | 1 B | `sample_pool.c` | statut erreur SD | déplaçable immédiatement | statut storage |
| `g_sample_pool_last_load_error` | 1 B | `sample_pool.c` | statut erreur load | déplaçable immédiatement | statut storage |
| fill | 18 B | linker | alignement | N/A | alignement 32 B final |

## 7.4 Groupes de rôle D3

| Rôle | Taille Release | Symboles |
|---|---:|---|
| DMA/periph | 0 B | aucun |
| Audio I/O | 0 B | aucun |
| Cache maintenance | 0 B | aucun |
| UI/display | 0 B | aucun |
| Storage/SD status | 130 B | `g_sample_cache_file_open`, `g_sample_cache_last_fresult`, `g_sample_pool_last_*` |
| Control/runtime Sampler page-cache | 14 092 B | `g_sample_page_*` |
| Fill/alignment | 18 B | fill |

## 7.5 Déplacement D3 vers SDRAM/D1/D2

### Déplaçable immédiatement

| Bloc | Gain |
|---|---:|
| `g_sample_cache_file_open` | 64 B |
| `g_sample_cache_last_fresult` | 64 B |
| `g_sample_pool_last_*` | 2 B, effet link probablement nul seul |

Gain réaliste immédiat : environ 128 B.

### Déplaçable avec vérification

| Bloc | Gain | Condition |
|---|---:|---|
| `g_sample_page_sample_desc` | 13 824 B | vérifier fréquence/coût dans chemins audio Sampler |

### À garder pour l'instant

| Bloc | Gain évité | Raison |
|---|---:|---|
| `g_sample_page_last_slot` | 256 B | accélérateur lookup hot potentiel |
| `g_sample_page_cache_state` | 12 B | compteurs touch/generation hot potentiel |

## 7.6 Opportunité inverse : D1/D2 vers D3

D3 peut recevoir des petits/moyens états control non-DMA. Sa marge disponible observée est d'environ 51 KiB.

Candidats transverses identifiés ensuite :

- certains états UI/storage D1 ;
- certains états param/runtime D2 ;
- éventuellement petits états DTCM non critiques, après profiling.

---

# 8. RAM_D2

## 8.1 Verdict RAM_D2

`RAM_D2` est contrainte mais, dans le map Release disponible, elle est moins pleine que dans l'état récent fourni.

- Taille totale : 294 912 B
- Utilisé Release observé : 262 528 B
- Libre observé : 32 384 B
- Occupation observée : 89,02%
- État récent fourni : 284 128 B / 96,34%
- Écart : 21 600 B

D2 est dominée par du séquenceur/param runtime interne, pas par le DMA.

## 8.2 Sections D2

| Section | Adresse | Taille | Rôle |
|---|---:|---:|---|
| `.ram_d2_dma` | `0x30000000` | 8 000 B | buffers DMA/shared non-cacheable MPU |
| `.ram_d2_lut` | `0x30001f40` | 254 528 B | audio DMA cacheable + états seq/param runtime |

`.ram_d2_dma` exporte :

- `__ram_d2_dma_start__`
- `__ram_d2_dma_end__`

`.ram_d2_lut` n'a pas de symboles start/end dédiés.

## 8.3 `.ram_d2_dma`

| Symbole | Taille | Fichier | Rôle | Statut | Justification |
|---|---:|---|---|---|---|
| `flush_snapshot` | 1 024 B | `drv_display.c` | source SPI5 TX DMA OLED | garder D2 DMA | payload SPI DMA non-cacheable |
| `pwm_buffer` | 2 800 B | `led_hw.c` | WS2812 TIM2 PWM DMA | garder D2 DMA | payload TIM DMA |
| `adc2_dma` | 2 B | `hall_adc.c` | mailbox ADC DMA | garder D2 DMA | ADC DMA circulaire |
| `adc1_dma` | 2 B | `hall_adc.c` | mailbox ADC DMA | garder D2 DMA | ADC DMA circulaire |
| `g_pattern_write_chunk` | 4 096 B | `pattern_sd_bank.c` | staging `f_write` 4 KiB | vérification SD/cache | pas audio, mais chemin SD/FatFs/SDMMC à valider |
| padding/fill | 76 B | linker/alignement | alignement | N/A | padding 32 B |

### Déplacement D2 DMA

- `flush_snapshot`, `pwm_buffer`, `adc1_dma`, `adc2_dma` doivent rester en D2 DMA pour l'instant.
- `g_pattern_write_chunk` est le seul candidat sérieux hors D2 DMA, mais uniquement avec preuve SD/FatFs/cache.

## 8.4 `.ram_d2_lut`

| Symbole | Taille | Fichier | Rôle | Statut | Justification |
|---|---:|---|---|---|---|
| `tx_buffer` | 4 096 B | `audio.c` | SAI TX DMA cacheable | garder D2 | maintenance D-cache explicite IRQ |
| `rx_buffer` | 4 096 B | `audio.c` | SAI RX DMA cacheable | garder D2 | maintenance D-cache explicite IRQ |
| `g_track_runtime_ctx` | 154 B | `track_runtime.c` | autorité binding runtime | D3 possible avec vérification | non-DMA, autorité transversale hot |
| `g_track_sound_state` | 1 792 B | `track_sound_state.c` | état COLORS/MIX/VCA/MOD | D3 possible avec vérification | param/audio track-aware |
| `g_track_tone_sound_state` | 8 176 B | `track_tone_sound_state.c` | état TONE tracks | D3 possible avec vérification | param/engine state |
| `g_param_runtime_track_valid` | 4 816 B | `param_registry_runtime_state.c` | valid flags param cache | D3 possible avec vérification | non-DMA, param runtime |
| `g_param_runtime_track_values` | 19 264 B | `param_registry_runtime_state.c` | valeurs param runtime | D3 possible avec vérification | non-DMA, gros candidat D2→D3 |
| `g_seq_hold_state` | 192 B | `seq_edit.c` | hold/edit UI seq | déplaçable D3 | hors scheduling audio |
| `g_seq_live_rec_*` | 11 B | `seq_live_rec_session.c` | flags live-rec | D3 probable | non-DMA, faible gain |
| `g_seq_project` | 121 968 B | `seq_model.c` | modèle séquenceur / p-lock pool | garder interne | lu par scheduler/boundary audio-block |
| `g_seq_param_slot_to_id` | 2 048 B | `seq_param_iface.c` | mapping p-lock slot→param | interne/D3 avec vérification | utilisé par p-lock apply |
| `g_seq_param_id_to_slot` | 1 376 B | `seq_param_iface.c` | mapping param→p-lock slot | interne/D3 avec vérification | utilisé par scheduler/edit/live-rec |
| `g_seq_param_mix_state` | 336 B | `seq_param_iface.c` | état p-lock MIX | D3 possible avec vérification | apply/restore p-lock |
| `g_seq_param_state` | 86 016 B | `seq_param_iface.c` | état p-lock COLORS/TONE/PLAY/MOD | garder interne | gros, audio apply/p-lock runtime |
| `g_seq_clock_bridge` | 40 B | `seq_runtime.c` | clock bridge audio-block | garder interne | utilisé par collect events audio |
| `g_seq_track_loop_generation` | 56 B | `seq_runtime.c` | génération loop track | garder interne | collect events audio |
| `g_seq_transport_fsm` | 8 B | `seq_runtime.c` | transport FSM | garder interne | collect events audio |
| `g_seq_runtime_diag` | 16 B | `seq_runtime.c` | diagnostics seq | D3 possible, gain nul | passé à collect events |
| `g_seq_runtime_control` | 43 B | `seq_runtime.c` | div/quant/swing mirrors | interne/D3 avec vérification | scheduler uses quant |
| fill | 24 B | linker | alignement | N/A | padding |

## 8.5 Regroupement D2 par rôle

| Rôle | Taille approx. | Commentaire |
|---|---:|---|
| DMA/periph non-cacheable | 7 924 B symboles | OLED SPI, LED PWM, ADC, SD chunk |
| Audio I/O DMA cacheable | 8 192 B | SAI RX/TX |
| Track/param runtime | 34 202 B | bons candidats D2→D3 après vérification |
| Séquenceur modèle/p-lock | 212 110 B | très gros, audio-block, pas SDRAM sans redesign |
| Fill/padding | 100 B | expliqué |

## 8.6 Gains D2

| Scénario | Gain théorique | Risque |
|---|---:|---|
| petits états seq edit/live-rec vers D3 | ~224 B | faible |
| `g_pattern_write_chunk` hors D2 DMA | 4 096 B | moyen SD/cache |
| `g_param_runtime_track_*` vers D3 | 24 080 B | moyen |
| `g_track_tone_sound_state` vers D3 | 8 176 B | moyen |
| `g_track_sound_state` vers D3 | 1 792 B | moyen/faible |
| `g_seq_project` vers SDRAM | 121 968 B | élevé, non recommandé |
| `g_seq_param_state` vers SDRAM | 86 016 B | élevé, non recommandé |

Gain réaliste D2 après vérifications raisonnables : environ 34–38 KiB.

---

# 9. RAM_D1

## 9.1 Verdict RAM_D1

`RAM_D1` est très contrainte et quasiment identique à l'état récent fourni.

- Taille totale : 524 288 B
- High-water Release observé : 498 880 B
- Libre observé : 25 408 B
- Occupation observée : 95,15%
- État récent fourni : 499 296 B / 95,23%
- Écart : 416 B

D1 mélange :

- données initialisées `.data` ;
- `.bss` générique ;
- `AUDIO_WARM` absorbé dans `.ram_d1` ;
- réserves heap/stack.

## 9.2 Sections D1 Release

| Section | Adresse | Taille | Rôle |
|---|---:|---:|---|
| `.data` | `0x24000000` | 2 912 B | données initialisées |
| `.bss` | `0x24000b60` | 321 420 B | zéro-init générale |
| gap alignement | `0x2404f2ec → 0x2404f300` | 20 B | alignement |
| `.ram_d1` | `0x2404f300` | 172 992 B | `AUDIO_WARM` absorbé |
| `.ram_d1_audio` | `0x240796c0` | 0 B | vide |
| `._user_heap_stack` | `0x240796c0` | 1 536 B | heap 512 B + stack 1024 B min |

## 9.3 `.ram_d1` / AUDIO_WARM effectif

| Symbole | Taille | Fichier | Rôle | Statut | Justification |
|---|---:|---|---|---|---|
| `g_revb_engine_buffer` | 131 072 B | `fx_reverb_revb.cpp` | buffer moteur RevB | garder D1 | explicitement validé en Z1 |
| `g_revb_predelay_buffer` | 17 288 B | `fx_reverb_revb.cpp` | predelay RevB | garder D1 | audio runtime RevB |
| `g_sample_cache_io_storage` | 4 097 B | `sample_cache.c` | scratch I/O sample import | vérification SD/cache | non audio IRQ direct, FatFs scratch path |
| `g_sd_preview_ring` | 16 384 B | `sd_preview.c` | ring preview SD vers MAIN | deplace SDRAM pour test | lu par `sd_preview_render_main()` dans DSP audio, cout SDRAM uniquement pendant preview |
| `g_sd_preview_io` | 4 096 B | `sd_preview.c` | scratch I/O preview | garder D1 | passe a `f_read`, conserve en D1 pour eviter risque SDMMC/cache |
| fill | 31 B | linker | alignement | N/A | padding |

## 9.4 Gros blocs `.bss` D1

| Symbole | Taille | Fichier | Rôle | Statut | Justification |
|---|---:|---|---|---|---|
| `g_sampler_clip_slots` | 67 664 B | `brick6_sampler_runtime.c` | slots clip/shifter Sampler | garder interne / redesign | audio render Sampler |
| `g_sample_cache_file` | 38 400 B | `sample_cache.c` | 64 objets `FIL` FatFs | déplaçable SDRAM/D3 | meilleur candidat D1 hors audio |
| `g_master_buffer_stretch` | 36 448 B | `brick6_master_buffer_stretch.c` | timestretch Master/Buffer | garder interne | audio render/stretch |
| `g_mod_lfo_dest_cache` | 19 432 B | `mod_lfo_v1.c` | cache destinations LFO | D3 possible avec vérification | modulation processée par bloc audio |
| `g_param_macro_sources` | 15 680 B | `param_macro.c` | sources macro param | D3 possible avec vérification | control/param |
| `g_keyboard_engine_group_note_track` | 14 336 B | `keyboard_engine.c` | runtime keyboard notes | D3 possible avec vérification | live performance state |
| `g_track_filters` | 11 648 B | `mixer.c` | filtres mixer par track | garder interne | mix final IRQ |
| `previous_filters.0` | 11 648 B | `mixer.c` | snapshot filters rebind | vérification | probable rebind-only, gros candidat réduction |
| `g_ui_template_family_registry` | 9 200 B | `ui_template_page.c` | registry UI templates | déplaçable SDRAM/D3 | UI metadata |
| `g_max_buffer` | 8 192 B | `hall_calibration.c` | calibration hall max | SDRAM/D3 possible | non audio |
| `g_min_buffer` | 8 192 B | `hall_calibration.c` | calibration hall min | SDRAM/D3 possible | non audio |
| `g_seq_play_active_event_token` | 7 168 B | `seq_play_scheduler.c` | anti doublons note events | garder interne | audio apply event |
| `g_ui_clipboard` | 6 232 B | `ui_core_clipboard.c` | clipboard UI | déplaçable SDRAM/D3 | UI only |
| `g_wav_catalog` | 6 208 B | `wav_loader.c` | catalogue WAV | déplaçable SDRAM/D3 | storage/UI |
| `g_seq_play_events` | 6 144 B | `seq_play_scheduler.c` | queue scheduler events | garder interne | collect/apply audio-block |
| `g_project_macro_state` | 4 104 B | `project_v1.c` | macro lock project | déplaçable SDRAM/D3 | persistence/control |
| `hall_sample_fifo` | 4 096 B | `hall_adc.c` | FIFO samples hall | D3 possible avec vérification | non audio, IRQ-ish hall |
| `g_seq_runtime_exec_state` | 2 808 B | `seq_runtime_exec.c` | état exec séquenceur | garder interne | timeline/progression audio-block |
| `g_ps` | 2 764 B | `param_store.c` | param store | D3 possible | control/param |
| `g_seq_live_rec_pending` | 2 048 B | `seq_live_rec_capture.c` | pending live rec capture | D3 possible avec vérification | live-rec |
| `g_seq_live_rec_pending` | 2 048 B | `seq_live_rec_session.c` | pending live rec session | D3 possible avec vérification | live-rec |
| `g_seq_output_guard` | 1 792 B | `seq_output_guard.c` | anti doublons seq output | garder/D3 avec vérification | scheduling output |
| `g_keyboard_engine_group_note_count` | 1 792 B | `keyboard_engine.c` | keyboard group counts | D3 possible | keyboard runtime |

## 9.5 `.data` D1

`.data` pèse seulement 2 912 B.

Gros éléments notables :

- `g_keyboard_arp` : 528 B
- tables function pointers Braids : 384 B / 280 B / 72 B
- `_impure_data` : 76 B
- USB descriptors et petits états HAL/audio/master

Gain potentiel faible. Les opportunités `.data` relèvent plus de `const` correctness que de migration mémoire.

## 9.6 Regroupement D1 par rôle

| Rôle | Taille significative | Commentaire |
|---|---:|---|
| DMA/periph payload | 0 B | les payloads DMA sont D2 |
| Audio hard-RT / mix / engines | >270 KiB | garder interne |
| Audio warm effects | ~157 KiB | RevB + SD preview I/O/sample scratch ; ring preview sorti vers SDRAM |
| UI/display | ~16 KiB | bons candidats hors D1 |
| Storage/SD | ~52 KiB | très bons candidats D3/SDRAM |
| Control/runtime | ~22 KiB | D3 possible |
| Keyboard/Hall | ~38 KiB | D3 possible, SDRAM selon latence |
| libc/HAL/USB/MIDI | ~15 KiB | petits, peu de gain |

## 9.7 Candidats D1 forts vers SDRAM/D3

| Candidat | Taille | Destination plausible | Priorité |
|---|---:|---|---|
| `g_sample_cache_file` | 38 400 B | SDRAM ou D3 | très haute |
| `g_ui_template_family_registry` | 9 200 B | SDRAM ou D3 | haute |
| `g_ui_clipboard` | 6 232 B | SDRAM ou D3 | haute |
| `g_wav_catalog` | 6 208 B | SDRAM ou D3 | haute |
| `g_project_macro_state` | 4 104 B | SDRAM ou D3 | moyenne |
| hall calibration min/max | 16 384 B | D3 ou SDRAM | moyenne |
| metadata pattern/project | ~1–2 KiB | D3/SDRAM | basse |

Gain D1 réaliste sans toucher audio : environ 55–65 KiB.

Gain avec hall calibration et vérifications : environ 80–100 KiB.

## 9.8 À garder D1/interne

| Bloc | Taille | Raison |
|---|---:|---|
| RevB buffers | 148 360 B | explicitement validés D1 en Z1 |
| `g_master_buffer_stretch` | 36 448 B | audio render/stretch |
| `g_sampler_clip_slots` | 67 664 B | audio render Sampler Clip/Shifter |
| `g_track_filters` | 11 648 B | mix final IRQ |
| `g_seq_play_*` | ~13 KiB | scheduler audio-block |
| `g_mod_lfo_runtime` | 1 232 B | modulation audio-block |

---

# 10. DTCMRAM

## 10.1 Verdict DTCM

Dans le map Release disponible, DTCM est loin d'être aussi plein que dans l'état récent fourni.

- Taille totale : 131 072 B
- Utilisé Release observé : 72 512 B
- Libre observé : 58 560 B
- Occupation observée : 55,32%
- État récent fourni : 123 776 B / 94,43%
- Écart : 51 264 B

Tous les symboles DTCM observés sont dans `.dtcm_audio` et relèvent du chemin audio hot.

## 10.2 Objets DTCM Release

| Objet | Taille |
|---|---:|
| `Src/Audio/audio_float.c.obj` | 2 112 B |
| `Src/Audio/fx_delay_dual.cpp.obj` | 19 400 B |
| `Src/Audio/fx_master_macro.c.obj` | 608 B |
| fill | 24 B |
| `Src/Audio/fx_pool.c.obj` | 1 664 B |
| `Src/Audio/fx_reverb.cpp.obj` | 256 B |
| `Src/Audio/fx_reverb_revb.cpp.obj` | 256 B |
| `Src/Audio/mixer.c.obj` | 14 904 B |
| `Src/Core/brick6_braids_runtime.cpp.obj` | 9 072 B |
| `Src/Core/brick6_opal_runtime.cpp.obj` | 12 528 B |
| `Src/Core/brick6_sampler_runtime.c.obj` | 10 472 B |
| `Src/Sampler/sample_cache.c.obj` | 1 216 B |
| Total | 72 512 B |

## 10.3 Symboles DTCM Release

| Symbole | Taille | Fichier | Rôle | Statut |
|---|---:|---|---|---|
| `master_gain` | 4 B | `audio_float.c` | gain master | garder |
| `master_gain_target` | 4 B | `audio_float.c` | cible gain master | garder |
| `master_gain_smoothed` | 4 B | `audio_float.c` | gain lissé | garder |
| `output_adjust` | 4 B | `audio_float.c` | gain sortie | garder |
| `postgain_recip` | 4 B | `audio_float.c` | scaling entrée | garder |
| `tracks` | 2 064 B | `audio_float.c` | buffers float `StereoTrack` | garder impérativement |
| `g_dual` | 160 B | `fx_delay_dual.cpp` | état delay dual | garder |
| `g_haas_r` | 9 608 B | `fx_delay_dual.cpp` | ligne Haas R | D1 possible avec mesure |
| `g_haas_l` | 9 608 B | `fx_delay_dual.cpp` | ligne Haas L | D1 possible avec mesure |
| `g_slots` | 608 B | `fx_master_macro.c` | slots Master FX macro | garder |
| `g_slots` | 320 B | `fx_pool.c` | slots FX pool | vérification, faible gain |
| `g_eq` | 320 B | `fx_pool.c` | EQ bus | garder |
| `g_track_sat` | 1 344 B | `fx_pool.c` | saturation par track | garder |
| `g_reverb_global_mono` | 256 B | `fx_reverb.cpp` | état reverb wrapper | vérification, faible gain |
| `g_revb_predelayed` | 256 B | `fx_reverb_revb.cpp` | buffer bloc RevB | garder |
| mixer scratch `delay_*`, `reverb_*`, `bus_*`, `send_*`, `ext_mono_*` | ~3 840 B | `mixer.c` | scratch bloc mix | garder |
| `g_reverb_input_filter` | 24 B | `mixer.c` | filtre entrée reverb | garder |
| `g_reverb` | 32 B | `mixer.c` | état reverb send | garder |
| `g_external_track_r` | 3 584 B | `mixer.c` | ingress externe R | D1 possible avec mesure |
| `g_external_track_l` | 3 584 B | `mixer.c` | ingress externe L | D1 possible avec mesure |
| `g_external_track_mono` | 3 584 B | `mixer.c` | ingress externe mono | D1 possible avec mesure |
| `g_braids_runtime` | 9 072 B | `brick6_braids_runtime.cpp` | runtime Braids | garder interne / D1 avec profiling |
| `g_opal_runtime` | 12 528 B | `brick6_opal_runtime.cpp` | runtime Opal/SixOp | garder interne / D1 avec profiling |
| `g_sampler_voice` | 10 472 B | `brick6_sampler_runtime.c` | voix Sampler | garder interne / D1-D3 avec profiling |
| `g_sample_cache_voice` | 1 216 B | `sample_cache.c` | curseurs voix sample-cache | garder / D3 avec vérification |
| fill | 24 B | linker | alignement | N/A |

## 10.4 Groupes DTCM par rôle

| Rôle | Taille approx. | Commentaire |
|---|---:|---|
| DMA/periph | 0 B | aucun DMA DTCM observé |
| Audio I/O / frontière float | ~2,1 KiB | `tracks`, gains |
| Scratch mix bloc | ~14 KiB | mixer bus/external track buffers |
| FX delay/reverb/master macro | ~23 KiB | Haas = plus gros levier théorique |
| Engines synth | ~21,6 KiB | Braids + Opal runtime |
| Sampler runtime/cache | ~11,7 KiB | voix/cursors Sampler |
| Fill | 24 B | alignement |

## 10.5 Déplacement DTCM

### Déplaçable immédiatement

Aucun gros bloc DTCM n'est déplaçable immédiatement sans preuve audio.

### Déplaçable avec profiling audio

| Candidat | Gain | Destination plausible | Risque |
|---|---:|---|---|
| `g_haas_l/r` | 19 216 B | D1 | élevé mais levier fort |
| `g_external_track_*` | 10 752 B | D1 | moyen/élevé |
| `g_sampler_voice` | 10 472 B | D1 ou D3 | moyen/élevé |
| `g_sample_cache_voice` | 1 216 B | D3 | moyen, faible gain |
| `g_braids_runtime` | 9 072 B | D1 | élevé |
| `g_opal_runtime` | 12 528 B | D1 | élevé |

### SDRAM déconseillée

Aucun symbole DTCM observé n'est un bon candidat SDRAM direct.

## 10.6 Gains DTCM

| Scénario | Gain DTCM | Risque |
|---|---:|---|
| rien déplacer | 0 B | safe |
| petits états non critiques | <2 KiB | faible/moyen, gain faible |
| `g_sample_cache_voice` vers D3 | 1,2 KiB | moyen |
| `g_external_track_*` vers D1 | 10,5 KiB | moyen/élevé |
| `g_haas_l/r` vers D1 | 18,8 KiB | élevé |
| engines runtime vers D1 | ~53 KiB | élevé |

Gain réaliste après libération D1 et profiling : 10–30 KiB.

---

# 11. Candidats transverses prioritaires

## 11.1 Candidats à déplacer vers SDRAM sans toucher audio hard-RT

| Région source | Candidat | Taille | Priorité | Remarque |
|---|---|---:|---|---|
| D1 | `g_sample_cache_file` | 38 400 B | très haute | meilleur candidat immédiat |
| D1 | `g_ui_template_family_registry` | 9 200 B | haute | UI metadata |
| D1 | `g_ui_clipboard` | 6 232 B | haute | UI clipboard |
| D1 | `g_wav_catalog` | 6 208 B | haute | storage/UI catalogue |
| D1 | `g_project_macro_state` | 4 104 B | moyenne | persistence/control |
| D1 | hall calibration min/max | 16 384 B | moyenne | vérifier latence calibration |
| D2 | `g_pattern_write_chunk` | 4 096 B | moyenne | vérifier SDMMC/FatFs/cache |
| D3 | `g_sample_cache_file_open` + `g_sample_cache_last_fresult` | 128 B | basse | gain négligeable |

## 11.2 Candidats à déplacer vers D3

| Région source | Candidat | Taille | Priorité | Remarque |
|---|---|---:|---|---|
| D2 | `g_param_runtime_track_values` + `g_param_runtime_track_valid` | 24 080 B | haute | bon candidat D2→D3 |
| D2 | `g_track_tone_sound_state` | 8 176 B | haute | état TONE, non-DMA |
| D2 | `g_track_sound_state` | 1 792 B | moyenne | état COLORS/MIX/VCA/MOD |
| D1 | `g_sample_cache_file` | 38 400 B | haute | tient presque seul dans marge D3 |
| D1 | `g_ui_template_family_registry` | 9 200 B | haute | peut compléter D3 si place |
| D1 | `g_param_macro_sources` | 15 680 B | moyenne | vérifier modulation/param |
| D1 | keyboard group arrays | ~16 KiB | moyenne | live performance latency |
| DTCM | `g_sample_cache_voice` | 1 216 B | basse | vérifier audio |

D3 ne peut pas recevoir tous les candidats. Sa marge observée est environ 51 KiB. Il faut donc arbitrer.

Combinaisons possibles :

- `g_sample_cache_file` + `g_ui_template_family_registry` ≈ 47,6 KiB ;
- `g_param_runtime_track_*` + `g_track_tone_sound_state` + `g_track_sound_state` ≈ 34 KiB ;
- `g_param_runtime_track_*` + `g_param_macro_sources` ≈ 39,8 KiB.

## 11.3 Candidats à réduire/redesigner plutôt que déplacer

| Bloc | Région | Taille | Piste |
|---|---|---:|---|
| `g_seq_project` | D2 | 121 968 B | réduire budget p-lock, compresser, sparse storage |
| `g_seq_param_state` | D2 | 86 016 B | structure sparse ou lazy, réduire dimensions |
| `g_sampler_clip_slots` | D1 | 67 664 B | séparer état hot/froid, allocation statique par capacité active |
| `g_master_buffer_stretch` | D1 | 36 448 B | séparer analyse froide / render hot |
| `g_haas_l/r` | DTCM | 19 216 B | D1 après mesure, ou réduire taille Haas |
| `g_external_track_*` | DTCM | 10 752 B | mutualiser mono/stereo si possible |
| `previous_filters.0` | D1 | 11 648 B | éviter snapshot complet ou stocker temporaire ailleurs |

---

# 12. Gains réalistes par région

## 12.1 RAM_D3

| Type de gain | Taille |
|---|---:|
| immédiat safe | ~128 B |
| après vérification page-cache | +13 824 B |
| fonction inverse : capacité libre pour accueillir d'autres états | ~51 KiB |

## 12.2 RAM_D2

| Type de gain | Taille |
|---|---:|
| immédiat safe | ~224 B |
| après vérification D2→D3 control/param | ~34–38 KiB |
| après vérification SD chunk | +4 KiB |
| redesign seq/p-lock | potentiellement >100 KiB |

## 12.3 RAM_D1

| Type de gain | Taille |
|---|---:|
| sans toucher audio | ~55–65 KiB |
| avec hall calibration + storage/UI | ~80 KiB |
| avec vérifications scratch/snapshots | ~80–100 KiB |
| non safe audio/runtime | >300 KiB théorique |

## 12.4 DTCM

| Type de gain | Taille |
|---|---:|
| immédiat safe | 0 B |
| petits états | <2 KiB |
| après profiling modéré | 10–30 KiB |
| non safe engines/audio | ~70 KiB théorique dans map disponible |

---

# 13. Ordre recommandé des prochaines passes mémoire

## 13.1 Récupérer le map exact de l'état récent

Priorité absolue avant patch mémoire : obtenir ou générer le map correspondant aux chiffres récents.

Raison :

- DTCM a un écart de 51 264 B entre le map disponible et le chiffre récent.
- D2 a un écart de 21 600 B.
- Les conclusions DTCM/D2 peuvent manquer des blocs majeurs absents du map disponible.

## 13.2 Libérer D1 par les candidats non-audio

Priorité probable :

1. `g_sample_cache_file`
2. `g_ui_template_family_registry`
3. `g_ui_clipboard`
4. `g_wav_catalog`
5. `g_project_macro_state`
6. hall calibration min/max si acceptable

Objectif : récupérer 55–80 KiB sans toucher au hard-RT audio.

## 13.3 Utiliser D3 comme réserve control interne

D3 peut absorber certains états control/runtime actuellement en D1/D2.

Deux stratégies possibles :

- D3 pour storage/UI fréquent non-DMA (`g_sample_cache_file`, UI registry) ;
- D3 pour param/runtime track-aware (`g_param_runtime_track_*`, `track_tone_sound_state`, `track_sound_state`).

Il faut choisir selon la pression dominante : D1 ou D2.

## 13.4 DTCM : ne toucher qu'après profiling

Les meilleurs leviers DTCM observés sont :

- `g_haas_l/r` ;
- `g_external_track_*` ;
- `g_sampler_voice` ;
- engines Opal/Braids.

Mais tous sont audio hot. La sortie de DTCM doit être validée par mesure CPU / underrun / worst-case.

## 13.5 D2 séquenceur : envisager redesign ciblé

Les gros blocs D2 ne sont pas de simples buffers froids :

- `g_seq_project`
- `g_seq_param_state`

Les déplacer vers SDRAM est risqué. Les vraies pistes sont :

- réduire le budget p-lock ;
- rendre certains états sparse/lazy ;
- séparer persistent model et runtime hot projection ;
- vérifier si toutes les dimensions `[track][set][slot]` doivent être matérialisées en permanence.


## 13.6 Passe appliquee — liberation D1 blocs froids UI/storage/calibration

Passe appliquee sans build et sans toucher DTCM, streaming SD critique, buffers audio ni payloads DMA. `g_sample_cache_file` reste volontairement exclu.

| Symbole | Ancienne region | Nouvelle region | Macro | Gain D1 estime | Raison froid / non audio / non DMA |
|---|---|---|---|---:|---|
| `g_ui_template_family_registry` | `RAM_D1` / `.bss` | `SDRAM` / `.sdram_ui` | `UI_SDRAM` | ~9 200 B | registry de metadata UI, ecrit au register UI, lu par resolution/render/clipboard UI, pas IRQ audio, pas DMA |
| `g_ui_clipboard` | `RAM_D1` / `.bss` | `SDRAM` / `.sdram_ui` | `UI_SDRAM` | ~6 232 B | etat copy/paste UI, manipule par handlers UI, pas IRQ audio, pas DMA |
| `g_wav_catalog` | `RAM_D1` / `.bss` | `SDRAM` / `.sdram_ui` | `UI_SDRAM` | ~6 208 B | catalogue storage/UI des WAV, rafraichi/consulte hors IRQ audio, pas payload DMA |
| `g_project_macro_state` | `RAM_D1` / `.bss` | `SDRAM` / `.sdram_ui` | `UI_SDRAM` | ~4 104 B | etat projet/persistence MACRO, capture/restaure par project save/load et UI macro, pas IRQ audio, pas DMA |
| `g_min_buffer` | `RAM_D1` / `.bss` | `RAM_D3` / `.ram_d3_ctrl` | `CTRL_STATE` | ~8 192 B | buffer median calibration Hall appele par page calibration UI, hors audio hard-RT, pas DMA |
| `g_max_buffer` | `RAM_D1` / `.bss` | `RAM_D3` / `.ram_d3_ctrl` | `CTRL_STATE` | ~8 192 B | buffer median calibration Hall appele par page calibration UI, hors audio hard-RT, pas DMA |

Gain D1 estime total : ~42 128 B, hors effets d'alignement linker.

Symboles explicitement exclus et inchanges dans cette passe : `g_sample_cache_file`, `g_sample_cache_io_storage`, `g_sd_preview_ring`, `g_sd_preview_io`, `g_revb_engine_buffer`, `g_revb_predelay_buffer`, `g_sampler_clip_slots`, `g_master_buffer_stretch`, `g_track_filters`, `g_seq_project`, `g_seq_param_state`, tous buffers DTCM et tous buffers D2 DMA/audio DMA.

Risques restants : revalider le gain avec un map post-link exact, car les tailles ci-dessus viennent de l'audit/map disponible ; verifier la latence SDRAM UI si une interaction clipboard/template est fortement sollicitee, sans risque audio hard-RT attendu.

## 13.7 Passe appliquee — cache runtime param track-scoped D2 vers D3

Passe appliquee sans build et sans toucher aux buffers DMA, aux buffers audio, aux gros blocs sequenceur/p-lock, a DTCM, ni aux etats track sonores canoniques.

| Symbole | Ancienne region | Nouvelle region | Macro | Gain D2 estime | Cout D3 estime | Justification |
|---|---|---|---|---:|---:|---|
| `g_param_runtime_track_values` | `RAM_D2` / `.ram_d2_lut` | `RAM_D3` / `.ram_d3_ctrl` | `CTRL_STATE` | ~19 264 B | ~19 264 B | cache runtime non-DMA, non per-sample, fallback param/event/mod boundary |
| `g_param_runtime_track_valid` | `RAM_D2` / `.ram_d2_lut` | `RAM_D3` / `.ram_d3_ctrl` | `CTRL_STATE` | ~4 816 B | ~4 816 B | cache runtime non-DMA, non per-sample, fallback param/event/mod boundary |

Gain RAM_D2 estime total : ~24 080 B (~24.1 KiB), hors effets d'alignement linker.

Cout RAM_D3 estime total : ~24 080 B (~24.1 KiB). Avec la passe D1 precedente, la marge D3 restante estimee est ~10.8 KiB.

Symboles explicitement exclus et inchanges dans cette passe : `g_track_sound_state`, `g_track_tone_sound_state`, `g_seq_project`, `g_seq_param_state`, `tx_buffer`, `rx_buffer`, `.ram_d2_dma`, tous payloads DMA, tous buffers audio, DTCM, RevB, delay, mixer et Sampler audio hot.

Risques restants : revalider l'adresse et la marge avec un map post-link exact ; aucune logique runtime/cache ni `PARAM_COUNT` n'a ete modifie.


---

## 13.8 Passe appliquee - infrastructure code ITCM

Passe appliquee sans build, sans deplacement de fonction et sans deplacement data.

Infrastructure ajoutee:

| Element | Valeur |
|---|---|
| Macro code hot | `AUDIO_CODE_HOT` |
| Section input/output | `.itcm_text` |
| Region VMA | `ITCMRAM` |
| Scripts cables | `STM32H743IITX_FLASH.ld`, `STM32H743IITX_RAM.ld`, `STM32H743XX_FLASH.ld` |

Contrats:

- `ITCMRAM` est reservee aux futurs tests de code audio hot opt-in.
- `.RamFunc` reste dans son corridor existant et n'est pas modifiee.
- Aucun buffer ni symbole data ne change de section par cette passe.
- Aucune fonction n'est placee en ITCM pour l'instant.
- Avant toute future annotation, le mecanisme de copie boot `.itcm_text` devra etre reinstalle et valide explicitement.

## 13.9 Passe appliquee - ring SD preview D1 vers SDRAM

Passe appliquee sans build, sans changement de logique audio/UI, sans deplacement de buffer DMA, sans toucher au sample streaming principal, a `g_sample_cache_file`, RevB, delay, mixer ou runtime Sampler.

| Symbole | Ancienne region | Nouvelle region | Macro | Gain D1 estime | Justification | Risque restant |
|---|---|---|---|---:|---|---|
| `g_sd_preview_ring` | `RAM_D1` / `.ram_d1_audio` | `SDRAM` / `.sdram_audio_cold` | `AUDIO_COLD_SDRAM` | 16 384 B | ring preview SD vers MAIN, ecrit en superloop par `sd_preview_process()`, lu en IRQ audio par `sd_preview_render_main()` uniquement pendant preview UI/audition temporaire | cout SDRAM en IRQ uniquement pendant preview ; test preview audio requis |

Symboles explicitement inchanges : `g_sd_preview_io` reste en `AUDIO_WARM` / D1 pour eviter le risque SDMMC/cache lie a `f_read`; `g_sample_cache_file`, sample streaming principal, buffers DMA/cache, RevB, delay, mixer et runtime Sampler ne sont pas touches.

# 14. Synthèse finale

## 14.1 Ce qui doit rester en interne rapide

- DTCM audio hot : `tracks`, gains, scratch mix, engines runtime, voix Sampler.
- D1 audio warm : RevB engine/predelay, Master/Buffer stretch, Sampler clip slots, mixer filters.
- D2 DMA : OLED/SPI DMA, LED/TIM DMA, ADC DMA, SAI RX/TX cacheable avec maintenance.
- D2 seq runtime : modèle séquenceur et p-locks tant qu'aucun redesign/profiling n'existe.

## 14.2 Ce qui peut sortir en premier

- `g_sample_cache_file` hors D1.
- UI metadata/clipboard hors D1.
- WAV catalog / project macro state hors D1.
- `g_param_runtime_track_*` de D2 vers D3 si la priorité est D2.
- `g_track_tone_sound_state` et `g_track_sound_state` de D2 vers D3 avec vérification.

## 14.3 Ce qu'il ne faut pas faire sans preuve

- déplacer des buffers DTCM audio hot vers SDRAM ;
- déplacer `g_seq_project` ou `g_seq_param_state` vers SDRAM sans profiling/redesign ;
- déplacer des payloads DMA D2 hors de la fenêtre MPU sans preuve DMA/cache ;
- generaliser le deplacement de rings audio IRQ vers SDRAM sans mesure ; le cas `g_sd_preview_ring` est limite a une preview UI non critique et doit etre valide par test audio cible ;
- considérer D3 comme non-cacheable ou DMA-safe sans configuration explicite.

## 14.4 Gain réaliste global hors hard-RT

Sans toucher aux gros blocs audio hard-RT, un gain réaliste est :

- D1 : 55–80 KiB ;
- D2 : 34–38 KiB si D3 accueille des états param/runtime ;
- D3 : reste une réserve à arbitrer ;
- DTCM : 0 KiB safe immédiat, 10–30 KiB seulement après profiling.

Le gain global réaliste sans refonte profonde est donc de l'ordre de 90–120 KiB de RAM interne réorganisable, principalement en libérant D1/D2 et en exploitant D3/SDRAM pour les états non audio.
