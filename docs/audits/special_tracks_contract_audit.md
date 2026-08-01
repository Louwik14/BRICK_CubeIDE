# Audit du contrat des tracks Special

Date de l’audit : 2026-08-01  
Périmètre : contrat produit/UI/runtime/storage uniquement. Aucun firmware, DSP, dual-core, format ou version de fichier n’est modifié par cet audit.

## 1. Verdict

Le contrat cible doit être : huit tracks Play, puis `Input1`, `Looper`, `FX`, `Master`; en Premium seulement, `Input2` et `Input3` suivent en positions 13 et 14. Le dépôt actuel place encore `Master` en position 9 et `FX` en dernière position Special. La source d’autorité à corriger est donc la table de `Src/Core/track_topology.c` et ses constantes de `Inc/Core/track_topology.h`.

Les identités fonctionnelles sont déjà exprimées correctement par `role + ordinal` : `Input1 = (INPUT,0)`, `Input2 = (INPUT,1)`, `Input3 = (INPUT,2)`, `Looper = (LOOPER,0)`, `FX = (FX,0)`, `Master = (MASTER,0)`. Le changement demandé est principalement une réindexation physique des slots, pas une nouvelle famille configurable.

Le bleu des tracks sélectionnables ne vient ni du rôle ni d’une capacité topology. Il vient de `Drivers/Drv_app/Src/led_rgb.c:led_apply_track_select_hall_scene()`, qui teste `ui_get_track_family(hall) != UI_TRACK_FAMILY_OFF` et applique `LED_FIXED_DARK_BLUE_*`; le track actif est ensuite forcé en blanc. En conséquence, `Looper` et les Inputs sont bleus, tandis que `Master` et `FX`, configurés UI en `Off/Audio`, sont noirs. Aucun violet n’est défini dans cette scène. Le correctif cohérent doit tester `track_topology_is_special()` ou le rôle, conserver le rendu Play actuel et appliquer un violet à tous les Specials, y compris au Special actif.

`Master` et `FX` sont bien deux surfaces distinctes et possèdent déjà une implémentation réelle de TONE : Master porte les effets globaux reverb/delay/compressor; FX porte quatre MacroFX. Elles sont toutefois bloquées par la porte de navigation commune qui résout encore la famille UI brute `Off/Audio`, et par le même résolveur générique utilisé par le clipboard d’ensemble. Le contrat Master TONE a en plus une lacune de persistence : `PARAM_MIX_REVERB_WET`, `SIZE`, `DECAY` et `PRED` sont exposés et appliqués, mais absents de la classification globale de `pattern_live`. ENV/MIX ne sont pas des surfaces Special manquantes : elles sont explicitement hors contrat actuel. MOD sur FX est une incohérence résiduelle de masque, sans template ni chemin paramètre fonctionnel.

Le déplacement des slots n’est pas rétrocompatible avec les sauvegardes actuelles par simple présence d’un champ identité : Pattern/Project et Kit valident l’identité à l’index brut puis restaurent plusieurs tableaux par index brut. Il faut donc prévoir une normalisation mémoire par identité lors du futur patch, sans changer les structures ni les versions, et tester cette migration avant d’accepter le changement de topology.

## 2. Topology cible et écart constaté

Les numéros ci-dessous sont 1-based côté produit; les indices sont 0-based côté C.

### LowCost

| Numéro | Indice | Contrat cible | Contrat actuel | Capacités topology actuelles |
|---:|---:|---|---|---|
| 1–8 | 0–7 | Play 1–8 | Play 1–8 | NOTES, AUDIO, MIDI, KEYBOARD, MIDI_FX, AUTOMATION, MUTE, INPUT_RESERVATION |
| 9 | 8 | Input1 | Master | AUDIO, AUTOMATION, MUTE |
| 10 | 9 | Looper | Looper | AUDIO, AUTOMATION, MUTE |
| 11 | 10 | FX | Input1 | AUDIO, AUTOMATION, MUTE dans la topology; MUTE est ensuite refusé par la projection runtime Special |
| 12 | 11 | Master | FX | AUDIO, AUTOMATION; pas de MUTE |

### Premium

