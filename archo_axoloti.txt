# ARCHI_AXOLOTI — Audit complet de la chaîne audio DSP

## 1. Vue globale

Architecture hybride IRQ + thread :

DMA SAI (horloge audio)
→ IRQ DMA
→ computebufI()
→ signal event (ChibiOS)
→ ThreadDSP (prio haute)
→ patchMeta.fptr_dsp_process()
→ écriture buffer TX DMA
→ sortie audio

---

## 2. Cadence audio (maître du système)

* Périphérique maître : SAI (codec ADAU1961)
* DMA en mode double buffer (ping-pong)
* IRQ déclenchée sur transfert complet (et/ou half selon config)

👉 Le SAI + DMA définissent **le tempo absolu du système**

---

## 3. Chemin d’exécution complet

### 3.1 Entrée audio

SAI RX DMA
→ remplit rbuf / rbuf2

### 3.2 IRQ DMA SAI

ISR (dma_sai_a_interrupt) :

* test bit CT (ping/pong)
* sélection buffers actifs
* appel :

```c
computebufI(rbufX, bufX);
```

---

### 3.3 computebufI()

* copie 32 samples (16 frames stéréo) vers `inbuf`
* assigne pointeur `outbuf`
* déclenche DSP :

```c
chSysLockFromIsr();
chEvtSignalI(pThreadDSP, 1);
chSysUnlockFromIsr();
```

👉 Aucun DSP ici → uniquement transfert + signal

---

### 3.4 ThreadDSP

Boucle principale :

```c
evt = chEvtWaitOne(7);
```

si `evt == 1` :

* mesure temps DSP
* watchdog_feed()
* appel DSP :

```c
patchMeta.fptr_dsp_process(...)
```

---

### 3.5 Sortie audio

* DSP écrit dans `outbuf` (buf/buf2)
* DMA TX envoie vers SAI
* DAC / codec sort le son

---

## 4. Buffers audio

### Types

* format interne : int32_t
* stéréo interleavé

### Buffers principaux

* buf / buf2 → sortie
* rbuf / rbuf2 → entrée
* inbuf[32] → copie DSP
* outbuf → pointeur vers buffer DMA

### Taille

* BUFSIZE = 16 (frames)
* DOUBLE_BUFSIZE = 32 (samples interleavés)

👉 bloc DSP = 16 samples @ 48kHz

---

## 5. Scheduling DSP

### Modèle

* IRQ → trigger
* thread → exécution

### Priorité

```c
PATCH_DSP_PRIO = HIGHPRIO - 1
```

👉 quasi max priorité système

---

## 6. Temps réel

### Durée bloc

```text
16 / 48000 = 333 µs
```

👉 DSP doit finir < 333 µs

---

### Mesure charge

```c
DspTime = RTT2US(...)
dspLoad200 = (2000 * DspTime) / timeslice
```

---

### Gestion surcharge

si overload :

* clear buffers
* reset USB audio
* flag erreur
* sleep 1 ms

👉 stratégie : **détection + recovery**

---

## 7. Modèle DSP

### Entry point

```c
patchMeta.fptr_dsp_process
```

→ défini dynamiquement par le patch

---

### Structure

DSP bloc-based :

```c
for (i < BUFSIZE)
    read inputs

root.dsp()

for (i < BUFSIZE)
    write outputs
```

---

### Nature

* DSP généré dynamiquement
* peut inclure Mutable Instruments DSP
* exécution unique (pas de multi-thread DSP)

---

## 8. I2S (chemin secondaire)

### Fonction

* flux audio additionnel

### IRQ

```c
dma_i2s_tx_interrupt()
```

→ appelle :

```c
i2s_computebufI()
```

---

### Particularité

* PAS de signal thread DSP
* copie uniquement buffers

👉 DSP reste piloté par SAI

---

## 9. Synchronisation I2S ↔ SAI

### Méthode

```c
wait_sai_dma_tc_flag()
```

* drift volontaire I2S (~47991 Hz)
* attente alignement IRQ
* re-lock PLL à 48kHz

---

### Nature

👉 lock initial uniquement
👉 maintien parfait : NON PROUVÉ

---

## 10. Modèle temps réel

### Type

```text
soft real-time avec mitigation
```

---

### Caractéristiques

* pas de garantie hard RT
* détection surcharge
* récupération active

---

## 11. Risques identifiés

### 1. Overrun DSP

* DSP > 333 µs
  → glitch + reset buffer

---

### 2. Race I2S

* i2s_inbuf sans lock
  → possible overwrite concurrent

---

### 3. Thread préemption

* dépend scheduler ChibiOS

---

### 4. Pas de garantie stricte

* pas de preuve DSP < deadline
* système tolère erreurs

---

## 12. Latence

### Min théorique

* 1 bloc entrée : 16 samples
* 1 bloc sortie : 16 samples

```text
≈ 32 samples ≈ 0.66 ms
```

---

### Réel

```text
≈ 0.7 – 1 ms
```

---

## 13. Architecture finale

```text
SAI DMA (clock master)
   ↓
IRQ DMA
   ↓
computebufI
   ↓
Event ChibiOS
   ↓
ThreadDSP
   ↓
DSP patch
   ↓
Buffer TX DMA
   ↓
SAI sortie
```

---

## 14. Philosophie Axoloti

* IRQ = horloge audio
* DSP = thread temps réel
* tolérance aux erreurs
* flexibilité maximale

---

## 15. Résumé clé

```text
- DSP exécuté en thread, pas en IRQ
- cadence pilotée par DMA SAI
- buffers ping-pong
- bloc = 16 samples
- système soft real-time avec recovery
- I2S = flux secondaire non maître
```

---
