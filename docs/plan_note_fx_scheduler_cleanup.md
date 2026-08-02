# Plan de nettoyage NoteFx, scheduler et evenements de notes

## 1. Verdict

Le socle actuel est exploitable pour l'ARP simple, mais il n'est pas encore un contrat fiable pour une chaine MIDI FX generative. Le scheduler a bien une reservation atomique de paire depuis le HEAD courant; ce point rend obsolete le constat historique `Z4-002` tel qu'il est formule dans l'audit Z4. Les dettes encore presentes concernent surtout l'identite perdue au seam NoteFx, l'appariement par hauteur, le proprietaire runtime multi-contexte, l'horodatage de fin de bloc, les budgets par sous-segment, les refus aval et le pipeline qui ne parcourt pas les quatre slots.

Audit statique du HEAD `2b6996fab` (`docs: refine euclid midi fx plan`). Aucune mesure H743 n'est disponible dans cette passe; les cycles, la marge MSP et les seuils de debit restent a mesurer. Aucun code fonctionnel n'est modifie par ce document.

Registre: 20 constats qualifies, dont 7 `CORRIGER`, 11 `REFONDRE`, 1 `SUPPRIMER` et 1 `HORS DOMAINE`. Aucun compromis actuellement dette n'est classe `CONSERVER`; les compromis volontaires sont listes en section 18.

## 2. Sources auditees et etat du HEAD

Sources obligatoires confrontees:

- HEAD et symboles de `Inc/NoteFx`, `Src/NoteFx`, `Src/Seq`, `Src/Audio`, `Src/Keyboard`, `Src/MIDI`, `Src/Core`, `Src/Param`, `Src/Storage` et `Src/UI`.
- `docs/plan_midi_fx_euclid.md`, utilise comme liste de symptomes et de contraintes futures, avec revalidation contre le code courant.
- `docs/audits/z1_hard_rt_debt_audit.md` et `docs/audits/z4_scheduler_clock_midi_debt_audit.md`.
- `docs/architecture/ARCHITECTURE_GLOBAL.md`, `z0_plateforme_cadence.md`, `z2_track_runtime_authority.md`, `z3_param_modulation_control.md`, `z4_seq_clock_scheduler.md`, `z6_state_persistence_patterns_projects.md` et les annexes de transitions/runtime pertinentes.
- `tests/note_fx_arp_test.c`, `tests/note_fx_runtime_test.c`, `tests/note_fx_pipeline_validation.ps1`, `tests/note_fx_plock_validation.ps1`, `tests/note_fx_persistence_validation.ps1`, `tests/seq_play_scheduler_pair_validation.ps1`, `tests/CMakeLists.txt`, ainsi que les chemins mute, panic et transport.

Le HEAD contient notamment `SEQ_PLAY_SCHEDULER_EVENT_CAP = 512`, `seq_play_scheduler_push_note_pair()` avec test atomique de deux places, generation et token partage par la paire. L'audit Z4 qui decrit encore deux pushes independants est donc obsolete pour ce point et ne doit pas etre recopie dans Euclid. En revanche, le token est ensuite reduit a une table `[track][note]`, puis perdu par le seam NoteFx et le terminal.

Le worktree contenait avant cette passe des modifications fonctionnelles, des artefacts de build et d'autres documents hors perimetre. Ils ne sont pas integres au livrable et ne doivent pas etre nettoyes par les etapes ci-dessous.

## 3. Architecture actuelle

### 3.1 Chemin des sources

```text
clavier / MIDI externe
  -> keyboard_engine_send_note_for_owner_track()
  -> note_fx_pipeline_submit(track, note, velocity, on/off, sample)

boundary / sequenceur PLAY
  -> seq_play_scheduler_push_note_pair() -> g_seq_play_events[512]
  -> audio collect par scan et offset
  -> seq_play_scheduler_audio_apply_event()
  -> note_fx_pipeline_submit(..., sample = timeline actuelle)

audio IRQ
  -> note_fx_pipeline_process()
  -> note_fx_engine_process()
  -> note_fx_pipeline_terminal()
  -> dispatcher commun MIDI + moteur local + output guard
```

`note_fx_engine_source()` recherche le premier slot ARP du track. Un evenement sans ARP va au terminal; un evenement retenu par ARP est emis plus tard directement au terminal. Les slots 1 a 4 existent dans `note_fx_track_state_t`, la page UI, les p-locks et la persistence, mais ne forment pas encore une chaine d'execution.

Le mute de piste concerne ici l'origine sequenceur: il bloque les nouveaux STEP au point d'entree scheduler, sans fermer les occurrences deja admises. Les FX qui en derivent peuvent terminer les owned et echeances existantes, mais ne doivent plus produire de nouveaux On depuis cette source mutee. Les sources clavier et MIDI live restent soumises a leurs regles propres.

Le terminal post-FX admet independamment le moteur interne et MIDI externe. `MIDI_DEST_BOTH` est une projection de routage, pas une admission atomique; chaque destination possede son resultat et son ledger.

### 3.2 Donnees existantes et pertes

| Couche | Donnees presentes | Perte ou limite verifiee |
|---|---|---|
| `seq_play_scheduler_evt_t` | sample absolu, track, note, velocity, type, generation globale/track, `event_token` | token miroir actif indexe seulement par track/note |
| `seq_play_scheduler_audio_event_t` | type, track, note, velocity, generation track, offset de bloc, token | pas de destination, provenance, occurrence distincte ou stage |
| `note_fx_event_t` | sample, token, generation, track, note, velocity, destination, type | `note_fx_pipeline_submit()` ne recoit ni token ni generation; les appels live commencent a zero |
| `note_fx_owned_t` | active, note, source_note, destination, token, generation | source retrouvee par `source_note`; sortie active occupee par `owned[0]` |
| terminal | track, canal, note, velocity, On/Off | ignore token/generation; guard compte par pitch; MIDI et moteurs n'acquittent pas tous |
| persistence/snapshot | 4 x 4 octets de base par Play Track | aucun runtime, token ou phase ne doit y entrer; ce contrat est actuellement respecte |

Les quatre parametres sont `PARAM1..PARAM3, MODEL`, avec mapping arithmetique sur 16 slots p-lock. Le runtime ARP possede 16 sources et 16 sorties par slot. La destination actuelle est un canal MIDI; elle ne porte pas encore l'identite d'une sortie interne/externe.

### 3.3 Files et execution

- scheduler: tableau fixe de 512 evenements, compactage en place, selection repetee par scan de toute la file;
- evenements audio locaux: 128 `seq_runtime_audio_event_t` dans `process_half`, export scheduler par paquets de 16;
- boundary: 32 evenements fixes;
- MIDI USB: RX 128, TX 128, burst 16; les channel-voice sont perdus si TX est plein;
- Stack: ring 256, capacite utile 255, commandes et fermetures partagees;
- NoteFx: pas de file; etat mutable directement dans `g_slot[8][4]`, overrides et bases.

La demi-zone audio est de 64 frames. `audio.c` peut la fragmenter jusqu'a 64 sous-blocs et appelle NoteFx au debut de chaque sous-segment. Le budget NoteFx de 8 emissions par track est donc un budget par appel, pas par demi-buffer, et les releases peuvent l'ignorer.

## 4. Invariants cibles

1. Un evenement est traite sur une timeline sample absolue monotone; son offset de bloc n'est jamais remplace par la fin du bloc.
2. Un Note On accepte porte une `occurrence_id` et une generation qui survivent jusqu'au terminal.
3. Un Note Off ferme exactement l'occurrence qui l'a creee; pitch, track, slot ou voix seuls ne suffisent jamais.
4. Aucun On differe n'est publie sans fermeture garantie, possedee par une table runtime ou refuse complete.
5. Un evenement stale est rejete avant tout effet terminal.
6. Les sources clavier, STEP, MIDI externe et ARP utilisent le meme contrat, avec une provenance differente.
7. Les quatre slots sont traverses dans l'ordre `[0,1,2,3]`; un fan-out reprend au stage suivant et ne reboucle pas.
8. Chaque stage peut transmettre, transformer, supprimer, fan-out borne ou differer, mais ne peut pas depasser sa capacite fixe.
9. Un seul owner audio applique l'etat runtime NoteFx; il applique des politiques de transition distinctes et ne ferme pas systematiquement les notes.
10. `MUTE_TRIGS` bloque uniquement les nouveaux trigs du sequenceur, conserve les occurrences actives et leurs echeances; STOP, PANIC et les reconfigurations destructives utilisent des politiques propres et idempotentes.
11. Les Note Off dus et les fermetures possedees sont prioritaires sur les nouveaux On, y compris pendant `MUTE_TRIGS`.
12. Les quotas sont fixes, mesures sur une demi-zone audio, et incluent releases, terminal, refus par destination et rattrapage de clock externe.
13. Une saturation de source/stage/fermeture refuse une occurrence complete; une admission terminale reussit independamment par destination et ne cree un Off que pour une destination admise.
14. MIDI externe et moteur local recoivent le meme evenement terminal mais ont chacun un resultat d'admission et un ledger; la polyphonie globale reste l'autorite des moteurs.
15. Les bases NoteFx persistent; phase, deadlines, tokens, owned et files runtime ne persistent jamais.
16. Aucun chemin audio/IRQ n'alloue dynamiquement, ne logge vers un stockage lent ou ne depend d'un lock non borne.

## 5. Registre complet des dettes

