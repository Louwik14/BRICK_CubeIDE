# Séquenceur — sonde WCET hardware

La structure RAM `g_seq_wcet_diag` expose, pour chaque section, `last_cycles`,
`max_cycles` et `count`. Aucun affichage n'est effectué dans le chemin chaud.

Sous GDB :

```gdb
info address g_seq_wcet_diag
x/21wx ADRESSE
```

En build Release/LTO, GDB peut conserver le symbole sans conserver son type.
Chaque métrique est exactement le triplet de `uint32_t` suivant :
`last_cycles`, `max_cycles`, puis `count`. Le dump se décode dans l'ordre :

| Word | Offset | Champ |
|---:|---:|---|
| 0 | `+0x00` | `plock_restore.last_cycles` |
| 1 | `+0x04` | `plock_restore.max_cycles` |
| 2 | `+0x08` | `plock_restore.count` |
| 3 | `+0x0C` | `plock_apply.last_cycles` |
| 4 | `+0x10` | `plock_apply.max_cycles` |
| 5 | `+0x14` | `plock_apply.count` |
| 6 | `+0x18` | `play_scheduler.last_cycles` |
| 7 | `+0x1C` | `play_scheduler.max_cycles` |
| 8 | `+0x20` | `play_scheduler.count` |
| 9 | `+0x24` | `note_fx.last_cycles` |
| 10 | `+0x28` | `note_fx.max_cycles` |
| 11 | `+0x2C` | `note_fx.count` |
| 12 | `+0x30` | `note_admission.last_cycles` |
| 13 | `+0x34` | `note_admission.max_cycles` |
| 14 | `+0x38` | `note_admission.count` |
| 15 | `+0x3C` | `publication.last_cycles` |
| 16 | `+0x40` | `publication.max_cycles` |
| 17 | `+0x44` | `publication.count` |
| 18 | `+0x48` | `full_pass.last_cycles` |
| 19 | `+0x4C` | `full_pass.max_cycles` |
| 20 | `+0x50` | `full_pass.count` |

Il y a 7 métriques contiguës, chacune composée de 3 mots de 4 octets :
`7 * 3 * 4 = 84` octets (`0x54`). Pour décoder `x/21wx`, prendre chaque
groupe de trois valeurs comme `last`, `max`, `count`; par exemple les mots
12 à 14 décrivent `note_admission`.

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
