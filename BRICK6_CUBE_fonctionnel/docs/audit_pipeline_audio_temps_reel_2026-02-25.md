# Audit technique — chaîne audio temps réel (BRICK6_CUBE_fonctionnel)

## Périmètre analysé
- IRQ/DMA/SAI: `Src/Audio/audio.c`, `Src/stm32h7xx_it.c`, `Src/main.c`
- Frontière I/O et moteur bloc: `Src/Audio/audio_float.c`, `Src/Audio/audio_io.c`, `Src/Audio/dsp_engine.c`
- Mix/routing/FX: `Src/Audio/mixer.c`, `Src/Audio/fx_chain.c`, `Src/Audio/fx_pool.c`
- Init/app orchestration: `Src/Core/brick6_app_init.c`
- Contrôles/paramètres: `Src/Param/control_router.c`, `Src/Param/control_events.c`, `Src/Param/param_store.c`
- API/contrats: `Inc/Audio/audio_float.h`, `Inc/Audio/mixer.h`, `Inc/Audio/audio.h`

---

## 1) Pipeline audio réel (ordre d’exécution effectif)

### A. Déclenchement IRQ DMA
1. `DMA1_Stream1_IRQHandler()` appelle `cpu_load_irq_begin()`, puis `HAL_DMA_IRQHandler(&hdma_sai1_b)`, puis `cpu_load_irq_end()`.  
2. HAL déclenche `HAL_SAI_RxHalfCpltCallback()` ou `HAL_SAI_RxCpltCallback()` (dans `audio.c`) selon half/full RX.

### B. Traitement bloc audio (dans IRQ)
3. `process_half(half_index)` calcule l’offset ping-pong puis appelle:
   - `audio_process_block_int32(rx_half, tx_half, 64)`.
4. `audio_process_block_int32()`:
   - incrémente `g_audio_block_counter`;
   - pop jusqu’à 8 événements via `control_event_pop()` (mais sans traitement appliqué);
   - `audio_io_unpack()` (int24 TDM8 -> tracks float).
5. `audio_dsp_process()`:
   - `dsp_engine_process_block(tracks, MAX_TRACKS, frames)`;
   - ce bridge appelle le callback app `my_dsp()` enregistré à l’init.
6. `my_dsp()` appelle `mixer_process()`:
   - inserts FX track (slots), pan/gain/mute,
   - sommation vers `bus_main` et `bus_cue`,
   - sends FX + retour send dans `bus_main`,
   - copie finale `bus_main -> tracks[0]`, `bus_cue -> tracks[1]`.
7. Retour dans `audio_dsp_process()`:
   - applique `master_gain` sur `tracks[0]` -> `bus_main`,
   - applique `master_gain` sur `tracks[1]` -> `bus_cue`.
8. `audio_io_pack()`:
   - applique `out_gain` (output_adjust),
   - pack int24 vers TX slots: MAIN->0/1, CUE->2/3, slots 4..7 = 0.

### C. Post-traitement IRQ
9. Callback RX notifie `engine_tasklet_notify_frames(64)`.

---

## 2) Vérification explicite des doublons DSP / double mix

### ✅ Double mixage (présence de 2 couches de mix/bus)
- **Mix 1 (principal)**: `mixer_process()` somme potentiellement plusieurs tracks + sends FX vers `bus_main/cue`, puis écrit dans `tracks[0/1]`.
- **Mix 2 (frontière audio_float)**: `audio_dsp_process()` reconstruit `bus_main/cue` à partir de `tracks[0/1]` avec `master_gain`.

Conclusion: il existe **bien un double étage de bus/mix logique** (mixer puis re-bus dans `audio_float`), même si le 2e étage est surtout un remap/gain et pas une nouvelle somme multi-tracks.

### ✅ Re-mix après DSP principal
- Oui, après `mixer_process()` (DSP principal), `audio_dsp_process()` refait une étape de bus (track0->main, track1->cue) et gain master.

### 🟡 Gains/FX appliqués plusieurs fois
- **Master gain**: appliqué une seule fois (dans `audio_dsp_process`), piloté via `mixer_set_master()` -> `audio_float_set_master_gain()`. Pas de double master explicite.
- **Output gain (`output_adjust`)**: appliqué ensuite dans `audio_io_pack()`. C’est une deuxième étape de gain globale (staging I/O), volontaire mais cumulative avec master.
- **FX**: appliqués dans `mixer_process()` seulement (inserts/sends) via `fx_chain_process_slot()`; pas de second traitement FX dans `audio_float` actuel.

---

## 3) Responsabilités des modules (rôle réel vs architecture propre)

