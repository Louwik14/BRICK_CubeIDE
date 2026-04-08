# Audit pré-D-cache STM32H7 (passage sans activation)

Date: 2026-04-08

## 1) Cartographie mémoire utile au D-cache

- `memory_layout.h` structure déjà les zones:
  - `AUDIO_HOT -> .dtcm_audio` (IRQ/hot path)
  - `AUDIO_WARM -> .ram_d1_audio`
  - `DMA_BUFFER -> .ram_d2_dma` (aligné 32)
  - `AUDIO_LUT_D2` / `SEQ_STATE_D2 -> .ram_d2_lut`
  - `CTRL_STATE -> .ram_d3_ctrl`
  - `UI_SDRAM -> .sdram_ui`
  - `AUDIO_COLD_SDRAM -> .sdram_audio_cold`
- Le linker `STM32H743IITX_FLASH.ld` mappe explicitement ces sections vers DTCM, D1, D2, D3 et SDRAM.
- Snapshot map release:
  - `.dtcm_audio` ≈ `0x35a0` (audio_float, mixer, fx_pool, tb3, dexed)
  - `.ram_d2_dma` ≈ `0x2b00` (LED DMA + audio ping/pong)
  - `.ram_d2_lut` ≈ `0x37960` (LUT Dexed + gros états seq/param/runtime)
  - `.ram_d3_ctrl` ≈ `0x40`
  - SDRAM majoritairement `audio_cold/ui` (sample pool, recorder, pattern RAM, etc.)

## 2) Buffers/chemins DMA sensibles exacts

### Audio SAI DMA (critique hard realtime)
- Buffers ping/pong: `Src/Audio/audio.c`
  - `static DMA_BUFFER int32_t rx_buffer[...]`
  - `static DMA_BUFFER int32_t tx_buffer[...]`
- Flux:
  - DMA remplit `rx_buffer` (SAI RX)
  - CPU lit `rx_buffer` + écrit `tx_buffer` dans IRQ (`HAL_SAI_RxHalfCpltCallback` / `HAL_SAI_RxCpltCallback`)
  - DMA lit `tx_buffer` (SAI TX)
- Risque futur si cacheable sans protocole:
  - stale read sur RX (manque invalidate)
  - stale write sur TX (manque clean)

### ADC Hall DMA
- `Src/App/Hall/hall_adc.c`:
  - `adc1_dma`, `adc2_dma` alimentés via `HAL_ADC_Start_DMA(..., &adcX_dma, 1)`
  - CPU lit ces variables dans `hall_adc_process_pair()`
- Risque: variables DMA actuellement non placées explicitement en non-cacheable.

### TIM2 PWM DMA (LED)
- `Drivers/Drv_app/Src/led_hw.c`:
  - `pwm_buffer` en `.ram_d2_dma`, aligné 32
  - clean D-cache déjà prévu conditionnellement avant `HAL_TIM_PWM_Start_DMA`
- C’est déjà « D-cache-ready » côté émission DMA.

### SDMMC DMA (stockage)
- `Src/SD/sd_diskio.c` contient le squelette de maintenance cache, mais
  `ENABLE_SD_DMA_CACHE_MAINTENANCE` est commenté (désactivé).
- Si D-cache activé et buffers cacheables: lectures/écritures SD potentiellement incohérentes.

## 3) Hot path audio: memory-bound / compute-bound ?

Verdict: **mixte, avec dominante compute dans les engines/fx et dominante mémoire dans le mix/copies**.

- Compute-bound:
  - synth render (DX7/MonoB/TB3) + filtres/FX par bloc (`brick6_audio_runtime.c`, `mixer.c`, `fx_*`).
- Memory-bound:
  - `audio_io_unpack` / `audio_io_pack` parcourent et convertissent des buffers continus.
  - `mixer_process` enchaîne beaucoup de passes buffers (`memset`, accumulations send/master/cue, `memcpy` final), donc pression mémoire locale forte.
