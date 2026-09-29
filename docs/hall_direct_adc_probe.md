# Comparaison DMA et ADC direct sur la même voie Hall

Après la capture mux fixe de 5 ms, TIM6 est arrêté. La superloop arrête les
deux DMA ADC, configure sur le même ADC physique et le même canal Hall une
conversion injectée déclenchée par logiciel avec le même temps
d'échantillonnage (64,5 cycles), puis lit jusqu'à 64 conversions par polling.
Le mux ne change pas entre les deux séries. Les ADC DMA et TIM6 sont ensuite
redémarrés et six paires sont rejetées avant le prochain échantillon Hall.
Les seuils et la décision RELEASE restent inchangés. La lecture injectée
utilise le même ADC et la même broche, mais un groupe de conversion distinct
du groupe régulier employé par DMA; cette différence doit être prise en
compte pour interpréter un désaccord.

`g_hall_direct_state` vaut 0 avant le déclenchement, 1 en attente de la
superloop, 2 pendant les lectures, 3 quand la trace est finie. Il faut
arrêter la carte quand il vaut 3. `g_hall_direct_error` vaut normalement 0
et `g_hall_direct_restore_ok` vaut 1; un autre résultat indique un échec de
configuration, de conversion ou de reprise du pipeline.

```gdb
shell cls
set pagination off
set logging file hall_direct_gdb.txt
set logging overwrite on
set logging enabled on
p/x *(unsigned char*)&g_hall_direct_state
p/x *(unsigned int*)&g_hall_direct_error
p/x *(unsigned char*)&g_hall_direct_restore_ok
p/x *(unsigned int*)&g_hall_direct_count
p/x *(unsigned int*)&g_hall_fixed_count
p/x *(unsigned char*)&g_hall_fixed_key
p/x *(unsigned char*)&g_hall_fixed_mux
p/x *(unsigned char*)&g_hall_fixed_adc
p/x *(unsigned short*)&g_hall_fixed_baseline
p/x *(unsigned short*)&g_hall_fixed_trigger_raw
p/x &g_hall_direct_trace
p/x &g_hall_fixed_trace
dump binary memory hall_direct.bin (char*)&g_hall_direct_trace ((char*)&g_hall_direct_trace+1024)
dump binary memory hall_fixed.bin (char*)&g_hall_fixed_trace ((char*)&g_hall_fixed_trace+5120)
dump binary memory hall_capture.bin (char*)&g_hall_capture ((char*)&g_hall_capture+294912)
dump binary memory hall_calibration.bin (char*)&g_hall_capture_calibration ((char*)&g_hall_capture_calibration+192)
set logging enabled off
```

`hall_direct.bin` contient 64 entrées de 16 octets, dont seules les
`g_hall_direct_count` premières sont valides : `sequence`, `tim5_tick`,
`adc_isr` (trois u32), puis `raw` et `status` (deux u16). Comparer `raw`
aux derniers `raw_a`, `raw_b` ou `raw_c` de `hall_fixed.bin` selon
`g_hall_fixed_adc` (0, 1, 2). Si les lectures directes restent hautes,
l'entrée ADC reste haute sans timer ni DMA. Si elles retombent, l'écart
porte sur la chaîne de conversion régulière, son DMA ou sa configuration;
la coupure temporaire de TIM6/DMA peut elle-même modifier un couplage
électrique, donc il faudra préciser ce mécanisme avant correction.