Chaque entrée fournit la preuve actuelle, le traitement, les alternatives rejetées et la fermeture attendue. Les lignes sont stables et doivent être citées par les futures commits.

### D-001 — Contrat NoteFx tronqué au seam source

- **Verdict:** `REFONDRE`; **sévérité:** `CRITIQUE`.
- **Preuve:** `Inc/NoteFx/note_fx_engine.h:11-14` déclare un événement riche, mais `Src/NoteFx/note_fx_pipeline.c:115-127` construit l'événement sans token ni generation. `Src/Seq/seq_play_scheduler.c:1449-1451` passe le getter de timeline plutôt que l'échantillon d'application.
- **Fichiers/symboles:** `note_fx_event_t`, `note_fx_pipeline_submit`, `seq_play_scheduler_audio_apply_event`, `seq_play_scheduler_audio_event_t`.
- **Conséquences:** clavier, STEP et MIDI externe ne peuvent pas être appariés au même niveau; l'occurrence et la provenance sont perdues avant le premier FX; un Off tardif peut agir sur la mauvaise note.
- **Solution:** définir un contrat fixe commun, transporter sample, track, destination, kind, provenance, source token, occurrence, generation et stage; retourner un statut borné plutôt que coder l'acceptation dans un champ ambigu.
- **Alternatives rejetées:** structure universelle géante; reconstruction par pitch; propagation partielle seulement pour ARP.
- **Dépendances:** D-002, D-003, D-004, D-005, D-006.
- **Tests de fermeture:** quatre producteurs identiques au terminal quant à l'identité; champs conservés au travers de quatre stages; ancien generation rejeté.
- **Risque:** changement simultané des headers scheduler/NoteFx et des stubs de test.
- **Étape:** 1.

### D-002 — Sources ARP et owned indexés par hauteur

- **Verdict:** `CORRIGER`; **sévérité:** `HAUTE`.
- **Preuve:** `note_fx_arp_t` ne contient que `note[]/velocity[]`; `note_fx_arp_note_on/off()` déduplique et retire par hauteur. `note_fx_engine_source()` recherche `owned.source_note == event->note`; deux occurrences de même hauteur ne sont pas distinctes.
- **Fichiers/symboles:** `Inc/NoteFx/note_fx_arp.h`, `Src/NoteFx/note_fx_arp.c`, `note_fx_engine_source`, `note_fx_owned_t`.
- **Conséquences:** deux sources simultanées, retrigger avant ancien Off ou deux FX générés peuvent se couper mutuellement; un ancien Off peut fermer une occurrence nouvelle.
- **Solution:** table fixe de sources par `source_token + generation`, table fixe de sorties par `occurrence_id`; remplacer la recherche par pitch par lookup d'identité.
- **Alternatives rejetées:** augmenter seulement 16; empiler les notes par hauteur; réutiliser la table scheduler `[track][note]`.
- **Dépendances:** D-001, D-003.
- **Tests:** deux mêmes pitches de deux sources, retrigger, accord dense, Off inversés, table pleine.
- **Risque:** ordre ARP historique et test random.
- **Étape:** 2.

### D-003 — Guard scheduler encore one-token-per-pitch

- **Verdict:** `CORRIGER`; **sévérité:** `HAUTE`.
- **Preuve:** `g_seq_play_active_event_token[TRACK][128]` est documenté comme compatibilité temporaire (`Src/Seq/seq_play_scheduler.c:95-103`) et On/Off le lit à `:1433-1447`.
- **Conséquences:** un second On même pitch écrase le premier; le premier Off est rejeté ou ferme la seconde logique; la transition ne peut pas énumérer toutes les occurrences.
- **Solution:** ledger fixe d'occurrences actives par track/destination, avec token exact, generation et état terminal; le scheduler reserve une occurrence par paire.
- **Alternatives rejetées:** conserver un compteur pitch sans identités; faire dépendre la correction uniquement du moteur polyphonique.
- **Dépendances:** D-001, D-002, D-004.
- **Tests:** deux occurrences identiques, deux sources, clear partiel, generation wrap simulée.
- **Risque:** interaction avec `seq_output_guard` et live record historique.
- **Étape:** 2.

### D-004 — Terminal sans identite ni admission remontée

- **Verdict:** `REFONDRE`; **sévérité:** `HAUTE`.
- **Preuve:** `note_fx_pipeline_terminal()` appelle `seq_play_scheduler_dispatch_terminal_note_to_channel()` sans token/generation (`Src/NoteFx/note_fx_pipeline.c:15-25`); le dispatcher envoie MIDI, marque le guard, puis appelle le moteur (`Src/Seq/seq_play_scheduler.c:691-707`).
- **Conséquences:** une fermeture stale atteint les deux destinations; un On refuse par moteur ou USB peut être compté comme actif; le futur Live Record ne dispose pas d'un seam complet.
- **Solution:** terminal post-FX unique recevant l'événement complet, validant generation, réservant/admettant la destination, publiant un résultat et enregistrant la fermeture possédée.
- **Alternatives rejetées:** ajouter des appels directs MIDI ou moteur dans chaque FX; prétendre qu'un API `void` est un acquittement matériel.
- **Dépendances:** D-001, D-003, D-013, D-014.
- **Tests:** admission interne refusée, TX plein, On/Off identifiés, terminal identique pour live/scheduler/ARP.
- **Risque:** toucher au contrat polyphonie sans changer sa limite globale; cette limite reste hors scope.
- **Étape:** 7.

### D-005 — Recherche implicite du premier ARP au lieu des slots ordonnés

- **Verdict:** `REFONDRE`; **sévérité:** `HAUTE`.
- **Preuve:** `note_fx_engine_source()` scanne `slot=0..3` puis s'arrête au premier modèle ARP (`Src/NoteFx/note_fx_engine.c:63-73`).
- **Conséquences:** un ARP en slot 4 est sélectionné comme s'il était premier; les autres slots sont ignorés; l'événement sans ARP contourne tous les modèles; l'unicité ARP est imposée globalement.
- **Solution:** dispatcher fixe par stage, quatre appels séquentiels, contexte de continuation et fan-out borné.
- **Alternatives rejetées:** graphe dynamique; rechercher par type; copier un traitement spécial Euclid au-dessus de l'ARP.
- **Dépendances:** D-001, D-006, D-007.
- **Tests:** ARP aux quatre positions, modèles OFF intercalés, ordre de transformation observable.
- **Risque:** compatibilité avec l'unicité ARP actuelle; elle devient une règle de modèle, non de pipeline.
- **Étape:** 5.

### D-006 — Les sorties générées ne poursuivent pas la chaîne

- **Verdict:** `REFONDRE`; **sévérité:** `HAUTE`.
- **Preuve:** `note_fx_engine_process()` émet directement via le callback terminal (`Src/NoteFx/note_fx_engine.c:123-146`); aucun index de stage n'est dans `note_fx_event_t`.
- **Conséquences:** un FX futur placé après ARP ne voit pas les notes générées; un différé peut contourner les slots ou créer une boucle implicite.
- **Solution:** queue locale fixe de continuation par stage; tout On/Off/fan-out généré est routé à `stage+1`, avec `stage==4` seul terminal.
- **Alternatives rejetées:** rappeler `source()` depuis zéro; callback direct moteur; graphe générique.
- **Dépendances:** D-001, D-005, D-011.
- **Tests:** généré slot 1 -> slots 2,3,4; suppression; fan-out maximal; aucun rappel slot 1.
- **Risque:** budget combinatoire et ordre Off/On au même sample.
- **Étape:** 5.

### D-007 — Runtime NoteFx multi-écrivain et état partiellement publié

- **Verdict:** `REFONDRE`; **sévérité:** `CRITIQUE`.
- **Preuve:** `docs/audits/z1_hard_rt_debt_audit.md` confirme `g_slot`, overrides, tokens et ARP écrits depuis clavier/UI/restore/paramètres et IRQ audio sans file. `note_fx_pipeline_sync_track()` configure quatre slots successivement; `release_slot()` modifie owned, generation, ARP et deadline pendant que process peut les parcourir.
- **Conséquences:** double Off, On créé dans une configuration partielle, phase ou deadline incohérente, lecture d'un snapshot non transactionnel. Un mute de trigs ne doit pas retirer les possessions déjà admises.
- **Solution:** bases NoteFx canonique hors runtime; commandes fixes vers un owner audio unique; apply transactionnel d'un track avant traitement; snapshots de diagnostic en lecture; politiques explicites `MUTE_TRIGS`, `STOP_CLOSE`, `PANIC_CLOSE_ALL`, `MODEL_RECONFIGURE`, `PATTERN_REPLACE` et `DESTINATION_REBIND`.
- **Alternatives rejetées:** section critique autour de tout le traitement audio; RTOS; allocation dynamique; double owner par modèle.
- **Dépendances:** D-008, D-009, D-011.
- **Tests:** interleavings injectés entre chaque écriture; p-lock/restore/cleanup concurrents; aucun runtime dans snapshot.
- **Risque:** latence de prise en compte d'une commande UI, qui doit être documentée.
- **Étape:** 4.

### D-008 — Cleanup distribué et règles non uniformes

