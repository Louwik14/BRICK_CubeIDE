# Baseline pre-refonte du premier chargement Waveview STREAM

Audit realise sur `main` a partir de `d410477fb`. Les volumes et nombres
d'operations ci-dessous sont derives exactement du code. Aucun H743 ni RT1172
n'etait connecte a cette passe : les temps hardware restent donc a relever avec
l'instrumentation ajoutee, et ne sont pas presentes comme des mesures.

Ce document conserve le diagnostic du chemin retire. Le contrat courant est
decrit dans `sample_streaming.md`, section "Overview Waveform sampler"; les
mentions ci-dessous du scan lazy Classic et du seuil 60 s ne decrivent plus le
runtime actuel.

## Verdict

La lenteur est structurelle : pour un sample Classic de plus de 2048 frames,
le premier affichage relit pratiquement 100 % des frames audio. Le service le
fait par lectures utiles de 4088 octets maximum et, pour chaque lecture,
rouvre le WAV, reparcourt son header, seek, lit, puis referme le fichier. Les
124 colonnes OLED ne sont publiees qu'une fois tous les bins couvrant la vue
prets. Il n'existe donc aucun premier pixel progressif.

Un cache persistant multi-resolution existe deja, mais la Waveview Classic ne
l'ouvre jamais. `waveform_request()` ne recupere que le handle appartenant au
Recorder via `sample_capture_model_waveform_cache_get_handle()`. En plus, les
samples inferieurs a 60 s sont deliberement exclus de la persistence. C'est la
cause principale, devant le cout CPU de min/max et le rendu UI.

## Pipeline actuel exact

```text
renderer TONE PLAY/LOOP (chaque rendu UI)
  -> slot Classic selectionne
  -> waveform_sampler_source()
       key(domain, object_id, generation) + registration_epoch + frame_count
  -> waveform_request(source, 0, frame_count, 124 colonnes)
       -> si <= 2048 frames: reserve/render via Page Cache STREAM
       -> sinon: choisit un niveau 16384/4096/1024/256/64 frames par bin
       -> cherche les tuiles min/max SDRAM
       -> cherche uniquement un sidecar Recorder (pas Classic)
       -> demarre/reprend une tuile locale manquante
  -> brick6_app_process()/superloop STORAGE
       -> waveform_service_storage_service()
       -> admission SD background
       -> mount, open, parse WAV (premier chunk), seek, f_read <= 4092 B, close
       -> decode chaque frame L/R et reduit min/max
       -> repete jusqu'a couverture complete du fichier
  -> rendu suivant: les 124 colonnes deviennent READY d'un seul coup
  -> normalisation verticale sur le peak des 124 colonnes
  -> dessin waveform, puis overlays START/END/LOOP/SLICE/playhead
```

Le declencheur est le renderer
`ui_renderer_template_draw_sample_waveform()`. Il appelle le service a chaque
rendu tant que la page est visible. La generation et les I/O sont executes
dans le superloop STORAGE, jamais dans AUDIO. Le rendu UI ne fait que les
recherches cache et la reduction des bins deja publies vers 124 colonnes.

## Resolution et quantites

- Cadre OLED : 126 x 17 pixels ; surface utile : **124 x 15**.
- Besoin global exact : **124 couples min/max** ; une representation alignee
  a 128 colonnes coute **512 octets** en `int16_t min/max`.
- Cache local : 16 tuiles x 512 bins, soit 8192 couples min/max (~32 KiB hors
  metadata).
- Niveaux : 16384, 4096, 1024, 256 et 64 frames/bin.
- Pour une vue globale, le niveau choisi produit entre environ 124 et 496 bins,
  mais sa construction inspecte tout de meme chaque frame source.
- Le sidecar actuel stocke les cinq niveaux, soit environ `frame_count / 12`
  octets : ~4 ko/s a 48 kHz, alors que la vue globale fixe demande 512 octets.
- RAM du cache sidecar : 64 tuiles x 512 x 4 = 128 KiB, hors metadata.

Le choix multi-resolution est utile au zoom Recorder, mais surdimensionne pour
la Waveview globale du sampler. Les overlays ne font pas partie des donnees
audio et sont deja dessines separement.

## I/O du premier affichage

Pour le format produit canonique 48 kHz, FLOAT32 stereo, un frame vaut 8
octets. `WAVEFORM_READ_BYTES=4092`, mais l'alignement a 8 octets donne **4088
octets / 511 frames par read**. Chaque read est une transaction logique FatFs
separee, precedee d'un open et d'un seek, puis suivie d'un close.

