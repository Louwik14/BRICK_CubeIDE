# Plan d’architecture et de refactorisation (STM32H743, bare-metal, sans RTOS)

## 1) Diagnostic de l’architecture actuelle

### Points de blocage / comportements à risque
- **Boucle principale monolithique** : dans `main.c`, la boucle `while(1)` mélange USB Host, MIDI, logs, LED, diagnostics SAI, et une logique de test SD longue et conditionnelle. Cela crée un point de contention unique et non prévisible pour l’usage CPU. (`Src/main.c`)
- **Blocages potentiels côté SD** : `sd_stream.c` contient un `Wait_SDCARD_Ready()` qui fait de l’attente active (polling) et qui est appelé depuis des callbacks HAL SD (Rx/Tx complete). Ce pattern peut bloquer en interruption (mauvais contexte) et consommer du CPU. (`Src/sd_stream.c`)
- **Utilisation de `HAL_Delay` dans des fonctions utilitaires** (tests SDRAM) déclenche du blocage en pleine init, ce qui est acceptable à l’init mais doit rester isolé de la boucle temps réel. (`Src/main.c`)
- **Couplage fort entre IRQ audio et logique de génération audio** : `AudioOut_ProcessHalf/Full()` fait les opérations de génération/mélange dans le callback DMA audio (IRQ) via `audio_out_fill_samples(...)`, ce qui peut devenir coûteux. (`Src/audio_out.c`)
- **Flux MIDI USB Host pollé en boucle** : `midi_host_poll()` lit en boucle tant que possible, ce qui peut monopoliser le CPU en cas de bursts, surtout si appelé sans budget. (`Src/midi_host.c`, `Src/main.c`)

### Fragilité / manque d’évolutivité
- **Pas de séparation claire des responsabilités** : la boucle principale gère tout (tâches temps réel et non-temps réel), ce qui nuit à la scalabilité (ajout pipeline SD→audio, UI, etc.). (`Src/main.c`)
- **Faible lisibilité des priorités** : l’audio a besoin de priorité dure, mais la logique de logs/diagnostic/SD partage le même contexte de boucle principale. (`Src/main.c`)
- **Risque de jitter audio** : le traitement audio est réalisé dans les callbacks DMA, ce qui peut augmenter la latence et provoquer des variations en fonction de la charge CPU. (`Src/audio_out.c`, `Src/main.c`)

### Risques temps réel
- **Attente active dans un callback SD DMA** : `Wait_SDCARD_Ready()` est appelée dans `HAL_SD_RxCpltCallback()` et `HAL_SD_TxCpltCallback()`. Bloquer dans un callback DMA est dangereux : cela peut retarder d’autres IRQ critiques (SAI, USB). (`Src/sd_stream.c`)
- **Boucle principale saturée** : le polling USB Host + logs + tests SD peuvent affamer des traitements nécessaires (MIDI, audio). (`Src/main.c`, `Src/midi_host.c`)
- **Absence de “budget CPU”** : aucune stratégie pour limiter le temps par tâche dans le `while(1)`. (`Src/main.c`)

---

## 2) Principes de la nouvelle architecture

### Rôles clairement séparés
- **IRQ (temps réel dur)**
  - Doit être **minimal** : capture d’événements, incrément de compteurs, push d’éléments dans des buffers lock-free (ou flags), rien de lourd.
  - Aucune attente active (`while`, `HAL_Delay`, polling) en IRQ.
  - Pour l’audio : callbacks DMA doivent notifier “half/full ready” et laisser la boucle principale traiter la génération/mixage.

- **Callbacks DMA**
  - Ne font que **signaler** (set flag, index buffer), jamais de logique lourde ni d’accès bloquants.
  - Exemple : `AudioOut_ProcessHalf/Full()` deviendrait un simple “mark half ready”.

- **Boucle principale (orchestration)**
  - Exécute des **tasklets coopératives** (petites fonctions non bloquantes), chacune avec un budget.
  - Gère la logique applicative, pipeline SD→audio, USB/MIDI, logs, etc.

### Tasklets / sous-systèmes coopératifs
- Chaque sous-système expose un trio de fonctions :
  - `*_init()` : initialisation.
  - `*_isr_*()` : ISR/callback (flags).
  - `*_poll()` : traitement non bloquant, appelé dans le `while(1)`.
- Le `poll()` ne doit jamais bloquer ; il consomme uniquement les événements disponibles ou effectue un fragment de travail.

### Machines d’état
- Chaque sous-système long (SD, pipeline audio, USB host) s’exécute via une **machine d’état**.
- États courts et explicites, transitions déclenchées par flags/counters.

### Règles de non-blocage (à faire respecter partout)
- ❌ Pas de `HAL_Delay()` ou boucle d’attente active dans la boucle principale.
- ❌ Pas de HAL bloquants depuis IRQ.
- ✅ Le polling doit être rapide et borné (budget fixe, “max N actions par tour”).

---

## 3) Nouvelle structure de la boucle principale

### Schéma logique (pseudo)
```
while (1) {
  tick = HAL_GetTick();

  audio_tasklet_poll();      // Priorité 1 (audio)
  sd_pipeline_poll();        // Priorité 2 (flux SD)
  usb_host_poll_bounded();   // Priorité 3
  midi_poll();               // Priorité 3
  app_logic_poll();          // Priorité 4
  diagnostics_poll();        // Priorité 5 (logs/LEDs)
}
```

