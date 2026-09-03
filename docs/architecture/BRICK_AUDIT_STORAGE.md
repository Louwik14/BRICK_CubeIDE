# BRICK — REGISTRE D’AUDIT STORAGE

## Statut

Ce document centralise les anomalies **déjà découvertes** dans le secteur STORAGE.

Dernière consolidation : **après ROUND 5**.

Objectif des prochains audits :

- ne pas recompter les problèmes connus ;
- chercher uniquement de nouvelles violations ;
- maintenir une vision globale des handoffs, jobs async, quiesce et transactions filesystem ;
- mettre à jour ce document après chaque round.

---

# 1. Architecture cible

STORAGE possède :

- FatFs ;
- SD scheduler ;
- DMA/storage transport ;
- decode ;
- packing ;
- loaders ;
- cache ;
- préparation filesystem.

CONTROL possède :

- décisions métier ;
- apply ;
- publication CONTROL→AUDIO.

UI possède :

- présentation ;
- requêtes.

Audio possède :

- data path temps réel.

Flux cible :

```text
UI / CONTROL
→ request
→ STORAGE_IO
→ result
→ CONTROL apply si mutation métier
```

Aucun FatFs depuis UI/CONTROL/AUDIO hors bootstrap explicitement démontré.

---

# 2. CONTROL exécute FatFs — Looper/Recorder arm

Chemin démontré :

```text
CONTROL
→ audio_recorder_control_sync_looper_arm()
→ génération/réservation path
→ préparation fichier
→ FatFs
```

Classification : `P0/P1 — BLOCKER H747`

---

# 3. Audio Rec / SampleCapture exécute Storage depuis UI

UI peut directement :

- préparer session ;
- faire waveform/cache ;
- save WAV ;
- charger sample RAM ;
- preview ;
- rec-bus ;
- Recorder/SD operations.

`g_sample_capture` a plusieurs writers.

Classification : `P1 / BLOCKER H747`

---

# 4. Recorder facade multi-owner

Structure :

```text
g_audio_recorder
```

Writers :

- UI ;
- CONTROL ;
- STORAGE.

Le ring PCM Audio→Storage est **propre**.

Classification : `P1`

---

# 5. `sd_preview` multi-owner

Structure :

```text
g_sd_preview
```

Writers :

- UI ;
- CONTROL ;
- STORAGE.

Le ring preview STORAGE→AUDIO est **propre**.

Classification : `P1`

---

# 6. `waveform_cache` multi-writer

Writers :

- UI_SERVICE ;
- STORAGE_IO.

Jobs capacité 4.
Tiles capacité 8.

Classification : `P1 — BEFORE H747`

---

# 7. Asset retire — publication métier depuis STORAGE

Storage publie directement certaines commandes CONTROL→AUDIO :

- sampler retire ;
- RAM sampler retire ;
- wavetable retire ;
- multi-sample retire.

Classification : `P1`

---

# 8. Résultats async RAM / Wavetable one-shot multi-consommateurs

Structures :

```text
g_sampler_ram_load_job
g_wavetable_load_job
```

Problème :

- plusieurs consumers possibles ;
- premier `take_result()` remet `IDLE`;
- autres consommateurs perdent le résultat ;
- pas de requester/token.

Classification : `P1 — BEFORE H747`

---

# 9. `g_wav_convert` clear depuis UI

Writer principal : STORAGE.

UI peut appeler :

```text
wav_convert_clear_finished()
```

qui peut `memset` le contexte Storage.

Classification : `P1 — BEFORE H747`

---

# 10. Patch Save — staging métier/filesystem mélangé

Structures :

```text
g_stage
g_patch_save_pending
```

CONTROL prépare/modifie.
STORAGE consomme.

Classification : `P1`

---

# 11. `g_sample_global_pool` clear multi-owner

CONTROL peut clear directement via Project remove sample.
STORAGE possède un chemin équivalent par requête.

Classification : `P1`

