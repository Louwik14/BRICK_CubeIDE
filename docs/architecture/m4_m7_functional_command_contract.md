# Contrat fonctionnel M4 vers M7

## Autorite et transport

M4 est l'unique autorite fonctionnelle. `control_audio_publication` est le seul
point final de publication et le seul appelant du writer de
`control_audio_fifo`. La FIFO est SPSC: M4 possede `head`, M7 possede `tail`.
M7 n'avance `tail` qu'apres l'application synchrone complete de la commande.
Le retour de `tail` signifie exclusivement que la case FIFO et les ressources
protegees par ce fence physique sont reutilisables; il ne porte aucun resultat
musical, statut ou ACK de commande.
Les indices sont monotones sur 32 bits; un lot devient visible apres copie
complete par une unique publication de `head` precedee de `DMB`. Il n'existe ni
retry tardif, ni fallback, ni perte silencieuse: un manque de place echoue la
publication et incremente `overflow_count`.

La FIFO finale contient 2048 commandes, soit 32 KiB. Le plafond contractuel
PASS 4 est de 1781 commandes: 1024 PARAM, au plus deux commandes pour chacune
des 233 actions NOTE internes et 128 actions externes admises, puis 35 places
pour les PROGRAM/TRANSPORT/RECORD/PANIC generaux du meme horizon. La marge est
donc de 267 commandes (4272 octets). 1536 est insuffisant; 2048 conserve un
indexage par masque et le meme contrat sur H743 et H747.

La capacite, sa propriete puissance de deux et sa couverture du burst sont
figees par assertions de compilation. Overflow, opcode/entite invalides et
regression de timestamp alimentent des compteurs diagnostiques; ils ne creent
ni retry, ni fallback, ni recovery musical.

## ABI partagee

Chaque commande vaut exactement 16 octets et ne contient aucun pointeur:

```text
u64 effective_sample_time
u32 value
u16 id
u8  entity
u8  opcode_kind       // opcode bits 0..2, sous-type bits 3..7
```

Les seuls opcodes sont, dans l'ordre ABI:

```text
0 PROGRAM
1 PARAM
2 NOTE
3 TRANSPORT
4 RECORD
5 PANIC
```

- `PROGRAM`: `entity`, `value=program_id`. Le descripteur prepare immutable est
  hors FIFO dans `control_audio_program` et ne contient que `family`, `type` et
  le role topologique structurel GROUP; aucun parametre produit, binding, etat
  BOUND, generation musicale ou ACK n'est public.
- `PARAM`: `entity/target`, `id=param_id`, `value=valeur finale`. Le sous-type
  ne decrit que la portee d'adressage; aucune provenance UI, p-lock,
  automation ou encodeur ne traverse.
- `NOTE ON`: `entity`, `value=output_id`, `id[7:0]=note`,
  `id[15:8]=velocity`. `NOTE OFF`: `value=output_id`. Un retrigger CONTROL est
  publie comme OFF puis ON au meme sample.
- `TRANSPORT`: START, STOP, CONTINUE ou LOCATE; `value` porte la position si
  necessaire. Tempo et cadence AUDIO sont des PARAM globaux.
- `RECORD`: START ou STOP; `value=session_id`, `id=config_id`, `entity=client`.
- `PANIC`: global ou entity. Il ne transporte aucune generation.

`effective_sample_time` reutilise exclusivement la timeline sample absolue,
la conversion TIM5 vers sample et la segmentation AUDIO existantes. Le writer
rabaisse une date anterieure au plancher deja publie afin de maintenir une
timeline monotone. A date egale, l'ordre d'intention M4 est conserve; les
fenetres NOTE fusionnent les sorties internes puis externes dans chaque bucket
`sample/kind`.

Les samples, wavetables, Multi, pages stream, PCM et gros descripteurs restent
hors FIFO. Seuls `program_id` et les resource IDs portes par PARAM traversent.
Pattern et Project restent CONTROL. Un Patch ne produit PROGRAM que si sa
famille/type de moteur differe; toutes ses proprietes restent des PARAM.

## Consumer AUDIO final

