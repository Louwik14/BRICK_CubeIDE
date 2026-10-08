# Benchmark Sampler STREAM end-to-end

> Cette page decrit la campagne historique trigger-vers-premier-rendu. Le
> benchmark courant et son contrat de presocle sont documentes dans
> [stream_presocle_benchmark.md](stream_presocle_benchmark.md).

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
-> IDMA double-buffer, publication progressive par chunks de 512 octets
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
un unique CMD18 avec IDMA double-buffer (`IDMABSIZE=512`, donc
`IDMABNDT=512/32=16`). Les bases initiales sont `page+0` et `page+512`.
Chaque IRQ `IDMABTC` rearme le buffer devenu inactif vers le chunk `n+2`,
jusqu'aux offsets `page+15360` et `page+15872`. Chaque chunk termine est
invalide individuellement avant publication du watermark
`available_frame_end`; AUDIO peut resoudre le prefixe d'une page encore
`LOADING`. Les autres tailles et chemins conservent le transport single-buffer.
Le H743 signale aussi le dernier des 32 buffers par `IDMABTC`: cette IRQ publie
le chunk 31, masque seulement `IDMABTCIE`, puis laisse IDMA, DPSM et DLEN
atteindre naturellement `DATAEND`. DATAEND emet ensuite l'unique CMD12 et la
completion normale effectue `LOADING -> READY`.

Le changement NVIC est limite a SDMMC1 : priorite preemptive 5 avant
l'experience, 1 dans cette image. SAI1, DMA1 Stream3 et DMA1 Stream4 restent a
la priorite 2. Le rearm IDMA critique peut donc preempter AUDIO; l'invalidation
D-cache, la barriere et la publication suivent le rearm dans la meme IRQ.

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

Le resultat expose aussi les quatre premiers timestamps de publication, les
erreurs de generation/ordre/doublon, les starvations, la verification
bit-perfect des 32 chunks, ainsi que le minimum de lead en frames et octets.
La structure `g_sdmmc_async_progress_diag` expose `IDMABTC`, CMD18/CMD12,
somme/moyenne/max du rearmement seul, somme/moyenne/max du handler IDMABTC
complet, preemptions AUDIO observees et intervalle min/moyen/max entre deux
IDMABTC d'une meme page. Le deadline miss compare le rearm reel de l'IRQ
precedente a l'intervalle materiel mesure jusqu'a l'IRQ suivante. Son buffer `trace[8]`
capture les huit premieres IRQ au maximum de la premiere transaction
progressive: STA, MASK, bases, taille IDMA, DCOUNT, DLEN, chunk logiciel,
`IDMABACT`, buffer termine selon le materiel et buffer attendu.

L'arret historique vers 6538 cold starts n'etait pas une panne FatFs ou SD.
`error=7` est `STREAM_E2E_ERROR_IO`; `fail_step=11` est la validation des
timestamps dans `stream_e2e_record_batch`; `last_fresult=2` est `FR_INT_ERR` et
`last_block_result=4` est `SD_BLOCK_DEVICE_READ_FAIL`. Ces deux derniers champs
etaient des sentinelles ecrites par cette branche de validation, pas les
resultats du backend. La campagne atteint alors environ la periode de wrap du
compteur DWT 32 bits (8,947848533 s a 480 MHz). Une valeur CYCCNT nulle valide
etait confondue avec "timestamp absent". Les timestamps critiques ont maintenant
des drapeaux de validite explicites et les comparaisons d'ordre utilisent des
deltas modulo 32 bits. `error_snapshot` capture une seule fois la page, la
generation, l'epoch, l'etat/cache watermark, les registres SDMMC, les compteurs,
le block device, le backend et le scheduler avant le nettoyage d'erreur.
La transaction qui precedait l'erreur avait donc deja franchi backend complete,
`finish_loading`, READY et le rendu AUDIO; la premiere etape en defaut etait la
validation benchmark. L'ecart historique `6576 CMD18` contre `6575 pages_ready`
ne designait pas cette page: le diagnostic transport n'etait pas remis a zero
au demarrage du benchmark et incluait une transaction anterieure. Il est
desormais reinitialise dans `stream_end_to_end_bench_init`.

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
p g_sdmmc_async_progress_diag.configured_idmabndt
p g_sdmmc_async_progress_diag.configured_chunk_bytes
p g_sdmmc_async_progress_diag.expected_idma_buffers
p g_sdmmc_async_progress_diag.idmabtc_irq_count
p g_sdmmc_async_progress_diag.average_rearm_cycles
p g_sdmmc_async_progress_diag.average_rearm_us
p g_sdmmc_async_progress_diag.max_rearm_cycles
p g_sdmmc_async_progress_diag.max_rearm_us
p g_sdmmc_async_progress_diag.average_idmabtc_handler_cycles
p g_sdmmc_async_progress_diag.average_idmabtc_handler_us
p g_sdmmc_async_progress_diag.max_idmabtc_handler_cycles
p g_sdmmc_async_progress_diag.max_idmabtc_handler_us
p g_sdmmc_async_progress_diag.idmabtc_interval_cycles_min
p g_sdmmc_async_progress_diag.idmabtc_interval_cycles_avg
p g_sdmmc_async_progress_diag.idmabtc_interval_cycles_max
p g_sdmmc_async_progress_diag.trace
p g_stream_end_to_end_bench.error_snapshot
p g_stream_end_to_end_bench.trigger_to_first_chunk_available
p g_stream_end_to_end_bench.first_chunk_available_to_audio_seen
p g_stream_end_to_end_bench.trigger_to_first_render
p g_stream_end_to_end_bench.trigger_to_full_page_ready
p g_stream_end_to_end_bench.chunk_data_mismatch_count
p g_stream_end_to_end_bench.chunk_publish_count
p g_stream_end_to_end_bench.duplicate_chunk_publish_count
p g_stream_end_to_end_bench.out_of_order_chunk_publish_count
p g_stream_end_to_end_bench.generation_mismatch_count
p g_stream_end_to_end_bench.starvation_count
p g_stream_end_to_end_bench.minimum_lead_frames
p g_stream_end_to_end_bench.minimum_lead_bytes
p g_stream_end_to_end_bench.first_render_not_before_ready_count
```
