# Audit d'architecture — SAMPLE « stream-by-path » sans raté

Date : 7 octobre 2026. Cible : STM32H743, build `Release`. Le code est
l'autorité. Les chiffres SD cités sont historiques, pas des WCET qualifiés.
Cette passe ne change aucun runtime.

## 1. Verdict

Le **stream-by-path est viable et constitue le bon modèle** pour START/LENGTH
déterministes. Le Streamer ne doit plus seulement entretenir la page voisine de
la tête : il doit matérialiser, fusionner et ordonner toutes les régions que les
trajectoires admises consommeront avant leurs deadlines AUDIO.

Il permet un contrat sans différence perceptible RAM/Stream pour START/LENGTH
statiques, p-locks, sample-locks, microtiming, retriggers et modulation
déterministe, même rapide si son working set est résident ou si son flux froid
passe l'admission. LENGTH sample-accurate ne coûte aucune I/O lorsqu'elle
rapproche seulement la fin ; elle ajoute du chemin lorsqu'elle découvre une
nouvelle région. Reverse réutilisera la même architecture avec un reader signé.

Deux réserves sont fondamentales :

1. Une garantie zéro raté exige des bornes SD certifiées (`Lmax`, débit minimal,
   coût d'un seek et d'un petit fragment). Le dépôt ne conserve qu'environ
   19 MB/s et 3,15–3,25 ms/64 KiB historiques, sans p99/max récent
   `request -> READY`. On peut dimensionner, pas encore certifier un nombre de
   régions froides/s.
2. Un changement live arbitraire reçu au sample N vers une région froide ne
   peut pas être READY au même N : c'est une limite de causalité. L'encodeur
   peut entrer dans le contrat via un corridor pré-résident et un taux physique
   qualifié, mais le dépôt ne mesure pas le sweep humain maximal.

Verdict produit : **GO architectural** pour `SAMPLE / MULTI` et une équivalence
stricte dans une enveloppe admise ; **NO-GO aujourd'hui pour annoncer cette
garantie**, avant qualification WCET SD/encodeur. Aucun late-start, maintien de
l'ancien son, silence ou comportement hit/miss ne fait partie du mode normal.

## 2. Contrat utilisateur garanti

### 2.1 Sémantique unique

```text
asset SAMPLE
START  = première frame de la région
LENGTH = longueur de région
stop   = min(total_frames, START + max(1, LENGTH * total_frames))
```

Il n'existe pas d'autorité utilisateur END. La résidence FULL, PATH ou STREAM
est une décision interne révocable, jamais un type de piste.

Être réellement « comme RAM » implique la sémantique actuelle de
`brick6_sampler_runtime_reconcile_ram_voice_bounds_live()` : au trig la tête
démarre à START ; pendant la lecture START/LENGTH changent les bornes au
timestamp demandé ; la tête n'est recalée que si elle sort de la nouvelle
région ; le recalage emploie le declick actuel de 16 samples. Raccourcir LENGTH
peut arrêter/wrapper au sample exact ; l'allonger ne saute pas la tête.

Une sémantique où chaque valeur START téléporte continuellement la tête est un
nouveau mode « scrub », pas le comportement RAM courant. Sa trajectoire et son
coût SD doivent être admis séparément.

### 2.2 Invariant de zéro raté

```text
si un plan affirme source(asset, frame) au sample n,
le fragment contenant frame est READY et pinné avant n.
```

Un plan n'est publiable à AUDIO que si toute sa fenêtre irrévocable est admise.
Une deadline impossible refuse l'armement avant son début ; elle ne modifie
jamais un timestamp publié. Une branche d'erreur peut couper proprement le son
sur retrait/corruption de carte ou violation de contrat, mais ce n'est pas un
mode produit.

- Un p-lock au sample N modifie les bornes au sample N.
- NOTE et sample-lock utilisent le même plan immutable et le même N.
- AUDIO ne consulte pas STORAGE et ne décale jamais N.
- La modulation START actuelle est évaluée sur 64 frames, soit 750 décisions/s
  à 48 kHz. START n'est ni rampable continu ni audio-rate dans
  `mod_destination_catalog.c`.