| Sample | Audio inspecte | Reads waveform | Read moyen/max | Resultat affiche |
|---|---:|---:|---:|---:|
| tres court, <= 42,7 ms | 0 direct ; Page Cache jusqu'a 16 KiB | 0 direct | page STREAM | 124 px |
| 1 s | 384 000 B | 94 | 4085 / 4088 B | 124 px |
| 10 s | 3 840 000 B | 940 | 4085 / 4088 B | 124 px |
| 1 min | 23 040 000 B | 5 637 | 4087 / 4088 B | 124 px |
| fichier audio 100 MiB | 100 MiB | 25 651 | 4088 / 4088 B | 124 px |

Ainsi, **100 MiB sont relus pour produire 496 octets utiles**. Pour les tres
longs samples, plusieurs tuiles L0 sont construites successivement ; la somme
reste le fichier complet. Les formats mono/16/24 bits changent le nombre
d'octets mais pas le fait que chaque frame est inspecte.

Une lecture lazy coalescee a 16/32/64 KiB ferait tomber les 25 651 reads du cas
100 MiB a respectivement 6400/3200/1600. Ce serait une nette amelioration du
fallback, mais pas une solution au premier affichage : 100 MiB resteraient lus.

## Recalculs et invalidations

La cle RAM est saine : `sample_audio_key` (domain/object/generation),
`registration_epoch` et `frame_count`, plus `media_epoch` pour les jobs. Un
changement de START, END, LOOP, SLICE ou PAGE PLAY/LOOP ne change pas cette cle
ni la requete globale ; il ne devrait donc pas relancer le scan. Quitter puis
revenir conserve les tuiles statiques tant qu'elles ne sont pas evincees.

Un nouveau sample, une nouvelle generation/registration, un changement de
media ou l'eviction LRU invalide correctement la RAM. Un changement de viewport
pendant un job peut abandonner le job actif ; ce cas concerne surtout les vues
detaillees Recorder.

Defauts du cache persistant actuel :

1. il est demande lors de la preparation Classic uniquement a partir de 60 s ;
2. son handle n'est jamais attache/resolu pour une source Classic dans
   `waveform_request()` ;
3. le handle Recorder global peut etre propose sans comparaison explicite avec
   la source demandee ; il faut interdire toute utilisation cross-source ;
4. l'identite persistante comprend path normalise, taille, format, nombre de
   frames et hash des premiers/derniers 64 KiB, mais pas le contenu central ;
   un remplacement de meme path/taille/extremites peut donc passer inaperçu ;
5. ouvrir/valider un handle recalcule ces hashes, soit jusqu'a 128 KiB lus ;
   cela ne doit jamais etre fait a chaque frame UI ;
6. cinq niveaux exacts sont construits pour tous les usages, au cout d'un scan
   complet et de nombreuses petites ecritures dispersees.

## Instrumentation ajoutee

`g_waveform_diag` est un bloc volatile lisible dans GDB pour la source courante.
`waveform_diag_reset()` permet de repartir de zero avant chaque sample. Il
mesure : requetes READY/PENDING/INVALID, hits/misses RAM et sidecar, builds,
abandons, completions, opens, parses, reads, octets, read max, delai
request->build, delai premier pixel/complet, cycles I/O et cycles de reduction.

Le premier pixel et la waveform complete ont aujourd'hui le meme timestamp,
car le contrat UI ne publie aucune colonne partielle. Conversion des cycles :
`secondes = cycles / SystemCoreClock`. La taille moyenne vaut
`read_bytes / read_calls`.

Table hardware a remplir sur H743 avec un sample froid, puis un second passage
chaud :

| Etape | Temps | Octets/ops |
|---|---:|---:|
| request -> build | `request_to_build_ms` | 1 build attendu |
| open/metadata | inclus dans `io_cycles` | `file_opens`, `format_parses` |
| I/O waveform | `io_cycles/SystemCoreClock` | `read_bytes`, `read_calls`, max |
| reduction min/max | `reduction_cycles/SystemCoreClock` | tous les frames |
| publication/premier pixel | `request_to_first_pixel_ms` | 124 colonnes |
| waveform complete | `request_to_complete_ms` | identique au premier pixel |

Sans cible connectee, aucun nombre de temps n'a ete invente. Les compteurs
statiques ci-dessus prouvent deja la disproportion I/O ; le protocole hardware
sert a separer latence FatFs/SD et cout de decode/reduction.

## Causes classees

