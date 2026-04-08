# Audit approfondi save/load PROJECT (architecture réelle)

Date: 2026-04-08

## Référence fonctionnelle utilisée
- Project = photo autonome de travail (état live utile + bank patterns + pattern active).
- SD = autorité du contenu lourd (project + patterns + pattern active).
- Flash interne = uniquement `active_project_slot`.
- Load project en 2 phases: validation complète puis commit.

## Constat rapide
- Le code implémente bien un format `.PRJ` riche, avec snapshot live + records patterns.
- Le flux load est bien structuré en validation puis commit conditionnel.
- Mais l'architecture reste fragile: optimisations (fast-path équivalent, commit patterns conditionnel)
  + imbrication PROJECT->PATTERN + gate SD permissif => risques de divergence de vérité.

## Points saillants
- Fast-path save: `project_v1_save_slot` peut retourner OK sans réécrire `.PRJ` si slot jugé équivalent.
- `project_v1_apply_snapshot` ignore le paramètre `resume_transport` côté project (cast void).
- Gate SD autorise cohabitation PROJECT/PATTERN sans ownership strict lors des appels imbriqués.
- Source de vérité active project répartie entre RAM runtime, flash boot context, et perception UI/SD.

## Détail modèle
- `ProjectSaveV1` = `state` + `PatternSaveV1 live`.
- Fichier `.PRJ` = header + payload `ProjectSaveV1` + 16x16 records patterns (+ payload optionnel).
- Le project porte donc à la fois une vue runtime et une représentation banque patterns.

## Détail boot context
- Persistance flash dédiée bank2 sector6.
- Donnée stockée: version/valid/crc/active_project_slot.
- Restauration au boot via `project_v1_restore_boot_context()` juste après `project_v1_init()`.
