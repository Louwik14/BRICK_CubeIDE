# Audit mÃ©moire interne â€” DTCM / RAM_D1 / RAM_D2 / RAM_D3

## 1. RÃ´le du document

Ce document regroupe les observations des audits ciblÃ©s rÃ©alisÃ©s sur les RAM internes STM32H743 du projet BRICK6 :

- `DTCMRAM`
- `RAM_D1`
- `RAM_D2`
- `RAM_D3`

Il consolide :

- les rÃ©gions et sections linker ;
- les macros de placement mÃ©moire ;
- les symboles observÃ©s dans les map/ELF disponibles ;
- les rÃ´les runtime rÃ©els ;
- les contraintes DMA / cache / IRQ audio ;
- les possibilitÃ©s de dÃ©placement vers SDRAM, D1, D2, D3 ou DTCM ;
- les gains thÃ©oriques et rÃ©alistes ;
- les questions restantes pour une dissection mÃ©moire complÃ¨te.

Ce document est un Ã©tat d'audit. Il ne prescrit aucun dÃ©placement automatique et ne remplace pas la preuve par profiling/audio worst-case.

---

## 2. Source observÃ©e et Ã©tat link actuel

L'Ã©tat link actuel Ã  prendre comme rÃ©fÃ©rence est :

```text
Memory region         Used Size  Region Size  %age Used
           FLASH:      462792 B      1792 KB     25.22%
  BOOT_CTX_FLASH:           0 B       128 KB      0.00%
         DTCMRAM:       72512 B       128 KB     55.32%
          RAM_D1:      430752 B       512 KB     82.16%
          RAM_D2:      233984 B       288 KB     79.34%
          RAM_D3:       48480 B        64 KB     73.97%
           SDRAM:    24485984 B        32 MB     72.97%  (ancien artefact Release; Debug courant expose 64 MiB)
         ITCMRAM:           0 B        64 KB      0.00%
```

Les audits ont Ã©tÃ© rÃ©alisÃ©s sur les artefacts existants disponibles dans le dÃ©pÃ´t, principalement :

- `build/Release/BRICK6_CUBE.map`
- `build/Release/BRICK6_CUBE.elf`
- `build/Debug/BRICK6_CUBE.map`
- `build/Debug/BRICK6_CUBE.elf`

Aucun build n'a Ã©tÃ© lancÃ© pendant les audits.

## 3. Ã‰tat aprÃ¨s passes mÃ©moire rÃ©centes

Les dÃ©placements dÃ©jÃ  appliquÃ©s sont reflÃ©tÃ©s dans l'Ã©tat courant :

- `g_revb_engine_buffer` reste en `AUDIO_WARM` / `RAM_D1`.
- `g_ui_template_family_registry`, `g_ui_clipboard`, `g_wav_catalog`, `g_project_macro_state` sont en `UI_SDRAM` / `SDRAM`.
- `g_min_buffer` et `g_max_buffer` sont en `CTRL_STATE` / `RAM_D3`.
- `g_param_runtime_track_values` et `g_param_runtime_track_valid` sont en `CTRL_STATE` / `RAM_D3`.
- `g_sd_preview_ring` est en `AUDIO_COLD_SDRAM` / `SDRAM`.
- `g_sd_preview_io` est en `AUDIO_COLD_SDRAM` / `SDRAM`.
- L'infrastructure `AUDIO_CODE_HOT` / `.itcm_text` existe, mais aucune fonction audio active n'est actuellement maintenue en `ITCMRAM`.
- `RevB` est l'unique reverb SEND runtime ; Drumboy/GVerb/Oliverb ne font plus partie du runtime.

ConsÃ©quences d'audit :

- `DTCMRAM` a retrouvÃ© de la marge et ne demande pas de dÃ©placement agressif hors profiling.
- `RAM_D1` est nettement plus saine, mais reste une rÃ©gion critique.
- `RAM_D2` reste chargÃ©e, avec les gros Ã©tats sÃ©quenceur/param runtime toujours en place.
- `RAM_D3` est maintenant une rÃ©serve control rÃ©ellement consommÃ©e, pas une zone vide.
- `SDRAM` porte dÃ©sormais une part plus large des blocs froids non-RT.

---

## 4. Linker script actif et rÃ©gions mÃ©moire

Le linker actif cÃ´tÃ© CMake est :

- `STM32H743IITX_FLASH.ld`

Il dÃ©finit les rÃ©gions utiles suivantes :

| RÃ©gion linker | Origine | Taille | Attributs |
|---|---:|---:|---|
| `DTCMRAM` | `0x20000000` | `128K` | `xrw` |
| `RAM_D1` | `0x24000000` | `512K` | `xrw` |
| `RAM_D2` | `0x30000000` | `288K` | `xrw` |
| `RAM_D3` | `0x38000000` | `64K` | `xrw` |
| `SDRAM` | `0xC0000000` | `64M` | `xrw` |
| `ITCMRAM` | `0x00000000` | `64K` | `xrw` |

La stack top `_estack` est placÃ©e Ã  la fin de `RAM_D1`, pas en DTCM.

---

## 5. Macros de placement mÃ©moire

Les macros principales dans `Inc/Storage/memory_layout.h` sont :

| Macro | Section cible | RÃ´le dÃ©clarÃ© | Alignement macro |
|---|---|---|---:|
| `AUDIO_HOT` | `.dtcm_audio` | IRQ critical data/state | aucun |
| `AUDIO_CODE_HOT` | `.itcm_text` | opt-in code hot audio futur en ITCM | aucun |
| `AUDIO_WARM` | `.ram_d1_audio` | block DSP state not directly DMA-owned | aucun |
| `DMA_BUFFER` | `.ram_d2_dma` | CPUâ†”DMA shared payload | `ALIGN32` |
| `AUDIO_DMA_BUFFER_CACHEABLE` | `.ram_d2_lut` | buffers audio RX/TX DMA cacheables | `ALIGN32` |
| `AUDIO_LUT_D2` | `.ram_d2_lut` | read-mostly audio LUTs hors D1 | aucun |
| `SEQ_STATE_D2` | `.ram_d2_lut` | Ã©tat sÃ©quenceur/runtime interne non-SDRAM | aucun |
| `CTRL_STATE` | `.ram_d3_ctrl` | low-rate control/flags | aucun |
| `UI_SDRAM` | `.sdram_ui` | UI / bulk non real-time | `ALIGN32` |
| `UI_STATE_SDRAM` | `.ui_state_sdram` | UI-owned state froid hors D1 | `ALIGN32` |
| `CONTROL_STATE_SDRAM` | `.control_state_sdram` | etat control low-rate hors IRQ audio | `ALIGN32` |
| `STORAGE_STATE_SDRAM` | `.storage_state_sdram` | metadata/handles storage hors IRQ audio et non DMA-owned | `ALIGN32` |
| `MULTI_LOAD_SDRAM` | `.multi_load_sdram` | files/etat LOAD Multi cooperatif hors IRQ audio | `ALIGN32` |
| `STORAGE_SCRATCH_SDRAM` | `.storage_scratch_sdram` | scratch conversion/storage FatFs hors IRQ audio | `ALIGN32` |
| `RECORDER_SCRATCH_SDRAM` | `.recorder_scratch_sdram` | scratch recorder/export hors IRQ producteurs | `ALIGN32` |
| `SDRAM_SAMPLES` | `.sdram_samples` | arena samples rÃ©sidents | `ALIGN32` |
| `AUDIO_COLD_SDRAM` | `.sdram_audio_cold` | large cold audio history | `ALIGN32` |
| `AUDIO_HISTORY_SDRAM` | `.audio_history_sdram` | audio history buffers with direct IRQ access | `ALIGN32` |

