# Embedded Audio Engine - Product Overview

Le raccourci ARP ouvre directement la surface `MIDI FX`, composee de quatre slots. Cette entree ne change pas le mode musical SEQ/KEYBOARD et n'active aucun effet a l'ouverture.

Chaque Play Track note-capable possede quatre slots MIDI FX independants. La surface propose `OFF/ARP`, avec `RATE`, `STYLE` et `RANGE`; un seul slot ARP peut etre selectionne par piste.

Les tracks synth Prism, Stack, Wave et DELUGE proposent 1 à 8 voix internes avec
moteur, filtre/keytrack, enveloppes VCA/filtre et pan indépendants par voix.

## Banque Patch

La banque SD Patch accepte 192 fichiers indexes, de
`BRICK/PATCH/P0000.B6P` a `BRICK/PATCH/P0191.B6P`.

## Reverb SEND

La reverb globale propose deux modeles exclusifs `MUTABLE` et `DIGITAL`. Les pages affichent uniquement les controles effectifs du modele; `LVL=0` reste un hard-off DSP. Les couples HPF/LPF des reverbs et delays partagent une courbe de reponse large tout en restant editables separement.

## Diagnostic AUDIO TEST 2

Les builds Debug/Test exposent `Settings > Test > Audio 2`, un programme
PCM24/48 kHz déterministe de 4 min 08 s pour comparer référence numérique, tap
pré-codec et enregistrements externes line/casque. Voir `docs/audio_test2.md`.

## Synth/DELUGE - SKEW et WIDTH

- `SINE`, `TRI`, `SAW`, `A-SAW`, `A-SQUARE`: SKEW manuel `0..100 %`, neutre a `0 %`.
- `SQUARE`: WIDTH `0..100 %`, carre normal a `50 %`.
- La Matrix peut toujours moduler SKEW sous zero; seul le reglage manuel est unipolaire.

## 1. Vision

Standalone embedded audio machine for live use.

The project is track-aware by construction: the meaningful unit is the logical track, not a hidden global node and not a physical lane.

Core use:
- real-time audio mixer for external synths
- performance FX and routing
- cue / main style routing
- track-aware sequencer and modulation
- contextual UI driven by the active track identity

This repository targets a playable, deterministic instrument firmware. It is not a generic DSP sandbox and it does not rely on ambiguous central nodes.

Variant audio resources:
- Premium declares exactly three stereo input resources, `Input1..3`.
- Low-cost declares one stereo line input, `Input1`; no `Input4` resource exists.
- On the low-cost revision without the volume potentiometer fitted, master volume uses a temporary fixed gain; PB1 is absent from the ADC sequence and runtime processing.

## 2. Product priorities

Priority order:
1. audio stability
2. predictable CPU usage
3. live-playable UX
4. flexible but controlled routing
5. maintainable codebase

Guiding rule:
- if it risks audio stability, reject it
- if it breaks worst-case predictability, rethink it
- if it improves live performance without breaking invariants, prioritize it

## 3. Hardware and runtime constraints

### Hardware
- MCU: STM32H743 @ 480 MHz
- codec: CS42448 via SAI TDM
- audio format: 24-bit / 48 kHz
- DMA: double buffer (ping-pong)
- block size: 64 or 128 frames depending on configuration

### Runtime model
- audio processing runs in IRQ
- no RTOS
- no dynamic allocation in the critical runtime path
- no blocking calls in the hard real-time path

These constraints are structural, not optional.

## 4. High-level product model

### Tracks
The product is track-aware.

Current model:
- central product topology: 12 logical tracks on Low-Cost and 14 on Premium
- Pattern/Project use one common role-identified format with separate Play and lightweight Special sequence payloads; track selection exposes 12 tracks on Low-Cost and 14 on Premium
- physical DSP ingress remains distinct from logical tracks
- logical track identity and physical audio lane must not be conflated

Current logical layout:
- 8 Play Tracks
- fixed Special Tracks: Master, Looper, physical Inputs and FX
- Low-Cost: `8 Play + Master + Looper + Input1 + FX`
- Premium: `8 Play + Master + Looper + Input1 + Input2 + Input3 + FX`
- only Play Tracks can change family/type, open instrument browsers, play notes, or use Keyboard/MIDI FX; Special Tracks expose a fixed identity

