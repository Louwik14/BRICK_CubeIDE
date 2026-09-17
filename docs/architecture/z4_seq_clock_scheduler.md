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

## Runtime borne

Le runtime fixe contient 64 lifetimes, 256 futures, 32 locks actifs par lane,
deux scratch MIDI FX de 32 candidats et une inbox live de 64 entrees. Il n'y a
ni allocation dynamique, ni heap de deadlines, ni attente de la superloop.
Les 4 slots MIDI FX partagent un seul contexte possede par SEQ.

Les p-locks d'un step sont tries pendant la preparation CONTROL. A la boundary,
l'ancienne liste active et la nouvelle sont fusionnees lineairement. Une cle
commune produit directement `old -> new`; seules les cles absentes restaurent
la base. Le stockage canonique autorise 32 locks par step et 512 noeuds par
lane; la borne globale d'une boundary reste 511 locks actifs et 991 transitions
ordinaires.

Les lanes top-level admettent au plus 8 lifetimes persistantes et les enfants
GROUP au plus une. Les obligations datees utilisent `Future[256]`. Chaque slot
possede un ticket 32 bits incremente au reuse (zero est saute au wrap); toute
resolution verifie le couple index/ticket et ignore une reference perimee.

## Ingress et terminal AUDIO

Hall, clavier et MIDI ne prennent aucune decision musicale terminale. Ils
capturent/correlent l'identite physique puis appellent l'inbox bornee
`seq_ingress_note`; l'inbox conserve le sample de capture et demande un reveil
urgent. SEQ applique Note FX, admission et datation, puis clampe une date deja
engagee sur le premier bloc AUDIO encore publiable.
STOP/PANIC
invalide l'inbox, les futures et les lifetimes au point de service suivant.

SEQ publie un seul bloc terminal date. AUDIO ne connait ni step, ni ROLL, ni
ARP, ni Euclid : il applique PARAM/NOTE dans l'ordre `(sample, OFF, PARAM, ON)`
et conserve uniquement l'allocation physique des voix. Les anciennes voies
cooperative, shadow/compare et publication legacy/RT ne sont pas compilees.

## Borne worst-case de publication

Les 64 Lifetime sont gerees par une free-list globale et des listes actives par
track. Une allocation sans stealing est O(1). Le quota existant reste 8 pour
une top-level et 1 pour un enfant; lorsqu'il impose un stealing, la recherche
de l'entree la plus ancienne est bornee au seul quota local (8 maximum), avec
le meme departage par plus petit index physique. La liberation met a jour la
liste de track et la free-list dans la meme operation SEQ.

Les NOTE produites, les NOTE_OFF Future, l'ingress et les PARAM sont accumules
sans tri intermediaire. Une unique passe finale de merge-sort stable, bornee
par `SEQ_ENGINE_EVENT_CAPACITY`, publie en O(E log E), sans allocation. Le
departage conserve est `(sample, NOTE_OFF, PARAM, NOTE_ON, PANIC, ordre
d'ajout)`. Les PARAM rejoignent donc le flux avant cette unique mise en ordre.

CONTROL decode aussi le mapping statique `(param NoteFX -> slot,parametre)`
dans le Pattern. A la boundary, SEQ conserve les decisions musicales et la
normalisation dependante du modele, puis configure les quatre slots; aucun
catalog lookup ni mapping d'identifiant n'y subsiste.

## Persistence et gros changements

Storage et CONTROL chargent, valident et preparent hors IRQ. Pattern/Project
Load n'effectuent aucune copie massive dans SEQ. Le swap musical se fait par
generation au service SEQ; FatFs, codec, snapshots AUDIO et resolution d'assets
restent hors du chemin chaud.
