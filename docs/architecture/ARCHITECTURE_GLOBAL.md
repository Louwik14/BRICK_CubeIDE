# Architecture globale BRICK6

Le code courant est l'autorite finale. Ce document est l'unique porte d'entree documentaire; les documents de domaine portent les contrats detailles sans les repeter ici.

## Invariants produit

- Seize identites logiques stables existent: huit entites top-level `0..7` et huit children GROUP `8..15`, actives uniquement lorsque l'entite 7 est GROUP master.
- `entity_topology` derive activite, role, parent et capacites. `track_state` possede la configuration CONTROL; `track_runtime` la projette vers les moteurs, ressources et voies physiques.
- Looper est un type de Sampler assignable. External est un moteur dont l'entree physique est arbitree par `track_input_ownership`. Ressource physique, voie mixer et quota ne sont jamais des identites logiques.
- CONTROL possede UI, MIDI, sequence, ROLL, Note FX, p-locks, outputs logiques, deadlines, stealing musical et catalogue Sample. STORAGE possede le Stream, son I/O et son page-cache. AUDIO possede IRQ, mapping d'execution `{output_id,note,velocity,gate} -> slot DSP`, RELEASE physique, lecteurs, positions, moteurs et mixer. PROGRAM peut remplacer un renderer compatible sans tuer l'output logique ni emettre NOTE OFF/ON.
- L'ordre fonctionnel CONTROL vers AUDIO traverse exclusivement la FIFO SPSC
  unique PROGRAM/PARAM/NOTE/TRANSPORT/RECORD/PANIC. Les gros data planes et
  retours physiques utilisent des structures fixes, pointer-free et separees.
- `STOP(output_id)` rend l'output musicalement mort dans CONTROL. AUDIO peut conserver une tail RELEASE et libere ou reutilise physiquement le slot sans ACK musical.
- Pattern, Project et Patch utilisent exclusivement le codec CONTROL explicite version 4.

## Flux principaux

```text
configuration CONTROL -> validation globale -> FIFO fonctionnelle -> AUDIO
SEQ/live -> resolution CONTROL -> START/STOP/RETRIGGER dates -> AUDIO
capture TIM5 -> conversion audio -> file datee -> segmentation -> rendu
credit de fenetre stream AUDIO -> I/O Storage tokenisee -> page AUDIO
Save/Load -> decode safety -> safe quiesce -> progressive install -> publication
```

Restore reste CONTROL et republie les effets AUDIO par la FIFO unique; aucune
completion AUDIO ne reconstruit ou ne confirme l'etat musical.

La configuration modulation/ENV3/Matrix, le routing Looper, l'ownership des
entrees, le bus Recorder, la selection Wavetable et le lifecycle Preview sont
des decisions CONTROL ordonnancees dans cette meme FIFO. Les rings PCM,
tables/mipmaps et compteurs de data plane restent separes; ils ne portent
aucune seconde chronologie fonctionnelle.

## Arborescence physique

```text
Inc/ et Src/
|-- App/                 bootstrap et orchestration produit
|-- Audio/               runtime AUDIO, mixer, DSP et effets
|   `-- Engines/         moteurs Prism, Stack, FM, Wavetable et Sampler AUDIO
|-- IPC/                 contrats, transports et projections CONTROL/AUDIO
|-- Platform/            configuration physique, memoire, faults et diagnostics
|-- Track/               identites, topologie et etat/runtime de piste
|-- Param/               catalogue, valeurs CONTROL et migration PARAM
|-- Mod/                 configuration Matrix/LFO et projection AUDIO derivee
|-- Seq/                 modele, edition, scheduler et runtime sequenceur
|-- NoteFx/              transformations musicales CONTROL
|-- Sampler/             pools, caches, readers et preparation de ressources
|-- Storage/             persistence, projets, capture et services I/O
`-- UI/                  navigation, pages, interactions et rendu
```

Les contrats publics vivent sous `Inc/<domaine>` et les implementations sous
`Src/<domaine>`. Les headers strictement internes restent pres de leur
implementation dans `Src`. Aucun domaine generique `Core` ne subsiste.

## Documents proprietaires

- [z0_plateforme_cadence.md](z0_plateforme_cadence.md): plateforme, memoire, cache, cadence et IPC CONTROL/AUDIO.
- [z1_audio_hard_rt_mix.md](z1_audio_hard_rt_mix.md): moteurs, voix, mixer, GROUP et effets.
- [z2_track_runtime_authority.md](z2_track_runtime_authority.md): identites, topologie, programmes, Looper et External.
- [z3_param_modulation_control.md](z3_param_modulation_control.md): parametres, valeur canonique, p-locks, modulation et commandes AUDIO datees.
- [z4_seq_clock_scheduler.md](z4_seq_clock_scheduler.md): sequence, Note FX, horodatage live, files et Undo/Redo.
- [z5_ui_navigation_interaction.md](z5_ui_navigation_interaction.md): navigation, modes, selection, Master et ordre des handlers.
- [z6_state_persistence_patterns_projects.md](z6_state_persistence_patterns_projects.md): modele, codec, cles, Pattern, Patch, Project et transactions Storage.
- [stream_need_contract.md](stream_need_contract.md): Sampler RAM, Wavetable, Multi, streaming, page-cache et transport I/O.
- [recorder_sd.md](recorder_sd.md): bus AUDIO REC unique, ARM TRIG/peak pointer-free, Recorder, capture Looper, reservation fichier et relecture de prise.
- [m4_m7_functional_command_contract.md](m4_m7_functional_command_contract.md): FIFO fonctionnelle unique et consumer AUDIO.
- [m7_m4_physical_return_contract.md](m7_m4_physical_return_contract.md): retours physiques minimaux, diagnostic et ownership des data planes.
