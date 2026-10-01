# Sampler, assets, streaming et page-cache

## Ownership

STORAGE possede les metadonnees du page-cache, index, generations,
reservations, etats, eviction et validation des completions Storage. AUDIO
possede uniquement les lecteurs, positions DSP et leases. CONTROL possede
l'output musical et garantit START pour une configuration/asset/workload
produit legaux. Storage possede SD, fichiers, maps physiques et lecture. Les
jobs locaux et completions de pages sont tokenises et bornes. Il n'existe plus
de mailbox ni d'ABI de transport entre le manager Stream et l'I/O.

Une page suit `FREE -> RESERVED -> LOADING -> READY`, `EVICTING`, ou `FAILED`. Une page LOADING n'est ni recyclable ni evictable. Pour recycler, STORAGE publie d'abord `EVICTING`, relit l'union des leases, restaure `READY` si la page est protegee, sinon passe `FREE`. AUDIO etend son lease avant resolution puis revalide `READY/key/registration_epoch/generation`. La completion valide key, slot, page generation, registration epoch et token; une completion tardive ne devient jamais visible.

## Lease physique et service

M7 ne publie aucun snapshot de playhead, deadline, pitch, phase ou demande I/O.
Chaque lecteur expose un lease seqlocke
`{seq, key, registration_epoch, pages[4], valid_mask}`. Les quatre roles fixes
sont `CURRENT`, `NEXT`, `LOOP_START` et `LOOP_START_NEXT`; les doublons de pages
sont admis et ne consomment qu'une page physique. Les lecteurs Streamer et
OVERDUB possedent chacun leur lease et peuvent partager les memes pages immuables.

Le scheduler STORAGE du M7 sert directement les slots absents des lecteurs
actifs en round-robin. AUDIO est l'unique producteur du besoin; STORAGE ne derive aucun
lookahead et il n'existe pas de loop cache parallele. Une page par lecteur et
par passe; aucune horloge
STREAM, low-water dynamique ou prediction temporelle ne conditionne le service.

Le contrat produit garantit un pre-socle d'une page 0 READY, soit 8192 frames
stereo. Au demarrage cette page est `CURRENT`: elle ne s'ajoute pas a la
fenetre runtime. Chaque reader garantit au plus quatre pages physiques
distinctes, pour 8 readers musicaux et un reader overdub reserve, soit 36 pages
reservees (32 + 4). Les limites
Classic/Multi/REC_SOURCE sont publiees avant jeu. Il n'existe ni READY par
note, ni ACK START, ni retry, rollback ou fallback musical. Un underrun dans ce
workload est une rupture de contrat, pas une admission tardive.

## I/O et cadence

Le service Storage traite une commande bornee hors IRQ. Le quantum de lecture reste 32 KiB et chaque page fait 64 KiB. Le backend physique resout des extents vers une FIFO DMA bornee et lit le payload FLOAT32 directement dans la page finale; FatFs reste le fallback cold-path vers cette meme page. Read-ahead ne change ni ordre, besoins ni lifecycle.

Le job I/O local contient geometrie source, format et token. STORAGE remplit le
payload puis publie READY; AUDIO conserve les credits de lecture. La
maintenance cache du block device reste la frontiere CPU/DMA et ne constitue
pas un transport AUDIO/STORAGE.

## Catalogue Classic unique

`sample_global_pool` est l'unique catalogue produit pour Classic, RAM, Multi et
Wavetable. Une ressource Classic est adressee directement par son index global :
le chemin n'existe que dans cette entree et sa description WAV n'existe que dans
le backend Classic. Le chargement analyse le WAV une seule fois puis choisit
FULL ou STREAM. Les pages, readers, leases, generations, scheduler SD et
politiques de recyclage forment le plan physique Stream; ils
ne constituent pas un second catalogue produit.

## Multi, Sampler RAM et Wavetable

Le bulk Multi prepare uniquement la page 0 et utilise le cache, l'I/O locale et
le scheduler communs, par lots de 64 KiB. La boucle immutable est ensuite
exprimee par les deux slots `LOOP_START`; Multi ne possede ni profondeur, ni
cache de boucle, ni FatFs, decodeur ou arbitre SD parallele propres.

La preparation Multi v4 separe la collecte WAV de la resolution des zones. Le
scan conserve SMPL, INST et les faits du nom; une analyse du dossier choisit
ensuite une convention unique avant conversion. La priorite root est
`SMPL -> INST -> range filename explicite -> serie root/velocity -> prefixe
MIDI corrobore -> note textuelle -> ordre alphabetique`. La priorite velocity
est `INST -> range explicite -> serie root/velocity -> 1..127`. Les suffixes
`_0001`, `_0002` sont des takes, jamais des velocities par eux-memes; un take
secondaire du meme sample est retire avant conversion. Les index anterieurs a
la version 4 sont invalides et doivent etre reconstruits depuis les WAV.