## 3. État actuel utile au nouveau modèle

### 3.1 Chemin et autorités

```text
UI / SEQ / p-lock / Matrix
          v
CONTROL : programme, valeurs, calendrier
          v FIFO horodatée
AUDIO IRQ : sampler -> reader -> cache READY -> mix
                         v leases
STORAGE : stream manager -> page cache -> I/O -> scheduler SD
                                                    v
                                              SDMMC DMA/IRQ
```

Fichiers/fonctions structurants :

- CONTROL/SEQ : `tone_program_control.c`, `param_registry_backends.c`,
  `seq_engine.c::schedule_step()`, `control_music_output.c` ;
- AUDIO : `brick6_sampler_runtime.c` et ses `.inc`, notamment
  `brick6_sampler_runtime_trigger_ram()`,
  `brick6_sampler_runtime_clip_start_playback()` et
  `brick6_sampler_runtime_reconcile_ram_voice_bounds_live()` ;
- reader : `sample_voice_reader.c`,
  `sample_voice_reader_bind_musical_play_plan()`,
  `sample_voice_reader_seek()`, `sample_voice_reader_cursor.inc` ;
- STORAGE : `sample_stream_manager.c`, `sample_page_cache.c`,
  `sample_stream_io.c`, `sample_stream_backend_physical.c`,
  `sd_scheduler_runtime.c`, `sd_block_device.c` ;
- modulation/entrée : `mod_lfo_v1.c`, `audio_mod_matrix_process.inc`,
  `mod_destination_catalog.c`, `encoders_hw.c`,
  `encoder_control_dispatcher.c`, `param_value_policy.c`.

RAM lit un pointeur contigu. START/LENGTH sont exposés ; forward, reverse,
loop/ping-pong, tune Q16, retrigger et changement live sont couverts.

Stream construit un `sample_play_plan_t`, publie un lease puis bind le reader.
Le bind exige la page initiale READY. Le reader est forward-only. START/LENGTH
ne sont ni exposés au type Stream, ni persistés dans son DTO, ni intégrés à son
plan. La divergence est donc modèle TONE/persistence + absence de préparation
des destinations + reader limité à la fenêtre présente.

Multi réutilise ce reader/cache et possède huit voix globales. L'index v4 vient
de `multi_sample_import.c`/`multi_sample_index.c`, est chargé par
`multi_sample_loader.c`, puis résolu par `multi_sample_pool_resolve_source()`.
Son stealing choisit la plus ancienne release puis held. Multi prouve le partage
de pages, pas l'anticipation d'une trajectoire.

### 3.2 Cache/fenêtre/I/O

| Élément | Valeur réelle |
|---|---:|
| Page | 64 KiB |
| Stéréo FLOAT32 | 8 192 frames/page |
| Durée/page x1 | 170,667 ms |
| Cache physique | 376 pages = 23,5 MiB |
| Slot/static pool | 340 pages = 21,25 MiB |
| Runtime mobile | 36 pages = 2,25 MiB |
| Crédits Stream+Multi | 8 |
| Overdub réservé | 1 reader |
| Rôles/lease | 4 |

Les rôles sont `CURRENT`, `NEXT`, `LOOP_START`, `LOOP_START_NEXT`. Une voix non
bouclée a zéro page arrière et une page avant. `sample_stream_manager` parcourt
les leases en round-robin sans deadline, horizon, pitch ni trig futur.

Le cache est déjà global, clé par `{domain, object_id, generation, page}` ; les
voix partagent un hit. Les pages mobiles restent confinées aux 36 pages runtime.
Deux jobs et deux requêtes peuvent être queueés, mais la SD est sérialisée ; la
cible physique in-flight reste 1. Une page non READY en sortie de fenêtre
termine actuellement la voix : futur fault de contrat, jamais politique produit.

### 3.3 Mesures disponibles

