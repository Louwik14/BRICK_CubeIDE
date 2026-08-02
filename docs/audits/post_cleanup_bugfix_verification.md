# Vérification de la vague de correctifs post-ménage

Date : 2026-08-02
Révision vérifiée : `a1588a1ea` avec le worktree courant préservé.

## 1. Verdict global

La vague est fonctionnellement cohérente et stable sur le périmètre demandé. Les corrections topology/storage, CFG, Master/FX, LEDs, ENV/VCA, Stream Sampler, Sampler Multi et frontière audio sont présentes dans le code réel. Les deux firmwares Release construisent.

Une seule validation reste rouge : `hall_lowcost_integration_validation.ps1` attend le seuil Low-Cost commité à `300U`, alors qu'une modification locale préexistante et non liée à cette passe place ce seuil à `200U`. Le chemin Hall sans filtre multi-échantillons et les builds restent valides. Cette modification parallèle a été préservée et n'est pas incluse dans le commit de vérification.

## 2. Matrice des corrections

| Zone | Preuve vérifiée | Verdict |
|---|---|---|
| Ordre Special | Low-Cost : Play 1..8, Input1, Looper, FX, Master. Premium : Input2 puis Input3 en slots 13 et 14. Les constantes et descripteurs topology portent l'ordre. | Conforme |
| Capacités Special | Identité, capacités, mute et binding sont résolus par rôle topologique, pas par position. Master/FX restent des rôles fixes. | Conforme |
| Remapping storage | Pattern, Project et Kit construisent une bijection bornée `role + ordinal`; config, sound, mix, séquences/actions, routes, globals trackés, Note FX, Multi et locks Macro sont normalisés avant application. Identité absente ou dupliquée : refus sans mutation. | Conforme |
| Ordre CFG | Catalogue explicite borné `Off -> Synth -> Drum -> MIDI -> External -> Sampler`, sans wrap aux limites; les familles indisponibles sont sautées et Input1..3 exclus. L'enum persisté reste `0..8` dans son ordre historique. | Conforme |
| TONE Master | Résolution effective par rôle; pages reverb, delay et compresseur accessibles. Clipboard global via `param_get/param_set` et undo snapshot. | Conforme |
| TONE FX | Résolution effective par rôle; quatre MacroFX et seize paramètres. Clipboard track-aware via le registre track et undo snapshot. | Conforme |
| Surfaces interdites | Master/FX quittent le calcul de masque après TONE : aucun ENV, MOD ou MIX et aucun template vide. L'ancien MOD FX n'est plus exposé. | Conforme |
| Persistence Master | `PARAM_MIX_REVERB_WET`, `SIZE`, `DECAY`, `PRED` sont classés globaux et restaurés par le chemin global avec callbacks DSP. | Conforme |
| LEDs TRACK | Les Play gardent leur rendu. Tout slot Special présent est violet `(128,0,128)`, y compris le Special actif; slots Premium absents éteints en Low-Cost. | Conforme |
| Dispatcher ENV/VCA | ADSR VCA et retrigger sont domaine/set ENV, ressource/backend MIX. Édition, readback, p-lock, macro, clipboard, undo, snapshot, transition et restore convergent vers l'état canonique et les setters mixer. Looper reste exclu. | Conforme |
| Stream pitch | Clavier et séquenceur convergent vers le trigger note commun. Racine 60 explicite, delta `int16_t` signé, notes sous racine descendantes, ratio recalculé au retrigger; timing, sample-rate et shifter composés sans écraser la note. | Conforme |
| Stream lifecycle | VCA ENV visible pour Stream seulement; Note Off gate pose `release_pending`, launch ignore Note Off, la source/reader reste vivante tant que le VCA la demande. EOF, slot absent, changement sample, underrun/panic/stop forcé libèrent par les chemins bornés. | Conforme |
| Multi lifecycle | Note Off ferme le gate accepté puis pose `release_pending`; le renderer garde la source jusqu'à fin de demande VCA. EOF, steal et panic ferment/libèrent la voix; polyphonie et retrigger restent par voix. Aucun second ADSR. | Conforme |
| Canaux audio | Les buffers internes restent L/R et la loi de pan mixer est inchangée. RX et TX inversent chaque paire uniquement dans `board_audio_{lowcost,premium}.c`, à la frontière SAI/PCB; aucune permutation ajoutée aux moteurs ou au mixer. | Conforme |
| Hall | Le filtre ASC multi-échantillons a été retiré du chemin des deux variantes; Note On est publié au franchissement du seuil dans le même cycle. Le seuil local Low-Cost `200U` diverge toutefois du test commité `300U`. | Conforme avec réserve de worktree |

## 3. Tests et builds

Validations passées :

- `track_topology_validation`
- `special_track_role_validation`
- `storage_track_identity_remap_validation`
- `play_special_storage_validation`
- `cfg_track_family_order_validation`
- `led_track_select_validation`
- `vca_dispatcher_validation`
- `env_ownership_validation`
- `stream_sampler_pitch_validation`
- `stream_sampler_vca_lifecycle_validation`
- `multi_sampler_vca_lifecycle_validation`
- `pattern_persistence_classification_validation`
- `track_paste_playback_validation`
- `sequence_track_models_validation`
- `seq_compact_storage_validation`
- `seq_param_compact_contract_validation`
- `seq_play_scheduler_pair_validation`
- `sample_stream_coalesced_lifecycle_validation`
- `sample_stream_deadline_validation`
- `cfg_polyphony_ownership_validation`
- `note_fx_persistence_validation`
- `external_input_ownership_validation`

Validation en échec :

- `hall_lowcost_integration_validation` : contrat statique attendu `300U/400U`, worktree préexistant `200U/400U`. Les autres assertions Hall passent jusqu'à cette garde.

Builds :

- Release Low-Cost : PASS; Flash `1 018 292 B`, RAM_D2 `153 504 B`.
- Release Premium : PASS; Flash `1 010 376 B`, RAM_D2 `168 224 B`.
- Aucun build TestPremium lancé, conformément à la mission.
- `stack_morph` et `synth_voice_budget` n'ont pas été modifiés ni utilisés comme prétexte de correction.

## 4. Micro-correction

Deux gardes strictement identiques `TRACK_TOPOLOGY_ROLE_FX` étaient dupliquées dans les helpers d'édition MacroFX TYPE et de quantification MacroFX. Le doublon mort a été retiré dans `Src/UI/ui_param.c`; aucun comportement, format, DSP ni son ne change.

## 5. Problèmes restants

- La modification locale parallèle du seuil Hall Low-Cost doit être soit confirmée avec son test mis à jour dans son propre chantier, soit ramenée au contrat commité. Elle n'est pas arbitrée ni embarquée ici.
- Les sujets plus larges déjà connus de l'audit VCA (étages d'amplitude internes de certains moteurs et contrat VCA d'External) ne sont pas des régressions de cette vague et restent hors périmètre.
- Aucun index Special historique actif, navigation CFG par ordinal brut, resolver Master/FX dupliqué, MOD FX résiduel, dispatch VCA conditionné au domaine MIX, écrasement Stream `voice->note = 60`, arrêt gate normal immédiat Stream/Multi ou seconde permutation L/R n'a été trouvé dans le périmètre actif.

## 6. Conclusion

La vague peut être conservée comme base stable. Toutes les corrections fonctionnelles demandées sont fermées par le code et les validations ciblées, avec une réserve isolée sur le seuil Hall modifié dans le worktree parallèle. Le seul patch de cette passe est un retrait de garde MacroFX dupliquée sans effet fonctionnel.
