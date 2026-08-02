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
