# Z6 - Persistance Pattern, Patch et Project

## Modele et format

Pattern, Project et Patch utilisent exclusivement `persistent_control_model` et le codec explicite `B6CP` version 13. Les DTO ne sont ni des snapshots runtime ni une ABI disque; chaque champ est encode explicitement. Header, kind, sections, longueurs et CRC sont stricts. Aucune ancienne version ni dump de structure n'est lu. AUDIO_GLOBAL contient exactement 51 floats, FILTER douze floats, et aucun type Drum Analog historique n'est accepte. Les enveloppes courantes sont 115 300 octets pour un Pattern et 29 799 358 octets pour un Project.

Les cles persistantes de famille, type, parametre, MIDI, clock, modulation et asset sont explicites et independantes des ordinaux C. Les FLOAT32 conservent leurs bits. Les indices runtime, contextes AUDIO installes, pointeurs, caches, voix, phases, playheads et UI sont exclus. Note FX persiste directement son unique bloc brut de seize octets `GENERATOR/VOICER/SCALER/TRIG`; il n'existe plus de cle de modele, count, slot ou ORDER, ni de migration depuis les anciennes chaines.

Pattern contient les seize identites. La configuration des children inactifs est conservee, mais pas leurs parametres, assets, routes, modulation, Note FX ou sequence dynamique. En GROUP, chaque child actif est obligatoirement `SAMPLER/RAM`; le master seul persiste MOD, LFO, ENV3 et operateurs, tandis que les children gardent leur lane a un PLAY et leurs niveaux A/B.

Patch contient une entite, ses parametres logiques, zero a deux references
d'assets typees et, pour FM, le DTO de l'owner. Project contient metadata,
Pattern de travail, manifeste d'assets, macros/scenes et jusqu'a 256 records
Pattern diffuses progressivement.
Quand `modulation_present` est actif, ENV3 n'existe qu'une fois dans le Patch,
dans l'enveloppe de modulation; capture, Init, codec et application utilisent
cette representation unique.
Le format Patch courant porte explicitement `modulation_present`: un Patch de
child GROUP ne capture ni ne restaure l'etat MOD partage du master.

La sequence persiste directement `Length`, `Division`, `Direction`, `Rotate`
et le bloc canonique `seq_track_timing_config_t`: `Base`, `Quantize`, identite
`Groove`, `Global`, `Timing`, `Random` et `Velocity`. Capture, comparaison et
restore passent par les owners runtime; aucun champ Quant/Swing historique ni
adaptateur intermediaire ne subsiste. Le Pattern porte aussi un seed Groove
32 bits copie avec lui. Un slot vide le derive une fois de son identite
bank/pattern; il alimente la direction `RANDOM` et le Random Groove futur sans
table persistante.

## Codec et application

Le decode commence par les controles de format, bornes et CRC, puis construit un
candidat borne dans l'espace inactif. Les capacites topologiques sont derivees
par `persistent_entity_topology`; la validation metier des owners reste dans la
phase d'installation. Les providers/consumers Project reutilisent un workspace
borne sans allocation dynamique.

`persistent_pattern_control` et `persistent_patch_control` sont les facades CONTROL. `pattern_control_bank`, `patch_product` et `project_product` sont les facades produit. Une reference asset persistante est `{kind, canonical_path}`; elle est canonicalisee une fois a l'entree de l'owner, puis le codec la valide et l'encode sans transformation. `project_control` ne resout le slot AUDIO qu'apres chargement, au moment de la publication fonctionnelle. Sample et tables Wave ne possedent plus de stable key Param. L'owner FM unique est encode champ par champ, sans packs flottants, copie operateur secondaire ni codec historique. Les tables et mipmaps restent des data planes immutables hors FIFO.

