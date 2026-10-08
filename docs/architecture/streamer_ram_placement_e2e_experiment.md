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

## Cause racine du boot pre-FMC

Le diagnostic `NOLOAD` ci-dessus etait incomplet : le clear intervient dans
`SDRAM_Init()`, donc apres `__libc_init_array`. Or `g_fm_voice` n'est pas un simple
tableau C : ses membres C++ declenchent un constructeur statique.

Dans l'ELF `d8a176c2a`, `.init_array` appelle `_sub_I_65535_0.0` avant `main()` et
avant `MX_FMC_Init()`. Le desassemblage montre que cette fonction charge
`0xC1E84B80`, adresse exacte de `g_fm_voice`, puis y effectue immediatement des
ecritures. La baseline placait le meme symbole en DTCM. Le premier acces reel a
cet objet etait donc un acces SDRAM alors que FMC n'etait pas initialise. Le clear
manuel, execute beaucoup plus tard, ne pouvait pas corriger ce defaut.

Le dernier jalon structurel atteint par l'ancienne image est `SystemInit`; `main`
n'est pas atteint. L'instruction fautive initiale est le `strb` a `0x080DC5E2` dans
`_sub_I_65535_0.0`, avec `r2 = 0xC1E84B80`. Sur Cortex-M7, cet acces FMC indisponible
declenche un BusFault; avant `main`, les faults configurables ne sont pas encore
actives, donc il est escalade en HardFault. Les anciens handlers demandaient aussitot
un reset systeme, ce qui explique la boucle de reset et rend les anciens CFSR/HFSR
irrecuperables apres coup.

`g_fm_voice` est le seul objet expulse reference par ce constructeur pre-main. Il
revient en DTCM. L'ELF corrige place le symbole a `0x20007500`; le constructeur
`_sub_I_65535_0.0` ne contient plus aucune cible SDRAM. Tous les autres objets
expulses restent en SDRAM : leur premier acces applicatif intervient apres
`SDRAM_Init()` et son clear.

| Groupe | Premier acces de boot | Classement |
|---|---|---|
| FM voices | constructeur `.init_array`, avant `main` | incompatible SDRAM avant FMC; remis DTCM |
| Braids, wavetable, TB303 | `brick6_audio_boot_apply_engines` | apres FMC/clear |
| audio FX runtime, vibe | `audio_boot_init_binding_io` | apres FMC/clear |
| Haas, reverb, mod-FX | init mixer/FX | apres FMC/clear |
| clip shifter | `brick6_sampler_runtime_init` | apres FMC/clear |
| pattern slot A / locks A | `seq_engine_control_init` | apres FMC/clear |
| wavetable pool I/O | `wavetable_pool_init` puis services storage | apres FMC/clear |

### Diagnostic materiel GDB

`g_boot_diag` est conserve dans la fenetre D3 non-cacheable (adresse Release
actuelle `0x3800CE80`). Les
handlers HardFault, MemManage, BusFault et UsageFault capturent desormais les
registres SCB et la frame empilee puis restent arretes, au lieu de demander un reset
immediat. `Error_Handler` capture egalement son adresse appelante.

```gdb
p/x g_boot_diag.stage
p/x g_boot_diag.fault_kind
p/x g_boot_diag.cfsr
p/x g_boot_diag.hfsr
p/x g_boot_diag.mmfar
p/x g_boot_diag.bfar
p/x g_boot_diag.shcsr
p/x g_boot_diag.stacked_pc
p/x g_boot_diag.stacked_lr
p/x g_boot_diag.stacked_xpsr
p/x g_boot_diag.msp
p/x g_boot_diag.psp
p/x g_boot_diag.exc_return
```

Les stages couvrent Reset, SystemInit, main, MPU/cache, HAL/clocks, FMC, peripheriques,
FatFs, sequence SDRAM, clear, CONTROL, SEQ, Sampler, AUDIO, UI, superloop, montage SD,
demarrage DMA audio et lancement benchmark.

### Occupation finale de l'image instrumentee

| Region | Utilise | Libre |
|---|---:|---:|
| DTCM | 89 152 | 41 920 |
| D1 | 500 128 | 24 160 |
| D2 total | 263 424 | 31 488 |
| D3, occupation physique linker | 52 896 | 12 640 |
| SDRAM cacheable | 32 235 904 | 925 312 |

Le cache reste a 368 pages, dont 36 pages de reserve VoiceReader. Le placement
Streamer est inchange; seul l'objet non-Streamer `g_fm_voice` (15 040 octets) revient
en memoire interne a cause de son constructeur pre-FMC. Aucune fonctionnalite ni
initialisation n'est supprimee.
## Suivi du blocage storage au boot (`8757c0c09`)

`g_boot_diag.stage == 21` is `BOOT_DIAG_STAGE_SUPERLOOP`.  The application
initialization and entry into `main()`'s superloop completed; stage 22
(`BOOT_DIAG_STAGE_SD_MOUNTED`) was never reached.  The observed
`last_pc_tag == 0x080098E9` is the return site in `main()` immediately after
marking stage 21.  The active boot loop is therefore
`brick6_app_process()` -> `groove_bank_boot_complete()` ->
`groove_bank_service()`.

