# Drum DSP portage — Passe 3 (couche paramètres dédiée + exposition TONE/COLORS)

Références:
- `docs/DRUM_DSP_PORTAGE_PLAN.md`
- `docs/DRUM_DSP_PASS1_SOCLE_AUDIT.md`
- `docs/DRUM_DSP_PASS2_RUNTIME_INTEGRATION.md`

## Objectif

Cette passe introduit une couche paramètres dédiée à `Drum` dans le registre global, séparée de `DX7/MonoB/TB3`, puis branche ces paramètres jusqu’au runtime `drum_synth` avec exposition `TONE`/`COLORS` track-aware.

## Points clés

- Ajout d’IDs `PARAM_DRUM_*` dédiés, sans recyclage des IDs synth.
- Classification explicite des paramètres `Drum` en domaines `TONE` et `COLORS`.
- Application runtime via `param_registry` -> `track_runtime` -> `drum_synth_set_param_for_instance()`.
- Exposition UI `TONE`/`COLORS` pour les types `Drum` dans les templates track-aware existants.
- `PLAY` inchangé (Drum reste mono via autorité runtime centrale).

## Écart notable

- `TRXHiHat::peak` existe dans la source desktop mais n’est pas utilisé dans `Process()` ; il reste disponible en paramètre technique (`PARAM_DRUM_TRX_HIHAT_PEAK`) mais n’est pas exposé dans le mapping utilisateur `COLORS`.

## Invariants conservés

- Aucune fusion `Synth`/`Drum`.
- Pas de mapping indirect Drum -> paramètres DX7/MonoB/TB3.
- Autorité de binding inchangée (`track_runtime`).
- Runtime drum toujours mono dans `PLAY`.
