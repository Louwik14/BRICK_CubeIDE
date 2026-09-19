# Z4 - Sequence, clock, Note FX et evenements live

## Owner et cadence

`seq_engine` est l'unique owner musical SEQ. Son coeur est CPU-agnostique et
travaille exclusivement en samples absolus avec :

```c
seq_service(now_sample, publish_until_sample);
seq_next_deadline();
```

Le port H743 utilise TIM4, priorite 2, avec une periode configurable
`SEQ_ENGINE_H743_PERIOD_SAMPLES`, actuellement 64 samples a 48 kHz. AUDIO reste
priorite 1. La periode du port n'entre ni dans les identites, ni dans les
decisions musicales du coeur.

CONTROL prepare un `seq_pattern_t` immutable dans le slot non publie. Le commit
transfere le slot a SEQ par generation; CONTROL ne modifie jamais le slot publie.
Le modele compact conserve 16 lanes, 64 steps, 512 noeuds p-lock par lane,
8 PLAY par top-level et 1 PLAY par enfant GROUP. Chaque slot arme dispose de
512 locks independants par lane dans les SRAM internes; le chemin chaud
conserve uniquement les listes actives bornees a 32.

Le Pattern publie aussi la projection executable de chaque lane: role
topologique, capabilities, destination, mute/timing et capacite logique. Cette
capacite reprend la polyphonie produit configuree lorsqu'elle existe, bornee a
8 pour une lane principale; elle vaut 1 pour un enfant GROUP et 0 pour le GROUP
master. Le master ne publie aucun walker MIDI FX.

Les quatre slots MIDI FX sont normalises par CONTROL. Le plan de base compact
et, pour chaque step, les quatre opcodes effectifs, le premier slot actif, le
masque actif et le masque de p-lock sont publies dans le Pattern. Les p-locks
MIDI FX d'un meme slot sont compactes en un seul mot de plan effectif. SEQ ne
refait donc ni mapping PARAM, ni validation de modele/plage/famille, ni
normalisation lors de la configuration du runtime courant. Une famille MIDI FX
ne peut apparaitre qu'une fois dans une chaine de quatre slots.

## Runtime borne

Le runtime fixe contient un ledger de 64 notes logiques et un scheduler commun
de 512 tickets de 24 octets. Ses types couvrent NOTE_OFF, Echo, source/ROLL,
ARP, Euclid et les resumes Groove. Les etats musicaux restent dans des pools
externes fixes; un ticket ne transporte qu'une echeance et une reference
compacte. Il n'y a ni allocation dynamique, ni heap de deadlines, ni attente
de la superloop. Les 4 slots MIDI FX partagent un seul contexte possede par SEQ.

Les p-locks d'un step sont tries pendant la preparation CONTROL. A la boundary,
l'ancienne liste active et la nouvelle sont fusionnees lineairement. Une cle
commune produit directement `old -> new`; seules les cles absentes restaurent
la base. Le stockage canonique autorise 32 locks par step et 512 noeuds par
lane; la borne globale d'une boundary reste 511 locks actifs et 991 transitions
ordinaires.

Les lanes top-level admettent au plus 8 notes logiques et les enfants GROUP au
plus une. Le master GROUP reste non emetteur. En cas de saturation, les notes
generees les plus anciennes cedent avant les originales les plus anciennes.
La saturation exceptionnelle du scheduler incremente un compteur diagnostic
et refuse deterministement la nouvelle obligation.

## Ingress et terminal AUDIO

Hall, clavier et MIDI ne prennent aucune decision musicale terminale. Ils
capturent/correlent l'identite physique puis soumettent un
`seq_ingress_event_t` canonique a `seq_ingress_submit`; l'inbox conserve le
sample de capture et demande un reveil
urgent. SEQ applique Note FX, admission et datation, puis clampe une date deja
engagee sur le premier bloc AUDIO encore publiable.

La frontiere accepte au maximum 64 evenements bruts, toutes sources confondues,
par fenetre fixe de 1024 samples, avec au maximum 64 evenements simultanement en
attente. Elle preserve donc un burst complet de l'inbox et borne le debit
soutenu a 3000 evenements/s a 48 kHz. Un evenement invalide,
hors ordre temporel, au-dela de cette borne ou arrivant lorsque l'inbox est
pleine est refuse sans mutation. Cette limite borne le debit brut; elle ne
remplace pas la capacite logique musicale de la lane.
STOP/PANIC invalide l'inbox, le scheduler, les curseurs source et le ledger au
point de service suivant.

SEQ publie un seul bloc terminal date. AUDIO ne connait ni step, ni ROLL, ni
ARP, ni Euclid : il applique PARAM/NOTE dans l'ordre `(sample, OFF, PARAM, ON)`
et conserve uniquement l'allocation physique des voix. Les anciennes voies
cooperative, shadow/compare et publication legacy/RT ne sont pas compilees.
Live Rec est alimente apres cette admission terminale; un candidat refuse n'est
donc jamais enregistre. Les PLAY issus de Live Rec portent le marqueur
`TERMINAL` et leur relecture contourne le walker MIDI FX sans creer de second
pipeline.

## Borne worst-case de publication

Les 64 curseurs source sont parcourus avec une borne fixe. Le quota reste 8
pour une top-level et 1 pour un enfant; lorsqu'il impose un remplacement, la
source la plus ancienne est choisie deterministement.

Les NOTE produites, les NOTE_OFF, l'ingress et les PARAM sont accumules sans
tri intermediaire. Une unique mise en ordre stable en place, bornee par
`SEQ_ENGINE_EVENT_CAPACITY`, publie sans allocation. La borne PASS 2 est 2880
entrees: 1280 NOTE_ON (fanout direct, Echo du et reprises Groove), au plus
1344 NOTE_OFF remplacements inclus, puis 256 parameter-locks. La capacite
retenue est 3072 (192 entrees, soit 6,7 % de marge). Echo possede 256 identites stables
`[track, lane, branche HARM]`; Groove conserve 128 lots de quatre branches
sans reduire le fanout. Le
departage conserve est `(sample, NOTE_OFF, PARAM, NOTE_ON, PANIC, ordre
d'ajout)`. Les PARAM rejoignent donc le flux avant cette unique mise en ordre.

CONTROL decode aussi le mapping statique `(param NoteFX -> slot,parametre)`
dans le Pattern. A la boundary, SEQ conserve les decisions musicales et la
configuration du runtime courant; aucun catalog lookup, mapping d'identifiant,
controle de famille ou normalisation dependante du modele n'y subsiste.

Les constantes produit figees sont 4 slots MIDI FX, une plage de tempo
40..300 BPM, 8 notes logiques par lane principale, un enfant GROUP mono et un
master a zero note. Echo conserve au plus un etat due-only par lane logique et
branche HARM (256 globaux), adresse directement dans les 64 lanes produit.
Groove conserve au plus deux resumes par lane (128 globaux); une
troisieme occurrence remplace d'abord un resume genere, sinon le plus ancien.
Une date Groove negative issue du clavier ou du MIDI est clampee au sample de
capture et aucun tweak ne reecrit une occurrence deja decidee.

## Persistence et gros changements

Storage et CONTROL chargent, valident et preparent hors IRQ. Pattern/Project
Load n'effectuent aucune copie massive dans SEQ. Le swap musical se fait par
generation au service SEQ; FatFs, codec, snapshots AUDIO et resolution d'assets
restent hors du chemin chaud.
