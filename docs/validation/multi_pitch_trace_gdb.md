# Trace GDB des voix Multi pitchées

Instrumentation temporaire, sans `printf`, sans freeze et sans modification de
l'algorithme audio. Elle est compilée uniquement avec
`BRICK6_MULTI_PITCH_TRACE=1`.

## Build Release Low-Cost

```text
cmake --preset Release -DBRICK6_MULTI_PITCH_TRACE=ON
cmake --build build/Release --target BRICK6_CUBE.elf -j 4
```

Le symbole de reset est `brick6_multi_pitch_trace_reset`.
Dans l'ELF Release construit le 2026-08-08 :

| Objet | Adresse | Taille |
|---|---:|---:|
| `g_brick6_multi_pitch_trace_ring` | `0xC1F17700` | `0xB0000` = 720896 octets |
| `g_brick6_multi_pitch_trace_header` | `0xC1FC7700` | `0x40` = 64 octets |
| reset `brick6_multi_pitch_trace_reset` | `0x0806ABE4` | `0x78` |

Les deux objets sont dans `.sdram_recorder` / `SDRAM_RECORDER`, région SDRAM
MPU non-cacheable et hors RAM_D1. Le ring contient 4096 événements de 176
octets. Le snapshot contigu
à dumper va de `0xC1F17700` inclus à `0xC1FC7740` exclus, soit `0xB0040`
(720960) octets.

## Séquence GDB sans DWARF

Les commandes utilisent uniquement des adresses brutes. Après le flash et la
connexion GDB, laisser le firmware démarrer, puis utiliser `Ctrl+C` au moment
voulu pour exécuter le reset manuel de trace et reprendre.

```text
set confirm off
set pagination off
set $trace_reset = 0x0806ABE4

# Run A : boot puis remise à zéro de la trace avant la capture propre
monitor reset halt
load
continue
# Ctrl+C
call ((void (*)())$trace_reset)()
continue
# laisser jouer la situation pitchée propre
# Ctrl+C
dump binary memory run_propre.bin 0xC1F17700 0xC1FC7740

# Run B : reset matériel, remise à zéro de la trace, puis capture du grésillement
monitor reset halt
load
continue
# Ctrl+C
call ((void (*)())$trace_reset)()
continue
# laisser jouer davantage de voix pitchées jusqu'au grésillement
# Ctrl+C
dump binary memory run_bug.bin 0xC1F17700 0xC1FC7740
```

Le `Ctrl+C` est le seul arrêt de capture ; le code ne se fige jamais et le
ring continue d'écraser les événements les plus anciens lorsqu'il est plein.

## Layout binaire

Chaque fichier commence par le ring, puis le header :

```text
offset 0x00000000, longueur 0x0B0000 : 4096 x event, 176 octets
offset 0x000B0000, longueur 0x000040 : header, 16 x uint32 little-endian
```

Header, dans l'ordre :

```text
magic, abi_version, header_size, entry_size,
capacity, write_index, valid_count, dropped_count,
reset_count, block_sequence, segment_count, voice_count,
block_count, anomaly_count, last_block_cycles, reserved
```

Chaque événement est exactement 44 `uint32_t` little-endian, dans l'ordre :

```text
sequence, event_kind, block_sequence, event_flags,
voice_id, owner_track, sample_id, key_domain, key_object_id,
voice_generation, registration_epoch,
position_before_q16, position_after_q16, step_q16, frames_rendered,
expected_page, actual_page, expected_neighbor_page, actual_neighbor_page,
offset_frames, current_slot, neighbor_slot,
current_page_generation, neighbor_page_generation,
current_epoch, neighbor_epoch,
fraction_begin_q16, fraction_end_q16,
loop_mode, direction_before, direction_after, page_changed,
source_checksum, source0_bits, source1_bits, neighbor_bits,
cycles, scratch0, scratch1, scratch2, scratch3,
multi_voice_count, pitched_voice_count, render_order
```

`event_kind` vaut `1=segment pitché`, `2=voix Multi`, `3=bloc audio`.
Les valeurs `source0_bits`, `source1_bits` et `neighbor_bits` sont les bits
bruts de flottants IEEE-754 ; `source_checksum` est un FNV-1a léger sur ces
trois valeurs. Les événements d'un même bloc sont dans l'ordre de rendu par
`sequence`; `render_order` est le rang de la voix dans ce bloc.

Les flags `event_flags` sont : bit 0 page réelle différente de la page
attendue, bit 1 offset hors page, bit 2 référence invalide, bit 3 epoch
incohérent, bit 4 position après incohérente, bit 5 NaN/Inf, bit 6 source hors
plage attendue.

Quand `BRICK6_MULTI_PITCH_TRACE=0`, les objets et la mémoire du ring ne sont
pas produits.