Pour PATCH SAVE, `prepare` capture un snapshot complet et son slot dans l'owner
produit. Ce snapshot reste admis pendant une modale de nommage et n'est libere que
par `submit`, par une annulation produit explicite, ou par la reinitialisation de
l'owner. La validation canonique du nom compare uniquement les `name_length`
octets persistants; les octets hors chaine d'un buffer de travail ne font pas
partie du format ni de l'admission transactionnelle.

Le Patch Load lit et decode cooperativement dans STORAGE_IO, precharge ses assets,
puis prevalide la topologie, les capacites, la polyphonie et tous les owners pour
l'ensemble du masque avant un unique commit structurel CONTROL. Sample RAM,
Wavetable et Multi possedent tous un resultat de preparation terminal; aucune
target n'est mutee avant que toutes les references du masque soient READY. Les
targets ne sont jamais appliquees une par une par l'UI. Le remplacement purge
les overrides TEMP runtime Patch actifs (sans modifier les p-locks stockes dans
la sequence), installe les owners CONTROL, puis publie un snapshot AUDIO unique
de type `CONTROL_AUDIO_STATE_PATCH` et attend que le consumer ait franchi le
commit. Les selectors d'asset sont projetes uniquement vers leur moteur owner:
un clear Wavetable n'est jamais adresse a FM, Prism, Stack ou Sampler. Avant la
premiere mutation, la transaction capture le Patch CONTROL et les overrides
runtime de chaque cible. Tout refus apres ce point annule le snapshot candidat,
restaure ces autorites dans un second snapshot PATCH et ne publie aucun nouveau
slot courant. `persistent_patch_control_make_default` derive ses
valeurs des factories CONTROL canoniques et est utilise par CLEAR via exactement
le meme contrat que LOAD, sans toucher sequence, mute, MIDI, slot ou fichiers.

## Transactions

Pour Project Load, P1 valide integralement le document Project avant le safe
point: magic/version, taille exacte, sections, bornes, CRC, semantique des
Patterns et coherence de leurs references avec le manifeste. P1 ne lit pas les
fichiers d'assets externes. Un refus abandonne le workspace et le staging sans
fermer l'ingress, PANIC ni retirer une ressource courante, et ne publie pas le
contexte de boot. Apres STOP, quiescence et `T_safe`, l'ancien catalogue et ses
gros payloads sont retires; les assets du nouveau Project sont alors charges
sequentiellement dans la memoire liberee. Une erreur locale de fichier conserve
la reference et la configuration Track, publie une source silencieuse et marque
l'asset `UNAVAILABLE`; elle n'annule pas le Project. Pour Multi, toute erreur
d'index ou de child invalide l'instrument entier. Un changement de media epoch,
une SD absente ou un mount globalement perdu arrete le load. Le contexte de boot
n'est publie qu'apres installation reussie du runtime. Les autres operations de
persistence conservent leur prevalidation locale. Pattern Store/delete/clear
construisent le namespace inactif puis publient `COMMIT.BIN`.
Les Save utilisent des tranches DATA de 4096 octets et des etapes METADATA
separees; `.TMP` n'est publie qu'apres header final, sync et close, avec `.BAK`
recuperable.

La fin de P1 et l'appel de quiescence definissent explicitement `T_commit` pour
Project Load. Avant `T_commit`, workspace et bank inactif peuvent etre jetes
sans mutation live. Apres `T_commit`, le remplacement est forward-only: les
payloads retires ne declenchent pas un rollback tardif. La publication CONTROL
finale installe Pattern et macros dans un unique snapshot AUDIO de type Project;
l'identite Pattern courante et le hook UI unique ne sont publies qu'apres le
commit AUDIO reussi. Le chemin interne d'installation Pattern ne cree donc pas
de transaction imbriquee et ne publie aucun etat UI intermediaire.

