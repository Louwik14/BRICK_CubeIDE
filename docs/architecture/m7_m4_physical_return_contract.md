# Contrat physique M7 vers M4 — PASS B

## Verdict architectural

La musique est strictement M4 vers M7 par la FIFO fonctionnelle unique. Les
retours M7 vers M4 restants ne transportent aucune decision musicale:

- `control_audio_fifo.tail`: liberation physique des cases FIFO et fence de
  retrait de ressource;
- credit STREAM compact par voix: page courante, longueur de fenetre et bornes
  de loop forward;
- Recorder PCM: ring stereo PCM24, head de frames produites, tail de frames
  liberees, session I/O et longueur exacte au stop;
- anchor boot `{TIM5_tick, audio_sample}` unique; extrapolation ensuite;
- diagnostic: BOOT_FAULT, compteurs, xrun/underrun, CPU, waveform, VU et erreurs. Le couper
  ne modifie aucune commande, voix, page ou decision CONTROL.

Il n'existe plus de live clock periodique, wake AUDIO/PendSV vers CONTROL,
cadence frames AUDIO, appel Looper vers Recorder CONTROL, getter FILTER POS,
snapshot de playhead, deadline STREAM, ACK Multi/RAM/Wavetable, ring de retire
ACK, READY, binding ou generation musicale. Les boundaries Recorder/Looper ne
necessitent aucun evenement M7 vers M4 separe: CONTROL publie directement le
RECORD date; le head final et le framing de session sont le fait physique.

## Cadence et boot

M4 avance de facon autonome. TIM12 porte le tick du tempo interne; TIM5,
derive du meme HSE que SAI, projette la timeline sample depuis une seule ancre
boot. Les callbacks SAI ne reveillent aucun code CONTROL. PendSV reste reserve
au transport USB MIDI local et ne sert plus le sequenceur.

Le nominal ne lit aucun etat boot AUDIO. L'absence d'ancre suffit aux chemins
qui doivent savoir qu'aucun consumer AUDIO ne peut encore avancer. L'unique
publication boot restante est `{state=FAULT, erreur_hardware}` pour l'ecran de
diagnostic; elle ne porte aucun READY musical.

FILTER POS et les contraintes de placement sont resolus depuis
`track_sound_state`, `track_runtime`, la topologie et le shadow de polyphonie
CONTROL. La valeur DSP privee n'est plus publiee ni relue par UI/CONTROL.

## Data planes et ownership

| Data plane | Producteur | Consommateur | Ownership et reutilisation |
|---|---|---|---|
| Sample RAM | M4/Storage | M7/AUDIO | slot retire apres fence `tail`; token de load protege les completions SD tardives |
| Wavetable/mipmaps | M4/Storage | M7/AUDIO | projection immutable; pages liberees apres fence `tail`; generation de load/registry physique conservee |
| Multi descriptors/pages | M4/Storage | M7/AUDIO | descripteurs publies puis pages protegees par le credit de fenetre; retrait apres fence `tail` |
| STREAM pages | M4/Storage | M7/AUDIO | cache partage; M7 publie le credit compact, M4 ne reutilise pas une page encore dans une fenetre/pin/use-count |
| Preview PCM | M4/Storage | M4/UI/audio preview | ring SPSC separe; reutilisation par consumer tail |
| Recorder PCM | M7/AUDIO | M4/Storage | ring SPSC; M7 head `accepted_frames`, M4 tail `released_frames`; stop fixe le head final |
| Looper preroll/live map | M7/AUDIO puis M4/Storage | M7/AUDIO | preroll borne puis carte physique append-only; lecture limitee au tail SD committed |

Les tokens/generations conserves appartiennent aux loads SD, registrations de
buffers et sessions Recorder. Ils rejettent une completion I/O obsolete; ils
ne valident jamais PROGRAM/PARAM/NOTE et ne reconstruisent aucun etat musical.

## Compatibilite

H743 utilise exactement les memes contrats via les adaptateurs locaux. H747
place M4 et M7 de part et d'autre des memes structures pointer-free. Aucun
handshake, peripheral ou service H747-only n'est requis au nominal.

## Empreinte et validation

Par rapport a la fin PASS A, RAM D3 passe de 16 480 a 16 448 octets (-32) sur
Low-Cost et Premium; RAM D1 passe de 483 872 a 483 936 octets (+64). DTCM,
ITCM, D2, SDRAM et la zone Recorder sont inchanges. Les builds H743 Release
Low-Cost, Premium et Test passent. La FIFO M4 vers M7, son ABI, STREAM, les
pointeurs/data planes, le cache, la latence live et le DMA ne sont pas modifies.