---

# 12. Pattern load ownership ambigu

Structures :

```text
g_pattern_load_state
g_pattern_io_workspace
```

CONTROL et STORAGE peuvent modifier/consommer selon un contrat existant.

Classification : `AMBIGUOUS — BEFORE H747`

---

# 13. Catalogue WAV — UI déclenche FatFs indirectement

Getters UI peuvent provoquer des accès filesystem via catalog get/find path.

Classification : `P1`

---

# 14. Catalogue WAV — pointeurs internes / lifetime

UI peut recevoir des pointeurs vers structures internes mutables.

Classification : `P1 / AMBIGUOUS selon callsite`

---

# 15. Catalogue WAV — completion sans identité

Completion catalog/page sans request identity suffisamment forte.

Classification : `P1`

---

# 16. Project Load — quiesce incomplet

Certaines mailboxes/jobs peuvent continuer pendant replacement.

Déjà identifiés notamment :

- conversion WAV ;
- import/delete multi ;
- autres pending spécifiques.

Classification : `P1`

---

# 17. Pattern Save — intent survivant au quiesce

Un intent capture/save peut survivre à la fermeture de l’ingress et recréer un pending save.

Classification : `P1`

---

# 18. Multi import/delete pendant replacement

Import progressif :

- pas toujours annulé/revalidé pendant Project replacement.

Suppression index :

- protection insuffisante.

Classification : `P1`

---

# 19. Patch apply completion sans epoch/projet/requête

Round 4.

Une completion RAM ancienne peut arriver pendant/après Project Load et être appliquée dans le nouveau contexte.

Classification : `P1`

---

# 20. WAV convert — `.BAK` résiduel ignoré

Round 4.

Si suppression `.BAK` échoue :

- conversion peut être déclarée réussie ;
- prochaine conversion peut rester bloquée.

Classification : `P1`

---

# 21. Multi Sample Loader — request B acceptée pendant completion A pending

Round 5.

Aucune request ID/epoch suffisamment forte.

Séquence possible :

```text
A DONE pending
→ B acceptée
→ completion A consommée dans contexte B
→ completion B perdue
```

Classification : `P0`

---

# 22. Project Save — DONE considéré non-busy

Round 5.

Une nouvelle sauvegarde peut être acceptée alors que la completion précédente n’a pas encore été consommée.

Classification : `P1`

---

# 23. RAM / Wavetable — incohérence busy/DONE

Round 5.

- `DONE` exclu de `busy`;
- nouveau load ensuite rejeté car état non-IDLE ;
- requête B perdue ;
- clear et load pending peuvent coexister ;
- clear exécuté avant load.

Classification : `P1`

---

# 24. `persistent_fatfs` — contrats d’erreur incomplets

Round 5.

Problèmes :

- erreur `stat` traitée comme “absent” ;
- échec suppression `.BAK` après commit ignoré ;
- rollback failure ignoré ;
- SUCCESS possible alors que transaction filesystem incomplète.

Classification : `P1`

---

# 25. Pattern/Patch — suppression ancien fichier avant rename

Round 5.

Séquence :

```text
delete old
→ rename new
```

Si rename échoue :

> ancien contenu perdu.

Pattern commit marker également concerné.

Classification : `P1`

---

# 26. WAV catalog / Multi index — écriture directe fichier final

Round 5.

Pas de TMP/BAK atomique.

Une interruption peut laisser :

- catalogue tronqué ;
- index final partiel.

Classification : `P1`

---

# 27. Sample trim — success avant `f_close`

Round 5.

SUCCESS peut être publié avant validation `f_close`.

Un fichier final partiel peut rester après échec.

Classification : `P1`

---

# 28. Waveform cache — READY avant validation close

Round 5.

Après sync :

- READY publié ;
- `close` failure ignorée.

Classification : `P1`

---

# 29. WAV convert mailbox — écrasement A par B

Round 5.

Nouvelle request peut écraser la précédente sans :

