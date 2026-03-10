# Historique du développement — Live Recorder

## Contexte du projet

Plateforme cible : **STM32H743**  
Mémoire externe : **32 MB SDRAM**  
Moteur audio : **DSP float @ 48 kHz**

Objectif : implémenter un **enregistreur audio live avec buffer circulaire + crossfader**, inspiré du fonctionnement de l’Elektron Octatrack.

Le système doit permettre :

- enregistrer le signal audio live dans la SDRAM
- relire ce buffer en boucle
- mixer progressivement entre :
  - le signal live
  - le signal enregistré

via un crossfader.

---

# Architecture audio actuelle

Pipeline temps réel :

DMA RX IRQ (audio.c)  
→ audio_process_block_int32() (audio_float.c)  
→ dsp_engine_process_block() (dsp_engine.c)  
→ my_dsp() (brick6_app_init.c)  
→ voice_manager_process()  
→ mixer_process()  
→ live_recorder_write()  
→ live_recorder_read()  
→ crossfade (LIVE / REC)  
→ audio_io_pack()  
→ TX DMA → DAC  

Tout le traitement DSP s’effectue dans le **chemin IRQ audio**.

---

# Ce qui a été implémenté

## Module Live Recorder

Nouveaux fichiers :

```

Src/Audio/live_recorder.c
Inc/Audio/live_recorder.h

````

Structure principale :

```c
typedef struct
{
    float *buffer;
    uint32_t max_frames;
    uint32_t loop_frames;

    uint32_t write_pos;
    uint32_t read_pos;

    uint8_t recording;
    uint8_t playing;

    uint32_t latency_offset_frames;

    uint8_t tap_mode;
} live_recorder_t;
````

Le module gère :

* buffer circulaire
* écriture audio
* lecture audio
* protection lecture/écriture
* gestion du mode record/play.

---

# Buffer SDRAM

Un buffer circulaire est réservé en SDRAM.

Configuration actuelle :

* **32 secondes**
* **48 kHz**
* **stéréo float**

Calcul mémoire :

48 000 frames/sec
32 sec
= **1 536 000 frames**

1 frame stéréo float = **8 bytes**

Total ≈ **12.3 MB SDRAM**

Allocation :

```c
static AUDIO_COLD_SDRAM float g_live_recorder_buffer[LIVE_RECORDER_MAX_FRAMES * 2];
```

---

# Écriture dans le buffer

Fonction :

```
live_recorder_write()
```

Fonctionnement :

* écrit les frames L/R dans le buffer SDRAM
* buffer circulaire
* wrap automatique à `loop_frames`

Position dans le pipeline :

après `mixer_process()`

Donc le recorder capture **le mix final**.

---

# Lecture du buffer

Fonction :

```
live_recorder_read()
```

Fonctionnement :

* lecture circulaire
* wrap automatique
* sortie silencieuse si buffer invalide

Protection anti-glitch :

```
LIVE_RECORDER_READ_SAFETY_MARGIN_FRAMES
```

Si la tête de lecture se rapproche trop de la tête d’écriture, la sortie est forcée à zéro.

---

# Crossfade

Crossfade implémenté dans `my_dsp()`.

Formule :

```
OUT = LIVE * (1 - xfade) + REC * xfade
```

Actuellement :

```
xfade = 0.0f
```

Donc seul le signal live est entendu pour l’instant.

Le crossfader matériel sera ajouté plus tard.

---

# Réduction de la consommation SDRAM

L’ancien système de pool de samples occupait potentiellement **plus de 100 MB SDRAM**.

Nouvelle architecture :

```
SAMPLE_POOL_RESIDENT_SLOTS = 8
SAMPLE_POOL_MAX_FRAMES_PER_SAMPLE = 32768
```

Consommation approximative :

≈ **2 MB SDRAM**

Cela libère suffisamment de mémoire pour le recorder.

---

# Problèmes résolus

✔ suppression complète du streaming SD runtime
✔ suppression du système attack preload
✔ correction du pool SDRAM oversized
✔ ajout d’un recorder temps réel stable
✔ protection lecture/écriture du buffer

---

# Ce qu’il reste à faire

## 1. Compensation de latence MIDI

Aligner lecture et écriture.

Concept :

```
read_pos = write_pos - latency_offset_frames
```

Permet de compenser :

* latence MIDI
* latence synthé externe
* latence ADC

---

## 2. Démarrage de loop sample-accurate

Utiliser le compteur audio global :

```
audio_get_frame_counter()
```

Pour synchroniser la lecture avec le moteur audio.

---

## 3. Crossfader matériel

Mapper la valeur du crossfader hardware vers :

```
xfade ∈ [0 ; 1]
```

Le hardware n’est pas encore installé (MUX en attente).

---

## 4. Overdub

Permettre d’enregistrer par-dessus la loop :

```
buffer = buffer * feedback + input
```

---

## 5. Longueur de loop dynamique

Support futur :

* 1 bar
* 2 bars
* 4 bars
* 8 bars

Basé sur BPM.

---

## 6. FX sur le buffer

Architecture prévue pour permettre :

* reverse
* stutter
* slicing
* granular
* timestretch
* multi read heads

---

# Architecture cible finale

```
ADC
 ↓
DSP engine
 ↓
Mixer
 ↓
Recorder write
 ↓
Recorder read
 ↓
Crossfader
 ↓
DAC
```

---

# État actuel

Le système recorder est **fonctionnel et stable**.

Les prochaines étapes concernent principalement :

* la synchronisation musicale
* le contrôle hardware
* les extensions créatives (overdub / FX).

```

---
