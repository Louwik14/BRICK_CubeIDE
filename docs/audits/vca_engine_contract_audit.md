# Audit du contrat ENV/VCA des moteurs

## Périmètre et verdict

Audit statique de bout en bout sur `HEAD 5a7c9ed63`, sans modification firmware, sans modification d'ID ni de format de stockage. Le périmètre couvre l'UI, les encodeurs, le registre de paramètres, le runtime de piste, le séquenceur/p-lock, la modulation, les snapshots/persistence et le chemin audio.

### Verdict principal

Le symptôme « les encodeurs VCA apparaissent mais ne modifient rien » a une cause commune, indépendante du moteur : la migration de propriété des paramètres VCA vers le domaine `ENV` n'a pas été propagée dans le dispatcher backend.

Dans `Src/Core/track_runtime.c:1705-1712`, `PARAM_VCA_ATTACK`, `PARAM_VCA_DECAY`, `PARAM_VCA_SUSTAIN`, `PARAM_VCA_RELEASE` et `PARAM_ENV_RETRIG_VCA` sont maintenant déclarés `TRACK_RUNTIME_PARAM_DOMAIN_ENV`, avec `TRACK_RUNTIME_RESOURCE_MIX` comme ressource d'exécution. Cette classification est cohérente avec l'UI et les p-locks.

Dans `Src/Param/param_registry_tone_backends.c:65-74`, l'entrée commune n'accepte cependant que les domaines `TONE`, `MIX`, et l'exception `PARAM_ENV_RETRIG_FILTER`. Un paramètre VCA, bien que ressource `MIX`, est donc rejeté à la ligne 73 avant d'atteindre `param_backend_apply_mix_track`. Le dispatcher de la ligne 112 ne branche vers le backend MIX que pour un domaine `MIX` ou `PARAM_ENV_RETRIG_FILTER`.

Conséquence : `param_registry_apply_track_edit()` retourne zéro pour une modification VCA (`Src/Param/param_registry.c:1764-1771`), l'UI abandonne l'écriture (`Src/UI/ui_param.c:1466-1474`), `track_sound_state` ne change pas et aucun setter `mixer_set_track_vca_*()` n'est appelé. La lecture relit donc la valeur canonique inchangée. Le défaut est un `PARAM_NOT_APPLIED` commun à tous les moteurs qui exposent une VCA.

Le contrôle statique `tests/env_ownership_validation.ps1` ne détecte pas cette rupture : il valide les domaines, les slots et les surfaces de persistance, mais pas le retour d'exécution de la chaîne `UI -> param_registry -> backend`.

## Chaîne UI -> paramètre -> audio

```text
BTN_PARAM_1 -> page ENV principale
BTN_PARAM_6 -> page ENV, sous-page VCA
                 |
                 v
ui_page_template_env.c : bank {VCA A, D, S, R}
                 |
                 v
ui_template_page -> ui_param_set_bank -> capture encoder context
                 |
                 v
ui_param_handle_encoder_with_context
                 |
                 v
param_registry_apply_track_edit
                 |
                 v
param_backend_apply_track_value
                 |  [REJET ACTUEL : domaine ENV non admis]
                 X
                 |
                 v
param_backend_apply_mix_track
                 |
        track_sound_state + mixer_set_track_vca_*
                 |
                 v
engine -> mixer submit/poly -> env_adsr VCA -> gain/pan -> sortie
```

La navigation elle-même est correcte : `Src/UI/ui_navigation.c:17-24` mappe `BTN_PARAM_1` et `BTN_PARAM_6` vers ENV, puis `Src/UI/ui_navigation.c:209-213` demande explicitement la sous-page VCA pour BTN6. La disponibilité de BTN6 est volontairement conditionnée par `track_runtime_supports_vca_gate()` (`Src/UI/ui_navigation.c:248-263`).

