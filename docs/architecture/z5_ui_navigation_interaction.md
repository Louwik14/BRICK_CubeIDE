# Z5 - UI / Navigation / Interaction

## 1. Perimetre

Perimetre operationnel de zone (appartient a Z5):
- `Src/UI/ui_core.c`
- `Src/UI/ui_core_runtime_bridge.c`
- `Src/UI/ui_core_navigation_bridge.c`
- `Src/UI/ui_hall_mode_contract.c`
- `Src/UI/ui_hall_mode_flow.c`
- `Src/UI/ui_hall_mode_projection.c`
- `Src/UI/ui_hall_mode_state.c`
- `Src/UI/ui_hall_input_service.c`
- `Inc/Core/track_state.h`
- `Src/Core/track_state.c`
- `Inc/UI/ui_core.h`
- `Inc/UI/ui_core_runtime_bridge.h`
- `Inc/UI/ui_core_navigation_bridge.h`
- `Inc/UI/ui_hall_mode_contract.h`
- `Inc/UI/ui_hall_mode_flow.h`
- `Inc/UI/ui_hall_mode_projection.h`
- `Inc/UI/ui_hall_mode_state.h`
- `Inc/UI/ui_hall_input_service.h`
- `Src/UI/ui_track_catalog.c`
- `Inc/UI/ui_track_catalog.h`
- `Src/UI/ui_navigation.c`
- `Inc/UI/ui_navigation.h`
- `Src/UI/ui_template_page.c`
- `Inc/UI/ui_template_page.h`
- `Src/UI/pages/` (toutes les pages enregistrees par `ui_core_init`)

Elargissements necessaires (preuves de frontieres et contrats):
- `Src/UI/ui_page_manager.c` + `Inc/UI/ui_page_manager.h`: autorite page active et changement de page.
- `Src/UI/ui_event.c` + `Inc/UI/ui_event.h`: pipeline input bouton/hall -> queue events UI.
- `Src/UI/ui_system_sync_internal.c` + `Src/UI/ui_system_sync_internal.h`: noyau interne de sync systeme track/config (module prive Z5).
- `Src/App/Hall/hall_keyboard_bridge.c`: consommation de `ui_get_hall_mode` et suppression notes hall (`ui_core_hall_note_is_suppressed`).
- `Src/Core/brick6_app_init.c`: ordre d'appel runtime (`ui_core_service_track_selection_inputs` puis `hall_keyboard_bridge_process`).

Dependances de Z5 sans appartenir a Z5:
- Z2 `track_runtime` (track validity/bind pour mute, routing, labels).
- Z3 `param_registry/param_store` (UI cfg, copy/paste, edits params).
- Z4 `seq_runtime/seq_edit/seq_model` (transport, pattern mode, clipboard seq).
- Storage (`pattern_live_ram`, `undo_v1`) pour recall/store/undo.
- Keyboard runtime/hall engine pour comportements mode hall.
- Z6 `wav_loader` / `sample_pool` / `sd_preview` pour le browser Settings/Sampler: Z5 affiche les snapshots RAM et declenche seulement les operations SD explicites.
- Z6 `multi_sample_index` / `multi_sample_import` / `multi_sample_pool` pour le browser Settings/Sampler/Multi: Z5 affiche les dossiers instrument, prepare/load les indexes et expose `CLEAR` page 3 pour supprimer uniquement les `.brickmulti` directs du dossier courant apres confirmation.

Exclusions explicites:
- Rendu audio hard-RT (Z1) hors possession UI.
- Autorite param/modele seq hors UI (Z3/Z4), seulement pilotees depuis Z5.

Sous-roles concentres dans `ui_core.c`:
- Etat UI global courant (track, hall mode, feedback, states pattern/mute).
- Orchestration centrale des interactions (track select, transport, shortcuts, pattern mode).
- Orchestration explicite des contrats de sync (selection legere, sync edit-context, reconfig runtime).
- Arbitrage UI des demandes runtime, avec delegation des effets et commits a `ui_core_runtime_bridge`.

Policy catalogue track/type/labels:
- `ui_track_catalog` porte les regles de validite/disponibilite family/type et les labels associes.
- `ui_core` conserve l'etat UI live (selection, modes, feedback) tandis que `track_state` porte l'autorite par-track pour family/type/midi.

## 2. Autorite(s) de verite

Autorite etat UI courant:
- `g_ui_track_state` dans `ui_core.c`.
- APIs autoritatives: `ui_get_active_track`, `ui_set_hall_mode`, `ui_get_hall_mode`, `ui_get_*` state helpers.

Autorite page/ensemble actif:
- Page active: `ui_page_manager` (`g_ui_current_page_id`, `ui_page_set`, `ui_page_get`).
- Ensemble/subpage template active: `ui_template_page` (`ui_template_page_state_t.active_subpage`, `ui_template_family_resolve*`, `ui_template_page_select_subpage`).
- Selectionnabilite subpage template: `ui_template_page_is_subpage_selectable`.
  - Une subpage desactivee par le resolver local ou vide (`PARAM_COUNT` sur tous les slots et titre vide/`-`/`N/A`) n'est pas selectionnable.
  - `ui_template_page_normalize_active_subpage` garantit que `active_subpage` pointe vers une subpage selectionnable; si aucune subpage ne l'est dans l'ensemble, le fallback borne reste la page `0`.

Autorite track select:
- `ui_core_service_track_selection_inputs` + `ui_core_handle_track_hall_action` + `ui_core_set_active_track`.
- Changement track actif explicite localement:
  - `ui_core_set_active_track`: selection legere (focus UI) + sync context UI explicite.
  - le pipeline `ui_system_sync_internal` n'est pas utilise pour la selection legere de track.

Autorite hall modes:
- Chemin central: `ui_set_hall_mode` (validation transition + forced clears mute/pattern + callback keyboard runtime + commit mode).
- Les transitions mute/pattern passent par `ui_set_hall_mode`; pas de chemin local direct autoritatif concurrent.
- Triggers SHIFT+HALL dans `ui_core_handle_shift_hall_action`.
- Ancien mode hall natif `MACRO` neutralise:
  - aucun trigger hall (`SHIFT + HALL 15` ne cible plus MACRO),
  - `Macro CFG` reste enregistre pour ne pas decaler les IDs de pages, mais n'a plus de chemin utilisateur,
  - `UI_HALL_MODE_MACRO` reste un tombstone enum local non selectionne par contrat hall mode,
  - `hall_switch_mode` projet reste un tombstone de layout Z6, sans autorite UI active.
- Overlay MACRO transitoire:
  - trigger: `SHIFT + clic TRACK`,
  - si l'overlay est deja actif, `SHIFT + clic TRACK` alterne le sous-mode local `Ctrl` / `Assign`,
  - `Ctrl` reutilise le chemin Switch/pressure de `ui_macro_interaction`,
  - `Assign` reutilise le chemin Scene/Assign de `ui_macro_interaction`,
  - l'overlay momentane reste actif tant que `SHIFT` ou `TRACK` est maintenu,
  - `TRACK` maintenu + release/repress de `SHIFT` latch l'overlay,
  - `SHIFT + TRACK + HALL` est reserve a l'overlay MACRO et ne change pas la track focus,
  - en latch, `SHIFT + HALL` sans `TRACK` reste disponible pour selectionner tout hall mode valide, y compris le mode brut deja courant,
  - en latch, `TRACK + HALL` sans `SHIFT` garde la selection focus track-aware normale,
  - toute selection d'un autre hall mode sort du latch et reset `ui_macro_interaction`,
  - labels visibles: `M-Ctrl` en sous-mode `Ctrl`, `M-Assign` en sous-mode `Assign`.
- Le geste d'assignation MACRO vit dans `ui_macro_interaction`:
  - `SHIFT` absent,
  - `Scene` mode: maintien hall -> selection de la scene 0..15 -> capture encoder sans write live -> relâchement -> écriture/mise à jour d'un lock dans la scene projet,
  - chaque scene contient jusqu'a 32 locks; une scene pleine refuse l'ajout d'un nouveau lock sans parcours non borne,
  - `Scene` mode + `SHIFT` maintenu pendant un edit encodeur: suppression du lock `track+param` correspondant si present, no-op propre sinon,
  - le feedback template teste chaque parametre visible contre tous les locks de la scene maintenue; plusieurs params lockes visibles peuvent donc etre encadres simultanement,
  - pendant un maintien de scene MACRO, tourner un macro pot lie ce pot a cette scene sans appliquer le morph audio du pot,
  - `Switch` mode: maintien hall -> morph momentane base -> scene 0..15 selon la position hall; relâchement -> retrait de cette source,
  - `Switch` mode mappe la pression avec un detecteur local generique: seuils raw ON/OFF dedies avec hysteresis et marge au-dessus du bruit, amount `0..1` depuis le seuil pressure, sans reutiliser la profondeur brute KBD ni modifier les seuils KBD `trig_hi/trig_lo`,
  - pendant un maintien de scene MACRO, la grammaire visuelle réutilise le modèle p-lock: paramètre présent sur la page = cadre slot-lock inversé, sinon fallback LED orange sur l'ensemble cible,
  - état de capture purement transitoire, reset aux changements de hall mode ou de sous-mode overlay.
- Hors overlay MACRO actif, la surcouche MACRO ne prend pas le contrôle global des encodeurs ni du track-select.
- La capture MACRO ne s'active que pendant un maintien de scene en sous-mode `M-Assign`, puis se finalise au relâchement.
- `ui_macro_ui` n'est plus un owner de fait; les call-sites UI passent par `project_v1` pour le modele MACRO.
- `KEYBOARD` reste un mode brut normal.
- Le mode brut persiste en `ARP`; `ROUT` n'est jamais un mode brut stocke.
- ARP HOLD:
  - la config ARP reste par track via `keyboard_arp`,
  - le runtime ARP est hybride: clavier physique/UI global, etats ARP de jeu par track dans `keyboard_arp`,
  - chaque track possede ses notes tenues/latched, pending notes, notes emises, phase et timers,
  - un changement de focus UI ne vide pas les HOLD des autres tracks et le tick scanne les tracks actives,
  - un nouveau note-on ARP sur une autre track alimente seulement cette track, sans reprendre d'owner global.
- Le feedback LED MACRO lit directement `project_v1` pour l'etat vide/non vide des scenes et `ui_macro_interaction` pour la scene maintenue; la couleur de base vient d'une table stable scene -> couleur dans `led_rgb.c`, sans encoder les locks ni revenir au mapping pot/slot.
- L'overlay MACRO reutilise le meme renderer LED avec priorite sur le rendu track-select tant que l'overlay est actif: `M-Ctrl` affiche une intensite continue par scene, lissee cote LED depuis la profondeur Hall raw calibree avec un seuil LED dedie bas (`min + bruit + marge`) independant du seuil audio pressure, `M-Assign` suit le feedback Scene vide/faible, non-vide/normal, held/fort.

Autorite navigation boutons param:
- `ui_navigation_handle_event` (table `g_ui_nav_rules` data-driven).
- Quand `UI_PAGE_SETTINGS` est ouverte, la navigation globale par boutons param est neutralisee afin que les actions locales Settings/Sampler restent proprietaires de leurs evenements locaux.
- Disponibilité d'ensembles template:
  - `ui_navigation_is_page_available` combine:
    - contrat structurel Z2 (`track_runtime_is_ui_ensemble_available`),
    - résolution template locale (`ui_template_family_resolve_active_track`).
  - Z5 ne redécide plus seule la présence des ensembles principaux (`COLORS/TONE/MOD/MIX/PLAY/VCA`).

Autorite raccourcis interaction:
- `ui_core_handle_global_shortcuts`, `ui_core_handle_transport_event`, `ui_core_handle_seq_mode_event`, `ui_core_handle_pattern_mode_event`, `ui_core_mute_handle_event`.

Clipboard UI:
- `Src/UI/ui_core_clipboard.c` + `Inc/UI/ui_core_clipboard.h` (etat et handlers dedies).

Seconde autorite concurrente:
- Pas de seconde autorite concurrente sur l'etat UI courant; `ui_core` reste le point central.
- `ui_page_manager` est sous-autorite dediee a l'etat page, invoquee par `ui_core`/pages.

## 3. API entrantes

Entrees directes:
- `ui_core_init`, `ui_core_tick`, `ui_core_service_track_selection_inputs`.
- Appelants reels:
  - `ui_tasklet_poll` -> `ui_core_init/ui_core_tick`.
  - `brick6_app_process` -> `ui_core_service_track_selection_inputs`.
  - `main` ne pilote plus `ui_tasklet_poll` en 1:1 avec `engine_tick_count`: la cadence UI est sous-echantillonnee par un diviseur explicite et le rattrapage par boucle principale reste borne.