`audio_command_executor` est l'unique consumer fonctionnel M7. Pour chaque
demi-buffer, AUDIO lit la prochaine date FIFO, rend jusqu'a cette date, applique
toutes les commandes dues dans leur ordre physique, puis reprend le rendu. Il
n'existe plus de merge AUDIO, de phases `general/STOP/PARAM/START`, ni de seconde
chronologie musicale.

PROGRAM separe desormais vie musicale et renderer. Entre Prism, Stack, Wave,
FM, Sampler RAM, Stream et Multi, CONTROL conserve son ledger et AUDIO conserve
le mapping d'execution `{output_id, note, velocity, gate}`. Au sample commande,
l'ancien renderer est detruit, le nouveau contexte est installe puis initialise
localement depuis ce mapping, sans commande NOTE ni mutation du ledger. Un
passage vers Drum, External/MIDI, Looper, GROUP ou OFF ferme explicitement les
outputs avant PROGRAM. Une ressource Sampler legale est preparee par
construction; un echec AUDIO reste une rupture d'invariant sans retry.
Les changements de polyphonie/spread d'un moteur installe traversent PARAM et
ne republient plus PROGRAM. MIDI config traverse egalement PARAM. Tone, MODEL,
Mix, filtre, VCA, ENV et FX ne font jamais partie du descripteur PROGRAM.

PARAM est applique directement au backend AUDIO final. NOTE ne porte que
`entity/output_id/note/velocity`; OFF est identifie exclusivement par
`output_id`. TRANSPORT, RECORD et PANIC ne transportent que leurs effets AUDIO.

Pour FM, un setter PARAM se limite au clamp, a l'ecriture de la cible et a un
masque dirty par operateur. Les conversions coarse/fine, `powf`, `log10f`,
`log2f`, `exp`/`log`, les mises a jour d'enveloppe et la projection sur voix
actives sont absentes du setter. Le consumer finalise les dirty FM une fois a
la fin de la serie de PARAM due et avant la commande non-PARAM suivante; NOTE
ON voit donc toutes les valeurs PARAM qui la precedent dans la FIFO. Le rendu
possede le meme garde prive pour les changements issus de la modulation.
Chaque finalisation parcourt au plus 16 instances et 6 operateurs par instance,
et ne recalcule que les familles derivees marquees.
Ainsi une serie de N PARAM de patch FM ne provoque plus N fois 6 projections:
le setter effectue zero appel libm et la finalisation en effectue au plus une
serie de 6 par track dirty au sample (96 operateurs si les 16 tracks FM sont
simultanement dirty). Le NOTE ON ne repete plus le refresh patch complet apres
avoir calcule ses frequences.

