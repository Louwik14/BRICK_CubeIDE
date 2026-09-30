# Contrat physique AUDIO vers CONTROL

> Historique hors Streamer. Le Streamer est desormais entierement local au M7;
> son contrat courant est defini par `stream_need_contract.md`.

## Verdict architectural

La musique passe strictement de CONTROL/SEQ vers AUDIO par la FIFO locale
unique du M7. Les retours AUDIO vers les services cooperatifs ne transportent
aucune decision musicale:

- `control_audio_fifo.tail`: liberation physique des cases et mesure de
  capacite SPSC seulement, jamais preuve generique de retrait de ressource;
- credit STREAM compact par voix: page courante, longueur de fenetre et bornes
  de loop forward;
- niveau REC, waveform audio et waveform synth, chacun avec publisher AUDIO et reader CONTROL separes;
- diagnostic Audio: boot/error et, pour la charge CPU, uniquement `{valid, avg_permille}`. Le couper
  ne modifie aucune commande, voix, page ou decision CONTROL.

Les retours physiques restent limites au `tail` FIFO et aux data planes encore
reellement separes. Les projections sont limitees au niveau REC, aux deux
waveforms et au diagnostic Audio. Le ring Recorder et son framing sont locaux
au M7 entre IRQ AUDIO et superloop STORAGE.

## Cadence et boot

CONTROL et SEQ avancent de facon autonome. TIM12 porte le tick du tempo interne; TIM5,
demarre avant les domaines et derive du meme HSE que SAI, est la reference
absolue commune. CONTROL possede son extension et sa conversion; M7 initialise
son curseur de rendu intra-bloc depuis la phase DMA placee dans la media clock TIM5 canonique. Les callbacks SAI ne reveillent aucun code CONTROL. PendSV reste reserve
au transport USB MIDI local et ne sert plus le sequenceur.

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
| Multi descriptors/pages | M4/Storage | M7/AUDIO | `RETIRING` bloque toute nouvelle programmation; la projection reste resolvable jusqu'au `MULTI_RESOURCE_STOP`, puis le slot n'est libere qu'apres franchissement wrap-safe du `head` capture par `tail` et extinction des leases |
| STREAM pages | M7/STORAGE | M7/AUDIO | backing local; un lease seqlocke par lecteur, `EVICTING` puis relecture; aucun transport inter-core |
| Preview PCM | M4/Storage | M7/AUDIO | ring SPSC separe; reutilisation par consumer tail |
| Recorder FLOAT32 | M7/AUDIO IRQ | M7/Storage | ring SPSC local; AUDIO publie `produced_frames`, STORAGE `released_frames`; stop fixe le head final |
| REC_SOURCE | M4/Storage | M7/AUDIO | snapshot immutable current; generation A/B retiree apres extinction des leases |

Les tokens/generations conserves appartiennent aux loads SD, registrations de
buffers et sessions Recorder. Ils rejettent une completion I/O obsolete; ils
ne valident jamais PROGRAM/PARAM/NOTE et ne reconstruisent aucun etat musical.

## Compatibilite

Le Recorder et le Streamer sont des data planes monocoeur. Ils ne font plus
partie du contrat de compatibilite physique M4/M7 ni d'une ABI pointer-free.