Entrees evenementielles:
- `ui_event_from_inputs` (buttons + hall) alimente queue lue par `ui_core_tick`.
- Les deltas encodeur sont pris sur un snapshot local du bank/track actif au debut du tick UI, pour que plusieurs edits arrives dans la meme passe restent deterministes si un edit reconfigure le contexte.
- Control All relatif:
  - quand `TRACK` est maintenu, un edit encodeur de base sur un parametre track-aware applique le meme delta demande aux autres tracks compatibles,
  - le point d'insertion reste `ui_param_handle_encoder_with_context` apres resolution du parametre/bank actif; Z3 reste l'autorite d'apply via `param_registry_apply_track_edit`,
  - le delta est relatif (`delta * step`) et chaque track clamp individuellement selon ses bornes effectives; une track active deja en butee n'empeche pas les autres tracks de bouger,
  - seules les tracks ou le meme `param_id` est autorise par `track_runtime_get_effective_param_status` et, pour `COLORS`/`TONE`/`PLAY`/`MOD`, resolu par `seq_param_iface` sont touchees,
  - exception SEQ per-track hors `track_runtime`: `PARAM_SEQ_LENGTH`, `PARAM_SEQ_DIV`, `PARAM_SEQ_QUANT`, `PARAM_SEQ_SWING` passent par l'autorite `seq_model_get/set_track_length` ou `seq_runtime_set/get_track_*` avec le meme delta UI (`delta * step`),
  - les params ARP sont des reglages par track portes par `keyboard_arp` sous forme de config par track; la lecture/ecriture UI passe par `keyboard_runtime_*_for_track`,
  - les tracks `Master`, les params globaux, les edits CFG structurels, les p-locks de steps et le live-rec p-lock restent exclus.
- `track_selection` reste hors table et execute en amont.
- `navigation` puis `active_page->handle_event` restent en fin de chaine.

Contrats implicites d'ordre:
- L'ordre `ui_core_service_track_selection_inputs()` puis `hall_keyboard_bridge_process()` (dans `brick6_app_process`) garantit que suppression hall est fixee avant emission notes clavier.
- Un `SHIFT + HALL` reconnu comme changement de mode/workflow est consommé au niveau central: le service direct marque le hall supprimé, puis le pipeline queue bloque le `UI_EVENT_HALL_PRESS` correspondant avant les chemins routing, seq, track-select, navigation et page locale.
- Dans `ui_core_tick`, la table de stages + blocage aval (equivalent `continue`) impose la priorite de consommation.
- `ui_navigation_handle_event` est volontairement execute avant `ui_page_get()->handle_event` pour que le meme event soit traite par la page active apres navigation.

## 4. API sortantes

Sorties vers UI interne:
- `ui_page_set`, `ui_page_get`, `ui_navigation_handle_event`, `ui_template_page_*`, `ui_param_set_bank`, `ui_param_handle_encoder`.

Sorties vers autres zones:
- Z2: `track_runtime_refresh_*`, `track_runtime_get_ctx`, `track_runtime_invalidate_all`, etc.
- Z2: `track_runtime_is_ui_ensemble_available` pour le gating de disponibilité d'ensembles.
- Z2: `track_runtime_resolve_track` pour la resolution explicite de cible filter depuis `ui_core`.
- Z3: `param_set`, `param_registry_apply_track_edit`, `param_registry_apply_track_structure_transition`, `param_registry_batch_*`, `param_store_set_active`.
- Z4: `seq_runtime_toggle_play_stop`, `seq_runtime_rec_toggle_arm`, `seq_runtime_set_track_div/quant/swing`, `seq_edit_*`, `seq_model_*`.
- Storage: `pattern_live_queue_slot`, `pattern_live_capture_to_slot`, `undo_v1_restore`.

Getters non-mutants vs mutables:
- Non-mutants: `ui_get_active_track`, `ui_get_track_*`, `ui_get_hall_mode`, `ui_get_pattern_stub_state`, `ui_get_mute_state`, `ui_get_mute_hall_led`.
- Mutables: `ui_set_track_family/type`, `ui_set_track_midi_channel/source`, `ui_set_hall_mode`, clipboard/pattern/mute handlers.

## 5. Etats structurants possedes

Etat global UI:
- `g_ui_track_state` (`ui_track_state_t`) dans `ui_core.c` pour l'etat UI pur.
- `track_state` dans `Src/Core/track_state.c` pour l'autorite par-track.
- Champs structurants: active track, shift, track_select_armed, hall_mode, mode/cfg tap timers, hall suppression, pattern state, feedback, mute state/buffers, armement prepare du hold quick mute.
- Etat track autoritatif externe: family/type/midi channel/source dans `track_state`.
- Ecritures: init + handlers d'events/selection.
- Lectures: getters UI, renderer template, hall keyboard bridge, logique shortcuts.

Etat clipboard:
- `g_ui_clipboard` (`ui_clipboard_state_t`) dans `ui_core_clipboard.c` avec sous-etats track/ensemble/page; placement `UI_SDRAM` car etat froid UI non audio et non DMA.
- Ecriture: copy handlers dans `ui_core_clipboard.c`.
- Lecture: paste/clear handlers dans `ui_core_clipboard.c`.

Etat page active:
- `g_ui_current_page_id` + `g_ui_pages[]` dans `ui_page_manager.c`.
- Ecriture: `ui_page_manager_init`, `ui_page_manager_register`, `ui_page_set`.
- Lecture: `ui_page_get`, `ui_page_get_id`, navigation/settings.

Etat browser Settings/Sample:
- `ui_page_settings` expose une racine `Settings > Sample` avec trois entrees: `Multi`, `RAM`, `Stream`.
- `Settings > Sample > Multi` reprend le browser split du pool projet `Sampler/Multi`, navigue dans les dossiers sous `0:/Multi/` et conserve `multi_sample_pool` comme autorite.
- `Settings > Sample > RAM` est maintenant la vue filtre `kind=RAM` du catalogue global. Elle reutilise le browser catalogue WAV, charge un WAV vers `sampler_ram_pool`, affiche les slots RAM READY/ERROR/EMPTY via `sample_global_pool`, et peut clear un slot RAM. Le playback produit associe est limite a `Sampler/RAM`, normal ou sliced via `Slice Count`.
- `Settings > Sample > Stream` reprend le browser split historique du pool `sample_pool`/STREAM utilise provisoirement par `Sampler/Stream`.
- Les headers `Stream`, `Multi` et `RAM` affichent le meme budget global de slots produit et de memoire page-cache slot-pool; les entrees restent des categories de browser, pas des budgets separes.
- Dans ces headers, `X/256` represente la capacite de slots actifs du catalogue global sample (`sample_global_pool`); `X/16MB` represente le budget memoire produit RAM/page-cache du meme pool. Un refus de chargement par saturation de slots fait flasher seulement `X/256`; un refus par budget memoire RAM/page-cache fait flasher seulement `X/16MB`. Le backend remonte la cause exacte (`GLOBAL_SLOT_FULL` vs `GLOBAL_BUDGET_FULL`/`RAM_POOL_FULL`), l'UI ne la devine pas apres coup.
- Les trois browsers sample sont des vues filtrees du catalogue global: Stream liste `kind=STREAM`, Multi liste `kind=MULTI`, RAM liste `kind=RAM`. Les actions utilisateur manipulent des slots globaux; les refs backend restent internes.
- Le browser RAM ne cree pas de parametre parallele: il charge/remplace/clear des slots globaux `kind=RAM`, dont `backend_index` pointe vers un slot interne `sampler_ram_pool`.
- Le selecteur TONE `PARAM_SAMPLER_SAMPLE` hors Multi edite un slot global actif. La selection n'est jouable en Stream que si ce slot global est `STREAM/READY` et pointe vers un backend `sample_pool` charge. Le backend Stream Classic couvre la capacite globale active courante.
- La page `Sampler/RAM` `TONE` affiche l'overview waveform deja portee par le slot `sampler_ram_pool`: elle ne scanne jamais le sample, ne declenche aucune construction, et dessine seulement les colonnes pretes/READY du cache min/max 124 colonnes associe au sample RAM. Aucun acces SD/STREAM ni autorite audio nouvelle n'est introduit par cette waveform.
- Dans cette waveform, `START`, `END` et `LOOP` sont des curseurs independants: chacun affiche sa propre valeur parametre. Modifier `START` ou `END` n'embarque pas `LOOP`; un `LOOP` hors plage fonctionnelle utilise `START` comme point de retour runtime effectif, mais l'UI continue d'afficher la vraie valeur `LOOP` avec clamp d'ecran seulement en mode non-slice.
- Les edits `START`, `END` et `MODE` depuis cette page sont audibles sans retrigger sur la voix RAM active du track; en `Loop`/`PingPong`, la tete est repliee dans la nouvelle plage valide plutot que stopper. `LOOP` reste audible sans retrigger seulement en mode non-slice; en mode slice, la valeur reste editable mais n'affecte pas les slices. Ce feedback live reste une projection runtime: les p-locks n'animent pas les curseurs et ne modifient pas la base visible.
- Les edits `TUNE` depuis cette page sont audibles sans retrigger sur la voix RAM active du track: `Slice Count=Off` conserve la transposition chromatique par note + `Tune`, tandis que `Slice Count!=Off` conserve note->slice et applique `Tune` comme pitch global de toutes les slices. Les p-locks/LFO/restore `TUNE` suivent le meme chemin runtime sans animer la valeur UI.
- Si `END <= START` ou si `LOOP` tombe hors region, l'UI ne corrige aucun curseur: seul le runtime construit une region effective minimale et replie la tete active sans tuer la voix.
- `LOOP` reste affiche comme repere editable des qu'un sample RAM valide existe en mode non-slice, y compris quand LOOP est OFF, et garde un style unique: ligne verticale pointillee XOR/inversee et lettre `L` sous le cadre. En mode slice (`brick6_sampler_runtime_ram_slice_mode_active(track) != 0`), le marqueur `L` est cache.
- `Slice Count` ajoute des divisions visuelles dans la waveform TONE: l'enum `Off, 2, 4, 8, 16, 32, 64` est mappe en `1, 2, 4, 8, 16, 32, 64` zones egales dans la plage `START/END`. Les separateurs internes sont des points XOR discrets dessines apres la waveform et avant les marqueurs `START`/`END`/`LOOP`; ils ne modifient ni overview, ni parametre.
- Clavier `Sampler/RAM`: `Slice Count=Off` garde le jeu chromatique/pitch existant; `Slice Count!=Off` transforme le clavier en selection de slices regulieres dans `START/END`, sans pitch par note. `TUNE` reste l'offset global commun a toutes les slices.
- La waveform `Sampler/RAM` affiche aussi un playhead runtime indicatif sans lettre, dessine en points XOR legers apres les divisions slice et avant les marqueurs. Il suit uniquement la voix RAM active du track selectionne pour le slot global affiche; les autres tracks/samples ne sont pas projetes.
- `ui_page_settings` conserve les listes UI froides en RAM (`sample_entries`, `multi_entries`) et navigue dans la vue Sample courante; le browser Multi relit seulement le dossier courant et scanne les entrees directes des dossiers candidats pour les classifier.
- Le catalogue WAV global vient de Z6 `wav_loader`; l'entree dans le browser Sampler ne rescane plus automatiquement. Elle charge la vue racine depuis le cache de vues Z6 ou depuis `0:/BRICK/SAMPLE.CAT`, ou affiche `REFRESH LIB` si le catalogue est absent/stale.
- Le browser Sampler intercepte les boutons physiques `BTN_PAGE_1..BTN_PAGE_4` uniquement dans `UI_SETTINGS_VIEW_SAMPLER`: `BTN_PAGE_1` = RETURN, `BTN_PAGE_2` sans SHIFT = OK sur l'entree sample courante, `BTN_PAGE_3` = reserve/no-op, `BTN_PAGE_4` = REFRESH sans SHIFT et REBUILD avec SHIFT. `BTN_COPY` et `BTN_PASTE` sont explicitement no-op dans le browser Sampler; les confirmations locales utilisent RETURN/OK. Hors browser Sampler, les boutons page gardent leur comportement normal de subpage.
- La navigation fichier/dossier du browser Sampler ne scanne jamais `0:/Samples`: page de dossier deja cachee = RAM-only; page non cachee = lecture depuis `SAMPLE.CAT`; `streaming_critical` + page absente = refus `SD BUSY`.
- Capacite catalogue Z6 courante: 9999 entrees persistantes. La vue locale Sampler est paginee par blocs de `WAV_LOADER_CATALOG_VIEW_MAX=256` lignes; un dossier de plus de 256 entrees reste navigable par chargements de pages depuis `SAMPLE.CAT`.
- Dans chaque dossier, l'entree virtuelle `..` est affichee en tete hors racine, puis les dossiers prefixes `> `, puis les fichiers WAV; `LIB FULL` reste reserve a la saturation globale du catalogue V1. Les paths trop longs ne vident pas la vue: l'UI affiche `PATH LONG` seulement au moment d'une action sur une entree dont le path depasse le contrat backend Sampler officiel (`SAMPLE_POOL_PATH_MAX=160`, aligne avec `SAMPLE.CAT` V2).
- Le footer du browser Sampler est decoupe en quatre zones egales alignees avec les boutons PAGE (`RETURN | OK | - | REFRESH/REBUILD`). La barre verticale centrale reste un rendu RAM-only et affiche un tiret de position selon le focus courant: `sample_selected/sample_child_count` cote bibliotheque ou index filtre de slot global / nombre de slots visibles cote slots.
- Apres REFRESH/REBUILD Sampler, Z5 tente de restaurer le dossier courant par path catalogue, puis l'entree selectionnee; si le dossier a disparu, il remonte au parent existant le plus proche, sinon racine. Un refus SD/streaming garde l'emplacement courant et affiche `SD BUSY`.
- Quand le refus vient du `sd_access_gate`, le feedback Settings/Sampler qualifie le bloqueur (`SD STREAM`, `SD CACHE`, `SD PREV`, etc.) a partir du diagnostic Z6 sans lancer de scan ni retry bloquant.

