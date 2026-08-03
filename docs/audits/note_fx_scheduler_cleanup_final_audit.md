# Audit final statique — nettoyage NoteFx, scheduler et événements de notes

## 1. Verdict

**Verdict structurel global : NON CONFORME.** Le chantier a introduit les briques attendues (événement riche, stages ordonnés, ledgers fixes, budget de demi-buffer, catalogue de divisions et terminal à admissions séparées), mais il ne ferme pas encore l'admission moteur dans tous les backends. Les lots correctifs 1 à 6 garantissent désormais réservation des fermetures, identité exacte, owner MPSC, transitions distinctes, admission USB atomique liée à une génération de connexion et defaults de modèle transactionnels.

**Verdict Euclid : `NON PRÊT — DETTES STRUCTURELLES OUVERTES`.**

Comptage D-001..D-019 après lots correctifs 1 à 6 : **12 fermées structurellement** (dont 8 avec validation dynamique restante), **6 partielles**, **1 non fermée**. D-020 est hors domaine. Revue des étapes : **4 terminées**, **6 partielles**, **0 non terminée**.

## 2. HEAD et commits contrôlés

- HEAD audité : `0d5cba9a0e9330d8276a108bc5747dbe800a11fd` (`feat: port Deluge Mutable reverb`, 2026-08-03).
- Plan initial : `6be85b892b4f11bff214e6e84d5d39da3b078f70` (`docs: plan note fx scheduler cleanup`).
- Politique mute/admission figée : `c152265b6d0cb1c4ee68d02cb60909d02cf4b0b7` (`docs: refine note fx mute and admission policies`).
- Implémentation des résultats annoncés pour les étapes 1 à 10 : un seul commit omnibus, `bbeaab1fb13e94db093a7afb65aa3f5a329f7f31` (`stable`). Il n'existe pas dix commits séparés correspondant aux dix étapes ni aux intitulés recommandés par le plan. La traçabilité étape par étape repose donc sur le diff de ce commit et sur les addenda du plan, pas sur des frontières de commits.
- Commits postérieurs contrôlés pour dérive sur le périmètre : `7ae8851e9`, `00ee35328`, `934a256ea`, `6b3d58fc7`, `801f00233`. Ils concernent surtout Multi, Undo et la topologie huit pistes; ils ne corrigent pas les écarts NoteFx/terminal relevés ci-dessous.
- Documents d'architecture modifiés dans le commit omnibus : `docs/architecture/z1_audio_hard_rt_mix.md`, `docs/architecture/z4_seq_clock_scheduler.md`, `docs/architecture/z6_state_persistence_patterns_projects.md` et `docs/architecture/memory_audit_internal_ram.md`.
- Le worktree était déjà très sale avant l'audit (588 entrées, dont des modifications fonctionnelles et les tests du périmètre marqués supprimés). Les preuves de code et de tests sont prises dans l'objet Git du HEAD; les suppressions locales préexistantes ne sont pas attribuées à cette passe. Dans le HEAD, les tests cités existent; dans le checkout courant, ils sont actuellement absents.

## 3. Résumé des 10 étapes

| Étape | Verdict | Fichiers et symboles principaux | Dettes | Invariant obtenu / manque restant | Validation restante |
|---:|---|---|---|---|---|
| 1 — contrat canonique | `PARTIELLE` | `Inc/NoteFx/note_fx_event.h`, `note_event_t`, `note_fx_pipeline_submit[_audio]`, `note_fx_pipeline_submit_source_occurrence` | D-001, D-017 | Structure fixe complète, occurrence obligatoire et retour `note_fx_result_t`; le seam live porte désormais l'identité producteur, mais pas encore le sample réel d'application. | Tests multi-producteurs et trace terminale réelle. |
| 2 — identité/atomicité | `STRUCTURELLEMENT TERMINÉE` | `note_fx_arp_t`, `note_fx_owned_t`, ledger producteur fixe, namespaces d'occurrence, `g_seq_play_active_occurrence`, handles synth | D-002, D-003 | Clavier/MIDI allouent une occurrence par On; Off retrouve le record exact FIFO; ARP, scheduler, terminal, synth poly et moteurs mono protégés conservent cette identité. | Retrigger inversé, mêmes hauteurs multi-source, wrap et voice steal. |
| 3 — timestamps/clock | `PARTIELLE` | `audio_apply_seq_event_at_sample`, `seq_runtime_exec_drive_external_steps_for_block` | D-009, D-010 | Le STEP conserve `sample_abs`; backlog traité au plus par 4 puis coalescé. Le live est horodaté avant consommation de commande et le débordement du pending 16 bits n'est pas compté. | Offsets réels, tempo, jitter et cycles H743. |
| 4 — owner/commandes | `PARTIELLE` | `g_note_fx_commands[32]`, sections PRIMASK, snapshots `note_fx_track_state_t`, politiques NoteFx distinctes | D-007, D-008 | Mutations runtime appliquées dans le domaine audio; publication MPSC sérialisée, commandes sans pointeur, configuration par snapshot et politiques de transition distinctes. | Interleavings main/IRQ, saturation de la ring et matrice des transitions. |
| 5 — quatre stages | `STRUCTURELLEMENT TERMINÉE` | `note_fx_engine_stage_source`, `note_fx_pipeline_stage_emit`, terminal stage 4 | D-005, D-006 | Parcours 0→1→2→3→4, continuation `slot+1`, aucun appel moteur/MIDI dans `Src/NoteFx`. | Test comportemental des quatre ordres et futurs fan-outs. |
| 6 — files/budgets | `PARTIELLE` | `note_fx_pipeline_begin_audio_half`, `NOTE_EVENT_FLAG_CLOSURE_RESERVED`, quotas On/Off/commandes, quota scheduler 128 | D-010, D-011, D-012 | Budget créé une fois par demi-buffer; réservation On+fermeture atomique, Off possédé prioritaire et ownership conservé sur refus; coût de cleanup jusqu'à 512 records non mesuré. | Saturations réelles et cycles H743 Low-Cost/Premium. |
| 7 — terminal/admission | `PARTIELLE` | `g_seq_terminal_admission`, `midi_note_*_admit`, `midi_usb_transport_generation`, `seq_output_guard` | D-004, D-013, D-014 | Masques interne/MIDI séparés; USB admis atomiquement seulement si configuré, réserve Off conservée et reconnexion détectée par génération; UART stub refusé. Les backends internes `void` restent supposés acceptés. | Quatre cas d'admission, moteur plein, USB plein/déconnecté/reconnecté. |
| 8 — transitions/mute | `STRUCTURELLEMENT TERMINÉE` | `seq_play_scheduler_destructive_transition`, fermeture terminale exacte, `NOTE_FX_TRANSITION_SOURCE_CLOCK_CHANGE` | D-008, D-015 | Mute STEP non destructif; chaque politique est transmise distinctement, ferme les admissions exactes avant purge/génération et conserve les ledgers tant qu'une destination refuse son Off. | Matrice complète, double action, saturation Off et notes actives. |
| 9 — divisions/defaults | `STRUCTURELLEMENT TERMINÉE` | `seq_division_catalog`, `g_note_fx_model_defaults`, `note_fx_state_set_param`, `note_fx_state_normalize_track` | D-016, D-017 | Catalogue musical unique; changement de modèle construit un snapshot complet avec les defaults cibles, restore normalisé avant publication et rollback si la commande owner est refusée. | Restore invalide, modèle aller/retour, clipboard/p-lock. |
| 10 — consolidation | `PARTIELLE` | scripts `note_fx_*`, audits Z1/Z4, docs architecture | D-018, D-019 | Plusieurs recherches négatives existent; scripts importants non enregistrés dans CMake, couverture surtout regex, APIs mortes et affirmations documentaires obsolètes subsistent. | Enregistrer/renforcer les tests puis exécuter ultérieurement la matrice. |