- **Verdict:** `REFONDRE`; **sévérité:** `HAUTE`.
- **Preuve:** cleanup existe dans `seq_play_scheduler_clear`, `clear_tracks`, `suspend/resume`, `seq_output_guard_panic`, `keyboard_runtime`, `ui_core`, `track_snapshot`, `param_registry` et `note_fx_pipeline`. `clear()` nettoie NoteFx et efface tokens mais ne force pas les Off non-ARP; `clear_tracks()` a une boucle distincte de forced Off.
- **Conséquences:** STOP, panic, changement de source, mute, changement de modèle et changement de pattern n'ont pas la même idempotence ni les mêmes generations. Le mute normal doit rester non destructif.
- **Solution:** une commande `transition_apply(scope, policy)` par scope. `MUTE_TRIGS` bloque les nouveaux events STEP et leurs dérivés, conserve les ledgers et échéances et laisse passer leurs Off; les autres politiques appliquent, selon leur contrat, close->invalidate->purge->reset. Les transitions ne mutent pas directement `g_slot`.
- **Alternatives rejetées:** appeler davantage de routines existantes; centraliser dans UI; utiliser `All Notes Off` comme remplacement universel.
- **Dépendances:** D-003, D-007, D-015.
- **Tests:** matrice des transitions section 16, double appel, événement ancien après chaque transition.
- **Risque:** double fermeture MIDI historique; le mute ne doit jamais appeler ce chemin de fermeture destructive.
- **Étape:** 8.

### D-009 — Sample d'application remplacé par la fin de bloc

- **Verdict:** `CORRIGER`; **sévérité:** `HAUTE`.
- **Preuve:** `seq_runtime_exec_begin_audio_block()` avance la timeline au début (`Src/Seq/seq_runtime_exec.c:269-273`). `audio.c` connait `block_start + cursor` (`:234`), mais `seq_play_scheduler_audio_apply_event()` utilise `seq_runtime_exec_get_audio_timeline_sample()` (`Src/Seq/seq_play_scheduler.c:1449-1451`).
- **Conséquences:** première note ARP décalée jusqu'à 64 frames, retrigger et microtiming faussés, mauvaise phase aux frontières de bloc.
- **Solution:** transmettre `event_sample_time` explicite de `audio_apply_seq_event_at_sample()` au seam scheduler puis NoteFx; conversions Q16 uniquement à la frontière qui en a besoin.
- **Alternatives rejetées:** recaler dans NoteFx; soustraire heuristiquement la taille du bloc; ajouter une horloge NoteFx.
- **Dépendances:** D-001, D-010.
- **Tests:** offsets 0/milieu/fin, frontière, external catch-up, tempo change.
- **Risque:** appels live hors audio à rebaser sur le même owner de timeline.
- **Étape:** 3.

### D-010 — Clock externe et pending steps sans budget temporel

- **Verdict:** `CORRIGER`; **sévérité:** `CRITIQUE`.
- **Preuve:** `z4_scheduler_clock_midi_debt_audit.md:37,91-95,126-130` confirme pending `uint16_t` jusqu'à 65535, sans timestamp, consommé sans quota et rattrapé au sample de début de bloc.
- **Conséquences:** coût non borné par demi-buffer, pulses regroupés au même sample, scheduler/NoteFx alimentés en rafale, dérive de phase externe.
- **Solution:** budget fixe et petit de rattrapage par demi-buffer/bloc. Sous la limite, traiter quelques clocks normalement; au-delà, abandonner ou coalescer le surplus, incrémenter un compteur, avancer les compteurs logiques sans exécuter chaque boundary intermédiaire, puis reprendre au prochain boundary cohérent. Utiliser un Song Position Pointer seulement s'il est effectivement reçu et supporté; sinon ne pas inventer de position absolue.
- **Alternatives rejetées:** traiter intégralement le backlog; utiliser `HAL_GetTick`; ajouter un timer dans NoteFx.
- **Dépendances:** D-009, D-011.
- **Tests:** rafale F8, backlog à 0/1/max, pause superloop, START/CONTINUE/STOP/source switch.
- **Risque:** les valeurs exactes de la petite borne et la politique de coalescence doivent être mesurées sur H743; la règle produit d'absence de rafale est déjà figée, sans modifier le FSM hors scope.
- **Étape:** 3 puis 6.

### D-011 — Budget NoteFx recréé par sous-segment et releases hors quota

- **Verdict:** `CORRIGER`; **sévérité:** `CRITIQUE`.
- **Preuve:** `note_fx_engine_process()` crée `uint8_t budget = NOTE_FX_MAX_EMISSIONS_PER_BLOCK` par track et par appel (`Src/NoteFx/note_fx_engine.c:109-146`). `audio.c:113-129,263-292` fragmente jusqu'à 64 fois; `release_slot()` peut émettre 16 Off sans ce budget.
- **Conséquences:** borne syntaxique normale jusqu'à `8 tracks x 8 emissions x 64 appels = 4096`, plus releases; le symbole `PER_BLOCK` ne borne pas une demi-zone.
- **Solution:** contexte de budget créé une seule fois pour 64 frames, quotas par track/stage et réserve Off, transmis à tous les sous-segments et cleanup.
- **Alternatives rejetées:** multiplier 8 par 64; laisser le moteur décider seul; limiter seulement les On.
- **Dépendances:** D-006, D-007, D-012.
- **Tests:** 8 tracks, 4 slots, ARP dense, deadlines chaque frame, cleanup simultané, compteur d'overflow.
- **Risque:** diminuer le débit musical; seuil final doit venir de H743.
- **Étape:** 6.

### D-012 — File scheduler O(N) et politique d'événements pleine incomplète

- **Verdict:** `REFONDRE`; **sévérité:** `HAUTE`.
- **Preuve:** `g_seq_play_events[512]` est compacte puis recherchée par scans complets dans `audio_collect_block_events()` (`Src/Seq/seq_play_scheduler.c:1258-1380`). `push()` est `void` et ne distingue que drop générique; les événements au même sample sont départagés par type, sans contrat complet pour occurrences.
- **Conséquences:** coût de scan et compactage dépend du backlog; program change, On, Off et transitions se disputent la capacité; high-water existe mais pas de quotas par classe ni réservation terminale.
- **Solution:** conserver des files séparées si la mesure le justifie: paires différées, commandes non-note, sorties Off et pending clocks. Ajouter quotas, high-water, ordre `(sample, priorité Off, occurrence_id)`, compteur de clocks abandonnées/coalescées et coût maximal documenté; aucune boucle ne traite l'intégralité d'un backlog dans une demi-zone.
- **Alternatives rejetées:** file universelle sans classe; tri dynamique; scan non borné jusqu'à 65535.
- **Dépendances:** D-010, D-011, D-013.
- **Tests:** 0/1/2 places, dense 8 tracks, ordre même sample, saturation et stale.
- **Risque:** RAM supplémentaire fixe; valider la carte Low-Cost avant adoption.
- **Étape:** 6.

### D-013 — TX MIDI sans admission ni priorité de fermeture

- **Verdict:** `REFONDRE`; **sévérité:** `CRITIQUE`.
- **Preuve:** `midi.c:164-183,321-343,717-720` utilise TX 128 et drop les channel-voice pleins; `midi_note_on/off()` sont `void` (`:1265-1290`). Seuls realtime clock/transport ont une insertion frontale prioritaire.
- **Conséquences:** On peut être compté actif alors que son paquet est perdu; Off ou panic peut être perdu; la reconnexion peut transmettre des paquets anciens; `MIDI_DEST_BOTH` ne fournit pas un acquittement par destination.
- **Solution:** admission indépendante par destination avec capacité réservée aux Off, retour d'enqueue jusqu'au terminal, compteur de refus par destination, purge générationnée de la seule destination MIDI sur déconnexion et politique déterministe de saturation. Un refus MIDI ne refuse jamais l'interne, et inversement.
- **Alternatives rejetées:** considérer `void` comme succès; envoyer davantage de All Notes Off; changer le débit USB hors chantier.
- **Dépendances:** D-004, D-008.
- **Tests:** TX 127/128, USB non-idle/deconnecte/reconnecte, On/Off/panic/clock mélangés.
- **Risque:** les deux destinations ne sont pas atomiques ensemble par décision produit; l'API doit exposer le masque d'admission réel et ne fermer que les destinations admises.
- **Étape:** 7.

### D-014 — Admission moteur ignorée et APIs internes pitch-based

- **Verdict:** `REFONDRE`; **sévérité:** `HAUTE`.
- **Preuve:** `seq_play_scheduler_emit_engine_note()` appelle plusieurs APIs `void`; l'admission Sampler Multi est castée/ignorée (`Src/Seq/seq_play_scheduler.c:637-661`). Les moteurs Stack, Wave, Deluge, Drum et VCA utilisent encore note/voix sans occurrence NoteFx.
- **Conséquences:** un On refuse ou vole une voix sans retour; un Off peut agir sur la voix la plus récente de même hauteur; le terminal ne sait pas si une fermeture est due.
- **Solution:** dispatcher terminal reçoit une admission logique indépendante et transmet un résultat/handle fixe à chaque destination; la polyphonie décide l'admission interne, NoteFx ne redéfinit pas la limite. Le ledger conserve `admitted_internal` et `admitted_midi` ou un masque équivalent.
- **Alternatives rejetées:** nouvelle polyphonie dans NoteFx; supprimer les refus moteurs; transformer tous les moteurs en graphes d'événements.
- **Dépendances:** D-004, D-003.
- **Tests:** moteur plein, retrigger même pitch, sampler multi, fermeture après steal, aucune NoteFx ne change le quota global.
- **Risque:** interfaces moteurs hors périmètre immédiat; adapter seulement le terminal nécessaire.
- **Étape:** 7.