- H743/SD 50 MHz/4 bits : environ 19 MB/s transactionnels historiques ;
- page canonique 64 KiB : environ 3,15–3,25 ms ;
- 2,58 ms concernait l'ancien PCM24 48 KiB ;
- aucun p50/p95/p99/max récent archivé.

`g_stream_rec_perf` mesure déjà `request_to_ready`, `submit_to_dma`, DMA,
`dma_to_finalize`, pages, bytes, misses et `audio_page_missing`.

## 4. Architecture proposée

### 4.1 Streamer le chemin

```text
SEQ/CONTROL                PATH PLANNER                 STORAGE EDF
événements futurs  ->  trajectoires par voix  ->  besoins fusionnés
trigs/locks/LFO        segments + générations      fragments + deadlines
                                                      v
                                              cache READY/pinné
                                                      v
AUDIO consomme à N <------------------------- plan READY immutable
```

Trois objets statiquement bornés :

```text
musical_intent { trig, locks, sample_lock, microtiming, due_sample }

path_segment {
  voice_generation, asset_key, audio_t0, audio_t1,
  source_x0_q32, source_dx_q32,
  region_begin, region_end, loop_mode, flags
}

storage_need {
  asset_key, first_fragment, fragment_count,
  ready_deadline, last_consume, plan_generation
}
```

Un segment linéaire couvre pitch/varispeed stable. Trig, p-lock, wrap,
direction ou asset coupent le segment. Une LFO n'émet pas artificiellement 750
requêtes/s : le planner évalue les mêmes fenêtres qu'AUDIO, n'émet que les
changements de fragments puis fusionne runs contigus et répétitions.

Le prédicteur doit partager les règles numériques AUDIO : Q16/Q32, clamps,
phase LFO, trig modes, seed random S&H, Matrix, slew, pitch et reconcile
START/LENGTH. CONTROL possède l'état futur ; un noyau pur commun calcule la
position ; AUDIO conserve l'état irrévocable. Un checkpoint
`{generation, audio_sample, path_hash}` détecte toute divergence avant usage.

LFO, automation séquencée, envelopes issues de notes planifiées et random S&H
seedé sont prédictibles. Potentiomètre, MIDI/CV/audio live ou changement live de
routing ne le sont pas : working set résident ou enveloppe live admise.

### 4.2 Horizon et recalcul

```text
H >= jitter_publication_max + demand_bound_SD + finalize_max + marge_certifiée
```

À 120 BPM, 1/4 mesure vaut 500 ms, 1/2 mesure 1 s, une mesure 2 s. Proposition
de prototype : horizon roulant 250 ms, étendu au prochain événement irrévocable
et jusqu'à une mesure pour les locks connus. La valeur produit dépendra de
`Lmax` et de l'analyse de demande.

Le SEQ actuel ne fournit pas cet horizon : `source_discovery_advance_q16` couvre
microtiming/quantize/groove et `SEQ_PRODUCT_MAX_SOURCE_GENERATIONS` vaut 12. Un
planner STORAGE séparé lit le snapshot sans publier les NOTE plus tôt.

Chaque bloc CONTROL avance l'horizon. Pattern/routing incrémente une génération
et reconstruit la partie révocable. La partie publiée reste immutable. Les pins
annulés sont libérés ; une I/O en vol finit normalement.

### 4.3 Scheduler STORAGE et admission

Le round-robin devient un EDF fixe : dédupliquer asset+fragment, fusionner les
adjacences compatibles, retenir deadline minimale/dernière consommation
maximale, servir le plus faible slack, réserver Recorder/métadonnées et publier
READY seulement après validation token/key/epoch/génération et D-cache/DMA.

```text
Cjob(G) = command_max + seek_max + transfer_max(G) + finalize_max
```

Pour toute fenêtre de deadlines, la somme des `Cjob` froids et des clients SD
réservés doit tenir dans la fenêtre. L'admission vérifie aussi payload pinné,
slots/jobs, coût planner et budget AUDIO. Une surcharge est refusée avant
publication, jamais découverte dans AUDIO.

## 5. Métrique réelle

```text
{ octets_froids/s,
  transactions_froides/s,
  seeks_physiques/s,
  demande_WCET par fenêtre de deadlines,
  working_set simultanément pinné }
```

