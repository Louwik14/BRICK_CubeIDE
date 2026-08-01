# Registre global de dette du firmware

Audit statique du worktree observé sur la branche `main_doublemcu_monocore`, au 2026-08-01. Le worktree contenait déjà des modifications locales importantes, notamment le remplacement du mode ARP par le pipeline MIDI FX ; elles font partie de l'état audité et n'ont pas été altérées. Aucun build ni test n'a été lancé. Les `compile_commands.json`, maps et fichiers `.su` présents ont seulement servi d'indices corroborants : le code et CMake restent l'autorité, car ces artefacts sont eux-mêmes modifiés.

## 1. Verdict exécutif

Le firmware possède des autorités métier de mieux en mieux identifiées (`track_topology`, `track_state`, `track_runtime`, `seq_runtime_exec`, registres de paramètres et transitions de restore), des cardinalités désormais explicites et plusieurs chemins bornés. Il n'est cependant pas prêt pour un découpage dual-core : plusieurs frontières main/IRQ reposent encore sur des globals mutables publiés en place, et les protections `PRIMASK` existantes ne constituent pas un contrat inter-cœur.

Le risque dominant est déjà monocœur. Huit sujets P0 sont confirmés : mutation directe d'état DSP lu en IRQ, publication non transactionnelle du binding track, état MIDI FX multi-écrivain, file Stack non sûre à saturation, rattrapage externe non borné dans l'IRQ audio, pertes possibles de Note Off dans les files scheduler et MIDI, et traitement USB Host pouvant bloquer le superloop pendant des délais imposés par la bibliothèque. Le niveau de dette est donc **élevé sur les frontières d'exécution**, mais **modéré sur le modèle produit et les autorités canoniques**, qui offrent de bonnes bases.

À nettoyer avant tout portage : borner les travaux IRQ, garantir l'intégrité Note On/Off, rendre les publications main vers audio transactionnelles, mesurer et gouverner la stack, et rendre les services SD/USB réellement coopératifs. À reporter au portage réel : protocole IPC, placement définitif M7/M4, mémoire partagée, cache/cohérence et affectation des périphériques. Les moteurs audio, formats de stockage et grandes unités UI ne doivent pas être réécrits pour leur seule taille : aucune preuve ne justifie une refonte globale.

## 2. Carte observée

### Matrice de build réellement déclarée

`CMakePresets.json` déclare cinq profils : Debug Low-Cost avec diagnostics, Release Low-Cost, Test Low-Cost, Release Premium et Test Premium. `CMakeLists.txt` compile le même arbre fonctionnel globé pour les deux variantes, ajoute les sources Board/CubeMX propres à la variante et retire les sources diagnostic des builds non-Test. Premium ne possède pas de preset Debug. Le backend granular est explicitement exclu. Les fichiers de diagnostic sont sélectionnés par `BRICK_TEST_BUILD`.

| Zone | Autorité et données canoniques observées | Projections et consommateurs | Frontières et fichiers pivots |
|---|---|---|---|
| Z0 | Configuration Board/CMake, initialisation et ordre du superloop | IRQ HAL, tasklets, services UI/MIDI/SD | `CMakeLists.txt`, `CMakePresets.json`, `Src/Core/brick6_app_init.c`, `Board/LowCost/Generated/Src/main.c`, `Board/Premium/Generated/Src/main.c` |
| Z1 | État des moteurs audio, mixer, DMA et caches de lecture/écriture | Callback SAI RX, `brick6_audio_runtime_dsp`, moteurs Synth/Sampler/Looper/Drum, FX globaux | `Src/Audio/audio.c`, `Src/Core/brick6_audio_runtime.c`, `Src/Audio/mixer.c`, `Src/Core/brick6_*_runtime.*`, `Src/Sampler/sample_cache.c` |
| Z2 | `track_topology` pour l'identité/capacité, `track_state` pour la config, `track_runtime` pour le binding, `track_input_ownership` et `synth_polyphony` pour les ressources | Descripteurs et mappings track logique vers engine, instance et lane mixer | `Inc/Core/track_topology.h`, `Src/Core/track_topology.c`, `Src/Core/track_state.c`, `Src/Core/track_runtime.c`, `Src/Core/track_input_ownership.c`, `Src/Core/synth_polyphony.c` |
| Z3 | Valeurs de paramètres, catalogue, état son track, bases modulation et bases MIDI FX | Apply track-aware vers Z1/Z2 ; overlays p-lock/modulation vers les mêmes backends | `Inc/Param/param_store.h`, `Src/Param/param_registry*.c`, `Src/Core/track_sound_state.c`, `Src/Mod/*.c`, `Src/NoteFx/note_fx_state.c` |
| Z4 | Projet séquenceur, transport/FSM, timeline audio et scheduler | Événements sample-domain vers moteurs, MIDI et MIDI FX ; clock interne/externe | `Src/Seq/seq_model.c`, `Src/Seq/seq_runtime.c`, `Src/Seq/seq_runtime_exec.c`, `Src/Seq/seq_boundary_engine.c`, `Src/Seq/seq_play_scheduler.c`, `Src/Seq/seq_output_guard.c`, `Src/MIDI/midi.c` |
| Z5 | État UI, navigation, focus, modes Hall, clipboard et pages | Projections family/type/capability ; commandes vers transitions, paramètres, séquenceur et stockage | `Src/UI/ui_core*.c`, `Src/UI/ui_navigation.c`, `Src/UI/ui_event.c`, `Src/UI/ui_param.c`, `Src/UI/pages/*.c`, `Src/App/Hall/*.c`, `Src/Keyboard/*.c` |
| Z6 | Snapshots Pattern/Project/Patch/Kit, undo, catalogues, pools et services SD | Restore transactionnel vers Z2/Z3/Z4/Z5 ; flux page-cache/writer vers Z1 | `Src/Storage/pattern_live_ram.c`, `Src/Storage/pattern_sd_bank.c`, `Src/Storage/project_v1.c`, `Src/Storage/project_sd_bank.c`, `Src/Storage/patch_v1.c`, `Src/Storage/kit_v1.c`, `Src/Storage/looper_storage.c`, `Src/Storage/sd_access_gate.c` |

Les tests hôte observés couvrent surtout filtres, modulation, Hall, Stack, topologie, mute, snapshots et le nouveau MIDI FX. Aucun test observé n'exerce la saturation des files, le worst-case de rattrapage clock externe, la préemption main/IRQ, le temps maximal des services SD/USB ou le high-water de stack.