### D-015 — Changement de source clock perd les notes non-ARP

- **Verdict:** `CORRIGER`; **sévérité:** `HAUTE`.
- **Preuve:** `seq_runtime_set_clock_source()` appelle `seq_play_scheduler_clear()`; `clear()` nettoie NoteFx, efface tokens et file mais ne force pas les Off des tokens actifs, contrairement à `clear_tracks()` (`Src/Seq/seq_play_scheduler.c:818-831,833-922`).
- **Conséquences:** un On non-ARP deja applique peut rester actif apres changement de source.
- **Solution:** faire de la transition commune une fermeture terminale idempotente avant invalidation, puis purger la file et incrementer generation.
- **Alternatives rejetées:** compter sur le prochain panic; conserver des événements vieux pour les rejouer.
- **Dépendances:** D-003, D-008.
- **Tests:** notes internes, MIDI, ARP actives lors de source switch; aucune note pendante ni replay.
- **Risque:** ordre STOP/CONTINUE; préserver la re-ancre absolue existante.
- **Étape:** 8.

### D-016 — Divisions musicales dupliquées

- **Verdict:** `REFONDRE`; **sévérité:** `MOYENNE`.
- **Preuve:** séquenceur expose `g_seq_div_labels` `OFF,1/2,1/4,1/8` (`Src/Param/param_registry_catalog.c:82,203`), UI ARP expose huit labels (`Src/UI/pages/ui_page_midi_fx.c:111`), et `note_fx_engine.c:100-106` possède une table privée numerator/denominator. `samples_per_step_q16` est l'autorité de cadence, mais aucun catalogue commun ne relie les indices.
- **Conséquences:** une future DIV peut interpreter un index ARP comme une autre durée; labels, persistence et conversions divergent.
- **Solution:** catalogue canonique label/ratio Q16/valeur persistée, sous-ensembles explicites par UI, conversion uniquement aux frontières.
- **Alternatives rejetées:** modifier les choix produit des pages; supprimer les sous-ensembles; stocker les labels en runtime.
- **Dépendances:** D-009.
- **Tests:** toutes divisions, ternaires, arrondis, tempo change, valeurs persistées invalides.
- **Risque:** migration historique de formats interdite; conserver les ordinals et convertir.
- **Étape:** 9.

### D-017 — Defaults/validation dépendants de l'unicité ARP

- **Verdict:** `REFONDRE`; **sévérité:** `HAUTE`.
- **Preuve:** `note_fx_state.c:9-22,65-104,117-134` applique les mêmes defaults aux quatre valeurs, clamp indépendamment et force un seul ARP. `note_fx_pipeline_sync_track()` réapplique encore une unicité runtime. Les paramètres génériques RATE/STYLE/RANGE ne peuvent pas décrire plusieurs modèles sans normalisation centrale.
- **Conséquences:** un changement de modèle futur peut réinterpréter des valeurs ARP; restore, Undo, clipboard, p-lock et UI ne partagent pas une validation inter-paramètres.
- **Solution:** autorité de modèle centrale, defaults par modèle, normalisation transactionnelle, unicité seulement pour ARP si produit confirmée; plusieurs instances d'autres modèles autorisées par slot.
- **Alternatives rejetées:** stockage de quatre banques par modèle; correction seulement UI; laisser chaque callback décider.
- **Dépendances:** D-005, D-016, persistence existante.
- **Tests:** OFF/ARP et modèles futurs, restore invalide, Undo/Redo, clipboard, p-lock partial/refus.
- **Risque:** toucher aux valeurs produit; ne changer ni format ni choix UI sans besoin architectural.
- **Étape:** 1 puis 9.

### D-018 — Abstractions historiques et tests de contrat incomplets

- **Verdict:** `SUPPRIMER`; **sévérité:** `MOYENNE`.
- **Preuve:** commentaires de `seq_play_scheduler.c:95-100` annoncent explicitement un miroir one-token-per-pitch temporaire; `tests/note_fx_plock_validation.ps1` verrouille l'unicité ARP runtime; le plan Euclid et les audits contiennent des prescriptions de paire déjà dépassées. Les fonctions `note_fx_pipeline_before_model_change` et `on_base_param_change` recouvrent des transitions distinctes mais non uniformes.
- **Conséquences:** un futur agent peut restaurer l'ancien chemin ARP ou traiter un audit historique comme état courant; la couverture valide des symboles, pas les invariants d'occurrence.
- **Solution:** supprimer après migration les miroirs et branches mortes; renommer seulement les APIs dont l'ownership change; remplacer les tests statiques historiques par tests comportementaux et recherches négatives.
- **Alternatives rejetées:** renommage cosmétique massif; supprimer des tests avant d'avoir leurs remplaçants.
- **Dépendances:** toutes les étapes 1-8.
- **Tests:** `rg` négatif sur anciens helpers/branches; CTest et scripts de transition; aucun symbole supprimé encore référencé.
- **Risque:** faux positif de recherche dans docs; exclure les historiques explicitement marqués.
- **Étape:** 10.

### D-019 — Matrice de validation NoteFx/RT absente

- **Verdict:** `CORRIGER`; **sévérité:** `MOYENNE`.
- **Preuve:** les tests actuels couvrent surtout l'algorithme ARP, le comptage terminal minimal, la présence des seams, les 16 p-locks et la séparation persistence/runtime. Aucun test ne prouve occurrences identiques, timestamp offset, quatre stages, moteur/TX refusé ou budget demi-buffer.
- **Conséquences:** les invariants critiques peuvent régresser tout en laissant CTest passer.
- **Solution:** ajouter tests unitaires fixes, tests d'intégration simulant les producteurs et scripts de recherches négatives; builds Release Low-Cost et Release Premium à chaque étape fonctionnelle.
- **Alternatives rejetées:** TestPremium; tests uniquement regex; mesure cible sans oracle logiciel.
- **Dépendances:** toutes les refontes.
- **Tests:** matrice complète section 16.
- **Risque:** tests trop couplés à une implémentation; tester contrats et compteurs.
- **Étape:** 10, avec tests de fermeture dans chaque étape.

### D-020 — Observation Live Record post-FX non encore disponible

- **Verdict:** `HORS DOMAINE`; **sévérité:** `FAIBLE`.
- **Preuve:** le terminal `note_fx_pipeline_terminal()` est le seam naturel, mais aucun enregistrement post-FX n'est implémenté dans le périmètre audité; les structures de live record actuelles sont une autre responsabilité.
- **Conséquences:** aucune capture post-FX aujourd'hui, mais un mauvais terminal empêcherait son ajout futur.
- **Solution recommandée:** conserver le terminal unique et son événement complet comme extension future; ne pas implémenter Live Record dans ce chantier.
- **Alternatives rejetées:** ajouter un hook de record maintenant; enregistrer dans chaque FX.
- **Dépendances:** D-001 et D-004 fournissent le contrat futur.
- **Tests:** présence d'un seam documenté, absence de nouveaux appels live-record dans les étapes.
- **Risque:** dérive de contrat si le futur chantier contourne le terminal.
- **Étape:** aucune étape fonctionnelle; garde en section 19.

### Constat historique non dette actuelle

`Z4-002` (On accepté avec une seule place libre) n'est pas inscrit comme dette: le HEAD réserve deux entrées sous une section critique dans `seq_play_scheduler_push_note_pair()` et le test `seq_play_scheduler_pair_validation.ps1` couvre 0/1/2 places. Cette garantie scheduler doit être conservée, mais elle ne couvre pas encore NoteFx, moteur ou USB; ces limites sont D-004, D-013 et D-014.

## 6. Contrat d'evenement recommande

Introduire un type fixe, partage par scheduler, NoteFx, terminal et instrumentation, sans mettre le runtime dans les structures persistées:

```text
note_event_t {
  sample_abs              uint64_t
  track                   uint8_t
  destination_id          uint8_t   // destination logique, canal projete ailleurs
  note, velocity          uint8_t
  kind                    NOTE_ON | NOTE_OFF
  provenance              SOURCE_KEY | SOURCE_STEP | SOURCE_MIDI | SOURCE_FX
  stage                   0..4
  source_token            uint32_t  // identite de l'entree possedee
  occurrence_id           uint32_t  // identite de chaque On accepte
  generation              uint32_t
  flags                   uint8_t   // generated/deferred/terminal/stale
}
```

`off_sample` ne doit pas etre ajoute a chaque evenement: il appartient au record fixe qui possede une occurrence et sa deadline. L'API de publication renvoie `ACCEPTED`, `REJECTED_CAPACITY`, `REJECTED_STALE`, `REJECTED_DESTINATION` ou `DROPPED_POLICY`; l'evenement ne ment pas sur son admission. `source_token` est conserve quand un FX transforme sans creer une occurrence; un fan-out cree des `occurrence_id` enfants bornés avec provenance parent, et chaque enfant garde son propre Off.

Le scheduler alloue le token source et reserve la paire; le runtime NoteFx alloue les occurrences generees seulement apres reservation de leur fermeture; le terminal ne rederive jamais une identite depuis la hauteur. La generation est incrementee par track pour transition et globalement pour reset complet. Wrap-around doit eviter zero et rejeter tout record dont generation ne correspond pas.

## 7. Identite et cycle de vie des occurrences

Cycle nominal:

