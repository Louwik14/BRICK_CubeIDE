# Campagne materielle finale du streamer

## Firmware de mesure

Configurer un build Release sans `-g`, avec le benchmark I/O conserve :

```powershell
cmake -S . -B build/ReleaseStreamBench -DCMAKE_BUILD_TYPE=Release `
  -DBRICK6_VARIANT=lowcost -DCMAKE_C_FLAGS=-DBRICK6_STREAM_BENCH=1
cmake --build build/ReleaseStreamBench --target BRICK6_CUBE.elf -j 4
```

Le symbole de benchmark est `g_sample_stream_benchmark`. Reinitialiser la
campagne avant chaque scenario, ou redemarrer la cible. Tester les tailles de
lecture 4, 8, 16 et 32 Kio avec `sample_stream_io_set_read_chunk_kib()`.

## Trace causale et admission

Le firmware Release expose `g_sample_stream_event_trace` sans macro de trace ou
d'audit. Le snapshot contient 128 evenements fixes et leur ABI stable. Extraire
le symbole et sa taille avec :

```powershell
arm-none-eabi-nm -S --size-sort build/ReleaseStreamBench/BRICK6_CUBE.elf |
  Select-String g_sample_stream_event_trace
```

```gdb
p/x &g_sample_stream_event_trace
p sizeof(g_sample_stream_event_trace)
dump binary memory stream_event_trace.bin &g_sample_stream_event_trace ((char *)&g_sample_stream_event_trace)+sizeof(g_sample_stream_event_trace)
```

Pour une surcharge volontaire, saturer progressivement les voix, les ratios et
les fichiers distincts. Verifier que l'admission publie les acceptations et
refuse proprement la demande depassant voix, debit ou latence ; le refus doit
preceder toute nouvelle allocation ou requete SD. Les valeurs d'admission
restent conservatrices jusqu'a recalibration par ces mesures.

Pour un miss, relever `last_miss_sequence`, puis suivre `cause_sequence` depuis
`CONSUME_MISS` vers `PAGE_READY` ou `IO_ERROR`, `IO_BEGIN`, `PAGE_SELECTED` et
`SERVICE_BEGIN`. Aucun classement EDF, rang, starvation guard ou repair n'est
necessaire pour expliquer le phenomene. `SERVICE_BLOCKED` identifie les polls
refuses par le gate SD. Les cycles DWT servent aux mesures de latence ; les
deadlines sont comparees sur la base des frames audio.

## Matrice obligatoire

Pour chacune des tailles 4/8/16/32 Kio, mesurer mono puis stereo, Classic puis
Multi, loop puis non-loop, un fichier partage puis des fichiers distincts,
1/2/4/8 voix simultanees, ratios 1.0/1.5/2.0 puis le ratio produit maximal.
Chaque scenario commence a froid, dure au moins 60 secondes et est repete
trois fois. Ajouter un wrap simultane et deux instruments Multi distincts.

## Criteres et decision DMA

Une configuration est admissible si aucune deadline n'est manquee, aucun
underrun n'apparait, le backlog revient a zero apres le pic froid et le p99
comme le maximum restent sous l'horizon avec au moins 25 % de marge. Choisir la
plus petite taille qui satisfait ces criteres dans tous les scenarios.

Le DMA SD asynchrone devient necessaire seulement si, apres selection de la
meilleure taille et validation de la cadence, le temps bloque synchrone maximal
ou p99 consomme la marge de deadline d'un scenario pourtant admis. Une
insuffisance de debit soutenu doit d'abord corriger l'admission.