L'audit des autres destinations p-lockables n'a trouve ni allocation, attente
ni I/O dans le chemin PARAM AUDIO. Les conversions de pitch Wave et Sampler
ont ete remplacees par une approximation `2^x` a reduction d'octave, bornee et
sans libm (erreur maximale inferieure a 0,01 % sur la fraction d'octave).
Les tables exponentielles filtre/mix sont
initialisees au boot; les setters de rendu utilisent ensuite des recherches de
taille fixe. Les changements de modele FX et le routing structurel ne font pas
partie des slots p-lockables. Le changement de sample Sampler reste une
projection bornee sur ressource deja preparee, sans lecture SD dans le setter.

Mesures Release PASS 4 (octets):

| cible | DTCM | ITCM | D1 | D2 total | D3 | SDRAM | FIFO |
|---|---:|---:|---:|---:|---:|---:|---:|
| H743 Low-Cost | 121024 | 57288 | 478752 | 256896 | 41344 | 33053856 | 32768 |
| H743 Premium | 121024 | 57288 | 478784 | 257248 | 41344 | 33053856 | 32768 |

La FIFO reste a 32768 octets. Les sept masques dirty ajoutent 8 octets alignes
par voix, soit 128 octets: `g_fm_voice` passe de 17600 a 17728 octets et DTCM
de 120896 a 121024 octets; D1/D2/D3/SDRAM sont inchanges. Le `.bss` ELF
generique mesure 251688 octets apres la passe (hors sections NOBITS dediees).
Restent a mesurer ulterieurement avec DWT: la finalisation composee des 16 tracks FM
(algorithme/ratio + six frequences + enveloppes au meme sample), un NOTE ON FM
avec projection polyphonique, et le changement de ressource Sampler preparee.

## Audit PASS 2

Producteurs converges vers le point final unique:

- notes sequenceur, live, MIDI et sortie terminale Note FX via
  `control_music_output`;
- p-locks et parametres live via `live_parameter_audio_publication`;
- moteur/patch via l'intention runtime convertie en PROGRAM prepare;
- routing/mix/FX via PARAM, registre parametres et publication routing;
- transport et cadence AUDIO;
- Looper AUDIO, resources Multi/RAM/wavetable via PARAM;
- Recorder START/STOP via RECORD;
- panic via PANIC.

Producteurs restant sur un ancien transport: aucun. Les anciennes queues AUDIO
notes, parametres et commandes generales, leurs mailboxes fonctionnelles et
leurs consumers ont ete supprimes. Les buffers internes/externes du scheduler
restent du staging local CONTROL; ils sont fusionnes avant une publication
atomique dans la FIFO unique et ne traversent pas la frontiere M4 vers M7.

Le runtime CONTROL est reconstruit synchroniquement au point de mutation. UI,
keyboard, scheduler, restore et Note FX ne lisent aucun etat AUDIO et ne font ni
polling, ni refresh consumer-edge, ni retry/republication. Le restore est valide
localement par CONTROL puis republie en PROGRAM/PARAM/TRANSPORT; il n'utilise
plus de plan partage, doorbell, attente ou resultat M7.

Compatibilite H747: ABI a largeurs fixes, sans pointeur; stockage des commandes
dans la fenetre SDRAM partagee non cacheable, metadonnees SPSC dans SRAM4
partagee, descripteurs prepares immutables hors FIFO. Aucune dependance
fonctionnelle M7 vers M4 n'est ajoutee.

## Audit PASS 5

PASS 5B est COMPLETE. `audio_transition_snapshot` et le mailbox fonctionnel de
configuration LFO ont disparu: les transitions et les quatre proprietes de
chaque LFO sont des PARAM executes sur l'etat M7 local. Aucune mutation
fonctionnelle de `mod_lfo_v1` ne traverse une frontiere parallele.

Chaque horizon sequenceur ouvre maintenant un builder CONTROL borne. P-locks,
PARAM generaux, metronome/boundary et sortie terminale Note FX y sont d'abord
copies sans rendre le head visible. Le commit ordonne par sample en conservant
l'ordre d'intention a sample egal, copie le lot complet dans la FIFO puis publie
le head une seule fois. Ainsi PARAM puis NOTE d'un meme horizon ne peuvent plus
etre separes par une avancee du plancher; OFF puis ON reste explicite dans le
lot. Le clamp nominal reste reserve aux commandes live effectivement tardives.

Le ledger NOTE CONTROL suit la meme transaction: une fenetre travaille sur une
copie temporaire (`alive`, age, stealing et retrigger), puis le ledger canonique
et ses observateurs de mort ne sont appliques qu'apres publication FIFO du lot
complet. Un abort, y compris apres preparation NOTE mais avant le commit FIFO,
jette cette copie sans mutation canonique. PANIC suit le meme ordre: preparation
et publication, commit FIFO, puis purge deterministe des ledgers CONTROL et
NoteFx. Une PANIC non publiee ne purge rien; aucun ACK AUDIO n'est requis.

Le plafond reste `1024 + 2*(233+128) + 35 = 1781`; la FIFO de 2048 conserve une
marge de 267 commandes. Les cas cibles p-locks/rolls, PROGRAM-PARAM-NOTE,
retrigger, quota MIDI externe, PANIC dense et FM worst-case sont couverts par le
meme ordre et la meme admission bornee. Aucun binding, generation musicale,
READY AUDIO, ACK fonctionnel, retry/fallback ou snapshot fonctionnel LFO n'est
reintroduit.

Empreinte PASS 2 Premium: D3 41 760 octets, soit 320 octets de moins que le
premier build PASS 2 avec snapshot public; RAM D1 478 784 octets. La FIFO
partagee 32 KiB et le registre PROGRAM restent en SDRAM. Les data planes Stream,
RAM, Multi, wavetable, Preview et Recorder PCM sont inchanges.

## Audit PASS A

PASS A est COMPLETE. LFO/ENV3 et chaque champ Matrix traversent PARAM. M7
conserve l'etat canonique Matrix et derive localement plans et endpoints. Routing
Looper, ownership des entrees, bus Recorder, selection Wavetable et lifecycle
Preview rejoignent PARAM/RECORD. Les mailboxes fonctionnelles correspondantes
ont disparu. Project Load attend uniquement la consommation d'une PANIC
terminale fencee; `audio_safe_seq` n'existe plus.

Restent hors FIFO uniquement les rings PCM Preview/Recorder, le transport
physique Recorder, les tables/mipmaps Wavetable et les descripteurs immutables
selectionnes par la FIFO.

Mesures Release PASS A (octets):

| cible | DTCM | ITCM | D1 | D2 DMA NC | D2 cache | SRAM2 | SRAM3 | D3 | SDRAM | Recorder |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| H743 Low-Cost | 120032 | 56640 | 483872 | 5568 | 115040 | 124288 | 17216 | 16480 | 32857504 | 253376 |
| H743 Premium | 120032 | 56640 | 483872 | 5952 | 115008 | 124288 | 17216 | 16480 | 32857504 | 253376 |

## Audit PASS 6

PASS 6 est COMPLETE. Le chemin fonctionnel reste strictement
`M4 -> control_audio_publication -> FIFO SPSC -> audio_command_executor ->
backend`; les data planes restent limites a leurs payloads, publication et
credits physiques.

Le cleanup a supprime le module relais `mixer_routing_publication`, 18 API
publiques ou no-op sans appelant, les anciens objets d'operation Matrix des
PARAM dates et le shadow AUDIO `base_override` Matrix devenu sans writer. Le
workspace Restore M4-only et sa validation par phases ont ensuite ete
supprimes: la validation porte directement sur l'etat final. La selection
Wavetable physique ne s'appelle plus binding. PROGRAM credits, staging borne
PARAM/NOTE, output_id, fenetre STREAM, cache H747 et lifecycles PCM n'ont pas
ete simplifies car ils portent encore ownership, ordre ou faits physiques.

Mesures de code de cette passe: 617 -> 615 fichiers C/C++/headers et
162015 -> 161804 lignes (-211). Une globale AUDIO de 1024 octets, deux types
legacy et 18 API publiques ont disparu. La FIFO reste 2048 x 16 = 32768
octets; son burst 1781 et ses six opcodes sont inchanges.

Mesures linker H743 avant -> apres PASS 6 (octets):

| cible | DTCM | ITCM | D1 | D2 total | D3 | SDRAM | Recorder NC |
|---|---:|---:|---:|---:|---:|---:|---:|
| Low-Cost | 120032 -> 120032 | 56640 -> 56640 | 483936 -> 482912 | 268736 -> 268736 | 14272 -> 14272 | 32886208 -> 32886208 | 224672 -> 224672 |
| Premium | 120032 -> 120032 | 56640 -> 56640 | 483936 -> 482912 | 269088 -> 269088 | 14272 -> 14272 | 32886208 -> 32886208 | 224672 -> 224672 |
| Test | 118240 -> 118240 | 54296 -> 54296 | 484960 -> 483936 | 268736 -> 268736 | 14272 -> 14272 | 32886208 -> 32886208 | 224672 -> 224672 |

Builds Release H743 Low-Cost, Premium et Test valides. Aucun changement de
politique STREAM, cadence, DMA live, hard-RT, H747, opcode, capacite ou burst.

## Correctifs post-audit final

Les lots Matrix canoniques utilisent la FIFO fonctionnelle existante. Le kind
PARAM encode directement le slot; aucune table d'IDs, aucun snapshot Matrix et
aucun descripteur H747 ne sont publies. Le writer reserve jusqu'a 1024 PARAM,
remplit les cases encore invisibles puis publie le head une seule fois.

Un passage AUDIO capture une seule limite de head au debut du demi-buffer et ne
consomme jamais au-dela. Les commandes publiees concurremment restent dans la
FIFO pour le demi-buffer suivant. Les changements Transport lies et le STOP
Looper immediat sont publies par lots atomiques; les shadows CONTROL avancent
uniquement apres succes du lot. Capacite, ABI, six opcodes et ordre FIFO restent
inchanges.