La banque VCA est correcte dans `Src/UI/pages/ui_page_template_env.c:22-26`, et sa synchronisation runtime conserve les quatre IDs dans `:306-332`. La page générique applique cette banque dans `Src/UI/ui_template_page.c:301-308`, puis l'encodeur choisit `ctx->bank.params[encoder]` dans `Src/UI/ui_param.c:2085-2110`.

Le défaut est donc après la résolution de l'ID, pas dans le bouton, la sous-page, l'ordre A/D/S/R ou le contexte encodeur.

## Contrat des paramètres et ownership

### Déclarations

Les descripteurs sont stables dans `Src/Param/param_registry_catalog.c:189-192` :

| ID | Domaine attendu | Plage | Défaut | Slot ENV |
|---|---|---:|---:|---:|
| `PARAM_VCA_ATTACK` | ENV | 0..127 | 0 | 14 |
| `PARAM_VCA_DECAY` | ENV | 0..127 | 0 | 15 |
| `PARAM_VCA_SUSTAIN` | ENV | 0..127 | 127 | 16 |
| `PARAM_VCA_RELEASE` | ENV | 0..127 | 0 | 17 |
| `PARAM_ENV_RETRIG_VCA` | ENV | bool | 1 | 23 |

Les quatre paramètres VCA ont `apply=NULL` par conception : ils doivent passer par le backend track-aware, comme FILTER/MIX. Le backend MIX contient bien les traitements attendus dans `Src/Param/param_registry_backends.c:1513-1574`, mais ils sont actuellement inatteignables depuis l'entrée commune pour les IDs VCA.

`PARAM_ENV_RETRIG_VCA` est lui aussi traité dans le backend MIX (`Src/Param/param_registry_backends.c:1513-1523`), mais n'est pas admis par l'exception ENV de `param_registry_tone_backends.c`; le défaut couvre donc aussi le retrigger VCA.

### Source canonique

Il n'y a pas de seconde copie canonique concurrente : `track_sound_state_t` contient les champs VCA dans `Inc/Core/track_sound_state.h:62-67`, initialisés par les defaults (`Src/Core/track_sound_state.c:43-48`). La lecture du registre renvoie ces champs (`Src/Param/param_registry.c:409-451`), et la transition documente explicitement que FILTER, MIX et VCA sont autoritaires dans le shadow-state par piste (`Src/Param/param_registry_transition.c:260-264`).

Le défaut actuel est donc une rupture d'application entre l'état canonique et sa projection mixer, pas une duplication de stockage.

### P-locks, modulation, clipboard et undo

- `Src/UI/ui_param.c:1224-1242` et `Src/Seq/seq_param_iface.c:248-255` routent correctement le domaine ENV vers `SEQ_PLOCK_SET_ENV`.
- `Src/Seq/seq_param_iface.c:377-390` valide également la cohérence du set ENV. Le routage p-lock est conforme ; l'application de la valeur échoue ensuite sur la même entrée backend.
- La reconfiguration de lane inclut explicitement les quatre VCA et `PARAM_ENV_RETRIG_VCA` (`Src/Param/param_registry_transition.c:143-190`), mais réutilise `param_registry_apply_track_value`, donc reproduit le rejet.
- Les snapshots capturent et réappliquent les paramètres via le registre (`Src/Core/track_snapshot.c:293-309`). La structure de roundtrip est présente, mais la réapplication live VCA est actuellement silencieusement rejetée.
- Le clipboard lit les valeurs du registre et les réapplique par la même API (`Src/UI/ui_core_clipboard.c:402`, `:543`, `:602`). Copy/query fonctionne statiquement ; clear/paste VCA ne projette pas la valeur au runtime.
- Les destinations de modulation VCA contournent cette entrée et appellent directement le mixer (`Src/Mod/mod_destination_catalog.c:94-98`, `:386-400`). Cela peut faire fonctionner une modulation runtime tout en laissant la base canonique/édition UI inchangée : les deux chemins ne sont pas équivalents.

Les IDs et versions ne sont pas en cause. Les validations confirment les versions Pattern/Project v4 et Patch/Kit v3 ; aucune extension de format n'est requise par cet audit.

