# Autorités Project CONTROL

`project_control` possède l'intention utilisateur des macros/scènes et les banks logiques d'assets Sample Stream/RAM, Wavetable et Multi. Les paramètres et p-locks ne conservent que les indices logiques; les correspondances vers les slots runtime sont publiées après chargement réussi et ne sont jamais persistées.

`persistent_project_control` capture, valide et applique le Pattern de travail canonique, la sélection Pattern active, les macros et le manifeste logique d'assets. `project_product` est l'unique façade produit Save/Load/Blank/boot.

Les UI macros/scènes, le presse-papiers et les LEDs interrogent directement `project_control`. Aucun wrapper Project V1 ni orchestration disque historique ne subsiste dans le code actif.
