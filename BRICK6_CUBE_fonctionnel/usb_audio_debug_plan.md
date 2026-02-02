# Plan de debug — Chaîne audio BRICK6 (preuve par instrumentation)

> **Objectif** : fournir un plan **exhaustif** d’instrumentation (faible cadence, ~1 Hz) pour **prouver** où le flux audio USB circule, où il stagne et où il est ignoré, **sans correction fonctionnelle** et **sans impact temps réel**.

---

## 1️⃣ Cartographie complète des points d’observation (producteur → consommateur → contrat)

> Toutes les cadences ci‑dessous sont données pour **48 kHz** et le bloc **256 frames** (contrat actuel).

| Maillon | Producteur | Consommateur | Cadence attendue | Contrat de données |
|---|---|---|---|---|
| **USB Iso OUT** | Host USB | TinyUSB UAC1 RX | 1 ms (ISO) | Frames USB (UAC1) → int16 | 
| **tud_audio_rx_done_isr** | TinyUSB | audio_io_usb_on_rx_samples() | ~1000 IRQ/s | nb frames USB reçues / ms | 
| **audio_io_usb_on_rx_samples** | TinyUSB RX | Ring buffer USB (audio_buffer) | 1 ms | Conversion int16 → int32 (24‑bit << 8) | 
| **Ring buffer USB** | audio_io_usb_on_rx_samples() | audio_core_process_block() | Continu | unit = frames × channels (int32) | 
| **audio_core_process_block** | Ring USB / SD / fallback | audio_out / audio_test | 256 frames / appel | 256 frames × 8 canaux (int32) | 
| **audio_out (SAI1 TX)** | audio_core | DMA SAI1 TX | 2 IRQ (half/full) / bloc | 256 frames × 8 slots | 
| **DMA SAI1 TX** | DMA | DAC CS42448 | IRQ half/full attendues | Transfert TDM8 24‑bit | 

**Cadences nominales à 48 kHz** (référence) :
- **audio_core** : 48 000 / 256 = **187.5 appels/s**.
- **DMA SAI1** : **2 IRQ par bloc** → **375 IRQ/s** (half + full).
- **USB RX** : **~1000 callbacks/s** (trames ISO).

---

## 2️⃣ Liste exhaustive des compteurs & métriques (par couche)

> Tous les compteurs sont **monotones** (uint32_t/uint64_t). Les min/max sont mis à jour dans les chemins **non‑IRQ** uniquement.

### ✅ USB / TinyUSB (RX)
- **usb_rx_irq_count** : nombre d’appels à `tud_audio_rx_done_isr()`.
- **usb_rx_samples_total** : total samples reçus.
- **usb_rx_bytes_total** : total bytes reçus.
- **usb_rx_zero_reads** : nombre d’appels avec 0 samples.
- **usb_rx_rate_samples_s** : delta samples / seconde.
- **usb_rx_rate_irq_s** : delta IRQ / seconde.

### ✅ Ring buffer USB (audio_buffer)
- **usb_rb_written_total** : total frames écrites.
- **usb_rb_read_total** : total frames lues.
- **usb_rb_dropped_total** : frames rejetées (overflow).
- **usb_rb_avail_min/max** : min/max disponibles.
- **usb_rb_free_min/max** : min/max libres.

### ✅ audio_core
- **core_calls_total** : nombre d’appels.
- **core_src_last** : dernière source sélectionnée (0/1/2/3).
- **core_usb_used_blocks** : blocs USB effectivement utilisés.
- **core_usb_missed_blocks** : blocs USB attendus mais absents.
- **core_fallback_count** : recours à fallback (input SAI).
- **core_frames_requested_total** : total frames demandées.
- **core_frames_provided_total** : total frames fournies.
- **core_usb_last_avail_frames** : disponibilité USB lors du dernier appel.
- **core_usb_last_need_frames** : frames demandées lors du dernier appel.

### ✅ audio_out / tasklet
- **audio_out_dma_half_irq** / **full_irq** : IRQ DMA TX.
- **audio_out_blocks_filled** : blocs réellement remplis.
- **audio_out_blocks_unchanged** : blocs non modifiés (copie identique) → signe d’absence de source.
- **audio_out_fill_latency_max_us** : latence max entre IRQ et remplissage (mesure coarse via timestamps non‑IRQ).

