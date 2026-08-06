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

## Panic MIDI prioritaire

Les controles MIDI CC120 et CC123 ne sont pas inseres dans la file ordinaire.
`note_fx_pipeline_request_panic()` publie une commande scalaire prioritaire,
toujours disponible, sans pointeur ni allocation. La demande est ordonnee par
l'audio au premier sample encore modifiable, avant les evenements ordinaires
du meme offset.

Le proprietaire audio invalide les commandes et echeances anterieures,
supprime les sorties ARP/NoteFx en attente, puis ferme les admissions
terminales via `seq_play_scheduler_panic_audio()`. Si un arret terminal ne
peut pas etre admis au premier passage, la commande prioritaire reste active
et est retentee au passage audio suivant; aucune note n'est abandonnee au
profit d'une insertion forcee dans la file ordinaire. Le traitement est
idempotent.

Les commandes publiees apres la demande portent l'epoch suivant et ne sont
pas purgees. Une nouvelle note recue apres le panic peut donc etre appliquee
normalement apres la frontiere de fermeture. Le chemin MIDI ne ferme jamais
directement une voix ou un moteur; NoteFx/audio reste l'unique autorite de
fermeture.

## Live Recording

L'autorite temporelle est unique et lineaire :

`TIM5 capture -> conversion audio -> garde -> clamp au premier sample rendu -> effective_sample_time`

La conversion, la garde et le clamp sont effectues une seule fois par la file
live audio-owned. Cette valeur effective est partagee par l'application audio,
le note-on, le note-off, le calcul de duree et la conversion vers la position
musicale du Live Recording. Le recording ne relit donc pas la timeline audio et
ne recalcule pas d'echeance.

Le transfert audio vers le Live Recording est borne, fixe et sans pointeur. Les
evenements Hall/MIDI timestampes sont draines hors de l'IRQ audio ; les appels
sans timestamp restent uniquement les wrappers de compatibilite des chemins
non temps reel. L'`occurrence_id` est conserve pour apparier les retriggers et
les note-off.

La quantification est appliquee une seule fois, uniquement a la representation
enregistree : quantification OFF conserve la position et le microtiming issus de
`effective_sample_time`; quantification ON applique la grille active du track
au resultat musical. Elle ne modifie jamais le sample entendu ni
`effective_sample_time`.

Les paquets Host rejetes au point de saturation de sa file RX incrementent le
diagnostic Host `USBH_MIDI_GetRxOverflowCount()` ; ce compteur est distinct des
compteurs Device et NoteFx et aucun log n'est produit dans le chemin chaud.

`NOTE_FX_SAMPLE_TIME_AUDIO_OWNER` reste uniquement le marqueur de compatibilite
des soumissions non timestampes. Il ne doit jamais etre utilise pour resoudre
l'heure d'un evenement Hall/MIDI capture.
