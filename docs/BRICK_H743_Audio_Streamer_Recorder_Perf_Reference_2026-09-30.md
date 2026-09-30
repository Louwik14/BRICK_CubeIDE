# BRICK H743 — Référence historique Audio / Streamer / Recorder

**Date de référence : 30 septembre 2026**  
**Plateforme : STM32H743 Cortex-M7 @ 480 MHz**  
**But du document :** conserver une trace technique des choix d'architecture, essais, régressions, mesures et optimisations réalisés sur BRICK afin de disposer d'une baseline lors du futur portage sur **i.MX RT1172**.

---

## 1. Baseline plateforme H743

### Audio

- 48 kHz stéréo.
- Bloc AUDIO : **64 frames**, soit **1,333 ms** par bloc.
- DSP interne : **FLOAT32**.
- Pas de RTOS.
- SAI piloté par DMA.
- Buffers SAI RX/TX volontairement **non-cacheables**.
- Un essai antérieur avec buffers SAI cacheables n'avait pas donné de gain significatif ; avec un D-cache write-back, TX exige un clean et RX un invalidate, ce qui annule largement l'intérêt sur des demi-buffers courts.
- Référence retenue : **SAI RX/TX non-cacheable**.

### Stockage

- SDMMC1 + DMA.
- FatFs R0.12c, exFAT, FASTSEEK.
- SDRAM FMC 64 MiB.
- Streamer et Recorder utilisent des transferts DMA asynchrones.
- Le CPU ne doit pas attendre physiquement le transfert SD dans le chemin AUDIO.

### Format audio canonique

- IEEE FLOAT32.
- Stéréo.
- 48 kHz.
- 32 bits par canal.
- 8 octets/frame.
- Header WAV canonique 512 octets avec chunk `fact`.

---

# 2. Architecture Streamer finale de référence

## 2.1 Contrat page

Taille de page H743 :

```text
64 KiB
= 65 536 octets
= 8 192 frames stéréo FLOAT32
```

Contrat runtime final :

```text
1 page READY avant démarrage

4 rôles fixes par reader :
CURRENT
NEXT
LOOP_START
LOOP_START_NEXT
```

Le Streamer est **forward-only**.

Le reverse-tail, lookbehind, profondeurs Classic/Multi différentes et caches loop spécifiques ont été supprimés.

### Pré-socle

La page préchargée avant PLAY est la même page physique que `CURRENT`.

Il ne faut donc pas dimensionner :

```text
1 page pré-socle + 4 pages runtime
```

mais bien :

```text
4 pages physiques protégées au maximum par reader
```

---

## 2.2 Budget readers

Le produit possède un budget partagé de :

```text
8 readers musicaux maximum
```

partagé entre :

- Classic ;
- Multi ;
- Classic lisant une REC_SOURCE.

Un Classic lisant une REC_SOURCE compte pour **un seul reader musical**, pas deux.

À cela s'ajoute :

```text
+ 1 reader Recorder overdub indépendant
```

Worst-case réel :

```text
8 readers musicaux
+ 1 overdub
= 9 readers actifs
```

Protection runtime :

```text
9 × 4 pages
= 36 pages
= 2,25 MiB
```

Le pool physique historique reste de **376 pages**, avec un cache global porté à environ **340 pages** après réduction de la réserve à 36 pages.

Commit associé au budget 8+1 / optimisation prefill :

```text
cf0e2b530
```

Cette passe a également remplacé un ancien scan prefill pouvant aller jusqu'à :

```text
256 samples × 376 pages
= 96 256 inspections
```

par un scan unique du pool.

---

# 3. Dé-dual-core / monocœur

Le H743 n'exécute qu'un M7, mais plusieurs couches héritées simulaient encore une architecture M7/M4.

Le travail a consisté à supprimer la plomberie inter-core inutile tout en conservant les vraies frontières concurrentes :

```text
AUDIO IRQ
CONTROL / SEQ
STORAGE superloop
DMA IRQ
```