### Families
Current families:
- `Off`
- `Input1`
- `Input2`
- `Input3`
- `Synth`
- `Drum`
- `Master`
- `Sampler`
- `MIDI`
- `External`

### Notable types
- Special `InputX`: `Audio` fixed
- `MIDI`: `MIDI`
- `External`: `External`
- `Synth`: `Prism`, `Wave`, `Stack`, `DELUGE`
- `Sampler`: `RAM`, `Stream`, `Looper`, `Multi`
- `Drum`: dedicated drum catalog
- `Master`: `FX`

An `External` Play Track reserves one exact physical input (`Input1` on Low-Cost,
`Input1..3` on Premium) while retaining MIDI note, Program and CC behavior. A
reserved Input Special displays `USED Pn` and is not monitored a second time.
Conflicting edits, paste and restore are rejected atomically; no alternate input
is selected silently.

### Ownership model

The architecture is organized around three distinct layers:
- canonical control state
- runtime projection
- execution

Features should hook into the layer that owns the decision:
- canonical control state for source-of-truth edits
- runtime projection for track-aware binding and capability resolution
- execution for hard real-time audio, transport, and other bounded runtime work

This separation is intentional. Do not add a second authority for the same state.

## 5. Current feature shape

### Audio / mixer
- track-aware audio routing
- main / cue separation
- sends and returns
- insert-style processing
- master-oriented performance processing
- The fixed `FX` track exposes 4 MacroFX DSP slots: `DRIVE`, `CRUSH`, `PUMP`, `CHOP`, `WOBBLE`, `COMB`, `RING`, `STUTTER`, `FREEZE`, `COLOR`; `STUTTER` and `FREEZE` are single FX resources. `STUTTER LVL` remains `OFF/ON` full wet. `FREEZE LVL=0` is audible off/history fill, while `FREEZE LVL=1..127` engages freeze, raises the wet/freeze return, and ducks dry until `LVL=127` behaves as repeater-dominant dry-off. `FREEZE B=HOLD` selects `SHORT/MID/LONG/INF` feedback modes; `INF` is a bounded quasi-hold. `FREEZE` reuses the existing per-slot MacroFX delay history and does not share STUTTER history.
- `FX DRIVE` uses `LVL` as slot wet/depth, `A=DRIVE` as overdrive-to-fuzz amount, and `B=TONE` as dark/bright guitar-style color.

### Sampler
- stereo runtime playback through the normal track-aware mixer path
- paged sample cache with RAM-only audio reads
- `READY_FULL` and `READY_PARTIAL` served from sampler-owned SDRAM pages
- `RAM` currently exposes `Shot`, `RevShot`, `Loop`, and `PingPong`; it plays global `kind=RAM/READY` slots from `sampler_ram_pool`
- sample slots expose 256 active global slots backed by a 16 MiB product slot-pool; Stream, RAM and Multi share this catalogue while voice reserve and page-cache margin remain separate
- RAM slicing is enabled by `Slice Count`: `Off` plays the global `Start`/`End` window normally, while `2..64` slices that same global window in a regular grid selected by `note % slice_count`; `Tune` and `Gain` remain global
- `Stream` now exposes `Sample`, `Gain`, `Src BPM`, `Play Mode`, `Loop`, `Stretch`, `Tune`, and `Sync Len`
- `Stream` supports forward `Gate`/`Launch` playback with three stretch modes:
  - `Off`: 1x playback
  - `Speed`: varispeed (`ratio = project_bpm / source_bpm`), pitch changes
  - `Shifter`: varispeed cursor followed by the local stereo pitch-shifter
- `Sync Len` remains exposed for stream timing configuration; `Stretch=Off` stays 1x playback, `Stretch=Speed` keeps the existing varispeed path, and `Stretch=Shifter` uses `Tune` plus `Grain` as the shifter controls while `Hop/Search` are stored but inactive
- `Looper` TONE exposes `ARM` (`Off`/`Rec`/`Overd`), `LEN` (`Free`/`1`/`2`/`4`/`8`/`16`), and `PLAY` (`Off`/`Auto`); current implementation records simple `ARM=Rec` takes, keeps `ARM=Overd` as a bounded no-op until audio overdub exists, and streams playback from transient page-cache pages when `PLAY=Auto`
- `Multi` exposes TONE `INST` / `GAIN` / `LOOP`; `LOOP=ON` loops active notes from valid WAV `smpl` bounds, otherwise forced import-time auto-loop bounds with a mechanical 40%/55% fallback for usable WAVs
- Multi Browser page 3 `CLEAR` deletes only visible `.brickmulti` indexes in the current Multi folder, so indexes can be regenerated without deleting WAVs
- legacy slice handling remains internal compatibility, not a product mode

