# SEQUENCER MASTER REFERENCE (V1)

Référence unique de cadrage/roadmap pour implémenter le séquenceur dans le firmware STM32/CubeIDE, alignée avec `docs/ARCHITECTURE_GLOBAL.md` et les décisions figées des passes précédentes.

---

## 1. Contexte et objectif

- Objectif produit V1: séquenceur step-based inspiré Elektron, intégré sans casser l’architecture track-aware existante.
- V1: 8 tracks fixes, 4 pages de 16 steps, max 64 steps/track, extensible plus tard.
- Priorité: robustesse runtime embarquée (C/STM32), simplicité debug, zéro malloc, séparation UI/runtime/persistance.

---

## 2. Décisions figées

- Hall modes:
  - HALL 9 = KBD
  - HALL 10 = ARP
  - HALL 11 = SEQ
- Activation:
  - SHIFT + HALL 11 => active SEQ
  - double clic HALL 11 => ouvre UI param SEQ
- En mode SEQ: transpose +/- change la page visible (pas la transposition clavier).
- Transport:
  - global unique
  - PLAY depuis STOP => démarre au step 0
  - PLAY pendant lecture => STOP
- Séquence:
  - 1 séquence/track en V1
  - longueur libre par track (1..64)
  - page SEQ mémorisée par track
- P-lock:
  - ensembles p-lockables V1: COLORS + TONE
  - stratégie: flag p-lockable par ensemble
  - stockage sparse (uniquement paramètres modifiés)
- Limites:
  - max p-locks/step V1 = 16
  - limite utilisateur = par step (pas de limite globale produit indépendante)

---

## 3. Comportements utilisateur figés

### 3.1 Steps / édition

- En mode SEQ, les 16 halls visibles représentent les 16 steps de la page courante.
- Tap hall:
  - step vide => ajoute trig
  - step actif => enlève trig
- 4 pages fixes de 16 steps, navigation via transpose +/-.

### 3.2 LEDs

- Trig actif = vert.
- Playhead lecture = blanc.
- Priorité visuelle: playhead > trig.
- Si playhead hors page affichée: non visible.

### 3.3 Multi-step edit

- Sélection: maintenir un ou plusieurs steps.
- Référence multi-step: plus petit index tenu (`min held`).
- Règle delta:
  - lock existant => `old + delta`
  - lock absent => `base_track + delta`
- Clamp individuel par step/param.
- Si lock absent et création nécessaire: auto-create lock.

### 3.4 Copy / paste

- Maintenir step(s) + COPY => copie.
- Maintenir step(s) + PASTE => paste.
- Maintenir step(s) + SHIFT + PASTE => clear steps tenus.
- Paste avec positions relatives (offsets depuis ancre source).
- Inter-track autorisé:
  - copier ce qui est compatible
  - ignorer incompatible
- Feedback minimal:
  - `PASTE PARTIAL` (incompatibilités)
  - `PASTE TRUNC` (hors longueur)
- Pas de remapping “intelligent” complexe en V1.

### 3.5 Track focus

- Changement de track active pendant lecture: lecture continue.
- Seul le focus UI change.

---

## 4. Règles techniques figées

### 4.1 Modèle step/p-lock

- Step = état trig + liste de locks (sparse).
- P-lock = override paramétrique appliqué au step.
- Un step peut contenir 0..16 locks (V1).

### 4.2 Pipeline runtime p-lock (ordre obligatoire)

À chaque boundary de step (par track):
1. calculer locks du step courant (`next_mask`)
2. restaurer params qui étaient lockés au step précédent mais plus lockés (`prev_mask & ~next_mask`) vers **base track**
3. appliquer locks du step courant
4. déclencher trig (si présent)

=> garantit absence de dérive d’état et cohérence note/param.

### 4.3 Transport/clock

- Horloge interne basée domaine audio (`engine_tasklet` / ticks audio), pas `HAL_GetTick()` pour moteur séquenceur.
- Clock MIDI externe prévue (point d’extension), hors V1 minimal.

### 4.4 Séparation des états

