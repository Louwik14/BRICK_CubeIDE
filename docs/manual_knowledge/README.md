# Base technique du manuel BRICK

Cette arborescence decrit le comportement utilisateur reconstruit depuis le firmware Release. Elle n'est ni un manuel, ni une specification d'intention. Le code reste l'autorite finale.

## Perimetre audite

- cible produit compilee par le `CMakeLists.txt` racine, carte LowCost/H743 ;
- UI, entrees physiques, pistes, moteurs, sequenceur, modulation, effets, assets, persistence, enregistrement et MIDI/USB ;
- documents `docs/architecture/` utilises uniquement comme index puis confrontes aux owners et handlers cites dans chaque fiche ;
- aucune validation sur materiel dans cette passe.

## Statuts

- **CONFIRME** : chemin atteignable et effet suivis dans le code compile ;
- **PARTIEL** : noyau prouve, mais une branche, une restitution visuelle ou une condition physique reste a verifier ;
- **NON VERIFIE** : preuve insuffisante ;
- **INCOHERENT** : contradiction ou rupture visible ;
- **INACTIF** : code present, mais non atteignable dans la configuration auditee.

`Validation materielle: OUI` ne diminue pas la preuve logicielle : elle distingue ce qui exige un essai de surface, de connectique, de niveau ou de timing.

## Entrees

- [INVENTORY.md](INVENTORY.md) : index des fonctions et statuts.
- [UI_NAVIGATION.md](UI_NAVIGATION.md) : pages, sous-pages et transitions.
- [PHYSICAL_CONTROLS.md](PHYSICAL_CONTROLS.md) : grammaire de la surface.
- [PARAMETERS.md](PARAMETERS.md) : contrat des 311 Param et controles hors Param.
- [WORKFLOWS.md](WORKFLOWS.md) : procedures techniques de bout en bout.
- [LIMITATIONS.md](LIMITATIONS.md), [UNCERTAINTIES.md](UNCERTAINTIES.md).
- [audit/COVERAGE.md](audit/COVERAGE.md) et [audit/DISCREPANCIES.md](audit/DISCREPANCIES.md).

## Traçabilite et maintenance

Chaque affirmation significative renvoie a un symbole owner, handler, catalogue ou codec. Pour une evolution, rechercher le symbole touche dans `docs/manual_knowledge/`; reexaminer ensuite sa fiche, les workflows qui la citent et les index centraux. Les dependances minimales sont :

| Zone modifiee | Fiches a reexaminer |
|---|---|
| `ui_core*`, `ui_navigation`, page UI | `PHYSICAL_CONTROLS`, `UI_NAVIGATION`, workflow concerne |
| `param_ids`, `param_spec_catalog`, registre/backends | `PARAMETERS`, moteur, modulation, sequenceur |
| Track/topologie/runtime | pistes, synthese/sampler, persistence, navigation |
| Seq/NoteFx | sequenceur, MIDI FX, patterns, controles |
| Storage/codec/product | projets/patterns/patches, assets, recorder, workflows |
| Audio/Engines/Mixer/FX | fiche moteur/effet et limitations materielles |

Les numeros de ligne sont volontairement secondaires : les symboles cites sont les ancres stables.