1. **Scan integral lazy** avant tout affichage utile.
2. **94 a 25 651 petits cycles open/seek/read/close** selon la taille testee.
3. **Cache persistant Classic non branche et seuil arbitraire de 60 s**.
4. Publication tout-ou-rien ; l'UI et les 124 reductions finales sont
   secondaires face au volume I/O.

## Comparaison des architectures

| Solution | Premier affichage | Fidelite | Cout/complexite | Verdict |
|---|---|---|---|---|
| overview exacte a l'import | lecture 512 B, puis RAM | exacte min/max | +1 accumulateur/frame pendant un passage existant | cible |
| sidecar/cache persistant actuel simplifie | idem | exacte | gestion identite/version ; eviter des milliers de petits fichiers | bon support |
| 5 niveaux persistants | rapide et zoomable | exacte | ~4 ko/s/sample et build/ecritures inutiles au sampler global | Recorder seulement |
| generation progressive | premier fragment rapide | exacte a terme | etat UI et publication partielle plus complexes | fallback optionnel |
| sampling sparse | rapide | peut manquer transient/silence/crete | simple mais visuellement malhonnete | rejete |
| scan lazy exact en 64 KiB | toujours proportionnel au fichier | exacte | fallback simple, 16x moins de transactions qu'actuellement | fallback |

## Architecture cible RT1172

1. Produire **une overview globale exacte de 128 couples min/max** pendant
   l'import/canonicalisation. Le nombre de frames est connu avant le passage ;
   chaque frame alimente son bucket. Aucun second read lorsque la conversion ou
   validation lit deja tout l'audio.
2. Stocker un record versionne de **512 B de donnees**, plus environ 32--64 B
   d'identite/CRC. Cible : **576 B par sample**.
3. Eviter un fichier FAT par sample. Utiliser un fichier packed associe a
   l'index (`SAMPLE.WFM`) ou une zone blob versionnee de l'index, avec offset
   dans l'entree sample. La cle doit contenir au minimum path/source stable,
   taille, date/heure ou generation d'import, frame_count, format et CRC du
   record. En prototypage, bump de version et regeneration totale.
4. Au chargement/assignation, attacher au descripteur Classic un handle deja
   valide. La premiere Waveview charge un record de 576 B au plus, le garde
   dans un petit LRU (4 records ~= 2,3 KiB), puis mappe 128 vers 124 colonnes.
   Aucun hash de 128 KiB sur le chemin UI.
5. Ne pas ajouter de niveaux pour la Waveview sampler tant qu'aucun zoom
   produit n'existe. Conserver le multi-resolution uniquement pour l'editeur
   Recorder.
6. Conserver un fallback lazy exact pour source nouvelle/corrompue : scan
   sequentiel 32 ou 64 KiB, admission background, puis persistence atomique.
   Une approximation progressive peut afficher les buckets termines, mais elle
   n'est pas requise si l'import garantit le record.
7. Pour les samples deja indexes : aucune migration. Invalider la version de
   catalogue/cache et reconstruire les records une fois.

### Cout et gain attendus

- Import avec conversion/scan existant : aucun octet audio relu ; 2
  comparaisons min/max par canal/frame environ et 576 B ecrits.
- Fichier canonique jamais scanne integralement : un scan sequentiel unique
  ajoute, idealement 32/64 KiB ; cout wall-clock = taille/debit SD + reduction.
- Affichage froid apres indexation : une lecture sous 1 KiB, objectif produit
  **< 1 frame UI (~16 ms) si admise**, borne prudente **< 50 ms** a valider sur
  RT1172/SD cible.
- Affichage chaud : copie/reduction de 128 couples, pratiquement immediat.
- Cas 100 MiB : passage de 100 MiB + 25 651 reads au premier affichage a
  environ 576 B + 1 read, soit >180 000x moins d'octets.

## Interaction AUDIO

Le code actuel reste hors IRQ AUDIO et demande une admission background. Il ne
demarre pas si le scheduler voit un travail prioritaire. En revanche, une
transaction FatFs synchrone deja admise n'est pas preemptable : AUDIO peut
devenir urgent pendant les ~4 KiB et attend la fin du chunk. La cible doit
garder la priorite `Streamer/Recorder > Waveform`, ne jamais parser/decoder en
AUDIO, et borner ou rendre annulable le fallback. Les gros reads 32/64 KiB ne
doivent etre utilises que si le backend asynchrone sait ceder aux urgences ;
sinon conserver des quanta plus petits tout en evitant open/close par quantum.

## Validation

- Build Release CMake reussi.
- L'instrumentation ne change ni l'algorithme, ni le cache, ni la priorite.
- Aucun redesign stockage n'est inclus dans cette passe d'audit.