- requester ;
- ID ;
- rejet explicite.

Classification : `P1`

---

# 30. Pools/assets mutables exposés

Déjà identifiés :

```text
g_sample_global_pool
g_sample_page_cache_state
g_sample_cache[]
g_sampler_ram_pool
g_wavetable_pool
g_multi_instruments[]
g_multi_samples[]
g_multi_zones[]
```

Classification : `P1 — BEFORE H747`

---

# 31. Zones explicitement validées comme propres

À ne pas rouvrir sans preuve :

- SD transport/page cache principal ;
- streamer ;
- recorder PCM ;
- preview PCM ;
- scheduler SD ;
- Project snapshots principaux ;
- Pattern single-flight principal ;
- wavetable transactional writer principal ;
- catalog request gating hors anomalies précises ;
- multi import/delete result gating hors leaks listés ;
- workspace buffers sérialisés déjà vérifiés ;
- recorder preparation spécifique Round 5 jugée propre ;
- aucune nouvelle omission quiesce au Round 5 au-delà des cas listés ;
- aucun nouveau FatFs hors owner au Round 5 en dehors des chemins listés.

---

# 32. Invariants structurels émergents

## Jobs async

Une request doit avoir :

- requester/owner clair ;
- identité suffisante ;
- completion non ambiguë ;
- exactly-once consumption ;
- cancel/new request cohérent.

## Replacement / quiesce

Une completion ancienne ne doit jamais modifier le nouvel état.

## Filesystem transaction

SUCCESS seulement lorsque :

- write ;
- sync ;
- close ;
- rename/commit ;
- cleanup nécessaire

ont atteint l’état durable attendu.

## Staging

Snapshot métier et staging filesystem ne doivent pas être le même objet mutable partagé.

---

# 33. Règles pour les prochains audits

Ne compter comme nouveau que :

- nouveau job ;
- nouveau stale completion ;
- nouveau workspace lifetime hazard ;
- nouvelle transaction filesystem incomplète ;
- nouvelle omission quiesce ;
- nouveau FatFs hors owner ;
- nouveau multi-owner réel.

Mettre à jour ce document après chaque round.

---

# 34. Nouvelles découvertes à ajouter

```text
## ROUND N

### ID — classification
- Workflow :
- Requester :
- Worker :
- Completion :
- Cause :
- Impact :
- H747 :
```

## ROUND 6

### R6-ST-01 - Patch replacement ingress sans quiesce
- Classification : P1
- Workflow : Project Load -> quiesce demande/active -> Patch Save/Rename/Delete
- Requester : CONTROL via `patch_product_control_process_intent`
- Worker : `patch_product_storage_request_service`, `patch_product_save_service`, `patch_product_rename`, `patch_product_delete`
- Completion : save/delete publient `g_patch_completion_valid`; rename n'a pas de completion dediee
- Cause : les chemins Save/Rename/Delete n'ont pas de garde `project_replacement_is_active()` et `project_load_quiesce` ne les retire pas
- Impact : une operation Patch peut etre acceptee/executed pendant le remplacement; Save peut capturer l'ancien etat, Rename/Delete peuvent modifier le produit pendant le changement de projet
- `NEW INSTANCE OF KNOWN RULE` (quiesce incomplet; staging/old-new request)

### R6-ST-02 - Patch Save orphan si une request Patch A est deja pending
- Classification : P1
- Workflow : request Patch A deposee -> intent Save B -> `patch_product_save` accepted -> `patch_product_request_begin(SAVE)` refuse B -> A termine
- Requester : CONTROL, intents Patch
- Worker : `patch_product_control_process_intent`, `patch_product_storage_request_service`
- Completion : aucune pour B; `g_patch_save_pending` reste pose sans `PATCH_REQUEST_SAVE` consommable
- Cause : `patch_product_save()` ne teste pas `g_patch_request`; l'echec de `patch_product_request_begin()` est ignore
- Impact : mailbox Save B reste bloque et son snapshot peut etre ecrase/retarde sans completion
- `NEW INSTANCE OF KNOWN RULE` (acceptation mailbox non atomique / old-new request mixing)

