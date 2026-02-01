# Audit complet — chaîne audio BRICK6 (état actuel)

> **Objectif** : cartographier la chaîne audio telle qu’implémentée dans le firmware, sans proposer de correction ni d’hypothèse de design. Toutes les observations ci‑dessous proviennent des fichiers listés et cités.

---

## 1️⃣ Vue d’ensemble — Architecture audio actuelle

### Schéma logique (flux audio)

```
USB (TinyUSB UAC1 RX)
   ↓
 audio_io_usb → audio_core → audio_out (SAI1 TX TDM8) → CS42448 DAC
                         ↘︎
                          audio_test_pcm5100a (SAI2 TX I2S) → PCM5100A

SAI1 RX TDM8 (CS42448 ADC) → audio_in → audio_core (fallback pass-through)
```

### Rôle global des sous‑systèmes

- **USB audio (TinyUSB)** : reçoit des frames audio USB (RX), convertit en `int32_t` 24‑bit (shift de 8), pousse dans `audio_io_usb`. Il prépare aussi le flux USB TX (1 ms) via `audio_io_usb_prepare_tx()` et l’envoie au host. Le traitement est cadencé par `HAL_GetTick()` (1 ms).【F:Src/tinyusb_app.c†L66-L146】【F:Src/tinyusb_app.c†L196-L244】
- **audio_core** : orchestre la production d’un bloc audio de 256 frames × 8 canaux (`int32_t`) depuis USB/SD ou mix, avec fallback vers la dernière copie d’entrée SAI (`core_input_copy`). Il est appelé par `audio_out` et `audio_test_pcm5100a`.【F:Src/audio_core.c†L33-L123】【F:Inc/audio_core.h†L7-L18】
- **audio_out (SAI1 TDM8)** : maintient le buffer DMA de sortie, reçoit les callbacks DMA half/full, et déclenche `audio_tasklet_poll()` qui demande un bloc à `audio_core` puis notifie l’engine. C’est le chemin principal vers CS42448 (SAI1 Block A).【F:Src/audio_out.c†L14-L45】【F:Src/audio_out.c†L242-L295】
- **audio_in (SAI1 RX TDM8)** : reçoit le DMA d’entrée (SAI1 Block B), expose le dernier demi‑buffer complet, et copie ce bloc vers `audio_core` via `AudioIn_TaskletPoll()` (fallback pass‑through).【F:Src/audio_in.c†L14-L49】【F:Src/audio_in.c†L138-L189】
- **audio_test_pcm5100a (SAI2 I2S)** : chemin de test conditionnel (`AUDIO_TEST_PCM5100A`) qui prend des blocs `audio_core`, garde les canaux 0/1, et envoie en stéréo via DMA SAI2 Block A. Le remplissage est déclenché par les callbacks DMA half/full et traité en tasklet. 【F:Src/audio_test_pcm5100a.c†L6-L110】

### Producteur / consommateur / cadenceur (factuel)

- **Producteurs audio** :
  - USB RX via `tud_audio_rx_done_isr()` → `audio_io_usb_on_rx_samples()` (int16 → int32).【F:Src/tinyusb_app.c†L196-L244】
  - SAI1 RX via DMA (SAI1 Block B) → `AudioIn_ProcessHalf/Full()` → `AudioIn_TaskletPoll()` → `audio_core_on_input_block()`.【F:Src/audio_in.c†L67-L189】
- **Consommateurs audio** :
  - SAI1 TX via `audio_out` (CS42448 DAC) et `audio_core_process_block()` dans `audio_tasklet_poll()`.【F:Src/audio_out.c†L242-L295】
  - SAI2 TX via `audio_test_pcm5100a` (PCM5100A test) et `audio_core_process_block()` dans `pcm5100a_fill_half()`.【F:Src/audio_test_pcm5100a.c†L23-L50】
- **Cadenceurs** :
  - **Cadence audio principale** : callbacks DMA SAI1 TX half/full déclenchent `AudioOut_ProcessHalf/Full()` puis `audio_tasklet_poll()` remplit les blocs et notifie l’engine. 【F:Src/audio_out.c†L206-L295】
  - **Cadence test PCM5100A** : callbacks DMA SAI2 TX half/full déclenchent des flags, consommés dans `audio_test_pcm5100a_tasklet_poll()`.【F:Src/audio_test_pcm5100a.c†L69-L105】
  - **Cadence USB** : `audio_task()` se déclenche à 1 ms (Tick HAL).【F:Src/tinyusb_app.c†L89-L146】

---

## 2️⃣ Initialisation — Séquence exacte au boot

Ordre d’initialisation dans `brick6_app_init.c` :

