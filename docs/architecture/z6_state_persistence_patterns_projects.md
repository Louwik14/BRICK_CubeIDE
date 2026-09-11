# Z6 - Persistance Pattern, Patch et Project

## Modele et format

Pattern, Project et Patch utilisent exclusivement `persistent_control_model` et le codec explicite `B6CP` version 5. Les DTO ne sont ni des snapshots runtime ni une ABI disque; chaque champ est encode explicitement. Header, kind, sections, longueurs et CRC sont stricts. Aucune ancienne version ni dump de structure n'est lu.

Les cles persistantes de famille, type, parametre, MIDI, clock, Note FX, modulation et asset sont explicites et independantes des ordinaux C. Les FLOAT32 conservent leurs bits. Les indices runtime, contextes AUDIO installes, pointeurs, caches, voix, phases, playheads et UI sont exclus.

Pattern contient les seize identites. La configuration des children inactifs est conservee, mais pas leurs parametres, assets, routes, modulation, Note FX ou sequence dynamique. En GROUP, le master possede MOD et Audio FX; les children ont leur lane a un PLAY et leurs niveaux A/B.

Patch contient une entite, ses parametres logiques, zero a deux references
d'assets typees et, pour FM, le DTO de l'owner. Project contient metadata,
Pattern de travail, manifeste d'assets, macros/scenes et jusqu'a 256 records
Pattern diffuses progressivement.

## Codec et application

Le decode commence par les controles de format, bornes et CRC, puis construit un
candidat borne dans l'espace inactif. Les capacites topologiques sont derivees
par `persistent_entity_topology`; la validation metier des owners reste dans la
phase d'installation. Les providers/consumers Project reutilisent un workspace
borne sans allocation dynamique.

`persistent_pattern_control` et `persistent_patch_control` sont les facades CONTROL. `pattern_control_bank`, `patch_product` et `project_product` sont les facades produit. Une reference asset persistante est `{kind, canonical_path}`; elle est canonicalisee une fois a l'entree de l'owner, puis le codec la valide et l'encode sans transformation. `project_control` ne resout le slot AUDIO qu'apres chargement, au moment de la publication fonctionnelle. Sample et tables Wave ne possedent plus de stable key Param. L'owner FM unique est encode champ par champ, sans packs flottants, copie operateur secondaire ni codec historique. Les tables et mipmaps restent des data planes immutables hors FIFO.

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

Patch Save et Rename utilisent une seule machine Storage cooperative. Le Save
capture un DTO immutable avant soumission; Rename relit le DTO du slot puis ne
remplace que `metadata.name`. Les deux operations ecrivent `P%04u.B6C.TMP`,
sync/close, commitent via `.BAK`, puis publient un resultat terminal consommable
une seule fois. Le slot, le filename et les metadonnees ne sont mis a jour
qu'apres commit reussi.

Pattern Save/Load, Project Save, browser SD, Sample RAM, Wavetable et Clear Multi utilisent l'admission Background cooperative de `sd_scheduler_runtime`. Toute demande RT ou transaction active produit `NOT_NOW`; le client conserve son etat et rend la main.

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
restore reconstruit ensuite une projection PROGRAM/PARAM fraiche depuis les
autorites CONTROL finales. Le commit Project libere toutes les installations
AUDIO avant leur reconstruction; le commit Pattern ne libere que les PROGRAM
modifies, sans PANIC global, puis rebind une fois les outputs encore vivants.
Le contrat wire classe chaque commande comme etat durable, action transitoire,
cycle de vie ressource ou requete. Le snapshot ne conserve que l'etat durable;
les commandes PARAM `TEMP` et `CLEAR_TEMP` sont donc exclues de cette projection,
comme les STOP de ressources et les requetes de waveform. Les etats
de selection exposes a l'UI sont `EMPTY`, `LOADED` et `UNAVAILABLE`. Patch garde
sa transaction asset locale distincte.

Une application Pattern ou Project reussie reconstruit runtime/AUDIO et invalide Undo/Redo. Un Project structurellement rejete conserve integralement l'etat courant; apres entree en remplacement, aucun rollback complet des gros assets n'est maintenu.

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

Boot-context Flash distinguishes valid, known-clear and unknown/corrupt state.
Clearing an already persisted clear context is a no-op; unknown state is still
erased and rewritten.
