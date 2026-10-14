# SCENE-01 — Scenes

## Statut

CONFIRME — sensation pressure a valider.

Quatorze touches blanches Hall adressent 14 Scenes. L'overlay MACRO est disponible depuis SEQ/KBD par SHIFT + touche Hall C2 (ID 7) et restaure ce mode en sortie. LIVE offre PRESSURE (interpolation momentanee) et TOGGLE (verrou). Le double tap passe en ASSIGN ; un tap revient en LIVE.

En ASSIGN, maintenir une touche blanche puis editer un encodeur capture la valeur absolue resolue avec le meme owner/plage que l'edition normale. SHIFT+encodeur efface cette destination. Un STEP physique tenu prend priorite pour le p-lock. La navigation reste possible. Les Scenes persistent dans le bloc Macro du Project, 14 tables de destinations ; etat live/pression non.

Sources : `Src/UI/ui_macro_interaction.c`, `Src/UI/ui_hall_mode_flow.c`, chemins Scene de `Src/UI/ui_param.c` et `Src/App/encoder_control_dispatcher.c`, `Src/Storage/persistent_*`.
