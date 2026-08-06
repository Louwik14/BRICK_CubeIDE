# File temporisée des événements live

La file audio-owned des événements live est la seule structure qui ordonne les
événements Hall et MIDI après leur ingestion. Elle contient au plus 31 éléments,
soit la capacité utile de la file de commandes NoteFx existante (32 cases, une
case réservée à la distinction plein/vide). Cette capacité couvre un burst USB
borné, un accord Hall et les événements live générés avant leur consommation,
sans allocation dynamique.

Chaque élément est fixe et sans pointeur : `sample_time`, `ingress_serial`,
`occurrence_id`, type, source, track, note et vélocité. Le tri est lexicographique
sur `(sample_time, ingress_serial)`. À timestamp égal, l'ordre d'ingestion est
donc conservé ; l'`occurrence_id` reste l'identité autoritaire d'un retrigger.
Les événements séquencés continuent d'emprunter leur chemin sample-accurate
existant et ne sont pas réordonnés par cette file.

La conversion TIM5 vers la timeline audio est effectuée par le propriétaire
audio. Une échéance future reste en file. Une échéance dépassée est appliquée au
premier sample encore modifiable et compte comme `late`; le retard maximal est
mesuré. Au-delà de 48 000 samples (1 seconde), l'événement compte aussi comme
`stale`, mais reste clampé afin de ne pas abandonner un note-off. Une file pleine
rejette l'événement de façon déterministe et incrémente `queue_drop_count`.

La prochaine échéance live est fournie au même calcul de frontière que les
échéances NoteFx existantes. Le bloc est ainsi découpé avant le deadline, puis
l'événement est appliqué avant le rendu du sample concerné. L'audio déjà rendu
n'est jamais réécrit.

Le producteur ne dépend que de l'API d'ingestion et d'un événement copiable.
Le format peut donc être placé ultérieurement en RAM partagée M4/M7 sans
modifier le moteur Hall ni le consommateur audio.