## Architecture audio VCA

Le VCA commun est une enveloppe `env_adsr_t` attachée au mixer par piste (`Src/Audio/mixer.c:2144-2262`).

- Les moteurs mono soumettent leur source au mixer ; `Src/Audio/mixer.c:2895-2998` applique le gain VCA avant volume/pan et conserve `vca_env_value`.
- Les moteurs poly Prism/Stack/Wave/DELUGE passent par `mixer_process_external_poly_voice()` (`Src/Audio/mixer.c:2553-2577`) : le VCA est appliqué une fois par voix avant sommation.
- Le chemin documenté dans le code est `engine -> filter -> VCA/volume/pan -> inserts/sends` (`Src/Audio/mixer.c:3012-3015`).
- Le dispatch audio confirme les deux formes : mono via `mixer_submit_external_*`, poly via `mixer_process_external_poly_voice()` (`Src/Core/brick6_audio_runtime.c:170-474`).
- Wave, DELUGE et Stack demandent au renderer de maintenir la source pendant un tail si `mixer_track_vca_requires_source()` est actif (`Src/Core/brick6_audio_runtime.c:286-289`, `:364-367`, `:442-463`).

Le modèle architectural cible est donc un VCA commun post-source, avec quelques contrats de durée de vie internes par moteur. Les écarts ci-dessous doivent être distingués d'un simple défaut de routage de paramètre.

## Cycle Note On / Note Off

Le scheduler pilote le gate VCA pour les pistes non-poly qui déclarent `supports_vca_gate`, et pilote le VCA par voix pour les synthés poly (`Src/Seq/seq_play_scheduler.c:518-565`). Le clavier reprend la même règle (`Src/Keyboard/keyboard_engine.c:275-286`).

La capacité est explicitement refusée pour Sampler Stream et Looper dans `Src/Core/track_runtime.c:315-325` et `:711-721`. Les paramètres VCA sont également bloqués pour ces types dans `:2208-2215`. Cette exclusion est cohérente avec l'absence de gate vocal dans ces lecteurs ; elle explique la VCA non visible, mais ne corrige pas le défaut commun des autres moteurs.

## Matrice par moteur et type

`Oui*` dans la colonne encodeur signifie « banque correctement résolue, mais écriture actuellement rejetée par le dispatcher commun ». La colonne persistance décrit le contrat de données déclaré/static, pas une promesse que la projection audio live réussit malgré le défaut d'application.