```text
reserve pair -> source On accepted -> stage 0
  -> stage n owns source/children -> deferred deadline
  -> terminal admission -> destination ledger owns occurrence
  -> exact Off -> release all ledgers
```

Un On refuse si l'un des elements necessaires manque: paire scheduler, source table, fan-out table, budget, destination ledger ou fermeture terminale. Un Off sans record correspondant est stale et compte, sans effet. Un retrigger cree une nouvelle occurrence; il ne remplace pas l'ancienne.

Sur STOP/panic/model change/pattern change: `close_active(scope, sample)` emet les Off encore possedes avec priorite, marque les records fermes, incremente generation, purge les files du scope, puis reset les phases/deadlines. Pour `MUTE_TRIGS`, aucune fermeture, purge ou invalidation des occurrences actives n'est autorisee: seules les nouvelles sources STEP sont bloquees. L'operation est idempotente.

Les tables de compatibilite par `[track][note]` et par voix sont supprimees du chemin NoteFx. Une table pitch peut rester une projection UI/diagnostic non normative, mais ne peut ni admettre ni fermer une occurrence.

La regle de cycle ci-dessus a une exception produit obligatoire: `MUTE_TRIGS` ne ferme ni ne purge les occurrences actives. Il bloque les nouveaux trigs STEP et leurs derivations futures, mais laisse leurs echeances et Note Off traverser. Seules les politiques STOP/PANIC/reconfiguration appellent `close_active()`.

## 8. Horloge, timestamps et deadlines

Autorite unique: `seq_runtime_exec_audio_timeline_sample` pour l'audio; `seq_runtime_exec_begin_audio_block()` est le seul avanceur. Le scheduler planifie en sample absolu; l'audio convertit une seule fois en offset; NoteFx reçoit le `event_sample_time` réel; le terminal conserve ce sample pour trace/record futur.

Notions et conversions:

| Notion | Autorité | Usage |
|---|---|---|
| sample absolu | execution audio | identite temporelle monotone |
| debut de bloc | execution owner | base de collecte |
| offset | audio | `sample_abs - block_start`, clamp explicite seulement pour retard |
| `samples_per_step_q16` | clock/runtime | cadence musicale commune |
| tick/step | scheduler/clock | production de boundaries, jamais timestamp terminal direct |
| deadline | owner du stage | sample absolu d'un Off ou prochain pulse |
| tempo/division | registre/catalogue -> conversion runtime | ratio Q16, pas table locale concurrente |

Les arrondis ont lieu à la frontière: scheduler pour microtiming entier, catalogue pour ratio Q16, audio pour offset entier. Un événement exactement à `block_start` est traité avant le premier frame; à `block_end` il appartient au bloc suivant; un retard est compte et traite a offset 0 selon la politique documentée. Le sample d'application ne doit jamais etre remplace par `get_audio_timeline_sample()` après l'avance de bloc.

Le changement de tempo recalcule les futures échéances à partir de l'autorité de cadence, mais ne déplace pas rétroactivement une occurrence déjà admise; une transition de modèle ferme d'abord les owned. Les pulses externes en retard ont un quota et une politique de rephase explicite, pas un rattrapage intégral non borné.

## 9. Chaine ordonnee des MIDI FX

```text
source(stage 0)
  -> slot[0]
  -> slot[1]
  -> slot[2]
  -> slot[3]
  -> terminal post-FX(stage 4)
```

Chaque stage reçoit un contexte fixe `{event queue, budget, source table, output table}` et peut:

- transmettre le même événement au stage suivant;
- transformer note/velocity/destination en conservant provenance et generation;
- supprimer et libérer la possession;
- produire plusieurs événements dans une capacité fixe;
- placer une échéance dans le scheduler différé du même stage;
- fermer ses enfants avant reset.

Le dispatch ne recherche jamais un modèle par type. Un stage OFF doit d'abord retrouver l'occurrence qu'il possède, puis propager l'Off à ses enfants; un On généré prend `stage+1`. La profondeur maximale est quatre, le fan-out maximal est une constante de build mesurée, et aucun callback ne rappelle stage 0.

## 10. Files, budgets et politique de saturation

### 10.1 Politique proposée

Conserver les files qui ont une responsabilité distincte, mais les doter de classes:

| File | Décision | Contrat futur |
|---|---|---|
| scheduler pairs | conserver/refondre | reservation atomique On+Off, quota pairs, generation |
| scheduler non-note/program | conserver séparée | ne peut pas consommer la réserve Off |
| NoteFx continuation | créer fixe par track/stage | fan-out et différés bornés, stage ordonné |
| NoteFx commandes runtime | créer fixe | seul owner audio écrit runtime |
| audio block events | conserver | capacité 128 vérifiée, refus diagnostiqué |
| MIDI TX/RX | conserver séparées | TX reserve fermetures, RX policy existante instrumentée |
| Stack | hors refonte NoteFx | son contrat Z1 reste à fermer, sans file universelle |

Les Note Off dus sont drainés avant nouveaux On. Si la capacité restante ne garantit pas On+Off à la source, au stage ou dans une file de fermeture, l'occurrence entière est refusée, `saturation_count` et `dropped_occurrence_count` sont incrémentés, et aucun Off artificiel n'est généré. Au terminal, l'admission interne et MIDI est indépendante: une destination peut être refusée sans refuser l'autre, et les releases consomment la réserve Off de chaque destination admise.

Saturation et admission terminale sont independantes par destination: une reservation de fermeture MIDI concerne le ledger MIDI uniquement; elle ne reserve pas et ne bloque pas l'interne. Les Note Off dus des deux destinations gardent la priorite, mais ne sont emis que pour les destinations dont le On a ete admis. Les clocks externes pending ont une classe separee: rattrapage limite, surplus abandonne/coalesce, compteur obligatoire, jamais de drain integral.

### 10.2 Budget hard real-time

Le rattrapage de clock externe partage ce budget. Une petite borne fixe de clocks peut etre executee normalement; tout surplus est abandonne ou coalesce, compte, et repercute dans les compteurs logiques/phase sans executer chaque step intermediaire. Aucun backlog important ne peut etre rejoue en rafale au meme sample ou monopoliser une demi-zone.

Le budget est créé une fois par demi-buffer de 64 frames, transmis aux sous-segments, et inclut scans, stages, continuations, terminal, releases et commandes de transition. Mesures H743 requises pour Low-Cost et Premium:

1. cycles max/min/percentile de `process_half`, collect, scheduler, NoteFx par stage, terminal MIDI et moteur;
2. max segments, max événements entrants, max émissions par track et par demi-buffer;
3. high-water de chaque file, refus On, Off prioritaires, stale et deadlines dépassées;
4. accord dense, huit tracks, ARP actif, quatre stages et saturation moteur/TX;
5. frame MSP, canari/high-water et marge réelle, en utilisant l'indicateur IRQ audio existant.

Seuil de fermeture: budget de travail dur `<= 80%` du temps disponible de la demi-zone, alerte à 70%, marge documentée par variante; les valeurs numériques de quota sont choisies après mesure et restent des constantes fixes. Le plan ne crée ni profiler lourd ni écriture lente depuis l'IRQ.

## 11. Terminal et destinations

Le terminal reste un point unique et identifiable. Il reçoit l'événement complet et effectue, dans un ordre documenté:

1. vérifier generation, possession et type;
2. réserver/admettre la destination interne et la file MIDI selon les quotas;
3. publier On ou Off à chaque destination avec le même `occurrence_id`;
4. mettre à jour le ledger seulement selon le résultat local;
5. incrémenter les diagnostics de refus.

MIDI et moteur ne doivent pas être appelés depuis un FX. Le canal MIDI est une projection de `destination_id`. L'admission est obligatoirement indépendante: un refus d'une destination ne refuse jamais l'autre. Le ledger conserve le masque réel, les Off ciblent uniquement les destinations admises, et si les deux refusent l'occurrence est entièrement refusée sans Off artificiel. La limite globale de polyphonie reste dans `synth_polyphony`/moteur et n'est pas redéfinie par NoteFx.

Admission terminale normative: moteur interne et MIDI externe sont independants. `BOTH` ne signifie pas "les deux doivent accepter"; il demande deux tentatives separees. Le ledger conserve au minimum `admitted_internal` et `admitted_midi`. Les quatre cas sont fixes: interne oui/MIDI non = son interne et Off interne seulement; interne non/MIDI oui = MIDI et Off MIDI seulement; oui/oui = les deux; non/non = occurrence entierement refusee, sans Off artificiel. Une deconnexion ou saturation MIDI ne purge jamais le ledger interne. Un panic ferme chaque destination selon son admission reelle.

## 12. Transitions et cleanup

Le contrat commun est `transition_apply(scope, policy)` ou l'equivalent conforme aux conventions du depot. La politique est obligatoire et distingue au minimum `MUTE_TRIGS`, `STOP_CLOSE`, `PANIC_CLOSE_ALL`, `MODEL_RECONFIGURE`, `PATTERN_REPLACE` et `DESTINATION_REBIND`; il est interdit qu'une routine ferme systematiquement les notes pour tous les scopes.

