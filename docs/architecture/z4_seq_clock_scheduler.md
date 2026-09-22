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

Les trois slots MIDI FX et leur permutation ORDER sont normalises par CONTROL.
Le plan de base publie exactement trois mots de cinq octets et l'octet ORDER,
sans masque ni cache de premier slot. Les p-locks MIDI FX d'un meme slot sont
compactes en une entree de 48 bits; ORDER utilise au plus une entree
supplementaire. SEQ ne
refait donc ni mapping PARAM, ni validation de modele/plage/famille, ni
normalisation lors de la configuration du runtime courant. Une famille MIDI FX
ne peut apparaitre qu'une fois dans une chaine de trois slots. La permutation
change uniquement l'ordre d'execution: l'identite et l'etat restent attaches
aux slots logiques S1, S2 et S3. ORDER est conserve une seule fois dans le
coeur SEQ; le runtime NoteFX n'en garde aucune copie mutable.

## Runtime borne

Le runtime fixe contient un ledger de 64 notes logiques, 768 curseurs
source/ROLL et un calendrier final de 512 occurrences. Le calendrier est une
roue de 4 096 buckets de 64 samples, chainee par indices dans un pool fixe:
insertion O(1), aucune allocation, aucun heap generaliste. Il ne contient que
des occurrences ayant deja traverse Quantize, les trois MIDI FX et le
finalizer Groove. ARP et Euclid conservent leurs etats held dans leurs
pools fixes et exposent leur prochain horizon au planificateur; il n'existe pas
de second calendrier source.

CONTROL compile hors IRQ, pour chaque piste, les bornes en Q16 musical et en
samples: avance source composee `microtiming -> Quantize`, avance/retard du
finalizer. Cette derniere est un scalaire unique puisque Groove est toujours
apres toute la chaine: aucune table par slot ou point de reprise n'est publiee.
Le source cursor materialise une occurrence a son `decision_due`, et
non a son timestamp audible. Les decisions eligibles sont toutefois appliquees
aux etats temporels dans l'ordre de leur timestamp nominal: un generateur est
avance jusqu'a cette frontiere avant que le nouvel etat held ne la remplace.
Les occurrences finalisees sont immuables.
La borne Quantize est derivee sur tout le domaine Q16 par
`floor((BaseQ16 - 1) / 2)`, puis ponderee par le dosage avec le meme arrondi que
le hot path. Elle ne depend donc ni d'une periode en nombre de steps, ni d'un
echantillonnage de phases; les Base triplet tronquees sont couvertes.

ARP et Euclid avancent piste par piste avec la seule avance du finalizer
de cette piste. Le watermark d'un generateur n'avance que sur une region
effectivement parcourue avec une source held connue; une region sans source
reste ouverte. L'arrivee live declenche immediatement ce traitement borne pour
sa piste, sans faire progresser les autres generateurs. Les grilles ARP/Euclid
sont evaluees en position musicale Q16 absolue puis converties en samples pour
chaque pulse; elles n'accumulent jamais une periode entiere tronquee. Le mode
SYNC recoit aussi la longueur de pattern et remet son ordinal a zero exactement
a la frontiere de loop, y compris quand le lookahead traverse cette frontiere.
Quantize ne sert donc jamais d'horloge aux generateurs: son deplacement des
sources held peut masquer un raccord, mais ne modifie pas leur grille nominale.
La reference transport/pattern du finalizer et des generateurs est capturee a
la frontiere gauche du bloc avant le scan des boundaries futures. Une boundary
situee plus loin dans le meme bloc ne peut donc pas avancer retrospectivement
la phase utilisee pour ce bloc. Les avances Random modifient uniquement
`decision_due` et l'horizon garanti, jamais cette origine temporelle.