| Moteur / type | ENV/VCA visible | Encodeurs éditent l'état | Gate déclenché | Note Off / release | Source ADSR | Gain VCA appliqué où | Persistance | Verdict |
|---|---|---|---|---|---|---|---|---|
| Prism mono | Oui | Non, `PARAM_NOT_APPLIED` | Mixer VCA + gate runtime | Mixer release ; Braids garde aussi un tail | `track_sound_state` -> mixer ; Braids a aussi un ramp de niveau interne | Mixer mono | Oui, statique | `DOUBLE_VCA` + `PARAM_NOT_APPLIED` |
| Prism poly | Oui | Non, `PARAM_NOT_APPLIED` | Mixer VCA par voix | `mixer_track_poly_note_off()` par voix | Même distinction ; source Braids possède son propre niveau/tail | Mixer par voix avant sommation | Oui, statique | `DOUBLE_VCA` + `PARAM_NOT_APPLIED` |
| Wave mono/poly | Oui | Non, `PARAM_NOT_APPLIED` | Mixer VCA ; gate source maintenu pendant tail | Gate source à 0, VCA mixer porte le tail | Pas d'ADSR d'amplitude interne identifié ; gate/source lifecycle | Mixer mono ou par voix | Oui, statique | `PARAM_NOT_APPLIED` |
| Stack mono/poly | Oui | Non, `PARAM_NOT_APPLIED` | Mixer VCA ; source maintenue si tail requis | Source `release_source_active`, VCA mixer | Pas d'ADSR d'amplitude interne identifié | Mixer mono ou par voix | Oui, statique | `PARAM_NOT_APPLIED` |
| DELUGE mono/poly | Oui | Non, `PARAM_NOT_APPLIED` | Mixer VCA ; renderer maintenu pendant release | Gate runtime à 0, source maintenue si VCA demande une source | Niveau interne = paramètre de niveau, pas ADSR de note | Mixer mono ou par voix | Oui, statique | `PARAM_NOT_APPLIED` |
| Drum MD / TRX-BD | Oui | Non, `PARAM_NOT_APPLIED` | Mixer VCA sur trigger | `note_off` drum est sans effet ; shot interne se termine seul | ADSR amplitude interne MD (`md_trx_env`) + VCA mixer | Mixer post-source | Oui, statique | `DOUBLE_VCA` + `PARAM_NOT_APPLIED` |
| Drum BD analog | Oui | Non, `PARAM_NOT_APPLIED` | Mixer VCA sur trigger | `note_off` drum est sans effet ; enveloppe interne Plaits/shot | Enveloppe interne du moteur + VCA mixer | Mixer post-source | Oui, statique | `DOUBLE_VCA` + `PARAM_NOT_APPLIED` |
| Sampler RAM / mélodique | Oui | Non, `PARAM_NOT_APPLIED` | Mixer VCA | Note Off laisse la source active jusqu'à la fin du VCA | Pas d'ADSR source identifié ; mixer VCA owns release | Mixer stereo final | Oui, statique | `PARAM_NOT_APPLIED` |
| Sampler Multi | Oui | Non, `PARAM_NOT_APPLIED` | Mixer VCA | `note_off_multi` pose `release_pending`, puis le renderer arrête la voix avant le prochain bloc (`brick6_sampler_runtime.c:4134+`) ; release VCA non audible | Pas d'ADSR source ; arrêt de voix spécifique Multi | Mixer final si source présente | Oui, statique | `ENGINE_SPECIFIC_CONTRACT` + `PARAM_NOT_APPLIED` |
| Sampler Stream | Non, bloqué | Non | Non | Note Off arrête le clip / declick | Aucun VCA vocal | VCA mixer désactivé/bypassé | Champs génériques présents, usage non applicable | `UI_BLOCKED` / `VCA_BYPASSED` |
| Sampler Looper | Non, bloqué | Non | Non | Transport/clip, pas un gate de voix | Aucun VCA vocal | VCA mixer désactivé/bypassé | Champs génériques présents, usage non applicable | `NOT_APPLICABLE` / `VCA_BYPASSED` |
| External | Oui actuellement | Non, `PARAM_NOT_APPLIED` | Le runtime autorise actuellement le gate VCA | Mixer release si une note de contrôle est reçue | Aucun générateur interne ; audio physique externe | Mixer sur l'entrée externe | Oui, statique | `ENGINE_SPECIFIC_CONTRACT` : risque de VCA artificiel |
| MIDI | Non | Non | Pas d'audio local | MIDI Note Off seulement | Aucun | Aucun | Paramètres MIDI dédiés | `NOT_APPLICABLE` |
| Input spécial | ENV/filter possible, VCA non | Non | Non | Monitoring/input ownership | Aucun générateur interne | Pas de VCA vocal | Paramètres d'entrée | `NOT_APPLICABLE` |
| Off / Master / FX spécial | Non | Non | Non | Aucun | Aucun | Aucun VCA vocal | CFG/TONE selon rôle | `NOT_APPLICABLE` |

### Lecture des écarts de la matrice

