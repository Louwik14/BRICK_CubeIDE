# Audit mémoire — BRICK6_CUBE_fonctionnel (STM32H743, audio IRQ)

## 1) Cartographie mémoire actuelle

### Linker / régions mémoire effectivement déclarées
- `FLASH` : `0x08000000`, 2 MiB.
- `DTCMRAM` : `0x20000000`, 128 KiB.
- `RAM_D1` (AXI SRAM) : `0x24000000`, 512 KiB.
- `RAM_D2` : `0x30000000`, 288 KiB.
- `RAM_D3` : `0x38000000`, 64 KiB.
- `ITCMRAM` : `0x00000000`, 64 KiB.

Dans le binaire Release, les sections `data`, `bss`, `.ram_d1`, heap/stack sont toutes placées en `RAM_D1` (AXI SRAM), pas en DTCM.

### Occupation observée (Release/ELF)
- `.data` : 168 B.
- `.bss` : 398 316 B.
- `.ram_d1` : 8 192 B.
- `._user_heap_stack` : 1 536 B (heap min 512 B + stack min 1 024 B).

=> Occupation statique en RAM_D1 ≈ 408 KiB avant stack d’exécution réelle.

### Buffers audio bloc / DMA
- DMA ping-pong RX/TX audio:
  - `rx_buffer` = 4096 B.
  - `tx_buffer` = 4096 B.
- Ces buffers sont en `.bss` (RAM_D1).
- Le traitement est bien déclenché dans les callbacks DMA RX half/full (`HAL_SAI_RxHalfCpltCallback` / `HAL_SAI_RxCpltCallback`).

### Buffers moteur audio / tracks
- `tracks` (3 tracks stéréo de 64 samples float) : 0x60C = 1548 B.
- Bus internes MAIN/CUE : 4 x 0x100 = 1024 B.
- État EQ/saturation : taille marginale.

### Buffers volumineux non audio IRQ immédiat
- État granular principal :
  - `(anonymous namespace)::g_state` = `0x5DDEC` = **384 492 B**.
  - Contient notamment `buffer_l[48000]` + `buffer_r[48000]` float (≈ 2 x 192 KiB).
- Ring buffer SD audio (`sd_audio_block_ring`) : `0x400C` = **16 396 B**.
- Buffers SD stream en section `.ram_d1` :
  - `Buffer0` = 4096 B.
  - `Buffer1` = 4096 B.

### Stack
- Le linker réserve uniquement `_Min_Stack_Size = 0x400` (1 KiB) dans `._user_heap_stack`.
- Le fichier `.su` indique une stack locale modérée dans la voie audio (`audio_process_block_int32` ~88 B), mais des fonctions HAL/SDRAM hors IRQ montent bien plus haut (jusqu’à ~240 B local + profondeur d’appel).

### SDRAM
- SDRAM FMC initialisée (`MX_FMC_Init`, `SDRAM_Init`, commandes init + refresh).
- Allocateur linéaire SDRAM présent (`SDRAM_Alloc`), sans `free`.
- Dans l’image Release actuelle, les buffers de test SDRAM (`sdram_tx_buffer`, `sdram_rx_buffer`) apparaissent à taille 0 (non retenus par l’édition de liens optimisée), donc pas de pression RAM depuis ces tests actuellement.

---

## 2) Problèmes identifiés

1. **Concentration RAM sur AXI SRAM (RAM_D1)**
   - Le design actuel n’exploite pas DTCM/D2/D3 pour les données applicatives critiques.
   - Toute la pression mémoire est sur un seul domaine.

2. **Goulot principal = granular state (~384 KiB)**
   - Le seul `g_state` granular consomme ~75 % des 512 KiB AXI SRAM.
   - Il limite fortement la marge pour extensions FX, USB/audio, logs, sécurité stack.

3. **Marge stack réelle difficile à garantir**
   - Réservation linker minimale 1 KiB seulement.
   - En contexte no-RTOS, IRQ imbriquées possibles + appels HAL non triviaux hors audio.

4. **Présence de heap/newlib malgré politique “pas de malloc en runtime audio”**
   - `_sbrk` et `_malloc_r` sont présents (newlib), donc techniquement accessibles.
   - Risque d’usage accidentel via libs si garde-fous non stricts.

5. **Réverb présente dans le code source mais non active en image Release**
   - Le module existe (objet avec état statique très grand ~0x19018), mais n’est pas visible dans les symboles finaux de l’ELF Release.
   - Risque de surprise mémoire si ré-activée sans plan d’emplacement.

---

## 3) Classification des données

### 🔴 Critique temps réel (à garder en mémoire interne rapide)
- Buffers DMA audio RX/TX ping-pong (`rx_buffer`, `tx_buffer`).
- Buffers bloc de calcul courant (`tracks`, bus MAIN/CUE, états DSP par-sample).
- Variables de contrôle touchées dans l’IRQ audio (compteurs, pointeurs, paramètres actifs).
- Code IRQ audio (`audio_process_block_int32`, callbacks DMA).

### 🟡 Important mais tolérant (optimisable, selon profilage)
- États DSP “petits à moyens” accédés par bloc (EQ, saturation, mix/routage).
- Ring buffer SD audio (pipeline non directement hard-deadline sample, mais contribue au flux).
- Buffers SD stream (actuellement en `.ram_d1`), dépendants du mode d’exploitation.

