# Audit architecture STM32 / CubeIDE pour intégration séquenceur 8 tracks (V1)

Date: 2026-03-25

## 1) Résumé exécutif

- Le projet est **déjà track-aware** et possède des briques solides: sélection de track active, résolution UI par template/family/type, moteur de hall modes, et base de temps audio-dérivée (`engine_tasklet`).
- En revanche, le **mode SEQ actuel est nominal uniquement**: pas de moteur de pas, pas de stockage steps/p-locks, pas d’UI dédiée, pas de runtime d’application des locks.
- La logique LED est aujourd’hui **statique par scène** (halls verts en mode non-KBD/ARP), donc non compatible avec un feedback de séquenceur (trigs + playhead).
- La persistance “projet/séquence” n’est pas en place: le repo contient des sauvegardes de calibration et de WAV/sample loading, mais pas de sérialisation structurée d’états track + séquences.
- Le meilleur chemin d’intégration consiste à **séparer clairement**:
  1. état UI SEQ,
  2. état édition step/p-lock,
  3. état runtime lecture (transport/tick),
  4. état persistant projet.
- Recommandation: implémentation incrémentale en 11 phases (0→10) sans casser KEYBOARD/ARP, avec refactors minimaux ciblés.

---

## 2) Ce que dit `ARCHITECTURE_GLOBAL.md`

> Note: le fichier demandé par l’utilisateur (`docs/ARCHITECTURE_GLOBAL.md`) n’existe pas à la racine du workspace; le document trouvé est `BRICK6_CUBE_fonctionnel/docs/ARCHITECTURE_GLOBAL.md`.

Le document pose les invariants suivants:

- Architecture firmware STM32H743 track-aware, UI template-based, audio bloc temps réel.
- Track active via `SHIFT` puis `HALL 0..7`.
- Hall modes structurants: `SEQ`, `KBD`, `ARP`; `SHIFT+HALL9` (KBD), `SHIFT+HALL10` (ARP), double tap pour ouvrir UI associée.
- Ensembles UI stables: `CFG`, `COLORS`, `TONE`, `KEYBOARD`, `ARP`.
- LEDs: boutons param verts + actif blanc; `ARP` réutilise la scène LED `KEYBOARD`; remaps explicites recommandés.
- ARP sync: `Int`, `Clock`, `Free` avec `Clock` préparé.
- Règle de maintenance: toute évolution d’architecture structurante doit mettre à jour la doc.

Lecture: le document anticipe SEQ au niveau conceptuel, mais pas son implémentation concrète actuelle.

---

## 3) Architecture actuelle réellement observée dans le code

## 3.1 Boucle applicative et ownership runtime

- `main.c` orchestre la superloop: `brick6_app_process()`, USB host/MIDI polling, `ui_tasklet_poll()` calé sur `engine_tick_count`, rendu OLED périodique.
- `brick6_app_init.c` centralise init audio, synthés, `engine_tasklet`, `param_store`, hall loop, runtime MIDI.
- `engine_tasklet.c` fournit une base de temps issue de l’audio (ticks de 32 frames, soit 1500 Hz à 48 kHz), hors IRQ.

=> Point d’ancrage propre pour futur scheduler SEQ: **tasklet audio-cadencé** (stable, déterministe, déjà en place).

## 3.2 Track active / family / type

- `ui_core.c` porte l’état track/UI global (`g_ui_track_state`), incluant:
  - `active_track`
  - `track_configs[8]`
  - `hall_mode`
  - suppression d’événements hall notes lors des actions SHIFT+halls
- Sélection track active: SHIFT must be down + hall edge press, logique consommante et robuste.
- Validation family/type et exclusivité `Input1..4` déjà gérées.

=> Base très compatible avec un séquenceur par track.

## 3.3 Hall modes et navigation

- Enum existante: `SEQ`, `KEYBOARD`, `ARP`.
- Déclencheurs mode shift+halls définis table-driven dans `ui_core.c`:
  - hall index 8 → KEYBOARD
  - hall index 9 → ARP
- Double tap détecté via timestamp pour ouvrir page template cible.
- Mode SEQ est présent comme mode par défaut/label, mais **pas de comportement step editor**.

## 3.4 UI param/page/template

- Navigation bouton→page via table `ui_navigation.c`.
- UI template générique dans `ui_template_page.c` + familles résolues par `(template family, track family, track type)`.
- Familles enregistrées: `COLORS`, `CFG`, `TONE`, `KEYBOARD`, `ARP`.
- Rendu header dans `ui_renderer_template.c`:
  - track active,
  - label runtime track,
  - label hall mode (`KBD±x`, `ARP±x`, sinon `SEQ`).

