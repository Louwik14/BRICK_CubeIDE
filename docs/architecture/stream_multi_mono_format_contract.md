# Contrat de format Stream/Multi

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
