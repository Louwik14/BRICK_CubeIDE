# Capture Hall avant faux RELEASE

Le détecteur Hall travaille directement sur la valeur ADC brute. Pour une
touche enfoncée, il émet RELEASE dès qu'un seul échantillon est supérieur ou
égal au seuil `trig_hi` (`raw >= release`). Il n'existe pas de filtrage
intermédiaire. `hall_min` et `hall_max` sont fixés par la calibration chargée
ou terminée; ils ne sont pas adaptés en jeu.

Dans le dump initial : clé 2 `22417 >= 21168`, clé 7 `22659 >= 21189`,
clé 11 `23755 >= 21979`. Les clés 2 et 7 partagent la sortie Hall A
(ADC1) aux mux 4 et 5; la clé 11 vient de la sortie Hall B (ADC2) au mux 6.
Les seuils franchis sont réels dans les valeurs remises au détecteur, mais
ce dump ne dit pas encore si les capteurs ont réellement bougé ou si
l'acquisition a attribué des conversions au mauvais canal.

La nouvelle trace enregistre **chaque triplet ADC admis** avant et après la
décision Hall, les deux compteurs de callbacks DMA, le canal mux attendu et
les bits GPIO du mux (sortie et entrée). Les trois entrées couvrent les 24
touches à chaque tour de huit canaux. Le quatrième ADC1 est le potentiomètre
de volume, utile comme témoin analogique indépendant du mux. Les valeurs de
calibration et les deux seuils sont figés dans un tableau séparé.

La trace se fige après trois RELEASE de touches maintenues depuis au moins
500 ms dans une fenêtre de 100 ms. Reproduire le défaut, puis arrêter la
carte avec GDB sans redémarrer. Si `g_hall_capture_frozen` vaut zéro, arrêter
GDB immédiatement après l'incident pour conserver les dernières 1,4 s.

```gdb
shell cls
set pagination off
set logging file hall_capture_gdb.txt
set logging overwrite on
set logging enabled on
p/x *(unsigned int*)&g_hall_capture_sequence
p/x *(unsigned int*)&g_hall_capture_calibration_generation
p/x *(unsigned char*)&g_hall_capture_frozen
x/3ub &g_hall_capture_release_keys
x/3uw &g_hall_capture_release_ticks
p/x &g_hall_capture
p/x &g_hall_capture_calibration
dump binary memory hall_capture.bin (char*)&g_hall_capture ((char*)&g_hall_capture+229376)
dump binary memory hall_calibration.bin (char*)&g_hall_capture_calibration ((char*)&g_hall_capture_calibration+192)
set logging enabled off
```

`hall_capture.bin` contient 4096 entrées de 56 octets little endian. Leur
ordre en mémoire est : `sequence`, `tick_ms`, `tim5_tick`, `held_before`,
`held_after`, `calibration_generation` (six u32), puis `adc1_hall_a`,
`adc2_hall_b`, `adc1_hall_c`, `adc1_volume`, `adc1_callbacks`,
`adc2_callbacks` (six u16), puis `mux_expected`, `mux_odr`, `mux_idr`,
`completing_adc` (quatre u8), `adc1_callback_tick` et
`adc2_callback_tick` (deux u32), puis `adc1_error` et `adc2_error` (deux
u16), puis `adc1_dma_ndtr_before` et `adc1_dma_ndtr_after` (deux u16).
Trier les entrées non nulles par `sequence`.
L'index mux revient à zéro après sept. Les touches 2, 7 et 11 sont
respectivement (ADC1 A, mux 4), (ADC1 A, mux 5) et (ADC2 B, mux 6).

`hall_calibration.bin` contient 24 entrées de huit octets : `minimum`,
`maximum`, `press`, `release` (quatre u16) dans l'ordre des clés 0 à 23.

Décodage CSV sous PowerShell :

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/decode_hall_capture.ps1 hall_capture.bin hall_calibration.bin > hall_capture.csv
```

Lecture du prochain incident :

- Si `mux_expected`, `mux_odr` ou `mux_idr` divergent, le canal attribué à
  l'échantillon n'est pas celui sélectionné sur les broches.
- Si les compteurs ADC divergent ou sautent, ou si les deux ticks de callback
  s'écartent au voisinage du pic, les callbacks assemblent potentiellement
  deux cycles de conversion différents. Les champs `adc*_error` indiquent
  une erreur détectée par HAL. Si `adc1_dma_ndtr_before` et
  `adc1_dma_ndtr_after` diffèrent, le DMA a écrit pendant la lecture des
  trois valeurs ADC1 : le triplet peut être déchiré.
- Si mux et callbacks restent cohérents mais que les trois canaux Hall et le
  potentiomètre bougent ensemble, chercher une perturbation de référence,
  d'alimentation ou d'acquisition ADC.
- Si seuls des capteurs Hall bougent, chercher une interaction électrique ou
  magnétique des capteurs; comparer les touches relâchées du même scan.
- Si `calibration_generation` change, comparer les deux générations et les
  seuils; une recalibration peut elle-même invalider les touches held.

Un échantillon isolé au-dessus du seuil suffit aujourd'hui à émettre RELEASE;
la capture permet de distinguer ce cas d'un déplacement analogique durable.
