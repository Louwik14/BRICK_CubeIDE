# NoteFX : capacites et continuations temporelles

> Audit historique du runtime a slots. Le contrat courant et son autorite sont
> decrits dans `z4_seq_clock_scheduler.md`: chaine fixe
> `GENERATOR -> VOICER -> SCALER -> TRIG`, sans ordre ni type de slot.

Le runtime musical appartient exclusivement a SEQ. Les trois slots logiques
S1 a S3, executes selon l'une des six permutations ORDER, partagent le meme
walker et le meme ledger logique du sequenceur. AUDIO ne
recoit que des NOTE terminales sample-datees et conserve l'allocation, le
reuse, le release, le stealing et le DSP physiques.

## Contrat d'ownership terminal

Une occurrence traverse trois domaines distincts. La source STEP, KEY ou MIDI
cree son identite; les MIDI FX conservent cette cause ou creent une identite du
namespace FX pour une branche derivee. SEQ est l'autorite sur la vie musicale:
il choisit `(track, logical_slot)`, reserve le ledger, date ON/OFF et decide le
stealing logique. La publication terminale est une demande de transition, pas
une preuve que la voix physique a deja change. AUDIO est l'unique autorite sur
la possession physique du slot et de la voix moteur.

Pour chaque `(track, logical_slot)`, AUDIO applique les transitions suivantes:

| Transition | Regle AUDIO |
|---|---|
| `ON(identity)` sur slot vide | installe l'owner puis ouvre la voix |
| `ON(new)` sur slot occupe | ferme l'owner courant puis installe `new`, dans la meme acquisition AUDIO |
| `OFF(identity)` owner courant | ferme la voix puis vide le slot |
| `OFF(identity)` deja supplantee | no-op idempotent; ne ferme jamais le nouvel owner |

Ainsi SEQ peut oublier une reservation apres avoir publie son OFF, sans declarer
pour autant une ressource physique libre. Une reutilisation ulterieure est
toujours un `ON(new)` atomiquement interprete par l'autorite AUDIO comme un
handoff. Aucun ACK AUDIO->SEQ n'est necessaire: SEQ ne reutilise pas une voix
physique, il demande le nouvel owner logique; AUDIO ferme l'ancien owner avant
d'ouvrir le nouveau. Le token d'occurrence qualifie les OFF et interdit qu'une
release retardee ferme un owner plus recent.

Ce contrat est commun a STEP, live KEY/MIDI et toutes les sorties FX. Il couvre
donc ARP/Euclid (generateurs held), SCALER/VOICER
(fanout), Gate (duree derivee) et Probability (filtrage). Le Groove est un
finalizer de piste apres le dernier slot execute, pas un owner de slot MIDI FX; aucun de ces
traitements ne possede un mecanisme de release AUDIO particulier.

Le modele Groove n'appartient plus au catalogue MIDI FX. Il ne compile aucun
opcode et ne possede aucun etat runtime. Le seul Groove executable est le
finalizer track-level.

## Capacites figees

| Ressource | Borne |
|---|---:|
| curseurs source/ROLL | 768, douze generations par lane produit |
| calendrier final | 512 occurrences, buckets de 64 samples |
| ledger logique | 64 notes |
| polyphonie top-level | 8 maximum |
| polyphonie enfant GROUP | 1 |
| polyphonie master GROUP | 0 |

La capacite logique des sources Note FX reste celle des PLAY du modele. La
polyphonie physique de la piste ne participe ni a la capture held, ni au rang
ARP, ni au fanout Euclid; sa limite est appliquee seulement a la projection
AUDIO terminale.
| held ARP/Euclid | 256 identites lane/branche par famille, 512 globaux |
| ARP | 1 owner logique par piste, 256 identites lane/branche |
| Euclid | 1 owner logique par piste, 256 identites lane/branche |

Les held ARP/Euclid sont indexes directement par famille, lane produit et
branche causale; le slot logique owner est conserve par piste. Un retrigger
remplace l'identite existante; il ne consomme pas une entree par occurrence
ROLL. `temporal_index` conserve les huit lanes et `destination_id` conserve ses
huit bits.

Le calendrier ne recoit qu'un evenement complet apres le finalizer. Les
curseurs ARP/Euclid restent dans leurs pools fixes.
Il n'y a ni allocation dynamique ni heap. Un overflow incremente le diagnostic
et refuse deterministement la nouvelle obligation; il ne constitue pas une
politique musicale normale.

Le plan NoteFX compile vaut 16 octets par piste: trois configurations de cinq
octets et ORDER. Le plan timing vaut 48 octets et ne contient aucune table de
suffixe par slot, Groove etant toujours le finalizer global. Avec les deux
snapshots de seize pistes, ces deux contractions economisent 1 408 octets; les
watermarks temporels `16 x 3` economisent 128 octets supplementaires. Le bloc
de configuration canonique et la projection runtime restent volontairement a
16 octets par piste: `3 x (4 parametres + MODEL) + ORDER` remplace exactement
`4 x (3 parametres + MODEL)`.

## Admission et ordre

Une note est reservee dans le ledger avant sa publication terminale. La lane
logique est le couple `(track, logical_slot)`. Lorsque la capacite est atteinte,
la note generee la plus ancienne cede d'abord, puis l'originale la plus
ancienne. Aucun candidat refuse n'est publie a AUDIO.

A sample identique, l'ordre terminal stable est NOTE_OFF, PARAM, NOTE_ON
originale, puis NOTE_ON generee. Les NOTE_OFF sont uniques et leur date est
figee lors du NOTE_ON par `on + max(duration, 1)`.

## Finalizer Groove

