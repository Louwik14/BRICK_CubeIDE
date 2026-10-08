# Streamer / Recorder performance baseline (H743)

Format non-cacheable D3 `g_stream_rec_perf`: magic `0x46505242` (`BRPF` little endian), version 4, fixed size in the header. La fenetre IRQ-shared D3 evite les lectures GDB de lignes D-cache sales; appeler reset avant lecture car cette section est NOLOAD. The one object contains a clock frequency, CPU spans (`calls`, `max`, `total` cycles), wall spans with the same fields, and 64 bit counters. `BRICK_PERF_DIAG=1` est explicitement defini par le preset lorsque l'option CMake homonyme est active. The decoder is `tools/decode_stream_rec_perf.py`.

## Procedure

1. Stop playback and recording. Call `brick_perf_diag_reset()` from GDB. This enables DWT/CYCCNT, clears counters and captures `SystemCoreClock` and the HAL tick.
2. Run a defined workload. Stop playback and recording before reading the data.
3. Call `brick_perf_diag_snapshot()` to capture test elapsed milliseconds. Dump the entire object with `x/Nwx &g_stream_rec_perf`, where `N=sizeof(g_stream_rec_perf)/4` for this ELF.
4. Save the GDB text and run `python tools/decode_stream_rec_perf.py dump.txt`. The decoder checks magic, version, size and truncation. The address comes from the ELF, never from the decoder.

The snapshot writes only elapsed time; it does not freeze concurrent writers. Halt all activity for a coherent dump. A reset while active invalidates the measurement, but does not reset Streamer/Recorder state.

## Meaning

CPU spans bracket executed code only. They must not be interpreted as asynchronous I/O duration. DWT also counts interrupt cycles that happen inside a bracket. DMA launch can nest inside synchronous submission; the decoder excludes its span from the sampled Streamer sum. The Recorder `rec_service` span includes `rec_prepare` and `rec_pack`; do not sum those spans as disjoint work. The retained `rec_pack` slot now measures direct source binding and, only for the final partial sector, its bounded 512-byte copy/padding; its layout is unchanged for baseline compatibility. Recorder AUDIO keeps the existing total, conversion and peak-meter slots for diagnostic ABI stability; native FLOAT32 capture leaves the conversion slot and frame counter at zero, while the peak span reads the float bus directly. REC_SOURCE FLOAT32 pages use the normal direct path. Its scratch/copy and PCM24-to-FLOAT spans remain reserved for legacy PCM24 recordings only. The SD driver `stream_dma`/`rec_write_dma` wall spans run from successful DMA launch to completion IRQ; `stream_submit_to_dma` and `rec_submit_to_dma` include queue delay. Unsigned subtraction handles CYCCNT wrap for individual intervals under one wrap (~8.9 seconds at 480 MHz).

`pages_requested` compte les besoins issus des quatre slots reader qui ont gagne
une reservation cache et effectue la transition vers `LOADING`; les scans,
retries et prechargements statiques du pre-socle n'y figurent pas.
`pages_ready` et ses ventilations comptent uniquement une completion validee
`LOADING -> READY`, jamais une restauration `EVICTING -> READY` ni une
allocation RAM rendue READY sans lecture. `stream_request_to_ready` commence
apres cette prise en charge et finit apres publication de la page; il exclut le
temps anterieur de publication du lease. `stream_dma_to_io_finalize` finit avant
la publication cache et constitue donc une borne basse DMA-vers-READY. Le
nombre et le volume de reads proviennent des completions reussies du backend
physique Streamer, sur octets alignes secteurs. Les temps DMA du block driver
commun peuvent inclure d'autres clients actifs pendant le banc. Recorder write
count/bytes proviennent des completions de descripteurs data, hors metadata
filesystem; `rec_write_dma` peut inclure les ecritures filesystem.
`cache_ready_observations` et `cache_loading_observations` restent des
observations de scans manager, pas des pages uniques. `cache_miss` compte un
besoin reader observe en `FREE/FAILED`. `audio_page_missing` compte les echecs
d'acquisition primaire reader, pas tous les underruns. Les niveaux du ring sont
echantillonnes lors des pushes AUDIO reussis; near-full signifie moins d'un
huitieme de capacite libre.

The main baseline omits a unique per reader breakdown, exact AUDIO need-to-I/O delay, late page classification, and exact attribution of scheduler CPU outside the measured functions. Zero-valued spans indicate no probe for that stage or no occurrence. The decoder does not claim a total CPU percentage; nested spans and unmeasured paths prevent that inference. There is no allocation, logging, SD diagnostic write or event ring in this instrumentation.

La version 4 ajoute le span `protection_update_audio`, limite a la publication
du compteur de changement des leases, et les compteurs
`protection_update_storage`, `recyclable_candidates`, `reserve_searches`,
`reserve_candidates_tested`, `reserve_candidates_tested_max`,
`reserve_revalidation_fail` et `reserve_no_candidate`. La moyenne des
candidates soumises a la revalidation finale vaut
`reserve_candidates_tested / reserve_searches`; `recyclable_candidates`
accumule la population du bitmap filtre au debut de chaque recherche.
