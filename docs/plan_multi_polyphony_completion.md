# Plan correctif — Multi Sampler polyphonique

## 1. Verdict du contre-audit

Le cœur audio est valide : pool Multi global de 8 voix, limite par piste, slots DSP indépendants, rendu par voix, spread avant sommation, bypass du filtre/VCA de piste, mono/stéréo et scheduler tokenisé fonctionnent.

Le chantier reste toutefois partiellement implémenté sur la chaîne de configuration :

- `VOICES` et `SPREAD` sont absents du template CFG Multi ;
- les capability/status runtime les classent comme ressources synthétiques ;
- les bornes UI de `VOICES` dépendent encore du pool synth ;
- Pattern live RAM, Snapshot, Patch et Kit ne restaurent pas tous l’état Multi ;
- `SPREAD` n’est pas aligné avec `VOICES` dans Undo/Redo ;
- l’API Note Off legacy peut fermer plusieurs occurrences identiques lorsqu’elle est appelée sans token.

Cette passe ne corrige rien. Chaque étape ci-dessous est isolable, doit produire un commit local dédié, puis s’arrêter.

## 2. Architecture actuelle valide à préserver

Les éléments suivants sont hors chantier correctif et ne doivent pas être refondus :

- `BRICK6_SAMPLER_MULTI_MAX_VOICES = 8` et pool global `g_sampler_multi_voice` ;
- `SAMPLER_MULTI_MAX_VOICES_PER_TRACK` et `SAMPLER_MULTI_MAX_GLOBAL_VOICES` ;
- `g_multi_voice_dsp_pool` de 8 slots ;
- reader, position, play plan, ownership/génération, filtre, enveloppe filtre, VCA, enveloppe VCA et release par voix ;
- `brick6_sampler_runtime_multi_voice_limit()` et la politique d’éviction déterministe ;
- `multi_reindex_spread()` et `multi_apply_spread()` ;
- soumission `MIXER_EXTERNAL_FORMAT_MULTI_MONO/STEREO` ;
- bypass du filtre/VCA partagé dans `Src/Audio/mixer.c` ;
- scheduler normal avec `event_token` ;
- budget synth indépendant de 16 voix ;
- formats mono/stéréo, Stream et kernels reverse/ping-pong du Sampler RAM.

Références principales :

- `Inc/Core/brick6_sampler_multi_contract.h`
- `Inc/Core/brick6_sampler_runtime.h`
- `Src/Core/brick6_sampler_runtime.c`
- `Inc/Audio/multi_voice_dsp.h`
- `Src/Audio/multi_voice_dsp.c`
- `Src/Audio/mixer.c`
- `Src/Seq/seq_play_scheduler.c`

## 3. Autorité cible de VOICES/SPREAD

Les identités restent obligatoirement celles déjà existantes :

- `PARAM_CFG_POLY_VOICES`
- `PARAM_CFG_POLY_SPREAD`

Autorité sémantique cible :

1. `param_registry` reste le point d’accès commun pour lecture et écriture.
2. Le runtime de piste Multi (`g_sampler_multi_track_state` et ses getters/setters) est l’autorité active pour une piste Multi.
3. `pattern_v1_track_param_block_t.sound.track_values/track_valid` est l’autorité persistée du Pattern pour l’état CFG courant.
4. Patch et Kit sont des sources de configuration sérialisées, pas des autorités runtime concurrentes. Lorsqu’ils portent ces valeurs, ils doivent appeler le setter canonique après chargement.
5. Les champs nommés `synth_voice_count` et `synth_spread` ne doivent plus être utilisés pour représenter une piste Multi. Le format courant peut être modifié ; aucune migration historique n’est à prévoir.

La représentation Patch/Kit retenue doit être commune et explicitement typée, par exemple des champs `poly_voice_count` et `poly_spread` dans les payloads concernés. Le plan ne demande pas de dupliquer les valeurs dans des structures sans chemin de restauration démontré.

## 4. Étape 1 — CFG Multi et capability

### But

