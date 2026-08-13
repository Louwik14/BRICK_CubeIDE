# Loader Project progressif et Patch CONTROL

Le décodage Project exige une source bornée fournissant `read`, `reset` et `size`. La première passe ne modifie aucun état : elle vérifie l'en-tête, le kind/version, la taille exacte, les quatre sections obligatoires dans leur ordre, leurs versions et bornes, les cardinalités Project/assets/banque et le CRC32 du payload complet.

Après cette prévalidation, la seconde passe décode le core, le manifeste d'assets et les macros, valide puis applique cette unité CONTROL. Le même workspace est ensuite réutilisé pour chaque record Pattern de banque, validé avant remise au consumer. Aucun staging de banque ni second Pattern simultané n'est détenu par le codec.

Le workspace maximal explicite vaut 693160 octets : union Project-core/record Pattern plus 4096 octets d'identifiants d'assets nécessaires à la validation locale des records. Il appartient à l'appelant et n'ajoute aucune mémoire statique permanente. Le workspace Patch canonique vaut 4060 octets.

`persistent_patch_control` capture et applique directement family/type, paramètres persistables applicables et référence d'asset logique. MOD reste couvert seulement par les paramètres sonores déjà sauvegardés par Patch V1; Note FX reste exclu. Aucun état sonore agrégé, slot physique, binding, cache ou index de pool n'entre dans ce chemin.

Les codecs et banques V1 restent les seuls chemins disque actifs pendant cette passe.