Observation linker importante :

- `AUDIO_WARM` cible `.ram_d1_audio`.
- `AUDIO_CODE_HOT` cible `.itcm_text`.
- `.itcm_text` est cablee vers `ITCMRAM` dans les scripts linker maintenus, avec une LMA en image de chargement (`FLASH` ou `RAM_EXEC` selon le script).
- `.RamFunc` reste inchangee et ne cible pas `ITCMRAM`.
- Aucune fonction n'est annotee avec `AUDIO_CODE_HOT`; la piste ITCM audio a ete abandonnee faute de gain IRQ utile et de stabilite suffisante.
- Mais la section de sortie `.ram_d1` capture `*(.ram_d1*)` avant la section `.ram_d1_audio`.
- Les objets `.ram_d1_audio` sont donc placÃ©s dans la section de sortie `.ram_d1`.
- La section de sortie `.ram_d1_audio` apparaÃ®t vide dans le map Release disponible.

---

## 5. Politique cache / DMA / MPU observÃ©e

### D2 DMA non-cacheable

Le code configure une rÃ©gion MPU non-cacheable en D2 :

- base : `0x30000000`
- taille MPU : 32 KiB
- subregions dÃ©sactivÃ©es : `0xF8`
- couverture effective prÃ©vue : 12 KiB
- section contrÃ´lÃ©e : `.ram_d2_dma`

`main()` vÃ©rifie explicitement que `__ram_d2_dma_start__` / `__ram_d2_dma_end__` restent dans cette fenÃªtre avant activation des caches.

### D2 audio DMA cacheable

Les buffers audio SAI RX/TX sont en D2 cacheable via `AUDIO_DMA_BUFFER_CACHEABLE` / `.ram_d2_lut`.

La cohÃ©rence cache est assurÃ©e explicitement en IRQ audio :

- invalidate RX avant lecture CPU ;
- clean TX aprÃ¨s Ã©criture CPU et avant lecture DMA.

### D1 / D3 / DTCM

Aucune rÃ©gion MPU dÃ©diÃ©e D1, D3 ou DTCM n'a Ã©tÃ© observÃ©e.

ConsÃ©quences :

- D1 est cacheable par dÃ©faut aprÃ¨s activation D-Cache.
- D3 est cacheable par dÃ©faut, sauf attribut matÃ©riel particulier non exprimÃ© dans le projet.
- DTCM est utilisÃ© comme mÃ©moire CPU trÃ¨s rapide pour `AUDIO_HOT`, pas comme zone DMA.
- Les payloads DMA explicites doivent rester D2 ou Ãªtre accompagnÃ©s d'une preuve DMA/cache complÃ¨te.

---

## 6. Vue globale par rÃ©gion â€” Ã©tat actuel

| RÃ©gion | Sections observÃ©es | Taille observÃ©e | Occupation observÃ©e | RÃ´le dominant |
|---|---|---:|---:|---|
| `DTCMRAM` | `.dtcm_audio` | 72 512 B | 55,32% | audio hot IRQ / engines / mix |
| `RAM_D1` | `.data`, `.bss`, `.ram_d1`, heap/stack reserve | 430 752 B | 82,16% | audio warm + gros Ã©tats runtime/UI/storage |
| `RAM_D2` | `.ram_d2_dma`, `.ram_d2_lut` | 233 984 B | 79,34% | DMA + sÃ©quenceur/param runtime |
| `RAM_D3` | `.ram_d3_ctrl` | 48 480 B | 73,97% | rÃ©serve control Sampler / runtime |

---

# 7. RAM_D3

## 7.1 Verdict RAM_D3

`RAM_D3` est sous-utilisÃ©e en taille mais contient actuellement presque uniquement du metadata/control Sampler potentiellement touchÃ© par des chemins audio RAM-only.

- Taille totale : 65 536 B
- UtilisÃ© Release observÃ© : 14 240 B
- Libre observÃ© : 51 296 B
- Section unique : `.ram_d3_ctrl`
- Section `NOLOAD`
- Alignement dÃ©but/fin : 32 B
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

| Symbole | Taille | Fichier | RÃ´le | Statut | Justification synthÃ©tique |
|---|---:|---|---|---|---|
| `g_sample_cache_last_fresult` | 64 B | `sample_cache.c` | dernier `FRESULT` par sample | dÃ©plaÃ§able immÃ©diatement | statut SD/FatFs, non DMA, non IRQ audio prouvÃ© |
| `g_sample_cache_file_open` | 64 B | `sample_cache.c` | flags fichiers ouverts | dÃ©plaÃ§able immÃ©diatement | cycle de vie FatFs, non DMA, non IRQ audio prouvÃ© |
| `g_sample_page_last_slot` | 256 B | `sample_page_cache.c` | accÃ©lÃ©rateur lookup sampleâ†’slot | garder pour l'instant | lookup page-cache potentiellement traversÃ© par rendu Sampler |
| `g_sample_page_cache_state` | 12 B | `sample_page_cache.c` | compteurs gÃ©nÃ©ration/touch | garder pour l'instant | acquire/release page-cache peut toucher ces compteurs |
| `g_sample_page_sample_desc` | 13 824 B | `sample_page_cache.c` | metadata sample/page-cache | dÃ©plaÃ§able seulement avec vÃ©rification IRQ/perf | plus gros bloc D3, pas DMA, mais lu par chemins page-cache Sampler |
| `g_sample_pool_last_sd_error_code` | 1 B | `sample_pool.c` | statut erreur SD | dÃ©plaÃ§able immÃ©diatement | statut storage |
| `g_sample_pool_last_load_error` | 1 B | `sample_pool.c` | statut erreur load | dÃ©plaÃ§able immÃ©diatement | statut storage |
| fill | 18 B | linker | alignement | N/A | alignement 32 B final |