Pages/fragments distincts/s mesure l'amplification ; octets/s le DMA ; seeks et
transactions le coût fixe ; la demand-bound capture huit jumps simultanés ; le
working set explique qu'une LFO rapide peut coûter zéro SD après warmup. La
métrique d'admission est donc le nombre de **fragments froids fusionnés avec
deadlines**, pas la fréquence LFO.

## 6. Granularité du cache

| Granularité | Frames stéréo | Durée x1 | Durée x4 | Plafond à 19 MB/s* |
|---:|---:|---:|---:|---:|
| 4 KiB | 512 | 10,667 ms | 2,667 ms | 4 639 fragments/s |
| 8 KiB | 1 024 | 21,333 ms | 5,333 ms | 2 319 fragments/s |
| 16 KiB | 2 048 | 42,667 ms | 10,667 ms | 1 160 fragments/s |
| 64 KiB | 8 192 | 170,667 ms | 42,667 ms | 290 pages/s |

\* Octets seulement, sans commande/seek ; petits reads non mesurés.

| Variante | Avantages | Risques |
|---|---|---|
| 64 KiB unique | code actuel, excellent séquentiel | amplification x8 face à 8 KiB sur jumps |
| petits partout | random précis | metadata, transactions, séquentiel sans coalescence |
| caches 64+petit | politiques simples | duplication, cohérence et partition rigide |
| sous-pages dans slabs 64 KiB | un pool, random fin, coalescence | index/bitmap et spans AUDIO plus fréquents |
| fragments variables | amplification minimale | allocateur/fragmentation, preuve RT difficile |

Recommandation : **fragments logiques 8 KiB dans des slabs 64 KiB**, coalescés
automatiquement par 2/4/8. 8 KiB donne 1 024 frames, 5,33 ms à x4 et divise par
huit les octets d'un jump. 4 KiB réduit la marge ; 16 KiB est le repli si le
coût fixe SD domine. Slabs et sous-pages restent alignés cacheline ; DMA invalide
la plage alignée, jamais un payload partiellement publié.

À payload constant 23,5 MiB, cela donne 3 008 fragments au lieu de 376 pages.
Descriptors/index ajouteront quelques centaines de KiB, à confirmer par le map
`Release`, contre 56 KiB économisés par destination aléatoire.

## 7. P-locks, pattern et branches

- Même destination : un besoin partagé, `last_consume` maximal.
- Sample-lock + START : résolution atomique de l'asset puis START dans cet asset.
- Plusieurs tracks au même sample : un groupe de demand-bound, admis ensemble.
- Microtiming : deadline après microtiming, pas au step nominal.
- Retriggers/rolls : occurrences développées puis fusionnées.
- Conditional/fill : précharger l'union des branches encore possibles.
- Pattern change : entry-set chaud par pattern sélectionnable, ou demande admise
  pour sa prochaine frontière quantifiée. Un switch au sample courant vers un
  asset froid est hors domaine car causalement impossible.

À 120 BPM, un 1/16 laisse 125 ms. Huit pages historiques coûtent environ 26 ms
de transfert pur, hors seek/scheduler : très plausible, non encore garanti. La
preuve est `8 * Cjob_wc <= 125 ms - marge`, ou un départ plus anticipé.

## 8. LFO/Matrix déterministes

Pour un span sinusoïdal couvrant une fraction `w` d'un fichier de durée `D` :

```text
Q64 = ceil(w * D * 48000 / 8192)
franchissements/s <= min(750, 2 * fLFO * Q64)
```

Ce sont des transitions de régions, pas nécessairement des I/O si le cycle-set
est résident. Colonnes : `0,1 / 0,5 / 1 / 2 / 5 / 10 / 20 Hz`.