| Numéro | Indice | Contrat cible | Contrat actuel | Identité |
---:|---:|---|---|---|
| 1–8 | 0–7 | Play 1–8 | Play 1–8 | `PLAY, ordinal 0..7` |
| 9 | 8 | Input1 | Master | `INPUT, ordinal 0` |
| 10 | 9 | Looper | Looper | `LOOPER, ordinal 0` |
| 11 | 10 | FX | Input1 | `FX, ordinal 0` |
| 12 | 11 | Master | Input2 | `MASTER, ordinal 0` |
| 13 | 12 | Input2 | Input3 | `INPUT, ordinal 1` |
| 14 | 13 | Input3 | FX | `INPUT, ordinal 2` |

Les capacités doivent rester attachées au rôle, pas à la position : Input/Looper/FX gardent le profil audio spécial; Master garde le profil audio/automation sans mute ordinaire. La fonction runtime `track_runtime_has_capability()` neutralise déjà MUTE pour Master et FX, ce qui doit rester explicite dans le contrat final. `track_state_topology_config()` doit continuer à forcer les Inputs sur `InputN/Audio`, Looper sur `Sampler/Looper`, et Master/FX sur une identité fixe non configurable.

La séquence Special reste une séquence d’actions, sans notes PLAY, avec 16 locks par step et sans quota d’instrument. Le déplacement physique ne doit pas transformer ces identités en familles sélectionnables.

## 3. Ordre CFG et persistance

L’ordre produit Play cible est :

`Off → Synth → Drum → MIDI → External → Sampler`

Les familles `Input1/2/3` existent comme valeurs UI internes pour les Specials fixes, mais ne doivent pas devenir des choix de famille Play. `ui_track_catalog_family_is_available()` les refuse pour un Play et ne les accepte pour un Special que si elles correspondent à sa configuration topology.

Le code actuel mélange trois notions :

1. la valeur numérique persistée de `ui_track_family_t` (`Off=0`, `Input1=1`, `Input2=2`, `Input3=3`, `Synth=4`, `Drum=5`, `MIDI=6`, `Sampler=7`, `External=8`);
2. le tableau de labels `g_track_family_labels[]`;
3. le parcours encodeur `ui_param_step_cfg_track()`, qui incrémente directement l’enum et saute les familles indisponibles.

La liste actuellement visible pour un Play est donc `Off, Synth, Drum, MIDI, Sampler, External`. Réordonner l’enum ou les valeurs brutes pour obtenir External avant Sampler modifierait les valeurs stockées dans `PatternSaveV1.track_cfg.family`, les Kits, les snapshots et les structures de configuration. Ce n’est pas acceptable sans preuve de migration complète.

La recommandation est de conserver les valeurs numériques et d’introduire un catalogue d’ordre CFG indépendant : ordre d’affichage/encodeur `Off, Synth, Drum, MIDI, External, Sampler`, conversion explicite affichage ↔ enum persistant, labels résolus par valeur enum. Aucun ancien Special configurable ne doit être ajouté à ce catalogue.

## 4. Matrice Master / FX

Légende : `IMPLEMENTED_BUT_BLOCKED` signifie qu’un chemin réel existe mais qu’une garde UI empêche son accès; `PARTIALLY_IMPLEMENTED` signifie que la surface existe mais que le contrat transversal n’est pas fermé; `INTENTIONALLY_NOT_APPLICABLE` signifie qu’aucun contenu Special n’est défini; `LEGACY_RELIQUARY` signifie qu’un reste de code donne une apparence de capacité sans surface fonctionnelle.

### Master

