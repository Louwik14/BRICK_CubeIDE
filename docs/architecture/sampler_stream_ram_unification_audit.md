# Audit d'architecture - unification Sampler RAM / Stream

Date de l'audit : 7 octobre 2026. Cible materielle et build de reference :
STM32H743, configuration `Release`. Le code courant est l'autorite; les mesures
historiques sont identifiees comme telles. Cette passe ne change aucun runtime.

## 1. Verdict court

La fusion UX `RAM / STREAM -> SAMPLE` est realiste, a condition de ne pas
promettre que tout saut froid est sample-accurate. L'architecture actuelle est
deja proche du bon socle : cache global cle par asset/page, leases AUDIO,
chargement STORAGE asynchrone, pages FLOAT32 finales et huit credits musicaux.
Ce qui manque est surtout une admission de destination avant le NOTE, une
seconde tete de transition et une vraie politique de cache chaud/lookahead.

La cible raisonnable est :

- START/END/LENGTH statiques et p-lockes robustes, y compris des sauts arbitraires
  dans des fichiers de plusieurs minutes, quand CONTROL a pu annoncer le trig;
- edition en lecture immediate si la destination est READY, sinon maintien de
  l'ancien son puis crossfade des que la destination est prete;
- modulation lente robuste, typiquement jusqu'a quelques changements de page
  par seconde et par voix; quelques hertz sont plausibles avec bornage global;
- modulation rapide seulement si elle reste dans des pages deja residentes;
- audio-rate sur une position de fichier arbitraire : No-Go. A 48 kHz, le pire
  cas demanderait 48 000 destinations/s, soit 3,1 GB/s en pages de 64 KiB avant
  meme les seeks SD;
- reverse robuste seulement apres ajout d'un reader bidirectionnel et d'un
  prefetch symetrique; le Streamer actuel est exclusivement forward.

La recommandation est une combinaison des architectures 1, 2/7, 3/5 et 4,
avec promotion RAM automatique (architecture 6) comme optimisation secondaire,
pas comme condition de correction.

## 2. Etat actuel

### 2.1 Autorites et chemins d'execution

```text
UI / SEQ p-lock / Matrix
        |
        v
CONTROL: tone_program_control + param_registry + scheduler date
        |
        v  FIFO PARAM/NOTE unique
AUDIO IRQ: param backend -> sampler runtime -> voice reader -> mixer
        |                                      |
        | lease {key, epoch, 4 roles}           | lookup READY uniquement
        v                                      v
STORAGE cooperatif: stream manager -> page cache -> stream I/O -> SD scheduler
                                                   |
                                                   v
                                                SDMMC DMA/IRQ
```

Responsabilites actuelles :

- CONTROL possede les valeurs, les p-locks, l'admission produit, les references
  d'assets et les commandes datees;
- AUDIO possede voix, playheads, interpolation, rendu, leases et declick;
- STORAGE possede metadonnees/cache/pages, maps physiques, FatFs et I/O;
- DMA/IRQ SD ne publie qu'une completion physique; il ne decide aucune action
  musicale.

Fichiers structurants : `tone_program_control.c`, `param_registry_backends.c`,
`track_runtime.c`, `brick6_sampler_runtime.c` et ses `.inc`,
`sample_voice_reader.c`, `sample_stream_manager.c`, `sample_page_cache.c`,
`sample_stream_io.c`, `sample_stream_backend_physical.c`,
`sd_scheduler_runtime.c` et `sd_block_device.c`.

### 2.2 Catalogue, import et index

`sample_global_pool` est le catalogue produit commun, mais les kinds restent
visibles : `CLASSIC` (Stream), `RAM`, `MULTI`, `WAVETABLE`. Le budget catalogue
est celui des 340 pages de slot-pool, soit 21,25 MiB; le page-cache physique
complet fait 376 pages, 23,5 MiB.

- Stream Classic : `sample_global_pool_load_classic()` -> `sample_cache_prepare()`
  parse le WAV, exige le format canonique FLOAT32 48 kHz, construit la map
  physique, enregistre le stream et reserve la page 0. La projection AUDIO est
  publiee par `sample_classic_audio_projection_control.c` lorsque cette page est
  READY.