| Span / fichier | Working set max | Franchissements 64 KiB/s |
|---|---:|---|
| ±1 %, 1 s | 2 pages | 0,2 / 1 / 2 / 4 / 10 / 20 / 40 |
| ±1 %, 10 s | 3 pages | 0,4 / 2 / 4 / 8 / 20 / 40 / 80 |
| ±1 %, 1 min | 9 pages | 1,6 / 8 / 16 / 32 / 80 / 160 / 320 |
| ±1 %, 10 min | 72 pages = 4,5 MiB | 14,2 / 71 / 142 / 284 / 710 / 750 / 750 |
| ±10 %, 1 s | 3 pages | 0,4 / 2 / 4 / 8 / 20 / 40 / 80 |
| ±10 %, 10 s | 13 pages | 2,4 / 12 / 24 / 48 / 120 / 240 / 480 |
| ±10 %, 1 min | 72 pages = 4,5 MiB | 14,2 / 71 / 142 / 284 / 710 / 750 / 750 |
| ±10 %, 10 min | 705 pages = 44,1 MiB | 140,8 / 704 / 750 / 750 / 750 / 750 / 750 |
| 0–100 %, 1 s | 7 pages | 1,2 / 6 / 12 / 24 / 60 / 120 / 240 |
| 0–100 %, 10 s | 60 pages = 3,75 MiB | 11,8 / 59 / 118 / 236 / 590 / 750 / 750 |
| 0–100 %, 1 min | 353 pages = 22,1 MiB | 70,4 / 352 / 704 / 750 / 750 / 750 / 750 |
| 0–100 %, 10 min | 3 517 pages = 219,8 MiB | 703 / 750 / 750 / 750 / 750 / 750 / 750 |

Exemple frontière : 0,1 Hz/0–100 %/10 min demande environ 703 pages froides/s,
soit 46 MB/s en 64 KiB, impossible. En 8 KiB, environ 5,8 MB/s mais jusqu'à 703
transactions/s : seul le WCET petit read décidera. À haute fréquence, une forme
périodique peut au contraire revisiter un petit ensemble de positions qui devient
entièrement résident. Pour la sémantique RAM réelle, sans téléportation continue,
les besoins sont souvent très inférieurs : cette table borne le mode scrub.

Le prototype évalue une valeur/bloc puis run-length-encode les IDs. Une résolution
analytique des crossings est une optimisation ultérieure. Random S&H reste
prédictible si seed et compte de triggers sont clonés.

## 9. Encodeur live

### 9.1 Bornes du code

- TIM7 : 5 208,34 polls/s dans le contrat FIFO.
- Build courant : encodeur continuous, 1 transition/incrément, donc plafond
  logiciel 5 208 événements/s/encodeur ; quatre peuvent publier au même poll.
- Queue SPSC : 32 detents, overflow = drop newest.
- Dispatcher : 32 événements/service, aucune coalescence AUDIO.
- START/LENGTH : pas normal 1/127 = 0,7874 %, SHIFT 0,01/127 = 0,007874 %.

Sur 10 minutes :

| Mode | Temps source/detent | Frames | Distance 64 KiB |
|---|---:|---:|---:|
| normal | 4,724 s | 226 772 | 27,68 pages |
| SHIFT | 47,24 ms | 2 268 | 0,277 page |

La borne 5 208/s n'est pas un débit humain mesuré et dépasse la SD random.

### 9.2 Politique garantissable

```text
READY = position courante
      + K detents devant le geste
      + K detents derrière (renversement)
      + continuation audio de chaque position
```

Vitesse/direction optimisent le corridor, mais la branche inverse reste prête.
Les positions capturées dans une même fenêtre peuvent être coalescées ; chaque
valeur acceptée garde son timestamp exact.

Précharger les 128 positions coarse coûte au plus 1 MiB/asset en fragments 8 KiB,
2 MiB avec continuation, et garantit le sweep normal indépendamment de sa
vitesse. Les doublons réduisent ce coût sur fichiers courts. Les 12 701 valeurs
SHIFT ne peuvent être toutes préchargées sur un long fichier : corridor et taux
certifié sont nécessaires.

```text
K >= ceil(Rencoder_certifié * Lmax) + marge_renversement
```