## 4. Registre D-001 à D-020

### D-001 — `PARTIELLE`

- Preuve/symboles : `note_event_t` porte sample, track, destination, note, vélocité, kind, provenance, stage, source token, occurrence et génération; `note_fx_result_t` porte le résultat. `note_event_is_valid()` exige maintenant une occurrence non nulle et `note_fx_pipeline_submit_source_occurrence()` reçoit l'identité créée par le producteur. Le timestamp live reste capturé avant consommation de la commande.
- Ancien supprimé / reliquat : l'ancien événement tronqué et le seam live pitch-only ont disparu; le timestamp différé reste à corriger.
- Invariant garanti : une fois construit, l'événement conserve son layout jusqu'au terminal.
- Reste : prouver l'identité dès chaque producteur et l'horodatage réel d'application. Impact Euclid : parent/enfant et réinjection ne peuvent pas s'appuyer sur un seam live ambigu. Passe corrective : **oui**.

### D-002 — `FERMÉE STRUCTURELLEMENT — VALIDATION DYNAMIQUE RESTANTE`

- Preuve/symboles : le clavier/MIDI tient `g_keyboard_engine_source_occurrence[128]`, alloue un identifiant namespacé par provenance et transmet cet identifiant à `note_fx_pipeline_submit_source_occurrence()`. `note_fx_pipeline_find_source()` recherche exclusivement `source_occurrence_id+provenance`; `note_fx_arp_note_on/off()` conserve token+génération. Le synth poly ferme par `occurrence_id`; les moteurs mono ne reçoivent un Off que si `g_seq_engine_mono_occurrence[track]` correspond encore.
- Ancien supprimé / reliquat : lookup live `(track,note,provenance)` supprimé du pipeline actif; les APIs synth pitch-only restent disponibles pour les chemins manuels historiques, mais ne sont plus utilisées par le terminal canonique.
- Invariant garanti : deux occurrences homonymes et deux provenances ont des identités distinctes; le FIFO producteur ferme l'ancienne occurrence avant un retrigger plus récent; un ancien Off ne libère ni la nouvelle voix synth ni le moteur mono retriggeré.
- Reste : tests Off inversés, doublons MIDI, wrap 30 bits et voice steal. Impact Euclid : identité exploitable de la source au moteur. Passe corrective : **non**, validation dynamique requise.

### D-003 — `FERMÉE STRUCTURELLEMENT`

- Preuve/symboles : `g_seq_play_active_occurrence[track][capacity]`, ajout/retrait par note+token+génération; `g_seq_play_active_event_token[track][note]` absent.
- Ancien supprimé : miroir one-token-per-pitch supprimé. La projection UI/Live Record par hauteur n'est pas l'autorité scheduler.
- Invariant : occurrences scheduler homonymes distinctes, table fixe, Off exact.
- Reste : tests non exécutés et wrap de génération. Impact Euclid : seam STEP exploitable. Passe corrective : **non pour cette dette**.

### D-004 — `PARTIELLE`

- Preuve/symboles : `seq_play_scheduler_dispatch_terminal_event()` reçoit l'événement complet et tient `g_seq_terminal_admission`. Il ne compare toutefois pas la génération reçue à une autorité de génération courante avant un nouveau On; il accepte tout entier non nul si la clé est libre. Les wrappers morts `dispatch_terminal_note[_to_channel]()` contournent encore le ledger complet.
- Ancien supprimé / reliquat : terminal tronqué remplacé dans le chemin NoteFx actif; wrappers sans identité encore présents.
- Invariant garanti : Off terminal retrouve `(track,occurrence,generation)` déjà admis. Non garanti : rejet de tout On stale et unicité absolue du terminal public.
- Reste : test stale après transition et suppression/privatisation des wrappers. Impact Euclid : un événement différé ancien peut recréer un ledger. Passe corrective : **oui**.

### D-005 — `FERMÉE STRUCTURELLEMENT`