Rendre `VOICES` et `SPREAD` visibles et éditables dans l’ensemble CFG d’une piste `UI_TRACK_TYPE_MULTI`, avec le même placement, les mêmes IDs et les mêmes métadonnées que le template synth.

### Périmètre autorisé

- `Src/UI/pages/ui_page_template_cfg.c`
- `Src/UI/ui_track_catalog.c` uniquement si le resolver exige une déclaration explicite du template Multi
- `Src/Core/track_runtime.c`
- `Inc/Core/track_runtime.h` uniquement si une capability commune doit être déclarée
- tests CFG/capability existants ou ajout d’un contrôle statique ciblé

### Hors périmètre

- setters/getters runtime déjà fonctionnels ;
- allocation Multi ;
- Pattern, Snapshot, Patch, Kit et Undo ;
- refonte des templates synth, external ou drum ;
- builds autres que Release Low-Cost et Release Premium.

### Fichiers et symboles réels à HEAD

- `g_ui_template_cfg_family`
- `g_ui_template_cfg_synth_family`
- `ui_page_template_cfg_register_families()`
- `ui_template_family_resolve_active_track()`
- `track_runtime_get_effective_param_status()`
- `track_runtime_param_rule()`
- `TRACK_RUNTIME_RESOURCE_SYNTH`
- `TRACK_RUNTIME_FLAG_CAN_SYNTH`

### Modifications attendues

1. Ajouter `PARAM_CFG_POLY_VOICES` et `PARAM_CFG_POLY_SPREAD` au template CFG spécifique au Multi, sans modifier le contenu des autres familles.
2. Corriger la règle de ressource/capability pour que ces deux paramètres soient reconnus comme polyphonie commune ou comme ressource Multi dédiée.
3. Garantir que le Multi n’obtient pas `TRACK_RUNTIME_FLAG_CAN_SYNTH` et ne puisse jamais tomber dans `synth_polyphony` par effet de bord.
4. Conserver le domaine CFG et le statut non p-lockable.

### Invariants

- IDs, noms, bornes et encodage inchangés.
- `UI_TRACK_TYPE_SYNTH` conserve exactement son comportement.
- `UI_TRACK_TYPE_MULTI` obtient `VOICES` puis `SPREAD` dans le même ordre que le synth.
- Aucun appel d’allocation synth pour une piste Multi.
- Aucun paramètre CFG supplémentaire créé.

### Validations ciblées

- Vérifier le template résolu pour une piste Multi.
- Vérifier la présence et l’ordre `VOICES`, `SPREAD`.
- Vérifier le statut effectif `ALLOWED` pour les deux IDs sur Multi.
- Vérifier le statut synth inchangé.
- Vérifier que les deux IDs restent non p-lockables.

### Build

- Release Low-Cost uniquement si le changement est limité aux sources UI/runtime ;
- Release Premium uniquement si le build Low-Cost passe.
- Jamais `TestPremium`.

### Critère de réussite

Une piste Multi affiche les deux paramètres dans CFG, les accepte comme paramètres éditables et n’active aucune ressource synthétique.

### Retour attendu

Fichiers modifiés, preuve du template et du statut effectif, validations exécutées, build(s), résultat, commit créé.

### Message de commit

`Expose Multi polyphony parameters in CFG`

## 5. Étape 2 — Setters/getters et bornes UI

### But

Fermer la chaîne d’édition UI sans dépendance au pool synth.

### Périmètre autorisé

- `Src/Param/param_registry.c`
- `Src/UI/ui_param.c`
- `Src/Core/brick6_sampler_runtime.c`
- `Inc/Core/brick6_sampler_runtime.h`
- tests de propriété/validation CFG existants

### Hors périmètre

- template CFG et capability déjà traités à l’étape 1 ;
- stockage persistant ;
- changement de formule audio du spread ;
- scheduler.

### Fichiers et symboles réels à HEAD