| Ensemble | Capacité/garde actuelle | Template UI | Paramètres / état canonique | Apply / runtime | Persistence | Clipboard / undo | Verdict |
|---|---|---|---|---|---|---|---|
| CFG | Toujours disponible. Identité fixe; `ui_set_track_family()` refuse un Special. | Template CFG Special avec identité/description fixe. | Pas de famille Special configurable. Config UI brute `Off/Audio`; rôle Master vient de la topology. | `track_runtime_prepare_ctx_base()` force `SPECIAL_MASTER/SPECIAL_MASTER`, bind sans moteur. | `track_cfg.identity`, famille/type et métadonnées sont persistés par slot. | Snapshot complet conserve l’identité; undo structurel passe par Pattern. | `INTENTIONALLY_NOT_APPLICABLE` pour une mutation de famille; CFG reste disponible comme surface descriptive. |
| ENV | Absent du masque : le calcul runtime retourne après CFG/SEQ/TONE pour Master. | Aucun template Master ENV; les templates ENV ordinaires sont liés aux familles Play. | Les paramètres ENV sont des paramètres de pistes filtrables/sonores, pas un état Master. | `track_runtime_get_effective_param_status()` bloque faute de `CAN_FILTER`/cible audio Special. | Aucun domaine ENV Master. | Aucun clipboard d’ensemble utile; un delta qui viserait ces IDs est refusé à l’apply. | `INTENTIONALLY_NOT_APPLICABLE` |
| TONE | Présent dans le masque, mais la navigation vérifie aussi `ui_template_family_resolve_active_track(TONE)`, qui voit `Off/Audio` et renvoie NULL. | Template réel dans `ui_page_template_tone_resolve_family()` : reverb 1/3, delay 2/3, compressor 3/3. | Paramètres globaux `PARAM_MIX_REVERB_*`, `PARAM_MIX_DELAY_*`, `PARAM_COMP_*` et `PARAM_BUS_COMP_*`; canonique global via `param_get/param_set`, pas dans un état track. | Descripteurs et callbacks registry appliquent les DSP globaux; `track_runtime` autorise les globaux. | La majorité est classée GLOBAL et sauvegardée dans `globals.global_values`, mais `PARAM_MIX_REVERB_WET/SIZE/DECAY/PRED` sont oubliés par `pattern_live_classify_param()` : l’état UI/runtime existe, le round-trip Pattern/Project n’est donc pas fermé. | Le clipboard ensemble échoue avant collecte car il utilise le résolveur générique; undo delta global utilise `param_set`, snapshot Pattern couvre les globals déjà classés. | `PARTIALLY_IMPLEMENTED` |
| MOD | Absent du masque Master. | Aucun template Master MOD. | Aucun état MOD Master contractuel; les modules MOD refusent les familles Special. | Paramètres MOD bloqués par ressource PLAY/CAN_PLAY. | Aucun domaine MOD Master. | Non applicable. | `INTENTIONALLY_NOT_APPLICABLE` |
| MIX | Absent du masque Master. Les effets globaux ne sont pas un MIX par piste. | Le résolveur MIX exige une cible mix valide; Master n’en a pas. | `PARAM_MIX_LEVEL/PAN/SEND/MUTE` sont des paramètres de piste; reverb/delay/compressor restent globaux TONE. | `track_runtime_get_effective_param_status()` bloque explicitement les familles Special Master/FX pour MIX. | Aucun MIX Master par piste; globals persistés séparément. | Non applicable. | `INTENTIONALLY_NOT_APPLICABLE` |

### FX

| Ensemble | Capacité/garde actuelle | Template UI | Paramètres / état canonique | Apply / runtime | Persistence | Clipboard / undo | Verdict |
|---|---|---|---|---|---|---|---|
| CFG | Toujours disponible. Identité FX fixe; pas de conversion en famille Play. | Template CFG Special. | Config UI brute `Off/Audio`; runtime force `SPECIAL_FX/SPECIAL_FX`. | Bind sans moteur; rôle FX est la garde canonique. | Identité FX et configuration slot persistées. | Snapshot complet valide `role+ordinal`; undo structurel via Pattern. | `INTENTIONALLY_NOT_APPLICABLE` pour une mutation de famille. |
| ENV | Absent du masque. | Aucun template ENV FX. | Aucun état ENV FX; filtres/enveloppes sont associés aux pistes routables. | Bloqué faute de cible filtre/audio et de capacité runtime. | Aucun domaine ENV FX. | Non applicable. | `INTENTIONALLY_NOT_APPLICABLE` |
| TONE | Présent dans le masque, mais la garde de navigation résout `Off/Audio` et renvoie NULL. | Template réel `g_ui_template_tone_family_macro_fx`, quatre sous-pages FX1..FX4. | 16 paramètres `PARAM_MACRO_FX1..4_{TYPE,LEVEL,A,B}`; état canonique `track_tone_sound_state_t.macro_fx[4]`. | `track_runtime_get_effective_param_status()` autorise précisément le rôle/type FX; `param_registry_tone_backends.c` normalise les types; `param_backend_apply_macro_fx_track()` et `fx_master_macro` appliquent le runtime. | Paramètres track-aware dans `sound.track_values`; état tone inclus dans Track Snapshot/Kit; Pattern/Project restent aux formats courants. | Clipboard ensemble bloqué par le résolveur générique; clipboard Track et snapshot complet transportent `tone.macro_fx`; undo delta track-aware appelle `param_registry_apply_track_value()`. | `IMPLEMENTED_BUT_BLOCKED` |
| MOD | Le masque runtime ajoute encore MOD pour FX, mais la navigation échoue sur le résolveur générique `Off/Audio`. Aucun override Special MOD n’existe. | Aucun template MOD associé à `SPECIAL_FX`; le résolveur MOD appelle le registre générique. | Les IDs MOD sont classés ressource PLAY/MOD et aucune capacité CAN_PLAY n’est donnée à FX; pas d’état FX MOD produit. | `track_runtime_get_effective_param_status()` bloque les paramètres MOD pour FX; aucun backend Special MOD. | Aucun domaine MOD FX. | Le clipboard renvoie `ENS N/A`; undo n’a pas de surface applicable. | `LEGACY_RELIQUARY` |
| MIX | Absent du masque. | Le résolveur MIX renvoie la famille indisponible si `has_mix_target == 0`; FX n’a pas de cible MIX par piste. | Aucun MIX FX; ne pas déplacer les globals Master vers FX. | Bloqué explicitement pour `SPECIAL_FX`. | Aucun domaine MIX FX. | Non applicable. | `INTENTIONALLY_NOT_APPLICABLE` |

