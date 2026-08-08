# Banc automatique de calibration Stream

Ce firmware temporaire est activé uniquement par
`BRICK6_STREAM_CALIBRATION=1`. Il remplace le workflow utilisateur par une
machine d'états autonome, conserve le scheduler round-robin strict et ne change
à runtime que le nombre de passages du tour et la profondeur mobile. La taille
physique de page est choisie au build.

## Grille

Chaque image exécute 15 cas dans cet ordre :

```text
N1/A2 N1/A3 N1/A4 N1/A5 N1/A6
N2/A2 N2/A3 N2/A4 N2/A5 N2/A6
N3/A2 N3/A3 N3/A4 N3/A5 N3/A6
```

Les images 16 Kio et 32 Kio donnent donc 30 cas. Chaque cas arrête les huit
tracks, réinitialise le cache et le streamer, puis confie la préparation de
`/voix1.wav` à `/voix8.wav` au workflow commun `sample_pool_prepare_batch`.
Celui-ci séquence le loader produit et attend que son présocle soit jouable,
sans validation WAV ni timeout propres au banc. Le cas configure ensuite huit
tracks Stream forward sans loop, puis déclenche les huit voix dans la même
itération superloop avec la note MIDI 127 (maximum clavier après octave +4).
La fenêtre mesurée dure 96000 frames, suivie de 4800 frames de séparation.

## Builds Release Low-Cost

```powershell
cmake -S . -B build/Calibration16 -G Ninja "-DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake" "-DCMAKE_BUILD_TYPE=Release" "-DBRICK6_VARIANT=lowcost" "-DBRICK6_STREAM_CALIBRATION=ON" "-DBRICK6_STREAM_CALIBRATION_PAGE_KIB=16"
cmake --build build/Calibration16 --target BRICK6_CUBE.elf -j4

cmake -S . -B build/Calibration32 -G Ninja "-DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake" "-DCMAKE_BUILD_TYPE=Release" "-DBRICK6_VARIANT=lowcost" "-DBRICK6_STREAM_CALIBRATION=ON" "-DBRICK6_STREAM_CALIBRATION_PAGE_KIB=32"
cmake --build build/Calibration32 --target BRICK6_CUBE.elf -j4
```

Exécuter une image puis l'autre avec la même SD. La seconde image conserve les
15 résultats de l'autre taille et remplace ceux de sa propre taille. Aucun
fichier n'est écrit pendant les cas ; les deux fichiers de résultat sont créés
seulement après le dernier cas. Une campagne prend environ 35 à 60 secondes par
image selon la carte SD, soit environ 70 à 120 secondes au total.

## Écran

Pendant la campagne : titre `STREAM TEST`, barre de progression, `CASE x / 15`
et configuration `16K|32K / Nx / Ay`. À la fin : `COMPLETE`, compteurs PASS et
FAIL. Seule une erreur terminale du workflow produit bloque la campagne sur
`STREAM TEST ERROR`, avec le fichier et la cause.

## Format binaire

`/stream_calibration.bin` est little-endian, ABI 1. L'en-tête fait 32 octets :

```text
u32 magic = 0x5343414C
u16 abi_version
u16 header_size
u16 record_size = 160
u16 result_count (15 ou 30)
u32 firmware_page_kib
u32 sample_rate = 48000
u32 case_duration_frames = 96000
u32 grid_signature = 0x01020306
u32 payload_crc32 (CRC-32 IEEE des records)
```

Chaque record `brick6_stream_calibration_result_t` contient, dans l'ordre défini
par `Inc/Core/stream_calibration.h`, page/N/avance/verdict, premier défaut,
underruns, marge minimale globale et par voix, intervalle maximal de service,
cycles moyen/max des tours, cycles moyen/p99 supérieur/max des lectures,
octets source/lus, cycles lecture/service, durée, pages, lectures physiques,
opérations FatFs, lectures contiguës/FatFs, seeks, opens, erreurs, pages/s Q16,
octets/s et dépassements IRQ audio.

Le CSV contient une ligne par record avec les mêmes identifiants et les mesures
principales. Les colonnes exactes sont écrites en première ligne par le firmware.

PASS exige zéro underrun, zéro erreur I/O, zéro dépassement IRQ, huit voix
effectivement actives et au moins une page mesurée. Parmi les PASS, la
configuration produit candidate est celle de coût minimal (page, N, avance)
qui conserve une marge minimale strictement positive et stable ; la meilleure
marge est le maximum de `minimum_margin_frames`. Une marge nulle est un échec de
robustesse même si aucun miss n'a été observé sur cette passe unique.