- RAM : `sampler_ram_pool_load_async_begin()` alloue un run contigu dans le
  slot-pool, puis lit directement le FLOAT32 sous budget cooperatif de 2 ms avec
  quanta 4/16 KiB. `sampler_ram_audio_projection_control.c` publie ensuite un
  pointeur local stable et une generation.
- Multi : `multi_sample_import.c` scanne/convertit les WAV puis produit l'index
  v4 via `multi_sample_index.c`. `multi_sample_loader.c` applique l'index au
  pool, enregistre chaque sample dans le page-cache et prepare sa page 0.
  `multi_sample_pool_resolve_source()` et la projection AUDIO resolvent ensuite
  note/velocity vers zone, root note, boucle et sample.
- Pages Settings : `ui_settings_sample_assets.inc` charge Classic;
  `ui_settings_multi_assets.inc` orchestre import/index/load Multi. Les pages
  TONE sont distinctes dans `ui_page_template_tone.c` : RAM expose
  START/LENGTH/MODE/TUNE/LOOP START, Stream expose BPM/launch/loop/stretch/pitch,
  Multi gain/loop.

La persistence distingue encore `PERSIST_ASSET_SAMPLE_RAM` et
`PERSIST_ASSET_SAMPLE_STREAM`, ainsi que `TRACK_TYPE_RAM`, `TRACK_TYPE_STREAM`
et leurs DTO TONE separes (`persistent_control_*`, `project_control.c`).

### 2.3 Sampler RAM

Au NOTE, `brick6_sampler_runtime_trigger_ram()` resout le descriptor RAM,
calcule `region_begin`, `region_end`, `loop_begin`, direction et pas Q16, puis
lit directement le tableau resident. START est `start * frame_count`. LENGTH
est deja semantiquement une abstraction de END :

```text
END = min(frame_count, START + max(1, length * frame_count))
```

`brick6_sampler_runtime_reconcile_ram_voice_bounds_live()` reapplique
START/LENGTH/LOOP START pendant une voix active. Si la nouvelle region exclut la
tete, la voix cree une tail de 16 samples, recale la tete et effectue un fade-in
de 16 samples. `sampler_ram_voice.inc` couvre forward, reverse, loop et ping-pong,
pitche ou non. `set_tune()` reprojette le pas en direct.

Une piste RAM porte une voix principale; un nouveau NOTE/retrigger remplace
cette voix avec le declick 16 samples. Changer de sample actif fait de meme puis
retire le pointeur. Aucun I/O n'est necessaire au geste : toutes les positions
sont immediatement adressables.

### 2.4 Stream Classic

Au NOTE, `brick6_sampler_runtime_clip_start_playback()` resout la projection,
construit un `sample_play_plan_t`, publie un lease puis appelle
`sample_voice_reader_bind_musical_play_plan()`. Le bind exige que la page de
depart soit deja READY; sinon il echoue immediatement. En lecture, le reader
rend la page courante, interpole lineairement si le pas differe de 1, publie ses
nouveaux besoins et stoppe la voix au premier segment non READY/underrun.

START/LENGTH ne font pas partie du catalogue Stream actuel. Le backend generique
sait appeler `set_start()`/`set_length()`, mais `tone_param_catalog.h` les rend
inapplicables a `TRACK_RUNTIME_TYPE_STREAM`, le DTO Stream ne les stocke pas et
le plan Stream ignore les champs RAM. C'est la cause produit directe de la
divergence, en plus de la contrainte physique de page READY.

Le Stream actuel est forward-only. `sample_play_plan.h` ne declare que
`SAMPLE_PLAY_LOOP_FORWARD` et deux kernels forward. Reverse/ping-pong sont
explicitement reserves a RAM dans `stream_need_contract.md`.

Changer le sample Stream pendant la lecture appelle
`brick6_sampler_runtime_clip_stop_playback()`, conserve seulement la tail
declick 16 samples, change l'ID, et attend un nouveau trigger. Il n'existe ni
ancienne/nouvelle fenetre ni crossfade de deux sources.

### 2.5 Multi