### ✅ SAI / DMA
- **sai_tx_error_count** : erreurs HAL SAI TX (changement d’état).
- **sai_tx_half_full_desync** : incohérences de séquence half/full.
- **sai_tx_irq_rate_s** : delta IRQ/s mesuré.

---

## 3️⃣ Plan d’instrumentation précis (où / quoi / comment / log)

> **Principe** : mise à jour des compteurs en ISR minimaliste, **agrégation et logs uniquement dans `diagnostics_tasklet` à ~1 Hz**.

### 3.1 USB / TinyUSB

| Métrique | Où instrumenter | Quoi compter | Agrégation | Log 1 Hz |
|---|---|---|---|---|
| usb_rx_irq_count | `tud_audio_rx_done_isr()` | ++ à chaque IRQ | compteur | `usb.rx_irq` |
| usb_rx_samples_total | `audio_io_usb_on_rx_samples()` | += nb samples | compteur | `usb.samples` |
| usb_rx_bytes_total | `tud_audio_rx_done_isr()` | += nb bytes RX | compteur | `usb.bytes` |
| usb_rx_zero_reads | `audio_io_usb_on_rx_samples()` | ++ si samples==0 | compteur | `usb.zero` |
| usb_rx_rate_samples_s | `diagnostics_tasklet` | delta/s | delta | `usb.rate_smp` |
| usb_rx_rate_irq_s | `diagnostics_tasklet` | delta/s | delta | `usb.rate_irq` |

### 3.2 Ring buffer USB

| Métrique | Où | Quoi | Agrégation | Log |
|---|---|---|---|---|
| usb_rb_written_total | `audio_io_usb_on_rx_samples()` | += frames écrites | compteur | `rb.wr` |
| usb_rb_read_total | `audio_core_process_block()` | += frames lues | compteur | `rb.rd` |
| usb_rb_dropped_total | ring write | += frames refusées | compteur | `rb.drop` |
| usb_rb_avail_min/max | ring helpers | min/max avail | min/max | `rb.av_min/max` |
| usb_rb_free_min/max | ring helpers | min/max free | min/max | `rb.free_min/max` |

### 3.3 audio_core

| Métrique | Où | Quoi | Agrégation | Log |
|---|---|---|---|---|
| core_calls_total | `audio_core_process_block()` | ++ | compteur | `core.calls` |
| core_src_last | `audio_core_process_block()` | dernier routing | last | `core.src` |
| core_usb_used_blocks | `audio_core_process_block()` | ++ si USB utilisé | compteur | `core.usb_used` |
| core_usb_missed_blocks | `audio_core_process_block()` | ++ si USB absent | compteur | `core.usb_miss` |
| core_fallback_count | `audio_core_process_block()` | ++ si fallback | compteur | `core.fallback` |
| core_frames_requested_total | `audio_core_process_block()` | += frames demandées | compteur | `core.req` |
| core_frames_provided_total | `audio_core_process_block()` | += frames écrites | compteur | `core.prov` |
| core_usb_last_avail_frames | `audio_core_process_block()` | avail USB dernier appel | last | `core.usb_avail` |
| core_usb_last_need_frames | `audio_core_process_block()` | frames demandées | last | `core.usb_need` |

### 3.4 audio_out / tasklet

| Métrique | Où | Quoi | Agrégation | Log |
|---|---|---|---|---|
| audio_out_dma_half_irq | `HAL_SAI_TxHalfCpltCallback()` | ++ (SAI1 TX) | compteur | `out.irq_h` |
| audio_out_dma_full_irq | `HAL_SAI_TxCpltCallback()` | ++ (SAI1 TX) | compteur | `out.irq_f` |
| audio_out_blocks_filled | `audio_tasklet_poll()` | ++ si bloc rempli | compteur | `out.filled` |
| audio_out_blocks_unchanged | `audio_tasklet_poll()` | ++ si bloc inchangé | compteur | `out.same` |
| audio_out_fill_latency_max_us | `audio_tasklet_poll()` | max(latence) | max | `out.lat_max` |

### 3.5 SAI / DMA