Tant que `Rencoder_certifié` et `Lmax` manquent, l'édition fine arbitraire ne peut
pas entrer dans l'enveloppe zéro-raté. Le sweep coarse peut y entrer via grille
entièrement résidente.

## 10. Dimensionnement

### 10.1 Flux séquentiel

| Rate | Pages/s/voix | Débit/voix | Durée page |
|---:|---:|---:|---:|
| x0,5 | 2,93 | 0,192 MB/s | 341,3 ms |
| x1 | 5,86 | 0,384 MB/s | 170,7 ms |
| x2 | 11,72 | 0,768 MB/s | 85,3 ms |
| x4 | 23,44 | 1,536 MB/s | 42,7 ms |

À 19 MB/s, plafond volumique 290 pages/s. Headroom sans aucune marge WCET,
seek, filesystem, Recorder ou waveform — donc non garanti :

| Voix | x1 | x2 | x4 |
|---:|---:|---:|---:|
| 1 | 284 pages/s | 278 | 266 |
| 4 | 266 | 243 | 196 |
| 8 | 243 | 196 | 102 |
| 16 | 196 | 102 | **-85** |

16 voix x4 sont No-Go : 24,576 MB/s > 19 MB/s. 8 x4 et 16 x2 consomment
12,288 MB/s et laissent environ 6,7 MB/s bruts avant marge.

### 10.2 RAM d'horizon x4

Payload minimal, sans branches/guards/arrondi :

| Voix | 100 ms | 250 ms | 500 ms |
|---:|---:|---:|---:|
| 1 | 0,146 MiB | 0,366 MiB | 0,732 MiB |
| 4 | 0,586 MiB | 1,465 MiB | 2,930 MiB |
| 8 | 1,172 MiB | 2,930 MiB | 5,859 MiB |
| 16 | 2,344 MiB | 5,859 MiB | 11,719 MiB |

À x1, diviser par quatre. Les 2,25 MiB mobiles actuels ne couvrent pas 8 voix x4
sur 250 ms avec guards/branches. Candidat conforme aux priorités produit :
8–12 MiB de path/hot cache dans les 23,5 MiB, laissant 15,5–11,5 MiB au
RAM/static. L'admission tranche, pas l'ancienne division 340/36.

### 10.3 Enveloppes cibles

L'enveloppe actuellement prouvée de nouvelles régions froides/s est inconnue,
car `Cjob_wc` manque. Cibles à qualifier :

| Profil | Cible | Condition dure |
|---|---|---|
| 8 voix jusqu'à x4 | START/LENGTH déterministes | débit réservé + EDF + path cache |
| 16 voix jusqu'à x2 | même flux que 8 x4 | étendre readers et prouver CPU |
| 16 voix x4 | hors contrat | débit physique insuffisant |
| 8 p-locks simultanés | tous prêts au sample | `H >= demand_bound(8 jobs)` |
| LFO locale jusqu'à 80 Hz | garantie | cycle-set résident |
| LFO large | selon cold fragments | admission WCET, pas limite arbitraire Hz |
| encoder normal | sweep entier | grille 128 positions résidente |
| encoder SHIFT | corridor | taux humain + `Lmax` certifiés |

Après mesure :

```text
R64 <= min(Bmin_résiduel/65536, capacité issue de C64_wc)
R8  <= min(Bmin_résiduel/8192,  capacité issue de C8_wc)
```

et la demand-bound doit passer dans chaque intervalle. Un p99 ou 70 % de
19 MB/s est un réglage de prototype, jamais une preuve zéro miss.

## 11. Répartition temps réel

**AUDIO IRQ** : plans READY immutables, lookup/interpolation, pas signé, wrap,
declick, checkpoint fixe. Aucune FatFs, allocation, wait, polling SD, gros memcpy
ou invalidation payload. Un miss admis est un fault contractuel.

**CONTROL/SEQ/PATH** : résoudre locks/branches/timestamps, projeter les sources
déterministes avec le noyau commun, compresser, admettre puis publier ; annuler
par génération sans toucher la fenêtre irrévocable.