=> L’architecture UI est réutilisable pour ajouter une **page param SEQ** sans redesign.

## 3.5 Entrées halls / keyboard / arp

- `hall_juno_midi.c` route les halls vers clavier uniquement si mode `KEYBOARD` ou `ARP`.
- En mode non-KBD/ARP (donc SEQ aujourd’hui), pas de logique d’édition steps, les halls ne servent pas à un séquenceur.
- `keyboard_runtime.c` encapsule proprement keyboard + arp et hooks de changement mode/track.

## 3.6 Clock / timing / transport

- Base temps déterministe: `engine_tasklet` (audio-derived).
- ARP interne: utilise encore `HAL_GetTick()` avec BPM interne fixe (120) dans `keyboard_arp.c`.
- MIDI clock API présente (`midi_clock_*`), décodage F8/FA/FB/FC en RX présent, mais intégration scheduler global incomplète.

=> Il existe des briques, mais **pas de transport séquenceur unifié**.

## 3.7 Persistance

- Persistance repérée: calibration halls, gestion WAV/samples.
- Pas de couche projet/snapshot globale pour track configs + séquences + locks.
- `param_store` est un double-buffer runtime, pas une persistance durable projet.

## 3.8 LEDs / feedback visuel

- `led_rgb.c` applique des scènes fixes:
  - halls en vert par défaut,
  - scène KBD/ARP dédiée (bleus/omnichord),
  - boutons param: actif blanc, autres verts.
- Aucune notion runtime de playhead, trigs step, page visible, priorité de couches SEQ.

=> La pile LED doit être étendue pour SEQ, mais base remap/layers existe déjà.

---

## 4) Écarts éventuels entre le document et le code

1. **SEQ annoncé vs SEQ implémenté**
   - Doc: SEQ hall mode structurant.
   - Code: enum/label existe, mais pas de moteur step/p-lock/UI dédiée.

2. **UI ensembles stables**
   - Doc: ensemble `TONE` existe (ok).
   - Code: fichier/page historique encore nommé `ui_page_template_dx7.*` mais sert aussi `TONE` MonoB; naming non aligné avec la sémantique produit.

3. **Timing “Clock ready”**
   - Doc ARP: Clock préparé.
   - Code: vrai, mais pas encore branché à un scheduler commun; on reste en logique partiellement locale.

4. **Persistance projet**
   - Doc/objectif produit sous-entendent persistance de states.
   - Code actuel n’expose pas de persistance structurée de séquence track-wise.

---

## 5) Points bloquants / dettes techniques

## Bloquants pour V1 SEQ

- Absence de modèle de données séquence (track/page/steps/p-lock entries).
- Absence de moteur runtime advance step + transport + lecture page-aware.
- Absence de mapping halls→édition steps en mode SEQ.
- Absence de logique LED séquenceur (trig + playhead blanc).
- Absence de persistance projet des séquences.

## Dettes techniques importantes (mais adressables)

- Couplage `ui_core` (track+hall+navigation+mode logic) assez dense.
- `keyboard_arp` timing en `HAL_GetTick()` plutôt qu’une clock centrale.
- Nommage TONE/DX7 partiellement hérité (risque confusion long terme).
- Pas de notion “p-lockable par ensemble” dans les métadonnées UI/param actuelles.

---

## 6) Architecture cible recommandée

## 6.1 Modules proposés (minimaux)

### A. `Seq/seq_model.[ch]` (données persistables)

Responsabilité:
- Définir le format mémoire V1:
  - 8 tracks fixes,
  - 64 steps max/track,
  - trig on/off,
  - liste sparse de p-lock entries par step.

Proposition de structure V1 (simple + embarqué):
- `seq_project_t` 
  - `seq_track_t tracks[8]`
- `seq_track_t`
  - `length` (1..64)
  - `play_pos` runtime (non persistant)
  - `steps[64]`
- `seq_step_t`
  - `flags` (`TRIG`, `MUTED`, etc. réserve)
  - `lock_count`
  - `lock_index_start` (pool compact)
  - `color_hint` (V1=derived simple, V2=combinaisons)
- pool global fixe d’entrées locks (évite malloc)
  - entrée = `{param_id, value_q15/u16, source_set_id}`

### B. `Seq/seq_runtime.[ch]` (lecture temps réel)

Responsabilité:
- Clock/tick advance pas (interne/externe).
- Position réelle par track.
- Application des p-locks à l’instant step.
- API lecture page visible pour LEDs/UI.

