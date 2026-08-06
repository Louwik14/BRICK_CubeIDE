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

## Segmentation et NoteFx

La file live alimente le même calcul de frontière que le séquenceur et que les
échéances internes de NoteFx. Le préfixe audio est rendu jusqu'au deadline,
l'événement source est soumis avec son `sample_time` absolu, puis le suffixe est
rendu. Les événements NoteFx générés conservent leur échéance absolue et
`note_fx_pipeline_frames_until_deadline()` l'expose au scheduler.

Une note immédiate d'un MIDI FX reprend le timestamp de sa source. Une note
retardée ou une échéance d'arpège est traitée par le moteur NoteFx à son sample
absolu. Les événements séquencés restent dans leur propre flux ordonné ; aucun
ordre global note-off avant note-on n'est imposé aux événements live.

## Garde de transport fixe

`LIVE_GUARD_SAMPLES` vaut une demi-zone SAI (`BOARD_AUDIO_FRAMES_PER_HALF`,
64 samples sur les deux cartes). Cette valeur couvre la frontière audio normale
entre la capture et la prochaine consommation, avec une politique identique pour
Hall, USB Device et USB Host. Le sample final est donc la capture TIM5 convertie
plus cette garde. Si l'ingestion dépasse cette échéance, l'événement est clampé
au premier sample encore modifiable et les diagnostics `late`/retard maximal
le signalent ; l'audio déjà rendu n'est jamais réécrit.