Le finalizer Groove recoit les sorties du dernier slot execute. Son avance maximale,
son retard maximal et la borne Random sont compiles hors IRQ. Une occurrence
`SCHEDULED` est materialisee avant sa date terminale minimale puis inseree dans
le calendrier final; une occurrence live `LIVE_IMMEDIATE` ne recoit que le
traitement de velocite. Aucun clamp ne masque un horizon insuffisant et un
changement live ne reecrit jamais une occurrence deja finalisee.

Les generateurs temporels sont avances separement par piste avec la borne du
seul suffixe final de cette piste. Leur watermark ne couvre jamais une zone
sautee faute de source held; une source qui arrive ensuite peut donc produire
la premiere occurrence encore decidable sans rewind general. Random et
Probability se fondent sur l'identite musicale stable, jamais sur le numero
sequentiel attribue par l'ordre de precalcul.
Pour Probability, `LOT=GROUP` conserve la cle logique `group_id`. Les LOT
temporels utilisent `track + slot + floor(position transport Q16 / division)`;
les divisions binaires et ternaires sont calculees avec leur rapport rationnel
exact, sans duree en samples tronquee ni cache mutable.

## Contrats produit MIDI FX

Le catalogue courant est `ARP`, `EUCLID`, `PROBABILITY`, `GATE`, `VOICER` et
`SCALER`, en plus de `OFF`. Leurs quatre parametres sont respectivement:

- `ARP`: `TYPE | DIV | RANGE | HOLD`. La phase est synchronisee au pattern.
  HOLD OFF suit uniquement les notes tenues; HOLD ON memorise chaque lane
  apres relachement. Le premier NOTE ON recu sans aucune note encore
  physiquement tenue ouvre une nouvelle saisie et remplace en bloc le groupe
  latche precedent; les NOTE ON suivants, tant qu'au moins une note physique
  reste tenue, completent ce nouveau groupe. Le passage de HOLD a OFF retire
  uniquement les notes deja relachees; STOP/PANIC et le changement de
  proprietaire ARP nettoient tout le groupe.
- `EUCLID`: `LENGTH | PULSES | DIV | ROTATE`. ROTATE effectue une rotation
  circulaire du masque construit par le moteur Euclid, sans changer LENGTH ni
  PULSES.
- `PROBABILITY`: `CHANCE | CONDITION | LOT | KEEP`. LOT fixe l'identite de la
  decision CHANCE partagee par fenetre musicale. Une occurrence sur la grille
  rationnelle KEEP force ensuite provisoirement PLAY, puis CONDITION est
  evaluee en dernier et reste autoritaire. Le test KEEP porte sur le timestamp
  musical effectif recu par ce slot.
- `GATE`: `LENGTH | VARIATION | MODE | SEED`. La duree est un pourcentage de
  la duree du step, pas de la duree source. VARIATION est une excursion
  symetrique maximale autour de LENGTH. SEED selectionne une fonction pure de
  la position relative au pattern, de la piste, du slot, de la lane et de la
  branche: un reloop reproduit donc le meme motif. MODE `CLIP` borne le resultat
  a un step; MODE `EXTEND` autorise une articulation jusqu'a 200 % du step.
- `VOICER`: `TYPE | SPREAD | INVERT | VOICES`. VOICES conserve les N premieres
  voix de la recette, racine comprise. INVERT monte d'une octave les premieres
  voix de la recette; SPREAD ajoute ensuite `12 * SPREAD * rang_de_voix`
  demi-tons, ce qui garantit un voicing monotoniquement plus ouvert.
- `SCALER`: `SCALE | KEY | STICK | TRSP`. TRSP est applique avant le mapping.
  Le catalogue de gammes est celui du clavier. Une note hors gamme est mappee
  sur la note valide inferieure (`DOWN`), superieure (`UP`) ou supprimee
  (`DROP`).

## Placement memoire

Le ledger et les curseurs sont en SRAM interne. Le linker garde
l'etat NoteFX principal en SRAM2. Le calendrier final et ses buckets, structures
bornees sans pointeurs externes ni allocation, vivent dans la zone SDRAM SEQ;
l'admission et le ledger chauds restent internes.

## Borne apres retrait du delay MIDI

La chaine constructible la plus couteuse est `VOICER -> EUCLID -> GATE`
(a permutation ORDER equivalente): VOICER porte le fanout direct maximal
de 4, EUCLID conserve jusqu'a 256 identites held et GATE produit une obligation
OFF par ON admis. ROLL reste en amont du walker et impose deux points de source
adjacents par horizon de 64 samples au tempo maximal.

La borne de publication passe de 1280 a 1024 NOTE_ON par horizon, de 2624 a
2112 evenements NOTE et de 3648 a 3136 evenements terminaux avec les 1024
transitions PARAM. Le gain demonstrable est donc 256 ON et 256 OFF, soit 512
evenements terminaux (14,0 %) par horizon. Les 256 reprises futures et leur
passage dans le suffixe du walker disparaissent aussi; le gain IRQ reel est au
moins proportionnel au travail terminal retire et peut etre superieur lorsque
ces reprises traversaient plusieurs slots. ARP/Euclid, le fanout VOICER, le tri
terminal et les p-locks restent les couts dominants; aucun pourcentage en cycles
n'est revendique sans mesure DWT.

Le retrait libere exactement 12 288 octets D1 pour les 256 etats de 48 octets
et 32 octets internes pour leurs quatre masques actifs. La reduction des deux
blocs terminaux de 3648 a 3136 entrees libere 6 144 octets de SDRAM par bloc,
soit 12 288 octets de SDRAM. Les capacites calendar 512, held 512, scratch 32,
ledger 64, sources 768, fanout 4 et profondeur source 12 restent imposees par
les generateurs restants, ROLL, la polyphonie ou le finalizer.
