# NoteFX : capacites et continuations temporelles

Le runtime musical appartient exclusivement a SEQ. Les quatre slots S1 a S4
partagent le meme walker et le meme ledger logique du sequenceur. AUDIO ne
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
donc ARP/Euclid (generateurs held), Echo (continuations), Chord/Harmonizer
(fanout), Groove (resume temporel), Gate (duree derivee) et Probability
(filtrage): leurs differences s'arretent avant le terminal et aucun ne possede
un mecanisme de release AUDIO particulier.

## Capacites figees

| Ressource | Borne |
|---|---:|
| scheduler commun | 512 tickets |
| taille d'un ticket | 24 octets |
| ledger logique | 64 notes |
| polyphonie top-level | 8 maximum |
| polyphonie enfant GROUP | 1 |
| polyphonie master GROUP | 0 |
| Echo | 1 etat par lane et branche HARM, 256 globaux |
| held ARP/Euclid | 256 identites lane/branche par slot, 1024 globaux |
| Groove | 2 resumes par lane, 128 globaux |
| ARP | 1 etat actif par slot et lane, 16 slots |
| Euclid | 1 etat actif par slot et lane, 16 slots |

Les held ARP/Euclid sont indexes par slot, lane produit et branche causale. Un
retrigger remplace l'identite existante; il ne consomme pas une entree par
occurrence ROLL. `temporal_index` conserve les huit lanes et `destination_id`
conserve ses huit bits.

Le scheduler ne contient aucun evenement musical developpe. Chaque ticket
porte une echeance, un type et une reference vers un etat externe fixe. Il n'y
a ni allocation dynamique ni heap. Un overflow incremente le diagnostic et
refuse deterministement la nouvelle obligation; il ne constitue pas une
politique musicale normale.

## Admission et ordre

Une note est reservee dans le ledger avant sa publication terminale. La lane
logique est le couple `(track, logical_slot)`. Lorsque la capacite est atteinte,
la note generee la plus ancienne cede d'abord, puis l'originale la plus
ancienne. Aucun candidat refuse n'est publie a AUDIO.

A sample identique, l'ordre terminal stable est NOTE_OFF, PARAM, NOTE_ON
originale, puis NOTE_ON generee. Les NOTE_OFF sont uniques et leur date est
figee lors du NOTE_ON par `on + max(duration, 1)`.

## Echo et Groove

Echo ne developpe plus son train a l'ingress. Le NOTE_ON arme ou retrigger un
etat borne et le runtime ne materialise que le repeat du. Un retrigger
reechantillonne TIME, REPEATS et DECAY et conserve l'echeance deja promise par
`min(old_next_due, new_anchor + new_delay)`. Chaque repeat consomme son index,
meme si l'admission terminale le refuse. L'etat Echo survit au NOTE_OFF sans
 occuper le ledger. L'adresse d'etat est directe: les pistes principales
 utilisent `(track * 8 + lane)`; les huit enfants GROUP reutilisent les huit
 indices du master non emetteur. La branche HARM selectionne ensuite l'un des
 quatre etats de la lane. Il n'existe donc ni recherche lineaire, ni refus
 d'allocation pour une identite produit legale.

Groove suspend le walker lorsqu'il deplace une occurrence. Deux resumes au plus
sont conserves par lane. Une troisieme occurrence remplace d'abord un resume
genere, sinon le plus ancien. Une avance demandee pour une entree clavier ou
MIDI est clampee au sample de capture. Un changement live ne reecrit jamais une
echeance deja decidee.

## Placement memoire

Les tickets, le ledger, les curseurs et les etats Echo sont en SRAM D2 interne.
Le linker repartit les zones SEQ entre SRAM1 et SRAM3 et garde l'etat NoteFX
principal en SRAM2. Aucun etat temps reel critique n'est place en SDRAM.
