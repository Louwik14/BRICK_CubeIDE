# Audit architecture complète — STM32H743 (CubeIDE/CubeMX)

## 1) Audit summary (problèmes / risques actuels)

### 1.1 Répartition mémoire observée (Release)

- Le linker place `.data`, `.bss`, `.ram_d1`, heap et stack en **RAM_D1** (AXI SRAM) par défaut.
- Empreinte `.bss` importante (~`0x61254`), dominée par `g_state` granular (`0x5e044`).
- Buffers audio DMA `tx_buffer`/`rx_buffer` (`0x1000` chacun) également en `.bss` (RAM_D1).
- Buffers SD DBM (`Buffer0`/`Buffer1`) sont en section `.ram_d1`.
- Le buffer UI display est déjà en `.sdram` (`0x400`).
- `_Min_Stack_Size = 0x400` et `_Min_Heap_Size = 0x200` (marge limitée).

### 1.2 Ce qui est risqué

1. **Surcharge RAM_D1**
   - Presque tout est dans un seul domaine mémoire ; peu de marge d'évolution.

2. **Granular trop monolithique**
   - `g_state` regroupe metadata HOT + historiques longs. Les historiques longs ne sont pas séparés.

3. **DMA audio pas isolé pour D-Cache futur**
   - Pas de section dédiée DMA dans RAM_D2 non-cacheable.
   - Risque de cohérence cache si D-Cache activé plus tard.

4. **Heap présent malgré contrainte "no malloc audio"**
   - Newlib/`_sbrk` présent ; usage accidentel possible hors discipline stricte.

5. **Stack minimale pour projet en croissance**
   - 1 KiB peut devenir insuffisant en cas de complexité accrue (IRQ + appels HAL imbriqués).

### 1.3 Risque déterminisme temps réel

- Le hard real-time est surtout menacé si:
  - buffers DMA partagés CPU/DMA deviennent cacheables sans maintenance,
  - ou si des accès SDRAM pénètrent le chemin HOT per-sample sans contrôle.

---

## 2) Architecture mémoire proposée (HOT/WARM/COLD)

| Classe | Description | Mémoire cible | Exemples concrets |
|---|---|---|---|
| HOT | IRQ/per-sample critique | **DTCM** (+ un peu RAM_D1 si besoin) | petites states biquads/sat, variables de mix, compteurs IRQ |
| WARM | per-block DSP / structures actives | **RAM_D1** | états d'effets actifs, routing, tables petites |
| DMA | buffers DMA CPU↔périph | **RAM_D2** | SAI RX/TX ping-pong, SDMMC DBM, autres buffers DMA |
| CTRL | contrôle bas débit / flags | **RAM_D3** | flags UI↔audio, compteurs diagnostics, snapshots |
| COLD | UI + gros historiques | **SDRAM** | framebuffer UI, longs delays, history granular |

### Règles fortes

- **Jamais** de buffer DMA en DTCM.
- **Jamais** de malloc/free dans le chemin audio IRQ.
- Les accès SDRAM dans IRQ audio doivent rester exceptionnels et profilés.

---

## 3) Stratégie linker (compatible CubeMX)

### 3.1 Sections ajoutées

- `.dtcm_audio`  → DTCMRAM
- `.ram_d1_audio` → RAM_D1 (ou RAM_EXEC sur script RAM)
- `.ram_d2_dma`  → RAM_D2
- `.ram_d3_ctrl` → RAM_D3
- `.sdram_ui` → SDRAM
- `.sdram_audio_cold` → SDRAM

### 3.2 Compatibilité CubeMX

- Les ajouts sont faits de manière incrémentale dans les `.ld` existants.
- En pratique: conserver les sections dans une zone "USER MEMORY SECTIONS" pour faciliter les régénérations/merges.
- Scripts modifiés:
  - `STM32H743IITX_FLASH.ld`
  - `STM32H743IITX_RAM.ld`

---

## 4) Conventions code (macros d’annotation)

Header proposé: `Inc/memory_layout.h`

