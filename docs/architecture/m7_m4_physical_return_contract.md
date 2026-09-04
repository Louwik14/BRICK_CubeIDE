# Contrat physique AUDIO vers CONTROL

## Verdict architectural

La musique est strictement M4 vers M7 par la FIFO fonctionnelle unique. Les
retours M7 vers M4 restants ne transportent aucune decision musicale:

- `control_audio_fifo.tail`: liberation physique des cases et mesure de
  capacite SPSC seulement, jamais preuve generique de retrait de ressource;
- lease STREAM seqlocke par lecteur: cle, epoch de registration et jusqu'a deux
  ranges de pages physiques protegees;
- Recorder PCM: ring stereo PCM24, head de frames produites, tail de frames
  liberees, session I/O et longueur exacte au stop;
- niveau REC, waveform audio et waveform synth, chacun avec publisher AUDIO et reader CONTROL separes;
- diagnostic Audio: boot/error et, pour la charge CPU, uniquement `{valid, avg_permille}`. Le couper
  ne modifie aucune commande, voix, page ou decision CONTROL.

Les retours physiques restent limites au `tail` FIFO, aux leases STREAM et au
PCM/framing Recorder. Les projections sont limitees au niveau
REC, aux deux waveforms et au diagnostic Audio. Les boundaries Recorder/Looper ne
necessitent aucun evenement AUDIO vers CONTROL separe: CONTROL publie
directement le RECORD date; le head final et le framing de session sont des
faits physiques.

## Cadence et boot

M4 avance de facon autonome. TIM12 porte le tick du tempo interne; TIM5,
demarre avant les domaines et derive du meme HSE que SAI, est la reference
absolue commune. CONTROL possede son extension et sa conversion; M7 initialise
sa sample clock locale depuis TIM5 au premier callback valide. Les callbacks SAI ne reveillent aucun code CONTROL. PendSV reste exclusivement
reserve au noyau FreeRTOS; le deferred USB MIDI est traite par USB_SERVICE.

Le nominal ne lit aucun etat boot AUDIO et aucun transport Clock M7->M4
n'existe. L'unique
publication boot restante est `{state=FAULT, erreur_hardware}` pour l'ecran de
diagnostic; elle ne porte aucun READY musical.

FILTER POS et les contraintes de placement sont resolus depuis
`track_sound_state`, `track_runtime`, la topologie et l'etat de polyphonie
CONTROL. La valeur DSP privee n'est plus publiee ni relue par UI/CONTROL.

## Data planes et ownership

| Data plane | Producteur | Consommateur | Ownership et reutilisation |
|---|---|---|---|
| Sample RAM | M4/Storage | M7/AUDIO | slot retire apres fence `tail`; token de load protege les completions SD tardives |
| Wavetable/mipmaps | M4/Storage | M7/AUDIO | projection immutable; pages liberees apres fence `tail`; generation de load/registry physique conservee |
| Multi descriptors/pages | M4/Storage | M7/AUDIO | descripteurs publies puis pages protegees par les leases lecteurs; retrait apres fence `tail` |
| STREAM pages | M4/Storage | M7/AUDIO | cache partage; un lease par lecteur, union M4, `EVICTING` puis relecture; aucun pin/use-count/refcount |
| Preview PCM | M4/Storage | M7/AUDIO | ring SPSC separe; reutilisation par consumer tail |
| Recorder PCM | M7/AUDIO | M4/Storage | ring SPSC; M7 head `accepted_frames`, M4 tail `released_frames`; stop fixe le head final |
| Looper preroll/live map | M7/AUDIO puis M4/Storage | M7/AUDIO | preroll borne puis carte physique append-only; lecture limitee au tail SD committed |

Les tokens/generations conserves appartiennent aux loads SD, registrations de
buffers et sessions Recorder. Ils rejettent une completion I/O obsolete; ils
ne valident jamais PROGRAM/PARAM/NOTE et ne reconstruisent aucun etat musical.

## Compatibilite

H743 utilise exactement les memes contrats via les adaptateurs locaux. H747
place M4 et M7 de part et d'autre des memes structures pointer-free pour les
data planes enumeres. USB Audio est une exception explicite: le transport USB
et le runtime Audio restent M4-local sur H743, tandis que la cible H747 repartit
USB sur M4 et Audio sur M7. Le ring PCM existant prepare ce port, mais ownership,
handshake, reset, backpressure et activation inter-core ne sont pas encore
definis.