## 7.4 Groupes de rÃ´le D3

| RÃ´le | Taille Release | Symboles |
|---|---:|---|
| DMA/periph | 0 B | aucun |
| Audio I/O | 0 B | aucun |
| Cache maintenance | 0 B | aucun |
| UI/display | 0 B | aucun |
| Storage/SD status | 130 B | `g_sample_cache_file_open`, `g_sample_cache_last_fresult`, `g_sample_pool_last_*` |
| Control/runtime Sampler page-cache | 14 092 B | `g_sample_page_*` |
| Fill/alignment | 18 B | fill |

## 7.5 DÃ©placement D3 vers SDRAM/D1/D2

### DÃ©plaÃ§able immÃ©diatement

| Bloc | Gain |
|---|---:|
| `g_sample_cache_file_open` | 64 B |
| `g_sample_cache_last_fresult` | 64 B |
| `g_sample_pool_last_*` | 2 B, effet link probablement nul seul |

Gain rÃ©aliste immÃ©diat : environ 128 B.

### DÃ©plaÃ§able avec vÃ©rification

| Bloc | Gain | Condition |
|---|---:|---|
| `g_sample_page_sample_desc` | 13 824 B | vÃ©rifier frÃ©quence/coÃ»t dans chemins audio Sampler |

### Ã€ garder pour l'instant

| Bloc | Gain Ã©vitÃ© | Raison |
|---|---:|---|
| `g_sample_page_last_slot` | 256 B | accÃ©lÃ©rateur lookup hot potentiel |
| `g_sample_page_cache_state` | 12 B | compteurs touch/generation hot potentiel |

## 7.6 OpportunitÃ© inverse : D1/D2 vers D3

D3 peut recevoir des petits/moyens Ã©tats control non-DMA. Sa marge disponible observÃ©e est d'environ 51 KiB.

Candidats transverses identifiÃ©s ensuite :

- certains Ã©tats UI/storage D1 ;
- certains Ã©tats param/runtime D2 ;
- Ã©ventuellement petits Ã©tats DTCM non critiques, aprÃ¨s profiling.

---

# 8. RAM_D2

## 8.1 Verdict RAM_D2

`RAM_D2` est contrainte mais, dans le map Release disponible, elle est moins pleine que dans l'Ã©tat rÃ©cent fourni.

- Taille totale : 294 912 B
- UtilisÃ© Release observÃ© : 262 528 B
- Libre observÃ© : 32 384 B
- Occupation observÃ©e : 89,02%
- Ã‰tat rÃ©cent fourni : 284 128 B / 96,34%
- Ã‰cart : 21 600 B

D2 est dominÃ©e par du sÃ©quenceur/param runtime interne, pas par le DMA.

## 8.2 Sections D2

| Section | Adresse | Taille | RÃ´le |
|---|---:|---:|---|
| `.ram_d2_dma` | `0x30000000` | 8 000 B | buffers DMA/shared non-cacheable MPU |
| `.ram_d2_lut` | `0x30001f40` | 254 528 B | audio DMA cacheable + Ã©tats seq/param runtime |

`.ram_d2_dma` exporte :

- `__ram_d2_dma_start__`
- `__ram_d2_dma_end__`

`.ram_d2_lut` n'a pas de symboles start/end dÃ©diÃ©s.

## 8.3 `.ram_d2_dma`

| Symbole | Taille | Fichier | RÃ´le | Statut | Justification |
|---|---:|---|---|---|---|
| `flush_snapshot` | 1 024 B | `drv_display.c` | source SPI4 TX DMA OLED | garder D2 DMA | payload SPI DMA non-cacheable |
| `pwm_buffer` | 2 800 B | `led_hw.c` | WS2812 TIM1_CH3 PWM DMA | garder D2 DMA | payload TIM DMA |
| `adc2_dma` | 2 B | `hall_adc.c` | mailbox ADC DMA | garder D2 DMA | ADC DMA circulaire |
| `adc1_dma` | 2 B | `hall_adc.c` | mailbox ADC DMA | garder D2 DMA | ADC DMA circulaire |
| `g_pattern_write_chunk` | 4 096 B | `pattern_sd_bank.c` | staging `f_write` 4 KiB | vÃ©rification SD/cache | pas audio, mais chemin SD/FatFs/SDMMC Ã  valider |
| padding/fill | 76 B | linker/alignement | alignement | N/A | padding 32 B |

### DÃ©placement D2 DMA

- `flush_snapshot`, `pwm_buffer`, `adc1_dma`, `adc2_dma` doivent rester en D2 DMA pour l'instant.
- `g_pattern_write_chunk` est le seul candidat sÃ©rieux hors D2 DMA, mais uniquement avec preuve SD/FatFs/cache.

## 8.4 `.ram_d2_lut`

