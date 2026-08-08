# Cadence du service streamer H743

L'IRQ de demi-bloc audio publie uniquement un réveil monotone. Le tasklet
`brick6_stream_service_task` centralise la cadence stockage et consomme le
registre borné des voix. Aucun FatFs, décodage, scan de cache ou travail lourd
n'est exécuté dans l'IRQ audio.

Le service H743 traite une commande bornée à la fois, avec un budget de 32 Kio,
avant les clients SD de fond puis avant l'UI. Un réveil reçu pendant une lecture
synchrone reste visible au checkpoint suivant. Le gate `streaming_critical` est
actif tant qu'une page streamer reste chargeable ou LOADING ; il ne repose plus sur un
seuil d'avance. Le bulk Multi obtient une exclusivité explicite et utilise des
lots bornés à 64 Kio.

Le curseur et l'état du tour round-robin persistent entre les appels. Chaque
opportunité reprend au slot suivant, même lorsque le budget de tranche de 32 Kio
rend la main à la superloop. Un tour comporte exactement
`SAMPLE_STREAM_PAGES_PER_VOICE_PER_ROUND` passages complets des slots admis ;
une voix sans page chargeable est sautée pour le passage courant. Il n'existe
plus de plafond global de pages par appel, de limite historique en ticks ni de
limite d'opérations FatFs. Le budget octets borne seulement une tranche de
service et ne tronque donc plus le tour logique.

BENCH conserve les volumes, latences, appels FatFs, ouvertures, seeks, décodage
et backlog. La trace causale stable utilise `NEED_ADD`, `NEED_DROP`, `SELECT`,
`LOAD_BEGIN`, `LOAD_END`, `READY` et `CONSUME_MISS`. Les diagnostics obsolètes
de l'ancien ordonnanceur sont absents.

La cible de service reste 256 frames de sortie et la page produit temporaire
reste 16 Kio. Le temps maximal d'un tour, le read-ahead, le nombre de passes,
la profondeur d'avance et les seuils d'admission doivent être calibrés sur H743
avec SD réaliste, Classic/Multi simultanés, loop forward, pages partagées et
bulk Multi, notamment avec huit voix au pitch produit maximal.