- Preuve/symboles : `note_fx_engine_stage_source(event, slot, ...)` et continuation par `event->stage`; aucune recherche du premier ARP dans le chemin actif.
- Ancien supprimé : routage first-ARP et contrainte d'unicité globale.
- Invariant : chaque source démarre au slot 0 et chaque slot actif/off est visité dans l'ordre.
- Reste : validation comportementale non exécutée. Impact Euclid : placement arbitraire possible. Passe corrective : **non**.

### D-006 — `FERMÉE STRUCTURELLEMENT`

- Preuve/symboles : les On/Off ARP générés ont `stage=slot+1` et passent par `note_fx_pipeline_stage_emit()`; seul stage 4 appelle le terminal.
- Ancien supprimé : callback direct terminal depuis le moteur NoteFx.
- Invariant : pas de reprise au slot 1, profondeur maximale de quatre stages, absence d'appel MIDI/moteur dans `Src/NoteFx`.
- Reste : fan-out futur non implémenté et test chaîne non exécuté. Impact Euclid : continuation aval disponible. Passe corrective : **non pour le contrat de stage**.

### D-007 — `FERMÉE STRUCTURELLEMENT — VALIDATION DYNAMIQUE RESTANTE`

- Preuve/symboles : `note_fx_pipeline_enqueue()`, `_enqueue_batch()` et `_dequeue()` sérialisent head, tail, copie et publication sous PRIMASK avec barrières mémoire. Les commandes `SYNC/APPLY/RELEASE/RESET_TRACK` embarquent une copie fixe de `note_fx_track_state_t`; l'owner ne relit plus `g_note_fx_state` pendant l'application. `RESET_ALL` réserve et publie atomiquement huit commandes fixes. `note_fx_pipeline_submit_audio()` ne resynchronise plus la base à chaque événement.
- Ancien supprimé / reliquat : publication SPSC implicite et lecture owner d'une base externe mutable supprimées; la ring reste fixe, capacité utile 31, sans allocation dynamique.
- Invariant garanti : plusieurs producteurs ne peuvent plus publier sur le même head; une commande n'expose aucun pointeur mutable; les quatre slots d'une piste sont configurés depuis un seul snapshot sans traitement intercalé.
- Reste : interleavings main/IRQ, saturation 23/31 avec batch de huit, coût de la section critique et mesure H743. Impact Euclid : owner runtime exploitable structurellement. Passe corrective : **non**, validation dynamique requise.

### D-008 — `FERMÉE STRUCTURELLEMENT — VALIDATION DYNAMIQUE RESTANTE`

- Preuve/symboles : `seq_play_scheduler_note_fx_policy()` conserve distinctement STOP, PANIC, PATTERN, MODEL, DESTINATION et SOURCE_CLOCK; `seq_play_scheduler_destructive_transition()` ferme d'abord chaque `g_seq_terminal_admission` exact, refuse la purge si une destination reste ouverte, publie la commande NoteFx correspondante, puis purge et incrémente la génération. `clear[_tracks]` ne produit plus d'Off brut depuis la note source.
- Ancien supprimé / reliquat : forced Off pitch-only et rabattement systématique sur STOP/PANIC supprimés; les façades historiques `clear[_tracks]` restent comme alias explicites PANIC/STOP.
- Invariant garanti : ordre close→commande owner→purge→génération, fermeture limitée aux destinations admises, idempotence par ledger et suspension seulement pour MODEL/DESTINATION.
- Reste : matrice fonctionnelle, refus Off répété et coût de fermeture sous PRIMASK. Impact Euclid : transitions structurellement exploitables. Passe corrective : **non**, validation dynamique requise.

### D-009 — `PARTIELLE`

- Preuve/symboles : le chemin STEP réécrit `sample_abs` avec `block_start+offset` dans `audio_apply_seq_event_at_sample()`. Le chemin clavier/MIDI appelle `seq_runtime_exec_get_audio_timeline_sample()` avant mise en file; la commande est consommée plus tard et un passthrough terminal garde ce timestamp ancien au lieu du sample d'application.
- Ancien supprimé / reliquat : fin de bloc supprimée pour STEP; reconstruction live par projection de timeline subsiste.
- Invariant garanti : `block_start` est courant, `block_end` est exclu par `due_sample_time < block_end`. Non garanti : sample terminal exact pour live différé.
- Reste : offset 0/63, overdue live, tempo et frontière de bloc. Impact Euclid : première échéance live potentiellement recalée. Passe corrective : **oui**.

### D-010 — `PARTIELLE`

- Preuve/symboles : `SEQ_RUNTIME_EXEC_MAX_EXTERNAL_PULSES_PER_BLOCK=4`; surplus projeté sans boundaries et compté dans `external_pulses_coalesced`. Le compteur pending sature silencieusement à `0xFFFF` dans `seq_runtime_exec_increment_external_step_pulses_pending()` sans compteur de clocks abandonnées au-delà.
- Ancien supprimé / reliquat : drain arbitraire supprimé; pending sans timestamp et saturation muette subsistent.
- Invariant garanti : un snapshot de backlog ne déclenche au plus que quatre boundaries exécutés; aucune position SPP n'est inventée et le RX ne traite pas F2 comme position supportée.
- Reste : START/CONTINUE/STOP, très grand backlog, cycles/jitter H743. Impact Euclid : cadence bornée mais pertes extrêmes non observables. Passe corrective : **oui**.

### D-011 — `FERMÉE STRUCTURELLEMENT — VALIDATION DYNAMIQUE RESTANTE`

- Preuve/symboles : `note_fx_pipeline_budget_admit()` réserve atomiquement quota On et crédit Off, marque l'événement `NOTE_EVENT_FLAG_CLOSURE_RESERVED` et rembourse les deux crédits si l'admission aval refuse. `note_fx_engine_process()` effectue une première passe globale de fermetures avant toute génération; `release_slot()` et les chemins ARP ne libèrent `note_fx_owned_t` qu'après acquittement aval. Une fermeture refusée reste possédée et devient réessayable via `closing` ou `pending_source_close`.
- Ancien supprimé / reliquat : budget local par sous-segment et admission On/Off indépendante supprimés; les backends terminaux réels restent à qualifier.
- Invariant garanti : un On généré n'est publié qu'avec record owned et crédit de fermeture; un refus aval ne détruit plus ce record.
- Reste : saturation Off 0/1, refus aval puis retry, ordre Off/On, cleanup 8×4×16 et H743. Impact Euclid : socle génératif structurellement sûr, sous réserve de validation dynamique. Passe corrective : **non dans ce lot**.

