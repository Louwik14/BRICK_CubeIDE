# Audit de performance ciblé — SAVE/LOAD PATTERN & PROJECT
_Date: 2026-04-09_

## Portée et méthode
Audit statique ciblé des chemins:
- `pattern_live_ram.c` (capture/apply runtime)
- `pattern_sd_bank.c` (I/O PATTERN)
- `project_v1.c` (orchestration PROJECT)
- `project_sd_bank.c` (format PRJ + scan 16x16 + commit)
- `sd_diskio.c` (DMA, D-cache, scratch slow-path)

Aucune refonte appliquée dans cette passe.

---

## 1) Hiérarchie qualitative des coûts (par opération)

### SAVE pattern
1. **capture runtime (`pattern_live_capture_current`)**
   - Boucles `tracks x steps x plocks` + boucle `PARAM_COUNT x tracks`.
   - CPU pur, très dépendant de la volumétrie de paramètres/plocks.
2. **write payload SD (`f_write` payload) + `f_sync`**
   - I/O principal côté stockage.
3. **checksum payload (`pattern_sd_checksum`)**
   - Coût CPU linéaire sur `sizeof(PatternSaveV1)`.
4. open/header/close/path: coût secondaire.

### LOAD pattern
1. **apply runtime (`pattern_live_apply_snapshot`)**
   - Stop/panic, batch params, refresh runtime, apply séquence, reset playheads.
   - CPU dominant dès qu’on applique immédiatement (transport stop).
2. **read payload SD (`f_read`)**
   - I/O principal.
3. **checksum payload (`pattern_sd_checksum`)**
   - CPU linéaire sur payload.
4. open/header validation/close: secondaire.

### SAVE project
1. **double boucle 16x16 patterns (256 slots) — collecte + sérialisation**
   - Pour chaque slot: metadata (`get_slot_checksum`) + éventuellement load payload pattern + écriture record + écriture payload.
   - C’est le centre de coût dominant.
2. **write de fichier PRJ volumineux + `f_sync` final**
   - I/O important (records + payloads cumulés).
3. **checksums cumulés (project + records + payloads)**
   - CPU linéaire sur l’ensemble écrit.
4. capture `ProjectSaveV1` (inclut capture pattern live): coût notable mais généralement < coût boucle 256 + I/O.

### LOAD project
1. **validation phase 1 sur 256 records**
   - Lit tous records + payloads présents + checksum payload + checksum global + détection changements vs bank SD.
2. **si changements: commit phase 2**
   - Ré-ouverture PRJ + re-scan 256 records + réécriture des patterns modifiés (`store_slot_nosync`) / delete.
   - C’est souvent le plus gros bloc perçu comme “lent”.
3. **apply snapshot live (`project_v1_apply_snapshot` -> `pattern_live_apply_snapshot`)**
   - Coût CPU runtime/UI/seq après I/O.
4. update slot actif + commit boot context flash: coût faible (et skip si inchangé).

---

## 2) Origine des coûts: SD vs CPU/architecture

### Là où le coût est surtout SD I/O
- `f_read`/`f_write` de payloads pattern et records projet.
- `f_sync` (barrière de persistance).
- Ré-ouverture/reseek de fichier PRJ au load commit.
- Multiplication des open/read sur 256 patterns via APIs pattern.

### Là où le coût est surtout architecture/CPU
- Capture/apply runtime pattern (boucles paramètres/séquence/plocks).
- Calculs checksum répétés (payload pattern et checksum global projet).
- Scan 16x16 répété en phase validation puis phase commit (2 passes).
- Cascades d’appels metadata/payload pattern pendant save projet.

### DMA / D-cache / alignement
- Le chemin DMA + maintenance cache est bien en place, avec fast-path aligné et slow-path scratch pour buffers non alignés.
- Le goulet principal observé dans les chemins PROJECT est **au-dessus** (architecture I/O + double passe + 256 slots), pas la maintenance D-cache elle-même.
- Le D-cache aide sur latence moyenne I/O mémoire/cache, mais ne supprime pas le coût structurel des doubles scans et gros volumes sérialisés.

---

## 3) Vérification des optimisations déjà en place

### Aident vraiment
- `payload_size = 0` pour slot vide (pas de payload pattern écrit/lu pour slot vide).
- skip unchanged au load PROJECT (comparaison has_data + checksum; skip lecture payload en commit si identique via seek).
- skip commit complet si aucun changement (`has_pattern_changes == 0` => pas de phase 2).
- cache présence slots (`g_slot_has_data`, `g_project_slot_has_data`) pour réduire scans hors opérations lourdes.
- `boot_context_flash_commit` skip si contexte inchangé (évite erase/program flash inutile).

### Aide partielle / marginale
- Caches checksum “à la demande” existent, mais `get_slot_checksum` relit l’en-tête fichier à chaque appel pour slot non vide.
  - Sur 256 slots, ce n’est pas gratuit.
- `pattern_sd_bank_store_slot_nosync` réduit les sync unitaires pendant commit projet,
  mais le volume I/O global reste dominant.

### Complexité sans gros gain (au regard du coût actuel)
- Certaines validations de cohérence headers/versions sont utiles robustesse mais coût marginal (à garder).
- Le diagnostic texte (`PROJECT_SD_DIAG_ENABLED`) ajoute du bruit UART, potentiellement visible si très bavard, sans traiter le goulet principal.

---

## 4) Optimisations sûres et locales (sans casser robustesse)

### Gros gain / faible risque
1. **Ajouter un cache RAM des checksums/presence pattern invalide sur mutation**
   - Éviter `f_open+f_read(header)+f_close` par slot pour `get_slot_checksum` pendant save/load project.
   - Invalidation naturelle sur `store/delete` pattern.
2. **Réduire les appels imbriqués Pattern API pendant save project**
   - Exposer un chemin interne de lecture séquentielle metadata/payload (sans reopen par slot) pour la boucle 256.
   - Même format, même validations, moins overhead d’appels + open/close.

### Gain moyen / faible risque
3. **Profiling fin activable compile-time dans `project_sd_bank`**
   - Séparer temps `meta`, `payload`, `checksum`, `seek/skip`, `apply` pour objectiver la prochaine passe.
4. **Limiter logs diag en mode normal**
   - Garder seulement profils agrégés (pas log par slot).

### Gros gain / chantier risqué
5. **Passer de double passe load (validate + commit) à staging transactionnel unique**
   - Gros gain possible, mais touche invariants robustesse/atomicité.
   - À faire seulement comme chantier dédié.

### Peu utile pour l’instant
6. **Retoucher encore D-cache/SD DMA**
   - Le chemin est déjà structuré; gains attendus inférieurs à ceux des optimisations architecture PROJECT.

---

## 5) Recommandation concrète (prochaine passe)

1. **Instrumentation ciblée objective (sans changer la sémantique)**
   - Activer/prolonger profiling `project_sd_profile_t` pour isoler:
     - metadata checks 256 slots,
     - payload read/write total,
     - checksums,
     - phase validation vs phase commit.
2. **Implémenter cache checksum/presence pattern en RAM (invalidation stricte)**
   - Local, faible risque, gros levier sur save/load PROJECT.
3. **Mesurer avant/après sur 3 scénarios**
   - bank vide,
   - bank mixte (~25% non vide),
   - bank dense (100% non vide).
4. **Décider ensuite**
   - Si gain insuffisant, préparer chantier transactionnel plus large (double passe -> staging).

---

## Conclusion courte
Le ralentissement résiduel perçu vient majoritairement de la **structure PROJECT (scan 256 + validations + commit en 2 passes + I/O volumineux)**, pas d’un problème principal restant de D-cache.