Multi reutilise exactement le reader et le cache Stream. Le plan vient de la
zone resolue, applique root-note/pitch, region et boucle SMPL. Il dispose de
huit voix physiques globales. `brick6_sampler_runtime_multi_alloc_voice()`
respecte d'abord la limite par piste, vole la plus ancienne voix en release puis
la plus ancienne held, avec tail de 16 samples. Chaque voix Multi a son lease.

Multi ne propose actuellement aucun START/END/LENGTH utilisateur. Sa pertinence
pour la cible est forte : il prouve deja que plusieurs voix, fichiers et boucles
peuvent partager le meme cache et le meme ordonnanceur.

### 2.6 Fenetre, cache et politique de prefetch actuels

Geometrie exacte :

| Element | Valeur actuelle |
|---|---:|
| Page physique | 64 KiB |
| Stereo FLOAT32 | 8 192 frames/page |
| Mono FLOAT32 | 16 384 frames/page, mais le presocle produit Stream est stereo |
| Cache total | 376 pages = 23,5 MiB |
| Slot-pool/catalogue | 340 pages = 21,25 MiB |
| Reserve runtime | 36 pages = 2,25 MiB |
| Credits musicaux Stream+Multi | 8 |
| Reader overdub garanti | 1 |
| Roles par lease | 4 |

Les quatre roles sont `CURRENT`, `NEXT`, `LOOP_START`, `LOOP_START_NEXT`.
Pour une voix non bouclee, seuls CURRENT et NEXT sont normalement valides :
zero page derriere la tete et une page devant. Pour une boucle, deux pages
supplementaires protegent le debut de boucle. Les doublons de roles partagent
la meme page physique. Il ne s'agit donc pas d'une fenetre lineaire quatre
pages autour de la tete.

AUDIO publie le besoin; STORAGE ne predit rien. `sample_stream_manager` parcourt
les leases actifs en round-robin, dans l'ordre des roles, une page candidate par
slot/passe. Il n'existe ni deadline derivee du pitch, ni horizon musical, ni
priorite selon le temps avant NOTE. Le cache est deja global et cle par
`{domain, object_id, page_index}`; deux voix sur la meme page la partagent.
Cependant les pages mobiles sont cantonnees au pool de 36 pages, avec eviction
LRU approximative par `last_touch`; les 340 pages restantes servent au
presocle/static et aux allocations RAM/Wavetable. Ce n'est pas encore un cache
global chaud librement exploitable pour des destinations futures.

Quand la tete franchit une page, le lease devient CURRENT=ancienne NEXT et
NEXT=page suivante. Si CURRENT n'est pas READY, le rendu stoppe la voix; aucune
attente audio, aucun zero-fill poursuivi et aucun retry musical. Une boucle
revient par `sample_voice_reader_seek()` sur LOOP_START. Si ses pages protegees
sont READY, le retour ne touche pas la SD; sinon la voix stoppe.

Le code accepte deux slots `sample_stream_io` et deux requetes physiques
pendantes pour le chainage N+1, tandis que `sample_stream_limits.h` conserve un
`TARGET_MAX_IO_IN_FLIGHT` a 1 et la documentation de domaine parle d'une I/O.
Le materiel SD reste serialise par le scheduler. Cette divergence de vocabulaire
doit etre tranchee avant d'ajouter des files prioritaires : capacite queuee = 2,
transaction SD simultanee = 1.

### 2.7 Mesures et marge

La mesure historique H743/50 MHz donne environ 19 MB/s transactionnels et
3,15-3,25 ms pour 64 KiB. La valeur 2,58 ms/page concerne l'ancien payload PCM24
48 KiB; elle ne doit pas etre reutilisee comme latence du runtime canonique
FLOAT32 64 KiB. Le depot ne contient pas de dump recent donnant un percentile
ou un maximum `request -> READY` sur la version courante. Le pire cas SD n'est
donc pas borne par une mesure conservee. L'instrumentation existante
`g_stream_rec_perf` mesure deja `request_to_ready`, `submit_to_dma`, `stream_dma`,
`dma_to_io_finalize`, pages, misses, bytes et `audio_page_missing`; elle suffit
pour la prochaine campagne, mais il faut conserver p50/p95/p99/max hors firmware.