### D-012 — `FERMÉE STRUCTURELLEMENT — VALIDATION DYNAMIQUE RESTANTE`

- Preuve/symboles : `seq_play_scheduler_push_note_pair()` réserve deux cases sous PRIMASK ou refuse tout; capacité fixe 512, collecte plafonnée à 128 par demi-buffer, scans/compaction bornés par 512, high-water et compteurs présents.
- Ancien supprimé / reliquat : push séparé de la paire supprimé; algorithme O(quota×512) et compaction restent actifs mais bornés.
- Invariant : aucune paire STEP à moitié insérée; coût maximal fini et déterministe en nombre d'itérations.
- Reste : mesure de cycles et latence des Off sous backlog; test réel 0/1/2 non exécuté. Impact Euclid : scheduler de base utilisable, sans prouver la marge. Passe corrective : **seulement si la mesure échoue**.

### D-013 — `FERMÉE STRUCTURELLEMENT — VALIDATION DYNAMIQUE RESTANTE`

- Preuve/symboles : `midi_usb_channel_voice_admit()` effectue sous section critique le contrôle de connexion, la réserve de 16 Off et l'enqueue exact; UART, dont le backend est un stub, est refusé. `midi_usb_refresh_connection()` purge la file à chaque changement de connexion et incrémente `midi_usb_generation`; le ledger terminal mémorise cette génération et solde sans émission une ancienne possession après reconnexion. La priorité realtime ne peut plus évincer un paquet déjà admis.
- Ancien supprimé / reliquat : préflight USB séparé de l'enqueue et Off forcé avec éviction supprimés; les APIs `midi_note_on/off` void restent publiques pour les chemins historiques non transactionnels.
- Invariant garanti : chaque bit MIDI du ledger correspond à un paquet réellement accepté par le transport courant; aucune admission n'est déclarée pour le DIN non implémenté; une reconnexion ne réémet pas un Off appartenant à l'ancienne session.
- Validation restante : saturation USB réelle, débranchement pendant On/Off, reconnexion, ordre et latence de vidage sur H743.
- Impact Euclid / corrective : base structurelle disponible; aucune correction structurelle MIDI identifiée, validation dynamique obligatoire.

### D-014 — `NON FERMÉE`

- Preuve/symboles : `seq_play_scheduler_emit_engine_note()` retourne un booléen, mais la plupart des moteurs appelés sont `void` et aboutissent à `return 1U`. Le lot 2 ferme désormais les synthés polyphoniques par occurrence et protège les moteurs mono par `g_seq_engine_mono_occurrence`; l'identité est exacte, mais le succès d'admission réel ne l'est pas.
- Ancien présent : APIs moteur void/pitch-based derrière la garde terminale et admission supposée.
- Invariant violé : admission interne réellement acquittée et comportement de capacité défini pour chaque moteur.
- Reste : moteur plein, mêmes pitches, steal, Stack queue pleine, Sampler Multi. Impact Euclid : répétitions rapides non sûres. Passe corrective : **oui**.

### D-015 — `FERMÉE STRUCTURELLEMENT — VALIDATION DYNAMIQUE RESTANTE`

- Preuve/symboles : `seq_runtime_set_clock_source()` ignore un no-op, puis exige le succès de `SEQ_PLAY_TRANSITION_SOURCE_SWITCH` avant de muter la clock. Cette transition ferme les records terminaux avec leur canal et masque admis, publie `NOTE_FX_TRANSITION_SOURCE_CLOCK_CHANGE`, puis seulement purge événements, occurrences et pending clock.
- Ancien supprimé : appel direct à `seq_play_scheduler_clear()` après mutation de la clock supprimé.
- Invariant garanti : aucune source clock n'est changée si les fermetures ne sont pas acquittées; passthrough, ARP et destinations partielles utilisent le ledger terminal exact.
- Reste : test notes STEP/live/ARP actives sur chaque source clock et saturation réelle. Impact Euclid : source switch structurellement sûr. Passe corrective : **non**, validation dynamique requise.

### D-016 — `FERMÉE STRUCTURELLEMENT`

- Preuve/symboles : `Seq/seq_division_catalog` centralise labels ARP/track, numerator, denominator, ratio Q16 et conversions; `note_fx_engine` utilise `seq_division_period_samples()`; UI et catalogue paramètres réutilisent les labels.
- Ancien supprimé : tables RATE et numerator/denominator privées.
- Invariant : huit ordinals ARP inchangés, quatre sous-choix track, ternaires centralisés.
- Reste : tests de toutes valeurs/arrondis non exécutés. Impact Euclid : autorité DIV disponible. Passe corrective : **non**.

### D-017 — `FERMÉE STRUCTURELLEMENT — VALIDATION DYNAMIQUE RESTANTE`

- Preuve/symboles : `g_note_fx_model_defaults` est l'autorité par modèle. `note_fx_state_set_param()` construit désormais un `note_fx_track_state_t` complet, remplace les quatre valeurs du slot par les defaults de la cible lors d'un changement MODEL, normalise puis publie le snapshot en une affectation. `note_fx_state_restore_track()` normalise une copie locale avant publication. `param_registry_apply_track_value()` capture l'ancien état et le restaure si la commande fixe `note_fx_pipeline_sync_track()` est refusée.
- Ancien supprimé / reliquat : unicité ARP, conservation des valeurs brutes entre modèles et publication d'un restore avant normalisation supprimées; Undo NoteFx reste une fonctionnalité absente, pas une autorité concurrente.
- Invariant garanti : une reconfiguration de modèle publie ensemble modèle et paramètres cibles; une file owner pleine ne laisse pas l'état de base diverger du runtime commandé; les valeurs restaurées invalides retombent sur les defaults du modèle restauré.
- Validation restante : modèle aller/retour, restore invalide, clipboard/page et p-lock; vérifier la politique produit Undo. Impact Euclid : defaults model-aware prêts pour de futurs modèles. Passe corrective : **non**, sauf échec dynamique.