Conclusion fonctionnelle : TONE Master et TONE FX sont des surfaces implémentées mais inaccessibles par la navigation normale; elles doivent être ouvertes par une résolution de rôle contrôlée. Master TONE nécessite en plus de fermer la persistence des quatre paramètres reverb mutables oubliés. Il ne faut pas activer ENV/MIX pour donner l’illusion d’une surface complète. MOD FX doit être supprimé du masque ou recevoir un vrai contrat produit complet; la recommandation est de le supprimer tant qu’aucun contenu, état et backend dédiés ne sont définis.

## 5. Causes exactes et propriétaire du futur micro-patch

| Symptôme | Cause précise | Propriétaire | Micro-patch cohérent à planifier |
|---|---|---|---|
| Master/FX restent dans les mauvais slots | `TRACK_TOPOLOGY_MASTER_TRACK_INDEX`, `TRACK_TOPOLOGY_FX_TRACK_INDEX` et `g_track_topology[]` codent `Master, Looper, Input(s), FX`. | `Inc/Core/track_topology.h`, `Src/Core/track_topology.c`. | Réécrire l’ordre LowCost/Premium vers Input1, Looper, FX, Master, Input2, Input3; conserver les identités rôle/ordinal et les cardinalités. |
| Config Special encore `Off/Audio` | `track_state_topology_config()` laisse volontairement Master/FX sur le défaut; le runtime corrige ensuite seulement sa projection. | `Src/Core/track_state.c`, `Src/Core/track_runtime.c`. | Ne pas transformer Master/FX en familles CFG; ajouter si nécessaire un accessor UI de rôle/template Special afin que les consommateurs n’interprètent plus `Off/Audio` comme une absence de surface. |
| TONE est inaccessible | `ui_navigation_is_page_available()` exige masque runtime puis `ui_template_family_resolve_active_track()`; ce dernier résout la config UI brute. Le resolver de page `ui_page_template_tone_resolve_family()` contient pourtant les branches Master/FX, mais elles arrivent trop tard. | `Src/UI/ui_navigation.c`, `Src/UI/pages/ui_page_template_tone.c`, `Src/UI/ui_template_page.c`. | Ajouter une résolution d’accessibilité role-aware partagée par navigation et page, ou faire consulter le resolver effectif de la page sans créer de famille configurable. Vérifier bind, rôle et template avant d’ouvrir. |
| Clipboard d’ensemble Master/FX renvoie N/A | `ui_core_clipboard_collect_ensemble_params()` appelle directement `ui_template_family_resolve()` avec `config.family/type`; aucun override de rôle pour TONE Special. | `Src/UI/ui_core_clipboard.c`. | Réutiliser le même résolveur effectif role-aware que la navigation; ne pas copier les globals Master comme s’ils étaient track-aware, mais les traiter comme scope global. |
| FX MOD paraît disponible sans contenu | `track_runtime_compute_ui_ensemble_mask()` ajoute MOD pour tout rôle FX; le resolver MOD reste générique et les paramètres MOD sont bloqués par ressource PLAY/CAN_PLAY. | `Src/Core/track_runtime.c`, `Src/UI/pages/ui_page_template_mod.c`. | Retirer MOD du masque FX et ajouter une assertion/test “aucun bouton MOD”; ne pas créer de page vide. |
| Quatre contrôles reverb ne round-trippent pas | `pattern_live_classify_param()` ne contient pas `PARAM_MIX_REVERB_WET`, `PARAM_MIX_REVERB_SIZE`, `PARAM_MIX_REVERB_DECAY` ni `PARAM_MIX_REVERB_PRED`; ils tombent dans `NOT_RELEVANT`, malgré un template et des callbacks globaux. | `Src/Storage/pattern_live_ram.c`, à confirmer avec la matrice Z3 après correction. | Ajouter ces IDs à la classe GLOBAL, conserver `param_set`/`globals.global_values`, puis ajouter un round-trip Pattern/Project explicite. |
| Specials bleus/noirs/blancs | `led_apply_track_select_hall_scene()` choisit dark blue selon `ui_get_track_family()!=OFF`, puis blanc pour le track actif; la scène ne consulte pas topology et ne définit pas de violet. | `Drivers/Drv_app/Src/led_rgb.c`. | Définir une palette violet dédiée; tester `track_topology_is_special(hall)` avant le chemin famille, conserver le rendu Play, et garder le Special actif violet. |
| CFG Sampler/External dans le mauvais ordre | L’encodeur parcourt directement les valeurs enum et les labels indexés; valeurs persistées et affichage sont confondus. | `Src/UI/ui_param.c`, `Src/UI/ui_track_catalog.c`, `Src/Param/param_registry_catalog.c`. | Ajouter un ordre d’affichage indépendant et des conversions; ne pas renuméroter `ui_track_family_t` ni les valeurs Pattern/Kit. |
| Anciennes sauvegardes rejetées après réindexation | `pattern_live_apply_snapshot()` et `pattern_live_seq_block_validate_plock_budget()` comparent identité au même index; ensuite config, sound, mix, sequence et matrices sont copiés par index. Kit valide `topology_role/ordinal` mais applique `kit->tracks[track]` par index. Project garde en plus `multi[track]` et les macro locks avec `track` brut. | `Src/Storage/pattern_live_ram.c`, `Src/Storage/kit_v1.c`, `Src/Storage/project_v1.c`. | Avant validation/apply, normaliser en mémoire les tableaux par `track_topology_identity_t`; remapper aussi `seq.special`, sound/mix, routes, globals track-indexés, note-FX, Kit payloads, Project multi et macro locks. Ne changer aucun format/version tant que cette migration reste dans les structures existantes. |

