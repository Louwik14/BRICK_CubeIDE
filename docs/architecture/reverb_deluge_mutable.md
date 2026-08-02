# Réverbération Deluge Mutable

La réverbération globale est un port du moteur Mutable du firmware open source du Deluge. Le niveau `WET` reste extérieur au moteur et règle le retour BRICK.

Le moteur utilise un buffer float de 32768 éléments, les délais Deluge `150, 214, 319, 527, 2182, 2690, 4501, 2525, 2197, 6312`, les lectures modulées `6261 ± 50` et `4460 ± 40`, et deux LFO fixes à 0,3 Hz et 0,5 Hz.

Les cinq paramètres moteur sont normalisés de 0 à 1 :

- `ROOM SIZE` : temps de boucle `0.01 + 0.97 x`, soit `0.01–0.98` ;
- `DAMPING` : `x = 0` donne `1`, sinon `1 - clamp(log2(((1-x)×50)+1) / 5.7, 0, 1)` ;
- `WIDTH` : diffusion `0.1 + 0.8 x`, soit `0.1–0.9` ;
- `HPF` : `fc = 20 + (exp(1.5x)-1)×150` Hz, puis coefficient `fc/48000 / (1+fc/48000)`, soit `20–542.25 Hz` ;
- `LPF` : `fc = (exp(1.5x)-1)×5083.74` Hz, puis le même calcul de coefficient, soit `0–17700 Hz`.

Il n'y a ni prédélai, ni smear AP1, ni géométrie alternative, ni sélection de modèle, ni fréquence de LFO dynamique, ni lissage de diffusion propre à BRICK.
