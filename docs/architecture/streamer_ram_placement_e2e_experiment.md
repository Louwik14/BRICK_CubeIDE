# STREAM E2E — expérience de placement RAM maximal

Image expérimentale uniquement. Les chiffres viennent de `build/Release/BRICK6_CUBE.map`
et de la table des symboles ELF, avant et après le changement. Aucun algorithme,
format, quota, cadence SD ou paramètre du benchmark n'est modifié.

## Occupation globale

| Région | Capacité | Avant utilisé / libre | Après utilisé / libre |
|---|---:|---:|---:|
| DTCM | 131 072 | 130 848 / 224 | 74 112 / 56 960 |
| D1 | 524 288 | 514 336 / 9 952 | 500 928 / 23 360 |
| D2 total | 294 912 | 267 552 / 27 360 | 263 424 / 31 488 |
| D3 | 65 536 | 34 464 / 31 072 | 32 352 / 33 184 |
| SDRAM cacheable | 33 161 216 | 32 698 976 / 462 240 | 32 775 232 / 385 984 |

Le pool audio reste strictement inchangé : `g_sample_page_data`, 24 641 536 octets,
adresse `0xC03CCB00`, section `.sdram_sample_page_pool`.

## Metadata Streamer après placement

| Ensemble | Octets | Région |
|---|---:|---|
| VoiceReaders/voix, leases, cache hot, cursors et petits états | 17 633 | DTCM |
| descriptors + index + registry + jobs I/O + extent pool | 354 176 | D1 |
| CLMT scratch + decode scratch | 4 104 | SDRAM cacheable |
| instrumentation E2E (`result` + `runtime`) | 22 824 | SDRAM cacheable |

Le cœur de metadata recensé en DTCM+D1 représente donc **371 809 octets**.
Les projections et catalogues de publication non nécessaires au trajet E2E restent
dans leurs arènes SDRAM existantes.

## Objets Streamer déplacés

Les adresses « avant » proviennent de l'ELF de `602e1d8d5`; les adresses « après »
de l'ELF lié par ce commit.

| Objet | Taille | Avant | Après | AUDIO | STORAGE | DMA | Motif |
|---|---:|---|---|:---:|:---:|:---:|---|
| `g_sample_page_sample_desc` | 262 480 | `0xC1B4CB00`, SDRAM | `0x24027960`, D1 | oui | oui | non | registry très parcouru |
| `g_sample_stream_physical_pool` | 53 248 | `0xC1B932C0`, SDRAM | `0x24068700`, D1 | non | oui | non | extents/mapping hot |
| `g_sample_stream_io_async` | 848 | `0x30003060`, D2 | `0x24067AB0`, D1 | non | oui/IRQ | non | jobs CPU/IRQ, pas payload DMA |
| `g_sample_page_leases` | 1 656 | `0x300033C0`, D2 | `0x20000064`, DTCM | oui | oui | non | publication/résolution hot |
| `g_sample_page_lease_active_mask` | 8 | `0x30003A60`, D2 | `0x20003330`, DTCM | oui | oui | non | scan hot |
| `g_sample_page_last_slot` | 1 544 | `0x30003A80`, D2 | `0x2000333C`, DTCM | oui | oui | non | lookup direct hot |
| `g_sample_page_reserved_count` | 1 544 | `0x38002854`, D3 | `0x20003944`, DTCM | non | oui | non | reserve/recycle hot |
| cache state + cursors + FREE/READY bitmaps | 36 | D3 | `0x20000000`, `0x20003338`, `0x20003F50`, DTCM | oui | oui | non | petit working set permanent |
| physical pending/generations | 16 | D1 | `0x20000034` et `0x2000332C`, DTCM | non | oui/IRQ | non | état CPU/IRQ hot |
| scheduler cursors/passes | 7 | D1 | `0x20003F60`, DTCM | non | oui | non | choix de job hot |
| manager initialized + I/O order + payload contract | 6 | D1 | `0x20003F67`, `0x20003328`, `0x20000010`, DTCM | oui | oui | non | petits états hot |
| `g_sampler_multi_stream_release_pending` | 512 | D3 | `0x20005FA0`, DTCM | oui | oui | non | publication de release |
| `g_sampler_preview` | 1 128 | `0x2404A1C0`, D1 | `0x20003F80`, DTCM | oui | non | non | VoiceReader embarqué |
| `g_audio_rec_overdub_reader` | 232 | `0x24037B94`, D1 | `0x2000E344`, DTCM | oui | oui | non | VoiceReader embarqué |

Déjà correctement placés et conservés :

| Objet | Taille | Adresse après | Région |
|---|---:|---|---|
| `g_sampler_voice` | 7 296 | `0x200016A8` | DTCM |
| `g_sampler_multi_voice` | 3 648 | `0x200006E0` | DTCM |
| `g_sample_page_descriptor` | 22 560 | `0x24022140` | D1 |
| `g_sample_page_index` | 15 040 | `0x24075700` | D1 |
| `g_sample_stream_clmt_scratch` | 1 032 | `0xC1B7B160` | SDRAM cacheable |
| `g_sample_stream_io_rec_decode` | 3 072 | `0xC1B52560` | SDRAM cacheable |
| `g_stream_end_to_end_bench` | 2 832 | `0xC1B519A0` | SDRAM cacheable |

`g_stream_rec_perf` est conditionnel à `BRICK_PERF_DIAG` et absent de cet ELF Release;
son placement déclaré passe en D3 non-cacheable afin qu'une image diagnostic ne
consomme pas le DTCM du chemin produit.

