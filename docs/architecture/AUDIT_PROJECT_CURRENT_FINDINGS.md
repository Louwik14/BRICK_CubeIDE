# Audit final Project / Pattern V3

Audit statique de l'état courant du dépôt. Aucun build et aucun fichier code n'ont été modifiés.

## VERDICT

**NEW FINDINGS**

Le chemin courant a bien une staging Pattern, un état AUDIO préparé et une commande `STATE_COMMIT` unique, mais l'installation CONTROL n'est pas isolée du live avant le commit Pattern. Les gates directs sont globalement présents ; plusieurs entrées indirectes et plusieurs completions ne respectent toutefois pas encore les contrats figés.

## NEW FINDINGS

### NF-01 — Installation CONTROL live avant le commit Pattern

Dans `project_product_control_process()` (`Src/Storage/project_product.c`), le premier passage appelle `project_control_begin_asset_restore()`, enregistre les runtimes, applique `persistent_pattern_control_apply()`, applique les macros, modifie l'état Pattern actif et construit l'état AUDIO préparé. Ces fonctions modifient les owners CONTROL ; la suppression de publication ne crée pas de copie CONTROL candidate.

Le commit persistant `pattern_control_bank_commit()` n'arrive qu'au passage Storage suivant, puis `STATE_COMMIT` est publié au passage CONTROL suivant. L'ordre courant est donc :

```text
installation CONTROL / préparation AUDIO
→ commit Pattern
→ STATE_COMMIT
```

et non une prévalidation CONTROL isolée suivie du commit Pattern puis de l'installation CONTROL finale. Si le commit Pattern échoue, `project_product_load_finish(0)` abandonne le staging mais ne restaure pas les owners CONTROL déjà remplacés. Une erreur de l'installation CONTROL peut aussi laisser un CONTROL partiellement candidat avant l'échec. La préservation de l'état courant n'est donc pas garantie sur ces branches.

### NF-02 — Échecs d'assets absorbés après purge du runtime physique

Après le safe point, `project_product_reset_physical_assets()` réinitialise le cache samples et le pool global avant que tous les assets candidats soient terminaux. Dans `PROJECT_LOAD_ASSETS`, `PROJECT_LOAD_WAIT_RAM` et `PROJECT_LOAD_WAIT_WAVETABLE`, plusieurs échecs font seulement progresser `asset_warning_count` et l'index.

Le CONTROL d'installation ignore ensuite les assets non `READY`; `apply_product_state()` peut alors effacer la sélection d'asset et retourner succès. Un fichier asset absent, illisible ou en échec I/O peut donc produire un Project Load annoncé réussi avec une configuration dégradée, après destruction de l'ancien runtime physique. Cela viole l'échec propre avant commit autant que possible et la conservation intégrale de l'état en cas de rejet.

### NF-03 — `project_product_load()` ne ferme pas lui-même l'exclusion Pattern Save

Les portes `control_domain_request_project()` et `project_product_control_process_intent()` testent `pattern_storage_save_busy()`. En revanche, l'API d'admission `project_product_load()` teste `pattern_storage_is_pending()` via `project_load_allowed()` et `pattern_control_bank_async_busy()`, mais pas `pattern_storage_save_busy()`.

Une demande Pattern Save encore seulement dans `g_pattern_save_request_valid` n'est ni un load pending ni une opération bank async. Un appel direct à `project_product_load()` peut donc créer le workspace et démarrer le remplacement alors qu'un Pattern Save est pending. L'exclusion est correcte pour certains appelants UI, mais pas garantie à l'admission produit elle-même.

### NF-04 — Succès Project et Flash boot avant consommation AUDIO

Après `control_rt_publish_audio_state_commit()`, `project_product_control_process()` passe `g_project_load_control_done` à `2`. Le service Storage peut alors appeler `project_product_load_finish()`, publier le succès et exécuter `boot_context_flash_commit()` avant que `audio_command_executor_apply_due()` n'ait consommé et appliqué la commande.

Le chemin courant ne comporte aucun ack de génération AUDIO vers CONTROL/Storage. La wake-up signale la publication, pas l'application. L'ordre effectif peut donc être `STATE_COMMIT publié → succès/Flash boot → AUDIO applique`, contrairement à la séquence attendue `AUDIO applique → succès live → Flash boot secondaire`.

### NF-05 — Intentions Pattern acceptées avant l'admission réelle

`pattern_live_capture_to_slot()` et `pattern_live_queue_slot()` valident le slot puis poussent une entrée dans `g_pattern_live_intents`. La capacité est de 32 entrées. Les gates Project/Pattern, Save/Load et recorder ne sont évalués que plus tard par `pattern_live_control_process()` et les fonctions `_control`.

