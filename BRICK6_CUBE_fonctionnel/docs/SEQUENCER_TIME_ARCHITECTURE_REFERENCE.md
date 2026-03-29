# SEQUENCER_TIME_ARCHITECTURE_REFERENCE

## 1) Objet du document

Ce document fige la référence d’architecture temporelle du séquenceur BRICK6.

Il sert de base de travail pour les prochaines passes (Codex/humain) afin de:
- conserver un cap cohérent,
- éviter les régressions d’ordre d’exécution,
- découpler progressivement `seq_runtime.c` sans refonte « big bang ».

Ce document n’implémente rien: il définit un contrat et une trajectoire.

---

## 2) Vision cible

Le séquenceur est conçu comme un **domaine de temps contrôle / MIDI autonome**.

Principes:
- Le séquenceur (comme futurs LFO/automation) est un moteur d’automatisation d’événements/paramètres.
- Il ne doit pas être conceptuellement maître par le temps audio.
- Une horloge dédiée (ex: timer hardware) est une direction plausible à terme.
- Cette migration timer n’est **pas** dans le périmètre courant.

Objectif de court/moyen terme: nettoyer le contrat temporel et les frontières de modules pour rendre cette migration ultérieure possible et sûre.

---

## 3) État réel actuel

Points déjà solides:
- modèle séquenceur dédié,
- runtime séquenceur séparé,
- persistance versionnée,
- architecture track-aware présente,
- infra p-lock fonctionnelle,
- live rec note présent,
- clock interne/externe déjà supportée,
- transport + préroll déjà intégrés.

Limites actuelles:
- domaine temps encore lié à `engine_tasklet` / `engine_tick_count` / superloop,
- `seq_runtime.c` concentre encore trop de responsabilités (clock + transport + boundaries + scheduler + live rec + guards).

Conclusion: la base n’est pas à jeter; le gap principal est l’ordonnancement temporel et le monolithisme runtime.

---

## 4) Contrat de temps retenu (canonique)

Pipeline canonique:
1. tick/clock
2. gate transport
3. préroll/count-in
4. boundary kernel
5. apply/restore p-lock
6. enqueue PLAY
7. dispatch événements dus
8. publication d’état

Règles fortes:
- le préroll passe avant tout running réel,
- scheduler PLAY en 3 phases strictes:
  1) décision au boundary,
  2) enqueue horodaté,
  3) dispatch quand dû,
- live rec:
  - `NOTE/VEL/MICTIM` au note-on,
  - `LEN` au note-off,
  - flush sur STOP / disarm.

---

## 5) Contrat d’ordre d’exécution

### 5.1 Frontière de step (ordre recommandé)

Ordre de référence à préserver:
1. gate transport,
2. préroll,
3. boundary,
4. apply/restore,
5. scheduling PLAY,
6. dispatch,
7. publication.

Contraintes:
- aucune décision PLAY avant apply/restore du step courant,
- aucun NOTE ON après STOP,
- boundary initial post-START traité explicitement (éviter off-by-one).

### 5.2 Transitions transport retenues

- **START**: initialise le contexte, entre en `START_PENDING` si count-in actif, sinon passe en running réel.
- **STOP**: purge/flush/restore/panic dans un ordre stable.
- **CONTINUE**: reprend sans reset destructif du playhead.
- **préroll -> running**: transition uniquement quand count-in consommé.

Principes transverses:
- purge queue PLAY au STOP/abort,
- restore locks selon contrat runtime,
- panic/all-notes-off cohérent et idempotent.

---

## 6) Modules cibles identifiés (référence)

Découpage cible (progressif):
- `seq_play_scheduler`
- `seq_output_guard`
- `seq_boundary_engine`
- `seq_live_rec_capture`
- `seq_transport_fsm`
- `seq_clock_bridge`
- `seq_runtime` (façade/orchestrateur)

### 6.1 `seq_play_scheduler`
- Possède la queue PLAY.
- Fait décision/enqueue/service.
- N’implique pas le transport.

### 6.2 `seq_output_guard`
- Possède le tracking notes actives.
- Fait panic/all-notes-off et garde-fou STOP.

### 6.3 `seq_boundary_engine`
- Fait advance playhead (quand autorisé), détection boundary,
- apply/restore p-lock,
- update `prev_step` / `prev_step_valid`,
- produit des boundary hits pour le scheduler.

### 6.4 `seq_live_rec_capture`
- Capture timing live-rec,
- écrit `NOTE/VEL/MICTIM` au note-on,
- clôture `LEN` au note-off,
- supporte flush pending.

### 6.5 `seq_transport_fsm`
- Porte les états/transitions transport (`STOPPED`, `START_PENDING`, `RUNNING`),
- gère start/stop/continue/abort,
- décide les autorisations temporelles (advance/schedule/rec actif) via outputs de gate.

### 6.6 `seq_clock_bridge`
- Abstraction source clock INT/EXT,
- conversion pulses -> boundaries,
- suivi validité tempo externe,
- découplé de la logique musicale.

### 6.7 `seq_runtime`
- Reste orchestrateur et façade API publique.
- Garantit l’ordre global des phases.
- Coordonne les modules sans ré-absorber leurs responsabilités.

---

## 7) Interfaces internes déjà cadrées (niveau architecture)

### 7.1 `seq_play_scheduler` (minimales)
- init/reset,
- schedule step,
- service due,
- clear/flush queue,
- diagnostics (optionnel).

### 7.2 `seq_output_guard` (minimales)
- init/reset,
- note_on_seen / note_off_seen,
- panic_all / all_notes_off,
- diagnostics (optionnel).

### 7.3 `seq_boundary_engine` (minimales)
- init/reset,
- advance tracks,
- process boundaries,
- retour des boundary hits.

### 7.4 `seq_transport_fsm` (minimales)
- init/reset,
- request_start / request_stop / request_continue,
- on_boundary_pulse,
- abort_pending,
- on_clock_source_change,
- export des gates transport.

Note: ces interfaces sont contractuelles (architecture), pas encore extraites en code dans cette passe.

---

## 8) Trajectoire de découplage retenue

Ordre de travail validé:
1. extraire `seq_play_scheduler` + `seq_output_guard`,
2. extraire `seq_boundary_engine`,
3. extraire `seq_live_rec_capture`,
4. extraire `seq_transport_fsm`,
5. extraire `seq_clock_bridge`.

Pourquoi cet ordre:
- il réduit d’abord la dette monolithique avec un risque limité,
- il stabilise le noyau boundary avant de toucher transport/clock,
- il prépare une migration temporelle plus autonome sans rupture brutale.

---

## 9) Invariants non négociables

- Pas de malloc.
- Pas de big bang / pas de réécriture totale.
- Préserver le comportement musical existant pendant le découplage.
- Préserver l’ordre canonique des phases temporelles.
- Une seule autorité d’orchestration (`seq_runtime`) pour l’ordre global.
- STOP doit rester sûr (pas de NOTE ON tardif; panic cohérent).
- Les extractions sont incrémentales et testables étape par étape.

---

## 10) Ce qu’on ne veut pas

- Refonte globale non bornée.
- APIs surdimensionnées « au cas où ».
- Mélange des responsabilités entre modules.
- Couplage implicite supplémentaire au global runtime.
- Décision de migration timer hardware dans ce document (hors périmètre).

---

## 11) Statut du document

- Statut: **référence active** pour les prochaines passes séquenceur.
- Portée: architecture temps / transport / boundary / scheduler / live-rec.
- Mise à jour: uniquement quand un contrat est explicitement revalidé.
