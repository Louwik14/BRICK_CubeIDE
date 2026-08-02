# Formats courants à huit slots

Les formats Pattern, Project, Kit et Patch ne décrivent que le contrat courant à huit slots `0..7`. Les versions de fichier ont été incrémentées et les en-têtes dont la version ou la taille ne correspond pas sont rejetés ; aucune conversion des anciens formats n'est effectuée.

Pattern contient huit séquences homogènes de 64 steps avec trig, roll et p-locks complets. Il ne contient plus de séquence Special, d'action Special, d'identité `role + ordinal` ni de remappage topologique. Les configurations, paramètres, routes, Note FX et réglages de cadence sont indexés directement par slot.

Project embarque ce Pattern courant et indexe directement ses états multi et macro par slot. Kit a une capacité exacte de huit pistes. Patch et les snapshots de track ne stockent plus de rôle topologique. Le Master global n'appartient à aucun de ces snapshots de track.

Track, Page et Ensemble Paste restent hors Undo. Le remplacement global réussi d'un Pattern ou d'un Project continue d'invalider l'historique Undo/Redo.