Le remplacement publie aussi, avant ce commit AUDIO, une generation Sequence
complete avec un nouvel epoch d'execution. Cette barriere vaut meme lorsque le
transport est arrete: au PLAY suivant, aucun terminal NOTE ou PARAM compile
depuis l'ancien Pattern ne peut etre applique aux moteurs du nouveau Project.
Les recalls Pattern a l'arret suivent le meme ordre; un recall en lecture garde
son epoch musical et publie sa generation a la frontiere de cycle choisie.
La compilation atomique de plusieurs geometries Groove reutilise alors le
scratch de decode du Project ou du Pattern, devenu mort apres validation. Elle
ne tente pas de reacquerir le workspace persistence encore possede par la
transaction; la publication SEQ peut donc terminer avant le commit AUDIO.
Le PANIC inclus dans un commit Project invalide dans la meme primitive les
renderers physiques et le miroir d'ownership terminal SEQ cote AUDIO. Cette
regle vaut aussi lorsque PANIC est invoque depuis un snapshot atomique, sans
passer par le post-traitement d'une commande FIFO autonome.

Au boot, les pools reconstruisent directement leur etat vide. En particulier,
Sampler RAM ne passe pas par le reset runtime quiesce: ses structures SDRAM et
ses flags D3 sont en sections `NOLOAD`, donc leurs octets retenus ne constituent
pas un etat de slot valide avant l'initialisation explicite. Le restore du
dernier Project peut ensuite employer le quiesce normal; retirer un pool vide
est alors une operation idempotente et prouvee.

Patch Save et Rename utilisent une seule machine Storage cooperative. Le Save
capture un DTO immutable avant soumission. Les tweaks UI ordinaires installent
d'abord leur valeur dans l'owner CONTROL canonique: Tone, FM, Filter, VCA, FX,
polyphonie et modulation sont donc captures avec leur valeur editee. Les
overrides TEMP de p-lock restent volontairement du runtime Sequence et ne sont
pas persistants. Save choisit exclusivement le premier slot vide et refuse au
niveau produit toute destination deja presente; le focus browser ne participe
pas a cette decision. Rename relit le DTO du slot puis ne remplace que
`metadata.name`. Les deux operations ecrivent `P%04u.B6C.TMP`,
sync/close, commitent via `.BAK`, puis publient un resultat terminal consommable
une seule fois. Le slot, le filename et les metadonnees ne sont mis a jour
qu'apres commit reussi.

Au boot, le scan Patch execute d'abord la recovery `.TMP/.BAK`, puis decode le
document complet avec `persist_codec_decode_patch`. Il n'existe plus de parseur
de header reduit divergent du codec: magic `B6CP`, version, taille, CRC header,
CRC payload, sections, cles et bornes suivent exactement le meme validateur que
LOAD. CLEAR dans le browser supprime le slot selectionne, y compris un document
invalide impossible a decoder, ainsi que ses sidecars; il ne signifie pas Init
du Track. Le cache RAM passe a EMPTY uniquement apres les unlink storage.

Pattern Save/Load, Project Save, browser SD, Sample RAM, Wavetable et Clear Multi utilisent l'admission Background cooperative de `sd_scheduler_runtime`. Toute demande RT ou transaction active produit `NOT_NOW`; le client conserve son etat et rend la main.

Le nom Project canonique (32 caracteres maximum, contrat `name_contract`) fait
partie du CORE Project version 2 et est donc engage dans la meme transaction
temporaire/backup que le snapshot. Cette version CORE est la seule acceptee.
Project Save revalide que le
transport est arrete avant le snapshot; un refus ou une erreur ne publie ni STOP
ni PANIC et ne modifie pas le son.
Le resultat terminal de Project Save reste dans sa mailbox jusqu'a
`project_product_save_take_result`; Save, Load et les autres operations Project
sont refusees jusque-la. Sur Load reussi, le slot actif et le nom decode sont
publies ensemble dans l'etat produit courant, puis seulement refletes dans le
cache browser. Cancel ou erreur conservent le couple precedent.