L'UI transforme immédiatement le retour positif en `PAT STORED` ou `PAT QUEUED`. Une action incompatible avec un Project pending/actif, un Pattern Save/Load pending/actif ou un recorder peut donc être annoncée acceptée puis refusée au drain. Cette file d'intentions constitue en outre une file d'attente implicite et n'est pas incluse dans les gates d'admission Project.

### NF-06 — Échec Pattern Save perdu après un faux succès UI

Le retour positif de `pattern_live_capture_to_slot()` provoque le feedback `PAT STORED` avant l'écriture SD. À la completion, `pattern_storage_publish_save_completion()` transporte bien un bit de succès, mais `pattern_live_control_process()` le consomme sans traiter le cas `success == 0` ni publier un échec utilisateur.

Un échec de codec, de commit ou d'I/O peut donc laisser l'UI dans l'état « stored » alors que le Pattern n'est pas sauvegardé. La completion existe techniquement, mais son erreur n'est pas propagée au consommateur produit.

### NF-07 — Gate SD absent trop tard pour Pattern Load

`pattern_storage_request()` (`Src/Storage/pattern_live_ram.c`) ne teste pas `sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA`. Il peut arrêter une preview, modifier l'état de load, définir l'owner Storage et réveiller le service.

Le workspace Pattern puis `pattern_control_bank_load_async_begin()` sont créés plus tard par `pattern_storage_service()`. Le refus SD n'est donc pas effectué avant mutation et création de job, contrairement au contrat fixé. Pattern Save et Project Save/Load ont un test SD plus en amont.

### NF-08 — Erreur Pattern Load sans completion terminale de la pending queue

Sur corruption ou I/O impossible, `pattern_storage_service()` passe `g_pattern_load_state` à `PATTERN_LOAD_ERROR` et réveille CONTROL/Storage. `pattern_live_try_take_pending_ready()` ne traite toutefois que `pattern_storage_load_available()` et ne purge pas `g_pending_queue_valid` sur cette erreur.

La queue Pattern peut rester pending dans le modèle live alors que le storage se considère sorti de `pattern_storage_is_pending()`. Aucun résultat terminal n'est transmis à l'utilisateur et un état pending résiduel peut perturber une admission ultérieure.

### NF-09 — Échec Flash boot ignoré par Project Save et Load

`project_save_finish()` et `project_product_load_finish()` ignorent le retour de `boot_context_flash_commit()`. Le résultat Project peut être marqué succès alors que le contexte secondaire de boot n'a pas été écrit, ce qui laisse le live et le boot persistant divergents sans erreur visible.

## CONTRACT SIMPLIFICATION

- Les gates directs Project Save/Load vérifient `STOPPED stable`, ne déclenchent pas de STOP automatique et refusent avant création de leur opération. Le quiesce Load purge/panic les ressources après admission, sans transformer cela en STOP transport.
- Project Save conserve un snapshot CONTROL/Pattern immuable et écrit par tranches DATA bornées de 4096 octets avec `.TMP`/`.BAK`; aucun défaut de fairness ou de mutation live supplémentaire n'est démontré sur ce chemin, hors completion Flash NF-09.
- Le décodage Project et la staging Pattern sont longs et synchrones dans la phase Storage, mais l'opération est STOPPED-only et modale. Aucun worker qui doit impérativement progresser pendant ce décodage n'a été démontré bloqué ; aucun quanta coopératif additionnel n'est justifié.
- Le chemin Project utilise une staging bank inactive, un prepared AUDIO state et une publication unique `STATE_COMMIT`. Les boucles `PARAM_COUNT` sont des copies/préparations bornées dans ce state ; aucun ancien flux actif de milliers de commandes PARAM/PROGRAM, second FIFO ou seconde chronologie Project n'a été trouvé. Il n'y a donc pas de motif pour grossir ou réserver la FIFO.
- `audio_command_state_commit_internal_failure()` traite les erreurs AUDIO internes comme invariants via `Error_Handler()`. Elles ne constituent pas un scénario externe normal et ne justifient pas un rollback général ; le défaut de completion après publication reste celui de NF-04.
- Les gates directs Pattern Save/Load sont mutuellement exclusifs et les chemins de boundary musicale conservent une génération et une limite `first_unpublished_sample`. Les écarts démontrés portent sur l'admission indirecte, le gate SD et les erreurs terminales ci-dessus.
- Les appels `persistent_pattern_control_apply()` restants correspondent à l'installation CONTROL Pattern/Project et aux changements live Pattern ; aucun ancien chemin de restauration Project par milliers de PARAM/PROGRAM n'est actif.

## OVERARCHITECTURE

Aucune mécanique active n'a été identifiée comme uniquement nécessaire à la survie d'un état rendu impossible par les contrats. Les retries et rollback généraux restent inutiles ; les problèmes constatés relèvent de l'isolation candidate, des gates et des completions.

## DOCUMENT

Document d'audit mis à jour uniquement. Aucun patch code et aucun build.