### D-018 — `PARTIELLE`

- Preuve/symboles : anciens miroirs first-ARP et `[track][note]` scheduler supprimés. Restent les wrappers morts `seq_play_scheduler_dispatch_terminal_note[_to_channel]`, des commentaires « legacy NoteFx », et des audits Z1/Z4 dont le corps affirme encore Z1-002/Z4-002/Z4-003/Z4-005 actifs malgré des addenda partiels. L'architecture Z4 affirme aussi à tort qu'une Off NoteFx « ne ferme que les destinations admises » sans signaler la perte possible par quota.
- Ancien supprimé / reliquat : plusieurs helpers retirés; documentation historique non clairement marquée et APIs mortes restantes.
- Invariant non garanti : docs correspondant exactement au HEAD.
- Reste : consolidation documentaire et suppression dans une passe corrective. Impact Euclid : risque de repartir de contrats faux. Passe corrective : **oui**.

### D-019 — `PARTIELLE`

- Preuve/symboles : 14 fichiers de tests/validations du périmètre existent dans le HEAD, mais la majorité sont des recherches regex. Seuls `note_fx_arp_test.c` et `note_fx_runtime_test.c` sont exécutables host; les scripts stage, budget, terminal, timing, division et lifecycle ne sont pas enregistrés dans `tests/CMakeLists.txt`. Aucun test ne simule réellement les quatre admissions, l'épuisement de réserve Off, le source switch ou les interleavings de queue.
- Ancien supprimé / reliquat : matrice de scripts ajoutée; couverture comportementale insuffisante.
- Invariant non garanti : non-régression des dettes critiques.
- Reste : tests d'intégration déterministes, raccord CTest et mesures. Impact Euclid : impossible d'étendre sans oracle fiable. Passe corrective : **oui**.

### D-020 — `HORS DOMAINE`

- Preuve/symboles : aucun hook Live Record post-FX n'est ajouté; `note_fx_pipeline_terminal()` reste le point naturel d'extension.
- Ancienne abstraction : aucune à supprimer dans ce chantier.
- Invariant : absence de runtime/capture post-FX nouvelle.
- Reste : chantier Live Record séparé. Impact Euclid : seam disponible mais terminal à corriger d'abord. Passe corrective dans ce chantier : **non**.

## 5. Contrat d'événement

`note_event_t` contient bien `sample_abs`, `track`, `destination_id`, `note`, `velocity`, `kind`, `provenance`, `stage`, `source_token`, `occurrence_id`, `generation` et `flags`; l'admission est renvoyée par `note_fx_result_t` et l'occurrence est obligatoire. STEP utilise le namespace bas, clavier, MIDI externe et FX utilisent trois namespaces fixes distincts. Le seam live reçoit l'occurrence créée par le producteur; ARP produit des événements avec une nouvelle occurrence FX et `stage=slot+1`.

Le contrat est donc complet comme structure mais pas comme contrat de tous les producteurs. La destination logique est en pratique un canal MIDI, tandis que le terminal choisit toujours `MIDI_DEST_BOTH`; elle ne décrit pas une politique de destination physique. Le résultat d'admission n'est pas mémorisé dans l'événement mais dans le ledger terminal, ce qui est acceptable si ce ledger est exact — condition non satisfaite pour tous les backends.

## 6. Identité et atomicité

Les tables producteur live, ARP, owned, scheduler, terminal et guard sont fixes. Deux tokens homonymes sont distincts de la source au terminal. Le lot correctif 2 supprime le lookup live pitch/provenance du pipeline, impose une occurrence non nulle, ferme le synth poly par occurrence et protège les moteurs mono contre les anciens Off. La faille restante de ce chapitre est l'absence de validation du On contre une autorité de génération courante au terminal.

La paire scheduler est atomique. La génération NoteFx réserve désormais simultanément le quota On et un crédit Off; l'échec aval rembourse la réservation. Les Off marqués réservés sont prioritaires et l'owned n'est effacé qu'après acquittement. La garantie reste à valider dynamiquement et ne couvre pas encore toutes les politiques réelles USB/moteur.

## 7. Timeline et clock externe

Le STEP est appliqué avec son sample réel. La frontière est structurellement correcte : la collecte accepte `due < block_end`; un événement à `block_start` est courant, un événement à `block_end` attend le bloc suivant. Les overdue sont appliqués à l'offset 0 et comptés. NoteFx n'a pas d'horloge parallèle; ses échéances sont absolues et `next_sample==block_end` est traité au bloc suivant.

Le live conserve un timestamp pris avant l'application différée, ce qui peut laisser un sample terminal ancien. Les pulses externes autorisés sont tous placés à `block_start`; la borne est 4, définie par `SEQ_RUNTIME_EXEC_MAX_EXTERNAL_PULSES_PER_BLOCK`. Le surplus avance playhead/divisions/générations sans exécuter les boundaries, puis incrémente `external_pulses_coalesced`. Le compteur pending saturé au-delà de 65535 perd toutefois des pulses sans compteur. Aucun SPP reçu n'est exploité : F2 tombe vers le traitement clavier et n'invente aucune position. START réinitialise, CONTINUE ré-ancre si STOP, STOP purge le pending; ces séquences restent à tester.

## 8. Owner runtime

`g_slot`, ledgers owned, phases ARP, deadlines et overrides sont mutés depuis le domaine audio. UI, paramètres, restore, clipboard, p-lock, mute et transport publient des commandes fixes sans pointeurs. La file est statique, capacité physique 32 mais capacité utile 31.