### Prism
- `Synth/Prism` is a track-aware mono engine with two Braids oscillators exposed on `TONE`
- `TONE/OSC1 VOICE`: `PARAM1`, `PARAM2`, `AMOD`, `MODEL`
- `TONE/OSC1 EDIT`: `LVL`, `TUNE`, `FM AMT`, `PHASE`
- `TONE/OSC2 VOICE`: `PARAM1`, `PARAM2`, `AMOD`, `MODEL`
- `TONE/OSC2 EDIT`: `LVL`, `TUNE`, `FM AMT`, `PHASE`
- `PHASE=Off` preserves the free phase behavior for that oscillator
- `PHASE=On` sends a one-shot sync pulse on the first rendered sample after note-on for Prism models that consume sync; random state is not reset
- `OSC1 LVL=100%` and `OSC2 LVL=0%` preserve the former single-oscillator Prism behavior; two active oscillators are level-normalized before the mono track output

### Stack
- `Synth/Stack` is a separate mono engine from `Synth/Prism`; Prism remains the historical Braids runtime.
- Stack exposes three independent oscillator slots plus noise through `TONE`.
- `TONE/OSC1..OSC3`: `MODEL`, `PARAM1`, `PARAM2`, `PARAM3` per slot, with model-aware labels.
- `TONE/LVL`: `OSC1 LVL`, `OSC2 LVL`, `OSC3 LVL`, `NOISE`.
- `TONE 2/2`: `TUNE` exposes `OSC DETUNE`, `TUNE 1`, `TUNE 2`, `TUNE 3`; `PHASE` exposes `RESET`.
- Stack models: `SINFD`, `SHAPE`, `WAVETABLE`, `SUB`, `FM`, `FEEDBACK FM`, `RING`, `TRIPLE SAW`, `TRIPLE SQUARE`, `SWARM`, `TRIFD`, `SINMORPH`, `TRIMORPH`.
- `SINFD` / `TRIFD`: `FOLD`, `SYM`, `SHAPE`; `SOFT` is no longer an active Stack model.
- `SINMORPH`: `MORPH`, continuous `TARGET` (`FULL RECT`, `HALF RECT`, `TRIANGLE`, `FOLD`), `ASYM`.
- `TRIMORPH`: `MORPH`, continuous `TARGET` (`PULSE`, `SAW`, `SQUARE`), `SKEW`.
- The three slots and noise are summed mono into the normal track path: filter, VCA/volume, track inserts and mixer bus.

### Wave

Le moteur WAVE natif float utilise des mipmaps band-limited préparées à l'import (2048 à 8 samples par frame), persistées dans le cache WAVETABLE et sélectionnées hors de la boucle sample selon le pitch. Le rendu conserve l'interpolation linéaire légère, sans chemin Q31 ni sinc.
- `Synth/Wave` is the user wavetable engine: two mono wavetable oscillators reading resident SDRAM `WAVETABLE` assets.
- BRICK TONE pages remain `OSC1 WAVE` (`TABLE`, `POS`, `START`, `END`), `OSC1 VOICE` (`LEVEL`, `TUNE`, `PHASE`, `FLIP`), `OSC2 WAVE`, `OSC2 VOICE`.
- Le cache autoritaire unique `B6WT` v2 contient le répertoire des neuf bandes, leurs offsets/tailles/seuils de pitch et le payload S16 contrôlé par CRC.
- `POS` is smoothed per oscillator inside the Wave runtime after `START/END` remap, so p-locks and Matrix modulation do not jump frames abruptly.
- `OSC1/2 WAVE` pages show a wide precomputed wavetable preview with `START/END` zone and `POS`; UI rendering never scans the full table.
- The track identity and runtime engine are separate from `Synth/Prism`.