| Transition | Nouveaux Note On | Occurrences actives | Note Off dus | Purge/generation | Reprise |
|---|---|---|---|---|---|
| MUTE_TRIGS | bloques pour les trigs sequenceur | conservees | toujours traites | aucune purge destructive, generation active conservee | prochain trig futur |
| UNMUTE | autorises | inchangees | toujours traites | aucun retrigger | prochain trig futur |
| STOP | bloques | fermees selon STOP | forces si necessaire | purge scheduler/NoteFx/pending et nouvelle generation | nouveau START/CONTINUE defini |
| PANIC | bloques | fermees immediatement | forces par destination admise | purge complete | etat silencieux |
| pattern/load | bloques pendant transaction | fermees avant remplacement | forces par destination admise | generation puis restore bases | nouveau pattern |
| modele FX/slot reset | bloques pour le slot | owned du slot fermees | forces par destination admise | generation slot, defaults | nouveau modele |
| changement MIDI channel/role | bloques pendant transition | fermees si destination rebind | forces par destination admise | purge destination/stale | rebind explicite |
| source clock | bloques pendant transition | fermees si necessaire au rebind temporel | garanties | purge pending/stale et reancrage borne | nouvelle clock |

Les routines publiques actuelles deviennent des façades de commande vers ce protocole; elles ne doivent plus chacune émettre un sous-ensemble différent de fermetures.

Correction normative de la ligne MUTE: `MUTE_TRIGS` bloque les nouveaux Note On provenant des trigs du sequenceur, conserve les occurrences actives, ne purge pas leurs ledgers ni leur generation, et laisse leurs Note Off normaux traverser. UNMUTE ne rejoue aucun trig manque et ne retrigger aucune note; il reprend au prochain trig futur. La politique NoteFx ne bloque pas les entrees clavier et MIDI live; toute suppression deja definie au niveau de leur source reste inchangee et doit etre testee separement.

## 13. Autorités de données et runtime

- `track_topology` reste autorité des tracks et capacités.
- `note_fx_state` reste autorité des bases persistables: modèle et paramètres normalisés.
- `seq_runtime`/clock reste autorité du temps et des divisions runtime projetées.
- un owner audio NoteFx est seul autorisé à muter `g_slot`, owned, phases, deadlines, overrides runtime et ledgers.
- UI, param registry, restore, Undo/Redo, clavier et MIDI publient des commandes fixes; ils lisent des snapshots cohérents.
- les commandes sont appliquées à une frontière déterministe avant le traitement des événements dus.

Aucune commande ne transporte un pointeur vers un état mutable et aucune API n'attend une section critique implicite. Les copies Pattern, Project, Patch, Kit, snapshot, clipboard et Undo contiennent seulement la base `note_fx_track_state_t`; token, generation, phase, owned, file et timestamp courant sont explicitement exclus.

## 14. Code mort, duplications et documentation

À supprimer après migration et recherches négatives:

- miroir `g_seq_play_active_event_token[track][note]` du chemin NoteFx;
- recherche du premier ARP et contraintes d'unicité déplacées hors de l'autorité modèle;
- callbacks directs d'anciens chemins ARP et branches de retour au terminal qui contournent stage;
- routines de cleanup dupliquées une fois la commande de transition commune prouvée;
- tests regex qui ne vérifient plus le contrat courant.

À conserver avec justification: `note_fx_arp_t` comme algorithme de parcours temporairement interne, quatre slots et 16 positions p-lock, `seq_play_scheduler_push_note_pair()` atomique, files MIDI RX/TX distinctes, compteur de charge IRQ existant, et bases NoteFx persistées séparément du runtime.

Documentation à corriger pendant consolidation: divergence Pattern v4/v5 entre `ARCHITECTURE_GLOBAL.md` et `z6_state_persistence_patterns_projects.md`, capacité scheduler historique 256/512 dans `z4_seq_clock_scheduler.md`, constat Z4-002 dépassé, et commentaires de compatibilité pitch-only. `docs/plan_midi_fx_euclid.md` ne doit pas être modifié dans cette passe; il sera réaudité ensuite.

## 15. Plan d'action par étapes

Chaque étape est autonome et committable. L'agent d'exécution doit relire les dettes citées, implémenter uniquement le périmètre de l'étape, faire les recherches négatives et produire les builds requis `Release Low-Cost` et `Release Premium`. `TestPremium` est exclu.

### Étape 1 — Autorités et contrat canonique d'événement

- **Objectif:** introduire le type commun, les statuts, provenance et stage sans changer encore l'algorithme des FX.
- **Dettes:** D-001, D-017 partiellement.
- **Fichiers/symboles:** headers `Inc/NoteFx`, `Inc/Seq/seq_play_scheduler.h`, `note_fx_pipeline_submit`, `seq_play_scheduler_audio_event_t`, param state.
- **Changements:** ajouter event/result fixes; propager token/generation/sample/stage; centraliser validation et defaults sans nouveau modèle.
- **Invariants:** aucune perte d'identité; aucune persistence runtime; anciens appels refusent explicitement les arguments invalides.
- **Hors périmètre:** quatre-stage effectif, Euclid, Live Record, UI fonctionnelle.
- **Dépendances:** aucune; garder paire scheduler actuelle.
- **Tests automatisés:** contrat de layout/valeurs, source keyboard/STEP/MIDI/scheduler, stale generation.
- **Tests manuels:** On/Off simple et trace des champs au terminal.
- **Instrumentation:** compteurs acceptance/rejection/stale bornés.
- **Recherches négatives:** aucun `note_fx_pipeline_submit` sans sample/provenance; aucun token persisté.
- **Fin:** tous les appels compilent, API documentée, Release Low-Cost/Premium verts.
- **Documentation:** section 6 et architecture Z4.
- **Commit:** `refactor: define canonical note fx event contract`.

### Étape 2 — Identité des occurrences et atomicité

- **Objectif:** remplacer les associations pitch-only par des ledgers d'occurrences.
- **Dettes:** D-002, D-003.
- **Fichiers/symboles:** `note_fx_arp`, `note_fx_engine`, scheduler active ledger, `seq_output_guard`.
- **Changements:** source token exact, occurrence table fixe, pair ownership, retrigger et Off exact.
- **Invariants:** aucune vieille occurrence coupée par un nouvel Off; refus complet sans On.
- **Hors périmètre:** admission USB/moteur complète, nouveau FX.
- **Dépendances:** Étape 1.
- **Tests automatisés:** mêmes pitches, sources simultanées, retrigger, generations, table pleine, pair 0/1/2.
- **Tests manuels:** clavier et MIDI externe simultanés, accord dense.
- **Instrumentation:** active_count/high-water, orphan Off, duplicate close.
- **Recherches négatives:** aucun lookup normatif par `note` seul dans NoteFx/guard.
- **Fin:** chaque On admis a un record et un chemin Off déterministe.
- **Documentation:** sections 4/7; mettre à jour commentaire scheduler.
- **Commit:** `refactor: preserve note occurrence identity through runtime`.

### Étape 3 — Horloge, timestamps et deadlines

- **Objectif:** aligner tous les événements sur le sample d'application monotone.
- **Politique clock externe figée:** autoriser seulement un petit rattrapage fixe, déterminé après mesure H743. Au-delà, abandonner ou coalescer le surplus, compter chaque clock abandonnée/coalescée, avancer les compteurs logiques disponibles sans exécuter tous les boundaries, puis reprendre au prochain boundary cohérent. Ne jamais rejouer un backlog important en rafale.
- **Dettes:** D-009, D-010 partiellement.
- **Fichiers/symboles:** `audio_apply_seq_event_at_sample`, `seq_runtime_exec`, `seq_play_scheduler_audio_apply_event`, NoteFx deadline.
- **Changements:** propager sample explicite; définir frontière Q16/entier; borner catch-up external clock.
- **Invariants:** offset 0/milieu/fin exact; aucun double horloge; deadline future monotone.
- **Hors périmètre:** nouvelle clock MIDI hardware, Live Record.
- **Dépendances:** Étapes 1-2.
- **Tests automatisés:** bloc split, frontière, tempo, microtiming, overdue.
- **Tests manuels:** ARP première note, MIDI externe et retrigger au boundary.
- **Instrumentation:** overdue, clamped offset, drift, pulses deferres.
- **Recherches négatives:** aucun `note_fx_pipeline_submit(...get_audio_timeline_sample())` pour un event audio; aucune horloge milliseconde.
- **Fin:** sample de terminal identique à l'offset appliqué et timeline monotone prouvée.
- **Documentation:** sections 8 et z4.
- **Commit:** `fix: use application sample for note fx deadlines`.

### Étape 4 — Propriété runtime et commandes de transition

- **Objectif:** donner un owner unique au runtime NoteFx.
- **Politique de transition figée:** l'owner applique `MUTE_TRIGS` sans close/purge/generation des occurrences actives; il bloque uniquement les nouvelles sources STEP et laisse les Off possédés traverser. STOP, PANIC, model/pattern change et destination rebind gardent leurs politiques destructives propres.
- **Dettes:** D-007, D-008 partiellement.
- **Fichiers/symboles:** pipeline/engine runtime, param interface, restore, UI cleanup, seq mute bridge.
- **Changements:** commande fixe, apply transactionnel par track, snapshot de lecture, close/invalidate/purge commun.
- **Invariants:** un seul écrivain runtime; reset idempotent; aucune configuration partielle observée.
- **Hors périmètre:** refonte autres DSP/RTOS.
- **Dépendances:** Étapes 1-3.
- **Tests automatisés:** interleavings, p-lock/restore/Undo/clipboard, double cleanup.
- **Tests manuels:** UI model change pendant lecture, mute/panic/transport.
- **Instrumentation:** owner violation, command high-water, transitions ignorées.
- **Recherches négatives:** aucun appel main direct à `note_fx_engine_*` de mutation; aucune section critique implicite.
- **Fin:** tous les producteurs publient une commande et le runtime ne fuit pas dans snapshots.
- **Documentation:** sections 12-13, z1.
- **Commit:** `refactor: serialize note fx runtime transitions`.