## 3. Registre des dettes

### GLOBAL-001 — publication directe des contrôles vers l'état DSP audio

- **Priorité / statut / zones :** P0, `CONFIRMED`, GLOBAL avec Z1/Z3/Z5/Z6.
- **Fichiers et symboles :** `Src/Audio/mixer.c` (`g_tracks`, `g_track_filters`, `g_reverb`, `mixer_set_track_*`, `mixer_set_reverb_*`, `mixer_set_delay_*`, `mixer_process`) ; `Src/Param/param_registry.c` (`param_registry_apply_track_value`, `param_registry_apply_track_value_runtime_temp`).
- **Preuve :** les setters appelés par UI/restore écrivent directement les structures que `mixer_process` lit et fait évoluer dans le callback audio. Plusieurs apply temporaires de p-lock/modulation atteignent les mêmes setters depuis le domaine audio. Les mises à jour multi-champs d'enveloppe, filtre, type de delay et moteur d'effet n'ont ni snapshot, ni file de commandes, ni commit de block.
- **Cause racine :** API historique de setters synchrones devenue une frontière main/IRQ sans contrat de publication.
- **Conséquence / déclencheur :** préemption d'un setter ou d'une transition au milieu d'une mise à jour, lecture d'une configuration incohérente pendant un block, clic, reset partiel ou état DSP divergent. Le partage direct bloque aussi toute séparation de cœur.
- **Impact :** stabilité audio et futur dual-core critiques ; maintenabilité dégradée par l'absence d'un propriétaire d'écriture unique.
- **Correction recommandée / patch minimal probable :** inventorier les champs réellement main→audio, introduire pour chacun une publication bornée consommée à une frontière de block, sans réécrire le mixer. Garder l'état d'exécution DSP propriétaire de l'audio.
- **Validations / régression :** test de préemption contrôlée pendant changements MIX/COLORS/Master, mesure CPU/underrun, p-locks et restore ; risque élevé sur lissage, ordre d'apply et rendu des FX. Dépend de `Z2-001` et doit être coordonné avec `Z1-002`.

### GLOBAL-002 — budget de stack non gouverné

- **Priorité / statut / zones :** P1, `STRONG SUSPICION`, GLOBAL avec Z0/Z1/Z5.
- **Fichiers et symboles :** `Src/Audio/audio.c` (`process_half`, tableau local `block_events`) ; `Src/UI/ui_core_clipboard.c` (`ui_core_clipboard_clear_track`) ; `Src/UI/pages/ui_page_audio_rec.c` ; linkers `Board/*/Generated/Linker/STM32H743IITX_FLASH.ld` ; artefacts `build/*/*.su`.
- **Preuve :** les deux linkers ne réservent explicitement que `_Min_Stack_Size = 0x400`. Le `.su` Release courant annonce 1632 octets pour `process_half`, 17976 pour `ui_core_clipboard_clear_track` et environ 12 Kio pour plusieurs fonctions Audio Rec. L'IRQ audio peut préempter ces fonctions ; aucun seuil CMake, analyse de callgraph, canari de stack produit ou marge documentée par variante n'a été observé.
- **Cause racine :** gros snapshots automatiques et absence de budget stack vérifié ; la réserve linker est traitée comme garde de link, pas comme contrat worst-case.
- **Conséquence / déclencheur :** le dépassement réel n'est pas prouvé car la stack peut utiliser le gap D1 libre, mais une combinaison UI à grosse frame + IRQ imbriquées peut corrompre D1 lors d'une croissance future.
- **Impact :** risque mémoire latent, plus sévère sur une future cible ou un cœur disposant de moins de RAM locale.
- **Correction recommandée / patch minimal probable :** audit local callgraph/high-water par variante, seuil CI sur `.su`, puis déplacement ciblé des seuls gros temporaires démontrés. Ne pas déplacer aveuglément en SDRAM.
- **Validations / régression :** stack painting en scénarios Audio Rec/clipboard sous charge IRQ, analyse des IRQ imbriquées ; risque moyen sur réentrance et durée de vie des buffers. Lié à `Z1-001` et `DOC-005`.

### Z0-001 — USB Host bloquant dans le superloop Low-Cost

- **Priorité / statut / zones :** P0, `CONFIRMED`, Z0 avec Z1/Z6.
- **Fichiers et symboles :** `Board/LowCost/Generated/Src/main.c` (`main`, `MX_USB_HOST_Process`) ; `Board/LowCost/UsbStack/usb_host.c` ; `Board/LowCost/UsbStack/usbh_conf.c` (`USBH_Delay`) ; `App/Middlewares/ST/STM32_USB_Host_Library/Core/Src/usbh_core.c` (`USBH_Process`).
- **Preuve :** lorsque le rôle host est actif, le superloop appelle directement `MX_USB_HOST_Process`. La machine ST exécutée par `USBH_Process` contient des délais de 200, 100, 10 et 2 ms ; `USBH_Delay` les implémente par `HAL_Delay`. L'API `usb_host_tasklet_poll_bounded` existe mais ne borne pas le coût interne d'un `USBH_Process` et n'est pas appelée.
- **Cause racine :** bibliothèque host coopérative en apparence, mais états d'énumération contenant des attentes synchrones.
- **Conséquence / déclencheur :** branchement/énumération USB Host pendant lecture Stream/Looper ou enregistrement : audio IRQ continue, mais refill cache, drain writer, UI et MIDI superloop peuvent être affamés jusqu'à 200 ms.
- **Impact :** underrun/overflow plausible et freeze live ; frontière périphérique difficile à isoler plus tard.
- **Correction recommandée / patch minimal probable :** audit local de la machine host et remplacement ciblé des délais par états temporisés ou service réellement borné ; aucune abstraction dual-core nécessaire.
- **Validations / régression :** hot-plug, erreur/reset USB et énumération sous Stream + record, mesure du temps maximal d'un tour ; risque élevé sur conformité USB. Dépend de `Z6-001`/`Z6-002` pour le budget global du superloop.

### Z0-002 — composition de sources CMake non auto-invalidante et règle audio orpheline

