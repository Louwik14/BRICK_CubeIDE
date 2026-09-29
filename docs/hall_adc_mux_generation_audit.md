# Cohérence temporelle Hall ADC/mux

## Contrat actuel

TIM6 déclenche ADC1 (trois rangs) et ADC2 (un rang) toutes les 50 µs.
Chaque DMA circulaire conserve sa dernière séquence. Chacun des deux
callbacks pose un drapeau booléen `ready`. Quand les deux sont à 1,
`hall_adc_process_pair` lit les derniers mots DMA, puis, après six paires
rejetées, les attribue à l'adresse mux courante et avance le mux. Le DMA
ne stocke pas l'adresse mux ni le numéro du déclenchement TIM6 avec les
valeurs. Le compteur de callbacks ne garantit donc pas, à lui seul, que
les deux valeurs proviennent du même déclenchement.

Une IRQ retardée ou plusieurs conversions DMA survenues avant son traitement
peuvent faire correspondre un drapeau ancien à une valeur DMA plus récente.
Les deux callbacks peuvent également correspondre à des déclenchements
différents. Le contrôle `NDTR` avant/après la lecture exclut certaines
écritures simultanées, mais pas un tour DMA complet entre les deux lectures.
Les six paires rejetées donnent normalement le temps au mux de se stabiliser;
le test à douze paires n'a pas supprimé le défaut. Une telle incohérence
reste possible par le code, sans être démontrée dans les captures fautives.

## Nouvelle capture ciblée

Chaque changement d'adresse incrémente `mux_generation`. Les callbacks
enregistrent la génération courante et le NDTR de leur DMA; chaque triplet
admis contient ces deux instantanés ainsi que la génération du mux utilisée
pour attribuer les touches. Comparer, autour de la première hausse brute,
`mux_generation`, `adc1_callback_generation` et
`adc2_callback_generation`. Une différence démontre qu'un callback assemblé
a été traité sous une autre adresse mux. Les ticks de callbacks déjà
présents permettent d'examiner leur ordre et leur espacement. Une égalité
des générations ne prouve pas l'identité des déclenchements : la génération
est prise au callback, après la conversion. Le dump suffit à confirmer ou
écarter certains décalages de mux; si les trois générations restent égales,
la mesure minimale suivante devra dater le déclenchement matériel ou
capturer directement la sortie analogique.

Les anciennes captures de 56 octets par ligne restent décodables par
`tools/decode_hall_capture.ps1`; les nouveaux champs y valent zéro.

## Historique pertinent

- `b340c6487` et `aa99c5db2` ont corrigé un affichage de valeur brute
  attribuée au mauvais index DMA/mux dans un ancien lecteur. Ce lecteur
  n'est plus celui qui alimente le détecteur actuel.
- `4e110d97f` a déplacé l'échantillonnage vers le callback des deux ADC
  et introduit le rejet de la première paire après changement de mux.
- `bc2d1a399` a porté ce rejet à six paires et restauré l'attente des
  deux callbacks. Cette protection est toujours présente; le test à douze
  paires n'a pas changé le défaut.
- `95db50434` a retiré le filtre ASC antérieur pour réduire la latence.
  Ce filtre intervenait après acquisition et n'établissait pas l'identité
  temporelle des conversions; son retrait n'explique pas à lui seul une
  hausse progressive des valeurs DMA brutes observées dans les captures.

Le test O0/noipa des six fonctions Hall critiques n'ayant pas changé le
défaut, ces fonctions sont revenues au build Release/LTO normal. Aucun seuil,
filtrage ou comportement RELEASE n'a été modifié.