À conserver même en monocœur :

- SPSC head/tail ;
- DMB de publication ;
- tokens ;
- generations ;
- media epoch ;
- page states ;
- DMA completion ;
- protections contre recycle pendant usage AUDIO.

À supprimer lorsqu'elles ne servent qu'au dual-core :

- mailboxes inter-MCU ;
- ABI inter-core ;
- copies de manager inutiles ;
- clean/invalidate uniquement destinés au partage M7/M4.

Commit CONTROL→AUDIO :

```text
b33239ab7
refactor(control-audio): localize M7 publication path
```

Le principe reste valable pour RT1172 : **monocœur ne veut pas dire absence de concurrence**, car l'IRQ AUDIO peut préempter STORAGE.

---

# 4. Recorder — migration PCM24 → FLOAT32

## 4.1 Ancien chemin

Historique :

```text
FLOAT DSP
→ conversion PCM24
→ ring PCM24
→ fichier PCM24

REC_SOURCE :
PCM24
→ scratch
→ conversion FLOAT32
→ Streamer
```

Mesures de référence :

```text
AUDIO total Recorder             ~0,816 % MCU
FLOAT→PCM24                      ~0,426 %
peak meter                       ~0,113 %
ring copy                        ~0,207 %
rec_service                      ~1,36 %

REC_SOURCE scratch copy          ~706 µs/page
REC_SOURCE PCM24→FLOAT           ~767 µs/page
total REC_SOURCE conversion      ~1,47 ms/page
```

---

## 4.2 Première migration FLOAT32

Commit :

```text
e597bb85c
recorder: write native float32 audio
```

Le format fichier est devenu FLOAT32 natif.

Cette première version a supprimé la conversion PCM24 mais introduit une mauvaise implémentation de `generic_recorder_pack()` :

- modulo 64 bits par frame ;
- `memcpy(..., 8)` par frame ;
- batching conservé selon l'ancien format byte-oriented.

Résultat :

```text
AUDIO Recorder                  ~0,706 %
rec_service                     ~2,45 %
rec_pack                        ~319 µs/appel
```

Conclusion : **la conversion FLOAT32 était bonne, le packing ne l'était pas**.

---

## 4.3 Recorder final : ring FLOAT32 → DMA direct

Commit :

```text
c0e415ccd
recorder: stream float32 ring directly to SD
```

Architecture finale :

```text
AUDIO
→ ring FLOAT32 cacheable
→ STORAGE réserve portion stable
→ clean CPU→DMA
→ SD DMA
→ completion
```

Suppression :

- buffer intermédiaire 64 KiB ;
- copie STORAGE principale ;
- pack per-frame ;
- ancienne logique byte-oriented.

Seule la dernière portion partielle d'un secteur peut nécessiter une petite copie, maximum 512 octets.

Paramètres :

```text
ring                 12 032 frames
batch nominal        32 KiB
                     = 4 096 frames
                     = 64 secteurs de 512 B
```

Mesures finales :

```text
AUDIO Recorder                  ~0,452 % MCU
rec_service                     ~0,98 %
rec_pack                        ~0,34 µs/appel

écritures sur ~69,5 s :
ancien                          ~2 539
final                           ~845
```

Gains :

```text
AUDIO Recorder :
0,816 → 0,452 %
≈ -44,6 %

rec_service :
1,36 → 0,98 %
≈ -27,9 %

nombre d'écritures :
2 539 → 845
≈ -66,7 %
```

Gain SDRAM statique :

```text
64 776 octets
```

---

# 5. Streamer — évolution et mesures

## 5.1 Unification du contrat

Commit :

```text
a9d431761
refactor(streamer): unify forward page contract
```

Classic, Multi et REC_SOURCE partagent désormais le même contrat :

```text
CURRENT
NEXT
LOOP_START
LOOP_START_NEXT
```

Telemetry corrigée ensuite :

```text
cef383967
fix(streamer): reconnect page telemetry to load lifecycle
```

