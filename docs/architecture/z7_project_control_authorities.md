# Autorités Project CONTROL

`project_control` possède l'intention utilisateur des macros/scènes et les banks logiques d'assets Sample Stream/RAM, Wavetable et Multi. Les paramètres et p-locks ne conservent que les indices logiques; les correspondances vers les slots runtime sont publiées après chargement réussi et ne sont jamais persistées.

`persistent_project_control` capture et applique les metadata et le Pattern de travail canonique. `project_product` diffuse séparément les assets, macros et records Pattern et reste l'unique façade produit Save/Load/Blank/boot.

Le couple persistant `{N, kind, path}` n'existe que dans les banks logiques Project. Une entité ne conserve que N. Lors de l'application d'un Patch contenant un path, `project_control_ensure_asset()` recherche ou enregistre ce path dans la bank correspondante, obtient N, puis seul N est appliqué au paramètre CONTROL.

Les UI macros/scènes, le presse-papiers et les LEDs interrogent directement `project_control`. Aucun wrapper Project V1 ni orchestration disque historique ne subsiste dans le code actif.