Etat template families/subpages:
- `g_ui_template_family_registry[...]` dans `ui_template_page.c`; placement `UI_SDRAM` car registry metadata UI non audio et non DMA.
- Etat courant par page template: `ui_template_page_state_t` (dans chaque page concrete `Src/UI/pages/*`).
- Ecriture: register families, `ui_template_page_select_subpage`, `ui_template_page_normalize_active_subpage`, enter handlers.
- Lecture: renderer template, clipboard active page.

Etat queue events UI:
- `g_ui_evt_q`, `g_ui_evt_w`, `g_ui_evt_r`, `g_ui_hall_prev_pressed` dans `ui_event.c`.
- Ecriture: `ui_event_from_inputs`.
- Lecture: `ui_event_pop` dans `ui_core_tick`.

Frontiere commit runtime UI:
- `ui_core_runtime_bridge` porte les effets runtime, la transition track structurelle et les callbacks de commit explicitement appeles depuis `ui_core`.
- `ui_core_runtime_bridge` porte aussi le post-commit visible UI: miroir active-track, sync edit-context, mirror MIDI actif et reconfiguration post-structure.
- Pour un changement local family/type, le bridge construit une requete system-sync ciblee track et appelle le pipeline Z3 local; il n'invalide pas tout le runtime et n'invalide pas tous les caches LFO.
- Les restore/load/bulk gardent la requete globale et le pipeline global.
- `ui_core_runtime_bridge` porte aussi les lectures runtime encore consommees par l'UI centrale (`seq_edit_step_hold_update`, `track_runtime_resolve_track`, `seq_edit_get_page`, `keyboard_runtime_get_octave_shift`, `pattern_live_*`) afin de garder `ui_core.c` du cote arbitrage.
- `ui_hall_mode_contract` porte la table de contrat hall mode centrale (trigger, page cible, label brut).
- `ui_hall_mode_flow` porte la politique d'activation hall et le double-tap/ciblage associe.
- `ui_hall_mode_projection` porte la vue effective, les labels et les helpers de lecture/injection du domaine hall.
- `ui_hall_mode_state` porte l'autorite brute du hall_mode et les transitions d'ecriture.
- `ui_hall_input_service` porte le chemin hors queue hall/transpose et delegue les effets aux helpers de domaine.
- `ui_core.c` conserve l'arbitrage et la decision; le bridge porte l'execution des actions runtime et la sync post-commit.

Frontiere navigation UI:
- `ui_core_navigation_bridge` porte les requetes de page et le dispatch navigation depuis `ui_core`/`ui_edit_context_sync`.
- `ui_core.c` ne porte plus les details des requetes de page cible ni le sync contextualise de navigation.
- `ui_hall_mode_contract.c` centralise la metadonnee de mode hall et les helpers de contrat associes.
- `ui_hall_mode_flow.c` centralise la transition/activation des hall modes, en gardant `ui_core` au niveau orchestration.
- `ui_hall_mode_projection.c` centralise la projection visible hall, sans couture dans `ui_core`.

## 6. Flux runtime

Flux nominal prouve:
1. Entree input UI
- Buttons/hall lus dans `ui_event_from_inputs` + `ui_core_service_track_selection_inputs` (path direct track-select/shift/hall).

2. Resolution navigation / raccourci
- Dans `ui_core_service_track_selection_inputs`: SHIFT+HALL => mode trigger; TRACK_MOD+HALL sans SHIFT => active track.
- Contrat explicite track halls sous `TRACK`:
  - `TRACK + HALL` ne change la track que si `SHIFT` est absent.
  - en overlay MACRO latche, `TRACK + HALL` sans `SHIFT` conserve ce contrat de focus track-aware.
  - `SHIFT + TRACK + HALL` reste consomme par l'overlay MACRO.
  - `TRACK` + tap track seul = focus/select track-aware historique uniquement.
  - `TRACK` + simple tap ne demande jamais `CFG`; l'ouverture `CFG` sous `TRACK` reste reservee au double tap explicite sur la track deja focus.
  - la mutation de chaine de voix ne s'arme que si une autre hall track est deja maintenue au moment du nouvel appui:
    `TRACK` maintenu + master candidate maintenue + target press.
  - sans seconde hall maintenue, aucun role `Solo/Master/Slave` ne change.
  - le geste groupe conserve les validations locales existantes: ajout contigu a droite, retrait uniquement sur la derniere slave, refus sans auto-fill ni mutation.
  - ajout special sur target `Off`: avant de devenir `Slave`, la target recoit une copie ponctuelle de l'etat instrument/per-track de la master candidate (family/type, config MIDI et params track-aware hors domaine `PLAY`), sans copie de sequence/trigs/steps/plocks `PLAY`.
- Contrat specifique `MACRO`: l'ancien `SHIFT+HALL15` ne cible plus de mode; l'acces utilisateur passe par l'overlay `SHIFT+TRACK`.
- Grammaire visuelle halls en overlay `MACRO`:
  - les 16 halls adressent les 16 scenes et chaque scene a une couleur stable dediee,
  - `M-Assign`: scene vide=couleur dediee faible, scene non vide=couleur dediee normale, scene maintenue=couleur dediee forte,
  - `M-Ctrl`: scene vide/non vide garde la meme identite couleur; l'intensite LED interpole continument de l'intensite de repos vers fort selon une profondeur Hall raw calibree, avec seuil LED dedie au-dessus du bruit, lissage local LED, montee rapide et descente douce, sans blink ni lecture de `hall_engine_get_value()` brut,
  - les Hall LEDs n'affichent jamais les 32 locks d'une scene, ni leur nombre, ni leur type.
- Dans `ui_core_tick`: resolution priorisee mute/transport/shortcuts/pattern/seq/navigation.

3. Mutation etat UI
- Mutations de `g_ui_track_state` (hall_mode, track_select_armed, active_track, pattern/mute/feedback).
- Noyau de sync systeme track/config factorise dans `ui_system_sync_internal` (profils `ui_system_sync_make_request_*` + apply unique) pour 3 chemins runtime: family change, type change, restore bulk.
- Durcissement du module prive: une requete de sync invalide (callbacks adapteur requis manquants) est rejetee sans execution partielle.
- Les edits `CFG_TRACK` / `CFG_TRACK_TYPE` passent maintenant par le meme corridor structurel complet que `restore bulk`:
  capture des mix targets -> mutation dans le pipeline -> rebind lanes -> reapply lane-bound -> neutralisation runtime -> sync UI structurelle.
- Pour une mutation structurelle `CFG_TRACK` / `CFG_TRACK_TYPE` / restore bulk, Z5 delegue le corridor a Z3 via `param_registry_apply_track_structure_transition(...)`; la resync UI active-track reste explicite cote Z5 (`ui_param_sync_active_track_mirror_from_runtime` puis `ui_param_sync_active_bank_values`) apres finalisation runtime Z3.
- Les valeurs visibles des params track-aware passent par `ui_param_get_active_track_display_value`: la lecture relit l'autorite track-scoped (`param_registry_get_track_value`, donc `track_tone_sound_state` pour TONE engines) et `param_store.active[]` reste seulement un miroir/fallback UI, jamais la verite d'affichage entre deux tracks.
- Pendant le playback, un p-lock sequenceur ne change pas cette source d'affichage: l'UI continue de lire la base canonique editable, tandis que la valeur lockee reste une projection runtime temporaire cote Z3/Z4. Aucun redraw/invalidation UI ne doit etre declenche par le passage d'un step p-locke hors feedback d'edition explicite.
- Restore bulk track config:
  - validation snapshot all-or-nothing,
  - ecriture de `track_state` comme autorite par-track,
  - pipeline post-apply localise: `ui_core_post_restore_global_sync()`
    (`ui_system_sync_make_request_restore_bulk` -> `ui_system_sync_apply_track_context_change` -> `ui_active_track_sync_after_track_structure_change(1)`).

4. Resolution contextuelle page/ensemble
- `ui_navigation_handle_event` mappe boutons param -> page cible selon disponibilite track-family/type.
- `ui_template_page` resout family/subpage active et banque param associee.
- Contrat subpage selectionnable:
  - visible dans une famille/template ne signifie pas selectionnable si la subpage est vide ou desactivee,
  - les boutons de page ne peuvent pas selectionner une subpage vide,
  - l'entree/reload d'ecran, le tick template, la sync active-track et les changements de famille/resolver normalisent le focus vers une subpage selectionnable,
  - les contextes dynamiques (`COLORS` EQ3/ADSR, `MIX 2/2` delay CLASSIC/DUAL, pages moteur/type track-aware) passent par cette normalisation centrale apres recomposition de leurs familles.
  - page/template ARP: `ui_page_template_arp_resolve_family` lit `ui_hall_mode_resolve_effective_view(...)` pour choisir ARP vs ROUT,
  - label mode hall: `ui_get_hall_mode_short_label` et suffixe s'appuient sur `effective_view`.
  - aucun template Buffer ni sous-page TONE Buffer ne reste actif.
  - aucun nouveau hall mode ni deplacement de `ROUT`.

5. Appels vers param/runtime/seq/storage
- Cas speciaux locaux `ui_core` (non resolver):
  - `ui_core_handle_transport_event`: shortcut REC/CLEAR buffer sous conditions (`track_select_armed` + track buffer unique).

6. Feedback / consommation aval
- Feedback texte via `ui_core_set_feedback` visible dans header track.
- Le header template affiche la charge CPU moyenne uniquement; il ne lit plus de VU/peak/clip meter audio.
- `hall_keyboard_bridge` consomme hall mode + flags suppression pour autoriser/bloquer emission note hall.
- Gardes runtime explicites en contexte `ROUT`:
  - `hall_keyboard_bridge_process` utilise `ui_hall_allows_injection(...)`,
  - `keyboard_runtime_tick` utilise `ui_hall_uses_arp_engine(...)` ou un HOLD ARP par-track actif pour continuer le tick hors focus,
  - `keyboard_input_note_on/off/all_notes_off` s'aligne sur `ui_hall_uses_arp_engine(...)` + `effective_view`.

## 7. Contraintes RT/CPU/memoire

Contraintes observees:
- Aucun malloc dans Z5; etats statiques.
- Traitement UI event-driven/control-rate (main loop/tasklet), pas hard-RT IRQ audio.
- File events taille fixe (`UI_EVENT_Q_LEN=32`) avec drop silencieux si pleine.
- Le service `ui_core_tick` n'est plus cadence a chaque tick moteur audio: la boucle principale agrège les ticks moteur et ne laisse passer qu'un tick UI sur N, avec budget de rattrapage borne par tour de superloop, afin de garantir du temps CPU au rendu/flush OLED.

Dependances de cadence:
- `ui_core_service_track_selection_inputs` doit tourner regulierement pour detection taps/double taps/armed states.
- `ui_core_tick` doit etre appele regulierement pour drainer queue events et tick page active.

## 8. Invariants a ne pas casser

Invariants prouves:
- Autorite unique etat UI courant: `g_ui_track_state` centralise dans `ui_core.c`.
- Autorite unique etat track: `track_state` centralise family/type/midi en dehors de `ui_core.c`.
- Une subpage template vide ne doit pas rester focus si une subpage selectionnable existe dans le meme ensemble.
- Tout changement de contexte track/type/mode qui peut modifier les subpages selectionnables doit repasser par `ui_template_page_normalize_active_subpage` via enter/tick/sync active-context ou appel equivalent.
- Mutation du mode brut: `ui_set_hall_mode` est l'unique mutateur de `hall_mode`.
- `effective_view` reste une projection read-only; aucune ecriture persistante.
- Resolution contextuelle track-aware: disponibilite pages/types depend de family/type de la track active (`ui_navigation_is_page_available`, `ui_template_family_resolve_active_track`).
- Priorite SHIFT/HALL sur track-select en service input: condition explicite `shift_down !=0` et `track_select_armed==0` avant trigger mode hall.
- Changement hall mode: chemin central via `ui_set_hall_mode` avec side effects explicites (mute/pattern inclus).
- Contrat d'ordre `ui_core_tick` explicite en code: `track_selection` amont, stages consommants ordonnes, puis `navigation` -> `active_page->handle_event`.
- `active_page->tick()` reste un tick de page local, pas un mecanisme de resync active-track.
- Contrat d'ordre stabilise: `global_shortcuts` est prioritaire sur `pattern/seq/navigation/page` pour un event consomme.
- Navigation et logique runtime ne sont pas separees strictement: `ui_core` appelle directement seq/param/track_runtime/storage.
- Cohabitation hall modes / ensembles UI maintenue par resolver family/subpage et label suffix selon mode.
  - page/label/template lisent la meme projection,
  - gardes runtime keyboard/hall consomment les helpers centraux.
- Contrat d'ordre utile confirme: `ui_core_service_track_selection_inputs` precede `hall_keyboard_bridge_process`; sur les cas frontiere audites, pas de fuite hall/note/arp prouvee.

## 9. Dependances inter-zones

Entrees vers Z5:
- Hall/buttons/encoders hardware via `ui_event` + polling boutons/hall.

Sorties de Z5:
- Z2 Track Runtime: invalidation/refresh/routing info.
- Z3 Param/Control: ecritures globales et track-aware, sync UI active.
- Z4 Seq/Clock: transport, edit seq, pattern operations.
- Z6 Storage: recall/store pattern, undo.