- `param_registry_get_track_value()`
- `param_registry_set_track_value()`
- `param_registry_apply_track_value_rt_fast()`
- `param_registry_edit_track_value()`
- `ui_param_get_edit_bounds()`
- `ui_param_track_accepts_relative_param()`
- `brick6_sampler_runtime_get_multi_voice_count()`
- `brick6_sampler_runtime_set_multi_voice_count()`
- `brick6_sampler_runtime_get_multi_spread()`
- `brick6_sampler_runtime_set_multi_spread()`
- `brick6_sampler_runtime_multi_voice_limit()`

### Modifications attendues

1. Conserver le routage Multi existant vers l’état Multi.
2. Remplacer, pour une piste Multi, la borne UI synth de `VOICES` par `1..8`.
3. Laisser `SPREAD` utiliser les bornes catalogue `0..1`.
4. Vérifier que le fast path continue de refuser p-lock/MOD pour ces IDs tout en acceptant l’édition CFG normale.

### Invariants

- `VOICES` ne consulte jamais `synth_polyphony_get_available_for_track()` pour Multi.
- `VOICES = 1, 2, 4, 8` est accepté et clampé à `1..8`.
- `SPREAD` est clampé à `0..1`.
- Les getters/setters synth restent inchangés.
- Une écriture Multi ne modifie pas les valeurs synth.

### Validations ciblées

- Tests des valeurs `VOICES = 0, 1, 2, 4, 8, 9`.
- Tests `SPREAD = 0, 0.5, 1` et valeurs hors bornes.
- Vérification getter après setter.
- Vérification de l’état synth avant/après édition Multi.
- Vérification absence de p-lock et MOD.

### Build

Release Low-Cost puis Release Premium. Jamais `TestPremium`.

### Critère de réussite

L’édition CFG d’une piste Multi modifie uniquement son état Multi, avec les bornes attendues et sans appel synth.

### Retour attendu

Valeurs testées, preuves de routage, absence de mutation synth, build(s), résultat, commit créé.

### Message de commit

`Route Multi CFG editing to polyphony state`

## 6. Étape 3 — Pattern live RAM et Track Snapshot

### But

Rendre capture, restauration et canonisation cohérentes pour `VOICES/SPREAD` dans le Pattern et les snapshots de piste.

### Périmètre autorisé

- `Src/Storage/pattern_live_ram.c`
- `Inc/Storage/pattern_live_ram.h` si nécessaire
- `Src/Core/track_snapshot.c`
- `Inc/Core/track_snapshot.h`
- tests de persistance Pattern/Snapshot existants

### Hors périmètre

- format Patch/Kit ;
- Undo/Redo ;
- modification des structures runtime audio ;
- changement de formule de spread.

### Fichiers et symboles réels à HEAD

- `pattern_live_resolve_voice_budget()`
- capture `pattern_live_ram` des domaines CFG/sound
- transition d’application autour de `PARAM_CFG_POLY_VOICES`
- canonisation `track_valid` des paramètres CFG
- `track_snapshot_t`
- `track_snapshot_capture()`
- `track_snapshot_restore()`
- `track_snapshot_reapply_track_params()`

### Modifications attendues

1. Supprimer le traitement qui saute `PARAM_CFG_POLY_VOICES` pendant l’application.
2. Appliquer `VOICES` et `SPREAD` via le setter canonique selon la famille de piste.
3. Étendre la résolution de budget pour le Multi sans toucher au budget synth de 16.
4. Canoniser/restaurer les deux IDs pour Multi dans `track_values/track_valid`.
5. Ajouter au snapshot des champs explicitement génériques ou des branches famille Multi ; ne jamais capturer l’état synth pour restaurer un Multi.
6. Exclure les pointeurs et handles runtime des structures persistées/snapshot.

### Invariants

- Pattern capture puis restore restitue exactement les deux valeurs Multi.
- `VOICES` réduit en lecture conserve la politique d’éviction runtime déjà validée.
- Les valeurs synth ne sont ni capturées à la place du Multi ni modifiées.
- Les paramètres invalides sont clampés avec les bornes catalogue.
- Les deux IDs restent identiques à ceux du registre.

### Validations ciblées