---

## 5.2 Coûts historiques avant les dernières optimisations

Ordres de grandeur observés pendant les audits précédents :

```text
reader_need              ~0,150 µs/appel
reader_lease             ~0,248 µs
reader_resolve           ~1,73 µs

manager_pick             ~18,1 µs/page
stream_io_begin          ~17,8 µs/page
stream_read_start        ~38–39 µs/page
```

Le transfert SD physique 64 KiB restait autour de :

```text
~3,15–3,25 ms/page
```

Ce coût est **asynchrone** et ne représente pas du temps CPU bloqué.

---

# 6. Régression cache_reserve révélée par le pool 36 pages

Après réduction du budget runtime à 36 pages, un ancien mauvais worst-case est devenu visible.

Mesure :

```text
cache_reserve
~396 µs moyen
~774 µs max
```

Cause :

```text
pour chaque candidate READY :
    rescanner les leases

36 candidates × 45 lease slots
+ validation finale
```

Worst-case logique :

```text
≈ 1 665 lectures partagées
```

Ce coût existait déjà mais était masqué lorsque le pool runtime était beaucoup plus grand et recyclait moins souvent.

---

## 6.1 Snapshot compact des readers actifs

Commit :

```text
b1a6a7d41
fix(streamer): bound cache reserve lease scans
```

Nouvelle stratégie :

```text
snapshot ≤9 readers actifs une fois
→ comparaisons locales pour les candidates
→ validation live finale seulement
```

Worst-case partagé réduit à environ :

```text
≤18 lectures live de leases
```

plus des comparaisons locales bornées.

Mesures observées après correction :

```text
cache_reserve
~34,5 µs moyen sur un run
~42 µs moyen sur un autre run
```

La variation dépend du workload.

Le point important est le passage de plusieurs centaines de microsecondes à quelques dizaines.

---

# 7. D-cache Streamer — clean-by-construction

## 7.1 Ancien chemin

Avant optimisation, une lecture physique pouvait faire :

```text
pré-invalidate destination
→ SD DMA écrit
→ post-invalidate
```

Le pré-invalidate était coûteux sur 64 KiB.

L'audit a établi qu'on pouvait imposer l'invariant :

```text
toute page éligible au Streamer est CPU-clean avant réutilisation DMA
```

Les writers CPU sont donc responsables de rendre la page clean lorsqu'ils terminent leur travail.

---

## 7.2 Nouveau contrat

Commit :

```text
44bd12ca3
Optimize Streamer DMA cache ownership
```

Chemin physique Streamer :

```text
destination prouvée CPU-clean
→ pas de pré-invalidate
→ DMA RX
→ post-invalidate obligatoire
→ publication READY
```

Le **post-invalidate reste indispensable** : CPU-clean ne veut pas dire CPU-fresh après qu'un DMA a réécrit la RAM.

Mesure clé :

```text
stream_read_start
~36,5 µs/page
→ ~3,2 µs/page
```

Gain :

```text
≈ -91 %
≈ 33 µs/page de CPU supprimés
```

Le DMA physique est resté autour de :

```text
~3,25 ms/page
```

Donc le gain vient bien du CPU/cache, pas du débit SD.

---

# 8. Compactage des commandes I/O, extents et hash

Commit :

```text
7228f8024
perf(streamer): compact I/O and bound cache lookups
```

## 8.1 Commandes I/O

Supprimé du chemin physique nominal :

```text
path[160]
wav_info_t complet
sample_page_stream_info_t
```

Nouveaux ordres de grandeur :

```text
snapshot             128 o
commande              196 o
job async             448 o
```

Deux jobs async :

```text
2 × 448 o = 896 o
```

déplacés en SRAM D2 interne.

Le `path` et les grosses métadonnées WAV restent uniquement dans le fallback FatFs.

---

## 8.2 Extents

Ajout d'un répertoire direct :

```text
16 index uint16_t
```

par map, afin de rendre :