Les decisions Probability et Gate aleatoire conservent leur cle musicale
propre. Random Groove derive chaque valeur de noeud du seed Pattern persistant,
du noeud BASE absolu dans le Pattern, de la piste, de la lane temporelle, de la
branche et de la note. L'index de geometrie reste modulo la periode du template,
mais l'identite Random ne l'est pas: un template court ne force donc pas la
repetition du meme bruit a chaque periode interne. La coordonnee est repliee
sur la longueur du Pattern et le noeud droit est replie au raccord, ce qui rend
la realisation stable et periodique entre ses loops.

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
urgent. SEQ applique Note FX, finalization et admission sur le premier bloc
AUDIO encore publiable. Une note live directe est marquee
`LIVE_IMMEDIATE`: aucun Quantize, Timing ou Random temporel ne lui est applique;
la Velocity Groove reste autorisee. Une occurrence future produite par
ARP/Euclid est marquee `SCHEDULED` et traverse le finalizer complet. Une
borne de lookahead insuffisante est un fault/drop explicite, jamais un clamp
temporel silencieux.

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
et conserve uniquement l'allocation physique des voix. Un OFF est qualifie par
l'identite d'occurrence et devient idempotent si cette occurrence a deja ete
supplantee. Un ON installe l'owner demande dans le slot logique; si un ancien
owner physique y subsiste, AUDIO le ferme et effectue le handoff avant d'ouvrir
le nouveau. Le ledger SEQ est donc une reservation musicale, jamais une preuve
de liberation physique, et AUDIO reste l'autorite unique du slot moteur. Les anciennes voies
cooperative, shadow/compare et publication legacy/RT ne sont pas compilees.
Live Rec est soumis seulement apres l'admission terminale; un candidat refuse
n'est donc jamais enregistre. Le calendrier conserve cependant le timestamp et
la velocite pre-finalizer: l'edition compacte enregistre cette version et ne
grave pas le Groove dans le step. Les PLAY issus de Live Rec portent le marqueur
`TERMINAL`: leur relecture contourne le walker MIDI FX, traverse Quantize source
puis le finalizer Groove, sans creer de second pipeline.

## Borne worst-case de publication

Les 768 curseurs source sont parcourus avec une borne fixe: douze generations
par voix produit. Cette profondeur couvre l'enveloppe composee maximale des
Base supportees sans confondre notes actives et occurrences successives dans
la fenetre de lookahead. Une treizieme generation encore occupee est refusee
sans remplacer une source active.

Les NOTE produites, les NOTE_OFF, l'ingress et les PARAM sont accumules sans
tri intermediaire. Une unique mise en ordre stable en place, bornee par
`SEQ_ENGINE_EVENT_CAPACITY`, publie sans allocation. La borne PASS 2 est 3136
entrees: 1024 NOTE_ON (fanout direct et occurrences finalisees), au plus
1088 NOTE_OFF remplacements inclus, puis 1024 transitions parameter-lock. La
capacite terminale est exactement 3136. Le
departage conserve est `(sample, NOTE_OFF, PARAM, NOTE_ON, PANIC, ordre
d'ajout)`. Les PARAM rejoignent donc le flux avant cette unique mise en ordre.
Les ingress clavier/MIDI captures dans le meme tick grossier sont etales sur
des samples consecutifs selon leur `ingress_serial`. Cet ordre physique doit
etre conserve avant le departage terminal: un cycle `ON/OFF/ON` ne doit jamais
devenir `OFF/ON/ON`, ce qui dissocierait l'etat des touches et les occurrences.

CONTROL decode aussi le mapping statique `(param NoteFX -> slot,parametre)`
dans le Pattern. A la boundary, SEQ conserve les decisions musicales et la
configuration du runtime courant; aucun catalog lookup, mapping d'identifiant,
controle de famille ou normalisation dependante du modele n'y subsiste.

Les constantes produit figees sont 3 slots MIDI FX, 4 parametres musicaux par
slot et 6 permutations ORDER, une plage de tempo
40..300 BPM, 8 notes logiques par lane principale, un enfant GROUP mono et un
master a zero note.
Le Groove historique des slots MIDI FX est retire du catalogue. Le finalizer
track-level est l'unique etape post-FX. Aucun tweak ne reecrit une occurrence
deja decidee.

## Controle track-level du timing

