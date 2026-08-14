# Format persistant CONTROL canonique — version 3

## Statut

Project, Pattern et Patch utilisent ce format sur les parcours produit. Aucun dump de structure C et aucune lecture d'un ancien format ne sont admis.

## Enveloppe commune

Tous les entiers sont little-endian et chaque champ est encodé explicitement. Aucun layout, padding, enum C ou pointeur n'est une ABI disque.

Le header fixe contient le magic `B6CP`, la version 3, le type de document, le nombre de sections, la longueur totale et les CRC32 du payload et du header. Chaque section possède son type, sa version et sa longueur. Pattern possède `PATTERN_BODY`; Patch possède `PATCH_BODY`; Project possède, dans l'ordre, `PROJECT_CORE`, `PROJECT_ASSETS`, `PROJECT_MACROS` et `PROJECT_BANK`.

Les clés famille, type, paramètre, source MIDI, horloge, Note FX et asset sont stables. Les valeurs CONTROL flottantes conservent leurs bits IEEE-754 binary32. Une référence d'asset portée par un Pattern ou un Patch est un N logique; seul le manifeste Project associe ce N à un type et un chemin.

## Codec Project progressif

L'encodage reçoit des providers distincts pour les metadata/globales, le Pattern de travail, les assets, les macros/scènes et les records de bank Pattern. Le décodage effectue d'abord une passe structurelle et une passe sémantique complètes sans mutation, puis diffuse ces mêmes unités vers des consumers explicites.

La bank Pattern est ouverte avant toute mutation Project. Ses records sont écrits un par un dans le namespace inactif et le commit n'est publié qu'après application complète; toute erreur appelle `abort`.

Le codec n'alloue aucune mémoire dynamique. Le workspace Project global mesure 512 904 octets sur l'ABI ARM cible, contre 697 128 octets auparavant. Il contient au maximum un record Pattern de transit, réutilisé en union avec les macros, un asset de transit et la table bornée des IDs d'assets. Sa taille ne dépend donc plus de la taille cumulée du Pattern de travail, des assets et des macros. Aucun DTO Project complet n'est matérialisé.

## Frontière

Le codec ne contient aucune application runtime, AUDIO, UI ou mixer. Les adapters produit capturent et appliquent les autorités CONTROL à la granularité des providers et consumers.
