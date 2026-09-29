# Test Hall avec mux figé pendant l'activité physique des autres touches

La touche cible est la clé 2 par défaut (`g_hall_hold_target_key`). On peut
changer ce global avec GDB avant le test, pour une clé de 0 à 23. Maintenir
la touche cible au moins une seconde jusqu'à ce que
`g_hall_hold_mode_active = 1`. Le firmware conserve alors son adresse mux
indéfiniment et n'alimente plus le détecteur Hall avec les autres voies :
les autres touches peuvent être pressées physiquement pour tester leur
couplage analogique, mais elles ne produisent plus de notes pendant ce mode.

Le tableau enregistre une paire ADC toutes les 40 conversions environ,
soit environ 2 ms par ligne et 8,2 s de fenêtre circulaire. Les trois
valeurs Hall sous la même adresse mux, les ticks TIM5, les compteurs de
callbacks et les GPIO mux sont conservés. La première hausse de 2500 codes
au-dessus du niveau stable cible renseigne `g_hall_hold_trigger_tick`; la
trace se fige 1,5 s plus tard. Si aucune hausse ne survient, arrêter la
carte avec GDB immédiatement après la séquence de touches pour conserver
les dernières 8,2 s. Ce seuil sert uniquement à figer l'enregistrement.
Il ne modifie ni seuil musical ni logique RELEASE.

```gdb
shell cls
set pagination off
set logging file hall_hold_mux_gdb.txt
set logging overwrite on
set logging enabled on
p/x *(unsigned char*)&g_hall_hold_target_key
p/x *(unsigned char*)&g_hall_hold_mode_arm
p/x *(unsigned char*)&g_hall_hold_mode_active
p/x *(unsigned char*)&g_hall_hold_target_mux
p/x *(unsigned char*)&g_hall_hold_target_adc
p/x *(unsigned short*)&g_hall_hold_baseline
p/x *(unsigned int*)&g_hall_hold_held_at_lock
p/x *(unsigned int*)&g_hall_hold_trigger_tick
p/x *(unsigned short*)&g_hall_hold_trigger_raw
p/x *(unsigned char*)&g_hall_hold_trace_frozen
p/x *(unsigned int*)&g_hall_hold_sequence
p/x &g_hall_hold_trace
dump binary memory hall_hold_mux.bin (char*)&g_hall_hold_trace ((char*)&g_hall_hold_trace+98304)
set logging enabled off
```

Les 4096 enregistrements font 24 octets chacun. Trier les entrées non
nulles par `sequence` (u32), puis lire `tim5_tick` (u32), `raw_a`, `raw_b`,
`raw_c`, `adc1_callbacks`, `adc2_callbacks` (cinq u16), `mux_odr`,
`mux_idr`, `completing_adc`, `reserved` (quatre u8), et deux octets de
padding. La valeur cible est `raw_a` si `g_hall_hold_target_adc = 0`,
`raw_b` si 1, `raw_c` si 2. `mux_odr` et `mux_idr` doivent rester égaux à
`g_hall_hold_target_mux`.