### Étape 5 — Chaîne fixe des quatre slots

- **Objectif:** exécuter `[0..3]` dans l'ordre et router les sorties au stage suivant.
- **Dettes:** D-005, D-006.
- **Fichiers/symboles:** engine/pipeline headers et C, nouveaux tests NoteFx.
- **Changements:** stage context, continuation fixe, fan-out borné, suppression/différé/close par stage.
- **Invariants:** aucun slot sauté, rebouclé ou contourné; terminal unique au stage 4.
- **Hors périmètre:** Euclid/Harmony/Gate/Probability et UI nouvelle.
- **Dépendances:** Étapes 1-4.
- **Tests automatisés:** quatre ordres, generated slot1 -> 2-4, suppression, fan-out.
- **Tests manuels:** ARP en chaque slot, événements différés, accords.
- **Instrumentation:** emissions par stage, continuation drops, max fan-out.
- **Recherches négatives:** aucun scan `first ARP`, aucun appel terminal depuis stage <4.
- **Fin:** test comportemental prouve la chaîne et l'absence de boucle.
- **Documentation:** sections 3/9 et plan Euclid seulement cité, non édité.
- **Commit:** `refactor: execute midi fx slots as ordered stages`.

### Étape 6 — Files, quotas et budgets hard real-time

- **Objectif:** rendre capacité et coût déterministes sur demi-buffer.
- **Clock externe:** le backlog pending est une classe budgétée distincte; quelques clocks peuvent être exécutées normalement, le surplus est abandonné/coalescé et compté. Aucun drain intégral du pending dans une demi-zone.
- **Dettes:** D-011, D-012, D-010 résiduel.
- **Fichiers/symboles:** audio, scheduler queues, NoteFx continuation/commands, diagnostics.
- **Changements:** budget demi-buffer, quotas classes, réserve Off, high-water, refus complets aux niveaux source/stage et refus indépendants par destination; conserver files séparées justifiées.
- **Invariants:** Off prioritaire; aucun On sans fermeture; coût borné par 64 frames.
- **Hors périmètre:** Stack Z1 global et modifications de polyphonie.
- **Dépendances:** Étapes 1-5.
- **Tests automatisés:** 0/1/2 places, dense 8 tracks, 4 stages, saturation.
- **Tests manuels:** accord dense/ARP, CPU load audio.
- **Instrumentation:** cycles via indicateur existant, high-water, drops, deadline misses, quota per half.
- **Recherches négatives:** aucun `budget =` dans une boucle de sous-segment; aucune release hors budget.
- **Fin:** mesures H743 Low-Cost/Premium sous seuil section 10, seuils commits dans doc.
- **Documentation:** z1/z4 et section 10.
- **Commit:** `perf: bound note event work per audio half-buffer`.

### Étape 7 — Terminal, admission et saturation

- **Objectif:** fermer les refus MIDI/moteur et préserver l'identité au terminal.
- **Admission figée:** les destinations interne et MIDI sont tentées indépendamment. Le ledger porte deux résultats; le refus d'une destination ne bloque jamais l'autre et les Off ciblent uniquement les destinations admises.
- **Dettes:** D-004, D-013, D-014.
- **Fichiers/symboles:** terminal scheduler, output guard, `midi.c`, adapters moteurs nécessaires.
- **Changements:** API d'admission indépendante, ledger par destination, réserve Off TX, retour moteur et refus déterministe par destination.
- **Invariants:** même événement terminal pour les deux tentatives; aucun guard actif sans admission locale; Off uniquement vers les destinations admises.
- **Hors périmètre:** changement de limite globale polyphonique et nouveau transport MIDI.
- **Dépendances:** Étapes 1-6.
- **Tests automatisés:** moteur plein, TX plein/non-idle, internal+external, panic.
- **Tests manuels:** USB débranché/rebranché, sortie MIDI saturée.
- **Instrumentation:** admitted/refused per destination, forced Off, USB drops.
- **Recherches négatives:** aucun appel `midi_note_*` dans `Src/NoteFx`; aucun `void` ignoré sur seam terminal.
- **Fin:** le terminal explique chaque admission/refus et n'introduit aucune note pendante.
- **Documentation:** sections 11/18, z4.
- **Commit:** `refactor: make note terminal admission explicit`.

### Étape 8 — Transport, mute, panic et pattern

- **Objectif:** unifier toutes les transitions et purger l'obsolète.
- **Mute de trigs:** remplacer le close/purge actuel du mute par un blocage des nouveaux trigs STEP. Les occurrences actives, deadlines et Note Off restent possédés; UNMUTE ne rejoue rien et reprend au prochain trig futur. STOP/PANIC/model/pattern/destination restent séparés et destructifs lorsqu'ils le nécessitent.
- **Dettes:** D-008, D-015.
- **Fichiers/symboles:** scheduler clear/suspend/resume, output guard panic, runtime transport, pattern/snapshot/model callbacks.
- **Changements:** protocole `transition_apply(scope, policy)`; MUTE_TRIGS = blocage des nouveaux trigs sans close/purge/generation active; STOP/PANIC/model/pattern/source/destination appliquent leur politique destructive propre, avec idempotence.
- **Invariants:** zéro occurrence pendante après chaque transition; aucun événement stale terminal.
- **Hors périmètre:** migrations de formats, Live Record.
- **Dépendances:** Étapes 1-7.
- **Tests automatisés:** STOP, CONTINUE, MUTE_TRIGS/UNMUTE non destructifs, Off possédé pendant mute, panic différé, pattern/model/source change.
- **Tests manuels:** notes actives sur chaque transition et double action.
- **Instrumentation:** forced close, stale purge, pending queue after transition.
- **Recherches négatives:** aucune routine de cleanup concurrente non deleguée; aucune purge sans generation.
- **Fin:** matrice transition verte et API public documentée.
- **Documentation:** sections 12/16 et architecture transitions.
- **Commit:** `fix: unify note fx transition cleanup`.

### Étape 9 — Catalogue musical et validation des paramètres

- **Objectif:** une autorité de divisions et defaults par modèle sans changement de formats.
- **Dettes:** D-016, D-017 résiduel.
- **Fichiers/symboles:** `param_registry_catalog`, seq runtime div, UI virtual labels, NoteFx state/persistence/p-lock.
- **Changements:** catalogue ratio Q16, sous-ensembles explicites, normalisation inter-paramètres, defaults model-aware.
- **Invariants:** labels et conversions cohérents; ordinals persistés inchangés; aucun runtime persisté.
- **Hors périmètre:** ajout Euclid/Harmony et changement produit des pages.
- **Dépendances:** Étapes 1-8.
- **Tests automatisés:** all divisions/ternaires, invalid restore, p-lock/Undo/clipboard.
- **Tests manuels:** changement tempo/div et affichage actuel ARP.
- **Instrumentation:** arrondis et conversion counters en debug seulement.
- **Recherches négatives:** une seule table canonique; aucun `numerator/denominator` concurrent hors catalogue.
- **Fin:** revue des ordinals et tests persistence verts.
- **Documentation:** z4, z6 et section 14; ne pas modifier Euclid.
- **Commit:** `refactor: centralize musical division conversions`.

### Étape 10 — Suppression, tests et consolidation

- **Objectif:** retirer les anciennes abstractions et fermer la matrice complète.
- **Recherches finales obligatoires:** absence de cleanup destructif appelé par le mute, absence de retrigger à l'unmute, absence de statut `BOTH` exigeant les deux admissions, refus MIDI indépendant de l'interne, et absence de boucle drainant un backlog clock arbitraire.
- **Dettes:** D-018, D-019; vérification de tous les autres IDs.
- **Fichiers/symboles:** anciens helpers, tests, docs Z1/Z4/architecture, plans de build.
- **Changements:** suppressions prouvées, tests comportementaux, docs corrigées, recherches négatives.
- **Invariants:** aucun symbole mort du registre, aucun test ancien contradictoire, code fonctionnel hors scope intact.
- **Hors périmètre:** implémentation Euclid, Live Record, migrations.
- **Dépendances:** Étapes 1-9.
- **Tests automatisés:** matrice section 16, CTest, scripts validation, Release Low-Cost/Premium.
- **Tests manuels:** sessions transition/USB/accord/ARP sur H743.
- **Instrumentation:** garder seulement compteurs bornés et désactivables; supprimer traces temporaires.
- **Recherches négatives:** pitch-only, first-ARP, direct FX terminal, dynamic allocation, runtime persistence, old queue cap.
- **Fin:** registre D-001..D-019 fermé, D-020 explicitement hors domaine, docs cohérentes.
- **Documentation:** ce plan et audits Z1/Z4; Euclid reste inchangé jusqu'au réaudit.
- **Commit:** `chore: consolidate note fx scheduler cleanup`.

## 16. Matrice de validations