### DELUGE
- `Synth/DELUGE` is a mono 48 kHz GPL-3.0 port of the Synthstrom Deluge basic oscillators, adapted from NEON to bounded scalar Cortex-M7 fixed-point rendering.
- Upstream reference: `SynthstromAudible/DelugeFirmware` commit `0d9cbf0440f0555e2544cc1eb019b31675637008`.
- Models: `SINE`, `TRI`, `SQUARE`, `A-SQUARE`, `SAW`, `A-SAW`; default `SQUARE`.
- TONE pages: `MAIN` (`MODEL`, `LEVEL`, `TUNE`, `FINE`) and `SHAPE` (`WIDTH` for `SQUARE`, `SKEW` for the other five models, `PHASE`, `RETRIG`, empty).
- The port keeps Deluge Q32 phase, sine/triangle and 20-band anti-alias table selection, analog square and mystery-synth analog saw tables, PWM and non-square read distortion.
- Matrix exposes `LEVEL`, `TUNE`, `FINE`, `WIDTH/SKEW`; `MODEL`, `PHASE`, `RETRIG` remain discrete/structural.

### Sequencer
- integrated sequencer
- transport / clock / scheduler
- `SEQ Length` defaults to 16 steps on new or blank projects; saved projects keep their stored per-track length
- parameter locks
- modulation baseline with three LFOs per track
- MOD LFO pages are `LFO 1`, `LFO 2`, `LFO 3`; each exposes `RATE`, grouped `SHAPE/PHASE`, then `TRIG` in slot 4. For `RND`, the `PHASE` slot is shown as `Slew`.
- LFO shapes include bipolar `SIN/TRI/SAW/SQR/RND/RSAW` and positive `SIN+/TRI+/SQR+`; trig modes are `FREE/TRIG/HOLD/ONE`
- live-performance oriented behavior

