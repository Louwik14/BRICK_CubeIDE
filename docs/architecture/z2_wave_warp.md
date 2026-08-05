# Wave warp contract

Le moteur Wave expose maintenant deux parametres par oscillateur:

- `WARP TYPE`: `OFF`, `BEND`, `SKEW`, `FOLD`, `REPEAT`, `QUANTIZE`.
- `WARP AMT`: valeur normalisee persistante dans `[-1, +1]`. Elle est
  bipolaire pour `BEND` et `SKEW`, unipolaire pour `FOLD`, et sert d'index
  discret pour `REPEAT` (`1X..4X`) et `QUANTIZE` (`64/32/16/8/4`).

La phase Q32 brute est transformee une fois par sample avant la lecture de
la wavetable. La phase transformee est partagee par les lectures frame 0 et
frame 1; aucun renderer, buffer temporaire ou oscillateur additionnel n'est
introduit. Les coefficients, seuils, valeurs discretes et le dispatch du
warp sont prepares au debut du bloc.

`OFF` conserve directement la phase brute. Pour les autres types, la pente
maximale preparee est multipliee par `phase_inc` pour la selection du mipmap.
Les valeurs extremes sont plafonnees a `UINT32_MAX`; aucun oversampling ou
filtre supplementaire n'est utilise.

`WARP TYPE` n'est pas une destination de modulation. `WARP AMT` reste un
parametre de piste lent dans cette passe: le chemin de modulation directe
actuel ne l'autorise pas. Les deux valeurs sont des champs de l'etat tone et
suivent les copies et snapshots courants. Les formats Pattern/Project passent
en version 8, les formats Patch/Kit en version 5; les chargeurs refusent ainsi
les anciens payloads contenant `PHASE`/`FLIP`, sans remapping implicite.
