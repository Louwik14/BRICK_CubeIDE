# Benchmark Sampler STREAM end-to-end

Le firmware Release courant lance, trois secondes apres le boot complet, une
campagne de verite du Sampler STREAM. AUDIO/SAI DMA reste actif. Le superloop
neutralise alors les services UI/USB ordinaires, mais le benchmark fait avancer
le service STORAGE normal; les preemptions AUDIO de STORAGE et l'attente de la
prochaine frontiere AUDIO font donc partie de la mesure.

## Chemin mesure

Le benchmark ne soumet aucune lecture au backend physique. A la frontiere IRQ
AUDIO, il arme de vrais `sample_voice_reader_t` par le binding differe reserve a
l'instrumentation. Ce binding publie la lease normale. Le chemin est ensuite :

```text
AUDIO VoiceReader -> lease -> STORAGE stream task -> Stream Manager
-> lookup Page Cache -> reserve/recycle -> sample_stream_io
-> mapping physique -> scheduler -> block device -> SDMMC/IDMA/D-cache
-> IDMA double-buffer, publication progressive par chunks de 4 KiB
-> reader resolve d'un prefixe LOADING -> rendu d'un bloc AUDIO de 32 frames
-> DATAEND/CMD12 -> LOADING->READY dans le Page Cache
```

Le trigger commun est le `DWT->CYCCNT` pris dans l'IRQ AUDIO juste avant les N
bindings. `PAGE READY` est pris immediatement apres la transition validee
`LOADING -> READY` effectuee par `sample_page_cache_finish_loading`.
`FIRST RENDERED` est pris apres que
`sample_voice_reader_render_fwd_1x_ready_simple` a effectivement lu et melange
un bloc de 32 frames de la page. `FIRST CHUNK AVAILABLE`, `AUDIO SEEN`, READY
et resolve restent des frontieres distinctes.

Cette image est un prototype materiel volontairement borne. Une lecture
canonique FLOAT32 stereo, alignee, contigue et exactement egale a 16 KiB arme
un unique CMD18 avec IDMA double-buffer (`IDMABSIZE=4096`). Les bases initiales
sont `page+0` et `page+4096`; les deux premieres IRQ `IDMABTC` recyclent les
bases inactives vers `page+8192` puis `page+12288`. Chaque chunk termine est
invalide individuellement avant publication du watermark
`available_frame_end`; AUDIO peut resoudre le prefixe d'une page encore
`LOADING`. Les autres tailles et chemins conservent le transport single-buffer.

Le binding differe ne duplique aucune logique Streamer : il initialise le meme
VoiceReader et publie par `sample_voice_reader_publish_lease`; seule la tentative
READY synchrone du binding produit normal est differee pour autoriser le cold
start mesure.

## Workload et cache

`STREAM_E2E_BENCH_PAGE_BYTES` est strictement egal a `SAMPLE_PAGE_BYTES` (16 KiB
dans cette image). Le fichier brut FLOAT32 stereo fait 512 MiB, ou 256 MiB si
l'espace libre impose le repli. Une permutation xorshift deterministe couvre
toutes ses pages, alignees sur la page produit. La selection ecarte les rares
pages qui croisent une frontiere d'extent FAT : chaque sample conserve ainsi la
semantique d'une transaction physique de 16 KiB, tout en laissant
le mapping normal refaire et mesurer sa propre resolution apres le trigger.
Le SDMMC de la campagne est force au reglage physique valide CLKDIV 2, soit
50 MHz avec son horloge kernel de 200 MHz.

Avant chaque trigger, STORAGE choisit N pages distinctes dont le lookup vaut
`FREE`; AUDIO refait atomiquement ce lookup avant publication. Une batch devenue
residente entre les deux controles est rejetee et compte comme hit. Seules les
batches confirmees miss incrementent `cache_misses` et alimentent les
distributions cold.

Le warm-up charge par le meme chemin
`SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT` pages (36 actuellement). Il remplit donc
le pool runtime sans clear artificiel. Les requetes suivantes provoquent la
selection de victime et le recycle normaux. `cold_with_free_slot` et
`cold_with_recycle` proviennent de la branche reellement prise dans
`sample_page_cache_alloc_empty_slot_key`.

La cardinalite se choisit a la compilation par
`STREAM_E2E_BENCH_SIMULTANEOUS_COLD_STARTS` : 1, 2, 4 ou 8. Une batch partage un
timestamp trigger et publie N leases vers N pages distinctes. Le resultat expose
les frontieres premiere/derniere voix et les distributions `batch_all_ready` et
`batch_all_rendered`; cette derniere mesure le trigger jusqu'au dernier premier
frame rendu.

## Resultat et recouvrements

La structure principale est `g_stream_end_to_end_bench`. Toutes les durees gardent
somme/min/max exacts en cycles et leur vue en microsecondes; les percentiles
prioritaires utilisent un histogramme de 2 us. La structure contient count,
somme, moyenne, min, max, P50,
P90, P95, P99 et P99.9 pour les quatre distributions end-to-end prioritaires,
`physical_transaction` et `data_transfer`. Les autres phases conservent
count/somme/moyenne/min/max.

Les phases `manager_pick`, `cache_lookup`, `cache_reserve` et `cache_recycle_time`
sont des couts CPU imbriques dans la chronologie murale; elles ne doivent pas
etre additionnees aux intervalles `trigger_to_*`. De meme,
`physical_transaction` contient command response, DATA et CMD12, tandis que les
sous-mesures les detaillent.

Les anciennes frontieres physiques restent disponibles : accept backend,
mapping, submit/accept/launch block-device, pre-maintenance cache, command
response, DATA, stop command, maintenance cache post-DMA, publication block et
completion backend. Aucun printf, UART, affichage, I/O synchrone ou calcul de
percentile n'existe dans le chemin mesure. La finalisation lourde arrive apres
la campagne.

Le resultat expose aussi les quatre timestamps de publication, les erreurs de
generation/ordre/doublon, les starvations, la verification bit-perfect des
quatre chunks et le compteur de rendus qui ne precedent pas READY. La structure
`g_sdmmc_async_progress_diag` expose `IDMABTC`, CMD18/CMD12, somme/moyenne/max
du rearmement et depassements de la fenetre 164 us.

Cette branche de mesure utilise des demi-buffers AUDIO de 32 frames, soit 667 us
a 48 kHz. La deadline fonctionnelle reste 64 frames, soit 1333 us et donc deux
IRQ AUDIO. Les compteurs voix exposes sont `render_within_1_block_32`,
`render_within_2_blocks_32` et `render_missed_2_blocks_32`; leurs equivalents
batch portent le prefixe `batch_all_rendered_` ou `batch_missed_`. Le resultat
expose aussi le nombre d'IRQ, la somme et le maximum de leurs cycles, ainsi que
leur charge moyenne en pourcentage sur la campagne mesuree.

Dump GDB :

```gdb
shell cls
set pagination off
set print pretty on
info address g_stream_end_to_end_bench
p g_stream_end_to_end_bench
p g_sdmmc_async_progress_diag
p g_stream_end_to_end_bench.trigger_to_first_chunk_available
p g_stream_end_to_end_bench.first_chunk_available_to_audio_seen
p g_stream_end_to_end_bench.trigger_to_first_render
p g_stream_end_to_end_bench.trigger_to_full_page_ready
p g_stream_end_to_end_bench.chunk_data_mismatch_count
p g_stream_end_to_end_bench.starvation_count
p g_stream_end_to_end_bench.first_render_not_before_ready_count
```