| Symbole | Taille | Fichier | RÃ´le | Statut | Justification |
|---|---:|---|---|---|---|
| `tx_buffer` | 4 096 B | `audio.c` | SAI TX DMA cacheable | garder D2 | maintenance D-cache explicite IRQ |
| `rx_buffer` | 4 096 B | `audio.c` | SAI RX DMA cacheable | garder D2 | maintenance D-cache explicite IRQ |
| `g_track_runtime_ctx` | 154 B | `track_runtime.c` | autoritÃ© binding runtime | D3 possible avec vÃ©rification | non-DMA, autoritÃ© transversale hot |
| `g_track_sound_state` | 1 792 B | `track_sound_state.c` | Ã©tat COLORS/MIX/VCA/MOD | D3 possible avec vÃ©rification | param/audio track-aware |
| `g_track_tone_sound_state` | 8 176 B | `track_tone_sound_state.c` | Ã©tat TONE tracks | D3 possible avec vÃ©rification | param/engine state |
| `g_param_runtime_track_valid` | 4 816 B | `param_registry_runtime_state.c` | valid flags param cache | D3 possible avec vÃ©rification | non-DMA, param runtime |
| `g_param_runtime_track_values` | 19 264 B | `param_registry_runtime_state.c` | valeurs param runtime | D3 possible avec vÃ©rification | non-DMA, gros candidat D2â†’D3 |
| `g_seq_hold_state` | 192 B | `seq_edit.c` | hold/edit UI seq | dÃ©plaÃ§able D3 | hors scheduling audio |
| `g_seq_live_rec_*` | 11 B | `seq_live_rec_session.c` | flags live-rec | D3 probable | non-DMA, faible gain |
| `g_seq_project` | 121 968 B | `seq_model.c` | modÃ¨le sÃ©quenceur / p-lock pool | garder interne | lu par scheduler/boundary audio-block |
| `g_seq_param_slot_to_id` | 2 048 B | `seq_param_iface.c` | mapping p-lock slotâ†’param | interne/D3 avec vÃ©rification | utilisÃ© par p-lock apply |
| `g_seq_param_id_to_slot` | 1 376 B | `seq_param_iface.c` | mapping paramâ†’p-lock slot | interne/D3 avec vÃ©rification | utilisÃ© par scheduler/edit/live-rec |
| `g_seq_param_mix_state` | 336 B | `seq_param_iface.c` | Ã©tat p-lock MIX | D3 possible avec vÃ©rification | apply/restore p-lock |
| `g_seq_param_state` | 86 016 B | `seq_param_iface.c` | Ã©tat p-lock COLORS/TONE/PLAY/MOD | garder interne | gros, audio apply/p-lock runtime |
| `g_seq_clock_bridge` | 40 B | `seq_runtime.c` | clock bridge audio-block | garder interne | utilisÃ© par collect events audio |
| `g_seq_track_loop_generation` | 56 B | `seq_runtime.c` | gÃ©nÃ©ration loop track | garder interne | collect events audio |
| `g_seq_transport_fsm` | 8 B | `seq_runtime.c` | transport FSM | garder interne | collect events audio |
| `g_seq_runtime_diag` | 16 B | `seq_runtime.c` | diagnostics seq | D3 possible, gain nul | passÃ© Ã  collect events |
| `g_seq_runtime_control` | 43 B | `seq_runtime.c` | div/quant/swing mirrors | interne/D3 avec vÃ©rification | scheduler uses quant |
| fill | 24 B | linker | alignement | N/A | padding |

## 8.5 Regroupement D2 par rÃ´le

| RÃ´le | Taille approx. | Commentaire |
|---|---:|---|
| DMA/periph non-cacheable | 7 924 B symboles | OLED SPI, LED PWM, ADC, SD chunk |
| Audio I/O DMA cacheable | 8 192 B | SAI RX/TX |
| Track/param runtime | 34 202 B | bons candidats D2â†’D3 aprÃ¨s vÃ©rification |
| SÃ©quenceur modÃ¨le/p-lock | 212 110 B | trÃ¨s gros, audio-block, pas SDRAM sans redesign |
| Fill/padding | 100 B | expliquÃ© |

## 8.6 Gains D2

| ScÃ©nario | Gain thÃ©orique | Risque |
|---|---:|---|
| petits Ã©tats seq edit/live-rec vers D3 | ~224 B | faible |
| `g_pattern_write_chunk` hors D2 DMA | 4 096 B | moyen SD/cache |
| `g_param_runtime_track_*` vers D3 | 24 080 B | moyen |
| `g_track_tone_sound_state` vers D3 | 8 176 B | moyen |
| `g_track_sound_state` vers D3 | 1 792 B | moyen/faible |
| `g_seq_project` vers SDRAM | 121 968 B | Ã©levÃ©, non recommandÃ© |
| `g_seq_param_state` vers SDRAM | 86 016 B | Ã©levÃ©, non recommandÃ© |

Gain rÃ©aliste D2 aprÃ¨s vÃ©rifications raisonnables : environ 34â€“38 KiB.

---

# 9. RAM_D1

## 9.1 Verdict RAM_D1

`RAM_D1` est trÃ¨s contrainte et quasiment identique Ã  l'Ã©tat rÃ©cent fourni.

- Taille totale : 524 288 B
- High-water Release observÃ© : 498 880 B
- Libre observÃ© : 25 408 B
- Occupation observÃ©e : 95,15%
- Ã‰tat rÃ©cent fourni : 499 296 B / 95,23%
- Ã‰cart : 416 B

D1 mÃ©lange :

- donnÃ©es initialisÃ©es `.data` ;
- `.bss` gÃ©nÃ©rique ;
- `AUDIO_WARM` absorbÃ© dans `.ram_d1` ;
- rÃ©serves heap/stack.

## 9.2 Sections D1 Release

| Section | Adresse | Taille | RÃ´le |
|---|---:|---:|---|
| `.data` | `0x24000000` | 2 912 B | donnÃ©es initialisÃ©es |
| `.bss` | `0x24000b60` | 321 420 B | zÃ©ro-init gÃ©nÃ©rale |
| gap alignement | `0x2404f2ec â†’ 0x2404f300` | 20 B | alignement |
| `.ram_d1` | `0x2404f300` | 172 992 B | `AUDIO_WARM` absorbÃ© |
| `.ram_d1_audio` | `0x240796c0` | 0 B | vide |
| `._user_heap_stack` | `0x240796c0` | 1 536 B | heap 512 B + stack 1024 B min |

## 9.3 `.ram_d1` / AUDIO_WARM effectif

| Symbole | Taille | Fichier | RÃ´le | Statut | Justification |
|---|---:|---|---|---|---|
| `g_revb_engine_buffer` | 131 072 B | `fx_reverb_revb.cpp` | buffer moteur RevB | garder D1 | explicitement validÃ© en Z1 |
| `g_revb_predelay_buffer` | 17 288 B | `fx_reverb_revb.cpp` | predelay RevB | garder D1 | audio runtime RevB |
| `g_sample_cache_io_storage` | 4 097 B | `sample_cache.c` | scratch I/O sample import | vÃ©rification SD/cache | non audio IRQ direct, FatFs scratch path |
| `g_sd_preview_ring` | 16 384 B | `sd_preview.c` | ring preview SD vers MAIN | fait | dÃ©placÃ© en `AUDIO_COLD_SDRAM`, coÃ»t IRQ limitÃ© Ã  la preview |
| `g_sd_preview_io` | 4 096 B | `sd_preview.c` | scratch I/O preview | fait et testÃ© OK | dÃ©placÃ© en `AUDIO_COLD_SDRAM`, pas de changement sonore ni latence visible hors preview |
| fill | 31 B | linker | alignement | N/A | padding |

## 9.4 Gros blocs `.bss` D1

