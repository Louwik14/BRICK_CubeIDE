# Z6 - Persistance Pattern, Patch et Project

## Modele et format

Pattern, Project et Patch utilisent exclusivement `persistent_control_model` et le codec explicite `B6CP` version 13. Les DTO ne sont ni des snapshots runtime ni une ABI disque; chaque champ est encode explicitement. Header, kind, sections, longueurs et CRC sont stricts. La section Macro Project version 2 contient 14 macros natives, chacune avec ses valeurs cibles `(track, parametre, valeur)`. Aucun dump de structure n'est lu. AUDIO_GLOBAL contient exactement 51 floats, FILTER douze floats, et aucun type Drum Analog historique n'est accepte. Les enveloppes courantes sont 115 300 octets pour un Pattern et 174 116 octets pour un Project.

Les cles persistantes de famille, type, parametre, MIDI, clock, modulation et asset sont explicites et independantes des ordinaux C. Les FLOAT32 conservent leurs bits. Les indices runtime, contextes AUDIO installes, pointeurs, caches, voix, phases, playheads et UI sont exclus. Note FX persiste directement son unique bloc brut de seize octets `GENERATOR/VOICER/SCALER/TRIG`; il n'existe plus de cle de modele, count, slot ou ORDER, ni de migration depuis les anciennes chaines.

Pattern contient les seize identites. La configuration des children inactifs est conservee, mais pas leurs parametres, assets, routes, modulation, Note FX ou sequence dynamique. En GROUP, chaque child actif est obligatoirement `SAMPLER/RAM`; le master seul persiste MOD, LFO, ENV3 et operateurs, tandis que les children gardent leur lane a un PLAY et leurs niveaux A/B.

Patch contient une entite, ses parametres logiques, zero a deux references
d'assets typees et, pour FM, le DTO de l'owner. `PROJECT.B6C` contient metadata,
manifeste d'assets et 14 macros. Jusqu'a 256 documents Pattern canoniques vivent
separement sous `PROJECTS/P##/PATTERNS/`.
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

Pour Project Load, PREPARE valide integralement `PROJECT.B6C` et charge le seul
Pattern actif directement depuis le dossier du Project avant la frontiere
forward-only: magic/version, taille exacte, sections, bornes, CRC, semantique
du Pattern actif, coherence de ses references avec le manifeste et capacites fixes.
La canonicalisation WAV
crash-safe appartient aussi a PREPARE. Un refus `MEDIA_ERROR` ou
`INVALID_DOCUMENT` abandonne uniquement ce candidat jamais publie: ingress,
CONTROL, SEQ, AUDIO, assets et racine Pattern actifs restent intacts.

`T_FORWARD` est defini une seule fois par l'appel de
`project_load_quiesce_request()` et, concretement, par sa fermeture de l'ingress.
PANIC et retrait des gros payloads rendent alors l'ancien Project definitivement
mort. Le changement de racine Pattern active et `AUDIO_STATE_COMMIT` sont des
publications techniques post-forward, jamais d'autres commits produit. Apres
`T_FORWARD`, une erreur locale de fichier asset conserve la reference et la
configuration Track, publie une source silencieuse et marque l'asset
`UNAVAILABLE`; elle n'annule pas le Project. Slot, pool, registration,
descriptor, resolution runtime, publication CONTROL/SEQ/AUDIO ou etat machine
impossibles apres leur preuve sont des invariants fatals.

Une perte de media ou un changement de `media_epoch` post-forward produit
`FAILED_FORWARD_MEDIA`: transport arrete, ingress ferme, aucun contexte de boot
du candidat publie et aucune restauration de l'ancien Project. Les chargements
partiels sont annules/retires vers l'avant. Un nouveau Project Load ou Blank
explicite peut reprendre quand le media revient; seul son FINALIZE reouvre
l'ingress. Le contexte de boot n'est publie qu'apres installation reussie du
runtime. Les resultats Project Load exposes sont `NOT_NOW`, `MEDIA_ERROR`,
`INVALID_DOCUMENT`, `FAILED_FORWARD_MEDIA` et le succes.
Project Blank suit exactement la meme machine; une construction ou validation
impossible du candidat canonique est fatale, tandis qu'un echec SD du dossier
Pattern reste une erreur media.
Les Save utilisent des tranches DATA de 4096 octets et des etapes METADATA
separees; `.TMP` n'est publie qu'apres header final, sync et close, avec `.BAK`
recuperable.

