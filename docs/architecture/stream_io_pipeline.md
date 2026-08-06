# Pipeline d'alimentation des pages streamées

L'étape 5 sépare le traitement d'une demande retenue par l'ordonnanceur en trois
frontières explicites. Le manager construit une `sample_stream_io_command_t`
autonome, contenant le token de génération, la cible SDRAM et la description de
source. Le module I/O est seul propriétaire des handles FatFs et des lectures SD.
Le décodeur ne connaît ni FatFs, ni les voix, ni l'état du cache et convertit le
bloc source directement dans la page SDRAM finale. Le publisher est le seul point
qui transforme le résultat associé au token en `READY` ou `ERROR`.

Le flux monocœur est actuellement synchrone : ordonnanceur, I/O, décodage puis
publication sont appelés directement. Les structures de commande et de résultat
ne contiennent aucun pointeur de voix et constituent les futures frontières de
files M7 vers M4 et M4 vers M7. Une publication n'est acceptée que si le token
correspond toujours à la clé, au slot, à la génération de page et à l'époque
d'enregistrement ; une annulation ou une réutilisation du slot ne peut donc pas
publier une ancienne lecture.

Le backend contigu ne décode plus lui-même. Il lit un bloc physique dans le
scratch I/O et retourne la tranche PCM au décodeur commun. Le fallback FatFs
conserve un fichier par source ouverte, effectue un seek seulement lorsque la
position courante diffère, puis assemble la page source avant conversion. Les
pages ne deviennent visibles qu'après lecture et décodage complets, avec la
barrière mémoire déjà portée par la transition finale du cache.

Cette passe ne change volontairement ni la cadence du service, ni ses budgets,
ni la politique de groupement, ni la taille configurable des lectures. Ces sujets
appartiennent aux passes suivantes ; le transfert DMA asynchrone reste hors scope
tant que le service dédié synchrone n'a pas été mesuré.