```text
block_index → bloc d'extents
```

O(1).

Coût RAM :

```text
+32 octets/map
≈ 24 704 octets maximum pour 772 registrations
```

Cette optimisation vise surtout le **worst-case fragmenté**, pas nécessairement le fichier contigu nominal.

---

## 8.3 Hash

Ancienne suppression :

```text
open addressing + tombstones
```

Nouvelle suppression :

```text
backward-shift
```

Sans malloc, sans rebuild global et sans accumulation de tombstones.

Worst-case miss théorique indiqué par l'audit :

```text
avant : 752 probes
après : 377 probes max
```

---

## 8.4 Mesure avant/après

Repères juste avant cette passe :

```text
cache_reserve          ~42 µs/page
stream_submit          ~12,7 µs
stream_io_begin        ~12,4 µs
manager_pick           ~8,4 µs
stream_read_start      ~3,2 µs
stream_io_finalize     ~2,9 µs
```

Mesure sur `7228f8024` :

```text
reader_need             0,144 µs
reader_lease            0,225 µs
reader_resolve          1,369 µs

manager_pick            8,727 µs
cache_reserve          40,522 µs
cache_recycle           3,510 µs

stream_submit           7,424 µs
stream_io_begin         7,176 µs
stream_io_finalize      1,910 µs
stream_read_start       3,316 µs
```

Gain principal :

```text
stream_submit
12,7 → 7,42 µs
≈ -42 %

stream_io_begin
12,4 → 7,18 µs
≈ -42 %

stream_io_finalize
2,9 → 1,91 µs
≈ -34 %
```

`stream_io_begin` étant imbriqué dans `stream_submit`, les deux gains ne doivent pas être additionnés.

Gain CPU réellement supprimé sur les postes indépendants observables :

```text
~5,3 µs/page sur submit
+ ~1 µs/page sur finalize
≈ ~6 µs/page
```

---

# 9. Expérience rejetée : first-fit + refs physiques AUDIO

Commit expérimental :

```text
d7501c024
perf(streamer): use bitmap recycle and physical windows
```

Cette passe combinait :

- bitmap FREE ;
- bitmap READY ;
- first-fit circulaire ;
- reader window physique ;
- `slot + generation + resolved` publiés depuis AUDIO ;
- absorption du lease dans la fenêtre physique.

L'idée était de rapprocher le Streamer d'AUDIO.

---

## 9.1 Résultat CPU

Mesure :

```text
reader_need             0,467 µs
reader_lease            0,612 µs
reader_resolve          2,645 µs

manager_pick           10,301 µs
cache_reserve          29,489 µs
cache_recycle          14,248 µs

stream_submit           8,748 µs
stream_io_begin         8,517 µs
stream_io_finalize      3,401 µs
stream_read_start       3,215 µs
```

Par rapport à `7228f8024` :

```text
cache_reserve :
40,52 → 29,49 µs
gain local

mais :

reader_need :
0,144 → 0,467 µs
≈ ×3,25

reader_resolve :
1,369 → 2,645 µs
≈ ×1,93

cache_recycle :
3,51 → 14,25 µs
≈ ×4,1

manager_pick :
8,73 → 10,30 µs
```

---

## 9.2 Churn observé

Sur deux captures qui n'avaient pas la même durée :

```text
7228f8024 :
174 pages requested
138 recycles

d7501c024 :
205 pages requested
169 recycles
```

Le nombre total de lectures a donc augmenté de :

```text
+31 pages
+17,8 %
```

et les recyclages de :

```text
+31
+22,5 %
```

Les ratios par seconde étaient encore plus élevés à cause de la durée de capture différente ; ils ne doivent pas être confondus avec une multiplication réelle du nombre total de pages.

L'audit code a montré :

- pas de double-load démontré ;
- pas de trou fonctionnel de protection ;
- les 31 pages supplémentaires correspondent à des pages évincées puis redemandées.

Cause principale :

```text
first-fit circulaire
```