1. **Prism et Drum** : le VCA mixer n'est pas le seul traitement d'amplitude. Prism rampe `instance->level` et configure un tail à partir de `PARAM_VCA_RELEASE` (`Src/Core/brick6_braids_runtime.cpp:441-464`, `:516-523`, `:650-681`) ; MD TRX-BD traite une `amplitude_env` interne (`Src/Audio/drum_synth.cpp:155-163`, `:190-221`). Le même geste de release traverse donc deux étages d'amplitude. Ce n'est pas un problème d'UI ; c'est un `DOUBLE_VCA` à arbitrer dans le contrat produit.
2. **Wave, Stack et DELUGE** : le moteur interne gère principalement gate, source active et tail de production ; l'amplitude de note reste dans le VCA mixer. Ces moteurs sont architecturalement compatibles avec un VCA commun, sous réserve de réparer l'application des paramètres.
3. **Sampler RAM** : le runtime attend que le VCA mixer porte la fin de note ; la voix RAM est nettoyée quand `mixer_track_vca_is_running()` tombe à zéro (`Src/Core/brick6_sampler_runtime.c:5752-5787`). C'est le cas sampler le plus conforme au contrat commun.
4. **Sampler Multi** : `release_pending` déclenche l'arrêt de la source dans `brick6_sampler_render_multi()` avant que le tail du VCA mixer puisse être audible. C'est un contrat moteur distinct à documenter ou à corriger séparément ; le défaut d'édition VCA reste toutefois le défaut commun décrit plus haut.
5. **Stream / Looper** : l'absence de VCA est intentionnelle dans le runtime (`track_runtime_supports_vca_gate == 0`), et non une panne d'encodeur. Leur statut est `UI_BLOCKED`/`VCA_BYPASSED`, avec `NOT_APPLICABLE` pour le Looper de transport.
6. **External** : le resolver mappe l'External vers `TRACK_RUNTIME_ENGINE_AUDIO_TRACK` (`Src/Core/track_runtime.c:829-833`) mais `track_runtime_supports_vca_gate()` retourne aussi vrai pour la famille External (`:733-735`). Au regard du contrat « pas de VCA artificiel sans génération audio interne », ce point est une incohérence de contrat à trancher ; il ne faut pas la confondre avec les moteurs synthétiques.

## Validations statiques exécutées

Toutes les validations ci-dessous ont retourné `PASS` sur le snapshot audité :

- `env_ownership_validation.ps1` — ENV 25/256, MIX 4, MOD 12, Pattern/Project v4, Kit v3.
- `param_reserved_slots_validation.ps1`.
- `pattern_persistence_classification_validation.ps1`.
- `track_topology_validation.ps1`.
- `cfg_track_family_order_validation.ps1`.
- `cfg_polyphony_ownership_validation.ps1`.
- `sequence_track_models_validation.ps1`.
- `special_track_role_validation.ps1`.
- `external_input_ownership_validation.ps1`.
- `engine_output_gain_validation.ps1`.
- `md_dsp_validation.ps1`.
- `md_trx_bd_validation.ps1`.
- `track_paste_playback_validation.ps1`.

Aucun build n'a été lancé : le périmètre demandé était un audit statique et aucun patch firmware n'est inclus.

## Conclusion et frontière de correction

Le défaut bloquant à traiter en priorité est unique et localisé : réaligner l'entrée commune `param_backend_apply_track_value()` avec l'ownership ENV des paramètres VCA, tout en conservant `TRACK_RUNTIME_RESOURCE_MIX` comme ressource d'exécution. Cette correction devra ensuite être vérifiée sur : encodeur, query/readback, `PARAM_ENV_RETRIG_VCA`, p-lock ENV, clipboard clear/paste, transition de lane, snapshot/restore et modulation.

Les sujets suivants sont séparés et ne doivent pas être masqués par ce micro-routage :

- double étage d'amplitude Prism et Drum (`DOUBLE_VCA`) ;
- arrêt immédiat de la source Sampler Multi (`ENGINE_SPECIFIC_CONTRACT`) ;
- exclusion Stream/Looper (`UI_BLOCKED` / `VCA_BYPASSED`) ;
- VCA actuellement autorisée sur External sans générateur interne (`ENGINE_SPECIFIC_CONTRACT`).

Cet audit ne modifie aucun de ces comportements et ne modifie ni firmware, ni IDs, ni formats persistés.