Pour du stereo FLOAT32 48 kHz :

| Vitesse | Duree d'une page | Pages/s/voix | Debit/voix | Marge brute avec une seule page NEXT |
|---:|---:|---:|---:|---:|
| x0,5 | 341,3 ms | 2,93 | 0,192 MB/s | 341,3 ms |
| x1 | 170,7 ms | 5,86 | 0,384 MB/s | 170,7 ms |
| x2 | 85,3 ms | 11,72 | 0,768 MB/s | 85,3 ms |
| x4 | 42,7 ms | 23,44 | 1,536 MB/s | 42,7 ms |

A x4, huit voix continues demandent 12,288 MB/s. Huit misses simultanees coutent
environ 26 ms de DMA a 3,25 ms/page, hors attente scheduler et autres clients :
la marge nominale restante avant 42,7 ms est faible mais positive. Seize voix
x4 demanderaient 24,576 MB/s, au-dela de la baseline de 19 MB/s; de plus le
runtime n'admet aujourd'hui que huit credits musicaux Stream/Multi. Ces calculs
sont des bornes de debit, pas une garantie d'absence d'underrun.

Un seek froid aleatoire n'a aucune marge : le bind demande la page et tente de
l'acquerir dans la meme commande AUDIO. Sa latence utile actuelle est donc
soit quasi nulle sur hit, soit un trigger silencieux/echoue sur miss.

## 3. Comportement cible par cas

### A. START statique

CONTROL doit convertir la valeur en frame, publier une intention de destination
et conserver la nouvelle valeur canonique immediatement. Hors lecture, deux
pages (`destination`, `destination+1`) peuvent etre preparees sans seconde tete.
Au retrigger : demarrage sample-accurate si READY; sinon politique explicite
`late start` (fade-in a readiness) plutot qu'echec invisible. Avec une page deja
chaude : moins d'un bloc AUDIO (1,33 ms). A froid : latence SD mesuree, ordre de
grandeur typique 3-10 ms mais maximum a mesurer.

### B. START par p-lock

Le p-lock doit etre resolu par CONTROL avant le NOTE et produire un
`stream_intent {key, frame, due_sample, generation, priority}`. STORAGE precharge
au moins la page cible et la suivante. Au boundary, AUDIO ne consomme qu'un plan
prepare/READY. Les sequences 5/82/31/60 % deviennent robustes si l'horizon
couvre la pire latence mesuree et si la file admet les jumps simultanes. Un trig
inattendu sans lookahead reste soumis a la politique de miss.

### C. START modifie pendant lecture

Trois politiques sont possibles :

1. `next trig` : sure et peu couteuse, mais geste peu immediat;
2. `immediate if READY, staged otherwise` : recommandee; hit -> seconde tete et
   crossfade immediat, miss -> ancienne tete continue, destination chargee,
   crossfade ensuite;
3. `fade-out, silence, seek, fade-in` : fallback quand conserver l'ancienne tete
   est impossible.

Le comportement ne devrait pas varier visuellement selon RAM/Stream; un petit
indicateur de destination en attente est acceptable. L'ancien audio ne doit
jamais etre presente sous la nouvelle position.

### D. END / LENGTH

LENGTH peut rester le parametre persiste pour compatibilite, mais le runtime
doit canoniser un plan `{start_frame, end_frame}`. Il est deja defini en RAM
comme `END = START + LENGTH * total`, clampe. Exposer END en UI peut etre une vue
derivee, pas une seconde autorite.

- END futur plus proche : fade-out/stop ou wrap sample-accurate; pas d'I/O;
- END recule derriere la tete : transition explicite vers START/loop ou stop;
- END allonge : prefetch des nouvelles pages avant qu'elles deviennent NEXT;
- p-lock LENGTH : inclus dans le meme plan prepare que START;
- changement continu : appliquer au block/control rate, pas reecrire le plan a
  chaque sample.

### E. Modulation continue

| Regime | Verdict |
|---|---|
| Tres lente (<1 Hz de changement de page) | Robuste avec staging/prefetch |
| Quelques Hz | Realiste, avec un seul target pending par voix, coalescence et quotas globaux |
| Rapide (dizaines/centaines de destinations/s) | Seulement dans le working set READY; sinon lag/quantification obligatoire |
| Audio-rate | Inutile et non soutenable pour un fichier arbitraire; autoriser seulement un scrub intra-page/RAM explicite |