There is no destructive SDRAM memory test or 0x55/0xAA fill in the firmware
boot path.  `SDRAM_Init()` performs the FMC command sequence and then clears
only `[__sdram_audio_cold_start__, __sdram_audio_cold_end__)`, currently
`0xC1E4FA20..0xC1EBE180`.  `g_stream_end_to_end_bench` is a NOLOAD object at
`0xC1AD19A0` in `.sdram_stream_service`, outside that interval, and its init
has not run at stage 21.  Its `0x55555555` words are consequently retained or
power-up contents of an intentionally uninitialized SDRAM range, not a
benchmark result and not evidence of a post-clear firmware write.  The
benchmark init itself starts with complete `memset()` calls for its public
record, runtime, and histograms.

The actual initialization defect exposed by the aggressive placement was in
the internal hot NOLOAD sections.  `g_sample_stream_manager_initialized` was
moved from ordinary startup-cleared BSS to `.dtcm_audio`.  Its first use was
as an initialization guard, before any explicit write.  A retained nonzero
value skipped `sample_stream_io_init()`, leaving the relocated D1 async jobs
and the scheduler/block-device runtime uninitialized.  Stale active-job state
can keep `streaming_critical` asserted, which makes the groove bank's
background scheduler admission return `NOT_NOW` indefinitely before the
first mount.  The physical backend pending pointers and its nonzero generation
seed had the same NOLOAD initialization defect.

The correction keeps the placement unchanged: the manager's public cold init
now establishes the guard before using it, and the stream I/O init explicitly
clears the physical pending array and restores generation 1.  No Streamer
object was moved, and the cache remains 368 pages.

`g_groove_boot_diag` is now exported in uncached D3 at `0x38008000` (76 bytes).
It records the real groove state and previous state, service step, scheduler
admission/owner/class, SD gate owner/count/streaming-critical flag, media
epoch/status, FatFs result, progress, call count, and last transition cycle.
The state values are the public `GROOVE_STATE_*` enum (0 WAIT_MEDIA through 15
FAILED).  In particular, a repeat of the original starvation is directly
identified by state 0, step `GROOVE_BOOT_STEP_WAIT_STORAGE_ADMISSION`,
admission 0, and `streaming_critical != 0` and/or a non-idle owner.

The complete `.sdram_audio_cold` Release interval is 452,448 bytes:

| Address | Bytes | Object(s) | Required initialization |
|---|---:|---|---|
| `0xC1E4FA20` | 304 | sampler RAM global-to-slot | explicit init, including invalid-slot sentinels |
| `0xC1E4FB60` | 5,472 | sampler RAM audio slots | explicit zero/init |
| `0xC1E510C0` | 20,480 | Multi sample projections | explicit zero/init |
| `0xC1E560C0` | 16,384 | Multi zones | explicit zero/init |
| `0xC1E5A0C0` | 320 | Multi instruments | explicit zero/init |
| `0xC1E5A200` | 29,568 | pattern slot A | pointers and generations assigned by `seq_engine_control_init()` |
| `0xC1E61580` | 12,288 | locks A | zero payload; attached explicitly to pattern A |
| `0xC1E64580` | 12,032 | audio wavetable registry | explicit zero/init |
| `0xC1E67480` | 16,384 | SD preview ring | payload scratch; indices initialized separately |
| `0xC1E6B480` | 16,384 | wavetable pool I/O | overwrite-before-use scratch |
| `0xC1E6F480` | 4,096 | SD preview I/O | overwrite-before-use scratch |
| `0xC1E70480` | 65,536 | clip-shifter delay | zero history, then runtime init |
| `0xC1E80480` | 6,400 | Braids poly runtime | explicit per-instance init; no pre-main constructor |
| `0xC1E81D80` | 6,400 | Braids runtime | explicit per-instance init; no pre-main constructor |
| `0xC1E83680` | 2,688 | wavetable poly runtime | explicit per-instance reset |
| `0xC1E84100` | 2,688 | wavetable runtime | explicit per-instance reset |
| `0xC1E84B80` | 4,800 | TB303 runtime | explicit reset with nonzero coefficients/sentinels |
| `0xC1E85E40` | 65,536 | Vibe history | explicit zero/init |
| `0xC1E95E40` | 6,144 | audio FX runtime | explicit zero then model initialization |
| `0xC1E97640` | 8,192 | global mod-FX history | explicit zero/init |
| `0xC1E99640` | 9,608 | Haas right history | explicit clear through delay init |
| `0xC1E9BBE0` | 9,608 | Haas left history | explicit clear through delay init |
| `0xC1E9E180` | 131,072 | RevB engine buffer | initialized by `engine.Init()`; raw float buffer, no constructor |

All objects in this arena either require zero/scratch semantics or have an
explicit functional initialization after FMC.  None has a nontrivial C++
pre-main constructor.  `g_fm_voice` remains the sole object returned to
internal SRAM specifically because its C++ construction occurs before FMC.

Release build after the correction: DTCM 89,152 bytes, D1 500,128 bytes, D3
52,992 bytes, SDRAM 32,235,904 bytes.  Hardware validation remains required;
a successful link alone does not establish that the board reaches stages
22-24.
