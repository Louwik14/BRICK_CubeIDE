# Projection AUDIO atomique et capacite FIFO

## Frontiere

CONTROL reste l'unique writer fonctionnel et AUDIO l'unique reader. Les
mutations ordinaires publient leurs deltas `PROGRAM`, `PARAM`, `NOTE`, transport
ou record dans la FIFO. Un remplacement Pattern ou Project ouvre en revanche
une transaction de projection: les `PROGRAM` et `PARAM` produits pendant
l'installation CONTROL alimentent la projection canonique sans entrer dans la
FIFO, puis une seule commande `AUDIO_STATE_COMMIT(generation)` est publiee.

La projection n'est pas une autorite produit. Chaque transaction repart d'un
conteneur vide, puis les `PROGRAM` sont reprojetes depuis `track_runtime` et les
owners restaurent leurs `PARAM` canoniques. La projection pointer-free est
dedupliquee par cle `(opcode, entity, id, kind)` et filtree contre la structure
CONTROL finale. Le kind PARAM wire exprime directement `BASE`, `TEMP` ou
`CLEAR_TEMP`, independamment de son timestamp. Les commandes `TEMP` et
`CLEAR_TEMP`, NOTE, automation, macros temporaires, panic, record et commandes
de liberation de ressource restent transitoires et ne sont jamais conservees.

Un PARAM mono est construit directement depuis la cible preparee CONTROL puis
publie. Les vraies transactions assemblent un tableau des memes commandes wire
finales avant publication atomique; elles ne possedent pas de builder distinct.

Juste avant publication, `control_rt` applique un contrat IPC mecanique: opcode
et kind representables, ID wire connu, namespace `entity` correspondant et bits
reserves des payloads packes. Ce controle ne consulte ni piste courante, ni
moteur, ni applicabilite PARAM, ni budget de polyphonie. Le writer FIFO ne
connait plus ces namespaces: il ne gere que capacite, ordre temporel, copie,
barriere et publication atomique du head.

Le descriptor PROGRAM est construit une seule fois par `track_runtime` depuis
l'etat CONTROL final. Son assertion partagee est pure (bornes du catalogue et
combinaison de bits structurellement representable). AUDIO ne recalcule plus
l'engine attendu, les flags de capacite, le role GROUP ou la polyphonie produit;
il conserve allocation de slots, renderer, ressources, quota Looper, teardown,
held notes et rebind. Une impossibilite physique reste fatale.

Pour PARAM, CONTROL publie la valeur canonique. AUDIO verifie une fois le
contrat de valeur a l'entree du runtime PARAM, puis resout la cible physique.
Les helpers de dispatch ne repetent plus la validation range/type; les gardes
de presence backend, instance, slot, generation et mapping restent en place.

## Snapshot partage

Le format commun contient un header de 32 octets (generation, count, checksum,
valid magic) et au plus 4618 commandes finales de 16 octets. Le snapshot unique
mesure 73920 octets dans `.sdram_audio_state_snapshot`, partagee, cacheable et
alignee 32 octets. Le producteur publie le contenu immutable avant le
`valid magic`, nettoie le cache si les coeurs sont separes, puis la FIFO
transporte uniquement la generation. Le consommateur invalide si necessaire,
verifie generation, borne, magic et checksum, puis applique une transition
structurelle en trois temps: liberation de tous les PROGRAM modifies,
installation de tous les PROGRAM finaux, PARAM et rebind unique des outputs
conserves. Aucun ACK n'existe.

Apres l'enqueue du commit, CONTROL attend la progression du `tail` FIFO au-dela
du `head` capture. AUDIO n'avance ce `tail` qu'apres le retour du handler de
commit: la traversee de cette frontiere prouve donc que l'application est finie
et rend le snapshot reutilisable. Sur H743 l'IRQ SAI preempte cette attente; sur
H747 le reader M7 progresse en parallele. Project et Pattern restent serialises,
et la fonction de restore ne retourne pas avant cette frontiere.

## Project et Pattern

Avant cette correction, un Pattern poussait individuellement les restores
Program/Tone ou FM/Filter/VCA/Mixer/FX/polyphonie/ENV/LFO/matrice/mute/routing/
globals, jusqu'a 4554 commandes; Project pouvait ajouter un bulk de 64 macros,
jusqu'a 4618. Le rebuild frais ajoute au plus les 16 `PROGRAM` et les 16
`MIDI_CONFIG` absents du flux d'installation. Les macros portent maintenant
leur vrai scope runtime-temp et sont exclues: le worst-case canonique vaut donc
`4554 + 16 + 16 = 4586`, soit 32 entrees de marge sans redimensionnement.

Maintenant Pattern installe le meme etat CONTROL sous transaction puis publie un
commit. Project ouvre la transaction externe, appelle Pattern en transaction
imbriquee, applique ses macros, puis publie lui aussi un seul commit externe.
Les assets sont resolus avant ce point et leurs selections finales font partie
de la projection. Le commit Pattern preserve le playback et ne fait aucun PANIC
global; il laisse les PROGRAM identiques en place et rebind les outputs vivants
des seuls PROGRAM/resources modifies. Le commit Project, execute transport
arrete, panique les sorties puis teardown toutes les installations avant le
rebuild. Les releases precedent ainsi toute acquisition incompatible, y compris
les deplacements Looper et les swaps Looper/Synth. Les setters locaux et Patch
restent inchanges pour les petites mutations.

Le rebuild porte un `PROGRAM` pour chacun des 16 slots, y compris `OFF`, car le
consumer valide une image structurelle complete. `MIDI_CONFIG` est en revanche
projete uniquement pour les entites actives: les enfants GROUP 8..15 inactifs
ont volontairement un contexte CONTROL zero, donc aucun canal MIDI final a
installer. Lorsqu'un GROUP les active, leurs configurations deviennent valides
et sont projetees comme celles des entites 0..7.

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

Les producteurs hors horizon ont ete groupes selon le seul intervalle pertinent,
entre deux drains AUDIO: un commit snapshot serialise (1), une transaction Patch
deja en file (348), le backlog encodeur maximal plus les sept remplissages TIM7
(540), et un tour de services/assignments borne par le bulk commun (64). Ils
peuvent tous se cumuler, d'ou 953, sans compter deux fois Pattern/Project qui ne
publient plus leur projection dans la FIFO. Avec l'horizon maximal et la marge,
`3292 < 4096`, soit 804 entrees encore libres. FIFO full au nominal est donc une
rupture d'invariant. Le test de capacite demeure obligatoire contre la corruption;
les chemins produit ne l'utilisent pas comme backpressure ou retry fonctionnel.

La FIFO occupe 65536 octets, contre 262144 dans le dimensionnement compensant
les anciens restores. Le snapshot unique ajoute 73920 octets: l'ensemble vaut
139456 octets, soit 122688 octets (119,8 KiB) de moins que cette ancienne FIFO
seule. Les deux slots supprimes economisent 147840 octets. Apres link, la region
non-cacheable `.sdram_recorder` utilise 251648 octets sur 256 KiB; sa fenetre
linker/MPU est donc reduite de 512 a 256 KiB. `head` et `tail` restent monotones
`uint32_t`, le free-count `uint16_t`, et le masque du ring reste valide.