- `audio.c`: rôle I/O IRQ DMA + dispatch bloc. ✅ Cohérent.
- `audio_io.c`: conversion/pack TDM<->float uniquement. ✅ Cohérent.
- `dsp_engine.c`: simple indirection callback. ✅ Cohérent.
- `mixer.c`: vrai cœur DSP de sommation/routing/FX. ✅ Cohérent pour un moteur track-based.
- `audio_float.c`: frontière format + orchestration, **mais** conserve un sous-mixer/bus (`audio_dsp_process`) qui chevauche la responsabilité de `mixer.c`. ⚠️
- `fx_chain.c` / `fx_pool.c`: routing slot+états statiques FX. ✅ Cohérent.
- `control_router.c`: fait store + binding runtime mixer direct. ⚠️ Couplage transitoire assumé.
- `control_events.c`: queue lock-free OK, mais consommation IRQ actuellement sans application d’événement. ⚠️

---

## 4) Violations / dérives d’architecture

1. **Mix/routing dans plusieurs couches**: `mixer.c` + `audio_float.c` effectuent tous deux une logique de bus MAIN/CUE.
2. **Possession ambiguë des sorties MAIN/CUE**: `mixer_process` calcule déjà MAIN/CUE, puis `audio_float` les recalcule depuis `tracks[0/1]`.
3. **Contrat tracks incohérent**: `MIXER_MAX_TRACKS=4` alors que `MAX_TRACKS=3`; API mixer expose track3 impossible côté audio I/O.
4. **API partiellement morte**: `track_set_gain()` est no-op; le gain track réel est géré seulement dans `mixer.c`.
5. **Événements contrôle consommés puis ignorés** dans le chemin IRQ (`control_event_pop` sans mapping), charge CPU sans effet fonctionnel.
6. **Constantes cadence incohérentes en commentaire**: `engine_tasklet` documente 32 frames/tick, mais audio notifie 64 frames.

---

## 5) Cohérence track-based (ownership/routing)

- **Qui possède les buffers tracks**: `audio_float.c` (statique `tracks[MAX_TRACKS]`).
- **Qui lit/écrit les tracks d’entrée**: `audio_io_unpack()` écrit depuis RX TDM.
- **Qui produit le résultat final logique**:
  - `mixer_process` produit `bus_main/cue` puis les recopie dans `tracks[0/1]`.
  - `audio_dsp_process` convertit `tracks[0/1]` en `bus_main/cue` définitifs pour le pack.
- **Qui décide du routing main/cue/send**: `mixer.c` (routes track, inserts, sends, send FX).
- **Qui écrit le buffer DAC/TX**: `audio_io_pack()` (slots 0..3 utiles).

Lecture architecture: le routing est décidé par mixer, mais la finalisation bus est redondante en frontière `audio_float`.

---

## 6) Temps réel (IRQ)

### Points coûteux / redondants détectés
- `mixer_process()` fait plusieurs `memset` de buffers taille bloc complète + copies `memcpy` vers tracks (coût borné mais non nul à chaque IRQ).
- `audio_dsp_process()` recopie/re-applique gain après mixeur (deuxième passage mémoire sur `frames`).
- `audio_process_block_int32()` dépile 0..8 événements à chaque bloc sans effet.

### Points corrects
- Aucune allocation dynamique dans le chemin IRQ.
- Pipeline bloc borné (64 frames), data statique.
- Conversion pack/unpack simple, linéaire, prédictible.

---

## Synthèse demandée

### 🔴 Problèmes critiques
1. **Double étage de mix/bus MAIN/CUE** (`mixer_process` puis `audio_dsp_process`) => responsabilité du mix final dupliquée.
2. **Re-bus post-DSP systématique** après mixer, augmentant la complexité conceptuelle et le coût mémoire par bloc.

### 🟠 Problèmes moyens
1. **Incohérence capacité tracks** (`MIXER_MAX_TRACKS=4` vs `MAX_TRACKS=3`).
2. **`track_set_gain()` no-op** alors que l’API laisse penser l’inverse.
3. **Événements contrôle poppés sans application** en IRQ.
4. **Commentaires cadence tasklet obsolètes (32 vs 64)**.
5. **Double gain global (master + output_adjust)** potentiellement ambigu en calibration si non documenté côté produit.

### 🟢 Points corrects
1. Pipeline IRQ clair, sans malloc, borné par bloc fixe.
2. Séparation matérielle (`audio.c`) / conversion (`audio_io`) / DSP applicatif (`mixer` via callback) globalement saine.
3. Architecture FX slot-based extensible (insert + send).

---

## Recommandations concrètes (sans refactor complet)

1. **Établir une seule “source de vérité” du bus final**:
   - court terme: documenter explicitement que `mixer_process` doit écrire `tracks[0/1]` et que `audio_dsp_process` ne fait qu’un “output stage”.
2. **Clarifier le gain staging** dans un diagramme unique: `track gain/pan -> routing/sends -> master -> output_adjust`.
3. **Rendre l’API cohérente**:
   - soit implémenter `track_set_gain`, soit marquer l’API deprecated.
4. **Aligner les dimensions tracks** (`MIXER_MAX_TRACKS` vs `MAX_TRACKS`) pour éviter les routes fantômes.
5. **Supprimer le coût inutile IRQ** des `control_event_pop` non appliqués (ou appliquer réellement les événements).
6. **Mettre à jour les commentaires/timing** (64 frames par half IRQ et tick).