Il faut distinguer modulation de frame et modulation de page. Une LFO rapide qui
reste dans une page ne coute pas de SD; la meme LFO sur plusieurs minutes peut
produire un miss a presque chaque mise a jour. La policy doit coalescer les
destinations obsoletes et limiter le taux de commits, par exemple au block ou a
un control-rate musical a calibrer.

### F. Reverse

Le reader doit accepter un pas signe, publier `PREVIOUS` plutot que `NEXT`, et
proteger les deux pages de fin de boucle pour un wrap inverse. Pour un jump,
la destination initiale est la page contenant START et sa voisine precedente.
Le cache et l'I/O n'ont pas besoin de lire les octets a rebours : ils chargent
toujours une page normalement, puis AUDIO parcourt ses frames en ordre inverse.
Le crossfade de direction utilise deux tetes; un simple changement de signe sur
la meme tete clique presque toujours.

### G. Boucles tres courtes

Une fois les pages de frontiere READY et leasees, la SD ne fixe plus la longueur
minimum : une boucle d'une frame est representable, mais musicalement peu utile
et sujette a discontinuite. Une cible de validation raisonnable est 32, 64, 128
et 256 frames (0,67/1,33/2,67/5,33 ms). Avec un crossfade, sa longueur doit rester
inferieure ou egale a la moitie de la boucle. Le minimum produit final doit etre
choisi par ecoute et mesure CPU; 64-128 frames est un bon candidat, pas une
limite demontree. Une boucle froide ne doit commencer qu'apres preparation des
deux pages de frontiere.

## 4. Architectures comparees

### Architecture 1 - fenetre actuelle amelioree

```text
tete -> CURRENT | NEXT | NEXT+2 | NEXT+3
reverse -> CURRENT | PREV | PREV-2 | PREV-3
loop -> reservation explicite des deux frontieres
```

Remplacer les roles fixes actuels par quatre roles directionnels/priorises,
avec deadline issue du pas et seek prepare. Avantages : changement local,
reutilise manager/cache/I/O, faible nouveau bookkeeping. Inconvenients : un
jump froid ne peut toujours pas etre instantane; dedier quatre pages lineaires
fait perdre les deux pages de loop actuelles ou impose plus de roles.

Budget : zero a +4 pages/voix selon conservation des pages de boucle;
0-256 KiB/voix, 0-2 MiB pour huit voix. CPU AUDIO quasi inchange. SD continue
au debit audio moyen; un seek ajoute 64 ou 128 KiB. Complexite moyenne, risque
principal : priorites mal ordonnees entre CURRENT froid et lookahead.

Verdict : necessaire mais insuffisant seul pour les p-locks arbitraires.

### Architecture 2/7 - double destination et double tete generique

```text
tete A joue ancien plan ----\
                            XFADE constant-power -> tete B devient A
STORAGE prepare plan B -----/
```

Le changement START, direction, sample, loop discontinu ou RAM/Stream cree un
plan B. A continue jusqu'a ce que B ait sa page courante et sa voisine READY.
Le crossfade peut durer 64-256 frames selon le geste; en cas de deadline ratee,
A continue ou effectue un fade-out controle.

Minimum pratique : deux pages destination, soit +128 KiB par transition. Une
double fenetre quatre roles complete coute +256 KiB/voix, donc +2 MiB pour huit
voix et +4 MiB pour seize. Seules les voix en transition doivent payer ce cout;
un pool global de 8-16 tetes B est preferable a une allocation par voix.

Avantages : meilleure immediatete et qualite, politique uniforme pour seek,
reverse, sample swap et loop. Inconvenients : deux readers simultanes, lifetime
plus delicat, double cout de mix pendant 1,3-5,3 ms typiques, et besoin d'un
arbitre si plusieurs transitions arrivent ensemble. Complexite elevee mais
localisable. Risque RT faible si toutes les tetes/pools sont statiques et si le
crossfade est borne.

Verdict : mecanisme AUDIO recommande.

