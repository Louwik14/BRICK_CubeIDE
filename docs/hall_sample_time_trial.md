# Essai diagnostique : sampling Hall prolonge

Les trois voies Hall restent reparties entre ADC3 (A), ADC2 (B) et ADC1
(C). Leur temps d'echantillonnage passe de 64,5 a 387,5 cycles ADC, sans
changer le mux, le declenchement TIM6, les six conversions rejetees apres
chaque adresse, les seuils ou la logique RELEASE.

Le HSE est configure a 12 MHz. PLL2P fournit
`12 / 3 * 150 / 20 = 30 MHz`; le prescaler asynchrone ADC `/2` donne
15 MHz. TIM6 recoit 240 MHz et sa periode est
`(239 + 1) * (49 + 1) / 240 MHz = 50 us`.

Le sampling de 387,5 cycles dure 25,83 us, contre 4,30 us avant l'essai.
Meme en reservant 16,5 cycles supplementaires pour la conversion,
l'acquisition tient en 26,93 us, avec 23,07 us avant le declenchement
suivant. L'option maximale de 810,5 cycles demanderait deja 54,03 us
pour le seul sampling : elle ne tient pas dans la periode de 50 us.
