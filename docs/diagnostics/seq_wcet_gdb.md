# Séquenceur — sonde WCET hardware

La structure RAM `g_seq_wcet_diag` expose, pour chaque section, `last_cycles`,
`max_cycles` et `count`. Aucun affichage n'est effectué dans le chemin chaud.

Sous GDB :

```gdb
p g_seq_wcet_diag
set g_seq_wcet_diag = {0}
```

Le scénario de stress recommandé est une boundary avec 32 locks à restaurer,
32 locks à appliquer, 8 PLAY (ROLL actif si pertinent), et les sorties déjà
occupées afin d'exercer retrigger/stealing. La publication mesure le batch final
fusionné CONTROL vers AUDIO. `full_pass` mesure l'appel CONTROL complet.

À 240 MHz et 750 Hz, le budget est d'environ 320 000 cycles par passe.
Après mesure d'une lane, une borne conservative est :

```text
WCET = 15 * (plock_restore + plock_apply + play_scheduler_8_PLAY
             + note_fx_lane + 8 * note_admission)
       + coût_lane_master_GROUP
       + publication_gros_batch
       + autres_coûts_globaux
```

Ne pas multiplier `publication`, `full_pass`, ni les autres coûts globaux par
16. `full_pass.max_cycles`, mesuré directement avec toutes les lanes du scénario
actives, est la comparaison finale privilégiée avec 320 000 cycles.
