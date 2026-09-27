# Audit performance ciblé — conversion WAV

Date : 2026-09-27  
Périmètre : audit statique du firmware Release actuel, sans modification fonctionnelle ni instrumentation embarquée.

## Verdict

Il reste des gains importants possibles. Le chemin 48 kHz est déjà correctement bloqué (lecture 32 KiB, écriture 63 KiB), mais la conversion est dominée par les E/S synchrones et par l'expansion systématique vers FLOAT32 stéréo (384 kB/s). Le SRC est le seul cas manifestement CPU-bound : il décode et interpole frame par frame avec une phase `double`.

Le débit réel et la décomposition en millisecondes ne sont pas observables dans le code actuel : aucun compteur de temps/appels n'existe et aucune trace de banc n'est présente. Toute valeur absolue sans exécution sur carte et carte SD cible serait inventée. L'instrumentation demandée est donc la première passe recommandée.

## Pipeline réel

| Phase | Travail actuel | Appels FatFs |
|---|---|---|
| OPEN / PARSE | ouvre source, parse RIFF par petits reads, `f_stat` backup, ouvre temp, seek data | 2 opens, reads 12/8/`fmt`, plusieurs seeks possibles, 1 stat, 1 seek |
| HEADER | construit et écrit l'en-tête canonique de 512 octets | 1 write de 512 B |
| READ | source par blocs jusqu'à 32 KiB ; FLOAT32 direct jusqu'à 63 KiB | 1 ou 2 reads par service selon format |
| DECODE | PCM16/24/32 vers float stéréo ; mono dupliqué | boucles CPU |
| SRC | fenêtre de 2 frames, interpolation linéaire frame par frame | reads de 32 KiB au fil du flux |
| REPACK | destination est déjà le buffer FLOAT32 ; pas d'encodage PCM24 | aucune copie supplémentaire après decode |
| WRITE | un bloc FLOAT32 stéréo, maximum 64 512 B | 1 write par service |
| SYNC / CLOSE | `f_sync(dst)`, puis `f_close(dst)` qui resynchronise via FatFs, puis close source | au moins 1 sync explicite + sync interne du close |
| VERIFY | reopen temp, read des 512 premiers octets, taille via `f_size`, close | 1 open, 1 read 512 B, 1 close |
| REPLACE | source→backup, temp→source, unlink backup | 2 renames + 1 unlink |
| CATALOG | hors convertisseur ; refresh/reparse selon appelant | variable |

La vérification ne relit pas le fichier entier et ne cherche pas jusqu'à sa fin. Son coût est fixe. Le parse initial et la finalisation sont négligeables sur un long fichier, mais peuvent dominer une rafale de très petits samples.

## Taille et nombre des E/S

Le quantum demandé par tous les appelants identifiés est 65 536 B. Le buffer destination vaut toutefois `512 * 126 = 64 512 B`, donc un service COPY produit au maximum 8 064 frames / 64 512 B, soit 168 ms d'audio à 48 kHz. Le buffer source vaut 32 768 B.

Pour environ 1 MiB de source à 48 kHz, hors dernier bloc et parse :

| Source | Mode | Source/service | `f_read`/service | services et `f_write`/MiB source | volume destination/source |
|---|---|---:|---:|---:|---:|
| PCM16 mono | decode bloc | 16 128 B | 1 | ~65 | 4,00× |
| PCM16 stéréo | decode bloc | 32 256 B | 1 | ~33 | 2,00× |
| PCM24 stéréo | decode bloc spécialisé | 48 384 B | 2 (32 766 + 15 618 B) | ~22 | 1,33× |
| PCM32 stéréo | decode bloc | 64 512 B | 2 (32 768 + 31 744 B) | ~17 | 1,00× |
| FLOAT32 stéréo | copie brute | 64 512 B | 1 | ~17 | 1,00× |
| SRC | decode + interpolation frame | refill 32 KiB | dépend du ratio | dépend du ratio de fréquences | `384000 / débit_octets_source` |

Les gros transferts destination sont alignés sur 512 B et le buffer est aligné sur 32 B. Les reads PCM24 ne sont pas sector-aligned malgré l'assertion sur la taille du buffer ; FatFs doit traiter les bords de secteurs. C'est un gain secondaire, pas le bottleneck principal.

## CPU, SD et mémoire

