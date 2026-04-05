# Audit d’architecture — Project Save/Load (V1)
_Date: 2026-04-05_

## Portée inspectée
- `project_v1.*`
- `project_sd_bank.*`
- `pattern_live_ram.*`
- `pattern_sd_bank.*`
- apply track config (`ui_set_track_family/type/...`)
- `track_runtime_refresh_all`
- `param_registry_apply_track_value`
- chemin UI `SETTINGS > PROJECT`

## Addendum implémenté — boot context minimal (MCU flash locale)
- Le contenu projet lourd reste **autorité unique SD** (`project_sd_bank` + `pattern_sd_bank`), sans duplication backend.
- Un mini contexte de reprise local MCU est ajouté en flash interne (secteur dédié) avec **5 champs uniquement** :
  - `version`
  - `valid`
  - `crc`
  - `active_project_slot`
  - `active_pattern_index`
- Écriture uniquement sur changement réel :
  1. après `project_v1_load_slot()` réussi (slot actif),
  2. après `project_v1_save_slot()` réussi (slot de save devient actif),
  3. après changement effectif de pattern active (`pattern_live_queue_slot` immédiat ou commuté sur boundary).
- Au boot: validation `version/valid/crc`, puis restore strict `project slot` -> `active pattern index`, sinon fallback silencieux sans bloquer la machine.
- La zone flash est réservée explicitement dans le linker (`BOOT_CTX_FLASH`, secteur bank2/sector7), séparée de la zone firmware applicative.

---

## 1) Ce qui est sain et doit être gardé

### 1.1 Séparation de base des couches (UI -> project_v1 -> storage)
- Le chemin UI appelle uniquement des APIs projet (`project_v1_load_slot/save/delete`), sans logique de sérialisation côté UI. C’est propre et doit rester ainsi.
- `project_v1` reste un orchestrateur léger (capture/apply + active slot metadata), ce qui est la bonne direction.

### 1.2 Snapshot pattern structuré et relativement complet
- `PatternSaveV1` capture séquence, cfg track, domaines params trackés, globaux tempo/clock/div/quant/swing.
- `pattern_live_capture_current()` lit explicitement l’état live (UI cfg track + seq_model + param_registry + seq_runtime), ce qui donne une “photo” cohérente d’intention.

### 1.3 Backends SD avec en-tête/version/checksum
- `pattern_sd_bank` et `project_sd_bank` valident magic/version/payload/checksum, ce qui est indispensable en embarqué.
- Le scan de slots mis en cache en RAM évite de faire un `f_stat` à chaque affichage UI.

### 1.4 Contrôle explicite des contraintes d’exclusivité track
- Les contraintes d’exclusivité INPUTx / DX7 / TB3 sont codées explicitement côté UI core (`ui_core_track_family_is_available`, `ui_track_type_is_available`).
- Cette logique est bonne en mode interaction utilisateur.

---

## 2) Fragilités structurelles observées (racines probables des bugs)

### 2.1 Couplage trop fort “restore projet” <-> APIs UI interactives
Le restore de track config passe par `ui_set_track_family/type/...`, qui sont des setters orientés interaction UI (validations, resync params actifs, callbacks clavier actif, contraintes d’exclusivité en ligne). Ça introduit des effets de bord pendant un restore bulk.

Conséquence:
- Le restore dépend d’états transitoires (track active, disponibilités instantanées, ordre exact d’application).
- En cas d’échec au milieu, l’état est partiellement appliqué sans rollback.

### 2.2 Pipeline d’apply non transactionnel
Dans `pattern_live_apply_snapshot`:
- on stop/panic,
- on applique config tracks,
- on refresh runtime,
- on pousse tous les params,
- puis seq, etc.

Mais si échec en milieu de route (ex: apply track cfg), on sort sans restauration/rollback et sans stratégie “ancien état intact”. On retombe donc sur des états mixtes.

### 2.3 `param_registry_apply_track_value` a des dépendances dynamiques fortes
Cette API:
- dépend de `track_runtime_refresh_track` (qui fait en fait un refresh global),
- dépend de résolution de target filter via contexte runtime/UI,
- dépend de bind_state moteur.

Utilisée en boucle massive pendant restore, elle peut réévaluer des contraintes à chaque param, créant de la variabilité et du coût.

### 2.4 `project_sd_bank_load_slot` modifie la bank PATTERN avant validation finale globale
Le load projet:
- lit records,
- restaure immédiatement chaque pattern dans `pattern_sd_bank` (ou delete),
- puis seulement à la fin vérifie checksum global du fichier projet.

