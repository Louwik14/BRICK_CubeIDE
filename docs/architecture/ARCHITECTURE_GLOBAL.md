# Architecture globale BRICK6

## Invariants

La topologie logique est un tableau homogène de huit pistes `0..7` sur Low-Cost et Premium. `track_topology` publie seulement cette cardinalité et les capacités communes; `track_state` porte la configuration canonique et `track_runtime` la projette vers les moteurs et ressources physiques. Aucun rôle, ordinal, remap ou slot Special n'entre dans l'identité.

Le Master est un état global du registre de paramètres et du mixer. Looper est un type de Sampler assignable. External est un moteur de piste dont l'entrée physique est arbitrée exclusivement par `track_input_ownership`. Les voies mixer, moteurs, entrées et capacités physiques sont des ressources; ce ne sont jamais des identités de piste.

## Flux d'autorité

`configuration canonique -> validation globale -> projection track_runtime -> moteur/voie physique -> mixer`

Les remplacements en masse valident toutes les familles, types, capacités Looper et conflits External avant mutation. Les getters runtime ne créent pas une seconde autorité.

## Séquence

`seq_model` contient huit modèles identiques de 64 steps et un pool de 1024 p-locks par piste. Le scheduler, le live record, les Note FX, le mute, le stop et le panic sont bornés par les mêmes huit index. `undo_v2` conserve huit transactions structurelles maximum et prévalide l'espace des pools avant toute application.

## UI

STEP 1 à 8 sélectionnent les pistes. STEP 9 à 16 sont contextuels; `SHIFT + STEP 16` ouvre en priorité la page Master globale sans modifier la sélection. Le Master n'a ni séquence, mute, clipboard de piste, Undo, ni slot persistant.

## Persistance

Pattern v6, Project v6, Kit v4 et Patch v4 sont des formats stricts sans conversion d'anciens payloads. Les collections de pistes sont indexées directement `0..7`; Patch représente un slot unique. Un Pattern ou Project appliqué avec succès invalide Undo/Redo.

## Cartographie

- [z1_audio_hard_rt_mix.md](z1_audio_hard_rt_mix.md) : audio, moteurs et mixer.
- [z2_track_runtime_authority.md](z2_track_runtime_authority.md) : état et projection runtime.
- [z2_assignable_looper_external_ownership.md](z2_assignable_looper_external_ownership.md) : Looper et entrées.
- [z3_param_modulation_control.md](z3_param_modulation_control.md) : paramètres et modulation.
- [z4_seq_clock_scheduler.md](z4_seq_clock_scheduler.md) : séquence et cadence.
- [z4_8_track_sequence_core.md](z4_8_track_sequence_core.md) : modèle et Undo/Redo.
- [z4_global_master_step_interface.md](z4_global_master_step_interface.md) : Master et touches STEP.
- [z5_ui_navigation_interaction.md](z5_ui_navigation_interaction.md) : navigation UI.
- [z6_state_persistence_patterns_projects.md](z6_state_persistence_patterns_projects.md) : persistance.
- [z6_current_eight_track_formats.md](z6_current_eight_track_formats.md) : formats courants.

Les documents de `docs/audits` et `docs/Passes` sont des archives datées et ne définissent pas le produit courant.