### C. `Seq/seq_edit.[ch]` (édition)

Responsabilité:
- Toggle step (vide ↔ trig).
- Multi-sélection steps maintenus.
- Écriture p-locks par deltas relatifs (offset editing).
- Gestion “current page 1..4” + mapping hall 0..15 ↔ step local/global.

### D. `Seq/seq_plock_router.[ch]`

Responsabilité:
- Décider si paramètre édité doit écrire lock selon:
  - mode SEQ actif,
  - steps maintenus,
  - ensemble p-lockable.
- Appliquer stratégie “flag p-lockable par ensemble”.

### E. `Seq/seq_persistence.[ch]`

Responsabilité:
- Save/load de `seq_project_t` dans projet.
- Versioning simple de format (header magic+version).

## 6.2 Intégration UI / hall mode

- Conserver `ui_core` comme autorité mode/track.
- Ajouter trigger SHIFT+HALL11 (index 10) dans table mode triggers.
- Double tap HALL11 → `UI_PAGE_TEMPLATE_SEQ`.
- En mode SEQ:
  - `BTN_TRANSPOSE_UP/DOWN` changent page visible (1..4),
  - halls 1..16 togglent steps de la page courante.

## 6.3 UI param SEQ

Ajouter famille template `UI_TEMPLATE_FAMILY_SEQ` + page `ui_page_template_seq.c`.
Pages recommandées V1:
1. `MAIN`: Length, Scale (future), ClockSrc, TrackMode
2. `PLAY`: Rate, Swing, Direction, Reset
3. `REC`: Quantize, RecMode (future), Legato (future), -
4. `TOOLS`: Copy, Paste, Clear, -

(En V1, certains params peuvent être placeholders read-only si pas encore câblés.)

## 6.4 P-lock “flag par ensemble”

Ajouter un registre simple:
- `enum ui_template_family_id_t` -> métadonnée `p_lockable` bool.
- V1: `COLORS=true`, `TONE=true`, autres false.

Au moment d’un mouvement encodeur:
- si steps tenus et ensemble courant p-lockable → écrire locks.
- sinon comportement normal param runtime.

## 6.5 LED architecture cible

Séparer 3 couches conceptuelles:
1. `StepStateLayer`: trig on/off, sélection steps, couleur de base.
2. `PlayheadLayer`: blanc prioritaire, lié position réelle track.
3. `UIOverlayLayer`: états mode/navigation.

Règles V1:
- trig actif = vert
- playhead = blanc
- si playhead sur step hors page visible: rien à afficher
- couleur finale = fusion de couches (playhead prioritaire)

Préparation V2 couleurs combinées:
- `seq_step_t.color_hint` ou `lock_set_mask` (bits par ensemble locké)
- map couleur dérivée centralisée dans `seq_led_palette.c`

## 6.6 Séparation des états

- **UI state**: page visible, sous-page, focus, mode hall.
- **Edit state**: steps maintenus, step de référence, page courante SEQ.
- **Runtime state**: play_pos, transport running, clock source.
- **Saved state**: pattern data + paramètres séquenceur par track.

Ne jamais fusionner ces plans dans une seule struct globale.

---

## 7) Plan d’implémentation étape par étape

## Phase 0 — Refactors préparatoires

- Objectif: préparer points d’extension sans feature visible.
- Modules touchés:
  - `ui_core.c/h` (hooks mode SEQ propres)
  - `ui_navigation.c` (ajout route vers UI SEQ)
  - `ui_template_page.*` (ajout family `SEQ`)
- Dépendances: aucune runtime lourde.
- Risques: régression navigation/template.
- Validation:
  - build ok,
  - KBD/ARP inchangés,
  - mode label intact.

## Phase 1 — Hall mode SEQ minimal

- Objectif: activer SHIFT+HALL11 + double tap vers UI SEQ.
- Modules:
  - `ui_core.c`, `ui_core.h`, `ui_page_manager` registrations.
- Risques: conflit avec track select SHIFT+halls.
- Validation: scénarios SHIFT before HALL respectés.

## Phase 2 — Pages + édition step on/off

- Objectif: page 1..4, halls 16 steps visibles, toggle trig.
- Modules:
  - `Seq/seq_model`, `Seq/seq_edit`, `hall_juno_midi.c` (dispatch mode SEQ), `ui_core.c` (transpose remap en SEQ).
- Risques: interférence KBD.
- Validation:
  - toggle step stable,
  - transpose ± change page seulement en SEQ.

## Phase 3 — Feedback LED minimal (vert + chenillard blanc)

