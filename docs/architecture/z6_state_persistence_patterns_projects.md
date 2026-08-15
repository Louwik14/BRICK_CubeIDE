# Z6 — État et persistance

Project, Pattern et Patch utilisent exclusivement le modèle CONTROL canonique et les codecs explicites de `persistent_control_codec`. Aucune famille V1, aucun dump de structure et aucune migration d'ancien format ne participent aux parcours produit.

La base voice FM étendue reste persistée par les listes de paramètres entity ordinaires. Ses 81 octets cachés sont regroupés dans 27 valeurs FLOAT32 exactes de 24 bits ; il n'existe ni blob SysEx, ni asset DX7, ni format de preset FM parallèle. Patch remet d'abord la base FM à ses valeurs déterministes avant d'appliquer sa liste, tandis que Pattern/Project utilisent leur remise à zéro canonique globale existante.

Pattern persiste les configurations, paramètres, routes, Note FX et séquences par entité logique. `pattern_live_ram` orchestre la sélection active, la queue différée et le Blank; capture, validation et application passent par `persistent_pattern_control`, et le stockage passe par `pattern_control_bank`.

La présence des slots Pattern est transactionnelle : Store, delete, clear et restauration Project construisent le namespace inactif puis publient son `COMMIT.BIN`. Un overwrite déjà présent emprunte le même corridor. Le bitmap committé décrit toujours exactement les fichiers du set récupérable.

Patch est mono-entité et passe uniquement par `patch_product`, `persistent_patch_control` et le codec canonique. Project passe uniquement par `project_product`, `persistent_project_control`, les banks logiques d'assets et les commits transactionnels de Pattern.

Les identités persistées sont logiques. Les slots de pool, indices globaux AUDIO, handles et états de chargement runtime sont reconstruits après lecture et ne figurent dans aucun payload persistant.

Toute prévalidation précède la mutation. Un rejet laisse l'état CONTROL courant intact; une application réussie reconstruit ensuite le runtime et l'AUDIO.

Le Project est encodé et décodé progressivement par section. Metadata, assets, macros et records Pattern disposent de providers/consumers dédiés; aucun objet Project complet n'existe en staging et un seul Pattern de transit est réutilisé.
