# Z6 - Persistance Pattern, Patch et Project

## Modele et format

Pattern, Project et Patch utilisent exclusivement `persistent_control_model` et le codec explicite `B6CP` version 3. Les DTO ne sont ni des snapshots runtime ni une ABI disque; chaque champ est encode explicitement. Header, kind, sections, longueurs et CRC sont stricts. Aucune ancienne version ni dump de structure n'est lu.

Les cles persistantes de famille, type, parametre, MIDI, clock, Note FX, modulation et asset sont explicites et independantes des ordinaux C. Les FLOAT32 conservent leurs bits. Les indices runtime, bindings, pointeurs, caches, voix, phases, playheads et UI sont exclus.

Pattern contient les seize identites. La configuration des children inactifs est conservee, mais pas leurs parametres, assets, routes, modulation, Note FX ou sequence dynamique. En GROUP, le master possede MOD et Audio FX; les children ont leur lane a un PLAY et leurs niveaux A/B.

Patch contient une entite, ses parametres logiques et une reference d'asset. Project contient metadata, Pattern de travail, manifeste d'assets, macros/scenes et jusqu'a 256 records Pattern diffuses progressivement.

## Codec et application

Le decode effectue une passe structurelle puis semantique complete avant mutation. Les capacites topologiques sont derivees par `persistent_entity_topology`. Les providers/consumers Project evitent tout DTO Project complet et reutilisent un workspace borne sans allocation dynamique.

`persistent_pattern_control`, `persistent_patch_control` et `persistent_project_control` sont les facades CONTROL. `pattern_control_bank`, `patch_product` et `project_product` sont les facades produit. Une reference asset persistante est `{N, kind, path}`; les entites conservent N et `project_control` publie les projections runtime apres chargement.

## Transactions

Toute prevalidation precede la mutation. Pattern Store/delete/clear et restauration de bank Project construisent le namespace inactif puis publient `COMMIT.BIN`. Les Save utilisent des tranches DATA de 4096 octets et des etapes METADATA separees; `.TMP` n'est publie qu'apres header final, sync et close, avec `.BAK` recuperable.

Pattern Save/Load, Project Save, browser SD, Sample RAM, Wavetable et Clear Multi utilisent l'admission Background cooperative de `sd_scheduler_runtime`. Toute demande RT ou transaction active produit `NOT_NOW`; le client conserve son etat et rend la main.

Project Load reste synchrone seulement apres fermeture transport/panic, arret Preview/Recorder, ACK SAFE AUDIO, drainage et exclusivite scheduler. Restore suit `PREPARE CONTROL -> REQUEST -> COMMIT AUDIO -> COMPLETE -> etat canonique CONTROL -> projection runtime CONTROL sans emission -> publication UI`. CONTROL ne fabrique jamais SAFE et n'execute jamais le commit AUDIO.

Une application Pattern ou Project reussie reconstruit runtime/AUDIO et invalide Undo/Redo. Un rejet conserve integralement l'etat courant.
