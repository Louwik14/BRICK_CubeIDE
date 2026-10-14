# Divergences, incoherences et code inactif

## D-01 — Version persistence documentee

`docs/architecture/ARCHITECTURE_GLOBAL.md` annonce encore codec version 7 et `docs/architecture/z6_state_persistence_patterns_projects.md` version 15, tandis que `Inc/Storage/persistent_control_codec.h` fixe le code courant a B6CP version 19 avec lecture legacy v18. Verdict : **INCOHERENT documentaire** ; la fiche retient v19.

## D-02 — MIDI DIN/Host

`Inc/MIDI/midi.h` decrit USB Host et DIN UART comme stubs futurs ; le depot contient `midi_host.c` et des piles USB. Verdict : **PARTIEL**, ne pas annoncer le support materiel avant essai et inspection carte complete.

## D-03 — Trous de pages

IDs UI 0, 1 et 14 ne sont pas enregistres. Verdict : **INACTIF/reserve**, pas des pages utilisateur.

## D-04 — Boutons encodeur

Quatre IDs PUSH existent dans la surface, sans handler utilisateur transversal trouve. Verdict : **NON VERIFIE** ; ne pas documenter une action.

## D-05 — Catalogues de modeles

Audio FX conserve des indices affiches `-`; Prism expose un label `????`. Verdict : **INCOHERENT produit/label** sans rupture de table ; ces valeurs ne doivent pas recevoir un nom invente.

## D-06 — Parametres presents mais non exposes

Plusieurs IDs (par exemple Feedback/Sync FM ou slots Mod FX incomplets) existent dans le registre/backends sans controle TONE actuel. Verdict : **INACTIF via UI directe** mais potentiellement chargeable/persistable selon owner ; ne pas les compter comme controles de page.

## D-07 — Documentation historique

Les documents d'audit ponctuels sous `docs/architecture/` decrivent parfois un bug ou un etat intermediaire corrige. Verdict : **NON AUTORITATIF** ; seuls leurs contrats repris par le code courant ont ete utilises.