- **Priorité / statut / zones :** P2, `CONFIRMED`, Z0.
- **Fichiers et symboles :** `CMakeLists.txt` (`file(GLOB ...)`, `AUDIO_ALL_SOURCES`, `AUDIO_C_SOURCES`, `EXCLUDED_SOURCES`) ; `CMakePresets.json`.
- **Preuve :** les globs de sources n'utilisent pas `CONFIGURE_DEPENDS`; ajouter ou retirer un fichier ne force donc pas nécessairement la reconfiguration. Plusieurs fichiers déjà pris par glob sont aussi ajoutés explicitement puis dédupliqués. La règle O3 filtre `AUDIO_C_SOURCES`, variable jamais initialisée, tandis que la règle effective applique O2 à `AUDIO_ALL_SOURCES`. Trois exclusions pointent vers des fichiers absents. La matrice n'offre pas de Debug Premium.
- **Cause racine :** accumulation de stratégies de sélection et de réglages de compilation sans source unique.
- **Conséquence / déclencheur :** un nouveau moteur peut être absent d'un build déjà configuré, un fichier supprimé rester référencé dans un arbre généré, et les profils ne valident pas tous les mêmes conditions.
- **Impact :** reproductibilité et diagnostic de parité de variante ; pas de bug runtime démontré.
- **Correction recommandée / patch minimal probable :** choisir listes explicites ou globs auto-invalidants, supprimer seulement les règles démontrées sans effet et ajouter un contrôle de matrice. Ne pas changer les niveaux d'optimisation sans mesure.
- **Validations / régression :** configure à neuf des cinq presets, comparaison des listes de sources et flags, ajout/suppression sentinelle ; risque faible à moyen de source oubliée. Indépendant des corrections runtime.

### Z1-001 — file Stack : perte silencieuse de commandes et drain IRQ en rafale

- **Priorité / statut / zones :** P0, `CONFIRMED`, Z1 avec Z4/Z5.
- **Fichiers et symboles :** `Src/Core/brick6_stack_runtime.c` (`STACK_COMMAND_QUEUE_CAP`, `brick6_stack_runtime_submit_command`, `brick6_stack_runtime_process_commands_from_audio`) ; `Src/Keyboard/keyboard_engine.c` ; `Src/Seq/seq_output_guard.c` ; `Src/Core/brick6_audio_runtime.c`.
- **Preuve :** la ring de 256 entrées retourne zéro si elle est pleine, sans compteur. Les appelants Note Off/All Notes Off et paramètres ignorent généralement ce retour. Le consommateur audio vide ensuite la file entière avec `for (;;)`, jusqu'à 255 commandes dans un appel. Le mécanisme `cancel_note_state` ne couvre que `BRICK6_STACK_MAX_INSTANCES`, alors que la soumission accepte `BRICK6_STACK_VOICE_INSTANCE_COUNT`.
- **Cause racine :** même file utilisée pour événements de sécurité note et réglages, sans priorité ni budget de consommation.
- **Conséquence / déclencheur :** burst de notes + réapplication de paramètres : Note Off perdu, voix bloquée ; au block suivant, pic CPU proportionnel au backlog maximal.
- **Impact :** stabilité audio et intégrité des notes ; une simple protection `PRIMASK` ne deviendra pas inter-cœur.
- **Correction recommandée / patch minimal probable :** métriques d'overflow, réservation/priorité des commandes terminales et quota fixe par block avec stratégie de rattrapage sûre ; aligner le cancel sur la cardinalité réellement supportée.
- **Validations / régression :** saturation artificielle, garantie All Notes Off, charge CPU maximale, 16 voix ; risque élevé sur ordre des commandes. Lié à `Z4-002`/`Z4-003`.

### Z1-002 — runtime MIDI FX multi-écrivain main/IRQ

- **Priorité / statut / zones :** P0, `CONFIRMED`, Z1 avec Z3/Z4/Z5/Z6.
- **Fichiers et symboles :** `Src/NoteFx/note_fx_engine.c`, `Src/NoteFx/note_fx_pipeline.c` (`g_note_fx_override_*`, `g_note_fx_runtime_arp_slot`, `note_fx_pipeline_submit`, `note_fx_pipeline_process`, `note_fx_pipeline_cleanup_track`, `note_fx_pipeline_apply_runtime_param`) ; `Src/NoteFx/note_fx_state.c` ; `Src/Audio/audio.c` ; `Src/Param/param_registry.c` ; `Src/Keyboard/keyboard_engine.c` ; `Src/Seq/seq_play_scheduler.c`.
- **Preuve :** l'audio appelle `note_fx_pipeline_process` et le scheduler soumet depuis la timeline audio ; UI/param/restore appellent aussi submit, cleanup, changement de base et changement de modèle. Ces chemins mutent les mêmes slots, overrides, ARP, tokens et notes possédées sans section critique, file SPSC ni publication par génération. Un cleanup hors IRQ peut en plus émettre des notes terminales pendant que l'audio parcourt l'état.
- **Cause racine :** nouveau pipeline branché uniformément sur trois producteurs sans définir un propriétaire d'exécution unique.
- **Conséquence / déclencheur :** édition MODEL, focus/clear/restore ou note clavier pendant un process audio : corruption logique de liste/ownership, double Note Off, note orpheline ou échéance incohérente.
- **Impact :** P0 monocœur et obstacle direct au split ; les tests hôte fonctionnels ne simulent pas la préemption.
- **Correction recommandée / patch minimal probable :** figer le contrat d'ownership puis faire transiter les mutations externes par une frontière bornée consommée par le propriétaire ; conserver `note_fx_state` comme base canonique et ne pas persister le runtime.
- **Validations / régression :** stress concurrent MODEL/p-lock/keyboard/scheduler/panic, invariants tokens/owned notes, CPU par block ; risque élevé. Dépend de `GLOBAL-001`, `Z4-002` et `Z4-003`.

### Z2-001 — binding `track_runtime` publié en place