Sample RAM charge le payload FLOAT32 stereo directement dans son allocation, par etapes sous un budget cooperatif de 2 ms et avec lectures adaptatives de 4 ou 16 KiB. Wavetable applique le meme budget, ajoute parse, CRC, mipmaps et preview, puis ecrit le cache `.B6WT` transactionnel sur cold path. Le loader asynchrone valide et recharge directement ce cache lors des loads suivants; un cache absent, invalide ou obsolete relance seul le build. Le format WAVE interne canonique est mono FLOAT32 normalise, 1024 samples par frame, avec mipmaps FLOAT32 band-major 1024/512/256/128/64/32/16/8. Les strides physiques sont egaux aux tailles logiques, sauf la bande 8 dont le stride est 16 samples avec 8 floats de padding; chaque frame commence ainsi sur une limite de 32 octets et aucune duplication cyclique n'est stockee. La geometrie source 1024/2048 est un choix explicite de l'importeur; les API sans geometrie explicite choisissent 2048. Les cycles 2048 sont convertis en 1024 dans le domaine frequentiel. Chaque bande est ensuite generee directement depuis la FFT canonique 1024, avec transition raised-cosine, marge avant Nyquist et bin Nyquist nul; aucune cascade ni saturation post-IFFT n'est appliquee. Le cache prepare est en version physique 5 et sa revision de preparation est 9; les anciennes preparations sont rejetees et regenerees depuis le WAV source. L'ancien slot reste publie jusqu'au swap du candidat, puis ses pages sont liberees.

Les descriptors Sampler RAM/Wavetable portent des pointeurs M7 locaux valides
jusqu'au retrait AUDIO. Leur publication CPU vers CPU utilise `ready` et
`DMB`, sans clean/invalidate. Un unload/remplacement ferme l'ingress, publie le
STOP, attend la grace bornee de 192 frames, retire le descriptor puis libere
les pages.

Pour Multi, `sample_page_cache_clear_key` retire aussi l'ownership
`static_resident` des pages de pre-socle. Une page sans lease est liberee
immediatement; une page encore leasee passe par `FAILED` jusqu'a la fin de son
lease, et un chargement en cours est annule sans conserver l'ownership. Les
compteurs UI du catalogue global ne sont pas une mesure des descripteurs
physiques du page-cache: le teardown doit donc maintenir ces deux plans
coherents, et non corriger seulement les compteurs publies.

Le registre compact de leases Stream est fixe, seqlocke et place en SRAM D2
cacheable locale au M7. Le seqlock et ses `DMB` synchronisent l'IRQ AUDIO avec
le service STORAGE cooperatif. Les
snapshots de besoins, pins, use-counts et refcounts de pages ont ete supprimes.
REC_SOURCE publie seulement une generation immutable READY; AUDIO conserve ses
playheads et ses leases.

## Format audio

Une page sample produit de 64 KiB porte 8192 frames FLOAT32 stereo entrelacees.
Le Streamer est exclusivement forward; reverse et ping-pong restent limites au
Sampler RAM. Format, stride et frames/page sont derives par
`sample_audio_format.h` et restent immutables pendant la voix.

Le format WAV canonique des samples BRICK est IEEE FLOAT32 stereo, 48 kHz,
32 bits par sample, 8 octets par frame, little-endian. Son chunk `data`
commence a l'offset 512 afin de conserver l'alignement secteur/page. Une
canonicalisation remplace transactionnellement le fichier au meme chemin via
les suffixes temporaires `.B6T` et `.B6B`; elle ne cree ni asset store, ni
nouvelle reference projet. Le chargement d'un ancien Project canonicalise ses
references Classic et RAM apres le quiesce et avant le commit; une reprise nettoie
`.B6T` ou restaure/nettoie `.B6B` avant toute nouvelle conversion. Le parser,
la preview et l'import continuent a
accepter PCM16/24/32 et FLOAT32 mono/stereo. Le runtime Classic/Multi exige ce
format canonique: le backend SD lit directement dans la page FLOAT32 finale,
sans decodeur ni scratch de page intermediaire. Le loader RAM lit de meme
directement dans son allocation FLOAT32. Le Recorder produit lui aussi ce
format canonique et ses pages suivent le chemin FLOAT32 direct. Le decodeur
PCM24 borne reste disponible uniquement pour les anciennes prises deja
presentes sur SD.

Preview est un ring PCM SPSC distinct. Le building REC utilise la carte append-only du Recorder; il n'est publie qu'apres finalisation et prechauffage des pages initiales. Le detail appartient a [recorder_sd.md](recorder_sd.md).