- Capture/restore Multi avec `VOICES = 1, 2, 4, 8`.
- Capture/restore avec `SPREAD = 0, 0.5, 1`.
- Fermeture/réouverture de CFG puis capture/restore Pattern.
- Snapshot capture/restore avec piste synth voisine inchangée.
- Vérification des flags `track_valid` après canonisation.
- Vérification absence de pointeur ou identité runtime sérialisée.

### Build

Release Low-Cost puis Release Premium. Jamais `TestPremium`.

### Critère de réussite

Pattern et Snapshot restaurent les deux paramètres Multi par le chemin canonique sans routage synth et sans perte après canonisation.

### Retour attendu

Cas de persistance exécutés, valeurs avant/après, preuve synth inchangée, build(s), résultat, commit créé.

### Message de commit

`Persist Multi polyphony in pattern and snapshots`

## 7. Étape 4 — Patch/Kit et autorité de persistance

### But

Supprimer l’hypothèse de payload synth-only et définir une représentation courante explicite pour les configurations polyphoniques Multi.

### Périmètre autorisé

- `Inc/Storage/patch_v1.h`
- `Src/Storage/patch_v1.c`
- `Inc/Storage/kit_v1.h`
- `Src/Storage/kit_v1.c`
- tests Patch/Kit existants

### Hors périmètre

- modification du format Pattern déjà traité à l’étape 3 ;
- nouvelle migration legacy ;
- duplication dans `track_sound_state_t` sans consommation démontrée ;
- changement du runtime audio Multi.

### Fichiers et symboles réels à HEAD

- `patch_v1_track_t`
- `PatchSaveV1`
- `patch_v1_capture_track()`
- `patch_v1_apply_slot_to_track()`
- `kit_v1_track_payload_t`
- `kit_v1_capture_track()`
- `kit_v1_apply_track_payload()`
- `patch_v1_track_reapply_params()`
- champs actuels `synth_voice_count` et `synth_spread`

### Décision d’architecture

- Pattern/track state reste l’autorité persistée principale des paramètres CFG courants.
- Patch et Kit restent des sources de configuration réutilisables lorsqu’ils restaurent effectivement une piste.
- Leur payload porte une représentation générique de la polyphonie (`poly_voice_count`, `poly_spread`, ou nom équivalent validé dans l’implémentation), avec famille/type conservés.
- L’application Patch/Kit appelle `param_registry_set_track_value()` ou le wrapper canonique équivalent ; elle ne modifie pas directement `synth_polyphony` pour une piste Multi.
- Les anciens champs synth-only sont remplacés ou renommés dans le format courant ; aucune migration historique n’est ajoutée.

### Modifications attendues

1. Remplacer les branches capture/restauration conditionnées uniquement par `UI_TRACK_FAMILY_SYNTH`.
2. Ajouter les deux valeurs génériques au payload seulement là où Patch/Kit ont réellement la responsabilité de restaurer la configuration de piste.
3. Rendre explicite la conversion famille/type vers setter synth ou setter Multi.
4. Ne pas créer une seconde autorité runtime dans Patch ou Kit.
5. Documenter dans les structures et fonctions que les payloads sont des snapshots d’entrée et non des états audio vivants.

### Invariants

- Patch/Kit synth continuent de restaurer les valeurs synth.
- Patch/Kit Multi restaurent les valeurs Multi.
- Aucun Patch/Kit Multi ne modifie le pool synth de 16.
- Aucun changement de format persistant non documenté.
- Aucune valeur n’est silencieusement perdue lors d’un capture/apply courant.

### Validations ciblées

- Capture/apply Patch Multi avec `VOICES = 1, 2, 4, 8` et trois valeurs de spread.
- Capture/apply Kit Multi avec les mêmes valeurs.
- Comparaison avant/après des valeurs synth sur une autre piste.
- Vérification des payloads écrits et relus.
- Vérification que l’application passe par le setter canonique.

### Build

Release Low-Cost puis Release Premium. Jamais `TestPremium`.

### Critère de réussite

Patch et Kit restaurent les paramètres Multi selon l’autorité définie, sans payload synth-only résiduel ni duplication contradictoire.