1. Logs + init SDRAM + test SDRAM + test alloc SDRAM.【F:Src/brick6_app_init.c†L33-L41】
2. Init SD stream + TinyUSB + USB host :
   - `diagnostics_on_sd_stream_init(sd_stream_init(&hsd1))`
   - `tusb_init()`
   - `tinyusb_app_init()`
   - `MX_USB_HOST_Init()`【F:Src/brick6_app_init.c†L43-L46】
3. Init audio SAI :
   - `AudioOut_Init(&hsai_BlockA1)`
   - `AudioIn_Init(&hsai_BlockB1)`
   - `audio_test_pcm5100a_init(&hsai_BlockA2)` (si `AUDIO_TEST_PCM5100A`)【F:Src/brick6_app_init.c†L48-L52】
4. Init engine tasklet : `engine_tasklet_init(AUDIO_OUT_SAMPLE_RATE)`.【F:Src/brick6_app_init.c†L54-L54】
5. Démarrage audio :
   - `AudioOut_Start()` → lance DMA TX sur SAI1 Block A.
   - `audio_test_pcm5100a_start()` (si activé) → lance DMA TX sur SAI2 Block A.
   - `HAL_SAI_Receive_DMA(&hsai_BlockB1, ...)` → démarre DMA RX SAI1 Block B.【F:Src/brick6_app_init.c†L56-L62】
6. `HAL_Delay(200)` puis fin de init (MIDI commenté).【F:Src/brick6_app_init.c†L64-L67】

**Qui démarre en premier ?** D’après l’ordre des appels, **SAI1 TX** démarre avant **SAI2 TX** (si activé) et avant **SAI1 RX**.【F:Src/brick6_app_init.c†L56-L62】

**Source de cadence audio** : côté sortie principale, la cadence provient des callbacks DMA TX de SAI1 Block A, qui signalent à `audio_tasklet_poll()` de remplir le demi‑buffer correspondant.【F:Src/audio_out.c†L206-L295】

---

## 3️⃣ audio_core — Rôle et contrat

- **Taille logique d’un bloc audio** : 256 frames × 8 canaux (`AUDIO_CORE_FRAMES_PER_BLOCK`, `AUDIO_CORE_CHANNELS`).【F:Inc/audio_core.h†L7-L18】
- **Cadence** : appelée une fois par demi‑buffer DMA (256 frames) dans `audio_tasklet_poll()` pour SAI1, et par demi‑buffer DMA dans `pcm5100a_fill_half()` pour SAI2. 【F:Src/audio_out.c†L242-L295】【F:Src/audio_test_pcm5100a.c†L23-L50】
- **Rôle** : sélectionner la source (`routing_get_source()`), lire les buffers USB/SD si disponibles, mixer ou fallback sur la dernière entrée SAI (`core_input_copy`).【F:Src/audio_core.c†L58-L123】
- **Contrat** : `audio_core_process_block(out, frames)` écrit `frames × 8` échantillons `int32_t` dans `out` et ne dépasse pas 256 frames. L’entrée SAI est capturée via `audio_core_on_input_block()` qui copie le dernier bloc reçu.【F:Src/audio_core.c†L45-L136】

---

## 4️⃣ Sorties audio — Analyse séparée

### 🔹 SAI1 / CS42448 (référence fonctionnelle)

- **Mode SAI1** :
  - Block A : Master TX, protocole libre, 24‑bit, TDM 8 slots × 32‑bit, 48 kHz. (FrameLength 256, SlotNumber 8, SlotActive 0xFF).【F:Src/sai.c†L50-L88】
  - Block B : Slave RX synchrone sur Block A, 24‑bit, TDM 8 slots × 32‑bit, SlotActive 0x3F (6 canaux).【F:Src/sai.c†L89-L128】
- **Buffer utilisé** : `audio_out_buffer` (int32) dimensionné `AUDIO_OUT_BUFFER_SAMPLES` (= 2 × 256 frames × 8 slots).【F:Src/audio_out.c†L52-L57】【F:Inc/audio_out.h†L12-L23】
- **Qui remplit** : `audio_tasklet_poll()` appelle `audio_core_process_block()` pour remplir les demi‑buffers (half puis full).【F:Src/audio_out.c†L242-L287】
- **Cadence / déclenchement** : DMA TX circulaire sur SAI1 Block A → callbacks `HAL_SAI_TxHalfCpltCallback` / `HAL_SAI_TxCpltCallback` → flags (`audio_dma_half_ready/full_ready`) → `audio_tasklet_poll()` remplit et notifie l’engine. 【F:Src/audio_out.c†L206-L295】
- **Pourquoi “fonctionnel” dans l’état actuel (factuel)** : ce chemin est entièrement câblé : initialisation SAI1 + DMA, démarrage DMA TX, callbacks SAI1 et remplissage `audio_out_buffer` par `audio_core` à cadence DMA. 【F:Src/brick6_app_init.c†L48-L62】【F:Src/sai.c†L50-L88】【F:Src/audio_out.c†L186-L295】