## 10. Dette technique observee

Points factuels:
- `ui_core.c` reste central pour l'UI, mais ne porte plus l'autorite par-track.
- `ui_core_runtime_bridge.c` concentre la couche d'execution UI -> runtime pour les mutations track, transport, shortcuts et remaps de commit.
- `ui_core_runtime_bridge.c` concentre aussi le post-commit UI associe aux changements structurels, au miroir de la track active, et aux lectures runtime encore utilisees par l'UI centrale.
- `ui_core_navigation_bridge.c` concentre la policy de requete/navigation appliquee depuis l'arbitrage UI.
- Logique de navigation distribuee entre `ui_navigation`, `ui_template_page`, et cas speciaux dans `ui_core`.
- Couplage fort a `param_registry`, `seq_*`, `pattern_live_*` depuis Z5; les lectures runtime restantes passent maintenant par `ui_core_runtime_bridge`.
- Dependance implicite a l'ordre d'appel superloop (`service_track_selection_inputs` avant `hall_keyboard_bridge_process`) pour suppression hall coherent.
- Le recorder SD/stems legacy n'est plus un cas special UI: un futur record SD devra passer par le contrat multi-client, sans reactiver l'ancien workflow start/stop/arm.
- Fragilites restantes prouvees:
  - priorites de consommation toujours tres centralisees dans `ui_core_tick` (desormais explicites via table locale),
  - contrat hors queue (`ui_core_service_track_selection_inputs`) restant critique pour la coherence mode/track avant bridge hall.

## 11. Impact eventuel sur la cartographie globale

- Z5 est confirmee comme zone d'orchestration interactionnelle centrale, avec sous-composants internes: event queue, page manager, template resolver.
- `ui_page_manager` et `ui_event` doivent etre rattaches explicitement a Z5 dans la carte globale (pas des utilitaires neutres).

## 17. Contrat UI Master/FX
- `Master/FX` est un type de la family `Master` expose en `CFG`.
- `TONE` expose 4 pages de 4 slots:
  - `FX1`: `FX1`, `LVL`, macro A, macro B
  - `FX2`: `FX2`, `LVL`, macro A, macro B
  - `FX3`: `FX3`, `LVL`, macro A, macro B
  - `FX4`: `FX4`, `LVL`, macro A, macro B
- Les labels visibles des macros A/B changent selon le type FX: OFF `---/---`, DRIVE `TONE/SHAPE`, CRUSH `BITS/RATE`, PUMP `RATE/REL`, CHOP `RATE/SHAPE`, WOBBLE `RATE/DEPTH`, COMB `TUNE/FB`, RING `FREQ/COLOR`, STUTTER `SIZE/RATE`, FREEZE `TIME/HOLD`, COLOR `AMT/FOCUS`.
- `RING COLOR` expose quatre positions nettes `SIN/TRI/SQR/DIRT`.
- Les valeurs visibles de `LVL` et des macros A/B sont formatees par la page TONE selon le type FX courant et le mapping DSP reel, sans modifier le stockage `0..127` ni les plages DSP.
- Pour `STUTTER`, `LVL` est un controle UI on/off: `OFF` si la valeur brute vaut `0`, `ON` si elle vaut `>0`. Le stockage reste `0..127`; le DSP interprete `0` comme audible OFF avec historique actif, et toute valeur `>0` comme audible ON full wet.
- Le selecteur `TYPE` Master/FX saute `STUTTER` si un autre slot Master/FX l'utilise deja; `STUTTER` reste selectionnable uniquement par le slot owner courant ou par le premier slot libre de cette ressource unique.
- Les macros Master/FX labelisees discretes sont editees par steps UI locaux dans `ui_param`: l'encodeur convertit step discret vers valeur raw canonique `0..127`, puis le chemin param track-aware standard applique la valeur.
- `LVL` garde le rendu parametre standard du template pour les autres MacroFX. Exception locale: `STUTTER LVL` remplace le potard par un switch et quantifie l'edition encodeur en OFF/ON; les macros A/B gardent leur rendu contextuel existant.
- `ARP` brut est projete en `ROUT` pour Master/FX. L'etat ROUT Master/FX est UI-only local et ne modifie pas le routing audio.
- Le hall de la track `Master/FX` active est affiche en vert fonce comme destination courante et son toggle est ignore.
- Les series DSP Master/FX cablees en Z1 sont `DRIVE`, `CRUSH`, `RING`, `CHOP`, `PUMP`, `COMB`, `WOBBLE`, `FREEZE`, `STUTTER` et `COLOR`. Les labels UI existants restent l'autorite visible de mapping A/B; `ECHO`, `FILTER`, `REVERB` et `REVERSE` restent absents de la grammaire MacroFX.


## 12. Contrat MIDI UI v1 (canonique)
- Nouvelle source UI explicite: `UI_TRACK_FAMILY_MIDI` + `UI_TRACK_TYPE_MIDI` dans `ui_core`.
- Exposition navigation borne pour une track MIDI:
  - exposes: `PLAY`, `MOD`, `TONE`, `CFG`,
  - non exposes: `COLORS`, `MIX`, `VCA`.
- `CFG` reste l'autorite UI pour le channel MIDI (`PARAM_CFG_MIDI_CH`) ; aucun deplacement vers `TONE`.
- Resolution template contextuelle MIDI:
  - `TONE` utilise une famille template MIDI dediee (`PROG`, `CC1`, `CC2`, `CC3`) sans fallback audio Synth,
  - page `PROG`: `PARAM_MIDI_PROGRAM`,
  - pages `CCX`: `PARAM_MIDI_CC1_1..PARAM_MIDI_CC3_4`.
- Contrat runtime/UI associe:
  - track MIDI non audio-routable (pas de cible mix/filter audio),
  - Z5 reste source family/type; Z2 reste autorite unique du bind runtime.
- Contrat KBD/canal partage:
  - le KBD interne emet sur le canal MIDI de la track focus/play-owner,
  - le routage note interne est canal-aware: toutes les tracks moteur ou `Input/Hybrid` en source `INT`/`ALL` qui partagent ce canal recoivent note-on/note-off,
  - ce routage reutilise le meme dispatch track-aware que l'entree MIDI externe, avec dedoublonnage par owner de voice group pour eviter un double trigger de la source.
- Contrat Omnichord:
  - le mapping physique des 8 boutons d'accord est Orchid-compatible: `Dim`, `Min`, `Maj`, `Sus`, puis `6`, `m7`, `M7`, `9`;
  - les Secret Chords Orchid sont resolus dans le dictionnaire clavier avant la fusion additive generique;
  - le label court du mode `KEYBOARD` affiche le nom ASCII de l'accord Omnichord actif quand une fondamentale est maintenue, sinon il reste `KBD`;
  - le label represente l'accord theorique construit avant quantification de gamme; les notes emises peuvent ensuite etre quantifiees si `Chord Override` est inactif.
  - l'etat sounding du clavier suit les notes MIDI effectivement envoyees, dedupliquees par pitch; a chaque changement note/chord/override, le delta compare ce set reel et eteint explicitement toute note absente du nouveau set.
  - en accord Omnichord, la root active est la derniere touche root pressee encore maintenue; si elle est relachee, le clavier revient a la root maintenue precedente.
  - relacher une extension revient a l'accord restant si une base `Dim/Min/Maj/Sus` et une root restent maintenues; les extensions seules restent silencieuses.
  - la sortie de `KEYBOARD` envoie les note-off locaux du clavier avant de nettoyer l'etat interne; les clears silencieux restent reserves aux sync de focus ou resets deja proprietaires du contexte.

## 13. Contrat Hybrid UI v1 (borne)
- `Hybrid` n'est pas une nouvelle family: `family=Input1..4`, `type=Hybrid`.
- Exposition UI pour `Input/Hybrid`:
  - exposes: `PLAY`, `MOD`, `TONE`, `VCA`, `COLORS`, `MIX`, `CFG`.
- `TONE` Hybrid utilise une famille template dediee (pas fallback Synth):
  - page `PROG`: `Gate` + `Program`,
  - pages suivantes: `CC1`, `CC2`, `CC3`.
- `PLAY` est explicitement navigable pour `Input/Hybrid`.

## 13.b Contrat COLORS UI
- L'ensemble `COLORS` conserve uniquement les pages filtre utiles:
  - `MAIN`: `F Type`, cutoff/low, resonance/mid, `EG Amt`/high selon type de filtre,
  - `ADSR`: `Atk`, `Dec`, `Sus`, `Rel` uniquement pour les filtres biquad avec envelope,
  - les slots 3 et 4 restent vides (`-`).
- L'ancienne page 4 `CRUNCH` est retiree et ne doit plus exposer `Drive`, `Bits`, `Rate` ni `Rate2`.
- Aucun fallback de page ou preset CRUNCH n'est conserve.

## 14. Contrat Sampler v0
- `UI_TRACK_FAMILY_SAMPLER` est exposee en `CFG`.
- `UI_TRACK_TYPE_RAM` est l'alias produit de `UI_TRACK_TYPE_ONE_SHOT`/`UI_TRACK_TYPE_SAMPLER`, valeur persistente legacy conservee comme type canonique de cette famille.
- `UI_TRACK_TYPE_STREAM` est l'alias produit de `UI_TRACK_TYPE_CLIP`, valeur persistente legacy conservee comme type distinct dans la meme famille.
- `UI_TRACK_TYPE_MULTI` est expose comme type produit distinct dans la meme famille; le workflow de gestion/import du pool projet passe par `Settings > Sample > Multi`.
- Les anciens labels/types UI `TB3` et `DX7` ne sont plus exposes ni conserves comme compat catalogue.
- `UI_TRACK_TYPE_STREAM`/`UI_TRACK_TYPE_CLIP` est borne a `BRICK6_MAX_CLIP_TRACKS=4` tracks simultanees: si 4 tracks sont deja `Stream`, le catalogue `CFG` cesse de le proposer aux autres tracks, tout en le laissant visible/editable pour une track deja `Stream`.
- Le rendu UI complet du Sampler expose maintenant deux pages Tone de base:
  - `PLAY`: `Sample`, `Mode`, `Start`, `End`,
  - `LOOP`: `Gain`, `Tune`, `Loop`, `Slice`.
- Les modes produits exposes pour `RAM` sont bornes a `Shot`, `RevShot`, `Loop`, `PingPong`.
- Le rendu UI `Stream` expose quatre pages Tone dediees:
  - `PLAY`: `Sample`, `Gain`, `Src BPM`,
  - `STRM`: `Play Mode`, `Loop`, `Stretch`, `Tune`,
  - `SYNC`: `Sync Len`,
  - `STR`: `Grain`; `Hop` et `Search` restent reserves/non exposes produit; `Search` n'est plus une destination LFO valide.
- `Stretch=Off` lit le stream a vitesse/pitch d'origine sans tempo-sync ni moteur stretch.
- `Stretch=Speed` conserve le varispeed courant; `Stretch=Shifter` conserve le cursor Speed puis applique le shifter stereo local.
- En `Shifter`, `Grain` pilote la taille de fenetre; `Tune` est le libelle produit du pitch stream interne; `Hop` et `Search` restent stockes mais non exposes et sans effet DSP.
- `STR` utilise les valeurs bornees `Grain = 384/512/768/1024/1536/2048`, avec default `Grain=1536`.
- Le rendu UI `Multi` expose une page TONE minimale:
  - `INST`: selecteur local parmi `NONE` et les instruments deja presents dans le `multi_sample_pool`, sans scan SD, import, reload ni browser; l'edition assigne seulement l'id instrument a la track `Sampler/Multi` active,
  - `GAIN`: edition du gain Multi runtime,
  - `LOOP`: edition `OFF/ON` du bool track-aware `PARAM_SAMPLER_MULTI_LOOP`, sans reutiliser le parametre Stream.
- Le clavier live `Sampler/Multi` reutilise le dispatch track-aware `keyboard_engine`: note-on appelle le trigger Multi runtime de la track, note-off appelle `brick6_sampler_runtime_note_off_multi_track_note(track,note)` pour raccorder les voix Multi au lifecycle VCA existant.
- `VCA` n'est pas expose pour `Sampler/Stream`; le niveau utilisateur passe par `MIX/Level`.
- La rotation du parametre `Sample` dans `TONE` met seulement a jour l'etat runtime, sans preview audio implicite.
- `Slice` / `RevSlice` restent en compat legacy interne uniquement, hors navigation produit `RAM`.
- `Settings > Sample > Stream` porte la preecoute SD manuelle via le flux `PREVIEW / STOP` pour le pool STREAM de `Sampler/Stream`.
- La preecoute s'arrete au changement de selection, au retour/back, et avant `Load/Replace`.
- `Load/Replace` reste l'autorite d'import vers le pool projet; la preview reste hors slots projet.
- Les etats visibles de slot Sampler suivent `sample_pool_get_state()`:
  - `LOADED` pour `READY_FULL`, `READY_PARTIAL` jouable et `PLAYING`,
  - `PREP` pour `PREPARING`, `PREFILLING`, `DONE`, `UNDERRUN`, `NEEDS_REPREPARE`,
  - `ERROR` pour une faute cache/SD/format,
  - `MISSING` reserve au catalogue/path absent ou fichier manquant au load/restore.
- Invariants conserves:
  - pas de seconde autorite UI,
  - pas de refonte de page,
  - pas de nouveau flux de navigation autonome.