| Symbole | Taille | Fichier | RÃ´le | Statut | Justification |
|---|---:|---|---|---|---|
| `g_sampler_clip_slots` | 67 664 B | `brick6_sampler_runtime.c` | slots clip/shifter Sampler | garder interne / redesign | audio render Sampler |
| `g_sample_cache_file` | 38 400 B | `sample_cache.c` | 64 objets `FIL` FatFs | Ã  auditer | touche FatFs/streaming, ne pas marquer safe immÃ©diat |
| `g_mod_lfo_dest_cache` | 19 432 B | `mod_lfo_v1.c` | cache destinations LFO | D3 possible avec vÃ©rification | modulation processÃ©e par bloc audio |
| `g_param_macro_sources` | 15 680 B | `param_macro.c` | sources macro param | D3 possible avec vÃ©rification | control/param |
| `g_keyboard_engine_group_note_track` | 14 336 B | `keyboard_engine.c` | runtime keyboard notes | D3 possible avec vÃ©rification | live performance state |
| `g_track_filters` | 11 648 B | `mixer.c` | filtres mixer par track | garder interne | mix final IRQ |
| `previous_filters.0` | 11 648 B | `mixer.c` | snapshot filters rebind | vÃ©rification | probable rebind-only, gros candidat rÃ©duction |
| `g_ui_template_family_registry` | 9 200 B | `ui_template_page.c` | registry UI templates | dÃ©plaÃ§able SDRAM/D3 | UI metadata |
| `g_max_buffer` | 8 192 B | `hall_calibration.c` | calibration hall max | SDRAM/D3 possible | non audio |
| `g_min_buffer` | 8 192 B | `hall_calibration.c` | calibration hall min | SDRAM/D3 possible | non audio |
| `g_seq_play_active_event_token` | 7 168 B | `seq_play_scheduler.c` | anti doublons note events | garder interne | audio apply event |
| `g_ui_clipboard` | 6 232 B | `ui_core_clipboard.c` | clipboard UI | dÃ©plaÃ§able SDRAM/D3 | UI only |
| `g_wav_catalog` | 6 208 B | `wav_loader.c` | catalogue WAV | dÃ©plaÃ§able SDRAM/D3 | storage/UI |
| `g_seq_play_events` | 6 144 B | `seq_play_scheduler.c` | queue scheduler events | garder interne | collect/apply audio-block |
| `g_project_macro_state` | 4 104 B | `project_v1.c` | macro lock project | dÃ©plaÃ§able SDRAM/D3 | persistence/control |
| `hall_sample_fifo` | 4 096 B | `hall_adc.c` | FIFO samples hall | D3 possible avec vÃ©rification | non audio, IRQ-ish hall |
| `g_seq_runtime_exec_state` | 2 808 B | `seq_runtime_exec.c` | Ã©tat exec sÃ©quenceur | garder interne | timeline/progression audio-block |
| `g_ps` | 2 764 B | `param_store.c` | param store | D3 possible | control/param |
| `g_seq_live_rec_pending` | 2 048 B | `seq_live_rec_capture.c` | pending live rec capture | D3 possible avec vÃ©rification | live-rec |
| `g_seq_live_rec_pending` | 2 048 B | `seq_live_rec_session.c` | pending live rec session | D3 possible avec vÃ©rification | live-rec |
| `g_seq_output_guard` | 1 792 B | `seq_output_guard.c` | anti doublons seq output | garder/D3 avec vÃ©rification | scheduling output |
| `g_keyboard_engine_group_note_count` | 1 792 B | `keyboard_engine.c` | keyboard group counts | D3 possible | keyboard runtime |

## 9.5 `.data` D1

`.data` pÃ¨se seulement 2 912 B.

Gros Ã©lÃ©ments notables :

- `g_keyboard_arp` : 528 B
- tables function pointers Wave : 384 B / 280 B / 72 B
- `_impure_data` : 76 B
- USB descriptors et petits Ã©tats HAL/audio/master

Gain potentiel faible. Les opportunitÃ©s `.data` relÃ¨vent plus de `const` correctness que de migration mÃ©moire.

## 9.6 Regroupement D1 par rÃ´le

| RÃ´le | Taille significative | Commentaire |
|---|---:|---|
| DMA/periph payload | 0 B | les payloads DMA sont D2 |
| Audio hard-RT / mix / engines | >270 KiB | garder interne |
| Audio warm effects | ~153 KiB | RevB + sample scratch ; ring et I/O preview sortis vers SDRAM |
| UI/display | ~16 KiB | bons candidats hors D1 |
| Storage/SD | ~52 KiB | trÃ¨s bons candidats D3/SDRAM |
| Control/runtime | ~22 KiB | D3 possible |
| Keyboard/Hall | ~38 KiB | D3 possible, SDRAM selon latence |
| libc/HAL/USB/MIDI | ~15 KiB | petits, peu de gain |

## 9.7 Candidats D1 forts vers SDRAM/D3

| Candidat | Taille | Destination plausible | PrioritÃ© |
|---|---:|---|---|
| `g_sample_cache_file` | 38 400 B | SDRAM ou D3 | Ã  auditer |
| `g_ui_template_family_registry` | 9 200 B | SDRAM ou D3 | haute |
| `g_ui_clipboard` | 6 232 B | SDRAM ou D3 | haute |
| `g_wav_catalog` | 6 208 B | SDRAM ou D3 | haute |
| `g_project_macro_state` | 4 104 B | SDRAM ou D3 | moyenne |
| hall calibration min/max | 16 384 B | D3 ou SDRAM | moyenne |
| metadata pattern/project | ~1â€“2 KiB | D3/SDRAM | basse |

Gain D1 rÃ©aliste sans toucher audio : environ 55â€“65 KiB.

Gain avec hall calibration et vÃ©rifications : environ 80â€“100 KiB.

## 9.8 Ã€ garder D1/interne

| Bloc | Taille | Raison |
|---|---:|---|
| RevB buffers | 148 360 B | explicitement validÃ©s D1 en Z1 |
| `g_pitch_shifter` | 16 396 B | audio playback shifter |
| `g_sampler_clip_slots` | 67 664 B | audio render Sampler Clip/Shifter |
| `g_track_filters` | 11 648 B | mix final IRQ |
| `g_seq_play_*` | ~13 KiB | scheduler audio-block |
| `g_mod_lfo_runtime` | 1 232 B | modulation audio-block |

---

# 10. DTCMRAM

## 10.1 Verdict DTCM

