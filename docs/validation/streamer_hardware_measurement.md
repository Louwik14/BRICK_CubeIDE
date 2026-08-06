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