- Compat UI/restore:
  - un ancien couple `Synth/Sampler` est remappe vers `Sampler/RAM`,
  - la famille `Synth` propose `Wave`,
  - `Wave` peut être sélectionné sur plusieurs tracks `Synth` (dans la limite runtime `BRICK6_BRAIDS_MAX_INSTANCES`).



## 14.b Contrat UI Wave
- `Synth/Wave` reste dans l'ensemble `TONE`, sans UI Mutable originale ni mode global dédié.
- La famille template `TONE` Wave expose exactement 8 params dans l'ordre runtime:
  - `EDIT`, `FINE`, `COARSE`, `FM`, `TIMBRE`, `MODULATION`, `COLOR`, `PHASE RESET`.
- Le layout UI suit deux sous-pages:
  - `EDIT`: `EDIT`, `FINE`, `COARSE`, `FM`
  - `TONE`: `TIMBRE`, `MODULATION`, `COLOR`, `PHASE RESET`
- L'ordre visible doit rester aligné avec `track_runtime_tone_slot_to_param()` / `track_runtime_tone_param_to_slot()` pour le type runtime `Wave`.
- Le clavier live réutilise le même seam track-aware que le scheduler:
  - `note on/off` Wave passent par `keyboard_engine` puis `brick6_braids_runtime`
  - aucun chemin UI local parallèle n'est autorisé pour le jeu de notes.
- `PHASE RESET=Off` conserve le comportement historique; `On` force une sync phase au premier sample rendu apres note-on pour les moteurs Wave sensibles a `sync_block`, sans reset random.


## 14.c Contrat MIX send2 delay
- La page template `MIX` conserve la page track-aware `MIX`: `Level`, `Pan`, `Send1`, `Send2`.
- L'ensemble `MIX` est scinde en deux sous-ensembles locaux UI-only:
  - appui `MIX` depuis un autre ensemble: ouvre `MIX 1/2`, subpage `MIX`,
  - appui `MIX` depuis `MIX`: alterne `MIX 1/2` / `MIX 2/2`,
  - le changement de sous-ensemble conserve la subpage active si elle reste disponible, sinon revient a `0`.
- `MIX 1/2` expose:
  - `MIX`: `Level`, `Pan`, `Send1`, `Send2`,
  - `REVB`: `Wet`, `Size`, `Decay`, `PreD`,
  - `REV2`: reservee/vide,
  - `REV3`: `HPF`, `LPF`.
- La page active MIX n'expose plus `REV2/Type`; `PARAM_MIX_REVERB_TYPE` reste reserve en stockage avec `0/RevB`, et `RevB` est l'unique reverb SEND runtime.
- Les params delay globaux sont exposes dans `MIX 2/2`, sans nouveau mode UI.
- `Send2` reste le niveau par track vers le delay global; `VOL` reste le niveau global de retour wet master et `REV` le send wet delay vers la reverb globale.
- Le delay global expose une surface `MIX 2/2` contextuelle selon `TYPE`:
  - CLASSIC `DLY1`: `TYPE`, `TIME`, `X`, `VOL`,
  - CLASSIC `DLY2`: `HPF`, `LPF`, `REV`, `FDBK`,
  - DUAL `DLY1`: `TYPE`, `TIME`, `MODE`, `VOL`,
  - DUAL `DLY2`: `HPF`, `LPF`, `REV`, `FDBK`,
  - DUAL `DLY3`: `TIME_R`, `WID`, `FBW`, `MOD`,
  - DUAL `DLY4`: `M.RATE`.
- `TYPE=CLASSIC` expose 2 pages delay; `TYPE=DUAL` expose 4 pages delay.
- `TYPE=CLASSIC` reste le default visible et conserve l'ancien controle `X`.
- `TYPE=DUAL` substitue `MODE` au slot de `X`; `MODE` propose `Normal`, `PingPong`, `Tap`, `ClassicPP`.
- `TIME_R` et `WID` sont visibles uniquement en DUAL; en `Tap`, `TIME_R` sert de temps principal.
- `SWING` et `ACCENT` sont retires de la surface delay produit V1.

## 14.d Contrat Settings Sample Stream split browser

- `Settings > Sample > Stream` ouvre directement le browser STREAM split, sans passer par l'ancien detail `Slot > Load/Preview/Clear`.
- Surface OLED permanente:
  - colonne gauche: bibliotheque SD depuis `0:/Samples`, fichiers WAV et dossiers;
  - colonne droite: slots projet `sample_pool` existants;
  - les labels visibles retirent l'extension `.wav`, les paths internes conservent le nom complet.
- Controles:
  - `Enc1`: navigation bibliotheque gauche;
  - `Enc2`: navigation slots droite;
  - `Enc3`: focus gauche/droite borne, sans wrap;
  - `Enc4`: volume preview `sd_preview`;
  - focus gauche par defaut.
- Actions PAGE:
  - `PAGE 1` RETURN remonte au dossier parent ou sort du browser depuis `0:/Samples`;
  - `PAGE 2` OK sur focus gauche entre dans un dossier, remonte sur `..`, ou charge un fichier dans le premier slot libre;
  - `PAGE 2` OK sur focus droite conserve la grammaire d'action de slot existante;
  - `PAGE 3` est reserve/no-op;
  - `PAGE 4` REFRESH, `SHIFT+PAGE 4` REBUILD.
- `BTN_COPY` et `BTN_PASTE` sont no-op dans ce browser.
- Les appuis Hall dans ce browser declenchent la preview de la selection surlignee: fichier gauche si focus bibliotheque, sample du slot si focus slots; aucun acces FatFs n'est ajoute au chemin audio IRQ.
- Le clear de slot ne supprime jamais le fichier SD. Les operations delete/rename/move de fichiers SD restent hors contrat UI.
- Si un load vers slot Sampler refuse un WAV PCM convertible car incompatible avec le format runtime 48 kHz, le browser propose `CONVERT TO 48K ?`.
- `PAGE 2` confirme la conversion; `PAGE 1` annule.
- Si transport, start pending, record writer ou export Looper est actif au moment du YES, l'UI affiche `STOP AUDIO TO CONVERT` et annule: l'utilisateur doit stopper l'audio lui-meme.
- Conversion acceptee: la preview est stoppee, `wav_convert` convertit destructivement le fichier source en WAV PCM24 stereo 48 kHz avec progression `CONVERT n%`, puis l'UI relance le load du slot cible sur le meme path.
- Pendant la conversion, les events du browser sont ignores pour eviter navigation/load concurrente; le chemin preview et le runtime audio restent inchanges.

## 14.e Contrat Settings Sample Multi split browser

- `Settings > Sample > Multi` ouvre un browser split dedie au pool projet `Sampler/Multi`, sans modifier la page TONE `INST | GAIN`.
- Surface OLED:
  - header global de budget sample/page-cache partage avec les browsers `Stream` et `RAM`;
  - colonne gauche: entrees du dossier courant sous `0:/Multi/`, sans exposition des WAV internes;
  - colonne droite: slots instruments `multi_sample_pool` `M01..M32`;
  - les entrees considerees comme dossiers par le browser Multi (`NAV_FOLDER` et `EMPTY_FOLDER`) sont regroupees en haut comme dans le browser Stream;
  - la colonne gauche affiche un dossier de navigation avec le prefixe dossier `> `, et un dossier Multi chargeable sans prefixe dossier;
  - un dossier Multi chargeable affiche le nom instrument et le nombre de samples si `.brickmulti` existe, sinon `NEW`;
  - la colonne droite affiche slot, nom instrument et samples consommes.
- Classification gauche:
  - WAV direct dans le dossier candidat: `MULTI_ITEM`, affichage item chargeable, `OK` charge ce dossier comme Multi;
  - pas de WAV direct mais au moins un sous-dossier: `NAV_FOLDER`, affichage dossier, `OK` entre dans le dossier;
  - ni WAV direct ni sous-dossier: `EMPTY_FOLDER`, affichage dossier vide, `OK` entre puis affiche `EMPTY`.
  - la classification ne scanne pas recursivement: un WAV dans un sous-dossier ne rend pas le parent chargeable; un dossier mixte WAV directs + sous-dossiers est chargeable et les sous-dossiers ne sont pas inclus dans le Multi.
- Controles:
  - `Enc1`: navigation colonne gauche + focus gauche;
  - `Enc2`: navigation colonne droite + focus droite;
  - `Enc3` et `Enc4`: reserves/inutilises dans cette passe.
- Actions:
  - `PAGE 1` RETURN remonte au parent Multi, ou quitte le browser depuis `0:/Multi`;
  - `PAGE 2` OK sur focus gauche entre dans un dossier de navigation, sinon prepare si besoin puis charge le Multi dans le premier slot libre;
  - `PAGE 2` OK sur focus droit unload le slot Multi selectionne avec confirmation;
  - `PAGE 3` et `PAGE 4` reserves/no-op.
- Si le dossier n'a pas encore d'index, la confirmation visible est seulement `Prepare multi ?`; les labels bas communs conservent `RETURN` et `OK`, et le nom courant reste porte par la barre de contexte.
- Apres validation de cette confirmation, ou apres OK direct sur un Multi deja indexe, l'UI passe en etat bloquant `PREPARING`: les evenements et encodeurs du browser sont ignores. La barre basse couvre la sequence complete UI-control: import direct WAV (`Scan multi`), commit/index/pool (`Commit multi`), prechargement sample via `multi_sample_service_load` (`Prepare samples`) et refresh final du browser (`Refresh multi`). La sortie d'etat se fait seulement quand le slot Multi est `READY` ou `ERROR`, puis apres refresh des infos UI.
- Si le Multi ne peut pas rentrer dans le pool sample courant, le browser refuse avant confirmation quand le nombre de WAV directs est deja connu, et refuse aussi le load direct d'un Multi deja indexe. Le controle utilise le cout net en slots produit `X/256`: cout du nouveau Multi moins le cout du slot remplace, afin de ne pas refuser un remplacement qui libere assez de budget. La zone header `X/256` clignote pour signaler la saturation.
- Le load appelle le loader Multi cooperatif existant et reutilise un slot deja charge si le meme path `.brickmulti` est deja present; aucun nouveau cache, streamer ou acces FatFs IRQ n'est ajoute.
- Quand la track active est `Sampler/Multi`, un load/reuse depuis ce browser assigne le slot instrument a cette track et enregistre le path projet associe; le pool global reste l'autorite des instruments charges.
- Un manque de capacite sample du pool est refuse par feedback court `FULL need X`.
- L'unload retire le slot du pool projet; il ne supprime, renomme ni deplace aucun WAV SD.

## 14.f Contrat Settings Sample RAM

- `Settings > Sample > RAM` est la vue filtre `kind=RAM` du catalogue global sample.
- La page peut charger un WAV vers `sampler_ram_pool`, afficher READY/ERROR/EMPTY et clear un slot RAM; la persistence projet sauvegarde ensuite les slots RAM par `global_index`, `ram_slot` et path WAV, sans dupliquer l'audio.
- Le backend interne `sampler_ram_pool` est dimensionne sur la capacite active du pool global sample, pas sur les 16 pads/voix/pages UI. Avec la configuration courante, le 17e sample RAM doit donc charger si un slot global et le budget memoire restent disponibles.
- `Sampler/RAM` peut jouer un slot global `kind=RAM/READY` avec `Start`/`End` et `RevShot`; `Slice Count` active un slicing grille RAM dans cette meme region. En mode slice, la note selectionne la slice, ne transpose plus le sample, et le `Loop` global n'affecte ni le debut de boucle ni les bornes de la slice. RAM ne consomme pas le pool Stream.
- La page TONE de `Sampler/RAM` expose `PLAY` (`Sample`, `Mode`, `Start`, `End`) et `LOOP` (`Gain`, `Tune`, `Loop`, `Slice`). `Fade In`/`Fade Out` ne font plus partie du contrat RAM; l'enveloppe d'amplitude reste portee par VCA.
- Le rendu TONE `Sampler/RAM` garde les memes params et les memes edits encodeurs, mais remplace uniquement la zone potards par le cadre/waveform RAM stable entre pages; le nom du slot RAM actif est affiche au-dessus depuis `sample_global_pool.path`, sans scan SD/FatFs.
- Les textes des 4 slots `Sampler/RAM` reutilisent la grammaire template commune: label au repos, valeur temporaire au meme emplacement via le flash `ui_param_get_slot_value_flash`.

## 15. Contrat UI Settings - Load Project
- `PROJECT > LOAD` expose une entree explicite `BLANK PROJECT` (index 0), distincte des slots SD.
- Action associee: appel direct `project_v1_load_blank()`.
- Les slots SD restent listes apres cette entree (index decales de +1).
- Le flux `PROJECT > MANAGE` reste reserve aux slots reels.

## 16. Contrat boot UI
- Etat boot UI voulu:
  - track active logique = track 1 (index 0),
  - ensemble/page active = `CFG` (`UI_PAGE_TEMPLATE_CFG`) en boot normal.
