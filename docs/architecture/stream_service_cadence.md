# Cadence du service streamer H743

L'IRQ de demi-bloc audio publie uniquement un réveil monotone. Le tasklet
`brick6_stream_service_task` centralise la cadence stockage et consomme le
registre borné des voix. Aucun FatFs, décodage, scan de cache ou travail lourd
n'est exécuté dans l'IRQ audio.

Le service H743 traite une commande bornée à la fois, avec un budget de 32 Kio,
avant les clients SD de fond puis avant l'UI. Un réveil reçu pendant une lecture
synchrone reste visible au checkpoint suivant. Le gate `streaming_critical` est
piloté par le tasklet à partir des besoins non READY proches ; le bulk Multi
obtient une exclusivité explicite et utilise des lots bornés à 64 Kio.

BENCH conserve les volumes, latences, appels FatFs, ouvertures, seeks, décodage
et backlog. La trace causale stable utilise `NEED_ADD`, `NEED_DROP`, `SELECT`,
`LOAD_BEGIN`, `LOAD_END`, `READY` et `CONSUME_MISS`. Les diagnostics obsolètes
de l'ancien ordonnanceur sont absents.

La cible de service reste 256 frames de sortie. Sa marge réelle, le read-ahead,
les budgets et les seuils d'admission doivent être calibrés sur H743 avec SD
réaliste, Classic/Multi simultanés, loop forward, pages partagées et bulk Multi.