## 6. Plan d’implémentation recommandé

1. **Topology et identité.** Modifier uniquement les constantes/tableaux topology; ajouter des tests d’ordre pour LowCost et Premium. Vérifier que tous les consommateurs utilisent `track_topology_is_role()`, `track_topology_is_special()` ou `role+ordinal`, et non “FX = dernier slot”.

2. **Migration en mémoire.** Introduire une routine privée de remapping par identité pour Pattern/Project/Kit avant leurs validations actuelles. La routine doit être bijective pour les tracks actives, laisser les slots inutilisés LowCost hors payload logique et ne pas toucher aux formats Pattern v4, Project v4, Kit v3 ou Patch v3.

3. **CFG.** Décorréler ordre d’affichage et valeur enum persistée. Tester un cycle encodeur complet pour chaque famille Play, en vérifiant que les Specials gardent leur identité et que `Sampler` reste la dernière famille affichée.

4. **Navigation/TONE.** Créer un seul point de résolution “ensemble effectif du track” consommé par disponibilité, rendu de page et clipboard. Autoriser uniquement CFG/SEQ/TONE pour Master et CFG/SEQ/TONE pour FX; laisser MOD absent après suppression du reste de masque. Ne pas ajouter d’ENV/MIX vides.

5. **Paramètres et persistence.** Ne pas déplacer les paramètres globaux Master vers FX. Conserver Master sur `param_set`/global Pattern et FX sur `track_tone_sound_state.macro_fx`/backend MacroFX. Ajouter les quatre IDs reverb oubliés à la classification GLOBAL, puis tester le round-trip et le statut/apply de chaque ID TONE.