### UI
- contextual UI based on active track family/type/runtime
- track-aware page exposure
- hall-based interaction model
- keyboard / MIDI FX / pattern / mute workflows; in QUICK MUTE, track keys mute/unmute directly even while SHIFT remains held
- Omnichord chord buttons follow the Orchid order `Dim`, `Min`, `Maj`, `Sus`, `6`, `m7`, `M7`, `9`, with Orchid-style secret chord combinations and a live chord label in `KEYBOARD`
- OLED template parameter slots show the widget first and the parameter name below; after explicit user edits, the bottom text temporarily shows the formatted edited value, then returns to the name
- `ENV` exposes `ENV 1/2` for filter/VCA/ENV3 shaping and `ENV 2/2 > RETRIG` for `ENV FLT`, `ENV VCA`, `ENV MOD` hard/soft retrigger switches plus filter `KeyTrk`; retrigger defaults to `ON`/hard.
- Filter, VCA and ENV3 parameters, including their retriggers, share the logical `ENV` owner and p-lock set. `MIX` is limited to level, pan and sends; `MOD` owns only LFO, Matrix, Multi and Slew controls. The VCA mixer backend and `mod_env3` execution backend are unchanged.
- On Premium, the former VCA parameter button is now a direct shortcut to `ENV/VCA`; the normal `ENV` button and Low-Cost navigation remain unchanged.
- Diagnostic pages and instrumentation are available in low-cost `Debug` (`-Og -g3`) and `Test`. The dedicated performance-representative firmware is built with `cmake --preset Test` then `cmake --build --preset Test`, and flashed with `flash_test.bat`; it keeps Release optimization/LTO. `Release` and `Premium` do not contain the diagnostic menu, runners, strings, buffers, or heavy instrumentation.
- In that Test firmware, `Settings > Test > Hall` provides a read-only live diagnostic for all 24 Hall keyboard keys; encoder 1 selects the key, encoder 2 selects raw MUX address `0..7`, and the page shows acquisition raw, the raw value delivered to the low-cost engine at 2.8 ms, calibration bounds, pressed state, velocity, and the three pre-mapping MUX raws.
- In that Test firmware, `Settings > Test > Audio` runs a 95-case automatic audio bench with temporary deterministic RAM/Wave calibration assets. The 69 engine/oscillator cases use a 300 ms warmup and a 1 s measurement; filters and other simple cases retain 300/500 ms timing. Seven delay/reverb cases record separate `FX_ACTIVE` (2 s) and `FX_TAIL` (3 s) windows after a 1 s warmup. Sum coverage is 1/2/4/8/12 coherent tracks, 12 musical tracks and 12 tracks with delay+reverb; coherent progression failures are reported as `FAIL`. The screen reports progress and `EST 02:45`; `PAGE 1 STOP` or leaving the page safely cancels, silences test voices and restores captured state. Results use the 178-column CSV schema v4 in `/AUDIO_TEST.CSV` (or `/AUDIO_TEST_V4.CSV` when preserving an incompatible file).
- In `Debug` and `Test`, `Settings > Test > Monkey` exposes MONKEY TEST. `PAGE 1` starts or stops deterministic injection through the normal button, encoder, and keyboard paths; the session may navigate away from Settings and remains active until explicitly stopped.
- MT-03 adds the seeded deterministic logical stream: weighted button taps/holds/chords, SHIFT combinations, encoder moves and keyboard press/release actions are scheduled from the 1500 Hz engine clock. MT-04 injects that stream through the normal bounded input paths; the page displays seed, action count and last type.
- MT-05 runs Monkey against a disposable live snapshot, caps unattended master gain, blocks persistent SD writers and protects Project/Sample actions with `TEST SAFE`; stopping restores the pre-test project, UI focus, playheads and transport state.
- MT-06 adds a bounded 10 Hz health monitor for IRQ load, Sampler/Looper underruns, UI invariants and monitor canaries. The page reports warning/error/crash counters and the latest classified issue; only corrupted invariants stop and restore the disposable session.
- MT-07 persists the running seed, logical action index, counters and the latest 16 actions in a CRC-protected double-slot capsule occupying 1 KiB of non-cacheable Backup SRAM. Breadcrumbs are committed before input injection; no SD write occurs in this path.
- MT-08 captures HardFault, MemManage, BusFault and UsageFault on a dedicated 1 KiB DTCM emergency stack, commits the Cortex-M7 frame and fault status registers to the capsule, then explicitly requests a system reset without depending on a watchdog.
- MT-09 arms a diagnostic-only 12 s IWDG when MONKEY TEST first starts. Only a completed main loop with fresh audio-derived engine progress reloads it; Debug freezes it while the debugger is halted, Test keeps target behavior, and Release/Premium contain no diagnostic watchdog.
- MT-10 captures RCC reset flags before HAL initialization, classifies fault and watchdog resets, archives the complete last failure in a separate CRC-protected Backup SRAM bank, then automatically resumes after boot with a different seed. The failing seed and action index remain available for explicit replay; no action is blacklisted.
- MT-11 writes crash reports and sparse session summaries to `0:/BRICK/DIAG/MONKEY.LOG` through an independent diagnostic SD client. Reports are synced before the Backup SRAM archive is marked reported, retry after SD errors, deduplicate completed writes, and rotate at 256 KiB to a single `MONKEY.OLD`.
- MT-12 adds explicit deterministic replay of the last archived failure: `PAGE 2` regenerates the original seed in the disposable test snapshot at its original logical timing, verifies the archived target breadcrumb exactly, and pauses immediately before it. `PAGE 3` fires that target; Debug breaks first when a debugger is attached. A mismatch stops safely, and no action is blacklisted.
- no product VU/peak meter in the mixer header
- boot default (normal path): track 1 focused on `CFG`; a missing or invalid Hall calibration opens `CALIBRATION`, and low-cost validates its 24 keyboard keys in two 12-key stages before saving
- low-cost: `Settings > Calibration` exposes `HALL KBD` and `HALL VEL` through the existing calibration workflows
- low-cost: `KEYBOARD > VELOCITY` exposes `PROFILE DEFAULT/USER`, default mode `DV/TIME/ENERGY`, the five velocity curves and USER calibration status/access; an unavailable USER profile is shown as `NO CAL`
- low-cost Hall velocity consumes every valid raw key measurement at about 2.8 ms; Premium keeps its historical digital ASC x4 path

### Parameter system
- UI-side parameter control
- runtime-side application
- modulation and track-aware filtering of valid targets

### Persistence
- pattern live state
- pattern save/load
- project save/load
- boot context restore



## 7. What must stay true

