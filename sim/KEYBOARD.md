# Clavier du simulateur

Le fichier `config/keymap.json` associe des noms de scancodes SDL3 à des actions
canoniques. Il peut être modifié sans recompiler. Le repeat Windows est ignoré pour
les boutons; les touches encore enfoncées sont relâchées à la perte de focus.

| BRICK | Clavier |
|---|---|
| PARAM 1–7, TRACK | F1–F8 (TRACK aussi sur T) |
| PLAY, REC, SHIFT | Espace, Entrée, Maj gauche |
| Transpose | Flèches haut/bas |
| COPY, PASTE, SETTINGS | C, V, F9 |
| PAGE 1–4 | A, Z, E, R |
| Push encodeurs 1–4 | Q, S, D, F |
| STEP 1–10 | 1–0 |
| STEP 11–16 | Y, U, I, O, P, G |
| Encodeur PAGE −/+ | W / X |
| Encodeur A −/+ | N / B |
| Encodeur B −/+ | M / / |
| Encodeur C −/+ | [ / ] |
| Capture simulateur | F12 |

Raccourcis documentaires utiles : maintenir SHIFT puis appuyer une fois sur
STEP 4 ouvre le Patch Browser; SHIFT + STEP 5 ouvre directement le Sample
Browser. Dans les browsers, PAGE 1 revient, PAGE 2 charge, PAGE 3 sauvegarde ou
renomme selon le contexte et PAGE 4 prévisualise lorsqu'elle est disponible.

Les touches H, J, K et L couvrent les quatre identifiants physiques inutilisés.
Sur certains claviers, le nombre de touches simultanées est limité par le matériel :
utiliser alors la CLI `press`/`release`, qui ne présente pas de ghosting.
