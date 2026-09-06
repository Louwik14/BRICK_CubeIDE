# Z6 - Persistance Pattern, Patch et Project

## Modele et format

Pattern, Project et Patch utilisent exclusivement `persistent_control_model` et le codec explicite `B6CP` version 5. Les DTO ne sont ni des snapshots runtime ni une ABI disque; chaque champ est encode explicitement. Header, kind, sections, longueurs et CRC sont stricts. Aucune ancienne version ni dump de structure n'est lu.

Les cles persistantes de famille, type, parametre, MIDI, clock, entree External, Note FX, modulation et asset sont explicites et independantes des ordinaux C. Les sources External utilisent uniquement `PERSIST_INPUT_LINE` et `PERSIST_INPUT_USB`; `MIC` est reserve a Recorder / Audio REC. Les FLOAT32 conservent leurs bits. Les indices runtime, contextes AUDIO installes, pointeurs, caches, voix, phases, playheads et UI sont exclus.

Pour une track External, le TONE `TRIG` est persiste comme parametre canonique : `OFF` laisse l'audio en THRU et `ON` le gate par les notes.

Pattern contient les seize identites. La configuration des children inactifs est conservee, mais pas leurs parametres, assets, routes, modulation, Note FX ou sequence dynamique. En GROUP, le master possede MOD et Audio FX; les children ont leur lane a un PLAY et leurs niveaux A/B.

Patch contient une entite et ses parametres logiques, zero a deux references
d'assets typees et, pour FM, le DTO de l'owner. Project contient metadata,
Pattern de travail, manifeste d'assets, macros/scenes et jusqu'a 256 records
Pattern diffuses progressivement.
Patch ne transporte ni source External ni ownership d'entree; Pattern et
Project restaurent `LINE` / `USB` par le commit canonique d'ownership.

## Codec et application

Le decode commence par les controles de format, bornes et CRC, puis construit un
candidat borne dans l'espace inactif. Les capacites topologiques sont derivees
par `persistent_entity_topology`; la validation metier des owners reste dans la
phase d'installation. Les providers/consumers Project reutilisent un workspace
borne sans allocation dynamique.

`persistent_pattern_control` et `persistent_patch_control` sont les facades CONTROL. `pattern_control_bank`, `patch_product` et `project_product` sont les facades produit. Une reference asset persistante est `{kind, canonical_path}`; elle est canonicalisee une fois a l'entree de l'owner, puis le codec la valide et l'encode sans transformation. `project_control` ne resout le slot AUDIO qu'apres chargement, au moment de la publication fonctionnelle. Sample et tables Wave ne possedent plus de stable key Param. L'owner FM unique est encode champ par champ, sans packs flottants ni copie operateur secondaire. Les tables et mipmaps restent des data planes immutables hors FIFO.

## Transactions

Pour Project Load, P1 decode et valide un candidat minimal hors quiesce; P2
impose ensuite le safe point et purge l'ancien etat; P3 installe les assets
sequentiellement puis applique Pattern, macros et globals avant le commit du
contexte de boot. Les autres
operations de persistence conservent leur prevalidation locale. Pattern
Store/delete/clear construisent le namespace inactif puis publient `COMMIT.BIN`.
Les Save utilisent des tranches DATA de 4096 octets et des etapes METADATA
separees; `.TMP` n'est publie qu'apres header final, sync et close, avec `.BAK`
recuperable.

Pattern Save/Load, Project Save, browser SD, Sample RAM, Wavetable et Clear Multi utilisent l'admission Background cooperative de `sd_scheduler_runtime`. Toute demande RT ou transaction active produit `NOT_NOW`; le client conserve son etat et rend la main.

Pattern Save capture un DTO immutable au point d'admission; les mutations
ulterieures ne modifient pas le document en cours d'ecriture. Pattern Load
valide puis committe le candidat comme nouvel etat CONTROL et peut donc
remplacer les edits non sauvegardes presents au moment de l'application.