| Métrique | Où | Quoi | Agrégation | Log |
|---|---|---|---|---|
| sai_tx_error_count | `diagnostics_tasklet` | ++ si `HAL_SAI_GetError` change | compteur | `sai.err` |
| sai_tx_half_full_desync | `audio_out` flags | ++ si ordre incohérent | compteur | `sai.desync` |
| sai_tx_irq_rate_s | `diagnostics_tasklet` | delta IRQ/s | delta | `sai.irq_rate` |

---

## 4️⃣ Format de log 1 Hz (lisible et compact)

Exemple de ligne UART :
```
USB irq=1000 smp=48000 B=96000 zero=0 rate_smp=48000
RB wr=48000 rd=48000 drop=0 av_min=0 av_max=512 free_min=0 free_max=512
CORE calls=187 src=1 usb_used=187 usb_miss=0 fb=0 req=47872 prov=47872 usb_av=256 usb_need=256
OUT irq_h=187 irq_f=187 filled=187 same=0 lat_max=320
SAI err=0 desync=0 irq_rate=375
```

---

## 5️⃣ Table de diagnostic par symptômes (preuves → causes probables)

| Symptômes observés (logs) | Cause probable | Interprétation |
|---|---|---|
| `USB irq` = 0, `USB smp` = 0 | TinyUSB RX non déclenché | Aucun flux USB au niveau ISR. |
| `USB irq` OK mais `USB smp` ~0, `zero` ↑ | Host n’envoie pas (silence) ou format incorrect | RX actif mais sans données exploitables. |
| `RB wr` ↑, `RB drop` ↑, `RB free_min` ~0 | Buffer saturé | Cadence production > consommation. |
| `RB wr` ↑, `RB rd` ~0 | audio_core ne lit pas le ring | Problème de sélection source / appel. |
| `CORE calls` OK mais `core.src` ≠ USB, `core.usb_used`=0 | Routage core non USB | Flux USB ignoré par choix de source. |
| `core.usb_used` ↑ mais `core.usb_miss` ↑, `core.fallback` ↑ | USB insuffisant | Débit USB < besoin core / taille bloc. |
| `OUT irq_*` OK mais `out.filled` stagne | tasklet non exécuté ou bloqué | Cadence DMA OK mais remplissage absent. |
| `OUT irq_*` OK, `out.filled` OK, mais silence | Problème SAI/codec/output | Flux numérique rempli mais pas rendu. |
| `sai.desync` ↑ | DMA half/full incohérent | Problème IRQ ou flag. |

---

## 6️⃣ Résumé “preuve → conclusion” (méthode d’exclusion)

1. **USB RX prouvé** si `USB irq` et `USB smp` augmentent.
   - **Sinon** : la chaîne est stoppée **avant** TinyUSB RX.
2. **Ring buffer prouvé** si `RB wr` augmente et `RB drop` reste nul.
   - **Sinon** : la chaîne casse au niveau de la **réception ou du stockage**.
3. **audio_core prouvé** si `CORE calls` et `core.usb_used` augmentent.
   - **Sinon** : la chaîne casse au **routage** ou au **consumer core**.
4. **audio_out + DMA prouvés** si `OUT irq_*` et `out.filled` augmentent.
   - **Sinon** : la chaîne casse au **cadenceur DMA / tasklet**.
5. **Flux numérique complet** si USB → RB → core → out sont tous OK.
   - **Si silence** malgré ça : anomalie **SAI/codec/output physique**.

👉 **Première anomalie mesurable** = premier compteur qui ne suit plus sa cadence nominale.

---

## 7️⃣ Checklist d’exécution (terrain)

1. Démarrer le firmware (diagnostics actifs).
2. Lancer un flux audio sur le host USB.
3. Relever 10–20 secondes de logs UART 1 Hz.
4. Comparer les **cadences** aux valeurs nominales.
5. Appliquer la table symptôme → cause pour isoler le maillon défaillant.

---

## 8️⃣ Rappel des contraintes

- **Aucun printf en IRQ**.
- **Aucune refonte fonctionnelle**.
- **Logs uniquement dans diagnostics_tasklet (~1 Hz)**.
- **Instrumentation minimaliste, preuves chiffrées uniquement**.