La frontière multi-producteur est maintenant protégée par PRIMASK : scan de coalescence, réservation, copie, barrière et publication du head sont indivisibles face aux IRQ; le dequeue protège symétriquement tail et copie. Les commandes de configuration embarquent le snapshot de base complet de la piste. L'owner applique les quatre slots à partir de cette seule copie, et le batch `RESET_ALL` réserve ses huit entrées avant d'en publier une seule. Restent à mesurer la durée maximale des sections critiques et à exercer les interleavings/saturations.

## 9. Chaîne des quatre slots

Le chemin actif est bien : source/stage 0 → slots 0,1,2,3 → stage terminal 4. Un OFF transmet; ARP transforme/supprime la source, produit des On/Off bornés, diffère par `next_sample`, puis reprend à `slot+1`. Aucun événement généré ne repart au slot 0, aucune recherche first-ARP n'est active et `Src/NoteFx` n'appelle ni MIDI ni moteur.

Le fan-out courant est au plus un par pulse et la profondeur d'appel est bornée à quatre stages. La structure permet un fan-out futur seulement si la réservation de fermeture et le quota combinatoire sont corrigés; l'existence d'un tableau `owned[16]` ne constitue pas cette garantie.

## 10. Files et budgets

Garanti par construction : demi-buffer de 64 frames; budget NoteFx ouvert une fois; 8 On générés par piste; 32 Off générés; 31 commandes en file et 32 consommables; 128 événements scheduler par demi-buffer; scheduler 512; ARP sources 16; owned 16 par slot; terminal/guard 64 occurrences par piste; USB RX/TX 128; aucun malloc dans NoteFx/Seq; stage max 4; scans bornés.

Seulement supposé : qu'une réserve globale de 32 Off suffise à tous les On admis antérieurement; que le guard ait toujours une case lorsque le terminal en a une; que les APIs moteur `void` aient accepté; que la queue USB progresse; que les commandes ne se concurrencent pas; que 4 pulses externes et les scans/compactages scheduler respectent le deadline.

Dépendant H743 : coût de 64 sous-segments, scans `128×512`, cleanup `8×4×16`, retries bornés de fermetures, quatre pulses externes avec 8 pistes, saturation terminale, interaction IRQ USB/DMA, cycles/p99/marge et underruns.

## 11. Terminal et admission indépendante

La structure `seq_terminal_admission_t` sépare `internal_admitted` et `midi_dest_mask`. Les quatre combinaisons sont exprimables dans le code, et un refus interne n'empêche pas l'appel MIDI ni l'inverse. L'Off consulte le record et vise uniquement les flags mémorisés.

Le lot 5 rend l'admission MIDI fidèle : UART stub refusé, USB refusé hors configuration, contrôle de capacité et enqueue atomiques, réserve de 16 Off, aucune éviction par une priorité realtime et génération renouvelée avec purge lors de chaque déconnexion/reconnexion. Le terminal mémorise la génération USB et ne transmet pas à une nouvelle session l'Off d'une ancienne admission. Le lot 4 conserve séparément `internal_admitted` et chaque bit MIDI jusqu'à acquittement. Les quatre cas restent à démontrer dynamiquement et l'admission des backends moteur `void` demeure structurellement ouverte.

## 12. Mute et transitions

Le mute normal bloque les nouveaux On STEP via `g_seq_play_track_suspended`, laisse les Off collectés passer, ne change pas la génération et ne purge pas NoteFx. L'unmute retire seulement le blocage; aucun replay/retrigger n'est codé. Un ARP déjà alimenté avant le mute continue donc son cycle et conserve ses deadlines. Les sources live utilisent leurs politiques séparées et ne passent pas par le flag scheduler.

Les transitions destructives sont maintenant distinctes. STOP, PANIC, PATTERN, MODEL, DESTINATION et SOURCE_SWITCH ferment les occurrences du ledger terminal avec leur canal et leur masque réels; un refus conserve uniquement la destination restant à fermer et bloque purge/génération. La commande NoteFx spécifique est ensuite publiée, puis événements et occurrences source sont purgés. MODEL et DESTINATION suspendent; PATTERN, STOP, PANIC et SOURCE_SWITCH reprennent selon leur politique. Les façades `clear` et `clear_tracks` ne sont plus que les alias PANIC et STOP. Le mute reste hors de ce chemin destructif.

## 13. Divisions et paramètres

Le catalogue `seq_division_catalog` est l'autorité unique active pour labels, rapports, Q16, sous-ensembles et conversions. Les valeurs persistées ARP 0..7 et track restent inchangées; les ternaires sont présentes.

Les paramètres de base sont normalisés et le modèle ARP n'est plus unique dans le pipeline. Aucun nouveau modèle ni format historique n'a été ajouté. Le lot 6 applique les defaults du modèle cible sur un snapshot complet du slot, normalise les restores avant publication et restaure l'ancien snapshot si la commande owner ne peut être admise. Clipboard et restore reviennent vers ces mêmes autorités; l'Undo v2 courant reste limité aux snapshots structurels de steps et ne porte pas NoteFx. Cette absence doit rester documentée comme limite produit, pas comme une seconde voie de mutation.

## 14. Persistance et runtime

Pattern et track snapshot stockent uniquement `note_fx_track_state_t`, soit quatre slots × quatre octets de base. Project persiste via Pattern. Le clipboard piste s'appuie sur `track_snapshot`; clipboard page/ensemble copie les paramètres par les APIs communes. Les recherches dans les payloads NoteFx ne trouvent ni phase, deadline, token, occurrence, ledger, owned, file, génération runtime, timestamp courant ni compteur de saturation.

Les restores normalisent les octets, nettoient/reconstruisent le runtime par commandes et ne modifient pas les versions de format pour ce chantier. La faiblesse restante est la publication non transactionnelle base→commande et non une fuite de runtime persistant.

## 15. Code mort et documentation