- FLOAT32 stéréo 48 kHz est une vraie copie directe source→destination, sans decode.
- Tous les PCM 48 kHz font nécessairement PCM→FLOAT32 parce que le format canonique est FLOAT32. Il n'existe donc pas de chemin `decode float → encode PCM24` inutile dans cette version.
- PCM24 stéréo dispose d'une boucle déroulée par deux frames et forcée O3. PCM16/PCM32 restent dans la boucle générique avec tests de format/largeur dans chaque itération ; des boucles spécialisées peuvent apporter environ 10–25 % sur la seule phase decode, mais probablement moins de 10 % end-to-end si la SD domine.
- Le SRC appelle decode et gestion de fenêtre par frame, calcule l'index depuis un `double`, interpole deux canaux puis incrémente une phase `double`. C'est la meilleure cible CPU ; une phase fixe 32.32 et des boucles spécialisées par format ont une chance réaliste de donner 1,5× à 2× sur la phase SRC.
- `g_storage_shared_io` et tout `g_wav_convert` (dont `stream.io_buf`) sont contigus en SDRAM cacheable, alignés 32 B (`0xC0000000` et `0xC000FC00` dans l'ELF). Il n'y a pas d'aller-retour SRAM↔SDRAM dans le convertisseur.
- Les transferts SDMMC DMA font les invalidate/clean requis. Les deux buffers du convertisseur prennent le chemin DMA direct aligné, sans bounce buffer. Les maintenances de cache avant/après read sont réelles mais nécessaires avec la MPU actuelle ; les supprimer serait incorrect.
- Le gate SD est conservé pendant toute la conversion : pas de contention concurrente avec recorder/streamer. En revanche les `f_read`/`f_write` sont bloquants et occupent la superloop pendant le DMA ; l'audio IRQ continue, mais USB/UI et les services coopératifs attendent.

## Compiler

Les commandes Release réelles de `build/Release/compile_commands.json` se terminent par `-O2` pour `wav_convert.c`, `wav_audio_stream.c` et `wav_audio_codec.c` (après les options globales `-Os -O1`). Elles utilisent Cortex-M7, FPU hard, `-ffast-math` et `-fsingle-precision-constant`. Le PCM24 porte en plus `optimize("O3"), hot`.

Ces trois unités n'ont pas `-flto`. Le lien global a LTO, mais cela ne permet pas l'inlining inter-TU de ces objets non compilés LTO. Passer le trio à O3+LTO est une passe courte à mesurer, surtout pour SRC ; gain end-to-end attendu faible à modéré (0–15 %), pas un facteur 2.

## Scheduler et quanta

Le chemin Settings appelle le service depuis l'UI : tick moteur 1 500 Hz divisé par 4, donc cadence nominale 375 appels/s. Project load et multi-import sont servis directement depuis la superloop. Avec 64 512 B/appel, le plafond du seul espacement UI est 24,19 MB/s de sortie ; il est supérieur au débit pratique attendu d'une conversion lecture+criture sur la même SD. Le quantum 64 KiB actuel n'est donc pas trop petit.

Plafonds de sortie dus uniquement au scheduler Settings :

| Quantum utile | Sortie/appel | Plafond à 375 appels/s | Appels/MiB sortie |
|---:|---:|---:|---:|
| 4 KiB | 4 096 B | 1,54 MB/s | 256 |
| 8 KiB | 8 192 B | 3,07 MB/s | 128 |
| 16 KiB | 16 384 B | 6,14 MB/s | 64 |
| 32 KiB | 32 768 B | 12,29 MB/s | 32 |
| actuel | 64 512 B | 24,19 MB/s | 16,25 |

Réduire à 4–16 KiB ralentirait vraisemblablement le cas Settings et augmenterait le nombre d'appels FatFs. Augmenter au-delà de 64 KiB exige de plus gros buffers SDRAM et augmente encore la latence bloquante USB/UI sans preuve d'un gain SD. Le temps moyen réel entre services doit néanmoins être instrumenté : les E/S bloquantes peuvent créer du retard et le catch-up UI est borné à 8 ticks.

## Débits par cas : bornes honnêtes

Soient `R` le débit séquentiel lu et `W` le débit séquentiel écrit de la carte dans ce firmware, en MB/s. Hors CPU, le débit en octets source est borné par `1 / (1/R + E/W)`, avec `E` le ratio destination/source ci-dessus. Le plafond scheduler Settings s'ajoute : `24,19/E` MB/s source.

| Cas | Limite E/S source | Plafond scheduler source | Classement attendu |
|---|---:|---:|---|
| PCM16 mono 48 kHz | `1/(1/R+4/W)` | 6,05 MB/s | write-bound |
| PCM16 stéréo 48 kHz | `1/(1/R+2/W)` | 12,10 MB/s | write-bound |
| PCM24 stéréo 48 kHz | `1/(1/R+1,333/W)` | 18,14 MB/s | I/O, puis decode |
| PCM32 stéréo 48 kHz | `1/(1/R+1/W)` | 24,19 MB/s | I/O, puis decode |
| FLOAT32 stéréo 48 kHz | `1/(1/R+1/W)` | 24,19 MB/s | purement I/O-bound |
| SRC vers 48 kHz | même formule avec ratio de bitrates | selon ratio | CPU/SRC probable |

Le "débit actuel mesuré" reste indéterminé sans banc. Le débit FLOAT32 direct mesuré fournira la limite raisonnable du pipeline actuel et permettra de séparer SD de CPU pour les cinq autres cas.

## Classement des gains

| Optimisation | Cause actuelle | Gain estimé | Risque | Complexité |
|---|---|---:|---|---|
| Instrumentation DWT + compteurs FatFs par phase | aucune mesure absolue ni attribution I/O/CPU/scheduler | décisionnel, 0 % produit | faible si compile-time | faible |
| Préallocation contiguë du temp (`f_expand`) | allocation de clusters et mises à jour FAT pendant chaque gros write | +10–50 %, parfois >1,5× sur carte fragmentée | moyen : espace/compatibilité FatFs | faible-moyenne |
| SRC phase fixe + decode/SRC en blocs spécialisés | boucle frame par frame, double, appels et branches | 1,5×–2× phase SRC ; +25–100 % end-to-end si CPU-bound | moyen : exactitude aux bords | moyenne |
| Pipeline/double-buffer SD asynchrone | lecture, CPU et écriture strictement sériels et bloquants | +25–100 % si SD/CPU se recouvrent | élevé : SD ownership/USB/UI | élevée |
| Boucles PCM16/PCM32 spécialisées O3/LTO | branches et dispatch par frame | +10–25 % decode, 0–10 % total | faible | faible |
| Supprimer le `f_sync` redondant ou éviter le sync du close | sync explicite suivi du sync interne `f_close` | 0‒ sync-latences, important sur petits fichiers | moyen : durabilité | faible |
| Reuse du handle destination pour verify | close/reopen/read/close | surtout latence fixe ; <2 % longs fichiers | moyen : preuve de durabilité | faible |
| Reads PCM24 sector-friendly | 32 766 B crée des bords de secteurs | probablement <10 % | faible | faible |
| Quantum >64 KiB | plafond actuel déjà 24,19 MB/s | probablement 0–10 % | UI/USB bloqués plus longtemps | moyenne |

## Trois premières passes proposées

1. Ajouter derrière un flag une structure de stats de fin (`DWT->CYCCNT` + temps par phase, octets/appels/min/max de read/write, sync/seek/open/close, service calls, intervalle moyen/max entre services), puis exécuter les six fichiers sur une carte vide et une carte fragmentée.
2. Si FLOAT32 direct est lent ou irrégulier, tester la préallocation contiguë avant toute optimisation CPU ; comparer aussi le coût des deux syncs et de la finalisation.
3. Si le SRC est CPU-bound, remplacer uniquement son accumulateur `double` et sa boucle générique par un noyau bloc spécialisé, avec comparaison bit/écart numérique et mesures avant/après.

Gain cumulé plausible : +10–50 % sur les cas 48 kHz si la fragmentation/allocation FAT est responsable ; 1,5×–2× sur les cas SRC si le profil confirme la domination CPU. Un facteur 2 sur FLOAT32 direct n'est plausible qu'avec une carte/FS actuellement très défavorable ou avec une refonte asynchrone, pas avec une micro-optimisation Cortex-M7.

## Validation

Build Release terminé avec succès : `cmake --build build/Release --target BRICK6_CUBE.elf -j 4`.