- Objectif: visibilité claire trig+playhead page-aware.
- Modules:
  - `led_rgb.c` (+ éventuel `seq_led_view.c`).
- Risques: clignotements/perf si recalcul complet trop fréquent.
- Validation:
  - trig verts,
  - playhead blanc,
  - invisibilité hors page.

## Phase 4 — Infrastructure p-lock

- Objectif: stockage sparse locks + flag p-lockable par ensemble.
- Modules:
  - `Seq/seq_model`, `Seq/seq_plock_router`, `ui_template_page` metadata.
- Risques: overflow pool locks.
- Validation:
  - création/suppression locks par édition,
  - mémoire bornée.

## Phase 5 — Lecture runtime des p-locks

- Objectif: appliquer locks au passage step.
- Modules:
  - `Seq/seq_runtime`, `param_store/param_registry` via API contrôlée.
- Risques: glitch audio si applique dans mauvais contexte.
- Validation:
  - locks entendus/visibles,
  - pas de surcharge CPU notable.

## Phase 6 — Persistance projet

- Objectif: save/load séquences par track.
- Modules:
  - `Seq/seq_persistence`, couche Storage projet.
- Risques: compat versions.
- Validation:
  - power cycle/load => séquences intactes.

## Phase 7 — Clock externe / MIDI clock

- Objectif: transport sync externe robuste.
- Modules:
  - `Seq/seq_runtime`, `midi.c` integration points, éventuellement timer clock source.
- Risques: jitter, drift, start/stop edge cases.
- Validation:
  - lock clock stable sur source externe.

## Phase 8 — Copie inter-tracks / mémoire séquences

- Objectif: copy pattern avec compat partielle.
- Stratégie:
  - copier trigs,
  - copier locks seulement si param présent/cible valide,
  - ignorer le reste proprement.
- Validation: pas d’écriture invalide ni crash.

## Phase 9 — Extensions notes/trig/realtime rec

- Objectif: préparer ensemble futur notes/trigs.
- Dépendance: infra p-lock déjà stable.
- Validation: architecture extensible sans casser V1.

## Phase 10 — Couleurs avancées par combinaisons d’ensembles

- Objectif: colorimétrie dérivée (`TONE+COLORS`, etc.).
- Modules: `seq_led_palette`, `seq_model` metadata.
- Validation: logique additive sans dette de coupling.

---

## 8) Risques principaux

- **Architecture**: surcharger `ui_core` si on ajoute SEQ sans extraire `seq_*`.
- **Perf CPU**: recalcul LED complet à 1500 Hz inutile; préférer cache/dirty flags.
- **Mémoire**: snapshot complet de params par step exploserait RAM; rester sparse.
- **Timing**: `HAL_GetTick()` insuffisant pour scheduler précis commun; privilégier base audio tasklet.
- **UI cohérence**: conflit transpose KBD vs SEQ si dispatch non strict par mode.
- **Maintenabilité**: absence de frontières module claire (UI/edit/runtime/persist).

---

## 9) Recommandations finales

1. **Absolument éviter** snapshot complet de paramètres par step.
2. Utiliser un **pool fixe borné** pour locks (no malloc, no fragmentation).
3. Introduire une API d’application lock atomique (`seq_runtime_apply_step_locks(track, step)`) et éviter les écritures diffuses.
4. Garder `ui_core` comme orchestrateur, mais déplacer la logique métier SEQ dans `Seq/*`.
5. Ajouter un flag `ensemble_is_plockable` au niveau famille template (pas param-par-param en V1).
6. Pour copie inter-tracks hétérogènes: validation param ID côté cible + skip propre.
7. LED: séparer état step, état playhead, rendu couleur; préparer `lock_set_mask` dès V1.
8. Timing: clock interne dérivée `engine_tasklet`; clock externe via bridge MIDI ensuite.
9. Persistance: format versionné dès V1 pour éviter migration douloureuse.
10. Garder les invariants existants non négociables: `SHIFT before HALL`, KBD/ARP intacts.

---

## 10) Questions ouvertes réellement bloquantes

1. Souhaite-t-on un **transport global unique** (play/stop commun) dès V1 ou un SEQ “always running” interne au début ?
2. La longueur track V1 doit-elle être **forcément 64** ou configurable (16/32/48/64) dès phase 2 ?
3. Lorsqu’un paramètre est p-locké sur step, la valeur “courante UI” doit-elle refléter immédiatement le lock sélectionné (sur step maintenu) ou rester sur la valeur live track ?
4. Politique de saturation du pool de locks: bloquer écriture, remplacer oldest step-local, ou compactage ?