- **Priorité / statut / zones :** P0, `CONFIRMED`, Z2 avec Z1/Z3/Z4/Z5/Z6.
- **Fichiers et symboles :** `Src/Core/track_runtime.c` (`g_track_runtime_ctx`, `track_runtime_refresh_all`, `track_runtime_refresh_track`, `track_runtime_get_ctx`, `track_runtime_rebuild_mix_track_reverse_map`, `g_track_runtime_revision`) ; `Src/Core/brick6_audio_runtime.c`.
- **Preuve :** `refresh_all` reconstruit successivement chaque entrée de `g_track_runtime_ctx`, puis les allocations, reverse maps et révisions. `refresh_track` affecte une structure puis recalcule plusieurs projections. L'audio parcourt directement les pointeurs retournés par `track_runtime_get_ctx`. La garde IPSR empêche un refresh déclenché depuis IRQ, mais n'empêche pas l'IRQ de préempter un refresh main et de lire un mélange ancien/nouveau.
- **Cause racine :** invalidation explicite correcte, mais publication confondue avec reconstruction de la projection.
- **Conséquence / déclencheur :** changement family/type, paste/restore ou ownership input pendant l'audio : engine/instance/lane/reverse map temporairement incohérents, mauvaise cible DSP ou ressource reset pendant son usage.
- **Impact :** stabilité audio, séparation logique/physique et futur dual-core critiques.
- **Correction recommandée / patch minimal probable :** construire dans une table shadow puis publier atomiquement une génération cohérente à une frontière autorisée ; limiter la durée de validité des pointeurs retournés. Ne pas déplacer l'autorité hors `track_runtime`.
- **Validations / régression :** transitions répétées sous audio, quotas/inputs/polyphonie, cohérence forward/reverse map et révisions ; risque élevé sur restore et reapply. Dépend de `GLOBAL-001`.

### Z3-001 — identité de paramètres brouillée par réutilisation d'IDs legacy

- **Priorité / statut / zones :** P1, `CONFIRMED`, Z3 avec Z4/Z6.
- **Fichiers et symboles :** `Inc/Param/param_store.h` (enum `param_id_t`, aliases `PARAM_CFG_POLY_*`, `PARAM_DRUM_MD_*`, reverb et IDs `PARAM_MIX_TRACKx_*`) ; `Src/Param/param_registry_catalog.c` ; `Src/Seq/seq_param_iface.c`.
- **Preuve :** plusieurs noms produit récents sont des aliases numériques d'anciens paramètres physiques MIX. Le catalogue utilise ces aliases comme désignateurs, tandis que logs, dumps ou outils peuvent afficher un autre nom du même entier. La même enum sert de protocole entre catalogue, p-lock, snapshots et persistence.
- **Cause racine :** conservation de layout par recyclage symbolique plutôt que métadonnée explicite de compatibilité. La rétrocompatibilité disque n'est pourtant pas requise pendant le prototype.
- **Conséquence / déclencheur :** diagnostic ou futur transport d'un ID sans son domaine : mauvaise interprétation humaine, validation incomplète et risque de dispatcher selon un ancien sens.
- **Impact :** maintenabilité et contrat d'interface futur ; aucun mauvais apply courant n'a été démontré.
- **Correction recommandée / patch minimal probable :** audit exhaustif ID→domaine→owner, table canonique testée et politique explicite de tombstones/aliases ; ne pas renuméroter avant d'avoir vérifié tous les payloads.
- **Validations / régression :** unicité des descripteurs effectifs, round-trip slot/param/persistence, fichiers v3 courants ; risque élevé si renumérotation. Lié à `DOC-002` et `DOC-006`.

### Z3-002 — surface granular active sans backend actif

- **Priorité / statut / zones :** P3, `CONFIRMED`, Z3 avec Z0/Z1/Z5.
- **Fichiers et symboles :** `Src/Audio/fx_granular.cpp` ; `CMakeLists.txt` ; `Inc/Param/param_store.h` (`PARAM_GRAN_*`) ; `Src/Param/param_registry_catalog.c` ; `Src/Param/param_registry_apply_wrappers.c` (`apply_gran_*`) ; `Src/UI/ui_param.c`.
- **Preuve :** le fichier DSP granular est explicitement retiré du build ; les six paramètres restent catalogués et présents dans une banque UI de fallback, mais leurs wrappers apply sont des no-op. Z1 les décrit comme tombstones.
- **Cause racine :** backend retiré sans séparation nette entre IDs réservés et surface sélectionnable.
- **Conséquence / déclencheur :** si la banque fallback devient atteignable, l'utilisateur peut éditer des contrôles sans effet ; sinon le code augmente seulement la surface de diagnostic.
- **Impact :** dette legacy limitée, pas un défaut audio démontré.
- **Correction recommandée / patch minimal probable :** audit d'atteignabilité UI puis masquer les tombstones tout en conservant les IDs si nécessaire ; supprimer le backend exclu seulement après décision produit.
- **Validations / régression :** résolution de toutes les pages/families et stabilité des IDs. Risque faible à moyen. Lié à `Z0-002`.

### Z4-001 — rattrapage de clock externe non borné dans l'IRQ audio

- **Priorité / statut / zones :** P0, `CONFIRMED`, Z4 avec Z1/MIDI.
- **Fichiers et symboles :** `Src/Seq/seq_runtime_exec.c` (`seq_runtime_exec_consume_external_step_pulses_pending`, `seq_runtime_exec_drive_external_steps_for_block`, `seq_runtime_exec_process_step_pulse_at_sample_q16`) ; producteurs de clock dans `Src/Seq/seq_runtime.c` et `Src/MIDI/midi.c`.
- **Preuve :** le domaine audio consomme un `uint16_t pending_steps`, puis exécute `while (pending_steps > 0)` sans plafond par block. Chaque pulse traverse le traitement de step. Tous les pulses rattrapés utilisent en outre `block_start_sample`, et `block_frames` est ignoré dans cette fonction.
- **Cause racine :** compteur d'accumulation traité comme backlog intégral plutôt que comme entrée bornée avec politique d'overflow.
- **Conséquence / déclencheur :** longue interruption de consommation, burst ou source externe défectueuse : jusqu'à 65535 traitements dans un callback audio, deadline manquée et événements temporellement écrasés au début du block.
- **Impact :** violation hard real-time directe et frontière externe non transportable telle quelle.
- **Correction recommandée / patch minimal probable :** plafond strict par block, diagnostic d'overflow et politique explicite de coalescence/drop/rephase fondée sur les timestamps disponibles ; pas de nouvelle architecture de transport globale.
- **Validations / régression :** fuzz de clock, pause/reprise, tempo extrême, mesure worst-case et phase ; risque élevé sur sync externe. Lié à `Z4-002`.

### Z4-002 — insertion Note On/Note Off non atomique dans le scheduler

