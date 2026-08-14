# Z6 — État et persistance

Project, Pattern et Patch utilisent exclusivement le modèle CONTROL canonique et les codecs explicites de `persistent_control_codec`. Aucune famille V1, aucun dump de structure et aucune migration d'ancien format ne participent aux parcours produit.

Pattern persiste les configurations, paramètres, routes, Note FX et séquences par entité logique. `pattern_live_ram` orchestre la sélection active, la queue différée et le Blank; capture, validation et application passent par `persistent_pattern_control`, et le stockage passe par `pattern_control_bank`.

Patch est mono-entité et passe uniquement par `patch_product`, `persistent_patch_control` et le codec canonique. Project passe uniquement par `project_product`, `persistent_project_control`, les banks logiques d'assets et les commits transactionnels de Pattern.

Les identités persistées sont logiques. Les slots de pool, indices globaux AUDIO, handles et états de chargement runtime sont reconstruits après lecture et ne figurent dans aucun payload persistant.

Toute prévalidation précède la mutation. Un rejet laisse l'état CONTROL courant intact; une application réussie reconstruit ensuite le runtime et l'AUDIO.