Il remplaçait le minimum global de `last_touch`.

`last_touch` n'était pas un vrai LRU AUDIO, mais constituait tout de même un **FIFO temporel exact basé sur l'âge de réservation**.

Une page récemment sortie de la fenêtre pouvait devenir rapidement victime avec first-fit, notamment dans les loops.

---

## 9.3 Pourquoi Option B coûtait plus cher

La fenêtre physique faisait environ 100 octets par reader publié.

La validation recycle devait reconstruire :

- jusqu'à 9 fenêtres ;
- jusqu'à 36 rôles ;
- bitmap physique ;
- listes resolved/unresolved ;
- validations slot/generation.

Avant, le snapshot logique était beaucoup plus compact.

La première acquisition AUDIO faisait en pratique :

```text
lookup hash
→ descriptor
→ publication ref physique
→ revalidation slot/generation
→ seconde résolution directe
```

Le lookup initial n'avait donc pas disparu ; une seconde validation avait été ajoutée.

Conclusion :

> **Rapprocher le Streamer d'AUDIO via des refs physiques persistantes n'a pas apporté de gain net sur H743.**

À ne pas reproduire automatiquement sur RT1172.

---

# 10. Architecture recycle finale

Commit final de correction :

```text
b7558079e
fix(streamer): retain temporal cache policy
```

Conservé :

```text
bitmap FREE
bitmap READY
FREE O(1) par bit-scan
transitions d'état centralisées
hash backward-shift
réservation + cible fusionnées
```

Retiré :

```text
first-fit victime
reader windows physiques
snapshot physique enrichi
double résolution reader
```

Lease final :

```text
key
epoch
4 × page_index
valid_mask
```

publication seqlock compacte.

Recycle final :

```text
READY bitmap
→ filtrage leases
→ minimum last_touch
→ EVICTING
→ validation live
→ recycle
```

RAM :

```text
lease publié :
4 500 → 1 620 octets

D2 cacheable :
106 720 → 103 840 octets

DTCM :
127 136 → 127 712 octets
```

Le petit surcoût DTCM vient du retour de l'état logique local reader.

---

## 10.1 Mesure finale `b7558079e`

```text
reader_need             0,154 µs
reader_lease            0,203 µs
reader_resolve          1,383 µs

manager_pick            7,956 µs
manager_finish          1,275 µs

cache_reserve          38,109 µs
cache_recycle           3,705 µs

stream_command          2,616 µs
stream_submit           7,360 µs
stream_io_begin         7,114 µs
stream_io_finalize      1,921 µs

stream_read_start       3,370 µs
stream_read_complete    0,412 µs
stream_dma_launch       1,247 µs
```

Volume :

```text
167 pages requested
167 pages ready
131 recycles
```

Ce volume est revenu dans la zone de la baseline `7228f8024` :

```text
174 pages
138 recycles
```

La régression de churn de `d7501c024` a donc disparu.

---

## 10.2 Gain final du chantier recycle par rapport à `7228f8024`

```text
cache_reserve :
40,522 → 38,109 µs
≈ -6,0 %

manager_pick :
8,727 → 7,956 µs
≈ -8,8 %

reader_need :
0,144 → 0,154 µs
≈ équivalent

reader_resolve :
1,369 → 1,383 µs
≈ équivalent

cache_recycle :
3,510 → 3,705 µs
≈ équivalent
```

Conclusion :

> Le chantier bitmap/recycle apporte un **petit gain net**, mais le gros gain espéré de 10–20 µs sur `cache_reserve` n'a pas été atteint.

Le coût restant vient essentiellement de :

```text
snapshot protections
→ parcourir READY
→ tester protection
→ chercher min(last_touch)
→ EVICTING
→ validation live
```

À 36 pages, une structure plus complexe de type heap/queue n'est pas justifiée à ce stade.

---

# 11. Maxima observés — prudence

Sur `7228f8024` :

```text
cache_reserve max       ~308 µs
```