- **Priorité / statut / zones :** P0, `CONFIRMED`, Z4 avec Z1.
- **Fichiers et symboles :** `Src/Seq/seq_play_scheduler.c` (`SEQ_PLAY_SCHEDULER_EVENT_CAP`, `seq_play_scheduler_push`, `seq_play_scheduler_push_note_pair`, `g_seq_play_diag`).
- **Preuve :** `push_note_pair` appelle deux fois indépendamment `seq_play_scheduler_push`. Avec une seule place libre, le Note On est accepté et le Note Off est rejeté ; le seul effet du rejet est l'incrément de `queue_overflow_drop_count`. La capacité actuelle est 512, mais aucune priorité ni réservation de paire n'existe.
- **Cause racine :** file d'événements générique sans transaction pour une unité sémantique note.
- **Conséquence / déclencheur :** densité de rolls/p-locks/notes proche de la capacité : voix moteur ou note MIDI bloquée jusqu'au panic.
- **Impact :** intégrité musicale et sécurité terminale P0.
- **Correction recommandée / patch minimal probable :** réserver la paire atomiquement ou refuser le Note On si sa terminaison ne peut être garantie ; protéger les Note Off/All Notes Off.
- **Validations / régression :** saturation à chaque nombre de places restantes, rolls maximaux, générations/clear/panic ; risque moyen sur politique de drop audible. Lié à `Z1-001` et `Z4-003`.

### Z4-003 — la file MIDI USB peut perdre un Note Off déjà acquitté par le guard

- **Priorité / statut / zones :** P0, `CONFIRMED`, Z4 avec Z1/Z0.
- **Fichiers et symboles :** `Src/Seq/seq_play_scheduler.c` (`seq_play_scheduler_emit_midi_note_raw`, `seq_play_scheduler_dispatch_terminal_note_to_channel`) ; `Src/MIDI/midi.c` (`MIDI_USB_TX_QUEUE_LEN`, `usb_tx_queue_push`, `usb_device_enqueue_packet`, `midi_note_off`) ; `Src/Seq/seq_output_guard.c`.
- **Preuve :** l'émission terminale appelle `midi_note_off`, API `void`, puis marque immédiatement le Note Off comme vu dans `seq_output_guard`. Le backend USB rejette pourtant un paquet quand sa file de 128 est pleine et ne remonte que `midi_tx_stats.tx_mb_drops`. Aucun traitement prioritaire des Note Off n'existe. Le DIN est actuellement un stub, donc aucune seconde sortie ne compense la perte.
- **Cause racine :** acquittement placé avant la confirmation d'acceptation du transport, avec une API qui masque l'échec.
- **Conséquence / déclencheur :** burst MIDI FX/scheduler/clock remplissant la file : Note Off perdu côté périphérique et guard convaincu que la note est fermée.
- **Impact :** note externe bloquée et difficulté à définir un ownership de sortie futur.
- **Correction recommandée / patch minimal probable :** résultat d'enqueue explicite, politique prioritaire/garantie pour messages terminaux et acquittement du guard seulement selon le contrat retenu.
- **Validations / régression :** saturation USB avec Note On/Off/clock, déconnexion/reconnexion, panic ; risque élevé sur ordre et débit MIDI. Dépend de `Z4-002`, `Z1-002` et `Z0-001`.

### Z5-001 — file d'événements UI avec perte silencieuse

- **Priorité / statut / zones :** P1, `CONFIRMED`, Z5 avec Z0.
- **Fichiers et symboles :** `Src/UI/ui_event.c` (`UI_EVENT_Q_LEN`, `g_ui_evt_q`, `ui_event_push`, `ui_event_poll`) ; services de boutons/Hall dans `Src/UI` et `Src/App/Hall`.
- **Preuve :** la ring de 32 entrées abandonne silencieusement l'événement lorsque l'index suivant rejoint la lecture. Elle n'expose ni compteur, ni high-water, ni resynchronisation de l'état physique. Les événements incluent press/release.
- **Cause racine :** file dimensionnée sans politique d'overflow pour des événements d'état.
- **Conséquence / déclencheur :** burst d'entrées pendant un tour superloop long : release perdu, SHIFT/TRACK/Hall ou interaction maintenue logiquement, raccourci involontaire.
- **Impact :** UX live et diagnostic ; `Z0-001` et Z6 augmentent la probabilité.
- **Correction recommandée / patch minimal probable :** instrumentation d'abord, puis coalescence des niveaux ou resync périodique depuis l'état physique ; ne pas simplement agrandir sans mesure.
- **Validations / régression :** burst maximal boutons/Halls/encodeurs pendant SD/USB, invariants release, compteur overflow ; risque moyen sur ordre des gestes. Dépend de `Z0-001`, `Z6-001`, `Z6-002`.

### Z6-001 — budget de chargement Pattern seulement nominal

- **Priorité / statut / zones :** P1, `CONFIRMED`, Z6 avec Z0/Z1/Z5.
- **Fichiers et symboles :** `Src/Core/brick6_app_init.c` (`brick6_app_process`) ; `Src/Storage/pattern_live_ram.c` (`pattern_load_service`) ; `Src/Storage/pattern_sd_bank.c` (`pattern_sd_bank_load_slot`).
- **Preuve :** le superloop appelle `pattern_load_service(4096)`. La fonction vérifie seulement que le budget est non nul, puis appelle en une fois `pattern_sd_bank_load_slot`, qui ouvre, lit et valide le snapshot complet avant de fermer. Le nombre 4096 ne limite aucun volume ni temps de travail.
- **Cause racine :** API de service budgétée enveloppant une transaction FatFs synchrone.
- **Conséquence / déclencheur :** recall Pattern sur SD lente/fragmentée pendant streaming : tour superloop long, retard cache/UI et underrun plausible.
- **Impact :** stabilité du streaming et fausse garantie architecturale.
- **Correction recommandée / patch minimal probable :** mesurer le worst-case puis transformer seulement le load en machine par phases/chunks ou renommer honnêtement le contrat si le coût est acceptable et le recall interdit sous charge.
- **Validations / régression :** SD lente, erreurs partielles, checksum, recall sous Stream/Looper ; risque élevé sur atomicité du restore. Lié à `Z0-001` et `Z6-002`.

### Z6-002 — export RAW monopolise volontairement un tour de superloop

