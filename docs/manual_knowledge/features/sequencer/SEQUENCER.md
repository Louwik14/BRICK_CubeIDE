# SEQ — Sequenceur, PLAY, p-locks et enregistrement

## Statut

CONFIRME — validation timing/LED materielle requise.

## Modele

Chaque lane active a 64 steps, quatre pages, longueur, masque de pages, division/direction/groove, trig et roll. Un step peut etre vide, note, p-lock seul ou note+p-lock. PLAY porte jusqu'a huit voix, chacune Note, Velocity, Length et Microtiming ; ces quatre champs structurels ne sont pas des Param.

## Commandes

En mode SEQ, STEP bascule trig. STEP tenu + encodeur cree/met a jour un p-lock du parametre affiche ; relachement ferme le geste. Les PAGE sous SHIFT choisissent page, chordent un masque ou, par double tap 300 ms, isolent une page. PLAY ouvre l'edition V1..V4 ; encodeurs modifient les quatre champs, sur tous les steps tenus. COPY/PASTE choisit steps tenus sinon toute la sequence ; SHIFT+COPY clear. Undo/Redo s'appuie sur `undo_v2` et est invalide par restore global.

REC arme la capture live. Les evenements notes/params sont horodates, associes a une session et quantifies/microtimes dans la lane. STOP, changement structurel ou restore ferme les captures et gestes projetant l'ancien Pattern.

## Execution

SEQ compile une generation immutable et schedule Note FX, p-locks et notes. Les p-locks Note FX restent CONTROL avant la NOTE de boundary ; les autres deviennent PARAM TEMP date. A la fin, BASE/CLEAR_TEMP revele la base courante. Roll emet sur une grille 60 points. Le Pattern recall en lecture swappe une generation preparee a une boundary globale.

## Limites/persistance

32 locks par step, 512 entrees par lane. Adresse p-lock = set canonique + slot, pas ordinal `param_id`. Pattern persiste modele, PLAY, locks, Note FX et geometrie ; etats scheduler/held/futures non.

## Sources

`Inc/Seq/{seq_types,seq_model}.h`, `Src/Seq/{seq_model,seq_edit,seq_runtime,seq_engine,seq_live_rec_session,seq_timing}.c`, `Src/UI/ui_core.c`, `ui_param.c`, pages SEQ/PLAY, `docs/architecture/z4_seq_clock_scheduler.md`.
