# Z6 - Persistance Pattern, Patch et Project

## Modele et format

Pattern, Project et Patch utilisent exclusivement `persistent_control_model` et le codec explicite `B6CP` version 4. Les DTO ne sont ni des snapshots runtime ni une ABI disque; chaque champ est encode explicitement. Header, kind, sections, longueurs et CRC sont stricts. Aucune ancienne version ni dump de structure n'est lu.

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

Pour Project Load, P1 decode un candidat minimal; P2 impose le safe point et
purge l'ancien etat; P3 installe les assets sequentiellement puis applique
Pattern, macros et globals avant le commit du contexte de boot. Les autres
operations de persistence conservent leur prevalidation locale. Pattern
Store/delete/clear construisent le namespace inactif puis publient `COMMIT.BIN`.
Les Save utilisent des tranches DATA de 4096 octets et des etapes METADATA
separees; `.TMP` n'est publie qu'apres header final, sync et close, avec `.BAK`
recuperable.

Pattern Save/Load, Project Save, browser SD, Sample RAM, Wavetable et Clear Multi utilisent l'admission Background cooperative de `sd_scheduler_runtime`. Toute demande RT ou transaction active produit `NOT_NOW`; le client conserve son etat et rend la main.

Project Load decode le document sous quiesce et exclusivite scheduler, puis rend
l'exclusivite et sequence les assets RAM avec le loader cooperatif canonique.
Chaque candidat RAM est complet avant retrait; un remplacement attend ensuite
STOP AUDIO et `T_safe` cote CONTROL avant liberation et commit. Un slot EMPTY
est commite directement. Le quiesce Project reste ferme jusqu'a la fin de cette
sequence, puis le restore republie PROGRAM/PARAM/TRANSPORT/RECORD par la FIFO
avant publication UI. Aucun loader RAM synchrone ni ACK AUDIO de restore
n'existe. Patch utilise le meme loader et differe son application lorsqu'un
asset RAM reference est absent.

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
