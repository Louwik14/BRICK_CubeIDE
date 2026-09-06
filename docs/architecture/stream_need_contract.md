# Sampler, assets, streaming et page-cache

## Ownership

STORAGE possede les metadonnees du page-cache, index, generations,
reservations, etats, eviction et validation des completions Storage. AUDIO
possede uniquement les lecteurs, positions DSP et leases. CONTROL possede
l'output musical et garantit START pour une configuration/asset/workload
produit legaux. Storage possede SD, fichiers, maps physiques, lecture et
decode. Les commandes et completions de pages sont tokenisees, bornes et sans
pointeur.

Une page suit `FREE -> RESERVED -> LOADING -> READY`, `EVICTING`, ou `FAILED`. Une page LOADING n'est ni recyclable ni evictable. Pour recycler, STORAGE publie d'abord `EVICTING`, relit l'union des leases, restaure `READY` si la page est protegee, sinon passe `FREE`. AUDIO etend son lease avant resolution puis revalide `READY/key/registration_epoch/generation`. La completion valide key, slot, page generation, registration epoch et token; une completion tardive ne devient jamais visible.

## Lease physique et service

M7 ne publie aucun snapshot de playhead, frame cursor, deadline, pitch, phase,
loop state ou demande I/O. Chaque lecteur expose uniquement le lease seqlocke
`{seq, key, registration_epoch, ranges[2]}`. Un range est
`{first_page, page_count}`. Il enumere les pages physiques que le lecteur peut
encore lire; le second range sert au wrap discontinu. Le Looper emploie un
second slot du meme type uniquement pendant son crossfade de resynchronisation.

Le scheduler M4 sert les lecteurs actifs en round-robin. Il derive localement
le lookahead produit a partir des ranges proteges; AUDIO ne publie ni liste de
besoins ni wake Storage. Une page par lecteur et par passe; aucune horloge
STREAM, low-water dynamique ou prediction temporelle ne conditionne le service.

Le contrat V1 borne le workload a huit voix Stream maximum, avec une page par
voix et par passe dans le meme round-robin strict. Toutes les voix admissibles
suivent cette cadence et cette politique; il n'existe pas d'EDF par voix, de
deadline individuelle ni de priorite derivee du playhead.

Le contrat produit garantit le pre-socle `2 x 32 KiB` et les limites Stream/Multi publiees avant jeu. Il n'existe ni READY par note, ni ACK START, ni retry, rollback ou fallback musical. Un underrun dans ce workload est une rupture de contrat, pas une admission tardive.

## I/O et cadence

Les admissions de Multi Load, Stream Load, Wavetable Load et Sampler RAM Load
sont autorisees uniquement sur transport `STOPPED` stable. Le gate est pris
avant creation du job ou mutation de pool; un transport actif ou une SD absente
produit un refus immediat, sans retry automatique. La Preview SD reste
disponible pendant le playback.

Le service Storage traite une commande bornee hors IRQ. Produit: tranche et
page de travail 32 KiB. Le backend physique resout des extents vers une FIFO DMA
bornee; FatFs reste le fallback. Read-ahead ne change ni ordre, besoins ni
lifecycle.

STORAGE_IO reveille le seul owner runnable concerne par une demande, une
completion ou une ressource liberee. Le refill Stream n'est execute qu'une
fois par activation pertinente; aucun second passage via une facade Storage,
aucun repoll generique et aucune strategie de scheduling supplementaire ne
modifient le round-robin strict ci-dessus. Le scheduler SD reste l'unique
arbitre du media physique.

Le transport contient geometrie source, format et token. STORAGE decode dans
un payload partage et publie READY; AUDIO invalide avant lecture. H743
et H747 conservent le meme contrat: M4 possede metadata et I/O, M7 publie les
leases physiques de ses lecteurs.

## Catalogue Classic unique

`sample_global_pool` est l'unique catalogue produit pour Classic, RAM, Multi et
Wavetable. Une ressource Classic est adressee directement par son index global :
le chemin n'existe que dans cette entree et sa description WAV n'existe que dans
le backend Classic. Le chargement analyse le WAV une seule fois puis choisit
FULL ou STREAM. Les pages, readers, leases, generations, scheduler SD et
politiques de recyclage forment le plan physique Stream; ils
ne constituent pas un second catalogue produit.