### Liste des tâches coopératives
1. **Audio tasklet**
   - Consomme les flags half/full issus du DMA.
   - Fait le mixage/loopback/génération dans le contexte main loop.
   - Remplit les buffers “ready” pour DMA.

2. **SD tasklet**
   - Pilote une FSM SD (lecture en continu, rechargement buffers, gestion erreurs).
   - N’appelle jamais `Wait_SDCARD_Ready()` en IRQ.

3. **USB Host tasklet**
   - `midi_host_poll()` modifié pour un nombre maximum de paquets par tour.

4. **MIDI device tasklet**
   - `midi_poll()` reste dans la boucle principale.

5. **Logique applicative**
   - Synthèse, contrôle UI, etc.

6. **Diagnostics**
   - Logs périodiques, LED heartbeat.

### Ordonnancement et budget CPU
- **Règle** : chaque `poll()` doit exécuter un **travail borné** (ex: N paquets ou N frames).
- **Audio toujours en premier** : priorité explicite dans l’ordre de la boucle.
- **Time-slicing** : possibilité d’ajouter un budget en microsecondes pour SD/USB.

---

## 4) Architecture du pipeline SD → audio

### Objectif
- Lecture continue SD vers buffers (double/triple buffering) → audio DMA consomme.

### États du pipeline (FSM)
- **IDLE** : pas de lecture.
- **FILLING** : demande de lecture SD pour remplir buffers.
- **READY** : buffers prêts pour l’audio.
- **STREAMING** : audio consomme, SD recharge en tâche de fond.
- **UNDERFLOW** : audio manque de données (silence/fallback).
- **ERROR** : erreur SD, reset pipeline.

### Buffers
- **SD DMA buffers** (déjà en double buffer : `Buffer0/Buffer1` dans `sd_stream.c`).
- **Audio buffers** (DMA audio déjà en double buffer).
- Ajouter un **ring buffer logique** qui fait le pont entre SD et audio.
  - Producteur = SD tasklet (copie depuis SD buffers).
  - Consommateur = audio tasklet (prépare les blocs audio).

### Qui produit / qui consomme
- **SD tasklet** : dès qu’un buffer SD est complet (flag set par callback DMA), copie vers ring buffer audio.
- **Audio tasklet** : récupère les frames prêtes du ring buffer et remplit le DMA audio.

### Synchronisation
- ISR SD → *flag* “buffer0/1 ready”.
- ISR Audio → *flag* “audio half/full ready”.
- Main loop lit ces flags et effectue copies/mixages.

### Éviter underflow / overflow
- **Watermarks** :
  - `LOW_WM` : si ring buffer en dessous, demande SD en priorité, audio peut générer silence temporaire.
  - `HIGH_WM` : si au-dessus, SD stoppe les demandes.
- **Pré-remplissage** : avant de démarrer l’audio, précharger N buffers.
- **Détection** : compteurs d’underrun/overrun incrémentés (diagnostic).

---

## 5) Plan de refactor par étapes sûres

### Étape 1 — Observation & instrumentation (sans casser)
- Ajouter des compteurs/flags minimalistes pour mesurer :
  - temps dans la boucle, temps max par tasklet.
  - événements audio half/full, SD half/full.
- Objectif : comprendre le budget temps réel, sans changer la logique.

### Étape 2 — Découpler IRQ vs traitement audio
- Modifier `AudioOut_ProcessHalf/Full()` pour ne faire que poser un flag.
- Déplacer `audio_out_fill_samples()` dans un `audio_tasklet_poll()`.
- Garder le même buffer DMA, sans changer les APIs externes.

### Étape 3 — SD pipeline non bloquant
- Éliminer l’attente active dans `sd_stream.c` (ne jamais attendre dans callbacks).
- Introduire un FSM SD piloté depuis la boucle principale.
- Le callback SD ne fait que signaler la fin de buffer, la boucle principale lance la suite.

### Étape 4 — Ring buffer SD→audio
- Ajouter un ring buffer intermédiaire (2–4 blocs).
- SD tasklet remplit, audio tasklet consomme.
- Ajouter gestion de watermarks + underflow/overflow.

### Étape 5 — Budgeting et priorités
- Limiter `midi_host_poll()` à N paquets par tour.
- Ajouter des budgets pour logs/diagnostics.

### Étape 6 — Nettoyage progressif
- Extraire les “tests” SD/SDRAM du chemin temps réel.
- Regrouper la logique de logs dans un `diagnostics_poll()`.

---

## 6) Ce qui ne doit JAMAIS être fait

- ❌ Blocage dans la boucle principale (`HAL_Delay`, boucle d’attente, polling long).
- ❌ Attente active en IRQ ou callback DMA (notamment `Wait_SDCARD_Ready`).
- ❌ Logique lourde dans les callbacks DMA (audio ou SD).
- ❌ Appels HAL bloquants dans un mauvais contexte (IRQ, callback).
- ❌ Accès concurrent sans protection minimale (flags non atomiques sans précaution).

---

## Notes spécifiques aux fichiers actuels
- `Src/main.c` : boucle principale mélangée (audio, MIDI, SD test, logs). À refactorer par tasklets.
- `Src/audio_out.c` : génération audio dans callbacks DMA → déplacer vers `audio_tasklet_poll()`.
- `Src/sd_stream.c` : remplacer l’attente active dans callbacks SD par un mécanisme non bloquant.
- `Src/midi_host.c` : ajouter un budget de paquets par appel pour éviter starvation.

---

**Ce document est un plan d’évolution progressive** : aucune réécriture totale, pas de RTOS, et respect strict du temps réel audio.
