# Architecture globale BRICK6

## Invariants

La topologie logique possède seize identités stables : huit entités top-level `0..7`, puis huit entités GROUP children `8..15` actives uniquement lorsque l'entité 7 est GROUP master. `entity_topology` est l'unique autorité de l'activité, du rôle et de la relation parent/membre. Ces propriétés et les capacités qui en découlent sont calculées, jamais stockées en parallèle. `track_state` porte la configuration canonique propre de chaque entité, y compris celle des children lorsqu'ils sont inactifs, et `track_runtime` la projette vers les moteurs et ressources physiques.

Le Master est un état global du registre de paramètres et du mixer. Looper est un type de Sampler assignable. External est un moteur de piste dont l'entrée physique est arbitrée exclusivement par `track_input_ownership`. Les voies mixer, moteurs, entrées et capacités physiques sont des ressources; ce ne sont jamais des identités de piste.

## Flux d'autorité

`configuration canonique -> validation globale -> projection track_runtime -> moteur/voie physique -> mixer`

Les remplacements en masse valident toutes les familles, types, capacités Looper et conflits External avant mutation. Les getters runtime ne créent pas une seconde autorité.

## Séquence

`seq_model` contient huit modèles identiques de 64 steps et un pool de 1024 p-locks par piste. Chaque piste Play possède exactement trois slots MIDI FX (`S1..S3`) ; le scheduler, le live record, le mute, le stop et le panic sont bornés par les mêmes huit index. `undo_v2` conserve huit transactions maximum pour les mutations de séquence uniquement ; l'état de base MIDI FX est persistant et copiable, mais reste hors Undo/Redo.

CONTROL possède le séquenceur, les Note FX, le routage musical et les sorties MIDI. AUDIO publie le sample clock, consomme les événements audio datés et reste l'unique autorité d'admission des notes internes, d'allocation et de stealing. Le MIDI ne traverse pas l'admission AUDIO.

Les transitions destructives et le panic ferment les admissions internes par des commandes `CLOSE_ENTITY` / `CLOSE_ALL` datées dans le même flux CONTROL vers AUDIO.

L'admission AUDIO conserve le binding exact associé à chaque occurrence. Une fermeture vise donc l'ancien moteur/instance même si CONTROL a déjà publié une nouvelle génération; aucun moteur, mixer ou allocator n'est appelé depuis le scheduler ou les Note FX.

## UI

STEP 1 à 8 sélectionnent les pistes. STEP 9 à 16 sont contextuels; `SHIFT + STEP 16` ouvre en priorité la page Master globale sans modifier la sélection. Le Master n'a ni séquence, mute, clipboard de piste, Undo, ni slot persistant.

## Persistance

Pattern v12, Project v12 et Patch v6 sont des formats stricts sans conversion d'anciens payloads. Pattern/Project embarquent trois slots MIDI FX par piste ; les p-locks MIDI FX courants occupent 12 positions. Les collections de pistes sont indexées directement `0..7`; Patch représente un slot unique. Un Pattern ou Project appliqué avec succès invalide Undo/Redo.

FM conserve une base voice DX7 compacte comme autorité unique, complétée par les macros BRICK non destructives. `TONE 1/2` expose les projections GLOBAL, OP QUICK et Pitch EG; les champs UI Pitch/Transpose restent persistés par les packs DX7 canoniques, sans asset, LFO DX7, destination MOD ni travail audio supplémentaire.

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
