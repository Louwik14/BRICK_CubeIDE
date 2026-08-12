# AGENT.md

## Rôle

Ce fichier guide les interventions dans BRICK6. La carte canonique vit dans `docs/architecture/ARCHITECTURE_GLOBAL.md`; le détail local dans `docs/architecture/z*.md`; le code courant reste l'autorité finale.

## Mode de travail

- Travailler silencieusement jusqu'au résultat final, sans progression ni narration.
- Réponse finale minimale: verdict, micro-patch ou absence de patch, docs mises à jour, dépendances hors zone.
- Auditer l'autorité canonique et ses consommateurs avant toute mutation.
- Préserver les changements utilisateur et éviter les refontes hors périmètre.
- Ne pas créer de double autorité, d'allocation dynamique dans le runtime critique ou de coût audio non borné.
- Mettre à jour cette carte et la documentation Z concernée quand un invariant change.

## Invariants produit

- Exactement huit pistes sonores homogènes `0..7` sur Low-Cost et Premium.
- L'index est l'unique identité; aucun rôle, ordinal, Play/Special ou slot fixe Master, FX, Input, Looper.
- `track_state` décide, `track_runtime` projette, les moteurs et le mixer exécutent.
- Looper est `Sampler / Looper`, assignable à un slot avec son runtime propre.
- External est MIDI + audio; `track_input_ownership` est l'unique autorité des entrées physiques et interdit deux propriétaires pour une même entrée.
- Une ressource physique ou un quota de variante ne doit jamais devenir une identité logique.
- STEP 1 à 8 sélectionnent les pistes; STEP 9 à 16 sont contextuels.
- Master est global et s'ouvre par `SHIFT + STEP 16` sans changer la piste active. Il n'a ni séquence, mute, clipboard de piste, Undo ou slot persistant.
- La piste FX et MacroFX n'existent plus. Reverb, delay, compresseur et gain Master restent globaux.

## Séquence et édition

- Huit modèles identiques: 64 steps, trig/note/vélocité/durée/microtiming/roll, p-locks complets, pool 1024 par piste.
- Aucun payload ou pool Special.
- Undo/Redo structurel: huit transactions maximum, no-op ignoré, Redo purgé sur nouvelle branche, validation de capacité avant application atomique.
- Copy ne crée pas de transaction; Clear et Paste de steps sont undoables.
- Un remplacement Pattern ou Project réussi invalide l'historique.

## Persistance

- Pattern v12, Project v12 et Patch v6, avec rejet strict de toute autre version/taille.
- Pattern et Project indexent directement huit slots `0..7`; Patch représente un slot unique.
- Aucun remap, rôle ou ordinal topologique persistant.
- Valider l'intégralité d'un snapshot, les quotas Looper et les conflits External avant toute mutation.

## Orientation

- Audio/mixer: `docs/architecture/z1_audio_hard_rt_mix.md`.
- Tracks/ressources: `z2_track_runtime_authority.md` et `z2_assignable_looper_external_ownership.md`.
- Paramètres/modulation: `z3_param_modulation_control.md`.
- Séquence/Undo: `z4_seq_clock_scheduler.md` et `z4_8_track_sequence_core.md`.
- UI/Master: `z5_ui_navigation_interaction.md` et `z4_global_master_step_interface.md`.
- Persistance: `z6_state_persistence_patterns_projects.md` et `z6_current_eight_track_formats.md`.

Les documents datés de `docs/audits` et `docs/Passes` sont historiques; ils ne remplacent jamais la carte courante.