### 🔹 SAI2 / PCM5100A (chemin de test)

- **Mode SAI2** : SAI2 Block A en master TX, protocole I2S standard, 24‑bit, 2 canaux, 48 kHz, FIFO full. 【F:Src/sai.c†L133-L167】
- **Source des samples** : `audio_core_process_block()` produit un bloc de 256 frames × 8 canaux ; `pcm5100a_fill_half()` copie uniquement les canaux 0/1 vers un buffer stéréo (`pcm5100a_tx_buffer`).【F:Src/audio_test_pcm5100a.c†L23-L50】
- **Buffer utilisé** : `pcm5100a_tx_buffer` (int32) dimensionné `AUDIO_TEST_PCM5100A_BUFFER_SAMPLES` (= 2 × 256 frames × 2 canaux).【F:Src/audio_test_pcm5100a.c†L17-L21】【F:Inc/audio_test_pcm5100a.h†L11-L16】
- **Callbacks DMA** : les callbacks SAI TX (déclarés dans `audio_out.c`) appellent `audio_test_pcm5100a_on_tx_half/full()` ; ceux‑ci vérifient `hsai->Instance == SAI2_Block_A` avant de lever les flags. 【F:Src/audio_out.c†L206-L240】【F:Src/audio_test_pcm5100a.c†L69-L89】
- **Ce que le DMA “prouve”** : les compteurs `AudioTest_PCM5100A_GetTxHalf/FullCount()` sont incrémentés uniquement lorsqu’une IRQ SAI2 TX half/full se produit. Cela atteste de l’activité DMA/IRQ sur SAI2 mais ne décrit pas l’état du signal analogique en sortie. 【F:Src/audio_test_pcm5100a.c†L69-L115】

---

## 5️⃣ DMA & IRQ — Chaîne d’événements réelle

### Chaîne SAI1 (TDM8)

1. DMA circulaire sur SAI1 Block A (TX) → IRQ half/full. 【F:Src/sai.c†L140-L207】
2. `HAL_SAI_TxHalfCpltCallback` / `HAL_SAI_TxCpltCallback` → `AudioOut_ProcessHalf/Full()` → flags `audio_dma_half_ready/full_ready`.【F:Src/audio_out.c†L206-L240】
3. `audio_tasklet_poll()` consomme les flags, appelle `audio_core_process_block()` pour remplir la moitié correspondante, puis notifie l’engine (`engine_tasklet_notify_frames`).【F:Src/audio_out.c†L242-L287】

### Chaîne SAI1 (RX)

1. DMA circulaire sur SAI1 Block B (RX) → IRQ half/full. 【F:Src/sai.c†L170-L234】
2. `HAL_SAI_RxHalfCpltCallback` / `HAL_SAI_RxCpltCallback` → `AudioIn_ProcessHalf/Full()` met à jour `audio_in_latest_half` et marque `audio_in_has_block`.【F:Src/audio_in.c†L67-L128】
3. `AudioIn_TaskletPoll()` (appelée ailleurs dans la boucle principale) copie le demi‑buffer courant vers `audio_core_on_input_block()`.【F:Src/audio_in.c†L138-L189】

### Chaîne SAI2 (I2S test)

1. DMA circulaire sur SAI2 Block A (TX) → IRQ half/full. 【F:Src/sai.c†L260-L324】
2. Callbacks TX (audio_out) → `audio_test_pcm5100a_on_tx_half/full()` → flags. 【F:Src/audio_out.c†L206-L240】【F:Src/audio_test_pcm5100a.c†L69-L89】
3. `audio_test_pcm5100a_tasklet_poll()` (appelée depuis `audio_tasklet_poll()`) remplit les demi‑buffers PCM5100A. 【F:Src/audio_out.c†L242-L289】【F:Src/audio_test_pcm5100a.c†L91-L105】

### Points communs / différences (factuels)

- **Commun** : DMA en mode circulaire, callbacks IRQ minimalistes (flags + compteurs), remplissage des buffers en tasklet. 【F:Src/sai.c†L140-L207】【F:Src/audio_out.c†L206-L295】
- **Différences** :
  - SAI1 TX utilise `audio_out_buffer` (TDM8, 8 canaux), SAI2 TX utilise `pcm5100a_tx_buffer` (I2S stéréo).【F:Src/audio_out.c†L52-L57】【F:Src/audio_test_pcm5100a.c†L17-L50】
  - SAI1 RX met à jour `audio_core` via `AudioIn_TaskletPoll()` ; SAI2 n’a pas de RX associé dans ce chemin de test.【F:Src/audio_in.c†L138-L189】【F:Src/audio_test_pcm5100a.c†L69-L105】

