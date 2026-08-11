# Noyau de séquence à huit tracks

Le noyau courant expose huit slots sonores, identifiés uniquement par leur index `0..7`, sur Low-Cost comme sur Premium. `SEQ_TRACK_COUNT`, les tableaux runtime de séquence, mute, stop et panic sont bornés par cette cardinalité.

Chaque slot utilise le même modèle : 64 steps, trig, roll, p-locks complets, 32 locks par step et un pool de 1024 entrées. Le noyau ne possède plus de payload d'action Special ni de pool Special.

## Snapshot canonique et clipboard de steps

`seq_step_snapshot_t` transporte seulement validité, trig, roll et p-locks. Il ne transporte aucune identité de rôle ou d'ordinal. La compatibilité d'une destination est déterminée par le support réel de chaque paramètre sur le slot cible.

Le Paste résout d'abord tous les steps réellement atteints, prévalide leur payload et le budget global du pool, puis applique la liste en une seule mutation. Copy ne démarre aucune transaction Undo. Clear et Paste restent les seuls producteurs structurels avec la pose ou suppression d'un trig.

## Undo/Redo

L'historique conserve huit transactions dans un anneau commun Undo/Redo. Une transaction référence directement un slot `0..7` et une image complète des steps touchés, p-locks inclus. Elle ne contient aucune identité topologique.

Les invariants restent : no-op non enregistré, nouvelle branche supprimant le Redo, éviction du plus ancien élément, échange image courante/image stockée, préflight de capacité, application atomique, suspension explicite de capture et invalidation après remplacement global Pattern/Project.

Les formats Pattern, Project, Kit et Patch restent hors de cette étape et sont migrés dans l'étape 2.

## Contrat PLAY, enregistrement et affichage

La base PLAY de piste porte NOTE, VELOCITY, LENGTH et MICROTIMING pour chaque voix disponible. Un step peut porter ces mêmes champs avec un masque de présence. Ces données PLAY sont structurelles et distinctes des p-locks génériques : un champ présent surcharge la base, un champ absent l’hérite dynamiquement.

Un quick trig ne matérialise donc pas NOTE. Avec une base C3, un trig sans NOTE propre joue C3; un step avec NOTE E3 joue E3. Si la base devient D3, le premier joue D3 et le second reste E3.

ROLL est une donnée structurelle indépendante de NOTE. Le scheduler résout d’abord la NOTE propre ou héritée, puis ROLL et ses retriggers. L’édition ROLL sur plusieurs steps tenus modifie tous ces steps dans une transaction Undo unique.

Le contexte d’édition est explicite : STOP sans step tenu et PLAY sans REC modifient uniquement la base; le playhead n’est jamais une cible d’édition. Un step tenu reçoit la donnée PLAY. PLAY + REC n’utilise le playhead qu’après validation du gardien REC.

En GROUP, l’active lane est la cible PLAY canonique pour base, step, feedback, affichage et flash; le parent GROUP ne remplace jamais l’identité de la sous-track active. L’UI affiche la base en contexte normal et la valeur structurelle en édition explicite de step.

Le live recording NOTE possède un owner et un pending uniques. Le NOTE ON filtre et fige l’identité source/canal/note; le NOTE OFF ferme ce pending avec cette identité même si la configuration MIDI/source a changé. STOP ou le désarmement REC vide cet état.