Dans le map Release disponible, DTCM est loin d'Ãªtre aussi plein que dans l'Ã©tat rÃ©cent fourni.

- Taille totale : 131 072 B
- UtilisÃ© Release observÃ© : 72 512 B
- Libre observÃ© : 58 560 B
- Occupation observÃ©e : 55,32%
- Ã‰tat rÃ©cent fourni : 123 776 B / 94,43%
- Ã‰cart : 51 264 B

Tous les symboles DTCM observÃ©s sont dans `.dtcm_audio` et relÃ¨vent du chemin audio hot.

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
| `Src/Core/brick6_sampler_runtime.c.obj` | 10 472 B |
| `Src/Sampler/sample_cache.c.obj` | 1 216 B |
| Total | 72 512 B |

## 10.3 Symboles DTCM Release

| Symbole | Taille | Fichier | RÃ´le | Statut |
|---|---:|---|---|---|
| `master_gain` | 4 B | `audio_float.c` | gain master | garder |
| `master_gain_target` | 4 B | `audio_float.c` | cible gain master | garder |
| `master_gain_smoothed` | 4 B | `audio_float.c` | gain lissÃ© | garder |
| `output_adjust` | 4 B | `audio_float.c` | gain sortie | garder |
| `postgain_recip` | 4 B | `audio_float.c` | scaling entrÃ©e | garder |
| `tracks` | 2 064 B | `audio_float.c` | buffers float `StereoTrack` | garder impÃ©rativement |
| `g_dual` | 160 B | `fx_delay_dual.cpp` | Ã©tat delay dual | garder |
| `g_haas_r` | 9 608 B | `fx_delay_dual.cpp` | ligne Haas R | D1 possible avec mesure |
| `g_haas_l` | 9 608 B | `fx_delay_dual.cpp` | ligne Haas L | D1 possible avec mesure |
| `g_slots` | 608 B | `fx_master_macro.c` | slots Master FX macro | garder |
| `g_slots` | 320 B | `fx_pool.c` | slots FX pool | vÃ©rification, faible gain |
| `g_eq` | 320 B | `fx_pool.c` | EQ bus | garder |
| `g_track_sat` | 1 344 B | `fx_pool.c` | saturation par track | garder |
| `g_reverb_global_mono` | 256 B | `fx_reverb.cpp` | Ã©tat reverb wrapper | vÃ©rification, faible gain |
| `g_revb_predelayed` | 256 B | `fx_reverb_revb.cpp` | buffer bloc RevB | garder |
| mixer scratch `delay_*`, `reverb_*`, `bus_*`, `send_*`, `ext_mono_*` | ~3 840 B | `mixer.c` | scratch bloc mix | garder |
| `g_reverb_input_filter` | 24 B | `mixer.c` | filtre entrÃ©e reverb | garder |
| `g_reverb` | 32 B | `mixer.c` | Ã©tat reverb send | garder |
| `g_external_track_r` | 3 584 B | `mixer.c` | ingress externe R | D1 possible avec mesure |
| `g_external_track_l` | 3 584 B | `mixer.c` | ingress externe L | D1 possible avec mesure |
| `g_external_track_mono` | 3 584 B | `mixer.c` | ingress externe mono | D1 possible avec mesure |
| `g_braids_runtime` | 9 072 B | `brick6_braids_runtime.cpp` | runtime Wave | garder interne / D1 avec profiling |
| `g_sampler_voice` | 10 472 B | `brick6_sampler_runtime.c` | voix Sampler | garder interne / D1-D3 avec profiling |
| `g_sample_cache_voice` | 1 216 B | `sample_cache.c` | curseurs voix sample-cache | garder / D3 avec vÃ©rification |
| fill | 24 B | linker | alignement | N/A |

## 10.4 Groupes DTCM par rÃ´le

| RÃ´le | Taille approx. | Commentaire |
|---|---:|---|
| DMA/periph | 0 B | aucun DMA DTCM observÃ© |
| Audio I/O / frontiÃ¨re float | ~2,1 KiB | `tracks`, gains |
| Scratch mix bloc | ~14 KiB | mixer bus/external track buffers |
| FX delay/reverb/master macro | ~23 KiB | Haas = plus gros levier thÃ©orique |
| Engines synth | Wave runtime |
| Sampler runtime/cache | ~11,7 KiB | voix/cursors Sampler |
| Fill | 24 B | alignement |

## 10.5 DÃ©placement DTCM

### DÃ©plaÃ§able immÃ©diatement

Aucun gros bloc DTCM n'est dÃ©plaÃ§able immÃ©diatement sans preuve audio.

### DÃ©plaÃ§able avec profiling audio

| Candidat | Gain | Destination plausible | Risque |
|---|---:|---|---|
| `g_haas_l/r` | 19 216 B | D1 | Ã©levÃ© mais levier fort |
| `g_external_track_*` | 10 752 B | D1 | moyen/Ã©levÃ© |
| `g_sampler_voice` | 10 472 B | D1 ou D3 | moyen/Ã©levÃ© |
| `g_sample_cache_voice` | 1 216 B | D3 | moyen, faible gain |
| `g_braids_runtime` | 9 072 B | D1 | Ã©levÃ© |

### SDRAM dÃ©conseillÃ©e

Aucun symbole DTCM observÃ© n'est un bon candidat SDRAM direct.

## 10.6 Gains DTCM

| ScÃ©nario | Gain DTCM | Risque |
|---|---:|---|
| rien dÃ©placer | 0 B | safe |
| petits Ã©tats non critiques | <2 KiB | faible/moyen, gain faible |
| `g_sample_cache_voice` vers D3 | 1,2 KiB | moyen |
| `g_external_track_*` vers D1 | 10,5 KiB | moyen/Ã©levÃ© |
| `g_haas_l/r` vers D1 | 18,8 KiB | Ã©levÃ© |
| engines runtime vers D1 | ~53 KiB | Ã©levÃ© |

Gain rÃ©aliste aprÃ¨s libÃ©ration D1 et profiling : 10â€“30 KiB.

---

# 11. Candidats transverses prioritaires

## 11.1 Candidats Ã  dÃ©placer vers SDRAM sans toucher audio hard-RT