- Encore actif et nécessaire : `note_fx_arp_t`, ledgers fixes, quatre slots, projections UI par pitch dans output guard/Live Record, paire scheduler atomique.
- Compatibilité temporaire documentée : aucune compatibilité runtime explicitement bornée n'est correctement étiquetée comme temporaire.
- Mort mais non supprimé : `seq_play_scheduler_dispatch_terminal_note()` et `_to_channel()` ne sont appelés que l'un par l'autre dans le HEAD; `seq_play_scheduler_dispatch_terminal_owned()` est leur chemin sans ledger complet.
- Documentation historique correctement marquée : certaines sections Z1/Z4 disent « méthode historique » ou ajoutent des addenda.
- Documentation encore ouverte : tableaux de verdict Z1/Z4 et plusieurs corps de texte continuent d'annoncer Z1-002, Z4-002, Z4-003 et Z4-005 comme état courant; inversement les addenda du plan/Z4 déclarent des garanties de fermeture et admission que le code ne fournit pas. Le plan Euclid est antérieur au chantier et doit bien rester inchangé jusqu'au réaudit demandé.
- Tests anciens/incohérents : `note_fx_runtime_test.c` conserve un stub du wrapper terminal sans identité; `note_fx_arp_test.c::arp_off` ignore son argument note; plusieurs scripts concluent `PASS` à partir de présence de chaînes et non d'un scénario exécuté. Sept scripts importants ne sont pas enregistrés dans CMake.

## 16. Couverture de validation existante

Les fichiers cités existent dans le HEAD mais sont marqués supprimés dans le worktree préexistant. Aucun n'a été exécuté.

| # | Validation | Classement | Preuve existante / manque |
|---:|---|---|---|
| 1 | On/Off simple | `COUVERTE PARTIELLEMENT` | `note_fx_runtime_test.c`, simple compteur terminal sans ledger réel. |
| 2 | Deux mêmes hauteurs | `COUVERTE PARTIELLEMENT` | `note_fx_arp_test.c`, tokens ARP seulement. |
| 3 | Deux sources même hauteur | `COUVERTE PARTIELLEMENT` | test ARP de deux tokens, pas producteurs/provenances. |
| 4 | Retrigger avant ancien Off | `COUVERTE PARTIELLEMENT` | aucune chaîne terminale/voice steal. |
| 5 | File 0/1/2 | `COUVERTE PARTIELLEMENT` | `seq_play_scheduler_pair_validation.ps1`, modèle PowerShell + regex, pas code C exécuté. |
| 6 | Réservation paire | `COUVERTE PARTIELLEMENT` | même script, invariant scheduler seulement. |
| 7 | Off prioritaire sous saturation | `AUCUN TEST IDENTIFIÉ` | aucune saturation combinée NoteFx/scheduler/USB. |
| 8 | STOP notes actives | `COUVERTE PARTIELLEMENT` | scripts transition statiques, aucun ledger réel. |
| 9 | Panic différé | `COUVERTE PARTIELLEMENT` | `note_fx_transition_lifecycle_validation.ps1`, présence de politique seulement. |
| 10 | Mute/reprise | `COUVERTE PARTIELLEMENT` | scripts transition regex. |
| 11 | Pattern stale | `COUVERTE PARTIELLEMENT` | recherche génération/transition, pas injection stale. |
| 12 | Modèle pendant lecture | `AUCUN TEST IDENTIFIÉ` | aucun scénario defaults+close. |
| 13 | Clavier | `COUVERTE PARTIELLEMENT` | `note_fx_pipeline_validation.ps1`, présence du seam. |
| 14 | STEP | `COUVERTE PARTIELLEMENT` | présence submit et test paire séparés. |
| 15 | MIDI externe | `COUVERTE PARTIELLEMENT` | présence du seam, aucune identité/latence. |
| 16 | Quatre slots ordonnés | `COUVERTE PARTIELLEMENT` | `note_fx_stage_chain_validation.ps1`, analyse statique non enregistrée CTest. |
| 17 | Généré slot1→2-4 | `COUVERTE PARTIELLEMENT` | même script, aucun oracle événementiel. |
| 18 | Différé généré | `AUCUN TEST IDENTIFIÉ` | pas de deadline+Off exact. |
| 19 | Début/milieu/fin bloc | `COUVERTE PARTIELLEMENT` | `seq_runtime_timing_validation.ps1`, regex de propagation seulement. |
| 20 | Frontière microtiming Off/On | `AUCUN TEST IDENTIFIÉ` | pas d'oracle d'ordre complet. |
| 21 | Changement tempo | `AUCUN TEST IDENTIFIÉ` | aucune échéance active vérifiée. |
| 22 | Huit pistes/quotas | `COUVERTE PARTIELLEMENT` | `note_fx_budget_validation.ps1`, constantes/structure seulement. |
| 23 | Accord dense | `COUVERTE PARTIELLEMENT` | capacité ARP testée, pas terminal/saturation. |
| 24 | Budget cycles demi-buffer | `VALIDATION MATÉRIELLE OBLIGATOIRE` | mesure DWT/charge IRQ H743 64 frames Low-Cost/Premium. |
| 25 | Moteur saturé | `COUVERTE PARTIELLEMENT` | `note_terminal_admission_validation.ps1` vérifie une branche, sans moteur simulé. |
| 26 | MIDI saturé/refusé | `COUVERTE PARTIELLEMENT` | API/réserve vérifiées statiquement, pas queue réelle. |
| 27 | Persistance | `COUVERTE PARTIELLEMENT` | `note_fx_persistence_validation.ps1`, recherche de payload; pas round-trip. |
| 28 | Undo/Redo/clipboard | `COUVERTE PARTIELLEMENT` | snapshot/clipboard statiques; Undo NoteFx absent. |
| 29 | Recherches négatives | `COUVERTE PAR UN TEST EXISTANT NON EXÉCUTÉ` | `note_fx_step10_consolidation_validation.ps1`; non enregistré dans tous les environnements hors Windows. |
| 30 | Chaque transition, zéro note | `COUVERTE PARTIELLEMENT` | enum/branches vérifiés, aucun état terminal simulé. |
| 31 | Accord long avant mute | `VALIDATION MATÉRIELLE OBLIGATOIRE` | écoute/enveloppes + ledgers sur H743. |
| 32 | Nouveaux STEP pendant mute | `COUVERTE PARTIELLEMENT` | guard scheduler vérifié statiquement. |
| 33 | Off possédé pendant mute | `COUVERTE PARTIELLEMENT` | exemption Off présente, pas scénario complet. |
| 34 | Unmute sans replay | `COUVERTE PARTIELLEMENT` | resume ne contient pas de replay; pas test dynamique. |
| 35 | Clavier/MIDI live pendant mute | `AUCUN TEST IDENTIFIÉ` | politiques distinctes non exercées. |
| 36 | Double mute/unmute, panic sous mute | `COUVERTE PARTIELLEMENT` | coalescence de commandes recherchée, ledgers non exercés. |
| 37 | Interne admis/MIDI refusé | `COUVERTE PARTIELLEMENT` | structure du terminal, aucun stub de refus réel. |
| 38 | Interne refusé/MIDI admis | `COUVERTE PARTIELLEMENT` | idem. |
| 39 | Deux admis | `COUVERTE PARTIELLEMENT` | idem. |
| 40 | Deux refusés | `COUVERTE PARTIELLEMENT` | idem; UART stub empêche le cas réel avec BOTH. |
| 41 | Déconnexion avant/après On | `VALIDATION MATÉRIELLE OBLIGATOIRE` | déconnexion/reconnexion USB réelle et purge/queue. |
| 42 | MIDI plein, interne disponible | `COUVERTE PARTIELLEMENT` | indépendance syntaxique, pas saturation réelle. |
| 43 | Moteur saturé, MIDI disponible | `COUVERTE PARTIELLEMENT` | indépendance syntaxique, pas saturation réelle. |
| 44 | Backlog clock 1/petit/limite/grand | `COUVERTE PARTIELLEMENT` | `seq_runtime_timing_validation.ps1` modèle un grand skip; pas RX/IRQ réel. |
| 45 | START/CONTINUE/STOP avec backlog | `AUCUN TEST IDENTIFIÉ` | FSM et pending non exercés ensemble. |
| 46 | SPP réellement reçu/supporté | `AUCUN TEST IDENTIFIÉ` | SPP RX n'est pas supporté; recherche négative confirme seulement l'absence de position inventée. |
| 47 | Pause traitement principal | `VALIDATION MATÉRIELLE OBLIGATOIRE` | pause superloop + saturation RX + cycles/IRQ sur H743. |