### 🔵 Volumineux non critique immédiat (candidats SDRAM)
- Buffers longs de FX temporels > plusieurs dizaines de millisecondes/secondes:
  - Granular circular buffers longs (actuellement 2 x 48000 float).
  - Futures lignes de delay/reverb de grande taille.
- Buffers de préchargement / cache médias / playback non directement dans la boucle sample stricte.

---

## 4) Analyse ciblée FX lourds

### Grain / granular (`fx_granular.cpp`)
- Taille buffer:
  - `buffer_l[48000]` + `buffer_r[48000]` float ≈ 384 KiB à eux seuls.
- Pattern d’accès:
  - Écriture séquentielle (write head circulaire) par sample.
  - Lectures interpolées quasi-aléatoires selon grains (positions et offsets stéréo), donc localité partielle.
- Fréquence d’accès:
  - Dans la boucle `for n in frames` => accès **par sample** en IRQ.
  - Multiples lectures additionnelles par grain actif.
- Conclusion:
  - C’est le principal candidat au découplage mémoire, mais aussi le plus risqué côté latence car très hot-path.

### Delay
- Pas de module delay dédié actif explicite dans `Src/` au moment de l’audit.
- Le besoin architectural reste identique : gros buffers temporels sont naturellement candidats SDRAM si accès tolérables.

### Reverb (`fx_reverb.*`, freeverb)
- Implémentation présente avec buffers internes statiques dans `revmodel` (combs/allpass).
- Taille théorique notable (~100 KiB ordre de grandeur) si instanciée.
- Mais l’ELF Release courant ne montre pas de symbole reverb en RAM finale, suggérant non-utilisation / élimination à l’édition de liens.
- Si activée, son empreinte devra être planifiée explicitement.

---

## 5) Risques SDRAM (hard real-time IRQ)

1. **Latence et jitter d’accès**
   - SDRAM externe via FMC = latence supérieure + variabilité (refresh, arbitrage bus).
   - Accès aléatoires (granular) accentuent la pénalité vs accès séquentiels.

2. **Effets cache / cohérence**
   - Le projet active I-Cache seulement, pas D-Cache globalement.
   - Sans D-Cache: latence brute SDRAM plus élevée.
   - Avec D-Cache futur: risques de cohérence DMA/CPU (clean/invalidate, alignement, MPU attributes) si buffers DMA ou partagés y sont placés.

3. **Casse audio en IRQ**
   - Déplacer sans discernement des données “hot per-sample” en SDRAM peut dépasser le budget bloc (64 samples @48 kHz = 1.333 ms).
   - Les pires cas (densité grains élevée + lectures interpolées) sont les scénarios de glitch à surveiller.

4. **Fausse sécurité de capacité**
   - SDRAM apporte de l’espace mais pas de garanties temporelles intrinsèques.
   - Une migration “tout en SDRAM” sans partition critique/non-critique peut dégrader fortement la robustesse IRQ.

---

## 6) Recommandations stratégiques (sans implémentation)

1. **Principe directeur: partition mémoire par criticité temporelle**
   - Interne (RAM rapide) pour “hot set IRQ” strict.
   - SDRAM pour “cold/large set” et historiques longs.

2. **Ce qui doit rester en interne absolument**
   - DMA ping-pong audio RX/TX.
   - Buffers bloc 64 samples en traitement courant.
   - États DSP micro (coeffs, accumulateurs, variables de contrôle par-sample).
   - Structures de pilotage IRQ, flags et compteurs timing.

3. **Ce qui peut migrer vers SDRAM en priorité**
   - Buffers temporels longs (granular history, delays longs, reverb tails).
   - Buffers de streaming/media non dans la boucle sample critique.
   - Tout buffer > quelques KiB dont accès n’est pas strictement “chaud par sample”.

4. **Granular: stratégie prudente recommandée**
   - Ne pas déplacer d’un bloc tout l’état aveuglément.
   - Séparer “metadata grains + write/read heads + paramètres” (interne) et “historique audio long” (SDRAM) si budget le permet.
   - Vérifier systématiquement le pire cas densité/pitch/freeze avant validation.

5. **Réverb/Delay futurs**
   - Prévoir nativement une classe “long lines in SDRAM / short states in SRAM interne”.
   - Favoriser accès séquentiels/burst côté SDRAM quand possible.

6. **Gouvernance déterministe**
   - Conserver la règle zéro allocation dynamique en runtime IRQ.
   - Autoriser uniquement des allocations statiques ou un allocateur linéaire d’init (avant audio start).
   - Interdire explicitement toute API `malloc/new` dans le chemin audio (garde-fou build/review).

7. **Sécurité stack / marge opérationnelle**
   - Rehausser la marge de stack réservée (stratégie), puis mesurer watermark en conditions réelles.
   - Objectif: marge robuste sous pics IRQ + diagnostics actifs.

---

## 7) Décisions d’architecture proposées (phase audit)

- Décision A: **Conserver tout le cœur bloc/IRQ en mémoire interne.**
- Décision B: **Utiliser la SDRAM pour les buffers volumineux de temporalité longue uniquement.**
- Décision C: **Traiter le granular comme priorité #1 de désengorgement RAM, avec migration partielle contrôlée.**
- Décision D: **Ne pas activer/étendre de FX lourds supplémentaires (reverb/delay longs) tant que la partition mémoire cible n’est pas figée.**
- Décision E: **Maintenir une politique stricte déterministe: pas de malloc/free runtime audio, pas d’accès SDRAM non borné en IRQ.**

