# Audit du contrat Groove vers les destinations

Audit realise sur `e6132389d` (HEAD avant ajout de ce rapport et du test de
contrat).

## Contrat reel

La source step est lue par `schedule_step`. Le microtiming puis Quantize sont
appliques par `seq_timing_source_timestamp`; ils fixent le timestamp source.
La note traverse ensuite la chaine Note FX. `fx_terminal` est l'unique sortie
terminale et appelle `seq_timing_finalize` pour toutes les pistes.

Le finalizer echantillonne le template a la position post-NoteFX. `Timing` et
`Global` dosent l'offset du template. `Random` ajoute une valeur deterministe
par noeud, interpolee, dosee par `Random * Global^2`. `Velocity` interpole le
relief du template et dose la velocite source par `Velocity * Global`.
Quantize n'est pas refait dans le finalizer: il a deja transforme la position
source. Pour une note programmee, debut et fin sont finalises afin de conserver
une duree positive. Une note live immediate conserve son timestamp de capture,
mais sa velocite traverse le meme finalizer.

Le resultat est un `note_event_t` finalise, admis dans le ledger puis publie
comme `seq_terminal_event_t`. AUDIO, MIDI, synthese, drums et samplers ne
relisent aucun step. Ils consomment le timestamp du bloc terminal et la
velocite terminale.

## Comparaison poly, TB303 et ACID

La premiere divergence est apres `audio_command_executor_apply_seq_event`:
`audio_note_engine_adapter_apply_output` choisit le moteur physique. Elle est
donc posterieure a Quantize, Note FX, Timing, Random, Velocity, admission et
publication. TB303 et ACID recoivent explicitement `note` et `velocity` du
terminal, comme les moteurs polyphoniques. Leur capacite logique vaut un, mais
elle ne cree aucun scheduler ni chemin de note distinct.

Slide est un p-lock de transition publie avant OFF/PARAM/ON a timestamp egal.
Les moteurs mono conservent leur etat gate/pending-release pour etablir le
legato. Groove deplace les NOTE_ON/OFF finalises; il ne reconstruit ni leur
timestamp ni leur velocite dans les moteurs. Accent est un parametre moteur;
la velocite Groove lui parvient toutefois sans reecriture.

## Matrice des destinations

| Famille | Timing | Velocity | Random | Quantize | Global |
|---|---|---|---|---|---|
| Synth poly | oui | oui | oui | oui | oui |
| Synth mono | oui | oui | oui | oui | oui |
| TB303 / ACID | oui | oui | oui | oui | oui |
| Drums | oui | oui | oui | oui | oui |
| Sampler / streamer | oui | oui | oui | oui | oui |
| MIDI | oui | oui | oui | oui | oui |
| Chord / ARP / generateurs | oui | oui | oui | oui | oui |

Pour les generateurs, la note generee est finalisee apres Note FX; le Random
emploie son identite temporelle propre. Une destination peut ne pas produire
de difference sonore avec la velocite, mais la valeur reste propagee: c'est
une limite du moteur, pas une perte du contrat.

## Recherche de bypass

La seule exception volontaire est `NOTE_EVENT_TIMING_LIVE_IMMEDIATE`: Timing,
Random temporel et Quantize ne retardent pas un jeu live. La Velocity Groove
reste appliquee. Les relectures Live Rec sont marquees terminales pour eviter
de rejouer Note FX, mais repassent par Quantize source et le finalizer Groove.

Les p-locks lisent le step a sa boundary nominale. Ce ne sont pas des notes et
ils ne portent ni timestamp Groove ni velocite; ils etablissent l'etat moteur
du step avant la note finalisee. Les p-locks slide disposent en plus de la
classe transition, ordonnee avant les changements de note. Aucun chemin trouve
ne remplace ensuite un timestamp ou une velocite finalise.

## Conclusion et cout

Le symptome ne provient pas d'une fuite mono au HEAD audite. Le contrat est
deja unique en amont des destinations; ajouter une branche TB303/ACID ou un
second scheduler serait incorrect. Aucun changement runtime n'est necessaire:
0 octet de RAM persistante, 0 octet de RAM runtime, 0 operation et 0 branche
par note. Le test `tools/test_seq_groove_destination_contract.ps1` verrouille
l'absence de dispatch destination dans le finalizer, la consommation du
terminal par MIDI et les deux destinations mono, ainsi que le contrat slide.

Une validation hardware reste utile avec un template aux offsets tres marques,
Random nul, puis avec Velocity seule. Il faut capturer les NOTE_ON de poly,
TB303 et ACID au meme BPM et verifier les memes offsets en samples; pour slide,
verifier une paire chevauchante puis une transition non-slide.