### R6-ST-03 - Sampler RAM : Load A et Clear B coexistent
- Classification : P1
- Workflow : Load A depose dans le mailbox -> Clear B accepte avant consommation -> Clear B traite en priorite -> Load A demarre ensuite
- Requester : CONTROL/UI asset operations
- Worker : `sampler_ram_pool_storage_request_service`, puis `sampler_ram_pool_load_async_begin`
- Completion : resultat async du Load A peut etre publie apres le Clear B, sans identite de Clear/Load associee
- Cause : `sampler_ram_pool_request_clear()` ne rejette pas `g_sampler_ram_load_request_valid`; le worker priorise Clear puis retourne
- Impact : un slot explicitement efface peut etre rehydrate par une ancienne request et son resultat
- `NEW INSTANCE OF KNOWN RULE` (busy/DONE et request ordering deja identifies)

### R6-ST-04 - Wavetable : Load A et Clear B coexistent
- Classification : P1
- Workflow : Load A depose dans le mailbox -> Clear B accepte avant consommation -> Clear B traite en priorite -> Load A demarre ensuite
- Requester : CONTROL/UI asset operations
- Worker : `wavetable_pool_storage_request_service`, puis `wavetable_pool_load_async_begin_with_geometry`
- Completion : resultat async du Load A peut etre publie apres le Clear B, sans identite de Clear/Load associee
- Cause : `wavetable_pool_request_clear()` ne rejette pas `g_wavetable_load_request_valid`; le worker priorise Clear puis retourne
- Impact : un slot efface peut etre rehydrate par une ancienne request et son resultat
- `NEW INSTANCE OF KNOWN RULE` (busy/DONE et request ordering deja identifies)

### R6-ST-05 - SampleCapture assignment non quiesce
- Classification : P1
- Workflow : Project Load lance -> ingress ferme -> evenement AudioRec Assign encore traite -> `sample_global_pool_load_classic`/prefill -> reset physique du nouveau Project
- Requester : UI AudioRec
- Worker : `sample_capture_model_assign_trimmed` ou `sample_capture_model_assign_saved_take_to_pool`, puis `sample_cache_prepare`/service
- Completion : retour `1` de l'assign est visible avant la fin du prefill; aucune completion liee a l'epoch du Project
- Cause : les chemins d'assign SampleCapture n'ont pas de garde replacement; ils ne sont pas retires par `project_load_quiesce`, alors que le load finit par `sample_global_pool_reset()`
- Impact : assign accepte puis perdu au commit du nouveau Project, avec travail/cache pouvant survivre au changement
- `NEW INSTANCE OF KNOWN RULE` (quiesce incomplet; completion/lifetime sans epoch)

## ROUND 7

### R7-ST-01 - Waveform readers/cache sans media epoch
- Classification : P1
- Workflow : waveform cache ou cache waveform SampleCapture en cours -> conversion/remplacement du WAV ou invalidation/remontage SD -> le worker reprend sur le meme path
- Requester : UI/UI_SERVICE (waveform cache, SampleCapture editor/overview/detail/line)
- Worker : `waveform_cache_service_build` et les services waveform de `SampleCapture`
- Completion : `READY`/cache tile/overview peut etre publie avec les metadonnees de l'ancienne source et les octets de la nouvelle source
- Cause : les jobs conservent path/frame counters mais aucun `media_epoch`; ils ne revalident pas l'epoch entre deux passes. `wav_convert` avance pourtant l'epoch apres remplacement
- Impact : waveform/cache stale ou incoherent; source metier non corrompue, mais affichage et cache peuvent etre associes au mauvais contenu
- `NEW INSTANCE OF KNOWN RULE` (stale completion / identite de source sans epoch)
