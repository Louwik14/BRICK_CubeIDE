# Contrat fonctionnel CONTROL vers AUDIO

## Autorite et transport

CONTROL est l'unique autorite fonctionnelle. `control_rt_publication` est le
seul point final de publication et le seul appelant du writer de
`control_audio_fifo`. La FIFO est SPSC: CONTROL possede `head`, AUDIO possede
`tail`. AUDIO n'avance `tail` qu'apres l'application synchrone complete de la
commande. Ce retour signifie uniquement que la case FIFO et les ressources
protegees par une fence sont reutilisables; il ne porte aucun resultat musical
ni ACK fonctionnel.

Les indices sont monotones sur 32 bits. Un lot devient visible apres sa copie
complete, par une publication unique de `head` precedee de `DMB`. Il n'existe ni
retry tardif, ni fallback, ni perte silencieuse: un manque de place refuse la
publication et incremente le diagnostic d'overflow.

La FIFO contient 2048 commandes de 16 octets. Son burst maximal est de 1781:
1024 PARAM, au plus deux commandes pour chacune des 233 actions NOTE internes et
128 actions externes, puis 35 PROGRAM/TRANSPORT/RECORD/PANIC. Les assertions de
compilation figent la capacite, sa propriete puissance de deux et cette marge.

## ABI partagee

Chaque commande vaut exactement 16 octets et ne contient aucun pointeur:

```text
u64 effective_sample_time
u32 value
u16 id
u8  entity
u8  opcode_kind       // opcode bits 0..2, sous-type bits 3..7
```

Les seuls opcodes sont:

```text
PROGRAM PARAM NOTE TRANSPORT RECORD PANIC
```

- `PROGRAM` porte directement `{family,type,topology_flags,reserved}` dans
  `value`. Il n'existe aucun registre, ID, slot, lookup, release ou credit
  PROGRAM.
- `PARAM` porte une propriete canonique finale. Son sous-type precise seulement
  la portee d'adressage; aucune provenance UI, p-lock ou encodeur ne traverse.
- `NOTE ON` porte `entity`, `output_id`, note et velocite. `NOTE OFF` est
  identifie par `output_id`. Un retrigger est un OFF puis un ON au meme sample.
- `TRANSPORT` porte START, STOP, CONTINUE ou LOCATE.
- `RECORD` porte START ou STOP, un `session_id`, une configuration et un client.
- `PANIC` est global ou limite a une entite.

`effective_sample_time` utilise la timeline sample absolue. A date egale,
l'ordre d'intention CONTROL est conserve. Les samples, wavetables, instruments
Multi, pages Stream et rings PCM restent des data planes separes; seuls leurs
identifiants traversent les commandes fonctionnelles.

## Consumer AUDIO

`audio_command_executor` est l'unique consumer fonctionnel. Pour chaque
demi-buffer, AUDIO rend jusqu'a la prochaine date FIFO, applique toutes les
commandes dues dans leur ordre physique, puis reprend le rendu. Il n'existe ni
merge AUDIO ni seconde chronologie musicale.

PROGRAM separe la vie musicale du renderer. CONTROL conserve son ledger et
AUDIO conserve le mapping `{output_id,note,velocity,gate}` pendant un changement
de moteur. A la date PROGRAM, AUDIO detruit l'ancien renderer, installe le
nouveau et reprojette localement les notes compatibles sans fabriquer NOTE
OFF/ON. Un renderer incompatible reste silencieux; un retour compatible rend de
nouveau les notes encore vivantes. Toute NOTE fermee pendant cette phase reste
morte au retour d'un renderer compatible. PROGRAM ne reset ni NOTE ni TONE.

PARAM est applique directement au backend final. Les setters FM se limitent au
clamp, a l'ecriture de cible et aux masques dirty; les conversions et projections
sur voix actives sont finalisees une fois par serie de PARAM due. Les autres
setters AUDIO restent bornes, sans allocation, attente ni I/O.

## Structure, PROGRAM et mute

L'autorite structurelle est l'etat CONTROL. Une mutation valide l'etat final,
compare les revisions, reconstruit les seuls runtimes modifies et publie les
PROGRAM necessaires. Le cache de destinations de modulation est invalide
localement; AUDIO derive son propre plan depuis PROGRAM/PARAM.

Le mute persiste par entite comme valeur locale. Pour un enfant GROUP, le mute
effectif vaut `mute_local_child || mute_local_parent`; seul ce derive est projete
vers le scheduler et AUDIO. Un changement de topologie recalcule la projection
sans reecrire le mute personnel des enfants.

## Autorite terminale NOTE

`control_music_output` est l'unique autorite CONTROL des sorties musicales. Son
ledger porte l'identite finale, la cause source, la generation, l'age de
stealing et les destinations MIDI admises. Le scheduler ne possede que ses
sources et echeances; Note FX ne possede que ses sources et sorties derivees.
Une mort terminale notifie ces proprietaires afin qu'aucun derive ne puisse
maintenir ou ressusciter la sortie.

Le remplacement d'une source ferme les sorties ayant le meme identifiant
causal, sans fermer les notes live independantes. Un changement de moteur ne
reset ni scheduler ni Note FX. Les NOTE OFF datees Euclid, generations et tokens
causaux restent des faits musicaux.

## Publication atomique et metronome

Chaque horizon sequenceur prepare PARAM, NOTE, metronome et sorties Note FX sans
rendre le head visible. Le commit ordonne les commandes par sample, conserve
l'ordre d'intention a sample egal et publie le lot complet. Le ledger NOTE est
mis a jour seulement apres succes; un abort jette sa copie de travail.

Le click metronome est l'unique one-shot transporte par NOTE sans appartenir au
ledger musical. Son `output_id` utilise le prefixe reserve
`CONTROL_AUDIO_NOTE_METRONOME_PREFIX`; `audio_command_executor` le consomme avant
le dispatch vers les moteurs NOTE. Il ne cree donc ni gate musical, ni voix, ni
etat dans l'autorite NOTE generale.

Les data planes physiques, generations de ressource, pins, refcounts, fences,
pages Stream, snapshots Undo/Clipboard et tails Recorder restent independants
de ce contrat fonctionnel.
