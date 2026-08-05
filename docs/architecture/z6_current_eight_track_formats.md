# Formats courants à huit slots

Les formats Pattern, Project, Kit et Patch ne décrivent que le contrat courant à huit slots `0..7`. Les versions de fichier ont été incrémentées et les en-têtes dont la version ou la taille ne correspond pas sont rejetés ; aucune conversion des anciens formats n'est effectuée.

Pattern contient huit séquences homogènes de 64 steps avec trig, roll et p-locks complets. Il ne contient plus de séquence Special, d'action Special, d'identité `role + ordinal` ni de remappage topologique. Les configurations, paramètres, routes, réglages de cadence et les trois slots MIDI FX `S1..S3` sont indexés directement par slot. Le format courant est Pattern v7 ; les payloads des versions antérieures sont rejetés.

Project embarque ce Pattern courant et indexe directement ses états multi et macro par slot. Son format courant est Project v7 ; les payloads antérieurs sont rejetés sans migration. Kit a une capacité exacte de huit pistes. Patch et les snapshots de track ne stockent plus de rôle topologique. Le Master global n'appartient à aucun de ces snapshots de track.

Les snapshots de track et Pattern/Project transportent l'état de base normalisé des trois slots, jamais le runtime Note FX. Page/Ensemble Paste appliquent d'abord `MODEL`, puis les trois paramètres dépendants ; la transaction Undo/Redo Note FX échange l'état de base complet sans capturer le runtime. Le remplacement global réussi d'un Pattern ou d'un Project continue d'invalider l'historique Undo/Redo.