| RÃ©gion source | Candidat | Taille | PrioritÃ© | Remarque |
|---|---|---:|---|---|
| D1 | `g_sample_cache_file` | 38 400 B | Ã  auditer | touche FatFs/streaming |
| D1 | `g_ui_template_family_registry` | 9 200 B | haute | UI metadata |
| D1 | `g_ui_clipboard` | 6 232 B | haute | UI clipboard |
| D1 | `g_wav_catalog` | 6 208 B | haute | storage/UI catalogue |
| D1 | `g_project_macro_state` | 4 104 B | moyenne | persistence/control |
| D1 | hall calibration min/max | 16 384 B | moyenne | vÃ©rifier latence calibration |
| D2 | `g_pattern_write_chunk` | 4 096 B | moyenne | vÃ©rifier SDMMC/FatFs/cache |
| D3 | `g_sample_cache_file_open` + `g_sample_cache_last_fresult` | 128 B | basse | gain nÃ©gligeable |

## 11.2 Candidats Ã  dÃ©placer vers D3

| RÃ©gion source | Candidat | Taille | PrioritÃ© | Remarque |
|---|---|---:|---|---|
| D2 | `g_param_runtime_track_values` + `g_param_runtime_track_valid` | 24 080 B | haute | bon candidat D2â†’D3 |
| D2 | `g_track_tone_sound_state` | 8 176 B | haute | Ã©tat TONE, non-DMA |
| D2 | `g_track_sound_state` | 1 792 B | moyenne | Ã©tat COLORS/MIX/VCA/MOD |
| D1 | `g_sample_cache_file` | 38 400 B | Ã  auditer | ne pas classer safe sans profiling dÃ©diÃ© |
| D1 | `g_ui_template_family_registry` | 9 200 B | haute | peut complÃ©ter D3 si place |
| D1 | `g_param_macro_sources` | 15 680 B | moyenne | vÃ©rifier modulation/param |
| D1 | keyboard group arrays | ~16 KiB | moyenne | live performance latency |
| DTCM | `g_sample_cache_voice` | 1 216 B | basse | vÃ©rifier audio |

D3 ne peut pas recevoir tous les candidats. Sa marge observÃ©e est environ 51 KiB. Il faut donc arbitrer.

Combinaisons possibles :

- `g_sample_cache_file` + `g_ui_template_family_registry` â‰ˆ 47,6 KiB ;
- `g_param_runtime_track_*` + `g_track_tone_sound_state` + `g_track_sound_state` â‰ˆ 34 KiB ;
- `g_param_runtime_track_*` + `g_param_macro_sources` â‰ˆ 39,8 KiB.

## 11.3 Candidats Ã  rÃ©duire/redesigner plutÃ´t que dÃ©placer

| Bloc | RÃ©gion | Taille | Piste |
|---|---|---:|---|
| `g_seq_project` | D2 | 121 968 B | rÃ©duire budget p-lock, compresser, sparse storage |
| `g_seq_param_state` | D2 | 86 016 B | structure sparse ou lazy, rÃ©duire dimensions |
| `g_sampler_clip_slots` | D1 | 67 664 B | sÃ©parer Ã©tat hot/froid, allocation statique par capacitÃ© active |
| `g_pitch_shifter` | D1 | 16 396 B | reuse `brick6_clip_shifter`, sans analyse separee |
| `g_haas_l/r` | DTCM | 19 216 B | D1 aprÃ¨s mesure, ou rÃ©duire taille Haas |
| `g_external_track_*` | DTCM | 10 752 B | mutualiser mono/stereo si possible |
| `previous_filters.0` | D1 | 11 648 B | Ã©viter snapshot complet ou stocker temporaire ailleurs |

---

# 12. Gains rÃ©alistes par rÃ©gion

## 12.1 RAM_D3

| Type de gain | Taille |
|---|---:|
| immÃ©diat safe | ~128 B |
| aprÃ¨s vÃ©rification page-cache | +13 824 B |
| fonction inverse : capacitÃ© libre pour accueillir d'autres Ã©tats | ~51 KiB |

## 12.2 RAM_D2

| Type de gain | Taille |
|---|---:|
| immÃ©diat safe | ~224 B |
| aprÃ¨s vÃ©rification D2â†’D3 control/param | ~34â€“38 KiB |
| aprÃ¨s vÃ©rification SD chunk | +4 KiB |
| redesign seq/p-lock | potentiellement >100 KiB |

## 12.3 RAM_D1

| Type de gain | Taille |
|---|---:|
| sans toucher audio | ~55â€“65 KiB |
| avec hall calibration + storage/UI | ~80 KiB |
| avec vÃ©rifications scratch/snapshots | ~80â€“100 KiB |
| non safe audio/runtime | >300 KiB thÃ©orique |

## 12.4 DTCM

| Type de gain | Taille |
|---|---:|
| immÃ©diat safe | 0 B |
| petits Ã©tats | <2 KiB |
| aprÃ¨s profiling modÃ©rÃ© | 10â€“30 KiB |
| non safe engines/audio | ~70 KiB thÃ©orique dans map disponible |

---

# 13. Ordre recommandÃ© des prochaines passes mÃ©moire

## 13.1 RÃ©cupÃ©rer le map exact de l'Ã©tat rÃ©cent

PrioritÃ© absolue avant patch mÃ©moire : obtenir ou gÃ©nÃ©rer le map correspondant aux chiffres rÃ©cents.

Raison :

- DTCM a un Ã©cart de 51 264 B entre le map disponible et le chiffre rÃ©cent.
- D2 a un Ã©cart de 21 600 B.
- Les conclusions DTCM/D2 peuvent manquer des blocs majeurs absents du map disponible.

## 13.2 LibÃ©rer D1 par les candidats non-audio

PrioritÃ© probable :

1. `g_sample_cache_file` après audit dédié
2. `g_ui_template_family_registry`
3. `g_ui_clipboard`
4. `g_wav_catalog`
5. `g_project_macro_state`
6. hall calibration min/max si acceptable

Objectif : rÃ©cupÃ©rer 55â€“80 KiB sans toucher au hard-RT audio.

## 13.3 Utiliser D3 comme rÃ©serve control interne

D3 peut absorber certains Ã©tats control/runtime actuellement en D1/D2.

Deux stratÃ©gies possibles :

- D3 pour storage/UI frÃ©quent non-DMA (`g_sample_cache_file`, UI registry) ;
- D3 pour param/runtime track-aware (`g_param_runtime_track_*`, `track_tone_sound_state`, `track_sound_state`).

Il faut choisir selon la pression dominante : D1 ou D2.

## 13.4 DTCM : ne toucher qu'aprÃ¨s profiling

Les meilleurs leviers DTCM observÃ©s sont :

- `g_haas_l/r` ;
- `g_external_track_*` ;
- `g_sampler_voice` ;
- engine Wave.