Sur `b7558079e` :

```text
cache_reserve max       ~57,6 µs
```

Mais les maxima DWT peuvent inclure une préemption/IRQ tombant au milieu du span.

La moyenne est donc plus fiable pour comparer le coût intrinsèque d'un chemin.

---

# 12. D-cache global — décisions utiles pour AUDIO

L'audit D-cache global a confirmé que les principaux parcours gratuits du Streamer avaient déjà été retirés.

## 12.1 Sampler RAM

Le loader RAM conserve encore des maintenances D-cache optimisables :

```text
allocation
→ pré-invalidate
→ DMA
→ post-invalidate
→ clean sample entier
→ invalidate sample entier
```

Un contrat DMA-only pourrait retirer plusieurs parcours.

Gain estimé par l'audit :

```text
~1,44–1,87 ms/MiB au chargement
```

Mais ce gain concerne **le chargement**, pas le playback continu.

Il n'est donc pas prioritaire si le seul objectif est davantage de budget DSP musical.

---

## 12.2 Shifter — meilleur candidat AUDIO continu

Buffers identifiés :

```text
3 × 8 192 floats
≈ 96 KiB
```

Placement actuel :

```text
SDRAM non-cacheable
```

Usage :

```text
CPU DSP uniquement
aucun DMA
```

C'est actuellement le meilleur candidat évident pour une reclassification cacheable ou une réorganisation mémoire.

### Mesure AUDIO de référence

Scénario utilisateur :

```text
Streamer sans shifter :
~4,8 % IRQ AUDIO

Streamer + shifter à -12 st :
~6,8 % IRQ AUDIO
```

Coût approximatif attribuable au shifter :

```text
+2 points d'IRQ AUDIO
```

Cette mesure doit servir de baseline avant toute modification du shifter.

Le gain éventuel du cache ne doit pas être déduit du coût d'un invalidate Streamer ; il dépend de la localité réelle des accès du DSP.

À auditer / mesurer :

- working set chaud ;
- lectures/écritures par sample ;
- interpolation ;
- wrap/modulo ;
- layout ;
- localité ;
- pression sur D-cache ;
- éventuelle séparation état chaud / gros delay.

---

# 13. SAI : décision de référence

Les buffers SAI RX/TX restent non-cacheables.

Raison :

```text
TX cacheable write-back :
CPU écrit
→ data peut rester dirty dans D-cache
→ DMA lit RAM ancienne
→ clean nécessaire

RX cacheable :
DMA écrit RAM
→ CPU peut conserver une ligne stale
→ invalidate nécessaire
```

Sur les petits demi-buffers SAI, le coût de maintenance gomme le gain attendu.

Conclusion H743 :

```text
SAI RX/TX non-cacheable
```

est le choix de référence.

Sur RT1172, ne changer ce choix qu'après mesure avec son architecture de cache/bus propre.

---

# 14. Mesure IRQ AUDIO globale à interpréter avec prudence

Une observation séparée a donné :

```text
avant un reflash/build :
~10,8 % IRQ AUDIO

après :
~13 %
```

Le reflash a reproduit environ 13 %, donc cette différence n'a pas été attribuée au patch Streamer concerné.

Elle ne doit pas être utilisée comme preuve de régression/gain d'un commit précis.

Pour les comparaisons RT1172, toujours reproduire :

- même firmware ;
- même scénario musical ;
- même durée ;
- mêmes voix ;
- mêmes FX ;
- même pitch ;
- même instrumentation.

---

# 15. Page size : 64 KiB H743 vs futur RT1172

Le H743 de référence utilise :

```text
64 KiB/page
```

Pour le RT1172, une page de **32 KiB** reste une option à benchmarker.

Avec FLOAT32 stéréo :

```text
64 KiB :
8 192 frames
170,7 ms à ×1
21,3 ms à ×8

32 KiB :
4 096 frames
85,3 ms à ×1
10,7 ms à ×8
```

Fenêtre 4 pages :

