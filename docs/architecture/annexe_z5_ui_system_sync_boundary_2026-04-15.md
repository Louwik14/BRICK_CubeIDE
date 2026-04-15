# z5_ui_system_sync_boundary
Statut documentaire: Annexe utile (non canonique de zone).
Autorite: le document canonique de zone reste la source de verite.


Date: 2026-04-15  
Scope: nouveau chantier Z5 cible "sync système portée par l'UI", limité aux changements de track/config actifs dans `Src/UI/ui_core.c`.

## Preuve de flux minimal
- Superloop: `brick6_app_process()` appelle `ui_core_service_track_selection_inputs()` avant `hall_keyboard_bridge_process()` (`Src/Core/brick6_app_init.c:151-153`).
- Tasklet UI: `ui_tasklet_poll()` appelle `ui_core_tick()` (`Src/UI/ui_tasklet.c:50`).
- Implication: un changement de track peut déclencher la sync système hors queue (via hall direct), avant le dispatch des events UI queue.

## Carte courte: points de sync système portés par l'UI

Mise à jour micro-extraction locale (implémentée):
- Point interne unique introduit: `ui_core_sync_system_on_track_context_change(...)` (`Src/UI/ui_core.c`).
- Aucune API publique ajoutée/modifiée.
- Les 4 call sites ciblés passent désormais par ce point unique avec des flags, en conservant l'ordre historique propre à chaque cas.
- Durcissement local appliqué: profils d'appel nommés via helpers statiques (plus d'initialisations positionnelles ambiguës aux call sites).

### 1) Pivot de track actif
- Entrée UI: `ui_core_handle_track_hall_action()` -> `ui_core_set_active_track()` (`Src/UI/ui_core.c:1273-1297`, `1000-1015`).
- Noyau sync porté par UI:
  - `keyboard_runtime_on_active_track_changed()` (Z2/Keyboard runtime),
  - mutation `g_ui_track_state.active_track`,
  - `ui_core_sync_active_track_cfg_params()` (projection UI + sync paramètres) (`Src/UI/ui_core.c:992-998`).