Mais tous sont audio hot. La sortie de DTCM doit Ãªtre validÃ©e par mesure CPU / underrun / worst-case.

## 13.5 D2 sÃ©quenceur : envisager redesign ciblÃ©

Les gros blocs D2 ne sont pas de simples buffers froids :

- `g_seq_project`
- `g_seq_param_state`

Les dÃ©placer vers SDRAM est risquÃ©. Les vraies pistes sont :

- rÃ©duire le budget p-lock ;
- rendre certains Ã©tats sparse/lazy ;
- sÃ©parer persistent model et runtime hot projection ;
- vÃ©rifier si toutes les dimensions `[track][set][slot]` doivent Ãªtre matÃ©rialisÃ©es en permanence.


## 13.6 Passe appliquee â€” liberation D1 blocs froids UI/storage/calibration

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

Symboles explicitement exclus et inchanges dans cette passe : `g_sample_cache_file`, `g_sample_cache_io_storage`, `g_sd_preview_ring`, `g_sd_preview_io`, `g_revb_engine_buffer`, `g_revb_predelay_buffer`, `g_sampler_clip_slots`, `g_track_filters`, `g_seq_project`, `g_seq_param_state`, tous buffers DTCM et tous buffers D2 DMA/audio DMA.

Risques restants : revalider le gain avec un map post-link exact, car les tailles ci-dessus viennent de l'audit/map disponible ; verifier la latence SDRAM UI si une interaction clipboard/template est fortement sollicitee, sans risque audio hard-RT attendu.

## 13.7 Passe appliquee â€” cache runtime param track-scoped D2 vers D3

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

- `ITCMRAM` est disponible pour essais ciblés, mais la piste audio ITCM n'est plus prioritaire.
- `.RamFunc` reste dans son corridor existant et n'est pas modifiee.
- Aucun buffer ni symbole data ne change de section par cette passe.
- Aucune fonction audio active n'est maintenue en ITCM pour l'instant.
- Avant toute future annotation, le mecanisme de copie boot `.itcm_text` devra etre reinstalle et valide explicitement.

## 13.9 Passe appliquee - SD preview D1 vers SDRAM

Passe appliquee sans build, sans changement de logique audio/UI, sans deplacement de buffer DMA, sans toucher au sample streaming principal, a `g_sample_cache_file`, RevB, delay, mixer ou runtime Sampler.

| Symbole | Ancienne region | Nouvelle region | Macro | Gain D1 estime | Justification | Risque restant |
|---|---|---|---|---:|---|---|
| `g_sd_preview_ring` | `RAM_D1` / `.ram_d1_audio` | `SDRAM` / `.sdram_audio_cold` | `AUDIO_COLD_SDRAM` | 16 384 B | ring preview SD vers MAIN, ecrit en superloop par `sd_preview_process()`, lu en IRQ audio par `sd_preview_render_main()` uniquement pendant preview UI/audition temporaire | cout SDRAM en IRQ uniquement pendant preview ; test preview audio requis |
| `g_sd_preview_io` | `RAM_D1` / `.ram_d1_audio` | `SDRAM` / `.sdram_audio_cold` | `AUDIO_COLD_SDRAM` | 4 096 B | scratch I/O preview passe a `f_read`, puis lu hors IRQ par le decode WAV preview ; alignement 32 garanti par la macro | risque SDMMC/FatFs/cache a valider par preview WAV/SD |

Symboles explicitement inchanges : `g_sample_cache_file`, sample streaming principal, buffers DMA/cache D2, RevB, delay, mixer et runtime Sampler ne sont pas touches.

## 13.10 Candidats restants non validés

| Candidat | Statut | Raison |
|---|---|---|
| `g_sample_cache_file` | à auditer | FatFs/streaming, pas safe immédiat |
| `g_sample_cache_io_storage` | non déplacé | reste lié au chemin sample cache |
| `g_track_sound_state` | en D2 | garder D2 tant que la pression D3 n'est pas arbitrée |
| `g_track_tone_sound_state` | en D2 | garder D2 tant que la pression D3 n'est pas arbitrée |
| `g_seq_project` | en D2 | gros bloc séquenceur, ne pas sortir vers SDRAM sans redesign |
| `g_seq_param_state` | en D2 | gros bloc séquenceur, ne pas sortir vers SDRAM sans redesign |

# 14. SynthÃ¨se finale

## 14.1 Ce qui doit rester en interne rapide

- DTCM audio hot : `tracks`, gains, scratch mix, engines runtime, voix Sampler.
- D2 DMA : OLED/SPI DMA, LED/TIM DMA, ADC DMA, SAI RX/TX cacheable avec maintenance.
- D2 seq runtime : modÃ¨le sÃ©quenceur et p-locks tant qu'aucun redesign/profiling n'existe.

## 14.2 Ce qui peut sortir en premier

- `g_sample_cache_file` hors D1.
- UI metadata/clipboard hors D1.
- WAV catalog / project macro state hors D1.
- `g_param_runtime_track_*` de D2 vers D3 si la prioritÃ© est D2.
- `g_track_tone_sound_state` et `g_track_sound_state` de D2 vers D3 avec vÃ©rification.

## 14.3 Ce qu'il ne faut pas faire sans preuve

- dÃ©placer des buffers DTCM audio hot vers SDRAM ;
- dÃ©placer `g_seq_project` ou `g_seq_param_state` vers SDRAM sans profiling/redesign ;
- dÃ©placer des payloads DMA D2 hors de la fenÃªtre MPU sans preuve DMA/cache ;
- generaliser le deplacement de rings audio IRQ vers SDRAM sans mesure ; le cas `g_sd_preview_ring` est limite a une preview UI non critique et doit etre valide par test audio cible ;
- considÃ©rer D3 comme non-cacheable ou DMA-safe sans configuration explicite.

## 14.4 Gain rÃ©aliste global hors hard-RT

Sans toucher aux gros blocs audio hard-RT, un gain rÃ©aliste est :

- D1 : 55â€“80 KiB ;
- D2 : 34â€“38 KiB si D3 accueille des Ã©tats param/runtime ;
- D3 : reste une rÃ©serve Ã  arbitrer ;
- DTCM : 0 KiB safe immÃ©diat, 10â€“30 KiB seulement aprÃ¨s profiling.

Le gain global rÃ©aliste sans refonte profonde est donc de l'ordre de 90â€“120 KiB de RAM interne rÃ©organisable, principalement en libÃ©rant D1/D2 et en exploitant D3/SDRAM pour les Ã©tats non audio.
