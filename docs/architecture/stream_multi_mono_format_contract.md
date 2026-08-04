# Contrat audio mono du sampler

## État courant

Le sampler conserve le format mono depuis le stockage jusqu'au dernier
traitement qui exige deux canaux. Le contrat couvre le Sampler RAM, le Stream
et les voix Multi. Le futur `GROUP` doit sélectionner le même chemin à partir
du format immuable du sample, sans introduire de renderer particulier.

Une page Stream/Multi reste un slot physique statique de 16 Kio :

| Format | Stride FLOAT32 | Octets/frame | Frames/page |
| --- | ---: | ---: | ---: |
| `FLOAT32_MONO` | 1 | 4 | 4096 |
| `FLOAT32_STEREO_INTERLEAVED` | 2 | 8 | 2048 |

Les helpers de `Inc/Sampler/sample_audio_format.h` sont l'unique source de
géométrie. Le `block_align` du WAV ne sert jamais de stride FLOAT32 interne.

## Lecture

Le play plan, le reader et le cursor portent le format, le stride, les
frames/page et l'epoch d'enregistrement. La navigation reste commune : pages,
position, pas, direction, loop, ping-pong, interpolation, fin et underrun.

Les kernels de sortie sont spécialisés :

```text
reader mono   → une lecture utile → une écriture mono
reader stéréo → lectures L/R      → écritures L/R
```

Le dispatch de format est effectué par voix ou par bloc, jamais au moyen
d'une duplication mono vers un buffer droit de rejet.

## Sampler RAM et Stream

```text
sample mono
→ renderer mono
→ filtre de piste mono
→ VCA, volume et mute mono
→ pan final vers L/R
→ inserts, sends et bus stéréo
```

Le Stream partage la même acquisition de pages et le même cursor entre mono
et stéréo. Son fast path forward 1× possède une sortie mono dédiée. Le shifter
de clip reste explicitement stéréo et empêche donc la sélection du chemin
mono-native.

Le Sampler RAM mono utilise un renderer Q16 mono couvrant forward, pitch,
reverse, loop et ping-pong. Les fades de démarrage et tails de declick ont un
point d'entrée mono ; aucun buffer droit de rejet n'est conservé.

## Multi

Chaque voix Multi garde son reader et son slot DSP indépendants. Pour une
source mono, l'ordre est strict :

```text
renderer mono
→ filtre mono de voix
→ VCA mono de voix
→ spread/pan de voix vers L/R
→ sommation de piste
```

Le canal droit n'existe pas avant la projection du spread. Une voix stéréo
conserve le renderer, le filtre et le VCA stéréo. Un instrument Multi reste
homogène : toutes ses zones partagent le même format interne.

## Mixer et promotion

Une source externe marquée `MONO_NATIVE` ou `MULTI_MONO` emprunte toujours la
lane mono quand aucune source matérielle stéréo n'est mélangée à la piste.
La présence ou le type d'un insert ne provoque plus de duplication avant le
filtre : tous les inserts de piste sont placés après le pan final et reçoivent
donc le signal L/R produit à cet endroit.

Les sources matérielles stéréo, les sources poly/stéréo et les mélanges
matériel + externe restent sur la lane stéréo existante.

## Invariants de cycle de vie

- le format d'une voix active ne change pas ;
- key, page, génération, format, stride, frames/page et epoch sont validés ;
- stop, steal, release et underrun libèrent les cursors et owners existants ;
- aucune allocation dynamique n'est effectuée dans le chemin audio ;
- le chemin stéréo ne passe pas par les kernels mono ;
- la loi de pan, l'ordre des inserts, les sends et le routing restent inchangés.

## Validation

Les changements du contrat mono doivent réussir les builds Release Low-Cost
et Release Premium. La validation fonctionnelle couvre au minimum forward,
pitch, reverse, loop, ping-pong, frontières de page, fin, underrun, fades,
declick, filtre, VCA, pan, spread et non-régression stéréo.

La mesure exacte du coût IRQ reste une mesure sur matériel.