- Side effects prouvés:
  - Z2: purge/reset state clavier/arp selon mode (`Src/Keyboard/keyboard_runtime.c:297-321`),
  - Z3: `param_store_set_active(...)` + `param_registry_sync_ui_for_active_track()` (`Src/UI/ui_core.c:960-985`, `Src/Param/param_store.c:83-90`, `Src/Param/param_registry.c:1469-1510`),
  - Z4 (lecture d'état): `seq_runtime_get_rec_count_in_mode`, `seq_runtime_get_tempo_bpm_milli`, `seq_runtime_get_clock_source`, `seq_runtime_get_rec_len_mode` (`Src/UI/ui_core.c:964-983`, `Src/Seq/seq_runtime.c:902-905`, `1187-1190`, `1206-1209`, `1222-1225`).

### 2) Changement de family/type track
- Entrées UI: `ui_set_track_family()`, `ui_set_track_type()` (`Src/UI/ui_core.c:2904-3000`).
- Noyau sync porté par UI:
  - `ui_core_sync_audio_runtime_enables()` -> `track_enable(...)` (Z2/audio runtime route enable) (`Src/UI/ui_core.c:929-953`, `Src/Audio/audio_float.c:497-512`),
  - `track_runtime_invalidate_all()` (Z2 runtime rebind différé) (`Src/UI/ui_core.c:2953`, `2991`, `Src/Core/track_runtime.c:385-388`),
  - si track actif: `keyboard_runtime_on_active_track_changed()` + `ui_core_sync_active_track_cfg_params()`.

### 3) Restore bulk de config tracks
- Entrée UI: `ui_restore_track_config_bulk(...)` (`Src/UI/ui_core.c:1081-1177`).
- Noyau sync porté par UI: `ui_core_restore_post_apply_sync_and_notify()` (`Src/UI/ui_core.c:1017-1023`).
- Ordre imposé:
  1. `track_runtime_invalidate_all()`
  2. `ui_core_sync_audio_runtime_enables()`
  3. `keyboard_runtime_on_active_track_changed()`
  4. `ui_core_sync_active_track_cfg_params()`
- Ce bloc est un point de sortie existant de sync système, déjà isolé localement.

## Contrat explicite du point unique interne
- Fonction interne: `ui_core_sync_system_on_track_context_change(const ui_core_system_sync_request_t *request)`.
- Etapes supportées (activables selon flags):
  1. callback clavier avant pivot track (si demandé),
  2. commit `active_track` (si demandé),
  3. runtime sync `invalidate` / `sync enables` (ordre sélectionné),
  4. callback clavier après runtime sync (si demandé),
  5. projection/sync params track actif (si demandé).
- Ordres runtime conservés:
  - `INVALIDATE_THEN_ENABLES`: restore bulk, track-type change.
  - `ENABLES_THEN_INVALIDATE`: track-family change.
  - Aucun runtime sync: pivot de track actif.
- Profils d'appel explicites (helpers internes):
  - `ui_core_make_sync_request_active_track_resync_only()`
  - `ui_core_make_sync_request_active_track_change(next_track)`
  - `ui_core_make_sync_request_restore_bulk()`
  - `ui_core_make_sync_request_track_family_change(active_track_touched)`
  - `ui_core_make_sync_request_track_type_change(active_track_touched)`
- Effet du durcissement:
  - cohérence inter-call-sites inchangée,
  - coupling/régression par permutation de flags réduit,
  - ordre d'exécution historique strictement conservé.

## Séparation UI pure vs orchestration/sync système

### UI pure (interaction/présentation)
- Détection gestures et double-tap: `ui_core_handle_track_hall_action()` (`Src/UI/ui_core.c:1273-1297`).
- Mirrors modificateurs: `ui_core_update_shift_state()`, `ui_core_update_track_modifier_state()`, `ui_core_handle_track_selection_event()` (`Src/UI/ui_core.c:1203-1220`, `1329-1371`).
- Navigation/page: `ui_page_set(UI_PAGE_TEMPLATE_CFG)` sur double-tap (`Src/UI/ui_core.c:1294-1297`).

### Orchestration/sync système (responsabilité non-UI stricte)
- `ui_core_sync_active_track_cfg_params()` (projection trans-zone + refresh registry) (`Src/UI/ui_core.c:955-985`).
- `ui_core_sync_audio_runtime_enables()` (mapping family->enable lanes runtime) (`Src/UI/ui_core.c:929-953`).
- `ui_core_set_active_track()` / `ui_core_apply_active_track_change()` (callback runtime clavier + pivot état + sync) (`Src/UI/ui_core.c:992-1015`).
- `ui_restore_track_config_bulk()` via `ui_core_restore_post_apply_sync_and_notify()` (invalidations/runtime+param sync ordonnées) (`Src/UI/ui_core.c:1017-1023`, `1081-1177`).
- `ui_set_track_family()` / `ui_set_track_type()` quand ils déclenchent invalidate/runtime enable/sync (`Src/UI/ui_core.c:2952-2958`, `2991-2996`).

## Dépendances hors Z5 (nommées seulement)
- Z2: `track_runtime_invalidate_all`, `track_enable`, `keyboard_runtime_on_active_track_changed`.
- Z3: `param_store_set_active`, `param_registry_sync_ui_for_active_track`.
- Z4: getters `seq_runtime_get_*` utilisés pour exposer l'état courant dans les params UI CFG.

## Plus petite prochaine passe utile (une seule)
- Passe réalisée dans ce chantier: micro-extraction locale effectuée via `ui_core_sync_system_on_track_context_change(...)`, reroutée depuis:
  - `ui_core_set_active_track`,
  - `ui_set_track_family`,
  - `ui_set_track_type`,
  - `ui_restore_track_config_bulk` (via `ui_core_restore_post_apply_sync_and_notify`).
- Objectif atteint: un contrat d'ordre unique explicite, sans changement de comportement utilisateur ni d'API publique.

## Vérification de clôture (locale)
- Relecture ciblée faite sur:
  - point unique `ui_core_sync_system_on_track_context_change(...)`,
  - helpers de profils `ui_core_make_sync_request_*`,
  - call sites reroutés (`ui_core_set_active_track`, `ui_set_track_family`, `ui_set_track_type`, `ui_restore_track_config_bulk` via helper),
  - voisins directs track/config dans `ui_core.c`.
- Aucun call site voisin n'impose un ordre runtime concurrent hors point unique (pas de nouveau chemin `invalidate/enables/callback` parallèle).
- Exceptions locales intentionnelles conservées:
  - `ui_set_track_midi_channel` / `ui_set_track_midi_source`: projection UI partielle via `param_store_set_active` pour la track active uniquement.
  - branches de rejet dans `ui_set_track_family` / `ui_set_track_type`: `ui_core_sync_active_track_cfg_params()` pour ré-aligner l'UI sans transition runtime.
- Ces exceptions ne concurrencent pas le contrat runtime du point unique; elles restent de la sync UI locale.

## Statut
- Sous-chantier Z5 "sync système portée par l'UI": clos pour l'instant.

## Reouverture locale (2026-04-15, passe suivante)

### Verdict court sur la frontiere reelle
- Frontiere d'extraction validee: le noyau minimal est le couple
  `ui_core_sync_system_on_track_context_change(...)` + `ui_core_system_sync_request_t`
  et ses profils `ui_core_make_sync_request_*`.
- Cette frontiere couvre 4 chemins seulement:
  - pivot track actif (`ui_core_set_active_track`),
  - changement family (`ui_set_track_family`),
  - changement type (`ui_set_track_type`),
  - restore bulk (`ui_restore_track_config_bulk` via helper post-apply).
- Les dependances hors Z5 restent strictement des dependances:
  - Z2: `track_runtime_invalidate_all`, `track_enable`, `keyboard_runtime_on_active_track_changed`
  - Z3: `param_store_set_active`, `param_registry_sync_ui_for_active_track`
  - Z4: `seq_runtime_get_rec_count_in_mode`, `seq_runtime_get_tempo_bpm_milli`,
    `seq_runtime_get_clock_source`, `seq_runtime_get_rec_len_mode`

### Separation explicite
- UI pur (reste dans `ui_core.c`):
  - detection gestes/modificateurs/double-tap et navigation page.
- Orchestration systeme extractible:
  - ordonnancement callback clavier / pivot track / runtime invalidate-enables / projection params.

### Decision de passe (ce tour)
- Decision: cartographie/frontiere seulement, sans nouvelle extraction fichier.
- Motif: le point unique est proprement isole en logique, mais encore lie a
  l'etat interne `g_ui_track_state` et a deux helpers statiques (`ui_core_sync_audio_runtime_enables`,
  `ui_core_sync_active_track_cfg_params`).
