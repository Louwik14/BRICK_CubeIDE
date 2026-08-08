# Trace causale des underruns streaming

## Build capture

Macro d'activation : `BRICK6_STREAM_UNDERRUN_TRACE=1`.

Commande Release sans DWARF :

```text
cmake --preset Release -DBRICK6_MULTI_PITCH_TRACE=OFF -DBRICK6_SD_INIT_DIAG=OFF -DBRICK6_STREAM_UNDERRUN_TRACE=ON
cmake --build build/Release --target BRICK6_CUBE.elf -j4
```

Le build vérifié est `build/Release/BRICK6_CUBE.elf`. Les adresses ci-dessous
proviennent de `arm-none-eabi-nm -S --size-sort` sur cet ELF :

| élément | adresse | taille |
|---|---:|---:|
| `g_brick6_stream_underrun_trace` | `0xC1F17700` | `0x10020` = 65568 octets |
| `brick6_stream_underrun_trace_reset` | `0x080762D4` | `0xB0` |

Le snapshot est placé dans `.sdram_recorder` à `0xC1F17700`. Le ring est
contenu dans le snapshot, immédiatement après son en-tête. La trace est
inactive par défaut ; avec `BRICK6_STREAM_UNDERRUN_TRACE=0`, le symbole et la
réservation du snapshot n'existent pas dans l'ELF vérifié.

## Capture GDB sans symboles de debug

Avec le programme arrêté dans GDB :

```gdb
set pagination off
set confirm off
set $trace = 0xC1F17700
call ((void (*)(void))0x080762D5)()
continue
```

Déclencher les 8 voix streaming, attendre l'underrun, puis faire `Ctrl+C` et
dump le snapshot complet :

```gdb
dump binary memory run_8voices_underrun.bin 0xC1F17700 0xC1F27720
```

La borne est exclusive : `0xC1F27720 - 0xC1F17700 = 0x10020`.

## Layout binaire

Le fichier commence par 32 octets d'en-tête, puis 1024 événements de 64
octets. Les compteurs sont en little-endian ARM.

### En-tête, offsets relatifs au début du fichier

| offset | taille | champ |
|---:|---:|---|
| `0x00` | 4 | magic `0x53555254` |
| `0x04` | 4 | ABI `1` |
| `0x08` | 4 | taille événement `64` |
| `0x0C` | 4 | capacité `1024` |
| `0x10` | 4 | `write_index` monotone |
| `0x14` | 4 | `count`, plafonné à 1024 |
| `0x18` | 4 | `dropped_count` après rotation |
| `0x1C` | 4 | séquence du dernier `CONSUME_MISS` |
| `0x20` | 65536 | événements, slot `(sequence - 1) % 1024` |

### Événement, 64 octets

| offset | taille | champ |
|---:|---:|---|
| `0x00` | 4 | séquence |
| `0x04` | 4 | audio frame basse |
| `0x08` | 4 | audio frame haute |
| `0x0C` | 4 | `DWT->CYCCNT` |
| `0x10` | 4 | durée en cycles |
| `0x14` | 4 | domaine de la clé |
| `0x18` | 2 | objet/sample de la clé |
| `0x1A` | 2 | réservé |
| `0x1C` | 4 | page |
| `0x20` | 4 | génération |
| `0x24` | 4 | epoch d'enregistrement |
| `0x28` | 4 | `value0` |
| `0x2C` | 4 | `value1` |
| `0x30` | 4 | `value2` |
| `0x34` | 4 | `value3` |
| `0x38` | 1 | type |
| `0x39` | 1 | source : 0 Classic, 1 Multi |
| `0x3A` | 1 | `voice_id` |
| `0x3B` | 1 | état page/voix |
| `0x3C` | 1 | raison |
| `0x3D` | 1 | backend : 1 contigu, sinon FatFs/valeur backend |
| `0x3E` | 1 | résultat |
| `0x3F` | 1 | réservé |

Dans `state`, `0=FREE`, `1=RESERVED`, `2=LOADING`, `3=READY`, `4=FAILED` et
`0xFF=ABSENT`.

Les événements sont :

```text
0 VOICE_STATE       état, position, step, avance READY, premier manque
1 NEED_CREATED      besoin créé ; value0=role, value1=frames avant deadline
2 NEED_SELECTABLE   besoin sélectionnable ; value0=avance, value1=frames avant deadline
3 SCHEDULER_DECISION
4 LOAD_BEGIN
5 IO_BEGIN
6 IO_END            bytes, lectures physiques, seeks, FatFs/file-open
7 LOAD_END          résultat, bytes, lectures, seeks, décodage
8 READY             frame_count, bytes, décodage, slot
9 CONSUME_MISS      position, frames restantes, role, value3=cause sequence
10 PAGE_STATE       value0=ancien état, value1=nouvel état
11 SERVICE_BEGIN    budget, besoins actifs, wake sequence, intervalle
12 SERVICE_BLOCKED  value0=retard de poll, backend=propriétaire du gate
13 SERVICE_END      pages/FatFs, bytes lus, cycles I/O, cycles décodage
14 MANAGER_END      pages, FatFs, bytes lus, cycles décodage
```

Pour `VOICE_STATE`, `value0=position`, `value1=step_q16`, `value2=frames
READY` et `page` est la première page manquante. Les événements
`VOICE_STATE` suivants listent les besoins : `value0=état`, `value1=frames
avant deadline`, `value2=0`, `value3=(role << 24) | need_count`.

Pour `SCHEDULER_DECISION`, `value0=candidate_count`, `value1=voix sous le
seuil critique`, `value2=besoins chargeables`. Une décision réussie a
`result=1` et `value3=(advance & 0xffff) | (frames_deadline << 16)` ; une
décision sans page a `result=0` et `reason` donne la cause exacte.

Pour `IO_END`, `value0=bytes lus`, `value1=lectures physiques`, `value2=seeks`
et `value3=(fatfs_ops << 16) | file_opens`. La durée couvre l'I/O synchrone ;
`decode_cycles` est dans `value3` de `LOAD_END`/`READY` et les cycles I/O
agrégés sont dans `SERVICE_END`.

Les raisons sont :

```text
0 NONE                  1 NO_ACTIVE_NEED       2 ALL_READY
3 ALL_LOADING           4 NO_CANDIDATE         5 RESERVE_FAILED
6 TARGET_FAILED         7 EPOCH_MISMATCH       8 ZERO_BUDGET
9 SERVICE_BYTE_BUDGET  10 SERVICE_PAGE_LIMIT  11 SERVICE_FATFS_LIMIT
12 SERVICE_TICK_LIMIT  13 MULTI_BULK_BLOCKED  14 GATE_BLOCKED
15 LOAD_ERROR           16 PUBLISH_ERROR
```

## Coût

RAM : 65568 octets dans SDRAM, dont 65536 octets de ring et 32 octets
d'en-tête. À macro désactivée : 0 octet de ring et aucun symbole global.

CPU : aucune écriture ni recherche quand la macro vaut 0. En capture, chaque
événement écrit 64 octets, lit le compteur audio/DWT et protège la publication
par une courte désactivation IRQ. Le chemin normal de service ajoute au plus
les événements des voix/besoins actifs et des I/O ; le chemin audio ne trace
que `CONSUME_MISS` et ne modifie aucune décision, allocation, priorité ou
transition du streamer.
