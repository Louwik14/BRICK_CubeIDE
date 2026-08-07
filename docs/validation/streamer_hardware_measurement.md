# Campagne matérielle finale du streamer

## Firmware de mesure

Configurer un build Release sans `-g`, avec :

```powershell
cmake -S . -B build/ReleaseStreamBench -DCMAKE_BUILD_TYPE=Release `
  -DBRICK6_VARIANT=lowcost -DCMAKE_C_FLAGS=-DBRICK6_STREAM_BENCH=1
cmake --build build/ReleaseStreamBench --target BRICK6_CUBE.elf -j 4
```

Le symbole stable est `g_sample_stream_benchmark`. Réinitialiser la campagne
avant chaque scénario avec `call sample_stream_benchmark_reset()`, ou redémarrer
la cible. La taille se choisit avant le test avec :

```gdb
call sample_stream_io_set_read_chunk_kib(4)
call sample_stream_io_set_read_chunk_kib(8)
call sample_stream_io_set_read_chunk_kib(16)
call sample_stream_io_set_read_chunk_kib(32)
```

Retrouver et extraire le snapshot sans DWARF :

```powershell
arm-none-eabi-nm -S --size-sort build/ReleaseStreamBench/BRICK6_CUBE.elf | Select-String g_sample_stream_benchmark
```

```gdb
x/100wx 0xADRESSE_NM
dump binary memory stream_bench.bin 0xADRESSE_NM 0xADRESSE_NM+0x190
```

L'ABI 1 occupe exactement `0x190` octets (400 octets).

## Capture causale d'un underrun

Construire ponctuellement avec l'audit causal, nul par défaut et exclu du
firmware produit :

```powershell
cmake -G Ninja -S . -B build/ReleaseStreamAudit `
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake `
  -DCMAKE_BUILD_TYPE=Release -DBRICK6_VARIANT=lowcost `
  "-DCMAKE_C_FLAGS=-DBRICK6_STREAM_BENCH=1 -DBRICK6_STREAM_TRACE=1 -DBRICK6_STREAM_AUDIT=1"
cmake --build build/ReleaseStreamAudit --target BRICK6_CUBE.elf -j 4
```

Flasher, sélectionner 16 Kio, redémarrer et reproduire le cas 8 voix Multi. Le
premier `LATE_SELECTION` ou `CONSUME_MISS` gèle `g_sample_stream_trace`. Arrêter
alors la cible et extraire le snapshot complet :

```gdb
p/x &g_sample_stream_trace
p sizeof(g_sample_stream_trace)
p g_sample_stream_trace.trigger
p g_sample_stream_trace.trigger_key
p g_sample_stream_trace.trigger_page
dump binary memory stream_audit.bin &g_sample_stream_trace ((char *)&g_sample_stream_trace)+sizeof(g_sample_stream_trace)
```

Dans `operations`, retrouver le `key/page_index` du trigger.
`created_audio_frame`, `consume_deadline_audio_frame` et `selected_audio_frame`
donnent l'anticipation initiale, la deadline absolue et l'attente. Lire son
anneau `audit_history` de `audit_history_write-audit_history_count` à
`audit_history_write-1`, modulo 64 : chaque entrée donne rang EDF, requêtes
devant elle, backlog, deadlines dépassées et frames restantes.

Lire de même `audit_services` avec `audit_service_write/audit_service_count` :
intervalle entre passages, arrivées, pages sorties et motif exact de sortie
(`sample_stream_audit_exit_t`). Convertir les cycles avec `SystemCoreClock` et
les frames à 48 kHz. `other_sd_cycles_since_previous` et
`multi_bulk_cycles_since_previous` mesurent l'occupation réelle du gate SD;
`audit_blocked_*` mesure les polls refusés et leur durée audio. Comparer
`arrivals_since_previous` à `pages_selected` pour détecter un backlog croissant.

Répéter trois fois, puis faire un run témoin sans `BRICK6_STREAM_AUDIT` pour
vérifier que le classement diagnostic n'a pas déplacé le phénomène.

## Matrice obligatoire

Pour chacune des tailles 4/8/16/32 Kio, mesurer au moins : mono puis stéréo,
Classic puis Multi, loop puis non-loop, un fichier partagé puis fichiers tous
distincts, 1/2/4/8 voix simultanées, ratios 1.0/1.5/2.0 puis le ratio produit
maximal. Chaque scénario commence à froid, dure au moins 60 secondes et est
répété trois fois. Ajouter un scénario de wrap simultané et un scénario avec deux
instruments Multi distincts.

## Exploitation

Les moyennes sont les totaux divisés par `selected_pages` ou `service_calls`.
Le débit utile est `source_bytes` divisé par la durée audio depuis
`start_audio_frame`; le débit SD inclut le read-ahead via `read_bytes`. Le p99 se
déduit en cumulant l'histogramme jusqu'à 99 % des pages ; le bucket `n` représente
les valeurs de `2^n` à `2^(n+1)-1`. Relever également maxima, temps total de
service, backlog, deadlines manquées, changements de source, ouvertures, seeks,
lectures, hits de read-ahead, erreurs et polls bloqués.

## Critères et décision DMA

Une configuration n'est admissible que si aucune deadline n'est manquée, aucun
underrun n'apparaît, le backlog revient à zéro après le pic froid et le p99 comme
le maximum restent sous l'horizon avec une marge d'au moins 25 %. Choisir la plus
petite taille qui satisfait ces critères dans tous les scénarios, pas celle qui
maximise seulement le débit moyen.

Le DMA SD asynchrone devient nécessaire exactement si, après sélection de la
meilleure taille et validation de la cadence dédiée, le temps bloqué synchrone
maximal ou p99 consomme la marge de deadline dans un scénario pourtant admis.
Une insuffisance de débit soutenu doit d'abord corriger l'admission ; elle ne
justifie pas à elle seule le DMA.
