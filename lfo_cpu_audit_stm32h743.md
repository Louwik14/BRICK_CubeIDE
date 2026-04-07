# Audit technique CPU — moteur LFO embarqué (STM32H743, 480 MHz)

## Contexte et budget temps réel

- MCU: STM32H743 @ 480 MHz (Cortex-M7, FPU simple précision).
- Audio: 48 kHz, bloc de 64 échantillons.
- IRQ audio hard real-time, sans RTOS, sans allocation dynamique.
- Charge à auditer: 16 LFO actifs (8 tracks × 2 LFO).
- Deux hypothèses de fréquence LFO max:
  1. **Safe**: 512 Hz
  2. **Extrême tempo-scaled**: 1280 Hz

### Budget CPU disponible

- Cycles/seconde: `480e6`.
- Échantillons/seconde: `48e3`.
- **Budget global par sample**: `480e6 / 48e3 = 10 000 cycles/sample`.
- Blocs/seconde: `48e3 / 64 = 750 blocs/s`.
- **Budget par bloc**: `480e6 / 750 = 640 000 cycles/bloc`.

---

## Hypothèses de coût unitaire (worst-case embarqué réaliste)

Estimation volontairement prudente pour code C/C++ optimisé `-O2/-O3`, données en SRAM/DTCM, sans cache miss pathologique:

- Phase accumulator + wrap: 4–8 cycles.
- Génération forme d’onde:
  - saw/tri/square: 4–10 cycles
  - sine par LUT + interpolation linéaire: 12–24 cycles
  - random S&H (détection front + PRNG): 8–20 cycles (pics au front, plus bas sinon)
- Modes (free/trig/one-shot/hold): 4–12 cycles (branchements + état)
- Calcul modulation (depth, polarité, mapping destination): 8–20 cycles
- Smoothing paramètre (one-pole): 6–14 cycles

### Enveloppe de dimensionnement retenue

Pour majorer le pire cas:

- **Coût par LFO en mode sample-rate**: `60 à 90 cycles/sample/LFO`.
- Valeur de sizing conservatrice utilisée ci-dessous: **80 cycles/sample/LFO**.

---

## 1) Coût CPU par LFO et total pour 16 LFO

### Formule

`CPU% = (N_lfo × rate × cycles_update) / 480e6 × 100`

### Cas A — moteur LFO calculé à 48 kHz (sample-rate)

- `N_lfo = 16`, `rate = 48k`, `cycles_update = 80` (majorant).
- Cycles/s: `16 × 48 000 × 80 = 61,44 Mcycles/s`.
- **CPU ≈ 12,8%**.

Fourchette réaliste avec 60–90 cycles:
- bas: `9,6%`
- haut: `14,4%`

> Ce coût est essentiellement indépendant de 512 vs 1280 Hz si on met à jour à chaque sample.

### Cas B — moteur à control-rate (update événementiel)

Si un LFO est évalué à sa fréquence propre max:

- 512 Hz: `16 × 512 = 8 192 updates/s`
- 1280 Hz: `16 × 1280 = 20 480 updates/s`

Avec `50 cycles/update`:
- 512 Hz: `0,085% CPU`
- 1280 Hz: `0,213% CPU`

Le coût dominant devient alors l’application sample-rate de la modulation (interpolation/smoothing), pas le calcul brut de l’oscillateur.

---

## 2) Coût si calcul par sample

En dimensionnement conservateur:

- **Par LFO**: 60–90 cycles/sample.
- **16 LFO**: 960–1440 cycles/sample.
- Sur budget 10 000 cycles/sample: **9,6% à 14,4% CPU**.

Conclusion: acceptable sur H743 si DSP principal n’est pas déjà saturé, mais non négligeable.

---

## 3) Coût si calcul par bloc + interpolation/rampe intra-bloc

### Option “1 update/bloc” (750 Hz)

- Coût CPU très bas, mais **insuffisant** pour 1280 Hz (et limite pour 512 Hz en fidélité).
- Provoque zipper/alias de modulation rapide.
- **Non recommandé** pour un comportement type Elektron rapide.

### Option hybride recommandée

- LFO évalué à **control-rate fixe** (ex. 3 kHz, soit pas de 16 samples).
- Application sample-rate via rampe/interpolation linéaire.

Coût ordre de grandeur (16 LFO):
- Évaluation LFO à 3 kHz: `16 × 3000 × 50 = 2,4 Mcycles/s` (~0,5%)
- Interpolation + accumulateurs de destination sample-rate: ~80–240 cycles/sample global (selon nombre de destinations réellement actives)
  - soit ~0,8% à 2,4%

Total typique: **~1,3% à 3% CPU**.

---

## 4) Coût des formes d’onde

Par update (ordre de grandeur):

- **Square**: 3–6 cycles
- **Saw**: 4–8 cycles
- **Tri**: 6–10 cycles
- **Sine (LUT+interp)**: 12–24 cycles
- **Random S&H**: 8–20 cycles (pics sur front)

Worst-case global: considérer **sine + S&H + logique mode** coexistante en branchements défavorables => majorant 30–40 cycles pour la partie “wave+mode”.

