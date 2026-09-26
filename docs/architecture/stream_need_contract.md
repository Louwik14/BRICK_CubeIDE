# Sampler, assets, streaming et page-cache

## Ownership

STORAGE possede les metadonnees du page-cache, index, generations,
reservations, etats, eviction et validation des completions Storage. AUDIO
possede uniquement les lecteurs, positions DSP et leases. CONTROL possede
l'output musical et garantit START pour une configuration/asset/workload
produit legaux. Storage possede SD, fichiers, maps physiques et lecture. Les
commandes et completions de pages sont tokenisees, bornes et sans
pointeur.

Une page suit `FREE -> RESERVED -> LOADING -> READY`, `EVICTING`, ou `FAILED`. Une page LOADING n'est ni recyclable ni evictable. Pour recycler, STORAGE publie d'abord `EVICTING`, relit l'union des leases, restaure `READY` si la page est protegee, sinon passe `FREE`. AUDIO etend son lease avant resolution puis revalide `READY/key/registration_epoch/generation`. La completion valide key, slot, page generation, registration epoch et token; une completion tardive ne devient jamais visible.

## Lease physique et service

M7 ne publie aucun snapshot de playhead, frame cursor, deadline, pitch, phase,
loop state ou demande I/O. Chaque lecteur expose uniquement le lease seqlocke
`{seq, key, registration_epoch, ranges[2]}`. Un range est
`{first_page, page_count}`. Il enumere les pages physiques que le lecteur peut
encore lire; le second range sert au wrap discontinu. Les lecteurs Streamer et
OVERDUB possedent chacun leur lease et peuvent partager les memes pages immuables.

Le scheduler M4 sert les lecteurs actifs en round-robin. Il derive localement
le lookahead produit a partir des ranges proteges; AUDIO ne publie ni liste de
besoins ni wake Storage. Une page par lecteur et par passe; aucune horloge
STREAM, low-water dynamique ou prediction temporelle ne conditionne le service.

Le contrat produit garantit un pre-socle de 16384 frames par sample et derive le nombre de pages du format stereo canonique. Les limites Stream/Multi sont publiees avant jeu. Il n'existe ni READY par note, ni ACK START, ni retry, rollback ou fallback musical. Un underrun dans ce workload est une rupture de contrat, pas une admission tardive.

## I/O et cadence

Le service Storage traite une commande bornee hors IRQ. Le quantum de lecture reste 32 KiB et chaque page fait 64 KiB. Le backend physique resout des extents vers une FIFO DMA bornee et lit le payload FLOAT32 directement dans la page finale; FatFs reste le fallback cold-path vers cette meme page. Read-ahead ne change ni ordre, besoins ni lifecycle.

Le transport contient geometrie source, format et token. STORAGE remplit le
payload partage et publie READY; AUDIO invalide avant lecture. H743
et H747 conservent le meme contrat: M4 possede metadata et I/O, M7 possede les
credits de lecture.

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

Sample RAM charge le payload FLOAT32 stereo directement dans son allocation, par etapes sous un budget cooperatif de 2 ms et avec lectures adaptatives de 4 ou 16 KiB. Wavetable applique le meme budget, ajoute parse, CRC, mipmaps et preview, puis ecrit le cache `.B6WT` transactionnel sur cold path. Le loader asynchrone valide et recharge directement ce cache lors des loads suivants; un cache absent, invalide ou obsolete relance seul le build. Le format WAVE interne canonique est mono FLOAT32 normalise, 1024 samples par frame, avec mipmaps FLOAT32 band-major 1024/512/256/128/64/32/16/8. Les strides physiques sont egaux aux tailles logiques, sauf la bande 8 dont le stride est 16 samples avec 8 floats de padding; chaque frame commence ainsi sur une limite de 32 octets et aucune duplication cyclique n'est stockee. La geometrie source 1024/2048 est un choix explicite de l'importeur; les API sans geometrie explicite choisissent 2048. Les cycles 2048 sont convertis en 1024 dans le domaine frequentiel. Chaque bande est ensuite generee directement depuis la FFT canonique 1024, avec transition raised-cosine, marge avant Nyquist et bin Nyquist nul; aucune cascade ni saturation post-IFFT n'est appliquee. Le cache prepare est en version physique 5 et sa revision de preparation est 9; les anciennes preparations sont rejetees et regenerees depuis le WAV source. L'ancien slot reste publie jusqu'au swap du candidat, puis ses pages sont liberees.

Les payloads Sampler RAM/Wavetable sont des references `{region, offset, length}`. CONTROL clean avant publication, AUDIO invalidate avant installation. Un unload/remplacement suit `STOP -> invalidation voix synchrone -> avancee du tail FIFO -> FREE CONTROL`. Les ACK Multi/RAM/Wavetable et leur ring IPC ont ete supprimes; seul le fence du consumer physique est lu.

Le registre compact de leases Stream est fixe, pointer-free, seqlocke et place explicitement dans la fenetre IPC partagee SRAM3/D2. Les snapshots de besoins, pins, use-counts et refcounts de pages ont ete supprimes. REC_SOURCE publie seulement une generation immutable READY; AUDIO conserve ses playheads et ses leases.

## Format audio

Une page sample produit de 64 KiB porte 8192 frames FLOAT32 stereo entrelacees. Format, stride et frames/page sont derives par `sample_audio_format.h` et restent immutables pendant la voix.

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
directement dans son allocation FLOAT32. Le PCM24 du Recorder reste une
frontiere distincte; ses pages sont converties en place, de la fin vers le
debut, avec un unique tampon borne de 512 frames.

Preview est un ring PCM SPSC distinct. Le building REC utilise la carte append-only du Recorder; il n'est publie qu'apres finalisation et prechauffage des pages initiales. Le detail appartient a [recorder_sd.md](recorder_sd.md).