## Multi, Sampler RAM et Wavetable

Le bulk Multi calcule et epingle l'union start/loop, utilise le cache, le transport et le scheduler communs, par lots de 64 KiB. Il ne possede ni FatFs, ni decodeur, ni arbitre SD parallele.

Sample RAM charge par etapes, lectures de 4096 octets et conversion de 256 frames. Wavetable ajoute parse, CRC, mipmaps, cache `.B6WT` transactionnel et preview; une FFT/bande ou 256 samples sont traites par quantum. Le format WAVE interne canonique est mono FLOAT32 normalise, 1024 samples par frame, avec mipmaps FLOAT32 band-major 1024/512/256/128/64/32/16/8. Les strides physiques sont egaux aux tailles logiques, sauf la bande 8 dont le stride est 16 samples avec 8 floats de padding; chaque frame commence ainsi sur une limite de 32 octets et aucune duplication cyclique n'est stockee. La geometrie source 1024/2048 est un choix explicite de l'importeur; les API sans geometrie explicite choisissent 2048. Les cycles 2048 sont convertis en 1024 dans le domaine frequentiel. Chaque bande est ensuite generee directement depuis la FFT canonique 1024, avec transition raised-cosine, marge avant Nyquist et bin Nyquist nul; aucune cascade ni saturation post-IFFT n'est appliquee. Le cache prepare est en version physique 5 et sa revision de preparation est 9; les anciennes preparations sont rejetees et regenerees depuis le WAV source. L'ancien slot reste publie jusqu'au commit du candidat.

Les payloads Sampler RAM/Wavetable sont des references `{region, offset, length}`. CONTROL clean avant publication, AUDIO invalidate avant installation. Un unload/remplacement suit `STOP -> invalidation voix synchrone -> avancee du tail FIFO -> FREE CONTROL`. Les ACK Multi/RAM/Wavetable et leur ring IPC ont ete supprimes; seul le fence du consumer physique est lu.

Le registre compact de leases Stream est fixe, pointer-free, seqlocke et place explicitement dans la fenetre IPC partagee SRAM3/D2. Les snapshots de besoins, pins, use-counts et refcounts de pages ont ete supprimes. Le Looper ne publie aucun intent Stream: CONTROL derive directement track, chemin, longueur, reservation et finalisation depuis son Recorder; AUDIO ne conserve que son runtime, ses playheads et ses leases.

Pour une prise Looper, la reservation de deux credits couvre toute la
transition `REC -> READY -> PLAY`. Un STOP de capture ne la libere pas lorsque
`play_auto` conserve une lecture possible; elle est liberee seulement lorsque
la prise est abandonnee/remplacee, qu'une erreur annule le runtime, ou qu'aucun
playback suivant n'est demande. L'arret du transport ne detruit pas une prise
READY et ne recycle donc pas ses credits.

## Admission globale des readers

L'admission Stream est globale au produit: les huit credits fixes sont
partages par Classic, Multi et Looper. CONTROL reserve avant chaque START,
Multi associe ensuite son reader a un slot physique local, et Looper reserve
atomiquement son couple primaire/auxiliaire; un slot auxiliaire peut rester
absent tant qu'aucun crossfade ne le publie. Chaque credit porte proprietaire,
generation, role, presence physique et cycle `ACTIVE -> RETIRE_REQUESTED ->
PHYSICALLY_RELEASED`. AUDIO ne peut associer ou publier un reader sans token
actif. La liberation est signalee par masque de slots lors d'un lease vide;
CONTROL valide alors le seqlock et recycle uniquement les credits du groupe,
sans balayage periodique des 56 leases physiques.

Une page produit de 32 KiB porte 8192 frames mono FLOAT32 ou 4096 frames stereo. Format, stride et frames/page sont derives par `sample_audio_format.h` et restent immutables pendant la voix. Mono reste mono jusqu'au pan/spread final; aucune duplication droite de rejet n'est conservee.

Preview est un ring PCM SPSC distinct. Les prises Looper actives utilisent la carte append-only du Recorder et ne lisent jamais au-dela du tail physiquement committed; le detail appartient a [recorder_sd.md](recorder_sd.md).
