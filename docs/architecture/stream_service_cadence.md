# Cadence du service streamer monocœur

Le streamer possède désormais une voie de service prioritaire explicite,
`brick6_stream_service_task`, distincte des travaux SD de fond. Chaque demi-bloc
audio publie uniquement un réveil monotone depuis l'IRQ. Aucun accès FatFs,
aucune sélection et aucun décodage ne sont exécutés dans cette IRQ.

Sur le M7 monocœur, la voie prioritaire est interrogée avant les clients SD de
fond et une seconde fois avant le traitement UI. Un réveil survenu pendant une
lecture synchrone n'est pas perdu : le service acquitte la séquence capturée à
son entrée, ce qui force un nouveau passage au checkpoint suivant. Tant qu'un
backlog subsiste, le service reste éligible même sans nouveau réveil audio.

Le contrat de cadence cible est de 256 frames audio. Les compteurs exposent le
nombre de réveils, les passages, les refus liés à l'exclusivité bulk, le délai
maximal en frames audio et les dépassements de cadence avec backlog. Ces mesures
permettent de vérifier sur matériel la borne réellement obtenue par le chemin
coopératif.

Le bulk-loader Multi conserve son exclusivité et suspend cette voie puisque son
contrat impose un transport arrêté et aucune voix streamée active. Les autres
clients SD restent derrière le streamer grâce au gate `streaming_critical`.

La frontière est conçue pour être déplacée ultérieurement : le M7 publiera le
réveil et les demandes, tandis que le M4 exécutera l'ordonnanceur I/O. Cette passe
reste synchrone et n'introduit ni PendSV, ni FatFs en interruption, ni DMA SD
asynchrone. La décision DMA demeure conditionnée aux mesures de l'architecture
synchrone finale.