### Retour attendu

Autorité retenue, structures modifiées, cas Patch/Kit exécutés, preuve de routage, build(s), résultat, commit créé.

### Message de commit

`Persist Multi polyphony through patch and kit configuration`

## 8. Étape 5 — Undo/Redo

### But

Harmoniser `SPREAD` avec `VOICES` sans transformer inutilement une variation de valeur en opération structurelle.

### Périmètre autorisé

- `Src/Storage/undo_v2.c`
- `Src/UI/ui_param.c`
- tests Undo/Redo existants

### Hors périmètre

- modèle audio Multi ;
- stockage Pattern/Patch/Kit ;
- scheduler Note On/Off.

### Fichiers et symboles réels à HEAD

- `undo_v2_param_is_undoable()`
- logique de début d’undo structurel dans `ui_param.c`
- `ui_param_apply_final_value()`
- traitement de `PARAM_CFG_POLY_VOICES`
- traitement de `PARAM_CFG_POLY_SPREAD`

### Décision d’architecture

- `VOICES` conserve un traitement structurel si sa réduction peut détruire/évincer des voix actives.
- `SPREAD` est un changement de valeur simple : il doit être undoable sans déclencher de snapshot structurel, sauf preuve contraire.
- Les deux paramètres restent non p-lockables.

### Modifications attendues

1. Rendre `SPREAD` undoable par le chemin de valeur approprié.
2. Vérifier que `VOICES` capture bien l’état nécessaire avant une réduction.
3. Vérifier que l’undo d’une réduction réouvre la capacité sans reconstruire des pointeurs runtime invalides.
4. Conserver l’ordre de réindexation spread après modification de capacité.

### Invariants

- Undo/Redo de `VOICES` ne corrompt pas le pool de 8.
- Undo/Redo de `SPREAD` ne vole ni ne détruit inutilement une voix.
- Les valeurs synth restent isolées.
- Aucun p-lock ni MOD n’est créé.

### Validations ciblées

- Undo/Redo `VOICES` pour 1→4→8 et 8→2→8 avec voix actives.
- Undo/Redo `SPREAD` pour 0→0.5→1.
- Vérification des voix actives et de leur ownership après chaque opération.
- Vérification du snapshot de la piste Multi.

### Build

Release Low-Cost puis Release Premium. Jamais `TestPremium`.

### Critère de réussite

Les deux paramètres sont réversibles dans l’UI, avec traitement structurel uniquement lorsque nécessaire.

### Retour attendu

Scénarios Undo/Redo, distinction structurel/simple, état du pool, build(s), résultat, commit créé.

### Message de commit

`Align Multi polyphony undo behavior`

## 9. Étape 6 — Nettoyage de l’API Note Off legacy

### But

Éliminer l’ambiguïté résiduelle sans modifier le scheduler tokenisé normal.

### Périmètre autorisé

- `Inc/Core/brick6_sampler_runtime.h`
- `Src/Core/brick6_sampler_runtime.c`
- consommateurs directs trouvés par recherche de `note_off_multi_track_note`
- documentation locale de l’API si nécessaire

### Hors périmètre

- `seq_play_scheduler.c` et son chemin tokenisé fonctionnel ;
- génération des tokens ;
- logique Note On normale ;
- lifecycle déjà validé.

### Fichiers et symboles réels à HEAD

- `brick6_sampler_runtime_note_off_multi_track_note()`
- `brick6_sampler_runtime_note_off_multi_track_note_token()`
- `seq_play_scheduler_emit_engine_note()`
- consommateurs directs de l’API legacy

### Décision attendue

Après recherche des consommateurs :

- supprimer l’API si aucun consommateur légitime ne subsiste ; ou
- la conserver uniquement pour arrêts forcés explicitement documentés, avec un nom indiquant qu’elle ferme toutes les occurrences ; ou
- la remplacer partout par la variante tokenisée lorsque l’occurrence est connue.

### Modifications attendues

