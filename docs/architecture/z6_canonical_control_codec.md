# Format persistant CONTROL canonique — version 1

## Statut

Cette couche est disponible pour la future migration, sans être branchée sur les chemins produit. `PatternSaveV1`, `ProjectSaveV1` et `PatchSaveV1` restent les formats actifs.

## Enveloppe commune

Tous les entiers sont little-endian et chaque champ est encodé explicitement. Aucun layout, padding, enum C ou pointeur n'est une ABI disque.

Le header fixe de 24 octets contient :

- magic `B6CP` (4 octets) ;
- version de format `1` (u16) ;
- type de document Project, Pattern ou Patch (u8) et un octet réservé ;
- nombre de sections (u16) et deux octets réservés ;
- longueur totale bornée (u32) ;
- CRC32 IEEE du payload (u32) ;
- CRC32 IEEE des 20 premiers octets du header (u32).

Chaque section commence par type u16, version u16 et longueur u32. Pattern possède `PATTERN_BODY`; Patch possède `PATCH_BODY`; Project possède, dans l'ordre, `PROJECT_CORE`, `PROJECT_ASSETS`, `PROJECT_MACROS` et `PROJECT_BANK`.

Les clés famille, type, paramètre, source MIDI, horloge, Note FX et asset sont les clés disque stables définies par le modèle canonique. Les valeurs CONTROL flottantes conservent leurs bits IEEE-754 binary32. Les assets ne contiennent que l'ID logique, le type et le chemin.

## Staging et validation

Le lecteur est séquentiel. Pattern et Patch sont entièrement décodés dans un staging fourni par l'appelant. Project conserve son cœur et son working Pattern dans le staging appelant, puis diffuse les Patterns de banque un par un vers un consommateur transactionnel. `put` ne doit écrire que dans le staging transactionnel du consommateur; `commit` n'est appelé qu'après contrôle de toutes les sections, du CRC et du Project complet; toute erreur appelle `abort`.

Les contrôles couvrent magic/version/type, longueurs et sections, CRC, capacités de chaque collection, clés obligatoires, doublons, IDs d'entity, PLAY 8/8/1, budgets de p-locks, routes et destinations MOD, GROUP master/children, Note FX children sans Note FX master et références d'assets logiques.

Le codec n'alloue aucune mémoire dynamique et ajoute 0 octet de staging statique. Les tailles maximales des objets de staging, mesurées avec l'ABI ARM de la cible, sont : Project 1 193 732 octets, Pattern 504 664 octets, Patch 4 060 octets. Ces objets sont donc explicitement fournis et placés par l'intégrateur; ils ne doivent jamais être alloués sur une pile de tâche. Le Project comprend un seul record Pattern de transit, quelle que soit la taille de la banque.

## Frontière de la passe

Le codec ne contient aucune application runtime, AUDIO, UI ou mixer. La conversion DTO vers l'état produit et le remplacement des V1 appartiennent à la passe de migration suivante.