```text
64 KiB :
256 KiB / reader

32 KiB :
128 KiB / reader
```

Pour 9 readers :

```text
64 KiB :
2,25 MiB de protection runtime

32 KiB :
1,125 MiB
```

Mais 32 KiB implique environ :

```text
×2 pages/s
×2 transitions
×2 scheduler/manager/hash par quantité audio identique
```

Le fait qu'une page corresponde ou non à la taille du D-cache **n'est pas à lui seul une raison de choisir cette taille**.

Décision RT1172 :

> Benchmark 32 KiB vs 64 KiB selon débit SD réel, overhead par transaction et pression SDRAM.

---

# 16. Ce qui a réellement rapporté le plus

Ordre qualitatif observé sur H743 :

## Très gros gains

### 1. Suppression du pré-invalidate Streamer

```text
stream_read_start
~36,5 → ~3,2 µs/page
≈ -91 %
```

### 2. Correction du cache_reserve quadratique

```text
~396 µs
→ quelques dizaines de µs
```

### 3. Recorder FLOAT32 direct ring→DMA

```text
AUDIO Recorder
~0,816 → ~0,452 % MCU

rec_service
~1,36 → ~0,98 %
```

## Gains intermédiaires

### 4. Compactage I/O

```text
stream_submit
~12,7 → ~7,4 µs

io_finalize
~2,9 → ~1,9 µs
```

### 5. Dé-dual-core

Réduction de plusieurs scans/copies/barrières inutiles et architecture plus simple.

## Petit gain final

### 6. Bitmaps recycle conservés avec min(last_touch)

```text
cache_reserve
40,5 → 38,1 µs

manager_pick
8,73 → 7,96 µs
```

---

# 17. Fausses bonnes idées / essais à ne pas répéter sans raison

## First-fit circulaire

Avantage théorique :

```text
moins de descriptors inspectés
```

Résultat réel :

- rétention temporelle dégradée ;
- pages de loop évincées trop tôt ;
- +31 lectures et +31 recyclages sur les captures comparées.

Verdict :

```text
NE PAS utiliser first-fit pur
tant que la fenêtre runtime reste seulement 4 pages
```

## Refs physiques persistantes publiées par AUDIO — Option B

Avantage théorique :

```text
moins de hash lookup
protection physique directe
```

Résultat réel :

- fenêtres beaucoup plus grosses ;
- snapshots plus coûteux ;
- double résolution ;
- reader_need/resolve plus lents ;
- cache_recycle beaucoup plus lent.

Verdict :

```text
garder AUDIO logique et compact
garder cache/I/O owner STORAGE
```

---

# 18. Architecture finale recommandée pour servir de point de départ RT1172

```text
AUDIO IRQ
│
├─ reader logique compact
│   ├ CURRENT
│   ├ NEXT
│   ├ LOOP_START
│   └ LOOP_START_NEXT
│
├─ lecture des pages READY
└─ publication seqlock des besoins

        ↓

STORAGE
│
├─ manager
├─ page cache
│   ├ FREE bitmap
│   ├ READY bitmap
│   ├ last_touch temporel
│   └ hash sans tombstones
│
├─ mapping extent O(1) par block directory
├─ scheduler SD
├─ cache maintenance CPU↔DMA
└─ DMA async

        ↓

DMA / SDMMC
│
└─ transfert physique puis completion
```

Owners :

```text
AUDIO :
besoin logique et consommation

STORAGE :
cache, allocation, recycle, mapping, I/O

DMA :
payload LOADING pendant transfert
```

---

# 19. Checklist de baseline à reproduire sur RT1172

Avant toute optimisation spécifique RT1172, reproduire des mesures comparables.

## AUDIO

1. IRQ AUDIO idle / baseline.
2. Streamer sans shifter.
3. Streamer + shifter `-12 st`.
4. Même nombre de voix qu'au benchmark H743.
5. Cas pitch élevé.
6. Cas loop.
7. Recorder actif.
8. Overdub actif si pertinent.