The project should keep these rules visible in new work:
- every feature should declare its owner layer
- runtime seams must stay explicit
- track-aware behavior must remain the default reasoning model
- future dual-core work should be prepared by clean seams, not by premature IPC or central buses
- avoid hidden coupling through ambiguous shared nodes
- reuse existing authorities before creating new ones

When adding a feature, hall mode, engine, UI behavior, or runtime seam, prefer the smallest change that preserves these boundaries.

## 7.1 Sampler/Looper

`Sampler/Looper` is an audio-routable Sampler type with a dedicated transient runtime:
- `ARM=Rec` records/replaces one loop take, then returns to `Off`
- `ARM=Overd` is visible but remains no-op in this pass
- `PLAY=Off` keeps the captured loop ready but silent
- `PLAY=Auto` starts playback with transport after the take is finalized and its first page is ready
- `SAVE` commits the temp WAV to `PROJECT/LOOPS` without routing playback through project sample slots

Looper playback does not full-load the WAV, does not use the project `sample_pool`, does not depend on the WAV catalogue, and does not reuse normal Sampler slots. It streams temp/final WAV files through transient `sample_page_cache` ids; audio IRQ reads only ready RAM pages and falls silent locally if a page is missing.

## 8. What this repo is optimizing for

This project is optimizing for:
- deterministic embedded behavior
- bounded real-time cost
- coherent track-aware routing
- strong live interaction model
- incremental evolution of the existing codebase

This project is not optimizing for:
- abstract architecture purity at any cost
- speculative redesigns
- feature growth that breaks timing guarantees
- convenience patterns that add hidden authorities

## 9. Documentation map

Use the documents according to their role:

- `AGENT.md`
  - work rules
  - modification discipline
  - global invariants to respect

- `docs/architecture/ARCHITECTURE_GLOBAL.md`
  - orientation map
  - which architecture zone to read first

- `docs/architecture/z*.md`
  - detailed zone-level architecture
  - real authorities, boundaries, dependencies

This `README.md` is intentionally product-oriented.
It is not the authoritative architecture document.

## 10. Current status

The codebase already contains:
- one applied target topology (`8+4` Low-Cost, `8+6` Premium) with role-identified common storage
- track-aware runtime binding
- contextual UI families
- sequencer / clock / scheduler foundations
- parameter and modulation infrastructure
- persistence layers

Some areas are stable, others are still under active stabilization.
When in doubt, trust the code and the architecture zone documents before broad assumptions.

## 11. Principle

Keep it simple, deterministic, and playable.

## Master track status

Master and FX are fixed Special tracks. Master exposes the global reverb, delay and compressor under `TONE`; FX owns the four MacroFX slots. `MIX` contains only per-track level, pan and sends. The former buffer workflow has been removed; Looper XFade remains available on `Sampler/Looper`.

## Track filters

Track `LP`, `HP` and `BP` modes use one stereo/mono float TPT state-variable filter over `20 Hz..16 kHz`. Resonance progresses from clean to a softly saturated, bounded loop with a maximum Q of 6.5; LP, HP and BP have calibrated output levels. Cutoff keeps `0.01` control and p-lock resolution, while base cutoff is smoothed in octaves and Matrix/LFO, filter envelope and keytrack retain their musical depth. `OFF` uses a 256-sample constant-sum transition and clears stale states; LP/HP/BP changes use 64-sample transitions. The track order is identical for mono and stereo: filter, VCA/volume, track inserts, then sends/bus. Delay feedback, volume and stereo width use 10 ms change-triggered ramps.

# AUDIO TEST - calibration de volume

Le banc automatique du firmware de test mesure chaque modele sur C2/C4/C6 et
ses controles reellement exposes. Il produit dans le CSV les mesures
ATTACK/SUSTAIN ou ATTACK/STRIKE, trois repetitions des sons aleatoires, et une
recommandation indicative par modele, qui n'est pas appliquee au runtime.
L'alignement produit utilise uniquement des gains fixes par moteur avant le
filtre et les traitements de piste: PRISM `0 dB`, DELUGE `-7,1 dB`, WAVE
`-7,5 dB`. SAMPLER (RAM, streaming et slicing/looping), STACK et DRUM restent
sans correction.
## Prototype compresseur comparatif

