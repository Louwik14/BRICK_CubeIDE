# Test ADC1 sans rang Volume

Avant l'essai, `board_surface_start_hall_adc_dma()` programme ADC1 en scan
de trois rangs : rang 1 canal 11 (Hall A, PC1), rang 2 canal 19 (Hall C,
PA5), rang 3 canal 5 (volume, PB1). Les trois utilisent le même temps
d'échantillonnage, 64,5 cycles ADC; le DMA circulaire écrit trois demi-mots.
Le dernier rang Volume, observé à 65535 dans les captures, précède donc
immédiatement Hall A du déclenchement TIM6 suivant. Hall A précède
immédiatement Hall C. ADC2 convertit séparément Hall B, canal 18 (PA4).

Le passage d'une source proche pleine échelle vers Hall A maintenu vers
1000–1500 peut laisser une charge résiduelle sur le condensateur de
sample-and-hold si l'impédance de la source Hall et le temps d'acquisition
sont défavorables. Le code établit la séquence et les 64,5 cycles, mais le
dépôt ne contient pas la valeur d'impédance des sorties Hall, du mux ni du
potentiomètre. Une valeur Volume à 65535 ne prouve pas non plus à elle seule
que la broche est une source basse impédance.

Pour cet essai, ADC1 ne programme que les rangs Hall A puis Hall C et son
DMA circulaire utilise deux demi-mots. Le rang Volume n'est pas converti.
La lecture logicielle Volume retourne une constante 65535 pour laisser
passer la phase de boot audio et conserver le gain observé dans les captures;
le potentiomètre ne commande plus le volume pendant cet essai. ADC2, TIM6,
le scan mux, les six conversions rejetées, les seuils et RELEASE gardent
leur comportement normal. Les anciennes sondes à mux figé sont désarmées
par défaut (`g_hall_hold_mode_arm = 0`, `g_hall_fixed_probe_arm = 0`) afin
de ne pas interrompre ce test de scan.

Avec deux rangs, le dernier canal avant Hall A devient Hall C et non plus
Volume. Si les faux RELEASE disparaissent dans le même scénario matériel,
la présence du rang Volume ou l'ordre de scan ADC1 est impliqué, sans que
ce seul essai distingue un effet de charge ADC d'un autre couplage. S'ils
persistent, le rang Volume n'est pas nécessaire au défaut. Le diagnostic
direct antérieur, qui lisait 64 fois Hall A seul après arrêt du DMA/TIM6,
restait déjà haut; cela rend une simple charge résiduelle provenant du rang
Volume insuffisante pour expliquer toute la montée observée.