---

## 6️⃣ Diagnostics — Ce qui est observable aujourd’hui

### Compteurs disponibles

Les logs 1 Hz affichent (si `BRICK6_ENABLE_DIAGNOSTICS`) :

- Compteurs DMA audio SAI1 TX/RX : `brick6_audio_tx_half_count`, `brick6_audio_tx_full_count`, `brick6_audio_rx_half_count`, `brick6_audio_rx_full_count`.【F:Src/diagnostics_tasklet.c†L272-L296】
- Compteurs DMA SAI2 (PCM5100A test) : `AudioTest_PCM5100A_GetTxHalf/FullCount()`.【F:Src/diagnostics_tasklet.c†L272-L296】
- Erreurs SAI1 : `HAL_SAI_GetError(&hsai_BlockA1)` (log si changement).【F:Src/diagnostics_tasklet.c†L264-L314】

### Ce que ces compteurs prouvent (factuel)

- Les callbacks IRQ SAI1/SAI2 ont été exécutés (incrément des compteurs).【F:Src/audio_out.c†L206-L240】【F:Src/audio_in.c†L167-L188】【F:Src/audio_test_pcm5100a.c†L69-L115】
- Les logs ne contiennent pas de mesure analogique ni d’état matériel externe (pas de retour PCM5100A). Les compteurs reflètent uniquement l’activité firmware. 【F:Src/diagnostics_tasklet.c†L272-L296】

---

## 7️⃣ État actuel du système (factuel)

### Ce qui fonctionne (implémenté et câblé)

- Chaîne SAI1 TX + DMA + callbacks + tasklet + `audio_core_process_block()` + notification engine (chemin CS42448).【F:Src/brick6_app_init.c†L48-L62】【F:Src/audio_out.c†L206-L295】
- Chaîne SAI1 RX + DMA + callbacks + `AudioIn_TaskletPoll()` vers `audio_core_on_input_block()`.【F:Src/brick6_app_init.c†L60-L62】【F:Src/audio_in.c†L67-L189】
- Chemin test SAI2 TX (PCM5100A) : init + DMA + callbacks + tasklet si `AUDIO_TEST_PCM5100A` activé. 【F:Src/brick6_app_init.c†L48-L58】【F:Src/audio_test_pcm5100a.c†L53-L105】

### Ce qui ne fonctionne pas (indiqué par le contexte fourni)

- **DAC PCM5100A muet** (état fourni dans la demande). Aucune preuve dans le code ne contredit cette observation ; les fichiers analysés n’exposent pas de retour audio analogique. (Observation utilisateur, pas de mesure firmware.)

### Ce qui est validé par mesure logicielle (dans le code)

- Les IRQ DMA SAI1 et SAI2 sont comptabilisées et loggées, ce qui valide la réception des callbacks dans le firmware. 【F:Src/diagnostics_tasklet.c†L272-L296】

### Ce qui est indéterminé à ce stade

- L’état réel des signaux audio physiques (MCLK/BCLK/LRCK/DATA) et le comportement analogique du PCM5100A ne sont pas observables dans les fichiers analysés. (Aucune instrumentation analogique, aucun retour matériel.)

---

## 8️⃣ Questions ouvertes pour la suite (après audit)

> Questions factuelles, à vérifier par mesure ou instrumentation ultérieure.

1. **Signaux SAI2** : quels sont les niveaux/présence de MCLK, BCLK, LRCK, DATA sur les broches SAI2 (PD11/PD12/PD13/PI4) durant le DMA ?【F:Src/sai.c†L260-L302】
2. **Format audio SAI2** : les paramètres I2S 24‑bit/2 canaux (`HAL_SAI_InitProtocol`) correspondent‑ils à l’interface attendue par le PCM5100A dans ce montage ? (Vérification hardware/doc).【F:Src/sai.c†L133-L167】
3. **Alignement 24‑bit** : le PCM5100A reçoit des mots 32‑bit avec données 24‑bit (int32) dans `pcm5100a_tx_buffer` ; quelle est la justification mesurée côté bus ?【F:Src/audio_test_pcm5100a.c†L17-L50】
4. **Cadence de génération** : `audio_tasklet_poll()` est déclenchée par SAI1 TX ; si SAI1 est arrêté, le chemin PCM5100A est‑il encore alimenté (puisque son tasklet est appelé depuis `audio_tasklet_poll()` ) ?【F:Src/audio_out.c†L242-L289】

---

**Fin de l’audit.**