- Conséquence:
  - Le gain D-cache ne viendra pas de DTCM hot (déjà rapide), mais surtout des zones D1/D2/SDRAM qui sont relues/écrites fréquemment hors DMA strict.

## 4) Ce qui gagnerait probablement le plus avec D-cache

- États runtime/lists relues souvent en callback:
  - `track_runtime` / `param_registry` / `seq_runtime` en `.ram_d2_lut`.
- LUT Dexed en `.ram_d2_lut` (`tanhtab`, `exp2tab`, `sintab`, etc.).
- Structures mix/runtime en D1/D2 hors DMA exclusif.
- Accès cold SDRAM « lecture CPU » (sample pool, gros buffers audio) selon patterns d’accès.

## 5) Ce qui est dangereux à cacheer naïvement

- **À garder non-cacheable (recommandé simple/sûr)**:
  - `rx_buffer` / `tx_buffer` audio SAI
  - `adc1_dma` / `adc2_dma` (ou wrapper invalidate très strict)
  - tout buffer circulaire DMA fortement temps-réel partagé CPU/DMA
- **Cacheable + maintenance obligatoire**:
  - buffers SDMMC DMA (clean/invalidate par adresse)
  - buffers DMA ponctuels (si pas isolés MPU)
- **Déjà partiellement traité**:
  - LED DMA avec clean conditionnel avant start.

## 6) Préparation requise avant activation D-cache

Checklist pré-activation:
1. Lister tous les buffers DMA et marquer leur politique (non-cacheable vs maintenance).
2. Isoler les buffers DMA audio/ADC dans une région MPU non-cacheable dédiée (alignée, bornée).
3. Ajouter wrappers unifiés:
   - `dcache_clean_by_addr_aligned()`
   - `dcache_invalidate_by_addr_aligned()`
4. Appliquer wrappers dans tous les drivers DMA concernés (audio, SD, ADC si cacheable).
5. Formaliser convention de placement:
   - `DMA_BUFFER` = shared CPU/DMA
   - `AUDIO_HOT` = CPU only hard-RT
   - `AUDIO_COLD_SDRAM` = gros buffers non IRQ.
6. Ajouter assertions d’alignement 32B pour buffers DMA.
7. Documenter protocole cohérence (qui clean/invalidate, à quel moment, sens DMA).
8. Préparer un plan de test régression audio + stockage.

## 7) Stratégie d’activation la plus sûre

1. **Phase A (zéro risque audio)**
   - Activer D-cache global + MPU non-cacheable pour `.ram_d2_dma` audio/ADC.
   - Vérifier audio bit-perfect/stable (pas de crackle, pas de drift).
2. **Phase B (DMA secondaires)**
   - Activer maintenance cache sur SD (`ENABLE_SD_DMA_CACHE_MAINTENANCE`) et valider I/O.
3. **Phase C (optim perf)**
   - Mesurer charge IRQ avant/après via `cpu_load`.
   - Mesurer latence/overruns audio, stabilité longue durée.
4. **Phase D (fine tuning)**
   - Réévaluer quelles zones D2/SDRAM restent cacheables/non-cacheables selon profil réel.

Tests immédiats à prévoir au moment de l’activation:
- stress audio (48 kHz continu, tracks + FX + recorder)
- stress SD read/write pendant audio
- test hall ADC continu
- monitoring des compteurs `cpu_load` + artefacts audio

Symptômes à surveiller:
- clicks/pops périodiques,
- corruption ponctuelle buffers,
- glitches seulement sous charge DMA,
- incohérences SD aléatoires.

## 8) D-cache: vaut-il probablement le coup ici ?

**Oui, probablement oui**, pour ce projet, à condition d’une discipline cohérence DMA stricte:
- architecture déjà préparée (sections hot/warm/dma/cold explicites)
- hot path combine calcul DSP + beaucoup de trafic buffers
- présence de LUT/états runtime volumineux hors DTCM
- potentiel de gain CPU observable via `cpu_load`

Risque principal n’est pas la perf, mais la cohérence DMA. Donc activation progressive + isolation DMA est la stratégie recommandée.