La configuration canonique d'une piste est le bloc unique
`seq_track_timing_config_t`: `Base`, `Quantize`, identite de Groove, `Global`,
`Timing`, `Random` et `Velocity`. Il n'existe plus de phase Swing dans l'etat transport,
ni de copie Quant/Swing dans le Pattern executable; seul le plan timing compile
est publie vers SEQ. Le format compact des PLAY et leur microtiming restent
inchanges.

La page SEQ expose trois sous-pages sans dupliquer l'etat:

- `COMMON`: `LENGTH`, `DIV`, `DIR`, `ROTATE`;
- `GROOVE 1`: `TEMPLATE`, `GLOBAL`, `QUANT`, `BASE`;
- `GROOVE 2`: `RANDOM`, `VELOCITY`, `TIMING`, emplacement reserve.

`DIV` reste la vitesse de lecture du pattern et ne change jamais la grille
`BASE`. Le clipboard de piste capture/restaure `DIR`, `ROTATE` et le bloc timing
canonique en une operation, de sorte qu'aucune combinaison intermediaire ne
peut etre publiee.

Le transport conserve une phase physique par piste. Le resolver O(1) applique
`FWD`, `REV`, `PINGPONG` ou `RANDOM`, puis `ROTATE`, et produit l'unique index
de step stocke lu a la boundary. Trig, PLAY, microtiming, roll, p-locks et
configuration Note FX de ce step utilisent donc tous le meme index; aucune
donnee de pattern n'est copiee ni reecrite. `PINGPONG` ne duplique pas ses
extremites. `RANDOM` est une fonction deterministe du seed Pattern, de la piste
et de la phase, et reste identique a chaque boucle.

## Projection Groove preparee

CONTROL projette le template sur la grille `BASE` lors d'un changement de
template ou de Base. La periode Q32 du record est l'autorite de wrap; la
signature n'intervient pas. Le ratio periode/Base est arrondi dans ce domaine,
puis les positions de noeuds sont derivees de `period / node_count`, ce qui
conserve les grilles ternaires sans accumulation de la troncature Q16. La borne
est 512 noeuds pour une periode maximale de 64 noires en Base 1/32.

Chaque cellule existe, meme vide. Un trou porte `offset=0` et `velocity=1` mais
participe a `dL`, `dR`, `mu`, `dmin`, `alpha` et `H`. Plusieurs points dans une
cellule sont departages par leur distance au noeud; l'ordre stable BGRB gagne
un tie exact. SEQ ne scanne ensuite aucun point: il calcule deux indices, lit
`offset`, `H` et `velocity`, puis interpole.

Le pool permanent contient seize blocs actifs et un scratch, soit 87 720
octets SDRAM (`17 x 5 160`). Un changement unique compile dans le scratch et
echange les owners sans copie. Une publication multi-piste utilise temporairement
le membre Groove du workspace Persistence deja reserve; apres le swap atomique,
les geometries sont rapatriees dans les seize blocs devenus recyclables et les
pointeurs equivalents sont rediriges avant liberation du workspace. Aucun bloc
actif n'est mute avant publication et aucun cache n'est partage entre pistes.

Le finalizer applique `Timing x Global`, echantillonne ensuite le relief
Velocity et applique `Velocity x Global`. Random interpole les valeurs de bruit
des deux noeuds et utilise `Random x Global^2`; ce scaling est isole du calcul
geometrique. Les extrema reels prepares alimentent directement les bornes
d'avance et de retard du scheduler.

Pour une NOTE_ON programmee, Quantize reste exclusivement une transformation
du start source. Apres les MIDI FX, le finalizer evalue en revanche le champ
`Timing + Random` deux fois: au start post-FX et a son end post-FX. La duree
publiee vaut `max(1 sample, end_final - start_final)`. Une NOTE_OFF explicite
programmee traverse le meme champ a sa propre position. Le ledger impose ensuite
`off >= on + 1 sample`; les notes held gardent leur end explicite et PANIC reste
hors de cette transformation. Aucun MIDI FX ne connait les maths Groove.