- Etat loading boot:
  - `ui_boot_loading_begin()` arme un ecran transitoire avant l'UI normale,
  - un variant de rendu est choisi une seule fois par `ui_boot_loading_select_variant()` avec un seed faible `HAL_GetTick() ^ SysTick->VAL`; le choix reste stable pendant toute la phase loading,
  - les variants partagent le meme protocole, le meme logo bitmap 1-bit flash 107x19 `BRICK`, et lisent seulement `done/total/frame`.
  - variant `Tetris`: scene de blocs type Tetris, pile de 42 cellules mappee sur `done/total`, bloc actif en chute deterministe vers la prochaine cellule cible,
  - variant `Wall`: mur de briques 12x4 mappe sur `done/total`, brique active en chute simple,
  - si le total depasse les cellules visibles du variant, le rendu reste proportionnel sans inventer de progression,
  - si la progression stagne, seule l'animation idle continue sans remplir la pile/le mur,
  - quand `total=0`, la pile reste vide avec bloc actif idle pendant la premiere frame transitoire avant sortie ou restore effectif,
  - apres la premiere frame rendue, `ui_boot_loading_service()` lance le restore du dernier projet puis attend la fin des slots sample autoload,
  - le restore projet attend aussi que le premier flush OLED complet soit termine (`drv_display_flush_in_progress()==0`) afin d'eviter de lancer une phase SD monolithique avant que l'ecran idle soit reellement visible,
  - le texte bas choisit deux phrases courtes depuis une banque rodata de phrases boot/loading anglaises; le choix est fait une seule fois dans `ui_boot_loading_begin()` et alterne lentement entre ces deux phrases,
  - le compteur bas affiche la progression globale `done/total` en unites autoload utilisateur: un slot STREAM vaut 1 unite, un slot RAM vaut 1 unite synchrone, un slot MULTI vaut son nombre de samples si le header `.brickmulti` a pu etre prelu au restore; les pages internes ne sont jamais affichees comme compteur samples,
  - pendant cet etat, `ui_tasklet_poll()` consomme/ignore les inputs et n'appelle pas `ui_core_tick()`.
- Premier affichage propre:
  - `drv_display_init()` garde le SSD1309 `Display OFF` pendant l'init,
  - clear le framebuffer puis la RAM controleur en full-screen synchrone,
  - active le display seulement apres ce clear complet; aucun flush DMA partiel ne precede ce premier etat noir propre.
- Priorite hall/bootstrap:
  - `ui_bootstrap_init` pose l'etat initial `CALIBRATION`,
  - `brick6_app_init` decide ensuite selon `hall_calibration_load()`:
    - succes -> bascule vers `CFG`,
    - echec -> conserve `CALIBRATION`.
- La sortie de loading rend la main au renderer de page normal seulement quand Z6 signale que tous les slots attendus sont terminaux et que le loader Multi n'a plus de travail pending.
- Aucun fallback renderer n'est utilise pour masquer un etat UI invalide.
- Les buffers median de calibration Hall (`g_min_buffer`, `g_max_buffer`) restent utilises uniquement par la page calibration / `hall_calibration_process()` et sont places en `CTRL_STATE` D3; ils ne sont ni audio hard-RT ni DMA-owned.


## 9. Contrat query stricte - filter target
- `ui_resolve_filter_target_track` porte l'explicitation du refresh avant la query.
- `ui_core_runtime_bridge_resolve_filter_target_track` reste une query pure et ne refresh plus au passage.

## 10. Contrat transport explicite
- Le seam transport du runtime bridge est d�sormais d�coup� en commandes explicites PLAY / REC / pattern shortcut.
- L'arbritrage reste c�t� UI; le bridge porte seulement l'ex�cution des commandes et leurs feedbacks associ�s.

## 18. Contrat UI Drum Plaits direct

- L'autorite de la liste active des types Drum `CFG` est `ui_track_catalog`.
- La liste active Drum expose seulement `TRX BD` et `BD Analog`.
- `TRX BD` reste visible comme entree reservee/future; a date il n'a pas de moteur runtime actif et reste silencieux via le mapping Drum `NONE`.
- Aucun autre type Drum n'est expose ou conserve par la liste UI active.
- La page TONE Drum reservee garde une sous-page vide pour ne pas casser la navigation.
- `BD Analog` est le premier type Drum propre et experimental; il reste selectionnable dans le catalogue `Drum`.
- Pour `BD Analog`, `TONE` expose une sous-page legere `BD`: `Pitch`, `Decay`, `Tone`, `FM`.
- `PLAY`, `COLORS`, `MIX`, `MOD` et `VCA` restent les ensembles communs existants; aucune page UI lourde ni chemin de modulation local n'est ajoute.

## 19. Contrat UI Sampler/Looper skeleton

- Le catalogue `CFG` expose `Looper` comme type de la family existante `Sampler`.
- `VCA` n'est pas expose pour `Sampler/Looper`; le niveau utilisateur passe par `MIX/Level`.
- Pour `Sampler/Looper`, le hall mode brut `ARP` est projete visuellement en `ROUT`.
- `ROUT` toggle une selection de tracks logiques sources par looper track dans `ui_core_runtime_bridge`; la source peut etre focus UI ou non.
- `REC` global ne demarre jamais directement le Looper et ne devient pas focus-based: il garde le flux transport normal (`seq_runtime_set_pattern_rec_target_track` + `seq_runtime_rec_toggle_arm`) meme si la track active est `Sampler/Looper`, `ARM=Off`, `ARM=Overd` ou `ROUT` vide.
- Demarrage Looper: le bridge observe `transport running + REC global arme`; si une unique track `Sampler/Looper` est eligible (`ARM=Rec`, au moins une source `ROUT`) et qu'aucun writer actif/finalizing/export ne bloque, il rafraichit explicitement la projection runtime, resolve le slot RAW systeme via Z6/storage, appelle `multi_record_writer_prepare_raw`, puis arme `brick6_looper_runtime_arm_record_start`. `multi_record_writer_start` est consomme ensuite par Z1 au marker boundary audio.
- Depuis STOP, le chemin PLAY prepare/arme ce meme demarrage Looper avant `seq_runtime_start()`, afin que le premier marker boundary step 0 puisse consommer `brick6_looper_runtime_on_boundary_edge()` sans attendre le service UI suivant. Le service periodique reste le filet de securite pour le transport deja running et les etats post-start.
- Les refus RAW visibles distinguent les causes simples: `RAW MISS`, `RAW SIZE`, `RAW SLOT`, `RAW MOUNT`, `RAW BUSY`, `RAW STAT` ou `RAW INIT`.
- Apres demarrage writer reussi, `ARM=Rec` est consomme et repasse `Off` pour eviter tout redemarrage automatique sans nouveau geste utilisateur.
- Le demarrage `ARM=Rec` applique le contrat replace: l'ancien playback Looper de la track cible est arrete/detache et ses pages transient sont invalidees avant que le writer passe en `RECORDING`; l'ancienne loop ne reste pas audible pendant la nouvelle prise.
- Le focus UI n'est pas une condition d'eligibilite Looper.
- Politique multi-looper temporaire: si plusieurs tracks `Sampler/Looper` sont eligibles, aucun writer Looper n'est demarre; le controle utilisateur explicite devra reduire l'eligibilite a une seule track.
- `ARM=Overd` reste visible mais non fonctionnel pour l'audio overdub: la track est ignoree par le demarrage writer tant que l'overdub audio n'est pas implemente.
- `PLAY=Off/Auto` pilote maintenant le playback transient apres capture:
  - `Off`: la prise finalisee est chargee en runtime mais reste muette,
  - `Auto`: sur une prise LEN fixe, Z5 arme le playback live START_RAM des le stop musical Looper avec la longueur LEN connue; `TAKE_READY` ne sert ensuite qu'a attacher le RAW finalise/backing storage.
- Au STOP puis PLAY transport, Z5 ne relance pas directement la lecture Looper: il arme seulement l'intention `PLAY=Auto`; le redemarrage effectif est consomme cote Z1 au marker boundary edge sample-accurate fourni par Z4.
- STOP transport, transport non-running ou desarmement REC global arme l'arret via `brick6_looper_runtime_arm_record_stop` si un record Looper est actif; `multi_record_writer_request_stop` est consomme ensuite par Z1 au marker boundary audio quand le transport fournit encore une boundary.
- `LEN=Free` ne declenche aucun auto-stop Looper.
- `LEN=1/2/4/8/16` calcule une longueur cible en frames et la transmet comme intention a `brick6_looper_runtime_arm_record_start`; le demarrage effectif du writer, le sample start, l'auto-stop et le sample stop appartiennent ensuite au domaine audio/boundary.
- Sur le stop LEN, si `PLAY=Auto`, Z1/`brick6_looper_runtime` utilise le span exact `REC_STOP - REC_START` et le preroll RAM pour armer le depart START_RAM; il ne depend pas de l'etat writer `TAKE_READY` pour ce premier depart live.
- L'auto-stop Looper ne stoppe pas le transport global et ne desarme pas REC global; STOP manuel, transport stopped ou desarmement REC restent prioritaires et gagnent avant LEN.
- Apres auto-stop LEN, le demarrage Looper reste verrouille tant que transport et REC restent tous deux actifs, pour eviter de supprimer la temp finalisee et relancer une prise sans geste utilisateur.
- Si le writer est deja `STOP_REQUESTED`/`DRAINING`/`FINALIZING`, Z5 ne redemande pas un stop LEN.
- `SHIFT+SETTINGS` sur la track active `Sampler/Looper` est intercepte avant l'ouverture SETTINGS normale et demande le SAVE Looper:
  - accepte uniquement une prise writer `TAKE_READY`,
  - refuse transport running par `STOP SAVE`,
  - refuse `RECORDING` / `STOP_REQUESTED` / `DRAINING` / `FINALIZING` par feedback court `LOOP BUSY`,
  - stoppe une preview SD active avant de reserver le path durable,
  - refuse l'absence de prise finalisee pour cette track par `NO LOOP`,
  - demande le path durable a Z6/storage,
  - demarre `looper_storage_raw_export_start()` sur `raw_path + recorded_frames`,
  - laisse le playback transient RAW courant attache au reservoir RAW; le fichier durable est notifie au catalogue WAV si celui-ci est deja charge, sinon il sera retrouve par scan lazy Settings,
  - feedback visible par phase `SAVE WAIT` / `SAVE OPEN` / `SAVE n%` / `SAVE VERIFY`, puis `LOOP SAVED` ou `SAVE FAIL`.
- Le SAVE Looper ne lance pas de scan catalogue WAV global: apres export termine, Z5 ajoute seulement le path final au cache catalogue deja charge via `wav_loader_catalog_notify_file_created()`, sinon le scan Settings reste lazy/demande par la page Sampler.
- Le bridge UI ne possede pas la nomenclature fichier Looper, le scan de path SD ni la creation du dossier durable; cette autorite appartient a Z6/storage.
- SETTINGS normal reste inchangé hors `Sampler/Looper`.
- Le bridge conserve l'intention de looper active pendant le record; `brick6_looper_runtime` devient l'autorite sample-exacte de l'identite de capture active exposee a Z1, tandis que la matrice ROUT reste lue par bornes pour alimenter le ring RAM du writer.

## 20. Contrat UI apres retrait buffer master

- Le catalogue `Master` expose seulement `FX`.
- Les shortcuts `TRACK+REC`, `TRACK+PLAY` et `TRACK+SHIFT+REC` ne ciblent plus de backend buffer dedie; ils reviennent au transport/REC existant.
- `ROUT` reste une projection de `ARP` pour `Master/FX` et `Sampler/Looper`; `Sampler/Looper` expose `ARM`, `LEN`, `PLAY`, `XFade` via `PARAM_LOOPER_XFADE`.
- `XFade` Looper suit le contrat TONE normal de capture: il peut etre p-locke et assigne a une scene MACRO depuis la page `TONE/LOOP`.

## 21. Contrat UI Looper STRETCH

- La page TONE `Sampler/Looper` garde la sous-page `LOOP` inchangee: `ARM`, `LEN`, `PLAY`, `XFade`.
- Une sous-page `STR` expose `Stretch`, `Pitch` et `Grain`; le quatrieme slot reste vide.
- La surface Looper n'expose pas `SRC BPM` ni `SYNC LEN`.
- Ces controles sont stockes/UI-only dans cette passe; aucun changement audible de playback n'est associe a leur edition.

## 22. Contrat UI Audio Rec / Rec Edit restreint

- `SHIFT + HALL 14` ouvre immediatement le hall mode special `Audio Rec`, hors template classique et sans double-tap.
- Les halls du mode Audio Rec togglent le routage des tracks sources vers `sample_capture`; hall allume = source routee, hall eteint = source exclue. Les LEDs lisent ce routage `sample_capture` directement, sans reutiliser les routages Looper/Master/Buffer.
- Encodeurs Audio Rec:
  - E1 `ARM`: `OFF/REC`
  - E2 `LEN`: `FREE` ou `1..64` steps
  - E3 `QUANT`: `NOW/BAR/PATTERN`
  - E4 reserve