## Objets hors benchmark expulsés

Tous restent fonctionnellement présents, mais leur stockage passe en SDRAM cacheable.

| Objet | Taille | Ancienne région | Nouvelle région |
|---|---:|---|---|
| FM voices | 15 040 | DTCM | SDRAM |
| Haas L/R | 19 216 | DTCM | SDRAM |
| Braids runtime/poly | 12 800 | DTCM | SDRAM |
| audio FX runtime | 6 144 | DTCM | SDRAM |
| TB303 runtime principal | 4 800 | DTCM | SDRAM |
| wavetable runtime/poly | 5 376 | DTCM | SDRAM |
| reverb engine buffer | 131 072 | D1 | SDRAM |
| vibe history | 65 536 | D1 | SDRAM |
| clip shifter delay D1 | 65 536 | D1 | SDRAM |
| pattern slot A | 29 568 | D1 | SDRAM |
| locks A (4 lanes) | 12 288 | D1 | SDRAM |
| wavetable pool I/O | 16 384 | D1 | SDRAM |
| global mod-FX history | 8 192 | D1 | SDRAM |

Les previews synth/FX et le traitement Sampler utilisé par le test ne sont pas
neutralisés. Cette image n'est cependant pas un placement produit : les performances
des moteurs et FX expulsés sont volontairement sacrifiées.

## DMA, cache et vérification

- Aucun objet placé en DTCM n'est passé à DMA/IDMA : ce sont exclusivement des états,
  pointeurs, compteurs, bitmaps, leases ou VoiceReaders CPU.
- Les pages audio DMA restent en SDRAM cacheable avec la maintenance D-cache existante.
- Le scratch `sd_diskio` de 4 096 octets reste en D1 (`0x24009AE0`), donc accessible
  par SDMMC IDMA. Les buffers SAI restent dans `.ram_d2_dma_audio` non-cacheable.
- Les états SDMMC, block-device et scheduler DMA/IRQ ne changent ni de région ni de
  cacheabilité.
- `.dtcm_audio`, `.ram_d1_audio`, `.sdram_audio_cold` et toutes les adresses ci-dessus
  ont été vérifiées dans le MAP et la table ELF finale; aucune région ne déborde.
- Build Release réussi. Le benchmark matériel n'a pas été lancé et ses paramètres
  restent 64 KiB, 25 MHz, un cold start simultané, 10 000 itérations et 36 pages de
  warm-up.

## Correction de boot après déplacement SDRAM

### Cause exacte

Les objets expulsés de DTCM/D1 ont été placés dans `.sdram_audio_cold`, une section
`NOLOAD`. Contrairement aux anciens emplacements `.bss`, cette section n'est pas
remise à zéro par `Reset_Handler` : le FMC n'est pas encore utilisable à ce stade.
Le contenu au démarrage était donc indéterminé. Plusieurs initialisations réécrivent
leur objet intégralement, mais ce n'est pas un contrat commun : `g_pattern_slot_a`
et son pool de locks, notamment, ne sont initialisés que partiellement, tandis que
les buffers de délai/réverbération reposent sur leur init propre. La passe avait
donc supprimé implicitement la garantie BSS de ces objets, ce qui pouvait injecter
des états, compteurs et pointeurs invalides au boot.

Il n'y a ni accès à ces objets avant FMC, ni alias/adresse codée en dur, ni DMA sur
ces objets. Le défaut est la sémantique d'initialisation de la section `NOLOAD`, pas
la cacheabilité ou l'accessibilité de la SDRAM.

### Correction

Le linker exporte maintenant `__sdram_audio_cold_start__` et
`__sdram_audio_cold_end__`. `SDRAM_Init()` efface cette arène immédiatement après
la séquence d'initialisation FMC, avant `control_domain_init()`,
`seq_engine_control_init()` et `audio_domain_init()`. Les objets retrouvent ainsi
exactement leur garantie historique de zéro initial sans être remis en SRAM interne.

Tous les objets hors Streamer listés plus haut restent compilés, initialisés et en
SDRAM. Aucun moteur, FX, chemin UI ou init n'est neutralisé.

### Marge SDRAM et cache pages

Le cache passe de 376 à 368 pages de 64 KiB, soit **524 288 octets libérés**. La
réserve VoiceReader reste inchangée à 36 pages; seul le pool général passe de 340
à 332 pages. Les paramètres du benchmark restent inchangés : page 64 KiB, SDCLK
25 MHz, un cold start simultané, 10 000 requêtes et warm-up 36 pages.

| Région | Utilisé | Libre |
|---|---:|---:|
| DTCM | 74 112 | 56 960 |
| D1 | 500 128 | 24 160 |
| D2 total | 263 424 | 31 488 |
| D3, occupation physique linker | 52 768 | 12 768 |
| SDRAM cacheable | 32 250 944 | 910 272 |

Le placement Streamer reste maximal : 17 633 octets en DTCM et 353 376 octets en
D1, soit **371 009 octets** de metadata interne. Le payload
`g_sample_page_data` reste en SDRAM à `0xC03CCB00`, taille 24 117 248 octets.

Le MAP/ELF Release final confirme l'arène froide `0xC1E4FA20..0xC1EC1C40`, le
registry en D1 à `0x24027780`, l'extent pool en D1 à `0x24068520`, et l'absence de
débordement. La validation matérielle du démarrage et de l'audio reste à effectuer
sur la BRICK; la chaîne statique vérifiée est FMC/SDRAM puis remise à zéro de l'arène,
puis initialisations CONTROL/SEQ/AUDIO.
