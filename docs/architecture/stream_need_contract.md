# Sampler, assets, streaming et page-cache

## Ownership

AUDIO possede besoins, admission des voix, page-cache, generations, pins, refcounts, eviction et publication READY. Storage possede SD, fichiers, maps physiques, lecture et decode. Les commandes et completions sont tokenisees, bornes et sans pointeur.

Une page suit `FREE -> RESERVED -> LOADING -> READY` ou `FAILED`. Une page LOADING n'est ni recyclable ni evictable. La completion valide key, slot, page generation, registration epoch et token; une completion tardive ne devient jamais visible.

## Besoins et admission

Chaque voix Classic/Multi publie un snapshot et jusqu'a six besoins mobiles, plus le pre-socle loop forward. Le generateur commun calcule pages forward et loop et supprime les doublons. Reverse et ping-pong restent propres au Sampler RAM.

Le scheduler sert les voix admises en round-robin fixe Classic puis Multi. Une page par voix et par passe; une voix sans besoin chargeable est sautee. Le curseur survit aux retours superloop. Les deadlines sont diagnostiques et ne remplacent pas cet ordre.

L'admission calcule debit source, voix, sources distinctes et reserve de changement avant publication. Les seuils conservateurs doivent etre calibres sur carte; refus et liberations sont generationnels et traces.

## I/O et cadence

Le service Storage traite une commande bornee hors IRQ. Produit: tranche 32 KiB; page temporaire 16 KiB. Le backend physique resout des extents vers une FIFO DMA bornee; FatFs reste le fallback. Read-ahead ne change ni ordre, besoins ni lifecycle.

Le transport contient geometrie source, format et token. Storage decode dans un payload partage; AUDIO invalide, copie dans la page finale et publie READY. H743 utilise l'adaptateur local `sample_page_cache_port`; H747 conserve le contrat avec M4 executant l'I/O et M7 possedant le cache.

## Multi, Sampler RAM et Wavetable

Le bulk Multi calcule et epingle l'union start/loop, utilise le cache, le transport et le scheduler communs, par lots de 64 KiB. Il ne possede ni FatFs, ni decodeur, ni arbitre SD parallele.

Sample RAM charge par etapes, lectures de 4096 octets et conversion de 256 frames. Wavetable ajoute parse, CRC, mipmaps, cache `.B6WT` transactionnel et preview; une FFT/bande ou 256 samples sont traites par quantum. Le format WAVE interne canonique est mono FLOAT32 normalise, 1024 samples par frame, avec mipmaps FLOAT32 band-major 1024/512/256/128/64/32/16/8. Les strides physiques sont egaux aux tailles logiques, sauf la bande 8 dont le stride est 16 samples avec 8 floats de padding; chaque frame commence ainsi sur une limite de 32 octets et aucune duplication cyclique n'est stockee. La geometrie source 1024/2048 est un choix explicite de l'importeur; les API historiques choisissent le contrat legacy 2048. Les cycles 2048 sont convertis en 1024 dans le domaine frequentiel. Chaque bande est ensuite generee directement depuis la FFT canonique 1024, avec transition raised-cosine, marge avant Nyquist et bin Nyquist nul; aucune cascade ni saturation post-IFFT n'est appliquee. Le cache prepare est en version physique 5 et sa revision de preparation est 9; les anciennes preparations sont rejetees et regenerees depuis le WAV source. L'ancien slot reste publie jusqu'au commit du candidat.

Les payloads Sampler RAM/Wavetable sont des references `{region, offset, length}`. CONTROL clean avant publication, AUDIO invalidate avant installation. Un unload/remplacement suit `STOP -> invalidation voix + ACK AUDIO -> FREE CONTROL`.

## Format audio

Une page de 16 KiB porte 4096 frames mono FLOAT32 ou 2048 frames stereo. Format, stride et frames/page sont derives par `sample_audio_format.h` et restent immutables pendant la voix. Mono reste mono jusqu'au pan/spread final; aucune duplication droite de rejet n'est conservee.

Preview est un ring PCM SPSC distinct. Les prises Looper actives utilisent la carte append-only du Recorder et ne lisent jamais au-dela du tail physiquement committed; le detail appartient a [recorder_sd.md](recorder_sd.md).
