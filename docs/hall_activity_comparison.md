# Comparaison Hall avec et sans activité d'autres touches

Dans `hall_hold.bin`, les touches 0, 2, 4 et 6 restent enfoncées sans autre
activité pendant les 4096 triplets enregistrés. Le masque reste `0x55`, la
capture ne se fige pas et leurs mesures restent proches de 1300 (maximum
proche de 1550).

Dans `hall_capture.bin`, les mêmes touches sont maintenues pendant que
d'autres sont jouées. La mesure de la touche 6 (ADC1 Hall A, mux 7) commence
à monter au tick 16415 : 2615, puis 7977, 12854, 18543 et 23015 sur cinq
visites successives du canal. Elle franchit son seuil de 21320 et émet
RELEASE au tick 16427. Les touches 4 et 2 émettent RELEASE aux ticks 16442
et 16447; la touche 0 monte également avant le gel. L'activité des autres
touches est la différence expérimentale déterminante, mais ces deux essais
ne révèlent pas encore le mécanisme de couplage.

Autour de la première hausse, les deux compteurs de callbacks progressent
de sept par triplet admis. Le mux attendu, son ODR et son IDR concordent;
la génération de calibration reste 1 et les erreurs HAL sont nulles. Les
autres entrées du même triplet restent proches de leur niveau habituel.
Le décalage absolu entre compteurs ADC1 et ADC2, proche de 1933, existe
déjà avant cette hausse. Il signale d'anciennes différences de callbacks,
mais aucune perte nouvelle coïncidant avec le premier écart n'est démontrée.

Le code démarre les deux ADC avec TIM6, lit les DMA circulaires après les
deux callbacks, sélectionne le mux suivant puis rejette six paires. Un
PRESS ou RELEASE n'écrit ni dans la configuration ADC/DMA/TIM6 ni dans les
GPIO du mux. Les callbacks peuvent subir une latence liée à l'activité,
mais les valeurs successives et localisées de la touche 6 ne démontrent pas
une erreur d'index ou de synchronisation. Une variation du signal analogique
à l'entrée ADC et un défaut d'acquisition restent possibles. Mesurer en
même temps l'entrée ADC1 Hall A et les lignes de sélection du mux pendant
la première montée permettrait de les départager. Aucun changement de seuil,
de filtrage ou de séquencement n'est justifié par cette preuve incomplète.