6. **LED.** Ajouter la couleur violet au propriétaire LED; la classification doit être topology/role-based. Play garde son dark-blue/white actuel; tous les Specials sont violet, actif compris.

7. **Clipboard, undo et documentation.** Rendre le clipboard d’ensemble role-aware. Vérifier que les globals Master restent dans le scope global, que les MacroFX FX restent track-aware, et que les snapshots/undo restent applicables après la migration. Mettre à jour les tests et les documents d’architecture seulement après validation du code.

## 7. Tests d’acceptation

### Tests statiques et ciblés

- Mettre à jour `Tests/track_topology_validation.ps1` pour les deux ordres cibles et vérifier LowCost `8+4`, Premium `8+6`, `Master = index 11`, `FX = index 10`, `Input1 = index 8`, puis Premium `Input2 = 12`, `Input3 = 13`.
- Étendre `Tests/special_track_role_validation.ps1` aux masques CFG/TONE/ENV/MOD/MIX et à l’absence de conversion Special en famille Play.
- Conserver `Tests/play_special_storage_validation.ps1` et `Tests/sequence_track_models_validation.ps1`; ajouter une assertion de migration `role+ordinal` pour chaque Special.
- Ajouter un test CFG qui vérifie `Off, Synth, Drum, MIDI, External, Sampler` sans modification des valeurs numériques persistées.
- Ajouter un test UI/navigation qui prouve : Master TONE ouvre reverb/delay/compressor; FX TONE ouvre FX1..FX4; Master/FX ENV/MOD/MIX ne sont pas présentés; FX MOD ne se présente pas.
- Ajouter un test LED de scène TRACK maintenue : Play inchangé, Input/Looper/FX/Master violets, Special actif non blanc.
- Ajouter des tests clipboard : TONE Master global, TONE FX MacroFX, clipboard Track par identité, et rejet des rôles incompatibles.
- Ajouter des fixtures Pattern/Project/Kit capturées avec l’ordre actuel puis chargées avec l’ordre cible; vérifier conservation des actions Special, MacroFX, globals Master, routes Looper, note-FX et macro locks.

### Builds et validations de sortie

- Exécuter les validations PowerShell ciblées en LowCost et Premium.
- Construire les deux configurations sans artefact de source : `make -C Debug all -j4` puis `make -C Release all -j4`.
- Vérifier explicitement que les versions restent Pattern v4, Project v4, Kit v3 et Patch v3, et que la taille/les offsets des structures persistées n’ont pas changé.
- Exécuter les tests de persistance et de clipboard après build Release, puis un test de non-régression des pistes Play et du rendu LED.

## Références d’autorité inspectées

- Topology : `Inc/Core/track_topology.h`, `Src/Core/track_topology.c`.
- Projection runtime : `Inc/Core/track_runtime.h`, `Src/Core/track_runtime.c`.
- État/configuration : `Src/Core/track_state.c`.
- Navigation/templates : `Src/UI/ui_navigation.c`, `Src/UI/ui_template_page.c`, `Src/UI/pages/ui_page_template_tone.c`, `Src/UI/pages/ui_page_template_mod.c`, `Src/UI/pages/ui_page_template_mix.c`, `Src/UI/pages/ui_page_template_env.c`.
- Paramètres et backends : `Src/Param/param_registry.c`, `Src/Param/param_registry_tone_backends.c`, `Src/Param/param_registry_backends.c`, `Inc/Core/track_tone_sound_state.h`.
- LED/interaction : `Drivers/Drv_app/Src/led_rgb.c`, `Src/UI/ui_core.c`, `Src/UI/ui_hall_mode_flow.c`.
- Stockage/copie : `Inc/Storage/pattern_live_ram.h`, `Src/Storage/pattern_live_ram.c`, `Inc/Storage/project_v1.h`, `Src/Storage/project_v1.c`, `Inc/Storage/kit_v1.h`, `Src/Storage/kit_v1.c`, `Src/Core/track_snapshot.c`, `Src/UI/ui_core_clipboard.c`, `Src/Storage/undo_v2.c`.
- Architecture : `docs/architecture/z2_track_runtime_authority.md`, `docs/architecture/z3_param_modulation_control.md`, `docs/architecture/z5_ui_navigation_interaction.md`, `docs/audits/repo_spring_clean_verification.md`.