Master `TONE 3/3` permet de comparer sur le bus MAIN les modeles `OFF`, `DELUGE` et
`BRICK`, avec controles communs de seuil, ratio, enveloppe, makeup manuel, mix
et sidechain HPF. Deluge expose sa saturation; Brick expose le detecteur
PEAK/RMS et le soft knee.
# Addendum 2026-07-30 - DRUM / MD

- La famille Drum propose maintenant `MD` et `BD Analog`. `MD` est integre au
  routage mais reste volontairement silencieux avant les etapes DSP suivantes;
  `BD Analog` conserve son comportement actuel.
# Addendum 2026-07-30 - selection des modeles DRUM / MD

- `DRUM / MD` propose maintenant `TRX-BD`, `TRX-SD`, `TRX-CH`, `EFM-BD`,
  `EFM-SD` et `EFM-CB` via `MODEL`, avec des controles TONE nommes
  dynamiquement. Cette etape reste volontairement silencieuse.

## Addendum 2026-07-31 - TRX-BD jouable

- Le modele `DRUM / MD > TRX-BD` produit maintenant un kick natif controle par
  `PTCH DEC RAMP RDEC STRT NOIS HARM CLIP`. Les autres modeles MD restent
  silencieux jusqu'aux etapes suivantes; `BD Analog` reste inchange.
# Looper LowCost

La cible LowCost fournit un seul Looper utilisable dans tout le produit. Une seconde conversion est refusee; au chargement d'un ancien contenu, le premier Looper par ordre de track est conserve et les suivants sont convertis en `Sampler/RAM`.
# Budget des voix synthetiques

LowCost et Premium partagent un maximum global de 16 voix synthetiques reservees sur au plus 8 tracks. Chaque track Synth reserve de 1 a 8 voix; changer de moteur Synth conserve cette valeur. `VOICES` s'arrete au budget disponible et n'est ni modulable ni p-lockable. La famille Synth reste visible a saturation (`FAMILY : MAX`). Un collage de track ou une application de Patch trop polyphonique applique tout sauf la cardinalite, limitee avec `VOICE LIMITED`; sans slot disponible l'operation est refusee sans mutation avec `VOICE MAX`.

# Mute par type de track

Le mute coupe progressivement la sortie audio sans clic. Il stoppe les notes internes/MIDI des tracks Play, MIDI et External, fond le retour External/Input, laisse avancer le Looper en silence et retire seulement la contribution de la track FX. Master n'a pas de mute ordinaire; rien de manque pendant le mute n'est rejoue au demute.

# Sequences Play et Special

Les huit Play Tracks conservent 64 steps, jusqu'a 32 p-locks par step et 1024 p-locks par piste avec notes, velocite, duree et microtiming. Les Special Tracks utilisent 64 steps d'automatisation, 16 p-locks par step et un pool de 256, sans notes ni MIDI FX; leur champ d'action leger est reserve aux commandes propres a leur role.

Pattern, Project, Undo/Redo et Clipboard conservent ces deux modeles sans convertir une Special en Play. Un collage entre roles incompatibles est refuse; clear efface aussi l'action Special.

# Play Tracks independantes

Clavier, MIDI, MIDI FX, sequenceur et mute agissent directement sur chaque Play Track. Chaque piste conserve ses quatre voix PLAY et la polyphonie reste celle de son moteur; Stop/Panic ferme toujours toutes les notes actives.


Les p-locks, l'edition LENGTH et le copier/coller de steps agissent uniquement sur la track editee. Le clipboard conserve trig, roll et p-locks vers une track compatible, sans copie implicite d'autres pistes.
# MIDI FX

Les huit Play Tracks note-capable disposent de quatre slots MIDI FX ordonnes. Le modele ARP neuf transforme maintenant de facon identique les notes du clavier, du MIDI entrant et du sequenceur sur l'horloge audio sample-domain; `OFF` conserve le chemin direct et le live record capture uniquement la source.
Les quatre controles de chacun des quatre slots MIDI FX sont p-lockables. Un lock MODEL coupe proprement l'ancienne generation avant de changer de modele; la fin du lock restaure la base sans ressusciter de note.
Les reglages MIDI FX suivent la piste dans les Patterns, Projects, copies de Track et Undo/Redo. Ils ne font pas partie des Patchs ni des Kits.