## 17. Builds, tests et mesures

```text
Build Premium : EXÉCUTÉ AVEC SUCCÈS
Build Low-Cost : EXÉCUTÉ AVEC SUCCÈS
Tests : NON EXÉCUTÉS PAR DEMANDE
Mesures H743 : NON EXÉCUTÉES PAR DEMANDE
```

Les builds Premium et Low-Cost demandés après l'audit compilent et lient `BRICK6_CUBE.elf` avec succès. Aucun CTest, script PowerShell, instrumentation ou mesure n'a été lancé. Les mentions « PASS/verts » présentes dans les plans, scripts ou anciens addenda ne sont pas reprises comme preuve de cette passe.

## 18. Écarts structurels restants

6. Admission moteur encore supposée pour des APIs `void`; l'identité de voice steal est désormais protégée mais son admission réelle reste à acquitter. L'admission MIDI est fermée structurellement par le lot 5, sous réserve des essais USB matériels.
8. Génération d'un nouveau On stale non comparée à l'autorité courante au terminal.
9. Timestamp live antérieur au sample réel d'application.
10. Pending clock saturé sans compteur de perte.
11. Defaults de modèle non chargés transactionnellement.
12. Tests comportementaux incomplets/non raccordés et documentation Z1/Z4 contradictoire avec le HEAD.

## 19. Validations dynamiques et matérielles restantes

- Après correction structurelle : builds Release Low-Cost et Premium.
- Tests host/intégration : occurrences homonymes et Off inversés, stale générations, paire NoteFx 0/1 places, exhaustion réserve Off, quatre stages, timestamps 0/63/end, transition par politique, source switch, quatre cas terminal, voice steal, restore/defaults/clipboard/p-lock.
- Tests USB/MIDI : queue pleine et presque pleine, appareil non prêt, déconnexion/reconnexion, ordre On/Off/panic/clock, vérification de la purge et du masque réel.
- Tests moteurs : capacité pleine, Stack queue pleine, Synth voice steal, Sampler Multi et destinations BOTH.
- H743 Low-Cost/Premium : DWT cycles max/p99, charge IRQ et underrun sur 64 frames, 8 pistes×4 slots, 16 sources, cleanup simultané, 4 pulses clock, 64 sous-segments, saturation USB/moteur.
- Produit : mute long avec ARP STEP préexistant, live pendant mute, unmute, STOP/CONTINUE/START, changement de source clock et comportement audible des enveloppes.

## 20. Verdict de préparation Euclid

`NON PRÊT — DETTES STRUCTURELLES OUVERTES`

Les préconditions Euclid les plus importantes ne sont pas réunies : paire générée, identité source→moteur, owner et transitions sont maintenant structurellement fournis mais non validés dynamiquement; le terminal ne possède toujours pas une admission réelle de tous les moteurs/transports. L'absence de tests et mesures interdit en outre toute conclusion hard real-time ou matérielle.

## 21. Recommandation finale

Poursuivre la passe corrective avant le réaudit Euclid, limitée dans cet ordre à : admission moteur réelle; tests comportementaux enregistrés, dont réservation/retry, identité/retrigger, interleavings owner, defaults/restore et matrice transitions/source switch des lots 1–6; consolidation Z1/Z4. Réauditer ensuite statiquement, puis seulement exécuter builds/tests et mesures H743. Ne pas mettre à jour `docs/plan_midi_fx_euclid.md` avant cette fermeture.