La fin de PREPARE et l'appel de quiescence definissent explicitement
`T_FORWARD` pour Project Load. Avant `T_FORWARD`, workspace et candidat
peuvent etre jetes parce que le live n'a jamais ete modifie; ce cleanup de
candidat n'est pas un rollback runtime. Apres `T_FORWARD`, le remplacement est
strictement forward-only et aucun chemin d'echec ne rouvre implicitement
l'ingress. Le Working Pattern suit avant cette frontiere le pipeline commun
`DECODED -> PREPARE -> PREPARED`: le codec a valide le document, les owners
CONTROL sont ensuite prevalides une seule fois, les
cles persistantes et adresses de p-lock sont resolues, les capacites sont
prouvees, le `seq_pattern_t` inactif est entierement compile et le slot
`PreparedAudio` type est construit. Les 51 valeurs `AUDIO_GLOBAL` sont validees
par leur contrat Param complet (finitude, domaine, enum/type et mapping AUDIO)
avant que le candidat puisse devenir `PREPARED`. La publication CONTROL finale
installe Pattern et macros, finalise le slot depuis les owners CONTROL installes,
puis le publie dans un unique commit AUDIO de type Project;
l'identite Pattern courante et le hook UI unique ne sont publies qu'apres le
commit AUDIO reussi. Le chemin interne d'installation Pattern ne cree donc pas
de transaction imbriquee et ne publie aucun etat UI intermediaire.

Le remplacement publie aussi, avant ce commit AUDIO, la generation Sequence
complete deja compilee avec un nouvel epoch d'execution. Son commit ne parse,
ne resout, n'alloue et ne compile rien: il retime depuis le shadow transport
courant puis swap le slot/generation. Publication immutable et retrait du
graphe mutable SEQ forment une seule section critique: slots terminaux
READY/READING, curseur AUDIO, core/ledgers/calendriers, held NoteFX, ingress,
pending IRQ, force-stop et disarm/rearm sont retires ensemble. Les evenements
live-rec encore en attente dans CONTROL sont jetes au commit de remplacement,
avant la publication du slot prepare. AUDIO
n'acquiert ensuite qu'un bloc de la generation d'execution courante ou de la
generation publiee suivante. Cette barriere vaut meme lorsque le transport est
arrete: au PLAY suivant, aucun terminal NOTE ou PARAM compile depuis l'ancien
Pattern ne peut etre applique aux moteurs du nouveau Project.
Les recalls Pattern a l'arret suivent le meme ordre; un recall en lecture garde
son epoch musical et publie sa generation a la frontiere de cycle choisie.
Le meme prepareur sert les DTO charges, le Blank construit par
`persistent_pattern_control_build_defaults()` et le Working Pattern Project.
Le boot bas niveau conserve ses initialiseurs d'owners: il construit le premier
live et ne remplace aucun Pattern.
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
partie du CORE Project version 3 et est donc engage dans la meme transaction
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
canonique reutilise ses pages. Seuls le DTO Project, le Pattern actif prepare et
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

Une application Pattern ou Project reussie reconstruit runtime/AUDIO et invalide Undo/Redo. Un Project structurellement rejete conserve integralement l'etat courant; apres `T_FORWARD`, aucun rollback runtime ni restauration de l'ancien Project n'existe.

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
Quand un slot Multi est retire, l'admission des notes liee a ce slot est retiree aussi. A la publication du nouveau runtime READY, `project_control` reprojette les selections de pistes portant la meme reference canonique vers AUDIO et retablit leur liaison de notes. La reference affichee seule ne prouve pas que cette projection est active.
Une publication PROGRAM qui reconstruit une piste reinitialise le selecteur d'instrument dans AUDIO. La publication structurelle reprojette donc ensuite l'asset Sampler canonique deja selectionne et disponible; sinon le nom reste visible dans CONTROL alors que le NOTE_ON voit un instrument invalide dans AUDIO. La barre du selecteur Tone provient de la valeur UI de ce parametre, pas du nombre de pages effectivement lisibles.
La fin du chargement Multi annonce READY seulement apres la publication de la projection AUDIO et l'association du runtime logique; une erreur a cette etape suit le chemin d'echec du chargement.
La progression du Browser reste inferieure a 100 % tant que l'instrument est LOADING, meme si toutes les pages de preparation ont deja ete copiees.
Pool slots, logical ordinals and AUDIO handles are derived runtime data and are
never serialized. Project save/load may derive a manifest from selections, but
does not own an identity registry.