| # | Scénario | Oracle de fermeture |
|---:|---|---|
| 1 | On/Off simple | une occurrence, un Off |
| 2 | deux mêmes hauteurs successives | deux IDs indépendants |
| 3 | deux sources même hauteur | aucun alias |
| 4 | retrigger avant ancien Off | ancien Off ne coupe pas nouveau On |
| 5 | file 0/1/2 places | 0/1 refus complet, 2 paire admise |
| 6 | reservation atomique paire | count +2 ou aucun On |
| 7 | saturation Off prioritaire | Off dus drainés avant On |
| 8 | STOP notes actives | zéro owned/ledger |
| 9 | panic événements différés | purge + Off idempotents |
| 10 | MUTE_TRIGS/reprise | nouveaux STEP bloques, occurrences conservees, reprise sans replay |
| 11 | pattern pending | generation stale rejetée |
| 12 | modèle pendant lecture | close avant defaults/config |
| 13 | clavier | même contrat terminal |
| 14 | STEP | sample planifié et paire |
| 15 | MIDI externe | provenance distincte, même pipeline |
| 16 | quatre slots ordres | `[0,1,2,3]` toujours |
| 17 | généré slot1 -> 2-4 | aucun contournement |
| 18 | différé généré | deadline et Off exact |
| 19 | début/milieu/fin bloc | offsets exacts |
| 20 | frontière microtiming | ordre stable Off puis On |
| 21 | tempo change | timeline monotone, future deadline cohérente |
| 22 | huit tracks | quotas indépendants et global borné |
| 23 | accord dense | saturation explicite, pas de note fantôme |
| 24 | budget demi-buffer | seuil cycles respecté |
| 25 | moteur saturé | admission refusée et ledger cohérent |
| 26 | MIDI saturé/refusé | refus remonté, Off réservé |
| 27 | persistence | aucun phase/token/owned/deadline |
| 28 | Undo/Redo/clipboard | base restaurée, runtime reconstruit |
| 29 | recherches négatives | anciens symboles absents |
| 30 | chaque transition | zéro note pendante |

Les tests de chaque étape doivent identifier les numéros concernés. Les builds requis sont toujours Release Low-Cost et Release Premium; ne pas lancer TestPremium.

### 16.1 Validations produit obligatoires

| # | Scénario | Oracle |
|---:|---|---|
| 31 | accord long avant MUTE_TRIGS | aucun close; accord et enveloppe terminent normalement |
| 32 | nouveaux STEP pendant mute | aucun nouveau On sequenceur ni derivé FX |
| 33 | Off possédé pendant mute | Off traversant, ledger conservé jusqu'à fermeture |
| 34 | UNMUTE | aucun retrigger ni replay; prochain trig futur seulement |
| 35 | clavier/MIDI live pendant mute | comportement de source actuel conservé, pas de blocage implicite |
| 36 | double mute/unmute et panic sous mute | idempotence; panic ferme toutes destinations admises |
| 37 | interne admis + MIDI refusé | interne seul, Off interne seul |
| 38 | interne refusé + MIDI admis | MIDI seul, Off MIDI seul |
| 39 | interne/MIDI admis | deux ledgers et deux Off |
| 40 | interne/MIDI refusés | occurrence refusée, aucun Off artificiel |
| 41 | déconnexion avant/après On MIDI | interne indépendant; ledger MIDI purgé selon politique |
| 42 | MIDI plein, interne disponible | interne continue |
| 43 | moteur saturé, MIDI disponible | MIDI continue |
| 44 | backlog clock 1/petit/limite/supérieur/très grand | rattrapage limité; surplus compté et jamais rejoué en rafale |
| 45 | START/CONTINUE/STOP avec backlog | compteurs/phase cohérents, reprise sans rafale |
| 46 | Song Position Pointer supporté | recalage seulement depuis position réellement reçue |
| 47 | pause traitement principal | budget demi-buffer respecté, abandon/coalescence observable |

Les scénarios 1-30 restent applicables. La ligne historique `mute/reprise` du tableau précédent est interprétée par cette sous-matrice normative: elle ne signifie pas close/suspend destructif.

## 17. Instrumentation et mesures

Les diagnostics doivent séparer `admitted_internal`, `admitted_midi`, Off prioritaires par destination, refus MIDI, refus moteur, déconnexion/purge MIDI, clocks abandonnées et clocks coalescées. Ils doivent aussi compter les trigs bloqués par MUTE_TRIGS et vérifier qu'aucun owned actif n'est supprimé par ce seul motif.

Ajouter seulement des compteurs fixes, compile-time disableables, lus hors chemin chaud: high-water par file, émissions/Off prioritaires par demi-buffer, saturation/drop, stale, deadline dépassée, cycles max et max sous-segments. Réutiliser `audio_seq_diag`/indicateur de charge IRQ quand possible. Ne pas imprimer depuis IRQ, ne pas écrire SD/USB depuis IRQ, ne pas ajouter de trace à taille dynamique.

Protocole H743: instruments Low-Cost puis Premium, 64 frames, huit tracks, ARP dense, chaque ordre de slot, moteur/TX saturés, external catch-up et transitions. Reporter fréquence, cycles disponibles, max observé, p99 si utile, marge, high-water et compteurs. Les seuils finaux de quota sont acceptables seulement avec répétition worst-case et sans underrun.

## 18. Dettes conservées volontairement

Il n'y a aucune dette active qualifiée `CONSERVER`. Les compromis intentionnels suivants sont conservés et documentés:

- quatre slots fixes plutôt qu'un graphe dynamique;
- files fixes séparées par responsabilité plutôt qu'une file universelle;
- owner audio unique et commandes bornées plutôt qu'un RTOS/IPC;
- limite globale de polyphonie inchangée;
- bases NoteFx persistées dans Pattern/Project/snapshots, runtime reconstruit;
- table ARP de parcours conservée comme algorithme interne jusqu'à l'étape 10, sans lui laisser l'ownership du pipeline.

## 19. Éléments hors domaine

- Euclid, Harmony, Probability, Gate, Groove et tout nouveau modèle MIDI FX;
- Live Record post-FX, bounce vers piste et nouvelle UI fonctionnelle;
- changement de limite globale de polyphonie;
- migration historique de formats Pattern/Project/Patch/Kit;
- RTOS, allocation dynamique, bus général et split dual-core;
- refonte générale de la file Stack ou du mixeur hors dépendance démontrée.

Ces sujets peuvent consommer le contrat et le terminal produits ici, mais ne doivent pas être implémentés par une étape de ce plan.

## 20. Ordre recommandé d'exécution

`1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7 -> 8 -> 9 -> 10`.

Les dépendances sont structurelles: l'identité précède les paires et le terminal; le timestamp précède les deadlines; l'owner précède les stages; les stages précèdent le budget; l'admission précède le cleanup complet; les suppressions viennent après les tests de fermeture. Une étape ne doit pas être contournée pour livrer Euclid.

## 21. Conditions nécessaires avant la mise à jour du plan Euclid

Le plan Euclid pourra être réaudité sur le nouveau HEAD seulement lorsque:

1. D-001 à D-019 sont fermées ou requalifiées explicitement, avec aucun `CRITIQUE` ouvert dans le chemin NoteFx/scheduler.
2. Un événement complet avec occurrence/generation/stage atteint le terminal et les tests prouvent le même comportement pour clavier, STEP, MIDI externe, ARP et futur généré.
3. Les quatre slots sont ordonnés, les événements différés reprennent au stage suivant et aucun premier-ARP/terminal direct ne subsiste.
4. Les paires et sorties ont une fermeture garantie; les scénarios retrigger, saturation 0/1/2 et transitions ont zéro note pendante.
5. La timeline sample d'application et les deadlines sont monotones, testées aux frontières, avec tempo et clock externe bornée.
6. Le runtime NoteFx a un owner unique, les transitions sont idempotentes et les structures persistées ne contiennent aucun runtime.
7. Le quota demi-buffer et les mesures H743 Low-Cost/Premium sont documentés avec marge sous le seuil; l'indicateur IRQ audio confirme l'absence d'overrun.
8. Le terminal explicite les refus MIDI/moteur, garde les Off prioritaires et expose le seam post-FX futur.
9. Les fichiers/symboles à réinspecter sont au minimum `Inc/NoteFx/*`, `Src/NoteFx/*`, `Src/Seq/seq_play_scheduler.c`, `Src/Seq/seq_runtime_exec.c`, `Src/Audio/audio.c`, `Src/Seq/seq_output_guard.c`, `Src/MIDI/midi.c`, `Src/Param/param_registry_catalog.c`, `Src/Seq/seq_param_iface.c`, persistence/snapshots et tests.
10. Une recherche négative confirme l'absence du miroir pitch-only, du premier-ARP, des appels FX directs au terminal, des budgets par sous-segment et de tout runtime persisté.

11. Les validations produit démontrent que MUTE_TRIGS ne ferme ni ne purge, qu'UNMUTE ne retrigger rien, que les sources live restent distinctes et que chaque transition destructive est explicitement choisie.
12. Les quatre combinaisons d'admission interne/MIDI et les cas de déconnexion sont testés avec des ledgers indépendants.
13. Le retard clock externe est testé avec une petite borne mesurée, abandon/coalescence compté, recalage fondé uniquement sur les informations MIDI réellement reçues et aucune rafale.

Les constats Euclid rendus obsolètes seront au minimum: paire scheduler non atomique, premier ARP implicite, sample de fin de bloc, multi-écrivain runtime, budget recréé par sous-segment et politique de mute destructif. Les contraintes Euclid encore à valider après nettoyage seront sa capacité/fan-out H743, ses defaults/paramètres de modèle et ses règles produit propres. `docs/plan_midi_fx_euclid.md` doit rester inchangé jusqu'à ce réaudit.
