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
-> LOADING->READY dans le Page Cache -> prochaine IRQ AUDIO
-> reader resolve -> noyau de rendu d'un frame valide
```

Le trigger commun est le `DWT->CYCCNT` pris dans l'IRQ AUDIO juste avant les N
bindings. `PAGE READY` est pris immediatement apres la transition validee
`LOADING -> READY` effectuee par `sample_page_cache_finish_loading`.
`FIRST SAMPLE RENDERED` est pris apres que
`sample_voice_reader_render_fwd_1x_ready_simple` a effectivement lu et melange
un frame de la page. READY et resolve ne sont donc pas assimiles au rendu.

Le binding differe ne duplique aucune logique Streamer : il initialise le meme
VoiceReader et publie par `sample_voice_reader_publish_lease`; seule la tentative
READY synchrone du binding produit normal est differee pour autoriser le cold
start mesure.

## Workload et cache

`STREAM_E2E_BENCH_PAGE_BYTES` est strictement egal a `SAMPLE_PAGE_BYTES` (64 KiB
dans cette image). Le fichier brut FLOAT32 stereo fait 512 MiB, ou 256 MiB si
l'espace libre impose le repli. Une permutation xorshift deterministe couvre
toutes ses pages, alignees sur la page produit. La selection ecarte les rares
pages qui croisent une frontiere d'extent FAT : chaque sample conserve ainsi la
semantique historique d'une transaction physique de 64 KiB, tout en laissant
le mapping normal refaire et mesurer sa propre resolution apres le trigger.

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

La structure unique est `g_stream_end_to_end_bench`. Toutes les durees gardent
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

La deadline exposee est 64 frames, soit 1333 us a 48 kHz. Les compteurs voix et
batch indiquent le respect ou le depassement d'un bloc sans quantifier les
latences elles-memes a la frontiere de bloc.

Dump GDB :

```gdb
shell cls
set pagination off
set print pretty on
info address g_stream_end_to_end_bench
p g_stream_end_to_end_bench
```
