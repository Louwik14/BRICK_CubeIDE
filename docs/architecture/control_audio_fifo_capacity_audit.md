# Projection AUDIO atomique et capacite FIFO

## Frontiere

CONTROL reste l'unique writer fonctionnel et AUDIO l'unique reader. Les
mutations ordinaires publient leurs deltas `PROGRAM`, `PARAM`, `NOTE`, transport
ou record dans la FIFO. Un remplacement Pattern ou Project ouvre en revanche
une transaction de projection: les `PROGRAM` et `PARAM` produits pendant
l'installation CONTROL alimentent la projection canonique sans entrer dans la
FIFO, puis une seule commande `AUDIO_STATE_COMMIT(generation)` est publiee.

La projection n'est pas une autorite produit. C'est la forme AUDIO, pointer-free,
des derniers `PROGRAM` et `PARAM` canoniques acceptes. Elle est dedupliquee par
cle `(opcode, entity, id, scope)`, filtree contre la structure CONTROL finale et
ordonnee `PROGRAM` puis `PARAM`. NOTE, automation runtime-temp, panic, record et
commandes de liberation de ressource restent transitoires et n'y sont pas
persistes.

## Snapshot partage

Le format commun contient un header de 32 octets (generation, count, checksum,
valid magic) et au plus 4618 commandes finales de 16 octets. Le snapshot unique
mesure 73920 octets dans `.sdram_audio_state_snapshot`, partagee, cacheable et
alignee 32 octets. Le producteur publie le contenu immutable avant le
`valid magic`, nettoie le cache si les coeurs sont separes, puis la FIFO
transporte uniquement la generation. Le consommateur invalide si necessaire,
verifie generation, borne, magic et checksum, panique les sorties detenues,
puis applique la projection complete avant de reprendre le rendu. Aucun ACK
n'existe.

Apres l'enqueue du commit, CONTROL attend la progression du `tail` FIFO au-dela
du `head` capture. AUDIO n'avance ce `tail` qu'apres le retour du handler de
commit: la traversee de cette frontiere prouve donc que l'application est finie
et rend le snapshot reutilisable. Sur H743 l'IRQ SAI preempte cette attente; sur
H747 le reader M7 progresse en parallele. Project et Pattern restent serialises,
et la fonction de restore ne retourne pas avant cette frontiere.

## Project et Pattern

Avant cette correction, un Pattern poussait individuellement les restores
Program/Tone ou FM/Filter/VCA/Mixer/FX/polyphonie/ENV/LFO/matrice/mute/routing/
globals, jusqu'a 4554 commandes; Project ajoutait les macros, jusqu'a 4618.

Maintenant Pattern installe le meme etat CONTROL sous transaction puis publie un
commit. Project ouvre la transaction externe, appelle Pattern en transaction
imbriquee, applique ses macros, puis publie lui aussi un seul commit externe.
Les assets sont resolus avant ce point et leurs selections finales font partie
de la projection. Les setters locaux et Patch restent inchanges pour les petites
mutations.

## Recalcul FIFO

AUDIO draine a chaque demi-buffer de 64 frames a 48 kHz, soit 1,333 ms. Une
commande future en tete reste au plus un horizon de 64 frames. H743 interdit une
publication CONTROL concurrente pendant l'IRQ AUDIO prioritaire; le SPSC reste
identique sur H747.

| Source simultanement en vol | Commandes |
|---|---:|
| Horizon PARAM | 1024 |
| Horizon NOTE, `2 * (256 + 128)` | 768 |
| Horizon general | 35 |
| **Horizon maximal** | **1827** |
| Commit restore serialise | 1 |
| Patch deja queue | 348 |
| Encodeurs: `(32 + 7 * 4) * mute-groupe 9` | 540 |
| Services/assignments/transitoires du tour | 64 |
| **Hors horizon maximal** | **953** |
| Marge explicite | 512 |

```text
FIFO_REQUIRED = 1827 + 953 + 512 = 3292 commandes
FIFO_CAPACITY = puissance de deux suivante = 4096 commandes
```

La FIFO occupe 65536 octets, contre 262144 dans le dimensionnement compensant
les anciens restores. Le snapshot unique ajoute 73920 octets: l'ensemble vaut
139456 octets, soit 122688 octets (119,8 KiB) de moins que cette ancienne FIFO
seule. Les deux slots supprimes economisent 147840 octets. Apres link, la region
non-cacheable `.sdram_recorder` utilise 251648 octets sur 256 KiB; sa fenetre
linker/MPU est donc reduite de 512 a 256 KiB. `head` et `tail` restent monotones
`uint32_t`, le free-count `uint16_t`, et le masque du ring reste valide.
