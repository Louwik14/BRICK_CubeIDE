# Architecture globale BRICK6

Le code courant est l'autorite finale. Ce document est l'unique porte d'entree documentaire; les documents de domaine portent les contrats detailles sans les repeter ici.

## Invariants produit

- Seize identites logiques stables existent: huit entites top-level `0..7` et huit children GROUP `8..15`, actives uniquement lorsque l'entite 7 est GROUP master.
- `entity_topology` derive activite, role, parent et capacites. `track_state` possede la configuration CONTROL; `track_runtime` la projette vers les moteurs, ressources et voies physiques.
- Looper est un type de Sampler assignable. External est un moteur dont l'entree physique est arbitree par `track_input_ownership`. Ressource physique, voie mixer et quota ne sont jamais des identites logiques.
- CONTROL possede UI, MIDI, sequence, ROLL, Note FX, p-locks, outputs logiques, deadlines et stealing musical. AUDIO possede IRQ, mapping `output_id -> slot DSP`, RELEASE physique, moteurs, mixer et page-cache.
- Les echanges CONTROL/AUDIO sont des rings SPSC, mailboxes latest-wins, snapshots versionnes ou descripteurs `{region, offset, length}` fixes et sans pointeur.
- `STOP(output_id)` rend l'output musicalement mort dans CONTROL. AUDIO peut conserver une tail RELEASE et libere ou reutilise physiquement le slot sans ACK musical.
- Pattern, Project et Patch utilisent exclusivement le codec CONTROL explicite version 3.

## Flux principaux

```text
configuration CONTROL -> validation globale -> projection IPC -> AUDIO
SEQ/live -> resolution CONTROL -> START/STOP/RETRIGGER dates -> AUDIO
capture TIM5 -> conversion audio -> file datee -> segmentation -> rendu
besoin stream AUDIO -> commande tokenisee -> I/O Storage -> completion de page AUDIO
Save/Load -> prevalidation -> transaction -> publication atomique
```

Restore publie un plan immutable; AUDIO seul le commit et publie la completion. Project Load ferme les admissions, obtient SAFE d'AUDIO puis l'exclusivite Storage avant decode et restore.

## Documents proprietaires

- [z0_plateforme_cadence.md](z0_plateforme_cadence.md): plateforme, memoire, cache, cadence et IPC CONTROL/AUDIO.
- [z1_audio_hard_rt_mix.md](z1_audio_hard_rt_mix.md): moteurs, voix, mixer, GROUP et effets.
- [z2_track_runtime_authority.md](z2_track_runtime_authority.md): identites, topologie, bindings, Looper et External.
- [z3_param_modulation_control.md](z3_param_modulation_control.md): parametres, valeur canonique, p-locks, modulation et commandes AUDIO datees.
- [z4_seq_clock_scheduler.md](z4_seq_clock_scheduler.md): sequence, Note FX, horodatage live, files et Undo/Redo.
- [z5_ui_navigation_interaction.md](z5_ui_navigation_interaction.md): navigation, modes, selection, Master et ordre des handlers.
- [z6_state_persistence_patterns_projects.md](z6_state_persistence_patterns_projects.md): modele, codec, cles, Pattern, Patch, Project et transactions Storage.
- [stream_need_contract.md](stream_need_contract.md): Sampler RAM, Wavetable, Multi, streaming, page-cache et transport I/O.
- [recorder_sd.md](recorder_sd.md): bus AUDIO REC unique, ARM TRIG/peak pointer-free, Recorder, capture Looper, reservation fichier et relecture de prise.