Donc, en cas d’échec tardif (checksum global final, erreur I/O tardive), la PATTERN bank peut déjà être partiellement réécrite => état partiel persistant.

### 2.5 Save projet non strict sur l’intégrité de la bank source
Au save projet, si un pattern est marqué présent mais que `pattern_sd_bank_load_slot` échoue, le code force `has_data=0` et sérialise un payload vide au lieu d’échouer explicitement.

Conséquence:
- le projet sauvegardé peut perdre silencieusement des slots de bank.
- bug “ça a sauvé mais plus rien ne load pareil”.

### 2.6 Mélange de responsabilités “projet = live + mirror complet de la bank pattern SD”
Le format projet inclut:
- snapshot live (normal),
- ET miroir complet des 16x16 patterns de bank (lourd).

Ce design est viable, mais alors le load/save doit être transactionnel et strict. Actuellement il ne l’est pas.

### 2.7 I/O potentiellement lourdes et cascades de gates
`project_sd_bank_store_slot` relit 256 slots pattern via `pattern_sd_bank_load_slot` (acquisition gate pattern à répétition), puis écrit gros fichier projet.
Sur load, idem en sens inverse avec stores répétés.

Ce coût n’est pas forcément fatal, mais amplifie les zones d’échec et timing sensibles.

---

## 3) Pourquoi vous tournez en rond (diagnostic “allers-retours”)

1. **Frontière floue projet/pattern**: le projet agit à la fois comme snapshot live ET mécanisme de réplication de bank, sans transaction claire (prepare/commit/abort).
2. **Autorité live ambiguë pendant restore**: l’état runtime est reconstruit via APIs UI interactives + APIs param runtime dynamiques, donc résultat dépend de l’ordre et des états transitoires.
3. **Absence de mode restore dédié**: on utilise les mêmes setters que l’UI utilisateur au lieu d’un chemin backend atomique.
4. **Pas de rollback durable**: ni pour live state ni pour pattern bank SD en cas d’échec partiel.
5. **Erreurs “tolérées” au save** (fallback has_data=0) qui introduisent des corruptions logiques discrètes.

---

## 4) Architecture cible recommandée (V1 propre, réaliste)

## Décision A — Garder un fichier projet monolithique V1
Conserver le fichier `.PRJ` monolithique pour l’instant (simplicité UX, export/import slot unique), **mais** rendre le pipeline transactionnel.

## Décision B — Séparer explicitement 4 phases
1. **Capture** (live -> `ProjectSaveV1`)  
2. **Serialize/Deserialize** (`ProjectSaveV1` <-> fichier PRJ)  
3. **Stage bank restore** (préparer bank complète en zone temporaire ou journal)  
4. **Apply live snapshot** (track cfg + runtime params + seq) via backend restore

Aucune phase ne doit mélanger I/O et mutation runtime live sans barrière claire.

## Décision C — Introduire un backend restore de track config (sans setters UI interactifs)
Créer un chemin dédié type `track_config_restore_begin/apply/end`:
- ignore les callbacks UI interactifs pendant restore,
- applique familles/types/midi de manière atomique,
- effectue un check global de faisabilité avant commit,
- puis un seul refresh runtime + un seul sync UI final.

Les setters UI restent pour l’édition utilisateur, pas pour le bulk restore.

## Décision D — Param apply restore en mode “batch”
Pendant restore:
- figer/réduire les recalculs runtime (pas de refresh global par param),
- appliquer params via backend batch aware,
- faire un `track_runtime_refresh_all` à des points déterministes (par ex après track_cfg et avant batch tone/mix, puis final).

## Décision E — Restauration bank transactionnelle
Le load projet ne doit plus toucher `0:/PATTERN` avant validation complète du PRJ.

Option V1 pragmatique:
- lire/valider tout le PRJ,
- bufferiser records/patterns (ou stream vers fichiers temporaires),
- puis phase commit: remplacer bank.

Si mémoire limitée: journal minimal “manifest + progression” pour pouvoir reprendre/revenir proprement.

## Décision F — Politique d’erreur stricte
- Save projet: si lecture d’un slot pattern annoncé présent échoue => **échec save** (pas de fallback silencieux).
- Load projet: en cas d’échec avant commit => état live/pattern bank inchangé.
- En cas d’échec en commit, marquer état d’erreur explicite (code + statut UI), ne jamais retourner faux succès.