- `ARM=REC` autorise seulement une prise `SAMPLE_WAV`; il n'ouvre aucun fichier, ne pousse aucun audio et ne demarre pas le writer tant que le REC global n'est pas arme.
- Quand `ARM=REC` et le REC global sont actifs, depuis STOP le demarrage attend le transport PLAY; pendant PLAY il suit `QUANT`. Si le REC global est coupe pendant `RECORDING`, la prise demande STOP/finalize par le chemin writer normal.
- Les editions `ARM`, `LEN` et `QUANT` sont bornees, sans wrap circulaire; E4 Audio Rec reste no-op. `LEN=1..64` est une duree en steps, pas en mesures.
- Pendant `RECORDING`, l'ecran Audio Rec dessine une waveform live RAM alimentee par des buckets min/max signes issus des blocs deja pousses vers le ring `SAMPLE_WAV`; aucune lecture du WAV en cours ni FatFs n'est faite. La vue live garde toute la prise visible entre gauche et droite, compresse les buckets en RAM quand la prise depasse la resolution disponible, et utilise une echelle verticale fixe full-scale `int16` sans normalisation locale de la fenetre. Le renderer affiche toujours la ligne zero au centre et dessine les buckets en traits verticaux fins; le zoom Rec Edit utilise une progression de fenetre plus douce que les seuls paliers puissance de deux.
- Apres finalisation writer `TAKE_READY`, l'UI bascule vers `Rec Edit` restreint immediatement: cette transition ne valide pas le `.brkwave`, ne lit pas le WAV et ne prend pas le gate SD pour le cache waveform. Les services waveform/overview reprennent hors chemin d'entree a la passe suivante.
- `Rec Edit` n'est pas le Sample Editor complet: E1/E2 reglent zoom/scroll UI bornes sans mutation audio persistante, E3/E4 reglent `START/END`, et un appui hall toggle l'audition de la fenetre `START..END` via le chemin preview SD existant. Cette audition n'est autorisee que sur une prise temporaire finalisee/fermee (`TAKE_READY` deja passe). La waveform Rec Edit separe la carte globale et la loupe locale: zoom global ou vue plus large que le cache tuile utilise l'overview globale min/max editor-owned; les vues locales demandent un cache line derive depuis les tuiles audio RAM pretes quand la fenetre visible est couverte. Le cache line stocke des points signes ordonnes dimensionnes a la largeur ecran et devient le renderer detail Rec Edit sous forme de polyline. Pendant le remplissage des tuiles, l'ancienne line reste affichable si elle est compatible ou proche de la nouvelle vue pour eviter un saut de scale; sinon l'overview globale sert de fallback propre. L'echelle verticale Rec Edit est stable sur la prise courante, avec occupation accrue visant environ 4 px de marge haut/bas pour les pics forts. Au zoom maximum, la fenetre vise une zone courte bornee par la resolution line plutot qu'une fraction longue de toute la prise.
- Le zoom Rec Edit est proportionnel a la duree: 25 crans de diviseur quasi logarithmique de `1` a `1048576`, avec fenetre minimale bornee a 256 frames si la prise est plus longue. Un changement de zoom conserve le centre visuel de la fenetre puis clamp le start.
- PAGE 1 retourne a Audio Rec en conservant la prise temporaire et stoppe la preview active; PAGE 2 sauvegarde un WAV final trimme a nom automatique borne apres avoir stoppe la preview; PAGE 3 toggle `ZCROSS`; `SHIFT` est le modificateur momentane des fonctions secondaires Rec Edit.
- Apres SAVE reussi, Rec Edit affiche une confirmation courte `ASSIGN?`: PAGE 1 annule, PAGE 2 charge le WAV deja sauvegarde dans le premier slot libre `sample_pool` sans refaire SAVE.
- En Rec Edit, les encodeurs normaux sont `ZOOM`, `POS`, `START`, `END`. Avec `SHIFT` maintenu: `VZOOM`, `FINE`, `L.ST`, `L.END`. `SHIFT` n'est jamais toggle ni persiste; son affichage et les labels `SHFT` sont inverses pendant l'appui. `VZOOM` est un scale vertical UI-only en crans `x0.5/x0.75/x1/x1.5/x2/x3/x4/x6/x8`; il est applique comme facteur autour de la ligne zero puis clippe geometriquement au cadre de waveform.
- `ZCROSS` est un toggle UI/session non destructif: il ne modifie ni WAV, ni `.brkwave`, ni cache waveform, et ne snappe que les marqueurs `START/END/L.ST/L.END` depuis les donnees RAM editor deja disponibles. Le snap est directionnel: un delta encodeur positif cherche le prochain zero-cross strictement a droite du marqueur courant, un delta negatif le precedent a gauche, avec garde anti-retour au meme zero-cross.
- Les loop points Rec Edit sont UI/session uniquement: `LOOP START/END` restent bornes dans `START/END`, ne pilotent pas encore le playback et ne sont pas persistants.
- Aucune UI de nommage improvisee, aucun sidecar asset et aucun cache waveform persistant nouveau ne sont ajoutes.

## 23. Contrat UI Rec Edit cache audio RAM

- Rec Edit garde l'overview RAM de REC live comme fallback de demarrage, mais la carte normale de zoom global est une overview globale min/max complete construite hors IRQ pour la prise ouverte.
- La source primaire du rendu detaille Rec Edit est un cache audio RAM editor-owned, mono `int16`, decoupe en petites tuiles autour du focus/fenetre visible et recycle a chaque prise. Des niveaux volatiles par tuile servent les vues locales larges/moyennes pour eviter de rescanner toute la fenetre RAM a chaque zoom/scroll.
- La page UI ne fait aucun acces FatFs: elle demande une vue locale seulement si la fenetre tient dans la capacite du cache tuile, puis dessine la line derivee si les tuiles couvrent la fenetre; sinon elle garde l'ancienne line compatible/proche ou retombe sur l'overview globale.
- Le cache n'est pas un sidecar, n'est pas persistant et n'appartient pas au sample.

## 24. Contrat UI cache waveform persistant

- L'ouverture d'un WAV par Rec Edit/Sampler peut demander un `.brkwave` via `waveform_cache_request_for_wav()`, sans bloquer l'editeur.
- Le renderer Rec Edit tente d'abord les tuiles RAM persistantes `.brkwave` deja chargees: choix de niveau par `frames_per_pixel` (16384/4096/1024/256 frames/colonne), dessin min/max depuis RAM, et demande hors draw des tuiles visibles + marge via `waveform_cache_request_tiles()`.
- Les seuils de niveau restent centres geometriquement entre resolutions: `>=8192` -> 16384, `>=2048` -> 4096, `>=512` -> 1024, sinon 256 frames/colonne; un niveau plus grossier deja en RAM peut servir de fallback temporaire pour la meme zone pendant le chargement du niveau cible.
- Le renderer UI ne lit toujours pas la SD dans draw; les tuiles absentes gardent le fallback overview globale, ancienne line compatible ou cache audio RAM local.
- L'ancien cache audio tuile editor-owned ne couvre plus les vues `>= 256` frames/pixel; ces vues larges/intermediaires appartiennent au `.brkwave`, et le cache audio local reste reserve aux zooms plus fins bornes.
- Le browser ne doit pas exposer `0:/BRICK/.wavecache/`; le cache waveform est un detail systeme reconstructible, pas un asset utilisateur.

## 25. Contrat Settings/Sampler erreurs SD locales

- Un echec `OPEN FAIL` pendant LOAD ou PREVIEW reste local a l'action et ne relance pas de scan ni de refresh destructif du browser.
- `ui_page_settings` conserve la derniere liste valide tant qu'une lecture catalogue demandee par la navigation n'a pas abouti; un refus SD ou une erreur I/O catalogue affiche un feedback court sans vider `sample_entries`, `sample_dir` ni `sample_parent_id`.
- Le rendu Settings/Sampler reste RAM-only: il lit uniquement les snapshots UI et les vues catalogue deja chargees, jamais la SD.

## 26. Contrat layout commun template OLED

- `ui_renderer_template` porte le layout commun des pages template 128x64: header compact, quatre cartes parametres, footer quatre labels.
- Formatage des valeurs parametres:
  - les params continus abstraits encore declares `PARAM_DISPLAY_PERCENT` sont affiches en echelle normalisee `0.00..127.00` via le helper central `ui_format_param_127_00`, sans changer leur stockage ni leur apply DSP,
  - les params discrets (`BOOL`, `ENUM`, modes, slots, divisions, ratios nommes) conservent leurs labels texte,
  - les params ayant deja une unite metier explicite (`st`, `ct`, `ms`, `s`, `Hz`, `dB`, divisions tempo, pan/largeur bipolaire) conservent leur format dedie.
- Edition encodeur des pages potards classiques:
  - pour les continus abstraits `PARAM_DISPLAY_PERCENT`, un detent normal vaut environ `1.00` sur l'echelle affichee `0.00..127.00`,
  - `SHIFT + encodeur` vaut environ `0.01` sur cette meme echelle,
  - cette finesse est limitee au chemin template/`ui_param`; `Settings`, `Audio Rec` et `Rec Edit` gardent leurs handlers encodeurs locaux et leurs fonctions secondaires `SHIFT`.
- Le header conserve track, label runtime track-aware, mode hall, titre famille/page, CPU, tempo et pattern courant, avec ellipses pixel si la largeur reelle ne suffit pas.
- Les cartes parametres restent quatre slots egaux de 32 px, sans debordement horizontal; titre et valeur sont bornes avant centrage.
- Chaque slot template affiche le widget en haut et une seule ligne de texte en bas.
- La ligne basse affiche normalement le nom du parametre. Apres une edition utilisateur explicite du slot, elle affiche temporairement la valeur formatee pendant environ 800 ms, puis revient au nom.
- L'etat de flash valeur est UI-only dans `ui_param`, statique, sans malloc, indexe par slot visible + parametre + track, et reset par changement de bank/page/track.
- Le renderer ne deduit pas les causes de changement de valeur: il interroge seulement `ui_param` pour savoir si une valeur temporaire doit remplacer le nom.
- Les valeurs temporaires reutilisent le formatage commun du renderer et les callbacks `param_text`, afin de conserver les labels dynamiques Master/FX, Multi/Sample/Stream et les overrides locaux.
- Les widgets custom template sont optionnels via `custom_widget_picker`; ils se dessinent dans le rect utile du widget, ou dans un rect groupe explicite quand les quatre slots d'une sous-page declarent le meme custom widget, et doivent reutiliser la meme valeur visible que les widgets simples (base display, preview MACRO/scene, feedback p-lock), sans consommer le flash valeur comme source de courbe.
- Custom actif: `COLORS/ADSR` et `VCA/ADSR` remplacent les quatre widgets simples A/D/S/R par une seule courbe ADSR groupee sur la zone haute commune; les quatre slots restent editables et gardent leurs labels/flash/underline locaux, avec fallback widget classique si le groupe attendu est incomplet ou non supporte runtime.
- Le dessin ADSR groupe est decoupe en quatre zones horizontales stables A/D/S/R alignees sur les quatre slots; A/D/R n'entrent pas en competition de largeur globale et deplacent les points de transition de la courbe principale dans leur zone, tandis que S pilote la hauteur du plateau.
- `COLORS/MAIN` peut declarer des widgets custom locaux pour `F Type`, `Cutoff` et `Res`: `F Type` affiche le label court du type (`OFF`, `DJ`, `LP`, `HP`, `BP`), avec `OFF` en police compacte et les types actifs en police large du widget. `Cutoff`/`Res` affichent `-` par slot et masquent leur label bas normal quand le filtre est coupe; sinon `Cutoff`/`Res` forment une seule courbe filtre groupee sur leurs deux slots, basee sur la meme valeur visible que les widgets standards. La silhouette est adaptee au type filtre supporte (`EQ3/DJ`, `LP`, `HP`, `BP`) et conserve une baseline unique sans segment parasite colle en bas. Le fallback widget classique reste obligatoire quand le contexte custom est incomplet ou non supporte.
- Le flash est declenche uniquement par action utilisateur explicite sur le slot: edition directe encodeur, edition p-lock, live-rec p-lock issu de l'encodeur, ou edition de valeur scene/macro en assign. Playback p-lock, LFO, morph scene continu, macro pot physique, restore/recall et refresh UI ne declenchent pas le flash.
- Les pages `CFG`, `COLORS`, `TONE`, `MOD`, `MIX`, `PLAY`, `VCA`, `KEYBOARD`, `ARP`, `SEQ` et `MACRO` heritent du style commun tant qu'elles utilisent `ui_template_page_render`.

## 27. Contrat UI Wave labels moteur

- La page `TONE` de `Synth/Wave` conserve les IDs et l'ordre existants: `EDIT`, `FINE`, `COARSE`, `FM`, puis `TIMBRE`, `MODULATION`, `COLOR`, `PHASE RESET`.
- Les slots herites `TIMBRE` et `COLOR` affichent des labels UI dynamiques, un widget local et une valeur formatee derives de la valeur courante de `PARAM_WAVE_EDIT`; les valeurs stockees, l'ordre des params et le format projet/pattern ne changent pas.
- La table UI couvre les 39 moteurs actifs de `brick6_braids_runtime.cpp::kBraidsShapeMap`, incluant `Harm` et sans entree `Warm`.
- Les formats locaux Wave sont bornes a `PARAM_WAVE_TIMBRE` et `PARAM_WAVE_COLOR`: continu unipolaire normalise `0.00..127.00`, pourcent bipolaire conserve, intervalle en demi-tons/centiemes quand le mapping Braids est prouve, enum discret, stepped, morph et rate normalise.
- Les controles globaux visibles de `Synth/Wave` sont aussi formates localement dans TONE: `EDIT` devient `MODEL` avec le nom du moteur, `COARSE` devient `PITCH` en demi-tons, `FINE` en cents, `FM` suit l'echelle normalisee `0.00..127.00`, et `MODULATION` devient `A MOD` en pourcent bipolaire.
- La surface `MOD/LFO DEST` reutilise la meme source de noms Wave que `TONE`: les destinations `MODEL/FINE/PITCH/FM AMT`, `A MOD` et les labels dynamiques `TIMBRE/COLOR` dependent du moteur Wave canonique courant de la track, sans resolution temps reel des changements temporaires p-lock/LFO.
- `SawSq` garde `PARAM_WAVE_COLOR` en banque pour compatibilite d'edition/stockage, mais son affichage est neutralise en label `-`, valeur `---` et widget vide car aucun effet DSP n'a ete observe pour ce parametre.
- Les filtres Braids `ZLPF/ZPKF/ZBPF/ZHPF` restent hors surface UI Wave car ils ne sont pas dans le mapping runtime actif.

