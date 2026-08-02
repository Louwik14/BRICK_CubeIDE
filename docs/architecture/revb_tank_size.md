# RevB : taille du tank

Le chemin de production additionne les sends stéréo en mono, puis utilise le
traitement RevB mono-vers-stéréo. La variante stéréo reste disponible et partage
exactement les mêmes topologies.

`TANK SIZE` est un paramètre global de configuration non p-lockable :

- `NORMAL` est le défaut et conserve strictement les dix longueurs existantes
  `163, 233, 347, 574, 2375, 2928, 4899, 2748, 2391, 6870`, ainsi que les
  interpolations `6815.2383 ± 54.42177` et `4854.4219 ± 43.53742` ;
- `MAX` applique le coefficient `1.3924996` aux longueurs, centres et profondeurs.
  Les longueurs entières, arrondies vers le bas, sont
  `226, 324, 483, 799, 3307, 4077, 6821, 3826, 3329, 9566`. Les interpolations
  deviennent `9490.217 ± 75.78229` et `6759.780 ± 60.62584` échantillons.

Le buffer partagé contient 32768 floats. `NORMAL` réserve 23528 échantillons,
plus neuf séparateurs. `MAX` réserve 32758 échantillons, plus neuf séparateurs ;
le dernier élément restant couvre la lecture supplémentaire de l'interpolation.
Les maxima centre + profondeur restent strictement inférieurs aux longueurs de
`del1` et `del2`, ce que vérifient des assertions de compilation.

Un changement `NORMAL`/`MAX` appelle `Clear()` afin d'effacer le buffer partagé,
les états LP internes et les filtres de sortie avant d'utiliser les nouveaux
offsets. Aucun effacement automatique, traitement des non-finis, silence forcé,
saturation ou limiteur n'est exécuté pendant le fonctionnement normal.

La diffusion est bornée à `0.90`. Le decay transmis au tank est borné directement
à `0.98`. Le mapping existant de `DAMP` et la suppression historique de l'ancien
all-pass d'entrée `SMEAR` restent inchangés.