- UI state (focus/page)
- Edit state (sélection, référence)
- Runtime state (playhead/clock)
- Saved state: persistance locale SEQ retirée temporairement (runtime RAM-only en attendant persistance globale projet/state).

Aucune fusion de ces plans en une seule autorité globale.

---

## 5. Architecture cible

## 5.1 Modules retenus

- `seq_model`
  - Rôle: données persistées track/step/locks + index free-list.
  - Dépendances: aucune lourde (types/constants).
  - Pourquoi: centraliser le format de vérité.

- `seq_runtime`
  - Rôle: avance steps, apply/restore locks, playhead runtime.
  - Dépendances: `seq_model`, `seq_param_iface`, `seq_transport`.
  - Pourquoi: isoler le temps réel du reste.

- `seq_edit`
  - Rôle: toggle trig, multi-step delta, création/suppression locks.
  - Dépendances: `seq_model`, `seq_param_iface` (bornes/clamp/base).
  - Pourquoi: encapsuler comportement édition utilisateur.

- `seq_param_iface`
  - Rôle: API explicite track/param pour apply lock / restore base / compat.
  - Dépendances: couche paramètres/runtime existante (`param_registry` refactor ciblé).
  - Pourquoi: éviter dépendance à `active_track` UI.

- `seq_clipboard`
  - Rôle: copy/paste steps (offsets relatifs + locks) + feedback partiel/trunc.
  - Dépendances: `seq_model`, `seq_param_iface`.
  - Pourquoi: logique copy/paste déterministe et isolée.

- `seq_led`
  - Rôle: calcul état LED step/playhead par page.
  - Dépendances: `seq_runtime`, `seq_model`, `led_remap/led_layer`.
  - Pourquoi: ne pas surcharger `led_rgb.c` avec logique métier.

- `seq_persistence`
  - Statut actuel: module local mis de côté, non branché au boot/runtime (attente persistance globale projet/state).
  - Dépendances: storage projet.
  - Pourquoi: versioning maîtrisé et évolutif.

- `seq_transport`
  - Rôle: état PLAY/STOP global, source clock INT/EXT.
  - Dépendances: `engine_tasklet`, (future) MIDI clock bridge.
  - Pourquoi: autorité transport dédiée.

## 5.2 Intégration code existant (principale)

- `ui_core`: activation mode SEQ + remap transpose en mode SEQ.
- `ui_template_page`/`ui_navigation`/`ui_page_manager`: page SEQ minimale.
- `ui_param`: redirection édition vers `seq_edit` quand steps tenus en mode SEQ.
- `led_rgb`: branchement rendu seq via couches existantes.
- `brick6_app_process`: appel `seq_runtime_process()` dans la boucle centrale.
- `param_registry` (ciblé): exposer apply explicite par track pour params p-lockables.

---

## 6. Décisions mémoire

- Pas de malloc.
- Runtime critique en RAM interne (D2 acceptable).
- Éviter SDRAM externe pour steps/pool/runtime actif.
- Encodage V1:
  - `param8` sur 8 bits
  - `value16` sur 16 bits
- Pool global = matérialisation mémoire de la capacité théorique:

```c
SEQ_PLOCK_POOL_CAP = SEQ_TRACK_COUNT * SEQ_MAX_STEPS * SEQ_STEP_MAX_LOCKS
```

Avec V1:
- `8 * 64 * 16 = 8192` entrées.

Impact estimatif (ordre de grandeur):
- pool seul (8 bytes/entry): ~64 KB
- + steps/runtime/clipboard: budget compatible RAM interne cible annoncée.

Message utilisateur:
- pas de message “POOL FULL” en tant que limite produit distincte.
- limite métier visible: `STEP FULL` uniquement.

---

## 7. Point(s) encore ouvert(s)

Point bloquant restant:
- mapping persistant `param8` exact/stable pour les paramètres lockables COLORS/TONE.

Autres points bloquants:
- aucun identifié à ce stade pour démarrer l’implémentation V1.

---

## 8. Plan d’action par étapes

## Étape 0 — Préparation / refactors ciblés
- Objectif: poser types/modules SEQ sans changer comportement utilisateur.
- Fichiers/modules: création `Seq/*`, hooks minimaux dans `ui_core`, `brick6_app_process`, `ui_template_page`.
- DoD: build propre, KBD/ARP inchangés.
- Risques: couplage involontaire ui_core.
- Validation: smoke tests navigation/hall modes existants.