## Décision G — RAM vs SD
- **RAM**: snapshot live courant + buffer de travail restore + état transaction (petit).
- **SD**: source de vérité persistante des slots pattern et des projets.
- Pas besoin d’une “bank complète en RAM” permanente.

---

## 5) Plan concret de refactor (par étapes)

## Étape 1 — Instrumentation et contrat d’erreurs (indispensable)
- **But**: rendre visibles les causes et arrêter les faux positifs.
- **Fichiers**: `project_v1.*`, `project_sd_bank.*`, `pattern_live_ram.*`.
- **Garder**: APIs publiques existantes.
- **Remplacer**: retours bool seuls -> code erreur interne (enum), bool conservé en façade.
- **Risque**: faible.
- **Bénéfice**: diagnostic fiable et non-régression mesurable.

## Étape 2 — Durcir save projet (indispensable)
- **But**: empêcher perte silencieuse de bank.
- **Fichiers**: `project_sd_bank.c`.
- **Garder**: format PRJ V1.
- **Remplacer**: fallback `has_data=0` sur échec load pattern -> fail immédiat save.
- **Risque**: moyen (plus de SAVE FAIL visibles).
- **Bénéfice**: intégrité.

## Étape 3 — Load PRJ en deux phases validate -> commit (indispensable)
- **But**: plus de mutation PATTERN bank avant validation complète.
- **Fichiers**: `project_sd_bank.c`, éventuellement helper `project_bank_txn.*`.
- **Garder**: interface `project_sd_bank_load_slot`.
- **Remplacer**: restore immédiat dans la boucle de lecture -> staging puis commit.
- **Risque**: moyen.
- **Bénéfice**: fin des états partiels après LOAD FAIL.

## Étape 4 — Couche backend dédiée restore track config (indispensable)
- **But**: découpler restore bulk des setters UI interactifs.
- **Fichiers**: nouveau module ex. `Src/Core/track_config_restore.c`, adaptation `pattern_live_ram.c`.
- **Garder**: règles d’exclusivité existantes (INPUTx, DX7/TB3).
- **Remplacer**: `pattern_live_apply_track_config_block` direct via `ui_set_*`.
- **Risque**: moyen/élevé.
- **Bénéfice**: déterminisme restore.

## Étape 5 — Batch apply params runtime (indispensable)
- **But**: éviter `refresh_all` implicite à chaque param.
- **Fichiers**: `param_registry.c`, `pattern_live_ram.c`, possiblement `track_runtime.c`.
- **Garder**: logique de domaine param.
- **Remplacer**: boucle apply actuelle par mode batch (begin/apply/end).
- **Risque**: moyen.
- **Bénéfice**: perf + stabilité.

## Étape 6 — Sécuriser orchestration `project_v1_load_slot` (indispensable)
- **But**: pipeline explicite: load_file -> commit_bank -> apply_live -> commit_state.
- **Fichiers**: `project_v1.c`.
- **Garder**: API publique.
- **Remplacer**: orchestration actuelle linéaire minimale.
- **Risque**: faible/moyen.
- **Bénéfice**: lisibilité et invariants clairs.

## Étape 7 — Optimisations I/O (souhaitable, ensuite)
- **But**: réduire temps et usure SD.
- **Fichiers**: `project_sd_bank.c`, `pattern_sd_bank.c`.
- **Pistes**:
  - writes groupés + sync unique par transaction,
  - éviter open/close répétitifs quand possible,
  - éventuelle table CRC cache des slots pattern.
- **Risque**: moyen.
- **Bénéfice**: latence et robustesse long terme.

## Étape 8 — Extensions futures (souhaitable)
- auto-resume au boot,
- “dirty map” de bank pour save incrémental,
- meilleure UX projet (progression, erreurs détaillées),
- préparation sample streamer SD.

---

## 6) Optimisations raisonnables (sans alourdir inutilement)

1. **Ne pas faire de `track_runtime_refresh_all` dans chaque apply param** pendant restore (batch only).
2. **Préférer une seule séquence open/write/sync/close** par transaction (save/load commit).
3. **Éviter les delete/rewrite inutiles**: comparer manifest `has_data + checksum` avant d’écraser un slot PATTERN.
4. **Conserver scan slots en RAM** (déjà fait), mais invalider proprement après commit.
5. **Garder format monolithique V1** pour simplicité maintenant; n’introduire split assets que quand sample streamer arrivera.

---

## Priorisation finale

### Refactor indispensable (à faire maintenant)
- Étapes 1 à 6.

### Améliorations souhaitables plus tard
- Étapes 7 et 8.