## Streamer CPU

Mesurer au minimum :

```text
reader_need
reader_lease
reader_resolve
manager_pick
manager_finish
cache_reserve
cache_recycle
stream_command
stream_submit
stream_io_begin
stream_io_finalize
stream_read_start
stream_read_complete
stream_dma_launch
```

## Compteurs

```text
pages_requested
pages_ready
cache_hit
cache_miss
cache_alloc
cache_recycle
cache_alloc_fail
page_failed
audio_page_missing
reads
read_bytes
```

## Physique SD

Mesurer séparément :

```text
request→ready
submit→DMA
DMA physique
DMA→finalize
```

Ne jamais confondre :

```text
temps CPU
```

avec :

```text
latence DMA asynchrone
```

---

# 20. Instrumentation H743 de référence

Structure :

```text
g_stream_rec_perf
version 3
816 octets
204 words
CPU = 480 MHz
magic = BRPF
```

Important :

- les adresses changent avec le build ;
- toujours refaire `info address` ;
- les fonctions Thumb sont appelées à `adresse + 1`.

Séquence type :

```gdb
shell cls
info address g_stream_rec_perf
info address brick_perf_diag_reset
info address brick_perf_diag_snapshot
```

Puis :

```gdb
call ((void (*)(void))RESET_THUMB)()
continue
```

Après STOP transport et Ctrl+C :

```gdb
call ((void (*)(void))SNAPSHOT_THUMB)()
x/204wx PERF_ADDRESS
```

Les spans imbriqués ne doivent pas être additionnés naïvement.

---

# 21. Commits de référence

## Recorder

```text
e597bb85c
recorder: write native float32 audio

c0e415ccd
recorder: stream float32 ring directly to SD
```

## Streamer

```text
a9d431761
refactor(streamer): unify forward page contract

cef383967
fix(streamer): reconnect page telemetry to load lifecycle

cf0e2b530
budget 8+1 readers / prefill scan optimisé

b1a6a7d41
fix(streamer): bound cache reserve lease scans

44bd12ca3
Optimize Streamer DMA cache ownership

7228f8024
perf(streamer): compact I/O and bound cache lookups

d7501c024
perf(streamer): use bitmap recycle and physical windows
→ EXPÉRIENCE PARTIELLEMENT REJETÉE

b7558079e
fix(streamer): retain temporal cache policy
→ BASELINE FINALE DE CE DOCUMENT
```

## Publication CONTROL→AUDIO

```text
b33239ab7
refactor(control-audio): localize M7 publication path
```

---

# 22. Conclusion pour le portage RT1172

La principale leçon du H743 n'est pas seulement « le RT1172 sera plus rapide ».

Les gains les plus importants ont été obtenus en supprimant les **mauvaises architectures de travail**, pas uniquement grâce à davantage de MHz :

```text
éviter les parcours D-cache inutiles
éviter les scans quadratiques
garder des owners simples
ne pas publier de gros états physiques si le besoin logique suffit
éviter les copies de contexte I/O
garder les DMA asynchrones
dimensionner le cache selon le vrai nombre de readers actifs
```

Pour le RT1172, il faut donc conserver d'abord ces invariants, puis re-benchmarker :

```text
page size
débit SD
cacheability SDRAM
coût du shifter
placement des petits états DSP chauds
```

Le prochain gros candidat H743 encore non exploité pour le **rendu musical continu** est le shifter :

```text
~96 KiB de delay buffers CPU-only
actuellement SDRAM non-cacheable
coût mesuré approximatif :
+2 points d'IRQ AUDIO à -12 st
```

C'est le premier test mémoire/D-cache intéressant à reprendre avant ou pendant la migration RT1172.

---

**Baseline finale Streamer H743 de référence : commit `b7558079e`**  
**Dernière grosse passe I/O de référence : commit `7228f8024`**  
**Contrat D-cache Streamer de référence : commit `44bd12ca3`**
