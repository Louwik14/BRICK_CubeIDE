# Contrat de format Stream/Multi

## Etat livrÃ© aprÃ¨s l'Ã©tape 8

Le contrat est maintenant raccordÃ© aux chemins Stream et Multi. Une page
logique reste un slot physique statique de 16 Kio. Le pool compte 1280 pages :
1024 pages de slot, 128 pages de fenÃªtre voix et 128 pages de marge.

Le prÃ©socle Multi consomme 2 pages mono ou 4 pages stÃ©rÃ©o pour 8192 frames ;
la fenÃªtre suit la mÃªme gÃ©omÃ©trie et le budget Multi est de 512 pages. Les
refs vÃ©rifient key, page, gÃ©nÃ©ration, format, stride, frames/page et epoch.
Les pending et targets sont comparÃ©s Ã  la description courante avant de
remplir une page.

Les chemins stop, steal, owner release et libÃ©ration diffÃ©rÃ©e nettoient les
pins et les pending ; le relancement d'un Multi interrompu nettoie son pool.
Le reader Looper conserve Ã©galement la ref complÃ¨te. Le rendu mono reste natif
dans le mixer : le buffer droit de compatibilitÃ© est un discard de bloc, sans
accumulation L/R pour un instrument Multi mono. Le chemin stÃ©rÃ©o conserve son
comportement.

Les dettes non couvertes sont le filtre/VCA par voice Multi, la vraie
interpolation, le mono-tail natif, l'optimisation des pages voisines et la
mesure IRQ rÃ©elle sur matÃ©riel.

Ce contrat est introduit par l’étape 1 du plan `docs/plan_stream_multi_mono.md`.
Il ne modifie encore aucun décodage ni aucun chemin audio.

## Formats internes

Les seuls formats FLOAT32 du page cache Stream/Multi sont `MONO` et
`STEREO_INTERLEAVED`. Le WAV conserve ses `channels`, `bits_per_sample` et
`block_align` comme métadonnées de source. `block_align` n’est jamais utilisé
comme stride FLOAT32 interne.

| Format | Stride FLOAT32 | Bytes/frame | Frames/page | Pages pour 8192 frames |
| --- | ---: | ---: | ---: | ---: |
| `FLOAT32_MONO` | 1 | 4 | 4096 | 2 |
| `FLOAT32_STEREO_INTERLEAVED` | 2 | 8 | 2048 | 4 |

La page physique reste un slot statique de 16 Kio. Aucun slot n’est partagé
entre deux pages logiques et aucune allocation dynamique n’est introduite.

Les helpers de `Inc/Sampler/sample_audio_format.h` sont l’unique source de
géométrie : frames/page, stride, bytes/frame, index et début de page, nombre
de pages requis, présocle et fenêtre. Les calculs de frames utilisent des
intermédiaires 64 bits.

## Métadonnées et identité

Les descripteurs Stream, Multi, page, pending/target, span, ref, play plan,
reader et voice portent le format, le stride et les frames/page. Les refs et
targets portent aussi l’epoch d’enregistrement afin de rendre explicite
l’invalidation d’un ancien `(key, page, génération, format)` lorsque cette
protection sera raccordée au cache.

Un instrument Multi porte un format unique. La validation d’homogénéité et la
persistance effective de l’index sont traitées par les étapes Multi dédiées ;
cette étape ne convertit aucun sample et ne réserve aucune page supplémentaire.

## Consommateurs encore stéréo à migrer

Les alias historiques `SAMPLE_PAGE_FRAMES`, `SAMPLE_PAGE_CHANNELS`,
`SAMPLE_PAGE_FRAME_STRIDE_FLOATS` et `SAMPLE_PAGE_BYTES_PER_FRAME` décrivent
explicitement le chemin stéréo existant. Les occurrences suivantes restent
intentionnelles jusqu’aux étapes de cache/rendu correspondantes :

- `Src/Sampler/sample_page_cache.c` : tableau physique et index de page ;
- `Src/Sampler/sample_cache.c` : fenêtres, offsets et accès page ;
- `Src/Sampler/sample_stream_manager.c` : deadlines, plages et decode targets ;
- `Src/Sampler/sample_play_plan.c` : spans et exigences de pages ;
- `Src/Sampler/sample_voice_reader.c` : lecture interleaved et kernels ;
- `Src/Sampler/sample_pool.c` : budget de pages ;
- `Src/Core/brick6_looper_runtime.c` et `Src/Sampler/sampler_ram_pool.c` :
  consommateurs explicitement stéréo ou RAM hors chantier.

Les constantes ne sont donc pas supprimées à cette étape. Leur remplacement
par les helpers de format appartient aux étapes 2 à 7 du plan.