- Prochaine micro-passe utile candidate (separee): introduire un module interne Z5
  prive (non public) qui execute la requete de sync via callbacks fournis par `ui_core`,
  sans deplacer d'autorite vers Z2/Z3 et sans changer le comportement utilisateur.

## Micro-extraction locale realisee (2026-04-15, passe execution)

- Extraction effectuee hors `ui_core.c` vers module interne Z5 prive:
  - `Src/UI/ui_system_sync_internal.h`
  - `Src/UI/ui_system_sync_internal.c`
- Contenu extrait:
  - struct de requete (`ui_system_sync_request_t`),
  - profils nommes (`ui_system_sync_make_request_*`),
  - noyau d'orchestration (`ui_system_sync_apply_track_context_change`).
- `ui_core.c` conserve:
  - ownership de l'etat UI (`g_ui_track_state`),
  - projection UI non extractible telle quelle (`ui_core_sync_active_track_cfg_params`),
  - mapping enables runtime dependant de l'etat UI (`ui_core_sync_audio_runtime_enables`),
  - adaptateur prive minimal (`ui_system_sync_adapter_t`) pour binder le noyau extrait.
- Les 4 call sites restent reroutes avec ordre et side effects inchanges:
  - `ui_core_set_active_track`,
  - `ui_restore_track_config_bulk` (via helper post-apply),
  - `ui_set_track_family`,
  - `ui_set_track_type`.
- API publique inchangee (`Inc/UI/ui_core.h` non modifie).

## Durcissement contrat module prive (2026-04-15, passe suivante)

- Contrat d'entree explicite renforce dans `ui_system_sync_internal`:
  - les callbacks adapteur requis par les flags actifs sont obligatoires,
  - une requete invalide (callback manquant) est rejetee sans execution partielle.
- Aucun changement de comportement utilisateur attendu:
  - les 4 call sites existants fournissent deja un adapteur complet,
  - ordre et side effects restent identiques sur les chemins valides.
- Couplages restants assumes cote UI:
  - commit `active_track` (ownership `g_ui_track_state`),
  - projection params actifs,
  - mapping `track_enable` dependant de la configuration UI.

## Cloture locale (2026-04-15, passe finale)

- Verification locale: aucun chemin parallele de sync systeme n'a ete retrouve dans `ui_core.c` pour les transitions track/config ciblees.
- Les 4 call sites stabilises passent tous par le module prive:
  - `ui_core_set_active_track` -> `ui_system_sync_make_request_active_track_*` + `ui_system_sync_apply_track_context_change`
  - `ui_restore_track_config_bulk` (via helper post-apply) -> `ui_system_sync_make_request_restore_bulk` + apply
  - `ui_set_track_family` -> `ui_system_sync_make_request_track_family_change` + apply
  - `ui_set_track_type` -> `ui_system_sync_make_request_track_type_change` + apply
- Helpers voisins: aucun ordre concurrent/duplique `invalidate -> enables -> callback -> cfg sync` n'a ete trouve hors module prive.
- Statut: sous-chantier Z5 extraction noyau sync systeme clos pour l'instant.