- **Priorité / statut / zones :** P1, `CONFIRMED`, Z6 avec Z0/Z1/Z5.
- **Fichiers et symboles :** `Src/Core/brick6_app_init.c` (`brick6_app_process`) ; `Src/Storage/looper_storage.c` (`looper_storage_raw_export_service`, `g_storage_shared_io`) ; `Src/Storage/sd_access_gate.c`.
- **Preuve :** quand l'export est actif, le superloop appelle le service avec 516096 octets et saute les services Sampler, Looper normal, multi-sample, Pattern, waveform cache et preview. Le service conserve le gate SD et boucle tant que le budget autorise des chunks ; le budget courant permet plusieurs grosses lectures/écritures FatFs dans un seul appel.
- **Cause racine :** optimisation de débit d'export prioritaire sur la latence coopérative.
- **Conséquence / déclencheur :** export après prise pendant audio ou interactions : latence UI, retard de refill et contention SD. Le gate arbitre l'exclusion, pas le temps de possession.
- **Impact :** UX live et risque audio dépendant de la marge cache ; le worst-case n'est pas documenté par mesure.
- **Correction recommandée / patch minimal probable :** plafonner par temps/chunks mesurés et maintenir les services vitaux entre chunks ; conserver l'atomicité des phases fichier.
- **Validations / régression :** export maximal sous playback/stream, débit, fichier final et recovery erreur/alimentation ; risque moyen à élevé. Dépend de `Z6-001`.

### Z6-003 — métadonnée `dirty_pending_persist` sans lecteur et au sens inversé

- **Priorité / statut / zones :** P3, `CONFIRMED`, Z6.
- **Fichiers et symboles :** `Src/Storage/pattern_live_ram.c` (`pattern_live_slot_meta_t.dirty_pending_persist`, `pattern_live_capture_slot`, `pattern_live_init`).
- **Preuve :** le champ est initialisé à zéro et mis à un uniquement après la réussite de `pattern_sd_bank_store_slot`; aucune lecture utile n'existe dans le code. Le commentaire voisin demande au contraire un futur save différé.
- **Cause racine :** embryon de persistence différée jamais connecté.
- **Conséquence / déclencheur :** aucun bug runtime courant démontré, mais le nom suggère à tort une garantie de retry et peut tromper un futur correctif de save.
- **Impact :** maintenabilité locale.
- **Correction recommandée / patch minimal probable :** supprimer le champ après confirmation locale, ou implémenter séparément une vraie file de save seulement si le produit l'exige.
- **Validations / régression :** recherche de références générées/out-of-tree et tests save/recall ; risque faible. Indépendant.

### DOC-001 — `AGENT.md` et `AGENTS.md` décrivent deux produits différents

- **Priorité / statut / zones :** DOC, `DOC DIVERGENCE`, GLOBAL.
- **Fichiers et symboles :** `AGENT.md`, `AGENTS.md`, `Inc/Core/track_topology.h`, `Src/Core/track_topology.c`, `Inc/UI/ui_core.h`.
- **Preuve :** `AGENT.md` et le code décrivent 8 Play + Special fixes, 12 tracks Low-Cost/14 Premium, Input1 ou Input1..3 et une page MIDI FX. `AGENTS.md` décrit encore 14 tracks uniformes, Input1..4, Hybrid, family Master et hall mode ARP.
- **Cause racine :** guide racine dupliqué non synchronisé.
- **Conséquence / déclencheur / impact :** un audit ou patch guidé par `AGENTS.md` peut réintroduire précisément les modèles supprimés ; fort risque de régression transverse, sans preuve que le code est faux.
- **Correction recommandée / patch minimal probable :** choisir un seul document d'instructions autoritatif et faire de l'autre un renvoi généré ou minimal.
- **Validations / régression / dépendances :** comparaison automatisée des invariants ; risque documentaire faible, priorité de nettoyage élevée. Prérequis aux audits locaux.

### DOC-002 — politique de version de persistence contradictoire avec le code

- **Priorité / statut / zones :** DOC, `DOC DIVERGENCE`, Z6/Z3.
- **Fichiers et symboles :** `AGENT.md`, tête de `docs/architecture/z6_state_persistence_patterns_projects.md`, `Src/Storage/pattern_sd_bank.c` (`PATTERN_VERSION`), `Inc/Storage/project_v1.h`, `Inc/Storage/patch_sd_bank.h`, `Inc/Storage/kit_sd_bank.h`.
- **Preuve :** le guide impose de garder les versions à 1, tandis que les quatre formats courants utilisent la version 3 et que les addendums récents Z6 documentent des évolutions de payload.
- **Cause racine :** règle de prototype non mise à jour après migrations de structures.
- **Conséquence / déclencheur / impact :** futur changement de payload susceptible de suivre la mauvaise règle ; compatibilité non requise ne signifie pas absence de validation de format.
- **Correction recommandée / patch minimal probable :** documenter la politique réelle de rupture prototype et la validation exacte de chaque format, sans changer les numéros pendant l'audit.
- **Validations / régression / dépendances :** fixtures v3 courantes et comportement de rejet ; lié à `Z3-001`.

### DOC-003 — README ambigu sur families/types configurables

- **Priorité / statut / zones :** DOC, `DOC DIVERGENCE`, Z2/Z5.
- **Fichiers et symboles :** `readme.md` (listes Families/Types), `Src/Core/track_topology.c`, `Src/UI/ui_track_catalog.c`, `Src/UI/ui_core.c`.
- **Preuve :** le README explique correctement les Special fixes, mais liste ensuite Input1..3 et Master parmi les families et Looper parmi les types Sampler sans distinguer clairement représentation interne et choix CFG. Le catalogue code interdit ces choix sur Play et dérive les Special de la topologie.
- **Cause racine :** sections produit de générations différentes juxtaposées.
- **Conséquence / déclencheur / impact :** attentes produit et tests manuels erronés ; pas de défaut runtime établi.
- **Correction recommandée / patch minimal probable :** séparer explicitement catalogue CFG Play et identités/types de représentation Special.
- **Validations / régression / dépendances :** comparer aux tests `track_topology_validation.ps1` et `special_track_role_validation.ps1`; lié à `DOC-001`.

### DOC-004 — documents Z canoniques devenus des journaux contradictoires

- **Priorité / statut / zones :** DOC, `DOC DIVERGENCE`, Z1 à Z6.
- **Fichiers et symboles :** `docs/architecture/z2_track_runtime_authority.md`, `z3_param_modulation_control.md`, `z4_seq_clock_scheduler.md`, `z5_ui_navigation_interaction.md`, `z6_state_persistence_patterns_projects.md`.
- **Preuve :** des sections anciennes affirment Sampler/Looper configurable, groupes/master liés, hall mode ARP, absence de persistence MIDI FX ou versions 1 ; les addendums les plus récents affirment Special Looper fixe, pistes indépendantes, page MIDI FX et persistence des bases. Z4 documente encore une capacité scheduler de 256 alors que `SEQ_PLAY_SCHEDULER_EVENT_CAP` vaut 512.
- **Cause racine :** politique append-only sans consolidation ni marquage d'obsolescence.
- **Conséquence / déclencheur / impact :** la lecture obligatoire intégrale ne permet pas de savoir quelle section canonique est active sans refaire l'audit code.
- **Correction recommandée / patch minimal probable :** consolider chaque tête de zone en état courant et déplacer l'historique dans un changelog clairement non canonique.
- **Validations / régression / dépendances :** table code→contrat par zone ; à faire après confirmation des tickets, sans modifier le runtime.