Project Load decode et valide le document avant d'acquerir la quiesce et
l'exclusivite scheduler, puis sequence les assets RAM avec le loader cooperatif
canonique.
Le safe-point ne conserve que le nettoyage des leases physiques, queues/tails
residuels, etat AUDIO et coherence transactionnelle; le protocole de quiesce
destine a transformer un Load live en arret n'existe plus.
Chaque candidat RAM est complet avant retrait; un remplacement attend ensuite
STOP AUDIO et `T_safe` cote CONTROL avant liberation et commit. Un slot EMPTY
est commite directement. Le quiesce Project reste ferme jusqu'a la fin de cette
sequence. Le restore installe d'abord l'etat CONTROL sous suppression de
publication live, construit et valide avant la frontiere finale un prepared
AUDIO state immutable couvrant PROGRAM, LFO, MIDI channel/source, ownership des
entrees External, parametres audio, tempo/step transport, metronome, mute,
routage, FX, polyphonie et assets deja prets. Le Pattern bank est ensuite
committe, puis une seule commande `STATE_COMMIT` est publiee sur le canal
CONTROL->AUDIO existant. AUDIO applique cette generation de maniere
deterministe; aucun transport Project parallele, aucune deuxieme chronologie,
aucun ACK M7->M4, aucune confirmation de commit et aucun nouveau retour
inter-core ne sont introduits. Le succes Project cote M4 resulte de la
publication engagee et de l'invariant d'application AUDIO; le restore ne
depend donc plus de la capacite cumulative de la FIFO pour des milliers de
PARAM. Patch utilise le meme loader et differe son application lorsqu'un
asset RAM reference est absent.

## User admission

Project Save et Project Load sont des operations UI modales. Tous deux sont
refuses lorsque le transport est `RUNNING`, `START_PENDING` ou dans tout autre
etat non stable; un tel rejet ne provoque aucun `STOP` automatique, snapshot,
modal, transaction ou entree FSM Project. Ils sont admissibles uniquement
lorsque le transport est `STOPPED` stable, puis s'executent comme operations
modales et transactionnelles.
Leur etat busy est derive des lifecycles produit existants, y compris une
commande acceptee mais encore en attente de prise en charge Storage. L'UI
affiche la progression reelle et rejette les inputs locaux jusqu'a la fin
fonctionnelle; CONTROL garde
la meme invariance si une mutation arrive malgre l'UI. Cette regle ne suspend
pas l'IRQ audio ni les workers necessaires; le playback peut etre coupe lorsque
le contrat de remplacement l'exige.

Les Project Save/Load et Pattern Save/Load sont mutuellement exclusifs au point
d'admission: l'action incompatible est refusee, sans file d'attente ni retry
differe. Les mutations utilisateur Multi (Load, Import, Delete, Clear, Replace)
restent mutuellement exclusives au point d'admission. La gate SD reste une
protection physique, pas le mecanisme normal de refus produit. Pattern Save/Load
conserve son comportement non modal documente ci-dessus.

Les quatre Asset Loads (Multi, Stream, Wavetable et Sampler RAM) sont
`STOPPED`-only, avec gate avant creation du job, mutation de pool, demande SD ou
publication d'asset. `WAV Convert` et le rebuild complet du catalogue sont
egalement `STOPPED`-only. Si la SD est absente a l'admission, l'action est
refusee avec feedback `SD ABSENTE`; aucun job, retry ou restart automatique
n'est cree. Une disparition de SD pendant une operation provoque un abort
propre et une erreur visible, a relancer manuellement apres reinsertion.

La Preview SD reste live et utilisable pendant le playback. Le browsing et la
consultation d'un catalogue deja construit restent distincts du rebuild.

Une application Pattern ou Project reussie reconstruit runtime/AUDIO et invalide Undo/Redo. Un rejet conserve integralement l'etat courant.

Le DTO Pattern porte sous forme typee Keyboard, niveau de metronome, structure
Track/MIDI, nombre de voix, routing Audio FX et configuration Mod. Ces champs
sont remis directement a leurs owners et ne possedent ni stable key Param ni
scan `PARAM_COUNT`. Les valeurs PLAY de base appartiennent au snapshot Seq type
et sont copiees/restaurees avec le snapshot Track.

Le routing Looper restaure est projete directement comme seize masques finaux
dans un seul batch FIFO. Les masques CONTROL ne deviennent canoniques qu'apres
publication complete du batch; une ancienne route ne peut donc pas survivre a
un restore annonce comme reussi.
# Asset identity and FM ownership

Persistent asset selections are typed canonical references `{kind, path}`.
Pool slots, logical ordinals and AUDIO handles are derived runtime data and are
never serialized. Project save/load may derive a manifest from selections, but
does not own an identity registry.

FM patch and pattern persistence serializes the typed FM CONTROL owner once.
FM parameters are endpoint addresses into that owner; generic tone parameter
storage is not a second persistent representation for FM.
