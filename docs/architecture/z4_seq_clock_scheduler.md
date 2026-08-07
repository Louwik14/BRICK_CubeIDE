# Z4 — Séquence, clock et scheduler

Le séquenceur contient huit pistes identiques `0..7`. Chacune possède 64 steps avec trig, note, vélocité, durée, microtiming, roll et p-locks, plus un pool de 1024 entrées. Il n'existe aucun modèle ou payload Special.

La clock produit la cadence canonique; le scheduler transforme les occurrences en événements audio ou MIDI bornés. Live record, Note FX, output guard, mute, stop et panic partagent la même cardinalité de huit pistes.

L'Undo/Redo conserve huit transactions maximum pour les mutations structurelles de séquence. Une mutation no-op n'est pas enregistrée; une nouvelle mutation purge Redo. Paste prévalide destinations et capacité de pool, puis applique atomiquement. Les paramètres et modèles MIDI FX restent hors Undo/Redo, sans capture du runtime ni de ses overrides. Copy ne crée pas de transaction. Le remplacement réussi d'un Pattern ou Project invalide l'historique.

## Addendum 2026-08-05 - terminal et budget EUCLID

La chaine courante est source -> MIDI FX 1 -> MIDI FX 2 -> MIDI FX 3 ->
terminal post-FX. Le stage 3 est un hand-off terminal explicite ; il n'existe
pas de quatrième slot. Le terminal conserve au maximum 64 occurrences par
piste, sépare les admissions internes et MIDI, mémorise les retries Off et
expose son high-water.

Le scheduler et le pipeline refusent complètement une paire qui ne peut pas
être admise, conservent les fermetures possédées lorsqu'un Off est refusé et
traitent les fermetures avant les nouveaux On au même sample. Les compteurs et
high-water sont disponibles pour la future mesure H743 ; aucune conclusion de
charge CPU ou d'underrun n'est déduite sans cette mesure.
