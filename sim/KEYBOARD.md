# Clavier AZERTY français

Le fichier `config/keymap.json` utilise les scancodes physiques SDL3. Le nom du
scancode correspond à la position QWERTY SDL, pas au caractère imprimé sur un
clavier AZERTY : par exemple la touche française `A` produit `SDL_SCANCODE_Q`,
et `Z` produit `SDL_SCANCODE_W`. Cette conversion est déjà appliquée dans le
fichier; les libellés ci-dessous sont ceux réellement visibles sur un clavier
AZERTY français Windows.

| Fonction BRICK | Touche AZERTY Windows |
|---|---|
| STEP 1–8 | F1 à F8 |
| STEP 9–16 | `&` `é` `"` `'` `(` `-` `è` `_`, sans Maj |
| CFG, ENV, TONE, MOD | A, Z, E, R |
| MIX, PLAY, MIDI FX, AUDIO FX | T, Y, U, I |
| PAGE 1–4 | Q, S, D, F |
| Encodeur 1 −/+ | Pavé numérique 1 / 3 |
| Encodeur 2 −/+ | Pavé numérique 5 / 2 |
| Encodeur 3 −/+ | Flèche gauche / droite |
| Encodeur 4 −/+ | Flèche bas / haut |
| SHIFT | Maj gauche |
| TRACK | Ctrl gauche |
| PLAY / STOP | Espace |
| REC | Entrée |
| SETTINGS | F9 |
| Capture PNG | F12 |

Les chiffres des encodeurs utilisent volontairement le pavé numérique. Les
touches de la rangée supérieure sont déjà affectées aux STEP 9–16 et nécessitent
Maj pour produire des chiffres sur AZERTY; les réutiliser ferait aussi intervenir
le véritable bouton BRICK SHIFT.

Les raccourcis d'ensembles passent par les entrées physiques BRICK équivalentes.
CFG utilise la double sélection de la piste active; MIDI FX et AUDIO FX utilisent
le véritable accord SHIFT + STEP 7. Aucun changement n'est apporté à la logique
de navigation du firmware.

Le repeat Windows est ignoré pour les boutons. Toutes les touches maintenues sont
relâchées lorsque la fenêtre perd le focus. F12 est réservé au simulateur et ne
génère aucun événement BRICK.