Pour les chargements utilisateur Sample RAM et Wavetable, la superloop consomme
la completion physique, valide le slot, le global, le chemin et la resolution
logique, puis retient un resultat terminal par famille. Settings ne fait que
prendre ce resultat pour rafraichir sa vue. Un echec de preparation n'est
jamais publie comme succes et laisse l'ancien asset READY intact. Descriptor,
identite globale et capacite sont valides avant la frontiere de commit; apres
`T_safe`, le swap ne possede plus d'echec produit recuperable et les anciennes
pages ne sont liberees qu'apres installation.

Project Load ne double-bufferise pas les gros payloads RAM, Wavetable ou Multi:
le quiesce reste ferme, l'ancien payload est retire, puis le loader cooperatif
canonique reutilise ses pages. Seuls le DTO Project, le Pattern bank inactif et
un catalogue borne de references indisponibles coexistent temporairement. Le
retrait publie d'abord un STOP de ressource vers AUDIO. Une FIFO fonctionnelle
momentanement pleine est une contre-pression: le service attend un passage
suivant sans liberer le payload. `retire_failed` n'est arme que si la
publication est refusee alors qu'une place etait annoncee. Le quiesce ne devient
sur qu'apres extinction des leases, fin du travail SD du cache et retrait de
tous les pools. Le
restore reconstruit ensuite une projection PROGRAM/PARAM fraiche depuis les
autorites CONTROL finales. Le commit Project libere toutes les installations
AUDIO avant leur reconstruction; le commit Pattern ne libere que les PROGRAM
modifies, sans PANIC global, puis rebind une fois les outputs encore vivants.
Le contrat wire classe chaque commande comme etat durable, action transitoire,
cycle de vie ressource ou requete. Le snapshot ne conserve que l'etat durable;
les commandes PARAM `TEMP` sont donc exclues de cette projection; `CLEAR_TEMP`
est admis uniquement pour invalider atomiquement les overrides d'un Patch
remplace. Les STOP de ressources et les requetes de waveform restent exclus. Les etats
de selection exposes a l'UI sont `EMPTY`, `LOADED` et `UNAVAILABLE`. Patch garde
sa transaction asset locale distincte.

Une application Pattern ou Project reussie reconstruit runtime/AUDIO et invalide Undo/Redo. Un Project structurellement rejete conserve integralement l'etat courant; apres entree en remplacement, aucun rollback complet des gros assets n'est maintenu.

Le DTO Pattern porte sous forme typee Keyboard, niveau de metronome, structure
Track/MIDI, nombre de voix, routing Audio FX et configuration Mod. Ces champs
sont remis directement a leurs owners et ne possedent ni stable key Param ni
scan `PARAM_COUNT`. Les valeurs PLAY de base appartiennent au snapshot Seq type
et sont copiees/restaurees avec le snapshot Track.

Le routing d'une capture Audio REC est un etat de session Recorder et n'est pas
un backend de playback persistant par track. Les routes CONTROL persistantes ne
sont pas appliquees au data-plane AUDIO.
# Asset identity and FM ownership

Persistent asset selections are typed canonical references `{kind, path}`.
Pool slots, logical ordinals and AUDIO handles are derived runtime data and are
never serialized. Project save/load may derive a manifest from selections, but
does not own an identity registry.

FM patch and pattern persistence serializes the typed FM CONTROL owner once.
FM parameters are endpoint addresses into that owner; generic tone parameter
storage is not a second persistent representation for FM.

# Project-load storage cost

The Pattern bank no longer probes all 256 slot names and transactional variants
on the nominal path. `g_present` remains the active-set index, the staging
bitmap remains the authority for the set under construction, and the dedicated
`S0`/`S1` directories are enumerated to discover only files that actually
exist. The same enumeration recovers and removes `.TMP`/`.BAK` crash residue.
Commit publishes the staging bitmap directly and cleans only real entries from
the retired set.

Project decode retains one complete non-mutating pass before staging and one
application pass. Semantic validation and CRC now share the first pass; the
former separate CRC pre-read has been removed. The two remaining passes are
still synchronous and bounded by the on-disk format.

