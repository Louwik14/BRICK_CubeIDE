# BRICK User Manual — chaîne de publication

`docs/manual/` contient le manuel éditorial : un chapitre Markdown par fichier, assemblé en un unique `BRICK_Manual.pdf`. La preuve technique reste dans `../manual_knowledge/`; les commentaires `TECHNICAL SOURCES` en tête de chapitre relient les deux ensembles et ne sont pas rendus dans le PDF.

## Prérequis Windows

- Pandoc : <https://pandoc.org/installing.html>
- Typst CLI : <https://github.com/typst/typst/releases>

Les exécutables `pandoc` et `typst` doivent être disponibles dans `PATH`. Aucun gestionnaire de paquets particulier n'est imposé et les scripts n'installent rien.

## Générer le PDF

- Explorateur : double-cliquer `build.bat`.
- PowerShell : `./build.ps1` depuis ce dossier, ou `powershell -ExecutionPolicy Bypass -File ./build.ps1` si la politique locale l'exige.

Le résultat est `docs/manual/BRICK_Manual.pdf`.

## Modifier le manuel

- Éditer le chapitre concerné sans supprimer son commentaire `TECHNICAL SOURCES`.
- Changer les versions et l'auteur dans `metadata.yaml`; la date est générée par Typst.
- Ajouter une image sous `assets/images/` ou un schéma sous `assets/diagrams/`, puis utiliser `![Légende](assets/images/fichier.png)`; les figures sont centrées et légendées par le template.
- Écrire une note, un avertissement ou un conseil comme une citation Markdown commençant par `**Note —**`, `**Avertissement —**` ou `**Conseil —**`; le template applique le cartouche commun.
- Utiliser les ancres Markdown ordinaires pour les renvois internes, par exemple `[Voir le séquenceur](#sequencer)`.
- Pour ajouter, retirer ou réordonner un chapitre, modifier la liste `$Chapters` dans `build.ps1`; `build.bat` délègue à ce script. Conserver des préfixes numériques rend l'ordre visible dans Git.
- La présentation globale appartient à `template.typ`; éviter les styles locaux dans les chapitres.

Les placeholders `[IMAGE TODO: ...]` et `[DIAGRAM TODO: ...]` sont du texte volontairement compilable et peuvent être remplacés progressivement par de vrais médias.
