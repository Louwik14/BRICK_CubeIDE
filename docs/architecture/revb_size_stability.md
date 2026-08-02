# RevB : stabilité du paramètre SIZE

RevB utilise une seule géométrie : les dix longueurs historiques
`163, 233, 347, 574, 2375, 2928, 4899, 2748, 2391, 6870`, avec les lectures
modulées `6815.2383 ± 54.42177` et `4854.4219 ± 43.53742`. Aucun sélecteur de
géométrie ni effacement associé n'est exposé.

## Cause confirmée

`SIZE` est borné à `0..1`, puis lissé par bloc sans dépassement de cible. Il pilote
la diffusion `0.45..0.90` et les fréquences LFO1 `0.25..0.75 Hz` / LFO2
`0.15..0.50 Hz`; les profondeurs de modulation restent fixes. Les coefficients
LFO sont recalculés par bloc avec conservation des états de l'oscillateur. Une
simulation float exacte du tank sépare ce chemin LFO et montre qu'il reste borné.

La divergence vient du changement rapide de la diffusion. Un all-pass est
conservatif à coefficient fixe, mais pas lorsque son coefficient change avec un
état de délai déjà chargé. Les pas par bloc de l'ancien lissage (`0.125`) pompent
alors de l'énergie dans les huit all-pass. Avec decay `0.98`, DAMP maximal et une
entrée continue forte, des balayages répétés de SIZE font diverger en premier
l'accumulateur écrit dans `dap2b`; SIZE fixe au maximum reste borné. La vitesse et
les changements de sens augmentent le pompage, tandis qu'une entrée faible en
retarde l'apparition.

## Correction

La réponse SIZE des LFO conserve son lissage `0.125`. Une valeur dédiée pilote la
diffusion avec un lissage par bloc `0.010`, afin que la dissipation de la boucle
domine l'énergie injectée par les changements de coefficient. La cible statique
reste strictement identique : aucun changement sonore une fois SIZE stabilisé.
Aucun limiteur, saturation, traitement des non-finis ou auto-effacement n'est
ajouté.

Le decay est borné à `0.98`, la diffusion à `0.90`, le mapping DAMP et la
suppression historique de l'ancien AP1 `SMEAR` restent inchangés.
