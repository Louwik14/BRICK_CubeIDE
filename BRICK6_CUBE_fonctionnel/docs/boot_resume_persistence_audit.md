# Audit taille & stratégie — persistance boot/reprise

Date: 2026-04-05

## Données réellement capturées aujourd'hui

Le pipeline `project_v1_capture_current()` capture:
- un bloc `ProjectSaveV1.state` (active/queued pattern, active project slot, matrice `bank_has_data[16][16]`),
- plus un `PatternSaveV1 live` complet via `pattern_live_capture_current()`.

`pattern_live_capture_current()` capture:
- séquence complète (8 tracks × 64 steps, trig + plocks),
- config tracks (family/type/midi ch/source),
- params sound trackés,
- params mix trackés,
- globals utiles + tempo/clock/rec + div/quant/swing.

`pattern_live_apply_snapshot()` restaure ces blocs, puis:
- remet le playhead track à `0`,
- ne restaure pas un runtime transport “continu”,
- relance le transport seulement selon `resume_transport` et `was_running` en RAM.

## Taille observée (mesurée avec sizeof)

Mesures réelles (tooling local):
- `PARAM_COUNT = 197`
- `sizeof(PatternSaveV1) = 99,772` bytes
- `sizeof(project_v1_state_block_t) = 264` bytes
- `sizeof(ProjectSaveV1) = 100,036` bytes
- `sizeof(project_v1_file_header_t) = 36` bytes
- `sizeof(project_v1_slot_record_t) = 12` bytes
- `sizeof(seq_runtime_state_t) = 1,620` bytes (runtime, non requis pour reprise crédible)

Le format fichier projet (`project_sd_bank_store_slot`) écrit:
1) header projet,
2) `ProjectSaveV1`,
3) 256 records (16x16),
4) payload `PatternSaveV1` pour chaque pattern présent.

Formule taille fichier projet:

`T_project_file(P) = 36 + 100036 + (256*12) + P*99772`

où `P` = nombre de patterns réellement présents dans la bank pattern SD.

## Ce qui est indispensable pour “retrouver le taf”

### A. Indispensable
- Snapshot live de travail (`PatternSaveV1`) :
  - seq (steps + plocks + longueurs/pages),
  - track config,
  - params sound/mix,
  - globals utiles + tempo/clock/rec + div/quant/swing.
- Sélection pattern active (`active_bank`, `active_pattern`) pour retomber sur le même contexte de slot.

### B. Important mais pas strictement indispensable
- Pattern queue (`queued_valid`, `queued_bank`, `queued_pattern`) : utile UX mais non vital.
- `active_project_slot` : utile pour cohérence UI et flux “save over same slot”.
- Optionnel: flag “transport running at save time” (si on veut redémarrer auto en play).

### C. Inutile à persister pour reprise boot
- `seq_runtime_state_t` détaillé (tick_accum, last_tick, active locks runtime, phases): runtime dérivé/reconstruit.
- Métadonnées cache RAM (`g_pattern_slot_meta`, flags temporaires d’apply, last_playhead en RAM).
- `bank_has_data[16][16]` dans le snapshot de reprise boot (reconstructible par scan SD).
- État audio instantané non structurel (buffers DSP, états transitoires init boot).

## Comparatif stratégies

## Stratégie 1 — Réutiliser un projet complet
- Réutilise `project_v1` / `project_sd_bank` tel quel.
- Taille:
  - min (P=0): **103,144 B** (~100.7 KiB)
  - P=16: **1,699,496 B** (~1.62 MiB)
  - P=64: **6,488,552 B** (~6.18 MiB)
  - P=128: **12,873,960 B** (~12.26 MiB)
  - P=256: **25,644,776 B** (~24.46 MiB)
- Écriture potentiellement longue (parcourt et sérialise 256 slots).
- Très robuste (format existant), mais surdimensionné pour simple reprise de session.

## Stratégie 2 — Snapshot de reprise dédié minimal
- Fichier dédié `resume_v1` contenant:
  - `PatternSaveV1 live` (obligatoire),
  - mini état (active/queued + active project slot + version/checksum).
- Taille typique: **~99.8 à 100.1 KiB** (selon header choisi).
- Écriture bornée et stable (~100 KiB constant).
- Complexité modérée (nouveau format + load/save boot), faible risque si checksum/version.

## Stratégie 3 — Intermédiaire “pointeur slot + fallback live”
- Stocker petit enregistrement: active project slot / active pattern / queued + flags.
- Si projet/slot valide: reload via existant.
- Si invalide ou non sauvegardé récemment: fallback vers snapshot live dédié (PatternSaveV1).
- Taille:
  - pointer seul: **~32 à 128 B**
  - pointer + fallback live: **~100 KiB**
- Meilleur compromis robustesse UX si l’utilisateur oublie de sauvegarder le projet.

## Recommandation

Recommandation: **Stratégie 3 (intermédiaire), implémentée en 2 couches**
1) enregistrement minuscule “resume pointer/meta”,
2) snapshot live dédié `PatternSaveV1` en fallback fiable.

Pourquoi meilleur ratio:
- **Complexité**: réutilise largement les APIs déjà stables (`pattern_live_capture_current/apply_snapshot`, `project_v1_load_slot`) avec un format simple en plus.
- **Poids mémoire**: coût normal quasi nul (pointer), coût worst-case borné ~100 KiB (fallback), très inférieur au projet complet multi‑MiB.
- **Robustesse**: redémarre même si slot projet absent/corrompu/non synchronisé, grâce au fallback live checksummé.
- **Temps d’écriture**: généralement très court (pointer); fallback reste fixe (~100 KiB) et évite le scan/commit des 256 patterns du projet complet.

Décision pratique préparatoire:
- Ne pas réutiliser le fichier projet complet pour la persistance extinction.
- Créer un format dédié boot-resume compact, compatible avec le modèle `PatternSaveV1` existant.
- Garder `project_v1` pour l’usage “save/load projet utilisateur”, et le resume pour “continuité de travail après coupure”.