FM patch and pattern persistence serializes the typed FM CONTROL owner once.
FM parameters are endpoint addresses into that owner; generic tone parameter
storage is not a second persistent representation for FM.

# Project-load storage cost

Project Load changes the active Pattern root to `PROJECTS/P##/PATTERNS/` and
enumerates only that directory to build `g_present` and recover `.TMP`/`.BAK`
residue. It neither copies nor rebuilds a bank of 256 files.

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
`LOADING`, `DECODED` et `PREPARED`; READY et queue ne sont plus deux autorites. Apres decode,
la validation structurelle utilise les familles, types, inputs et polyphonies du
candidat complet; un budget de voix invalide est donc refuse avant APPLY. Le
candidat devient `PREPARED` seulement apres validation des owners CONTROL,
resolution des keys, compilation complete du slot SEQ inactif et construction
du slot `PreparedAudio` type. Une cible provisoire est preparee hors IRQ; ses
descripteurs PROGRAM, etats owners, mutes et handles de ressources sont tous
reconstruits depuis CONTROL apres l'installation. Le slot final est une cible
complete : OFF, default et absence de ressource effacent explicitement l'etat
du Pattern precedent. Il est ensuite
soit applique immediatement, soit arme sur la boundary. Tout travail faillible
est termine avant l'attente; CONTROL commit et SEQ commit n'ont plus de resultat
utilisateur normal. Une impossibilite a ce stade est un invariant fatal.
`AUDIO_STATE_COMMIT(slot,generation)` et sa fence FIFO restent le mecanisme
AUDIO. Le `changed_program_mask` est calcule tardivement dans l'IRQ; les
transitions conservent release-before-acquire, le rebind des held outputs pour
Pattern et le PANIC/full rebuild pour Project. Pattern/Project ne construisent,
ne dedupliquent et ne rejouent plus de transaction de commandes live.
Les tailles de reference ARM finales sont `PreparedPattern = 8 876` octets,
`PreparedAudio = 8 728` octets et slot `PreparedAudio = 8 736` octets. Le
workspace Persistence reste une union bornee par son budget SDRAM; Project Save
ne reserve plus de DTO Pattern ni de record de banque. La
transaction AUDIO legacy de `36 872` octets appartient uniquement a Patch.
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
immediatement apres leur `T_FORWARD`; PREPARE ne modifie pas ce candidat live.

Apres chaque application Pattern reussie, le hook UI de restauration globale
ferme les gestes/Undo d'edition encore ouverts, normalise la lane active vers une
entite sequencable dans la nouvelle topologie, invalide les caches derives et
resynchronise la page courante. Project Load et Project Blank empruntent ce meme
point d'application; l'identite current est publiee avant ce hook et aucune page
ne porte une seconde logique de resynchronisation.

Project Save reecrit uniquement `PROJECTS/P##/PROJECT.B6C` avec son cycle
`.TMP`/`.BAK`; il ne lit et ne reencode aucun Pattern. Pattern Save reecrit
uniquement le document du slot concerne dans `PATTERNS/`. Un fichier absent
represente un slot vide. La creation construit d'abord un candidat sous
`BRICK/TRANSACTIONS/PROJECT/`, puis publie le dossier; aucune seconde banque
persistante ne subsiste apres publication.

Le workspace Persistence est une union a ownership exclusif. Pattern IO le garde
jusqu'au commit/annulation du candidat. Project Save porte son scratch d'encodage
dans son propre membre jusqu'au commit/cleanup; Project Restore porte de meme
son scratch codec et ses DTO finaux dans son membre Restore. L'ancienne zone
record partagee et sa lease multi-owner n'existent plus. Aucun pointeur scratch
ne survit au changement d'owner de l'union; cette fusion reduit aussi le pic
SDRAM de 25 216 octets.