**STORAGE** : EDF borné, merge, coalescence, cache/pins/eviction/maps, FatFs cold
path, I/O asynchrone et publication READY atomique ; arbitrage explicite avec
Recorder/waveform.

**DMA/IRQ** : une transaction physique, completion courte, maintenance D-cache,
aucune décision musicale ni publication partielle. Aucun RTOS, dual-core, mutex
bloquant ou allocation dynamique n'est requis.

## 12. Plan de prototype

1. Qualifier en Release avec `g_stream_rec_perf` : 4/8/16/32/64 KiB,
   séquentiel/random, fichiers fragmentés, 1/8 jobs, Recorder/waveform, cartes
   qualifiées/lente ; mesurer aussi les detents/s physiques.
2. Produire hors AUDIO une trajectoire synthétique une voix
   `{asset, frame, due_sample}` et comparer aux frames réellement lues.
3. Shadow-prefetch 64 KiB sans alimenter AUDIO ; prouver READY avant deadline sur
   5/82/31/60 %, jumps simultanés et pattern changes.
4. Comparer 4/8/16/64 KiB et 8 KiB coalescé ; mesurer transactions, CPU STORAGE,
   D-cache, amplification et map Release.
5. Ajouter EDF/demand-bound shadow et injecter SD lente + huit deadlines égales.
   Toute surcharge est refusée avant armement ; `audio_page_missing` reste zéro.
6. Une piste AUDIO opt-in, plan READY, invariant fatal de test sur miss ; vérifier
   le sample exact au traceur.
7. P-locks, puis clone LFO/Matrix/checkpoints ; aucune généralisation avant zéro
   divergence.
8. Grille encoder coarse puis corridor SHIFT ; tester vitesse et renversements.
9. Généraliser 8 puis 16 voix selon preuves débit/RAM/CPU.
10. Migrer l'UX/persistence : lire `RAM ` / `STRM`, conserver param IDs
    START/LENGTH, écrire SAMPLE + `residency_hint` non musical, ne pas réutiliser
    les anciennes clés disque.

Les étapes 1–5 ne changent pas le comportement existant.

## 13. Validation

- START 5/82/31/60 % sur 1 s, 10 s, 1 min, 10 min ;
- sample-lock + START/LENGTH, microtiming ±24, rolls/retriggers ;
- 1/4/8 voix x0,5/x1/x2/x4, 16 voix jusqu'à x2 ;
- 8/16 destinations éloignées au même sample ;
- LFO ±1 %, ±10 %, 0–100 %, 0,1/0,5/1/2/5/10/20 Hz ;
- cycle-set résident puis cache sous pression ;
- pattern switch, fill, union conditional ;
- reverse, frontières, loops 32/64/128/256 frames ;
- encoder normal, SHIFT, accélération, renversement ;
- SD lente, fichier fragmenté, Recorder + waveform ;
- retrait/corruption hors contrat sans stale payload ;
- aucune opération interdite dans IRQ, publication DMA/cache sûre ;
- `audio_page_missing == 0`, marges positives, AUDIO < 1,333 ms.

## 14. Go / No-Go final

**Oui, BRICK doit streamer le chemin plutôt que le fichier.** Le préfetch
adjacent reste l'optimisation d'un segment linéaire, plus l'architecture
principale.

**GO** pour supprimer RAM/STREAM de l'UX après cache 8 KiB coalescé, planner
fidèle, EDF/deadlines, admission WCET, 8 voix garanties, grille/corridor live et
campagne SD qualifiée. Les limites restantes sont annoncées avant usage :
voix/rate, budget de trajectoires froides, Matrix live limitée au working set,
édition SHIFT dans son corridor. Une LFO jusqu'à 80 Hz reste garantie si son
cycle-set est résident ; sinon seule compte sa demande froide admise.

**NO-GO** pour prétendre dès maintenant à l'équivalence absolue : WCET SD et
encodeur manquent, 16 voix x4 dépassent le débit historique et un événement live
arbitraire vers une zone froide au même sample est non causal. Ces limites ne
justifient pas l'ancienne UX RAM/STREAM ; elles définissent l'enveloppe à
qualifier.