---

## 5) Coût des modes free / trig / one-shot / hold

- **free**: coût minimal (phase continue).
- **trig**: reset de phase sur événement note/trig (coût ponctuel, faible).
- **one-shot**: condition d’arrêt + latch (branchements additionnels).
- **hold**: sortie figée, calcul simplifié mais logique d’état.

Surcharge moyenne: **+4 à +12 cycles/update/LFO** en conception branchless/peu branchée. Le vrai risque est la variabilité timing si branches non uniformes; à traiter avec chemins bornés et sans appels dynamiques.

---

## 6) Coût destination modulation vers paramètres audio

Point critique en pratique.

- Mapping source→destination (index fixe, table statique): 2–5 cycles.
- Application depth/polarité/offset: 6–12 cycles.
- Accumulation multi-sources par paramètre: 4–10 cycles.

Si 16 LFO modulent 16 paramètres distincts, coût reste linéaire et prévisible.
Si plusieurs LFO convergent vers peu de paramètres, prévoir accumulateurs dédiés pour éviter contention/branches.

---

## 7) Coût smoothing paramètre côté DSP

Un one-pole par paramètre modulé:

`y += a * (x - y)`

- 2 soustractions/additions + 1 multiplication + loads/stores.
- **~6–14 cycles/sample/paramètre**.

Exemple 16 paramètres modulés en continu:
- ~96 à 224 cycles/sample
- soit **~1,0% à 2,2% CPU**.

---

## 8) Coût mémoire et structures runtime

Structure LFO sans allocation dynamique, SoA ou AoS compact:

- phase, inc, value, target, step, depth, fade, flags, mode, wave, dest, counters, rng_state.
- En pratique: **48–80 octets/LFO** (alignement inclus).

Pour 16 LFO:
- **~0,8 à 1,3 KB**

Tables complémentaires:
- LUT sine 1024 float: 4 KB (ou Q31: 4 KB)
- tables SPD/MULT pré-calculées: < 1 KB

Total moteur: typiquement **< 8 KB**.

---

## 9) Impact sur pire cas IRQ audio

Rappel budget IRQ par bloc: 640 000 cycles.

- Stratégie sample-rate pure (majorant 12,8% CPU):
  - ~81 920 cycles/bloc consommés par LFO.
- Stratégie hybride (~1,3–3% CPU):
  - ~8 300 à 19 200 cycles/bloc.

Même le cas sample-rate reste souvent tenable sur H743, mais réduit la marge pour:
- oscillateurs/filters voix,
- effets,
- UI/MIDI dans interruptions secondaires.

La stratégie hybride améliore fortement la marge IRQ worst-case et la déterminisme (charge bornée mieux contrôlée).

---

## 10) Recommandation d’architecture (V1 stable et scalable)

### Reco principale: **hybride control-rate + interpolation sample-rate**

1. **Phase accumulator fixe** par LFO (Q32 ou float), sans trig transcendante runtime.
2. **Évaluation onde à control-rate fixe** `f_ctrl = 3 kHz` (pas 16 samples), ce qui couvre 1280 Hz avec marge Nyquist.
3. **Interpolation linéaire intra-pas** pour sortie sample-rate continue.
4. **Destination routing statique** (index prévalidé), sans conteneurs dynamiques.
5. **Smoothing one-pole** par paramètre modulé côté DSP sample-rate.
6. **Tables pré-calculées** SPD/MULT→phase_inc (tempo aware) mises à jour au changement tempo/paramètre, pas par sample.
7. **Chemins bornés**: pas d’allocation, pas de virtual dispatch, pas d’I/O dans IRQ.

### LFO sample-rate vs control-rate

- **Sample-rate pur**: simple, robuste qualité, mais 10–15% CPU pour 16 LFO (non négligeable).
- **Control-rate pur bas (≤750 Hz)**: trop faible pour 1280 Hz.
- **Hybride 3 kHz + interp**: meilleur compromis qualité/CPU/déterminisme.

### Table lookup vs phase accumulator vs approximation

- **Phase accumulator + LUT sine**: recommandé (coût prévisible, stable).
- Approx polynomiale sine: possible, souvent plus coûteuse/variable que LUT sur M7.
- Tri/saw/square: directement depuis phase (ultra faible coût).

---

## Verdict final (16 LFO max-rate sur STM32H743)

- En **sample-rate pur**, 16 LFO max-rate sont **acceptables mais non négligeables** (ordre de 10–15% CPU).
- En **architecture hybride control-rate + interpolation**, ils deviennent **clairement négligeables à modérés** (~1–3% CPU), avec meilleure marge IRQ.
- Ils ne sont **pas dangereux** sur H743 si l’implémentation reste déterministe (tables, états statiques, pas d’allocation, chemins bornés), même en hypothèse extrême 1280 Hz.

## Classification demandée

- Hypothèse 512 Hz: **négligeable à acceptable** (selon architecture).
- Hypothèse 1280 Hz: **acceptable** en hybride, **acceptable mais à surveiller** en sample-rate pur.