### Architecture 3/5 - cache global partage avec zones chaudes

```text
key = asset + page
READY pages: presocle | voix actives | destinations futures | hot history
eviction: non leasee, non due, score age + urgence musicale
```

Le cache est deja global et partage; l'evolution consiste a rendre une partie du
slot-pool disponible comme hot cache et a ajouter pins/intentions bornes, au lieu
de limiter toute page mobile aux 36 pages runtime. Debut, loop start, dernieres
destinations et p-locks recurrents deviennent simplement des classes de score,
pas des caches paralleles.

Proposition de depart : 64 pages chaudes (4 MiB) prises dans le budget existant,
plus les 36 pages runtime. Il ne s'agit pas forcement de RAM additionnelle, mais
cela reduit le budget maximal d'assets RAM/static. Un cache local separe par voix
serait plus simple a raisonner mais dupliquerait les pages; il n'est interessant
que comme petit victim-cache fixe de 2 pages. Le vrai cache global economise
fortement les retriggers et la polyphonie sur un meme sample.

Bookkeeping : hash/index existent; ajouter deadline, classe, pin-generation et
score reste de l'ordre de quelques dizaines d'octets/page, a confirmer par le
map Release. Aucun refcount AUDIO mutable n'est requis : leases + intentions
CONTROL/STORAGE suffisent. Complexite moyenne/elevee; le risque est une eviction
starvation ou une priorite inverse, pas le cout CPU nominal.

Verdict : extension structurelle recommandee; architecture 5 devient une
politique du cache 3, pas un second mecanisme.

### Architecture 4 - prefetch des futurs p-locks

```text
SEQ/CONTROL lit steps futurs -> intents horodates -> STORAGE priority queue
                                      -> pages READY avant NOTE
pattern change -> generation invalidee, pages redevenues evictables
```

CONTROL, et non STORAGE, inspecte les p-locks. Un horizon adaptatif en temps est
plus correct qu'un nombre fixe de mesures : `max(p99 request->READY * fanout,
une fraction de beat)`, avec plafonds 1/4, 1/2 ou 1 mesure. Chaque trig reserve
au plus deux pages de START; les doublons sont fusionnes.

RAM additionnelle : aucune si les intents utilisent le cache global; plafond
conseille de 32 pages futures = 2 MiB de working set. SD moyenne inchangee si
les pages auraient ete lues ensuite; pire cas +64/128 KiB par destination qui
sera annulee. CPU CONTROL proportionnel au petit horizon de steps, hors IRQ.

Avantages : rend les p-locks quasi gratuits dans le cas sequencer normal et
permet des priorites par due time. Inconvenients : n'aide ni clavier live ni
encodeur imprevisible; pattern change, conditional trigs et microtiming exigent
generation/cancellation. Complexite moyenne. Risque RT bas si aucun scan de
pattern n'entre dans AUDIO.

Verdict : rendement produit maximal apres le cache/intents.

### Architecture 6 - promotion RAM automatique

```text
asset SAMPLE -> residency policy
  petit/frequent/random -> FULL RAM
  long/sequentiel       -> STREAM + hot pages
  pression memoire      -> demotion apres grace
