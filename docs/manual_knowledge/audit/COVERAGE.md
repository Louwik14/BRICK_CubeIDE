# Couverture de l'audit

## Methode et comptages

Comptages au 2026-10-14, configuration Release racine. Une page = un ID enregistre, pas chaque sous-page dynamique. Un parametre = un `param_id_t` dense, meme s'il n'est accessible que dans un contexte ; les controles types hors Param sont signales sans gonfler ce nombre. Une interaction = une famille de geste physique distincte, pas chaque paire bouton/page. Une fonctionnalite = une entree de `INVENTORY.md`. Un workflow = une section `WF-*`.

| Objet | Nombre | Preuve |
|---|---:|---|
| fonctionnalites inventoriees | 24 | lignes `CTRL/UI/TRACK/...` de `INVENTORY.md` |
| pages UI enregistrees | 19 | appels `ui_page_manager_register` dans `ui_bootstrap.c` |
| IDs Param | 311 | assertion `PARAM_COUNT == 311` dans `param_ids.h` |
| descripteurs Param explicites/macros catalogue | 311 apres expansion | `param_spec_catalog.inc`, macros LFO |
| workflows reconstruits | 13 | sections `WF-*` de `WORKFLOWS.md` |
| familles d'interactions physiques | 18 | navigation, selection, transport, rec, page, encoder normal/SHIFT, step press/hold/release, copy/paste scopes, Settings, modes Hall, doubles taps |
| fiches de domaine | 14 | `features/**/*.md` |

## Domaines entierement analyses au niveau code utilisateur

UI/navigation, topologie Track, catalogues moteur et Param, sequenceur/p-lock/PLAY, Note FX, modulation/Scenes, Audio FX/mixer, persistence Pattern/Project/Patch, sampler runtime, Audio REC, Settings visibles.

## Domaines partiellement analyses

Entrees physiques jusqu'au mapping carte mais sans essai ; formats/assets jusqu'aux loaders mais sans corpus SD ; MIDI/USB jusqu'aux backends mais sans preuve materielle DIN/Host ; import DX7 sans chemin UI Release complet ; DSP au niveau setters/effets et contrats, sans caracterisation audio de chaque modele.

## Domaines non analyses

Aucun domaine utilisateur majeur volontairement exclu. Les bibliotheques tierces, HAL generee, implementation mathematique interne de chaque oscillateur/filtre et dossiers `Inspiration/` sont hors objet sauf lorsqu'un symbole produit les appelle. Ils ne constituent pas des fonctionnalites UI independantes.

## Controle croise

- Pages : enum -> registration -> navigation -> handlers -> families/subpages.
- Controles : IDs -> mapping LowCost -> events -> ordre `ui_core_tick` -> handlers contextuels.
- Params : IDs/assertion -> descripteurs -> pages -> applicability Track -> owner CONTROL -> projection AUDIO -> persistence/p-lock/Matrix.
- Moteurs : Track catalog -> runtime engine -> page TONE -> backend Param -> renderer DSP.
- Persistence : UI browser -> product API -> codec -> transaction -> restore hook UI/AUDIO.

Les points non clos sont enumeres dans `UNCERTAINTIES.md`; le mot « exhaustif » n'est donc pas revendique pour la validation physique.