Le dernier projet actif est conserve dans `0:/BRICK/BOOT.B6C`. Un fichier
absent, invalide, corrompu ou designant un projet absent restaure les valeurs
par defaut. La calibration Hall globale est chargee en RAM au boot depuis
`0:/BRICK/HALL.B6C`; son absence ou son invalidite conserve le workflow de
calibration. Aucun de ces stores ne possede de fallback Flash interne.

## Invariants de recall et de restauration globale

Le recall Pattern possede un seul candidat et une seule identite
`{generation, bank, pattern, boundary}`. Ses phases sont `EMPTY`, `REQUESTED`,
`LOADING` et `PENDING`; READY et queue ne sont plus deux autorites. Apres decode,
la validation structurelle utilise les familles, types, inputs et polyphonies du
candidat complet; un budget de voix invalide est donc refuse avant APPLY. Le
candidat `PENDING` est ensuite soit applique immediatement, soit arme sur la boundary.
Cette boundary est globale au Pattern sortant et ne depend jamais de la lane
selectionnee dans l'UI. Comme le modele ne porte pas de longueur globale
separee, son cycle est celui de la lane sequencable dont la traversee complete
est la plus longue sur la grille transport: `cycle_traversee * division`.
`cycle_traversee` inclut le retour PINGPONG; longueur, division et direction
proviennent exclusivement du Pattern courant. En cas d'egalite, la lane de plus
petit identifiant est le representant deterministe de cette meme boundary.
La date est capturee une seule fois lors de l'armement depuis le curseur et la
phase de division possedes par SEQ, jamais reconstruite depuis l'UI ni depuis
un compteur CONTROL historique. Le candidat attend ensuite que cette date
entre dans le premier horizon non publie avant le swap atomique.
Son payload reste dans le workspace `PATTERN_IO`, owner unique et scope jusqu'au
commit ou a l'annulation. Un STOP vide le candidat et libere ce workspace; une
completion asynchrone d'une generation remplacee termine seulement son cleanup
et ne peut plus publier. Project Load et Project Blank annulent le candidat
avant leur staging.

Apres chaque application Pattern reussie, le hook UI de restauration globale
ferme les gestes/Undo d'edition encore ouverts, normalise la lane active vers une
entite sequencable dans la nouvelle topologie, invalide les caches derives et
resynchronise la page courante. Project Load et Project Blank empruntent ce meme
point d'application; l'identite current est publiee avant ce hook et aucune page
ne porte une seconde logique de resynchronisation.

Project Save materialise toujours le Pattern de travail capture comme record du
slot actif dans la section bank. Il remplace le record bank plus ancien, ou
l'ajoute si le slot etait jusque-la absent. Le CORE et le bank ne peuvent donc
pas diverger sur le Pattern actif et tout Project produit contient le record que
Project Load exige. Le nombre d'assets admis au Save est borne par la capacite
Restore effective afin qu'un fichier nouvellement cree reste rechargeable.
Au debut du Save, un token de generation fige le namespace Pattern bank et son
nombre de records jusqu'au replace final ou au cleanup. Store, delete et
staging Project sont refuses pendant cette lecture; le token est reverifie juste
avant la publication du `.TMP`. Le Save conserve ainsi une vue logique stable
sans copier les 256 Patterns ni ajouter un second bank RAM.

Le workspace Persistence est une union a ownership exclusif. Pattern IO le garde
jusqu'au commit/annulation du candidat. Project Save porte son record scratch
dans son propre membre jusqu'au commit/cleanup; Project Restore porte de meme
son scratch codec et ses DTO finaux dans son membre Restore. L'ancienne zone
record partagee et sa lease multi-owner n'existent plus. Aucun pointeur scratch
ne survit au changement d'owner de l'union; cette fusion reduit aussi le pic
SDRAM de 25 216 octets.