1. Ne laisser aucun appel ambigu dans le chemin scheduler normal.
2. Conserver une API d’arrêt forcé seulement si un consommateur hors scheduler l’exige réellement.
3. Documenter son comportement « toutes les occurrences correspondantes » si elle est conservée.

### Invariants

- Deux notes identiques simultanées restent indépendantes dans le scheduler normal.
- Note Off tokenisé cible une seule occurrence.
- Panic, transport stop et changement d’instrument conservent leur comportement global.

### Validations ciblées

- Recherche sans consommateur ambigu dans le scheduler.
- Deux Note On identiques puis deux Note Off séparés par token.
- Arrêt forcé explicite si l’API est conservée.
- Vérification génération/ownership après réutilisation de slot.

### Build

Release Low-Cost puis Release Premium. Jamais `TestPremium`.

### Critère de réussite

Le chemin normal ne peut plus fermer plusieurs occurrences par erreur ; toute fermeture globale restante est explicitement nommée et documentée.

### Retour attendu

Consommateurs trouvés, décision API, scénarios Note Off, build(s), résultat, commit créé.

### Message de commit

`Clarify legacy Multi note-off semantics`

## 10. Étape 7 — Validation finale et documentation

### But

Démontrer la conformité complète et documenter les invariants réellement livrés.

### Périmètre autorisé

- tests existants et scripts ciblés sans instrumentation permanente ;
- documentation Multi et mémoire/runtime déjà concernée ;
- fichiers de test si un ajout ciblé est strictement nécessaire

### Hors périmètre

- nouvelle fonctionnalité audio ;
- changement d’architecture non justifié par un échec ;
- `TestPremium` ;
- nettoyage de changements hors périmètre.

### Validations obligatoires

- CFG Multi affiche `VOICES` et `SPREAD`.
- Édition réelle des deux paramètres.
- `VOICES = 1, 2, 4, 8`.
- `SPREAD = 0`, valeur intermédiaire et maximum.
- Fermeture/réouverture CFG.
- Sauvegarde/restauration Pattern.
- Snapshot.
- Patch/Kit selon l’autorité retenue.
- Undo/Redo de `VOICES`.
- Undo/Redo de `SPREAD`.
- Aucune modification des valeurs synth.
- Aucun routage Multi vers le pool synth.
- Pool global Multi limité à 8.
- Rendu polyphonique sans régression.
- Mono/stéréo et promotion par spread.
- Aucun changement persistant non documenté.
- Aucun appel Note Off legacy ambigu restant dans le chemin normal.

### Builds

- Release Low-Cost.
- Release Premium.
- Jamais `TestPremium`.

### Critère de réussite

Tous les contrats du contre-audit sont PASS ou explicitement justifiés comme hors contrat. Aucun défaut CFG, capability, persistance ou Undo ne reste ouvert.

### Retour attendu

Rapport final synthétique, tests/builds, fichiers de documentation mis à jour, commit créé.

### Message de commit

`Validate completed Multi polyphony configuration path`

## 11. Commits attendus

Chaque étape doit être exécutée séparément et produire exactement un commit local dédié, sans push :

1. `Expose Multi polyphony parameters in CFG`
2. `Route Multi CFG editing to polyphony state`
3. `Persist Multi polyphony in pattern and snapshots`
4. `Persist Multi polyphony through patch and kit configuration`
5. `Align Multi polyphony undo behavior`
6. `Clarify legacy Multi note-off semantics`
7. `Validate completed Multi polyphony configuration path`

Après chaque commit, l’agent doit s’arrêter et attendre explicitement `go étape N`.

## 12. Dettes restantes

- Les validations statiques ne remplacent pas une mesure audio sur matériel avec huit voix actives.
- La mémoire Premium est déjà fortement chargée ; toute extension de payload doit être vérifiée dans les deux builds autorisés.
- Les formats Patch/Kit courants étant modifiables sans migration, leur nouvelle représentation devra être documentée précisément.
- Les chemins Stream et les kernels RAM communs doivent rester inchangés sauf preuve de régression.
- Aucun élargissement du contrat à reverse/ping-pong Multi ne doit être introduit.
