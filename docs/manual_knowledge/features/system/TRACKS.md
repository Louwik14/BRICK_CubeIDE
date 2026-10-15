# TRACK-01 — Pistes, topologie et modes

## Statut

CONFIRME — validation materielle : selection tactile/Hall.

## Description et acces

Huit top-level stables 0..7. Les familles utilisateur sont OFF, SYNTH, DRUM, MIDI, SAMPLER et EXTERNAL. Types : PRISM, DRUM_MD, MIDI, STREAM, MULTI, STACK, WAVE, EXTERNAL, GROUP, FM, ACID. GROUP n'est disponible que pour l'entite 7 ; il active les children 8..14, chacun SAMPLER/RAM mono. L'entite 15 est la Master harmonique permanente.

TRACK maintenu + surface selectionne une entite. CFG MAIN edite famille/type/polyphonie/entree selon contexte ; CFG MIDI edite canal 1..16 et source INTERNAL/EXTERNAL/ALL. Une mutation structurelle valide l'etat final, commit CONTROL, reconstruit PROGRAM puis resynchronise UI. External reserve une entree physique exclusive.

## Interactions et persistance

La topologie determine les pages, moteurs, sequence, owner MOD et ressources. Le master GROUP porte bus/FX/MOD communs ; les children gardent sequence, PLAY, TONE/MIX/FX local. La configuration complete, children inactifs compris, est dans Pattern/Project. Clipboard Track transporte l'etat logique et peut echouer sur conflit External.

## Sources

`Inc/Track/track_types.h`, `Src/Track/entity_topology.c`, `track_catalog.c`, `track_state.c`, `track_runtime.c`, `Src/UI/ui_core.c::ui_core_apply_track_family_change`, `ui_core_apply_track_type_change`.
