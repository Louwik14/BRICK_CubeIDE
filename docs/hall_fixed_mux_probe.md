# Sonde Hall à mux figé

La sonde attend une touche déjà HELD pendant au moins 360 visites de sa
voie (environ une seconde au scan nominal), avec au moins huit mesures
stables à 512 codes de son minimum local. Elle se déclenche lorsque sa
mesure dépasse ce minimum d'au moins 2500 codes tout en restant sous le
seuil RELEASE. Ces valeurs servent uniquement au déclenchement de la
capture; le détecteur Hall et ses seuils ne changent pas.

Le mux reste alors sur cette adresse pendant au plus 5 ms. Chaque paire
de callbacks enregistre les trois valeurs DMA, leurs ticks de callback,
leurs compteurs, les NDTR, et l'adresse mux observée sur les GPIO. La sonde
ne distribue pas ces échantillons au détecteur pendant cette brève pause;
elle reprend ensuite le scan normal avec six paires rejetées et fige la
trace pour GDB. La valeur de `g_hall_fixed_adc` désigne 0 = ADC1 Hall A,
1 = ADC2 Hall B, 2 = ADC1 Hall C. L'absence de déclenchement donne
`g_hall_fixed_done = 0`.

```gdb
shell cls
set pagination off
set logging file hall_fixed_gdb.txt
set logging overwrite on
set logging enabled on
p/x *(unsigned char*)&g_hall_fixed_done
p/x *(unsigned char*)&g_hall_fixed_key
p/x *(unsigned char*)&g_hall_fixed_mux
p/x *(unsigned char*)&g_hall_fixed_adc
p/x *(unsigned short*)&g_hall_fixed_baseline
p/x *(unsigned short*)&g_hall_fixed_trigger_raw
p/x *(unsigned int*)&g_hall_fixed_count
p/x *(unsigned char*)&g_hall_capture_frozen
p/x &g_hall_fixed_trace
dump binary memory hall_fixed.bin (char*)&g_hall_fixed_trace ((char*)&g_hall_fixed_trace+5120)
dump binary memory hall_capture.bin (char*)&g_hall_capture ((char*)&g_hall_capture+294912)
dump binary memory hall_calibration.bin (char*)&g_hall_capture_calibration ((char*)&g_hall_capture_calibration+192)
set logging enabled off
```

`hall_fixed.bin` contient 128 enregistrements de 40 octets. Seules les
`g_hall_fixed_count` premières entrées sont valides, dans l'ordre de leur
champ `sequence` (u32). Suivent : `tim5_tick`, `adc1_callback_tick`,
`adc2_callback_tick`, `adc1_callbacks`, `adc2_callbacks` (cinq u32),
`raw_a`, `raw_b`, `raw_c`, `adc1_ndtr_before`, `adc1_ndtr_after`,
`adc2_ndtr` (six u16), puis `mux_odr`, `mux_idr`, `completing_adc` et
`reserved` (quatre u8). Le canal sélectionné doit rester égal à
`g_hall_fixed_mux` dans toutes les entrées. Une baisse du raw de la voie
`g_hall_fixed_adc` sous mux fixe indique une dépendance au balayage; une
valeur durablement haute montre que l'ADC la voit également sans changement
d'adresse. La sonde ne date pas le déclenchement ADC matériel lui-même.