- `AUDIO_HOT` → `.dtcm_audio`
- `AUDIO_WARM` → `.ram_d1_audio`
- `DMA_BUFFER` → `.ram_d2_dma` + alignement 32 bytes
- `CTRL_STATE` → `.ram_d3_ctrl`
- `UI_SDRAM` → `.sdram_ui`
- `AUDIO_COLD_SDRAM` → `.sdram_audio_cold`

### Convention d’équipe (simple)

1. Toute nouvelle donnée globale doit être classée HOT/WARM/DMA/CTRL/COLD.
2. Tout buffer DMA doit utiliser `DMA_BUFFER` et être multiple de 32 bytes si possible.
3. Les gros buffers d’historique (delay/granular/reverb) vont en `AUDIO_COLD_SDRAM` par défaut.
4. Les états per-sample restent en HOT/WARM interne.

---

## 5) DMA + D-Cache (préparation sûre)

### 5.1 Principe

- Quand D-Cache sera activé, on veut:
  - Région DMA (RAM_D2) **non-cacheable** via MPU, **ou**
  - régions cacheables + maintenance stricte (`Clean`/`Invalidate`) avant/après DMA.

### 5.2 Recommandation pragmatique

1. Réserver `.ram_d2_dma` pour tous les buffers DMA dès maintenant.
2. À l’activation D-Cache:
   - configurer MPU RAM_D2 DMA en non-cacheable (approche la plus robuste),
   - garder RAM_D1/DTCM cacheables/perf.
3. Garder alignement 32 bytes systématique pour buffers DMA.

---

## 6) Architecture système d’effets (pool statique sans malloc)

## Objectif

- Effets activables/désactivables dynamiquement,
- Zéro allocation dynamique runtime,
- RAM consommée uniquement par slots réellement activés.

### 6.1 Design recommandé

- **Registry statique** des types d’effets (filter/disto/send FX/etc.).
- **Pool d’instances** fixe par classe:
  - `insert_pool[N_INSERT_MAX]`
  - `send_pool[N_SEND_MAX]`
- Chaque slot contient:
  - état léger HOT/WARM,
  - pointeur/offset vers buffer cold si requis,
  - flag `active`, `type`, `owner_bus`.

### 6.2 Allocation sans malloc

- Activation effet:
  - scanner slot libre,
  - marquer actif,
  - initialiser état,
  - réserver bloc dans un **arena statique** (si besoin de mémoire cold).
- Désactivation:
  - marquer inactif,
  - remettre bloc arena dans free-list déterministe.

### 6.3 Arena statique conseillée

- `AUDIO_COLD_SDRAM uint8_t fx_cold_arena[SIZE];`
- allocator simple à blocs fixes (classes 1K/4K/16K par exemple), O(1) ou O(n) borné petit.
- Aucune fragmentation non bornée type malloc généraliste.

---

## 7) Plan de migration incrémental (prioritaire)

1. **Étape 0 — instrumentation**
   - Conserver mesures CPU load IRQ + watermark stack + stats underrun.

2. **Étape 1 — préparer le linker et macros**
   - Introduire sections custom + `memory_layout.h` (fait).
   - Aucun déplacement critique immédiat.

3. **Étape 2 — isoler DMA non-audio d’abord**
   - Déplacer SD DBM (`Buffer0/Buffer1`) vers `DMA_BUFFER`.
   - Valider débit SD + stabilité audio.

4. **Étape 3 — déplacer DMA audio**
   - Déplacer `rx_buffer`/`tx_buffer` en `.ram_d2_dma`.
   - Vérifier latence ISR et absence de glitch.

5. **Étape 4 — split granular**
   - Garder metadata/grains en HOT/WARM interne.
   - Migrer uniquement historiques longs L/R vers `AUDIO_COLD_SDRAM`.
   - Tester presets worst-case (density max, freeze, pitch extrêmes).

6. **Étape 5 — activer D-Cache proprement**
   - Ajouter MPU pour RAM_D2 DMA non-cacheable.
   - Revalider audio + SD + UI.

7. **Étape 6 — architecture effets finale**
   - Introduire pools statiques insert/send.
   - Activer/désactiver effets via slots uniquement.

### Critères de validation à chaque étape

- 0 glitch audible sur test de charge prolongé.
- CPU IRQ max en dessous du budget bloc.
- Aucun xrun/underrun DMA.
- Stack watermark avec marge confortable.