La borne produit du deplacement final est derivee de la Base maximale 1/4,
du Global maximal 130 %, de la demi-cellule Timing et de la largeur Random
`16B/21`. Une periode Q32 non entiere en BASE peut porter la cellule uniforme
preparee jusqu'a la limite `3B/2`; la borne conservative vaut donc
`2717/1400` noire, soit 139 732 samples a 48 kHz / 40 BPM. Le calendrier final
couvre 4 096 blocs de 64 samples (262 144 samples), au-dessus de cette borne
sans remplacer les extrema reels des plans de piste par un forfait.

La portee temporelle de la roue est ainsi prouvee, mais sa capacite de 512
occurrences ne couvre pas encore le backlog Groove maximal. Avec 64 lanes,
ROLL 1/5 de step et le retard limite `2717/1400` noire, jusqu'a 39 occurrences
par lane, soit 2 496 occurrences, peuvent attendre simultanement. Porter ce pool
sans gonfler en meme temps la borne terminale demande de dissocier capacite de
retention et fanout exigible dans un bloc; cette correction reste un chantier
scheduler distinct. Un refus actuel du pool est un drop borne, jamais une fuite
ou une croissance memoire.

| Pool | Borne legale | Capacite | Marge | Budget statique |
|---|---:|---:|---:|---:|
| sources | 768 | 768 | 0 | 30 720 octets |
| held ARP/Euclid | 512 total | 512 | 0 | 12 288 octets |
| calendrier final | 512 occurrences | 512 | 0 | 28 672 octets + 16 384 octets de buckets |
| ledger | 64 | 64 | 0 | 1 536 octets |
| scratch NoteFX | 32 | 32 | 0 | 5 376 octets, quatre buffers et references source |
| terminal | 3 136 | 3 136 | 0 | 77 376 octets, deux blocs |
| p-lock | 1 024 (991 utile documente) | 1 024 | 33 sur 991 | 24 624 octets, trois blocs |

Ces pools sont tous statiques et n'utilisent pas le heap. Le held est separe
par famille ARP/Euclid; l'owner reste un slot logique et deux generateurs de
meme famille ne peuvent donc pas se voler une identite canonique.
Les 768 curseurs source forment un pool SDRAM cacheable contigu reference par
le coeur SEQ; les masques actifs, le ledger, les compteurs et les phases restent
dans le coeur D2. Les scans consultent d'abord les masques et ne lisent que les
curseurs actifs; cette scission absorbe la profondeur de lookahead sans charger
les SRAM internes avec les generations inactives.

La convergence a trois slots retire 44 octets de plan compile par piste
(`timing 88 -> 48`, `NoteFX 20 -> 16`), soit 1 408 octets sur les deux snapshots
de seize pistes et 128 octets de watermarks runtime (`16 x 4 -> 16 x 3`). Le
contexte NoteFX reste a 392 octets: les 16 octets retires des etats de slot
(`16 x 4 x 4 -> 16 x 3 x 5`) portent maintenant les 16 longueurs de pattern
requises par les generateurs; aucune seconde copie mutable d'ORDER n'y subsiste.
Le DTO persistant retire 8 octets par entite, soit 128 octets par image Pattern;
les trois images statiques effectivement reservees (boot et deux dans le plus
grand membre du workspace) ajoutent 384 octets. La contraction statique totale
attribuable au contrat trois slots est donc de 1 920 octets.
L'evenement transporte directement la branche VOICER; l'ancien bitmap de
dependance de slots disparait. Les capacites
scratch 32, fanout VOICER 4, held 512, calendrier 512 et sources 768 restent
identiques: elles sont imposees par la polyphonie, le fanout ou l'horizon
temporel. La borne terminale diminue de 3 648 a 3 136 entrees.

## Persistence et gros changements

Storage et CONTROL chargent, valident et preparent hors IRQ. Pattern/Project
Load n'effectuent aucune copie massive dans SEQ. Le swap musical se fait par
generation au service SEQ; FatFs, codec, snapshots AUDIO et resolution d'assets
restent hors du chemin chaud.
