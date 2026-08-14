# Diagnostic GDB du chemin piste vers MAIN

## Périmètre

Cette instrumentation temporaire existe uniquement avec
`BRICK6_MIXER_PATH_DIAG=ON`, configuration `Release Low-Cost`. Elle sélectionne
une entity sans tenir compte de la provenance clavier/séquenceur et couvre le
seam commun aux formats externes `POLY_STEREO`, `MULTI_STEREO` et `MULTI_MONO`.

Les quatre mesures sont :

- A : buffer external réellement retenu par le lane plan, avant le traitement
  commun de piste. Pour Poly et Multi, le filtre par voix est déjà appliqué en
  amont et le filtre commun de piste est volontairement sauté par le mixer.
- B : signal après gain, mute ramp, pan et inserts de piste. Le fast path Poly
  est mesuré sur les valeurs effectivement calculées dans son fan-out; Multi est
  mesuré dans ses buffers matérialisés après inserts.
- C : contribution sèche B multipliée par `MIXER_TRACK_NOMINAL_TRIM`, juste avant
  son ajout au bus de destination.
- D : bus de destination juste après cette contribution. C'est `bus_main` pour
  une piste normale et `bus_group` si le bit group-child est présent.

Les sends, retours, master FX, gain master, PCM24 et sortie hardware sont hors
périmètre.

## Format et RAM

Le ring contient 16 snapshots de 88 octets, soit 1408 octets. Les trois compteurs
32 bits et les quatre octets de contrôle occupent 16 octets; le map ajoute 4
octets d'alignement. Empreinte RAM liée exacte : **1428 octets** (1424 octets
d'objets plus 4 octets de padding).

Offsets sans DWARF : `seq=0`, `block_seq=4`, `entity=8`, `mix_track=9`,
`source_flags=10`, `track_flags=11`, `frames=12`, `invalid=14`, `stages=15`,
pics A/B/C/D à `16/24/32/40`, coefficients à partir de `48`. Un snapshot fait 88.

`source_flags` : bits 0..2 source-kind, bits 3..5 external-format, bit 6 hardware
actif, bit 7 external actif. `track_flags` : bit 0 lane actif, bit 1 mute, bit 2
route dry active, bit 3 group-child, bit 4 filtre commun appliqué, bit 5 insert
présent. Le mixer n'a pas d'état solo local. `invalid_flags` : NaN=1, Inf=2,
amplitude absolue >8=4. `stage_flags` doit valoir 15.

Les coefficients stockés sont gain piste début/fin, pan L/R début/fin, VCA
début/fin et mute-gain début/fin. Poly/Multi arrivent préfiltrés et pré-VCA par
voix; leur VCA commun de piste vaut donc 1.

## Build

```text
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBRICK6_VARIANT=lowcost -DBRICK_TEST_BUILD=OFF -DBRICK6_MIXER_PATH_DIAG=ON
cmake --build build/Release --target BRICK6_CUBE -j 4
```

## Acquisition KBD puis SEQ

Utiliser l'identifiant entity 0-based de la piste à comparer à la place de `0`.
Pour KBD, maintenir l'accord jusqu'à apparition de la distorsion avant d'armer.
Pour SEQ, laisser l'accord déjà établi avant d'armer. Faire deux resets et deux
acquisitions séparées :

```gdb
call mixer_path_diag_reset()
set *(unsigned char *)&mixer_path_diag_entity_id = 0
set *(unsigned char *)&mixer_path_diag_remaining_blocks = 8
set *(unsigned char *)&mixer_path_diag_enabled = 1
continue
```

Interrompre le CPU puis vérifier d'abord l'invariant :

```gdb
p/u *(unsigned int *)&mixer_path_diag_captured_blocks
p/u *(unsigned int *)&mixer_path_diag_write_seq
x/16wx &mixer_path_diag_snapshots
```

Le dump complet ne vaut que si `captured_blocks > 0`, `write_seq > 0` et le
premier mot du ring (`snapshot[0].seq`) est non nul.

## Dump complet sans types debug

```gdb
set $base = (unsigned char *)&mixer_path_diag_snapshots
set $i = 0
while $i < 16
  set $s = $base + $i * 88
  if *(unsigned int *)($s+0) != 0
    printf "snap=%u seq=%u block=%u entity=%u mix=%u src=0x%02x track=0x%02x frames=%u invalid=0x%02x stages=0x%02x\n", $i, *(unsigned int *)($s+0), *(unsigned int *)($s+4), *(unsigned char *)($s+8), *(unsigned char *)($s+9), *(unsigned char *)($s+10), *(unsigned char *)($s+11), *(unsigned short *)($s+12), *(unsigned char *)($s+14), *(unsigned char *)($s+15)
    printf "  A=(%g,%g) B=(%g,%g) C=(%g,%g) D=(%g,%g)\n", *(float *)($s+16), *(float *)($s+20), *(float *)($s+24), *(float *)($s+28), *(float *)($s+32), *(float *)($s+36), *(float *)($s+40), *(float *)($s+44)
    printf "  gain=(%g,%g) panL=(%g,%g) panR=(%g,%g) vca=(%g,%g) mute=(%g,%g)\n", *(float *)($s+48), *(float *)($s+52), *(float *)($s+56), *(float *)($s+60), *(float *)($s+64), *(float *)($s+68), *(float *)($s+72), *(float *)($s+76), *(float *)($s+80), *(float *)($s+84)
  end
  set $i = $i + 1
end
```

## Décision

- A diverge : hypothèse A, signal external réellement reçu.
- A concorde mais B diverge : hypothèse B, traitement commun piste/fast fan-out.
- B concorde mais C ou l'écart D diverge : hypothèse C, trim/routage/accumulation.
- A, B, C et l'effet sur D concordent : hypothèse D; poursuivre vers retours,
  master FX, gain master puis pack PCM24.

Comparer aussi `source_flags`, `track_flags` et les coefficients avant les pics.
En particulier, le bit hardware de `source_flags` révèle un lane HW+external et
le bit group-child indique que D représente le bus GROUP, pas encore MAIN.

## Audit statique ciblé

Aucune anomalie certaine n'a été trouvée dans ce périmètre : `bus_main` et
`bus_group` sont intégralement clear au début du callback, le lane mask ne visite
chaque mix-track qu'une fois, les external inputs sont invalidés après le mix,
`ext_frames` doit égaler `frames`, et `tracks[0]` n'est utilisé qu'après le mix
comme destination MAIN. Le trim nominal n'apparaît qu'au fan-out sec/send.

Deux états restent à trancher par la capture, sans constituer une preuve de bug :
le lane plan additionne volontairement hardware et external lorsque les deux sont
actifs (`source_flags` bit 6 et bit 7), et Poly utilise un fast fan-out tandis que
Multi utilise le chemin matérialisé. Les seams A/C/D et les équations de
gain/pan/mute restent communs conceptuellement; B mesure exactement chaque
implémentation sans en changer le chemin.
