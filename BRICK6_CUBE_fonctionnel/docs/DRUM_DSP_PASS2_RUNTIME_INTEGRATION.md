# Drum DSP portage — Passe 2 (intégration runtime)

Références:
- `docs/DRUM_DSP_PORTAGE_PLAN.md`
- `docs/DRUM_DSP_PASS1_SOCLE_AUDIT.md`

## 1) Intégration runtime `Drum` (sans routeur parallèle)

- Autorité inchangée: `track_runtime` reste le point unique de binding family/type -> engine/instance.
- `Drum` est bindée en engine runtime dédié `TRACK_RUNTIME_ENGINE_DRUM`.
- Instance drum = `track_id` (pas d’allocation dynamique, pas de table parallèle cachée).

## 2) Catalogue final séparé

- `Synth`:
  - `DX7`, `MonoB`, `TB3`
- `Drum`:
  - `TRX BD`, `TRX Claves`, `TRX HiHat`, `TRX Snare`
  - `FM Kick`, `FM Snare`, `FM Tom`, `FM Rimshot`, `FM Clap`, `FM Cowbell`, `FM Cymbal`

Séparation appliquée dans:
- catalogue UI family/type,
- validation family/type,
- binding runtime,
- résolution PLAY/contextuelle.

## 3) Modèles drum hébergés/exécutables

Modèles effectivement instanciés au runtime firmware:
- `TRXBassDrum`
- `TRXClaves`
- `TRXHiHat`
- `TRXSnareDrum`
- `FmKickModel`
- `FmSnareModel`
- `FmTomModel`
- `FmRimshotModel`
- `FmClapModel`
- `FmCowbellModel`
- `FmCymbalModel`

## 4) Cycle runtime branché

- init global: `drum_synth_init()`
- sélection modèle par instance: `drum_synth_set_model_for_instance()`
- trigger: `drum_synth_note_on_for_instance()`
- note off: `drum_synth_note_off_for_instance()` (no-op explicite pour one-shot)
- rendu audio: `drum_synth_process_block_for_instance()`
- all-notes-off/panic: `drum_synth_all_notes_off_for_instance()` / `_all()`

Entrées branchées:
- scheduler PLAY (`seq_play_scheduler`)
- keyboard interne/externe (`keyboard_engine`)
- panic global (`seq_output_guard`)
- rendu audio bloc (`brick6_audio_runtime`)

## 5) Mono/poly PLAY

- Déclaration centralisée via `track_runtime_get_voice_mode()`.
- `Drum` déclaré monophonique (`PLAY` page 1 uniquement) via engine runtime drum.
- Aucun changement de règle spéciale locale hors autorité runtime.

## 6) Contraintes respectées dans cette passe

- Aucun mapping de paramètres Drum sur DX7/TB3/MonoB.
- Aucune exposition complète `TONE/COLORS` Drum (templates Drum exclus de l’enregistrement TONE/COLORS).
- Pas de recyclage du catalogue `Synth` pour `Drum`.
- Pas de redesign runtime global.

## 7) Reste à faire (passe 3)

- couche paramètres utilisateur Drum dédiée (sans proxy synth);
- exposition UI `TONE/COLORS` Drum réelle;
- validation audio AB moteur par moteur contre source de référence;
- durcissement policy note-off/decay per-modèle si nécessaire produit.
