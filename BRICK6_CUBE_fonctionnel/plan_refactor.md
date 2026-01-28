# 🧱 Plan d’architecture et de refactorisation

**Projet : BRICK6_CUBE_fonctionnel (STM32H743, bare-metal, sans RTOS)**

---

# 0) Vision long terme

Le firmware doit pouvoir évoluer vers :

* Groovebox complète :

  * UI (écran, boutons, encodeurs)
  * Séquenceur 16 tracks
  * Moteur audio (mix, effets, synth, samples)
  * Streaming SD multipistes
  * MIDI In/Out (USB + DIN)
* Sans RTOS
* Avec :

  * **Priorité absolue à l’audio**
  * **Zéro blocage**
  * **Ordonnancement coopératif maîtrisé**

Le **DMA SAI audio est l’horloge maîtresse du système**.

---

# 1) Diagnostic de l’architecture actuelle


### Points de blocage / comportements à risque

* Boucle principale monolithique (`main.c`)
* Attente active `Wait_SDCARD_Ready()` dans callbacks SD ❌
* Traitement audio dans IRQ DMA ❌
* Poll USB Host non borné ❌
* Mélange logique temps réel / non critique ❌

### Fragilité / manque d’évolutivité

* Pas de séparation claire :

  * audio
  * moteur
  * UI
  * IO
* Pas de notion de budget CPU
* Pas de notion d’horloge système unifiée

### Risques temps réel

* IRQ longues = jitter audio
* SD / USB peuvent affamer l’audio
* Pas de contrôle de charge CPU

---

# 2) Principes fondamentaux de la nouvelle architecture

## 2.1 Le principe clé

> **IRQ = signalisation uniquement**
> **Main loop = travail réel**

---

## 2.2 Trois types de “temps”

### 🕒 1) Temps AUDIO (maître)

* Dicté par SAI + DMA
* Chaque half/full buffer = **tick audio**
* Le moteur audio et le séquenceur sont cadencés par :

  * le nombre de samples traités
  * ou le nombre de blocs audio

> ⚠️ Aucune autre horloge ne doit piloter le temps musical.

---

### ⏱️ 2) Timers hardware = horloges de service

Utilisés pour :

* UI scan (1 kHz, 500 Hz…)
* MIDI clock master (24 PPQN)
* Blink LED
* Housekeeping

> Les timers **ne font que poser des flags**.

---

### 🧠 3) Temps “soft” de la main loop

* Ordonnancement coopératif
* Basé sur :

  * flags
  * états
  * budgets CPU

---

## 2.3 Rôles clairement séparés

### IRQ / callbacks DMA

* ❌ Pas de logique lourde
* ❌ Pas de HAL bloquant
* ✅ Juste :

  * poser un flag
  * incrémenter un compteur
  * notifier un buffer prêt

---

### Boucle principale

* Exécute des **tasklets coopératives**
* Chaque tasklet :

  * travail borné
  * non bloquant
  * fragmentable

---

## 2.4 Couches logiques

On introduit **conceptuellement** :

* audio_tasklet → rendu audio
* engine_tasklet → séquenceur, synth, automation
* sd_tasklet → streaming
* ui_tasklet → boutons, écran
* midi_tasklet → MIDI
* usb_tasklet → USB host/device
* diagnostics_tasklet → logs, stats

---

# 3) Structure cible de la boucle principale

```c
while (1)
{
  audio_tasklet_poll();       // PRIORITÉ ABSOLUE
  engine_tasklet_poll();      // séquenceur, synth, etc
  sd_tasklet_poll();          // streaming
  usb_tasklet_poll_bounded(); // host/device
  midi_tasklet_poll();        // MIDI
  ui_tasklet_poll();          // UI
  diagnostics_tasklet_poll(); // logs, LED
}
```

---

## 3.1 Règles d’ordonnancement

* Audio toujours en premier
* Chaque tasklet :

  * max N opérations
  * ou max X microsecondes
* Jamais de while() interne non bornée

---

# 4) Architecture du pipeline SD → audio

*(reprend ton plan, validé, avec une clarification importante)*

## 4.1 Principe

> SD et Audio sont **découplés** par un **ring buffer logique**.

Jamais :

* audio ne lit directement dans les buffers SD DMA

---

## 4.2 Chaîne complète

```
SD DMA buffers → SD tasklet → ring buffer → audio tasklet → SAI DMA
```

---

## 4.3 États FSM

* IDLE
* PREFILL
* STREAMING
* UNDERFLOW
* ERROR

---

## 4.4 Règles critiques

* L’audio **ne doit jamais attendre le SD**
* En underflow :

  * silence
  * ou boucle
  * mais **jamais de blocage**

---

# 5) Audio = horloge du moteur

Le moteur (séquenceur, envelopes, LFO, etc) est mis à jour via :

```c
engine_process(block_frames);
```

En interne :

```c
samples_accum += block_frames;
while (samples_accum >= samples_per_tick) {
    sequencer_tick();
    samples_accum -= samples_per_tick;
}
```

---

# 6) Timers hardware : usage autorisé et interdit

## ✅ Autorisé

* UI scan
* MIDI clock master
* Blink LED
* Timeouts soft
* Profiling

## ❌ Interdit

* Audio
* SD
* FATFS
* USB
* Séquenceur
* Logique lourde

---

# 7) Plan de refactor par étapes sûres

*(reprend ton plan, avec un ordre légèrement renforcé)*

## Étape 1 — Instrumentation

* Compteurs :

  * audio callbacks
  * sd callbacks
  * durée max d’un tour de boucle
* Aucune modification fonctionnelle

---

## Étape 2 — Sortir l’audio des IRQ

* Callbacks SAI = flags seulement
* `audio_tasklet_poll()` fait le vrai travail

---

## Étape 3 — Introduire `engine_tasklet`

* Même si au début il ne fait rien
* C’est la couche futur séquenceur / synth

---

## Étape 4 — Rendre SD non bloquant

* Supprimer `Wait_SDCARD_Ready()` des callbacks
* FSM SD pilotée depuis main loop

---

## Étape 5 — Ajouter ring buffer SD→audio

* Watermarks
* Prefill
* Gestion underflow

---

## Étape 6 — Budgets CPU

* USB host borné
* Logs bornés
* UI bornée

---

## Étape 7 — Nettoyage

* Virer tests du chemin temps réel
* Séparer diagnostics

---

# 8) Règles absolues (à afficher au mur)

* ❌ Pas de blocage dans la main loop
* ❌ Pas de logique lourde en IRQ
* ❌ Pas de HAL bloquant en IRQ
* ❌ Pas de SD / USB dans l’audio
* ❌ Pas d’allocation dynamique
* ✅ Audio toujours prioritaire
* ✅ Tout doit être fragmentable

---

# 9) Notes spécifiques au projet actuel

* `main.c` : devient un scheduler coopératif
* `audio_out.c` : sort le traitement des IRQ
* `sd_stream.c` : devient un backend DMA + FSM
* `midi_host.c` : bornage strict

---

# 🏁 Conclusion

Cette architecture :

* scale vers :

  * UI
  * séquenceur
  * multi-pistes
  * effets
* reste :

  * déterministe
  * stable
  * audio-safe
* sans RTOS
* sans usine à gaz

---