## 28. Surface MOD LFO finale

- L'ensemble `MOD` expose quatre sous-pages dans cet ordre: `LFO1`, `LFO1#`, `LFO2`, `LFO2#`.
- `LFO1`/`LFO2`: `DEST`, `RATE`, `DEPTH`, `SHAPE`.
- `LFO1#`/`LFO2#`: `DELAY`, `TRIG`, `FADE`, `PHASE` ou `SLEW`.
- `RATE` affiche `OFF` au centre, `x.xxHz` a gauche et les divisions musicales a droite. L'encodeur sans SHIFT avance le cote Hz par pas lisibles de 1Hz; avec SHIFT il edite le cote Hz au pas fin de `0.01Hz`. Le cote sync reste discret par index.
- `DELAY` affiche toujours une valeur en secondes `x.xxs`; sans SHIFT l'edition avance par pas de 1s en conservant la fraction, avec SHIFT par pas de `0.01s`.
- Le slot `PHASE_SLEW` renomme dynamiquement le label en `PHASE` ou `SLEW` selon la shape du LFO concerne, meme si `SHAPE` est sur la page precedente.
- Le widget `PHASE/SLEW` dessine une miniature legere de la forme courante; en mode phase, un curseur indique le decalage horizontal, en mode `RND` le widget n'affiche pas de phase.

## 29. Retour UI apres load blank

- `Settings > Project > Load > Blank` revient directement sur `CFG` apres succes. Ce retour est le point produit sur apres remise a blanc des tracks, car une page template precedente (`TONE`, `MIX`, etc.) peut ne plus etre disponible quand la track active repasse sur `Off`.

## 30. Contrat UI Patch V1

- `SHIFT + HALL 0` est l'entree du domaine `Patch`.
- Premier tap `SHIFT + HALL 0`: arme une action Patch pending; aucun acces SD n'est lance pendant la fenetre double tap.
- Deuxieme tap dans `UI_HALL_MODE_DOUBLE_TAP_MS`: annule le pending single et lance un `Save Patch` direct de la track focus. `Patch` n'est pas conserve comme hall mode persistant pour ce chemin: l'UI affiche un overlay court `PATCH` + feedback (`PATCH SAVE`, puis resultat), puis le label/rendu du hall mode precedent redevient visible.
- Expiration sans deuxieme tap: ouvre `Patch Assign`, menu global d'attribution Patch avec target track initialisee sur la track focus. Le hall mode brut `PATCH` est temporaire pendant le browser et le mode precedent est restaure a la sortie.
- `Patch Assign` liste dynamiquement les patches visibles, affiche `BAD PATCH` en vue globale ou metadata minimale `name + family/type`, et garde `PAGE1` comme retour. Les slots `EMPTY` et `source_track` restent internes a la banque/save et ne polluent pas la navigation normale.
- Le rendu OLED de `Patch Assign` reprend la grammaire browser Sample: header compact avec filtre actif (`ALL`, `SAMPLER/RAM`, etc.) a la place d'un compteur de slots, bandeau Family horizontal (`SYN`, `SMP`, `DRM`, `IN`, `MST`) avec family active inversee et toutes les families inversees en `ALL`, liste centrale a selection inversee, curseur de position vertical, footer PAGE en quatre zones (`RETURN | APPLY | REN | DEL`) et feedback court sur la ligne basse. Aucun bandeau texte ne rappelle les targets ou la track cible; les targets restent portees par les LEDs Hall.
- `Patch Assign` expose une selection locale multi-target: a l'entree, seule la track focus est cochee; un appui Hall toggle la target track correspondante sans changer les filtres Family/Type.
- Pendant `Patch Assign`, le renderer LED Hall lit directement la target mask Patch: LED ON = target cochee, LED OFF = non-target; ce rendu est prioritaire sur le rendu du hall mode precedent et revient au rendu normal a la sortie.
- `PAGE2` applique le meme slot Patch selectionne vers toutes les targets cochees, dans l'ordre croissant des track id, via `patch_v1_apply_slot_to_track`; aucune copie runtime sauvage, preview, rollback ni audition temporaire n'est ajoutee.
- Si aucune target n'est cochee, `PAGE2` refuse par `NO TARGET`. En echec partiel, l'UI continue les targets restantes et affiche `AP n/m` avec la premiere cause disponible, afin de garder l'ordre deterministe sans rollback multi-target.
- `PAGE3` ouvre le mode generique `Name Edit` pour renommer le slot Patch valide selectionne. `Name Edit` recoit buffer initial, longueur max, titre/contexte et callback; `PAGE1` annule sans mutation, `PAGE2` retourne le nom valide a l'appelant. En `Name Edit`, seul `ENC1` change le caractere courant de la frise sans modifier le nom; `ENC2`/`ENC3`/`ENC4` sont no-op. `PAGE3` valide le caractere courant a la position d'ecriture puis avance le curseur si possible. `PAGE4` fait un backspace borne, `SHIFT+PAGE2` valide un espace, `PAGE3`/`PAGE4` avec SHIFT restent no-op. Le curseur represente la prochaine position a ecrire et reste borne aux caracteres existants ou a l'unique position append. Le mode affiche le nom et une frise de caracteres en police lisible, n'ecrit pas la SD lui-meme: `Patch Assign` reste responsable de `patch_v1_rename_slot` et du feedback.
- `PAGE4` demande confirmation puis delete le slot valide selectionne; le browser reste sur le prochain slot valide ou sur le slot devenu `EMPTY`.
- Les filtres Patch Assign sont manuels apres l'entree: `ENC1` choisit le slot visible avec navigation bornee sans wrap, `ENC2` choisit Family (`ALL`, `SYNTH`, `SAMPLER`, `DRUM`, `INPUT`, `MASTER`) avec navigation bornee sans wrap, `ENC3` choisit Type selon Family avec navigation bornee sans wrap. A l'entree, Family/Type sont initialises depuis la track focus; changer les targets par Hall ne recale pas les filtres.
- Type depend de Family: `ALL -> ALL`, `SYNTH -> ALL/WAVE`, `SAMPLER -> ALL/RAM/STREAM/MULTI/LOOPER`, `INPUT -> ALL/AUDIO/HYBRID`, `MASTER -> ALL/FX`, `DRUM -> ALL/TRX BD/BD Analog`.
- Regle de visibilite: `Family ALL + Type ALL` montre tous les slots Patch valides puis `BAD PATCH`; les filtres precis montrent uniquement les patches valides correspondants. Si aucun slot n'est visible, le menu affiche `NO PATCH`.
- Les feedbacks UI sont courts: `PATCH APPLIED`, `PATCH RENAMED`, `PATCH DELETED`, `EMPTY`, `BAD PATCH`, `ASSET MISS`, `SD BUSY`, `RENAME FAIL`, `DELETE FAIL` ou `ERROR`.
- `Kit Assign` reste le futur rappel de plusieurs patches differents vers plusieurs tracks; aucun niveau `Set` n'est conserve dans le contrat produit.

## 30b. Contrat UI Patch Poly v2

- Patch reste une categorie de preset sonore assignable; `polyX` designe la largeur en tracks liees, pas la polyphonie audio interne.
- La seule source autorisee pour creer un Patch Poly est le modele officiel `voice_group_role` de Z2/Z5: `SOLO`, `MASTER`, `SLAVE`, avec groupe contigu master puis slaves a droite.
- `PATCH_POLY_TRACK_MAX=4`: Save Patch sur `SOLO` capture `P1`; Save Patch sur `MASTER` capture master + slaves contigus jusqu'a `P4`; Save Patch sur `SLAVE` remonte au master effectif et capture le groupe complet.
- Un groupe incoherent ou plus large que 4 est refuse proprement; aucune selection libre de tracks, aucun target mask Patch Assign et aucun Set partiel ne peuvent creer un Patch Poly.
- Le browser Patch Assign affiche la largeur `P2/P3/P4` dans la liste; les filtres Family/Type restent inchanges et aucun filtre Width n'est ajoute en v2.
- Apply `P1` conserve le contrat multi-target existant: le meme patch mono est applique sequentiellement a toutes les targets cochees.
- Apply `P2/P3/P4` exige une seule target cochee, target role `MASTER`, et un groupe cible deja declare avec exactement la meme largeur; sinon refus court (`NEED 1 TRK`, `NO MASTER`, `NO SLAVES`, `NEED X TRK`).
- Aucun apply partiel, creation automatique de slaves, preview, rollback ni reload asset Sampler complet n'est ajoute.
- Rename/delete restent des operations de slot Patch et ne changent pas la largeur.

## 31. Contrat UI REC CFG START/TEMPO/METRO

- `REC CFG` page 1 expose `START`, `TEMPO`, `SYNC`, `METRO`.
- `LEN` reste expose sur la page 2 `LEN` pour ne pas disparaitre du produit.
- `START`: `DEFAULT`, `TRIG`, `ROLL 1/4`, `ROLL 1/2`, `ROLL 1`; la surface visible n'expose plus l'ancien libelle ni son ancienne valeur off.
- `TEMPO` est rendu comme texte numerique a deux decimales et non comme potard.
- Edition `TEMPO`: encodeur normal = `1.00 BPM`, `SHIFT+encodeur` = `0.01 BPM`; les bornes restent `40.00..300.00`, sans wrap.
- `METRO`: parametre global REC CFG `0..127`; `0` s'affiche `OFF`, `1..127` s'affiche numeriquement et pilote le volume du metronome MAIN monitor-only. Aucun choix de route CUE/BOTH n'est expose en V1.

## 32. Contrat UI Kit V1 étape 2

- `SHIFT + HALL 1` est l'entrée workflow `Kit` en overlay; aucun hall mode Kit persistant n'est ajouté.
- Premier tap `SHIFT + HALL 1`: arme une action Kit pending; aucun accès SD n'est lancé pendant la fenêtre double tap.
- Deuxième tap dans `UI_HALL_MODE_DOUBLE_TAP_MS`: annule le pending single et lance un `Save Kit` direct. Le feedback court affiche `KIT SAVE`, puis `KIT SAVED`, `KIT FULL`, `SD BUSY` ou `ERROR`.
- Expiration sans deuxième tap: ouvre `Kit Browser`, qui liste uniquement les slots Kit valides existants. Les slots vides ne sont pas visibles et la navigation encodeur est bornée sans wrap.
- `PAGE1` retourne à la page précédente. `PAGE2` applique le Kit valide selectionne et affiche `KIT APPLIED`.
- `PAGE2` refuse sans mutation sur `NO KIT` ou slot invalide; les autres refus visibles sont `BAD KIT`, `ASSET MISS`, `SD BUSY` ou `ERROR`.
- L'apply Kit reste complet machine: aucun apply partiel, target mask, selection de tracks, preview ou rollback.
- `PAGE3` ouvre le `Name Edit` générique pour renommer le slot Kit sélectionné; validation réécrit le payload/header avec checksum recalculé et affiche `KIT RENAMED`, cancel revient sans mutation.
- `PAGE4` demande confirmation (`DELETE?`) puis supprime le fichier Kit; le browser reste ouvert, sélectionne le prochain Kit valide si possible, sinon affiche `NO KIT`.
- Le browser affiche le nom Kit et le nombre de tracks depuis les metadata/header, puis une miniature read-only 2x8 issue du summary header. La miniature ne sélectionne aucune track et ne permet aucun apply partiel.
## 33. Contrat UI Kit Link / ecran principal

- Le header OLED principal affiche maintenant le tempo et la charge CPU sur la meme ligne haute. Le BPM conserve sa valeur et son autorite Z4, mais utilise la police compacte historique du Pattern; la CPU est seulement deplacee visuellement.
- La ligne principale droite du header affiche le Kit actif sous la forme `Kit: nom`; `Kit: ---` signifie qu'aucun Kit actif propre n'est expose. Un `*` suffixe le nom quand le Kit actif est dirty.
- Le Pattern actif reste visible sous le Kit en identifiant compact `A-01`, sans redevenir l'element principal de cette zone.
- Le Kit Browser garde la miniature 2x8 et marque le slot Kit actif par un prefixe `*` dans la liste. La navigation de selection ne change plus le slot Kit actif; seul Apply/Save modifie l'etat actif.
- `PAGE2 APPLY` applique le Kit complet puis lie le slot selectionne au pattern actif uniquement si l'apply reussit. En echec (`BAD KIT`, `ASSET MISS`, `SD BUSY`, `ERROR`), le lien pattern -> Kit n'est pas change.
- Delete du Kit actif depuis le browser invalide le Kit actif et clear au minimum le lien du pattern actif; les autres patterns seront refuses proprement au prochain chargement si leur lien pointe vers le slot supprime.
