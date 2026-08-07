# Streaming Classic/Multi fiable à froid

## Contrat des bords

Le présocle conserve 12 288 frames : trois pages mono ou six pages stéréo.
Un départ Classic ne devient jouable que lorsque toute sa fenêtre de départ est
`READY`; les pages restent détenues par l'owner de voix. Le chargement d'un
instrument Multi prépare et épingle l'union des présocles de départ et de loop.
Les pages communes ne sont ni allouées ni épinglées deux fois. L'instrument ne
passe à `READY` qu'après chargement de cette union. Le start gate et le loop gate
refusent donc un départ froid incomplet au lieu de laisser l'IRQ audio consommer
une page `QUEUED`.

Les owners de fenêtre mobile et de loop restent distincts. Leur génération est
libérée par les chemins différés existants lors de l'arrêt ou du remplacement de
la voix. Le cache physique reste partagé par `(domain, sample, page)`.

## Horizon et ordonnanceur

La fenêtre physique reste à trois/six pages. Chaque page reçoit toutefois un
rôle explicite (`start`, `loop`, `current`, `neighbor`, `anticipation`) et une
deadline exprimée en frames de sortie. Le calcul divise la distance source par
le ratio Q16 de lecture. Une page devient :

- urgente si elle est courante, de départ, ou à moins de 1 024 frames ;
- normale si elle appartient au bord de loop ou à l'horizon de 12 288 frames ;
- spéculative au-delà.

La sélection reste EDF globale. Le budget de 32 Kio, la limite de 2 ms et le
maximum de 16 pages restent les bornes du travail non critique. Dès qu'une
deadline absolue entre dans la garde de 4 096 frames (environ 85 ms à 48 kHz),
ces bornes deviennent souples : le même passage hors IRQ poursuit la sélection
EDF jusqu'à disparition de toute dette critique. Des checkpoints streamer entre
les services coopératifs de la superloop bornent en outre l'espacement par le
coût d'un seul segment applicatif. Les pools et la géométrie 3/6 ne changent pas.
Le verrou SD `streaming_critical` continue d'interdire les clients secondaires
tant qu'une fenêtre active est détenue.

Le backend contigu conserve une lecture SD multibloc par page physique. Le
fallback FatFs utilise par défaut des sous-lectures de 16 Kio. Aucun regroupement de pages
supplémentaire n'est activé sans mesure matérielle démontrant un meilleur temps
maximal; le débit moyen seul n'est pas un critère suffisant.

## Trace Release bornée

Définir `BRICK6_STREAM_TRACE=1` dans les options C du build Release active une
trace DWT de 32 opérations. Elle est désactivée par défaut et son code est retiré
par le préprocesseur.

Le snapshot GDB `g_sample_stream_trace` contient les identifiants source/page,
rôle, priorité, deadline, owner/génération, position, demande/sélection/début et
fin SD/READY, backend, octets, nombre de lectures physiques, résultat, backlog et
intervalle du service. Il expose aussi les maxima de lecture, attente, intervalle
de service, retard, backlog, changements de fichier et pages servies par passage.
La trace se fige au premier retard de sélection ou à la première acquisition
d'une page non `READY`. Aucun texte, fichier, UART, USB ou affichage n'est utilisé.

Les durées sont des cycles DWT. Les tests matériels doivent relever les valeurs
avec mono/stéréo, Classic/Multi, loop/non-loop, un à huit voix, fichiers partagés
et distincts, déclenchement simultané et ratios réalistes élevés.