### DOC-005 — audit mémoire non reproductible sur l'état courant

- **Priorité / statut / zones :** DOC, `DOC DIVERGENCE`, GLOBAL/Z0/Z1.
- **Fichiers et symboles :** `docs/architecture/memory_audit_internal_ram.md`, `build/Release/BRICK6_CUBE.map`, `build/Premium/BRICK6_CUBE.map`, fichiers `.su`, linkers Board.
- **Preuve :** le document ouvre sur une baseline FLASH 462792, DTCM 72512 et D1 430752, puis contient des addendums de déplacements sans nouvelle baseline complète ; il reconnaît lui-même que le map disponible manque des blocs majeurs. Les maps worktree courantes incluent de nouveaux états NoteFx/seq/UI et diffèrent entre variantes. Aucun seuil automatisé de région ou stack n'est déclaré.
- **Cause racine :** audit ponctuel enrichi après modifications sans régénération atomique des mesures.
- **Conséquence / déclencheur / impact :** décision de placement mémoire fondée sur une marge obsolète, particulièrement dangereuse avant H747.
- **Correction recommandée / patch minimal probable :** futur rapport généré depuis builds propres des variantes, avec date/hash/options et seuils ; aucune relocalisation avant profilage.
- **Validations / régression / dépendances :** builds propres et comparaison map/size/stack ; lié à `GLOBAL-002` et `Z0-002`.

### DOC-006 — annexes d'autorité et audit dual-core partiellement périmés

- **Priorité / statut / zones :** DOC, `DOC DIVERGENCE`, GLOBAL/Z3/Z4/Z5.
- **Fichiers et symboles :** `docs/architecture/annexe_z3_param_authority_matrix.md`, `annexe_z4_dual_core_preparation_summary.md`, annexes Z5 et `audit_dual_core_readiness.md`; `Inc/Param/param_store.h` (commentaire `PARAM_PERSIST_COUNT`).
- **Preuve :** plusieurs annexes citent des numéros de ligne, anciens symboles ARP ou capacités antérieures. Le commentaire de `PARAM_PERSIST_COUNT` dit encore que MIDI FX attend une étape future, alors que `PatternSaveV1.note_fx` persiste déjà les bases hors du tableau générique. L'audit dual-core existant reste utile mais ne couvre pas proprement le worktree MIDI FX courant et duplique des constats sans IDs stables.
- **Cause racine :** documents instantanés maintenus à côté des canoniques sans mécanisme de péremption.
- **Conséquence / déclencheur / impact :** preuves fragiles et décisions dual-core basées sur un ancien graphe.
- **Correction recommandée / patch minimal probable :** rattacher les annexes à une révision, remplacer les lignes par symboles et faire de ce registre l'index stable, sans effacer l'historique utile.
- **Validations / régression / dépendances :** revue après audits locaux Z3/Z4/Z5 ; pas d'impact runtime.

## 4. Code mort et legacy

### Suppression probablement sûre et démontrée

- `Src/Param/control_router.c::control_router_set_param` est compilé par le glob Param, mais aucune référence utile n'existe dans `Src`, `Board` ou les tests ; seules sa déclaration, sa définition et la documentation le citent. Une suppression future doit retirer ensemble header/source et vérifier les consommateurs externes éventuels.
- Les trois entrées `EXCLUDED_SOURCES` visant `Src/Audio/live_recorder.c`, `Src/Audio/recorder_transport.c` et `Src/Core/brick6_recorder_runtime.c` ciblent des chemins absents : elles n'ont aucun effet dans ce worktree.
- `pattern_live_slot_meta_t.dirty_pending_persist` n'a aucun lecteur ; voir `Z6-003`.

### Encore compilé ou exposé sans rôle produit clair

- Les `PARAM_GRAN_*`, leur catalogue et leurs wrappers no-op restent compilés, alors que `fx_granular.cpp` est exclu ; voir `Z3-002`.
- `usb_host_tasklet_poll_bounded` est compilé sur les deux variantes mais sans appel. Z0 documente qu'il a été laissé disponible ; ce n'est donc pas une suppression automatique.
- `brick6_stack_runtime_cancel_note_state` n'a pas de caller produit observé mais est exercé par `tests/stack_vca_release_test.c`; son contrat partiel 8/16 instances doit être décidé dans l'audit Z1.

### Suspect nécessitant confirmation produit

- `backend_usb_host_send` et `backend_din_send` sont des stubs explicites. Ils ne sont pas classés comme dette fonctionnelle : la complétude produit n'est pas établie par cet audit.
- L'ancien alias de capacité ARP a été supprimé lors de l'étape 3C1 après confirmation que `TRACK_CAPABILITY_MIDI_FX` est l'unique sémantique runtime; les vues historiques ARP restent à lire comme historique ou modèle MIDI FX.
- Les wrappers `audio.h`/mixer historiques et paramètres physiques `PARAM_MIX_TRACKx_*` exigent une analyse de symboles/link map avant suppression, car certains représentent encore les lanes physiques.

### Historique documentaire obsolète

- Modèle Input1..4/Hybrid/Master family dans `AGENTS.md`.
- Sections ARP hall mode, groupes liés, Looper configurable et persistence v1 antérieures aux addendums courants dans Z2–Z6.
- Numéros de ligne et capacités figées dans les annexes et anciens audits.

## 5. Divergences documentaires

Les divergences actionnables sont indexées par `DOC-001` à `DOC-006`. En synthèse : `AGENT.md` reflète beaucoup mieux le code courant que `AGENTS.md`; le README mélange modèle CFG et représentation des Special ; les têtes et addendums Z ne forment plus un contrat univoque ; la politique de versions contredit les formats v3 ; les audits mémoire/dual-core ne sont pas des baselines reproductibles du worktree actuel. Aucune de ces divergences n'établit que le runtime doit être modifié.

## 6. Impact sur le futur dual-core

### Dette à corriger même en monocœur