```

Cette architecture masque vraiment RAM/Stream. Criteres : taille, free pages,
nombre de destinations distinctes recentes, taux de misses, nombre de voix et
presence de reverse/modulation rapide. Cout stereo : 0,384 MB/s de duree audio,
donc 1,92 MiB pour 5 s, 3,84 MiB pour 10 s, 11,52 MiB pour 30 s. La promotion
doit etre preparee en STORAGE puis swappee transactionnellement; jamais de copie
complete en IRQ.

Avantages : comportement RAM exact pour petits samples et zones tres random;
reverse/modulation deviennent gratuits apres promotion. Inconvenients : le
budget commun 21,25 MiB impose des choix visibles sous forte pression, la
demotion et les pointeurs de voix demandent une grace stricte, et charger un
fichier long entier n'est pas une reponse aux p-locks immediats.

Verdict : excellente optimisation de phase 5, mais le produit doit rester
correct sans promotion grace au cache et au staging.

## 5. Anti-clic et qualite audio

| Methode | Trou audio | Immediatete | CPU/RAM | Usage recommande |
|---|---|---|---|---|
| Fade-out -> seek -> fade-in | possible pendant miss | moyenne | faible | fallback/degradation |
| Attendre destination prete en gardant A | aucun | destination retardee | faible | miss live |
| Crossfade deux tetes | aucun | meilleure | double lecture pendant fade | choix principal |
| Constant-power | aucun, niveau percu stable | meilleure | quelques multiplications/trigo pretabulee | regions decorrelees |
| Lineaire court | aucun mais creux possible | excellente | minimal | continuite proche/correllee |

Le crossfade ne preserve pas la phase entre regions arbitraires; il rend la
discontinuite perceptuellement douce. Il faut capturer generation/key/epoch
dans chaque plan, publier B seulement apres READY, et ne jamais reutiliser le
payload d'une page dont la generation a change. Les leases et validations
actuels couvrent deja la protection contre ancienne donnee; la double tete doit
les conserver separement jusqu'au commit.

Aux START/END/loop, une rampe 32-128 frames suffit souvent; les changements de
sample ou direction meritent 64-256 frames constant-power. Les valeurs exactes
doivent etre ecoutees sur transitoires, basses et signaux DC/asymetriques.

## 6. Recommandation BRICK

Choix : `reader bidirectionnel + plan de destination + double tete bornee +
cache global chaud + lookahead SEQ`, puis promotion RAM automatique.

Le contrat produit devient :

1. CONTROL possede un unique `SAMPLE` et construit un plan immutable
   START/END/direction/rate/key avec timestamp;
2. STORAGE admet les intentions, fusionne les pages et les sert par deadline;
3. AUDIO ne fait qu'activer un plan READY, continuer A ou appliquer la policy
   de miss; il ne demande jamais FatFs et n'attend jamais la SD;
4. la residence FULL/STREAM/HOT reste une projection physique, jamais une
   identite utilisateur;
5. MULTI conserve son UX propre mais reutilise exactement reader/cache/intents.

Ce choix donne l'experience Blackbox recherchee pour les gestes musicaux usuels
sans pretendre reproduire son implementation : fichiers longs, regions variees,
p-locks prepares, boucles courtes chaudes et changement live propre. Les seules
limitations visibles restent un indicateur/retard de quelques millisecondes sur
un seek live froid, un plafond de transitions simultanees, et une modulation
rapide limitee au working set resident.

## 7. Contraintes temps reel non negociables

### AUDIO IRQ

- lookup READY, lecture/interpolation, deux tetes au maximum et crossfade borne;
- publication de lease/intention de consommation par structure fixe/seqlock;
- aucune FatFs, allocation, wait, retry, gros memcpy ou invalidation 64 KiB;
- cout maximal connu par block de 64 frames.

### CONTROL

- resolution des p-locks, START/END/LENGTH, reverse et timestamp;
- construction des intents futurs, generation de pattern et cancellation;
- admission des budgets de tetes B et voix avant NOTE.

### STORAGE cooperatif

- cache, score/eviction, priority queue bornee, maps, FatFs cold path;
- soumission/poll des lectures asynchrones, publication READY apres validation
  token/key/page/epoch/generation;
- aucun blocage de l'IRQ AUDIO, service correct quand la SD est lente.

### DMA/IRQ peripheriques

- transfert physique et completion courte;
- pre/post maintenance D-cache suivant le contrat actuel;
- aucune decision musicale ni publication partielle d'une page.

Les collisions de voix sont resolues avant I/O par fusion de cle/page, deadline
la plus proche puis round-robin anti-starvation. Si plusieurs jumps froids ont
la meme deadline impossible, l'admission doit choisir explicitement quelles
voix gardent A, demarrent en retard ou sont refusees; jamais laisser AUDIO
decouvrir la saturation.

## 8. Plan de migration

1. **Baseline sans changement sonore.** Capturer en Release les distributions
   de latence avec l'instrumentation existante pour x0,5/x1/x2/x4, 1/8 voix,
   recorder actif/inactif et carte lente. Ajouter seulement le protocole et les
   dumps hors firmware.
2. **Plan unifie interne.** Etendre `sample_play_plan_t` avec direction et END,
   sans changer UI. Faire utiliser le meme constructeur par RAM, Classic et
   Multi. Tests host des bornes et generations.
3. **Seek prepare hors lecture.** Ajouter les intents CONTROL/STORAGE et rendre
   START statique + retrigger robuste. Conserver les pages TONE/persistence
   actuelles durant cette phase.
4. **Cache chaud global.** Repartir explicitement les 376 pages, ajouter pins
   d'intention/deadlines et politiques debut/loop/history. Mesurer le map Release
   et la regression des allocations RAM/Wavetable.
5. **Lookahead p-lock.** Scanner l'horizon CONTROL, generer/canceller les intents
   par generation de pattern, puis exposer START/LENGTH au Stream.
6. **Double tete/crossfade.** Generaliser changement live, sample swap, reverse
   et loop discontinu. Pool fixe et admission avant commande AUDIO.
7. **Reverse puis modulation.** D'abord retrigger/reverse statique, ensuite LFO
   lent avec coalescence. Ne pas exposer audio-rate.
8. **Promotion automatique.** Ajouter telemetry de random-access et swap
   transactionnel FULL/STREAM; seulement apres validation du chemin pagine.
9. **Fusion UX.** Remplacer RAM/STREAM par SAMPLE et garder MULTI.

Compatibilite : le codec courant est v14 et accepte v13. Une version suivante
doit continuer a decoder les type keys RAM/STREAM et asset kinds `RAM `/`STRM`,
les convertir vers SAMPLE avec un `residency_hint` non musical, puis persister
le modele unifie. Les DTO TONE RAM/Stream doivent etre fusionnes en conservant
les defaults absents; les p-lock keys PARAM restent stables. Tant que tous les
anciens projets n'ont pas ete re-sauves, ne pas reutiliser les anciennes cles
disk. Le changement peut donc etre compatible sans maintenir deux runtimes.

## 9. Criteres de validation

- P-lock START 5/82/31/60 % sur un fichier de 10 minutes, 120 BPM, puis avec
  microtiming et conditional trigs : zero miss apres warmup/lookahead.
- 8 voix x0,5/x1/x2/x4, destinations toutes distinctes; mesurer p50/p95/p99/max
  request->READY, debit, queue age et `audio_page_missing`.
- campagne 16 voix pour valider l'admission : degradation explicite, jamais une
  promesse silencieusement violee; x4 stereo 16 voix est hors budget SD H743.
- 8 jumps simultanes vers fichiers differents, puis meme fichier/meme page pour
  confirmer fusion et partage.
- carte SD lente et pauses longues : A continue, fade propre ou late start;
  aucun stale payload, deadlock ou spin AUDIO.
- reverse x0,5/x1/x2/x4, franchissement de page, retrigger et changement de sens.
- loops 32/64/128/256 frames, boucle sur une page puis de part et d'autre d'une
  frontiere; spectre/clicks et CPU.
- changement de pattern pendant horizon prefetch : anciennes generations
  annulables/evictables, aucune page ancienne jouee.
- retriggers rapides sur destination chaude/froide et voice stealing Multi;
  niveau constant et absence de click.
- changement de sample en lecture, RAM->Stream et Stream->RAM via tete B.
- stress Recorder + Stream pour verifier l'arbitrage SD et les marges.
- assertions : aucune FatFs/allocation/wait/memcpy page dans IRQ; cache/DMA
  valide; temps AUDIO max sous 1,33 ms avec huit crossfades admis.

## 10. Go / No-Go

**GO** pour `SAMPLE` + `MULTI`, avec implementation progressive de la pile
recommandee. **NO-GO** pour une promesse d'acces aleatoire froid sample-accurate,
de modulation audio-rate sur fichiers longs, ou de 16 voix stereo x4 sur le
H743/SD actuel.

Les limitations utilisateur residuelles peuvent etre reduites a : seek live
froid legerement differe, nombre borne de transitions simultanees, reverse et
modulation rapide garantis seulement quand le working set est resident. Ces
limites sont compatibles avec l'objectif musical et beaucoup moins structurantes
que la distinction RAM/STREAM actuelle.