## Étape 1 — Activation hall mode SEQ
- Objectif: SHIFT+HALL11, double tap HALL11 vers page SEQ.
- Fichiers/modules: `ui_core.c/h`, `ui_page_manager`, `ui_navigation`.
- DoD: entrée/sortie mode SEQ sans régression KBD/ARP.
- Risques: conflits SHIFT-before-HALL.
- Validation: scénarios de taps simples/doubles.

## Étape 2 — Édition step on/off
- Objectif: toggle trig sur 16 steps page courante.
- Fichiers/modules: `seq_model`, `seq_edit`, hook halls mode SEQ.
- DoD: édition stable pages 1..4.
- Risques: mapping hall/page incorrect.
- Validation: pattern simple 16/64 steps.

## Étape 3 — LEDs minimales
- Objectif: trig vert + playhead blanc page-aware.
- Fichiers/modules: `seq_led`, `led_rgb`, `led_layer`.
- DoD: priorités LED correctes, invisibilité playhead hors page.
- Risques: surcharge CPU LED.
- Validation: test lecture multi-pages.

## Étape 4 — Infrastructure p-lock
- Objectif: pool + step lock lists + limites/step.
- Fichiers/modules: `seq_model`, `seq_edit`.
- DoD: create/update/delete locks bornés.
- Risques: corruption free-list.
- Validation: tests saturation par step.

## Étape 5 — Runtime apply/restore
- Objectif: appliquer/restaurer locks selon pipeline figé.
- Fichiers/modules: `seq_runtime`, `seq_param_iface`, `param_registry` ciblé.
- DoD: pas de dérive param entre steps lockés/non lockés.
- Risques: apply sur mauvaise track si API implicite.
- Validation: scénario lock step N puis restore N+1.

## Étape 6 — Transport global / play-stop
- Objectif: PLAY start/stop global, start depuis step 0.
- Fichiers/modules: `seq_transport`, `ui_event`/gestion BTN_PLAY, `seq_runtime`.
- DoD: toggle fiable en boucle, tracks longueurs différentes OK.
- Risques: conflit consommation événement bouton.
- Validation: start/stop répétés, changement track active en lecture.

## Étape 7 — Copy/paste
- Objectif: COPY/PASTE + SHIFT+PASTE(clear) avec offsets relatifs.
- Fichiers/modules: `seq_clipboard`, `seq_edit`, hooks UI.
- DoD: paste local/inter-track, partial/trunc feedback.
- Risques: dépassement longueur, compat param.
- Validation: copies contiguës/non contiguës.

## Étape 8 — Persistance
- Statut actuel: rollback persistance locale SEQ.
- Décision: runtime SEQ en RAM only en attendant une persistance globale projet/state.

## Étape 9 — Clock externe
- Statut actuel: support MIDI realtime implémenté (F8/FA/FB/FC) côté runtime SEQ.
- Source clock active forcée sur INT pour le moment (préparation architecture INT/EXT en place, commutation runtime future).

## Étape 10 — Extensions futures
- Objectif: couleurs dérivées avancées, notes/trigs/realtime rec.
- Fichiers/modules: `seq_led` (palette via `lock_set_mask`), futurs ensembles.
- DoD: extensibilité sans casser V1.
- Risques: surcouplage UI/runtime.
- Validation: non-régression V1 + features incrémentales.

---

## 9. Règles de non-régression

Ne pas casser:
- logique track-aware existante,
- KBD/ARP existants,
- invariant SHIFT-before-HALL,
- navigation UI actuelle,
- cohérence LED existante hors mode SEQ,
- séparation UI/runtime/persistance.

Chaque étape doit être validée avec ces invariants avant merge.

---

## 10. Conclusion courte

Le cadre V1 est figé: comportements utilisateur, pipeline runtime, architecture module, politique mémoire et roadmap.
Le prochain travail est l’implémentation incrémentale (Étape 0 -> Étape 10) sans réouvrir les décisions déjà tranchées.

