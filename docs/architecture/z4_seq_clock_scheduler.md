# Z4 — Séquence, clock et scheduler

Le séquenceur contient huit pistes identiques `0..7`. Chacune possède 64 steps avec trig, note, vélocité, durée, microtiming, roll et p-locks, plus un pool de 1024 entrées. Il n'existe aucun modèle ou payload Special.

La clock produit la cadence canonique; le scheduler transforme les occurrences en événements audio ou MIDI bornés. Live record, Note FX, output guard, mute, stop et panic partagent la même cardinalité de huit pistes.

L'Undo/Redo conserve huit transactions maximum pour les mutations structurelles de séquence. Une mutation no-op n'est pas enregistrée; une nouvelle mutation purge Redo. Paste prévalide destinations et capacité de pool, puis applique atomiquement. Les paramètres et modèles MIDI FX restent hors Undo/Redo, sans capture du runtime ni de ses overrides. Copy ne crée pas de transaction. Le remplacement réussi d'un Pattern ou Project invalide l'historique.
