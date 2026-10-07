# START et END live pour STREAM

## Contrat produit

Les clips `TRACK_RUNTIME_TYPE_STREAM` exposent `PARAM_SAMPLER_START` et
`PARAM_SAMPLER_LENGTH` (libelle UI `End`) avec les memes normalisations que le
Sampler RAM. START designe le debut normalise; END conserve la semantique RAM
existante de longueur normalisee de region. La region effective est toujours
bornee et non vide.

Une voix RAM garde sa reconciliation live historique, sans throttle. Une voix
Multi n'est pas concernee: son plan est construit par zone/instrument avec
`start_frame = 0`, et son catalogue ne publie pas START/END.

Pour une voix Clip STREAM deja active, START et END partagent une fenetre de
publication de 4000 frames AUDIO, soit 83,333 ms a 48 kHz et au plus 12
applications par seconde. Le premier changement apres expiration de la fenetre
est traite immediatement. Les valeurs suivantes sont coalescees; seule la plus
recente est appliquee a la fenetre suivante. Un nouveau trigger construit en
revanche directement son plan depuis les valeurs courantes, sans attendre ce
throttle live.

## Chemin START

Au tick autorise, AUDIO calcule la region et la frame START cible. Si la page est
READY, le reader est repositionne au point sur, avec le declick court existant,
sans transaction SD. Sinon AUDIO publie seulement la page cible dans le lease du
reader. CURRENT, NEXT et LOOP_START restent proteges; la cible speculative prend
temporairement le role LOOP_START_NEXT. La lecture courante continue pendant que
STORAGE alloue, charge par DMA asynchrone et publie la page.

AUDIO teste READY aux blocs suivants et n'applique le nouveau plan et le seek
qu'apres publication coherente. Il n'attend jamais STORAGE et n'effectue aucune
lecture synchrone. Une generation logique identifie la valeur la plus recente.
Si une nouvelle valeur arrive pendant le chargement, l'ancien besoin est retire
du lease et ne peut plus etre applique; une completion deja engagee peut finir
dans le cache, sans devenir la cible active.

## Chemin END

END met a jour `region_end`, `loop_end` et les besoins du reader sans demander de
page si la position reste dans la nouvelle region. Si END passe derriere la
position courante, la reconciliation suit RAM: une loop revient au debut de
region; un one-shot se place sur la derniere frame. START et END recus ensemble
sont resolus atomiquement dans une seule region, ce qui preserve `START < END`.

## Diagnostic hardware

`g_sampler_stream_live_diag` expose les compteurs `start_requested`,
`start_applied`, `end_applied`, `start_coalesced`, `start_cache_hits`,
`start_cold_loads`, `start_target_ready`, `start_obsolete`,
`start_pending_max` et `audio_miss`. La remise a zero se fait par
`brick6_sampler_runtime_stream_live_diag_reset()`. `start_pending_max` vaut au
plus un par construction: aucune FIFO de cibles obsoletes n'est entretenue.

Le cout AUDIO ajoute un calcul de region au plus a 12 Hz par track, une
publication de lease uniquement quand son contenu change et un test READY par
bloc pendant une cible froide. Le moteur de modulation, le sequenceur, la taille
de page 64 KiB, le scheduler STORAGE et le contrat D-cache ne changent pas.
