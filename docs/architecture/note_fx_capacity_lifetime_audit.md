# NoteFX : capacites et continuations temporelles

Le runtime musical appartient exclusivement a SEQ. Les quatre slots S1 a S4
partagent le meme walker, le meme ledger logique et le scheduler statique du
sequenceur. AUDIO ne recoit que des NOTE terminales sample-datees et conserve
l'allocation, le reuse, le release, le stealing et le DSP physiques.

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
| Groove | 2 resumes par lane, 128 globaux |
| ARP | 1 etat actif par slot et lane, 16 slots |
| Euclid | 1 etat actif par slot et lane, 16 slots |

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