`GLOBAL-001`, `Z0-001`, `Z1-001`, `Z1-002`, `Z2-001`, `Z4-001`, `Z4-002`, `Z4-003`, puis les faux budgets Z6. Ces sujets ont des scénarios de panne indépendants d'un second cœur.

### Couplages qui compliqueront les frontières

- Pointeurs directs vers `g_track_runtime_ctx` et état DSP mutable.
- Producteurs main et IRQ mutant le même runtime NoteFx.
- Files protégées par désactivation locale des IRQ : `PRIMASK` ne sérialise pas un autre cœur.
- Scheduler terminal couplant dans le même appel moteurs, MIDI, output guard et NoteFx.
- Services SD/USB dont le temps de possession n'est pas borné.

### Seams déjà propres à préserver

- `track_topology` sépare identité Play/Special, capacité stockage et variante physique.
- `track_state` et les transitions bulk/restore fournissent une autorité de mutation explicite.
- `track_runtime` centralise réellement les mappings logique→physique ; le défaut porte sur la publication, pas sur l'existence de cette autorité.
- `seq_runtime_exec` matérialise une frontière timeline/block et les structures d'événements sont de taille fixe.
- `note_fx_state` distingue bases persistables et runtime transitoire.
- `sd_access_gate` rend les clients visibles, même s'il ne borne pas encore le temps de possession.

### Ownership et mémoire encore ambigus

Mixer, FX globaux, NoteFx, MIDI TX et certaines projections de paramètres n'ont pas un écrivain unique. Les sections DTCM/D1/D2/D3/SDRAM et buffers DMA sont conçues pour le H743 monocœur ; leur visibilité, cacheabilité et ownership ne peuvent pas être extrapolés au H747 à partir des macros actuelles.

### À reporter au portage réel

Choix des cœurs, protocole et granularité IPC, mailbox, placement des heaps/stacks, cache maintenance inter-cœur, ownership des IRQ et périphériques. Les APIs actuelles ne doivent pas recevoir une façade « dual-core » spéculative avant que les P0 monocœur soient fermés et que les coûts soient mesurés.

### Décisions impossibles sans mesures

Placement de moteurs hors DTCM, taille exacte des files, quotas SD/USB, taille de stack par cœur et distribution des services. Les maps propres, high-water, temps maximum par service et charge IRQ sont des prérequis.

## 7. Plan des audits locaux suivants

| Ordre | Audit | Questions et tickets à confirmer | Dépendances à lire | Critère de sortie | Parallèle |
|---|---|---|---|---|---|
| 1 | Z1 hard-RT/audio | Ownership mixer/FX/NoteFx, borne de la file Stack, stack IRQ : `GLOBAL-001`, `GLOBAL-002`, `Z1-001`, `Z1-002` | Z2 binding, Z3 apply, Z4 terminal | graphe d'appel IRQ complet, bornes chiffrées, liste minimale de publications | Non, base des autres audits critiques |
| 2 | Z4 clock/scheduler/MIDI | Catch-up externe, atomicité note, overflow MIDI : `Z4-001..003` | résultats Z1, `midi.c`, NoteFx, output guard | worst-case par block et invariant « aucun On accepté sans fermeture garantie » | Non avec Z1 ; ensuite oui avec Z0 |
| 3 | Z2 runtime authority | Transaction de binding et durée des pointeurs : `Z2-001` | Z1 consommateurs, Z3 transitions, Z6 restore | état forward/reverse cohérent à toute observation audio | Oui avec Z0 après gel des interfaces Z1 |
| 4 | Z0 cadence/build | Latence USB, matrice sources/flags : `Z0-001`, `Z0-002`, `GLOBAL-002` | Board variants, Z1 budgets, Z6 services | temps max d'un tour et manifests reproductibles des cinq presets | Oui avec Z2 |
| 5 | Z6 persistence/SD | Réalité des budgets, atomicité erreur/retry : `Z6-001..003` | Z0 superloop, Z1 cache/writer, Z2/Z3 restore | chaque service a une borne ou une interdiction d'usage explicite, recovery testé | Non avec changements Z0 du superloop ; analyse statique possible en parallèle |
| 6 | Z3 paramètres/modulation | Identité des IDs, owners d'apply, granular legacy : `Z3-001`, `Z3-002` | résultats Z1/Z2/Z4, formats Z6 | matrice ID→domaine→owner→contexte complète, aucun alias ambigu non documenté | Oui avec Z6 une fois Z2 stabilisé |
| 7 | Z5 UI | Overflow événement, dépendances directes et stack UI : `Z5-001`, `GLOBAL-002` | Z0 cadence, Z2 capacités, Z3 commandes, Z6 workflows | invariants press/release/focus et aucune mutation DSP hors commande autorisée | Oui avec consolidation documentaire finale, pas avant Z0/Z2/Z3 |

Chaque audit doit travailler depuis un worktree stabilisé ou enregistrer précisément la révision fonctionnelle, car le présent audit a observé une migration MIDI FX non commitée.

## 8. Ordre recommandé des futurs correctifs

1. **Lot bloquant intégrité événementielle :** `Z4-001`, puis `Z4-002` et `Z4-003`, puis `Z1-001`. Petits patches séparés, chacun avec saturation et worst-case.
2. **Lot bloquant ownership audio :** contrat NoteFx `Z1-002`, publication binding `Z2-001`, puis champs mixer/FX de `GLOBAL-001`. Ne pas fusionner en bus générique.
3. **Lot cadence superloop :** `Z0-001`, `Z6-001`, `Z6-002`, avec budgets mesurés et services vitaux préservés.
4. **Lot garde mémoire :** mesure et seuils `GLOBAL-002`/`DOC-005`, puis seulement les gros temporaires prouvés.
5. **Nettoyages sûrs :** entrées CMake mortes, `control_router`, `Z6-003`, puis `Z0-002`; un changement par patch avec manifests comparés.
6. **Corrections structurelles non urgentes :** `Z3-001`, puis décision `Z3-002` après inventaire de persistence et UI.
7. **Documentation :** `DOC-001` et `DOC-002` d'abord, consolidation Z/README ensuite, audits mémoire/dual-core en dernier lorsque le code est stabilisé.
8. **À reporter au H747 :** IPC, affectation des cœurs, régions partagées et cache/cohérence.
9. **À ne pas toucher avant mesure ou découpage réel :** relocalisation massive DTCM/SDRAM, taille arbitraire des files, réécriture des moteurs, des formats ou de `ui_core` pour leur seule taille.
