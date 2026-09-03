# BRICK — REGISTRE D’AUDIT UI / INPUT

## Statut

Ce document centralise les anomalies **déjà découvertes** dans le secteur UI / INPUT.

Dernière consolidation : **après ROUND 5**.

Les prochains audits doivent :

- lire ce document ;
- ne pas recompter les problèmes déjà connus ;
- chercher uniquement de nouvelles anomalies indépendantes ;
- mettre à jour ce document si de nouveaux problèmes sont démontrés.

---

# 1. Contrat produit BRICK — autorité

BRICK possède :

- **24 Hall switches**
- **16 STEP/TRIG physiques dédiés**

## STEP/TRIG

Ils servent :

- au séquenceur ;
- aux fonctions secondaires via `SHIFT + STEP`;
- à TRACK CHOOSE ;
- à MUTE ;
- notamment à sélectionner les modes Hall `KBD` et `SEQ`.

## Hall — mode KBD

Sans SHIFT :

```text
Hall 0..23
→ clavier chromatique normal
```

Avec SHIFT :

```text
SHIFT + Hall correspondante
→ fonctions secondaires
```

Fonctions historiques notamment :

- TONE
- ENV
- PLAY
- MOD
- MIX
- UNDO
- REDO
- SAVE
- REC CFG
- SETTINGS
- autres fonctions prévues.

## Hall — mode SEQ

Touches noires :

```text
Hall noir
→ fonction secondaire
```

sans SHIFT.

Touches blanches :

```text
Hall blanc
→ 7 notes de la gamme courante sur 2 octaves
```

Hall et STEP sont deux surfaces distinctes.

---

# 2. Régression STEP — mapping physique faux

Cause démontrée :

- `board_controls_button_remap_observed_id()` historique supprimé ;
- table board conservant encore un ordre observé/permuté ;
- UI recevait déjà le mauvais `BTN_STEP`.

Exemples observés :

- STEP1 → STEP2
- STEP2 → STEP3
- STEP3 → STEP4
- etc.

Correction ensuite annoncée :

> mapping STEP 1:1 restauré.

État actuel déclaré après patch :

`BRICK INPUT WORKFLOW RESTORED`

À rouvrir uniquement si nouveau test matériel contredit ce verdict.

---

# 3. SHIFT+STEP / TRACK CHOOSE / MUTE

Régression historique identifiée :

- routing contextual STEP aplati/perdu pendant refactors ;
- fonctions secondaires `SHIFT+STEP`, TRACK CHOOSE et MUTE devaient être restaurées.

État déclaré après patch :

- `SHIFT+STEP` restauré ;
- TRACK CHOOSE restauré ;
- MUTE restauré.

À rouvrir uniquement sur nouvelle preuve.

---

# 4. Hall→STEP Premium — ancienne régression

Une ancienne fuite avait créé :

```text
Hall
→ ui_core_seq_transport
→ CONTROL_SEQ_STEP_PRESS
→ seq_edit_step_press
```

Ce chemin a ensuite été supprimé.

État actuel déclaré :

> aucun Hall→STEP.

Ne pas recompter.

---

# 5. Hall noirs / shortcuts selon mode

Important : ce comportement est **voulu** selon le mode.

## KBD
- Hall noir seul = note ;
- SHIFT + Hall = fonction secondaire.

## SEQ
- Hall noir seul = fonction secondaire ;
- Hall blanc = note de gamme.

Ne pas classer `keyboard_input_trigger_black_shortcut()` comme legacy par principe.

Le bug n’existe que si le routing ne respecte pas le mode/SHIFT attendu.

---

# 6. TRANSPOSE — double consommation

Problème démontré :

- `BTN_TRANSPOSE_UP/DOWN` lu par plusieurs chemins ;
- premier lecteur consomme destructivement l’edge ;
- second reader ne voit plus l’événement ;
- peut être interprété dans mauvais contexte seq au lieu d’octave clavier.

Classification : `P1 INPUT ARCH LEAK`

---

# 7. Keyboard/Hall settings mutés directement depuis UI

UI appelle directement notamment :

- `keyboard_runtime_set_*`
- `hall_set_velocity_*`
- `hall_calibration_save`

au lieu de passer par l’owner CONTROL.

Classification : `P1`

---

# 8. Octave / transpose muté directement depuis UI

Callchain connue :

```text
UI
→ ui_hall_input_service_handle_transpose
→ keyboard_runtime_step_octave
→ ui_keyboard_app_set_octave_shift
```

Mutation directe hors CONTROL.

Classification : `P1`

---

# 9. Undo / Redo mutés directement depuis UI

Callchains UI/Keyboard vers :

- `undo_v2_undo`
- `undo_v2_redo`

sans sérialisation CONTROL.

Classification : `P1`

---

# 10. Multi cancel depuis Settings UI

UI appelle directement :

```text
multi_sample_cancel_load
```

et modifie le lifecycle loader.

Classification : `P1`

---

# 11. Audio Rec contourne CONTROL

UI Audio Rec appelle directement :

- SampleCapture ;
- Recorder ;
- Storage ;
- preview ;
- rec-bus ;
- save WAV ;
- sample load ;
- waveform/cache.

Classification : `P0/P1 — à traiter avec ownership global`

---

# 12. Polyphony / Audio FX mutés depuis UI

UI appelle directement :

- `polyphony_control_install_prepared`
- `audio_fx_control_state_install_prepared`
- `audio_fx_control_set_*`

alors que CONTROL possède déjà ces états.

Classification : `P1`

---

# 13. Encoder audio — publication depuis UI

Le chemin encoder conserve un vrai timestamp d’ingress, mais la publication finale peut partir depuis UI_SERVICE.

Doit être rerouté sans perdre la provenance temporelle.

Classification : `P1`

---

# 14. File UI — saturation silencieuse

Queue UI capacité 32.

Problème :

- `ui_event_push()` peut échouer ;
- certains callers ignorent le retour ;
- événements peuvent disparaître silencieusement.

Classification : `P1 / LOST INPUT`

---

# 15. Timestamps UI perdus

## Hall UI
Le timestamp TIM5/`ingress_serial` est perdu avant traitement UI.

Double taps peuvent ensuite être mesurés au dequeue via `HAL_GetTick()`.

Classification : `P1`

## Encoder UI
Le chemin non-AUDIO agrège le delta mais perd l’instant réel du detent.

Classification : `P1`

---

# 16. Hall 8..15 — anciennes limites à 8

Déjà identifiées :

## Patch Assign
Children GROUP 8..15 rejetés par une limite type `TRACK_ACTIVE_COUNT`.

## Looper Routing
Sources 8..15 rejetées par `TRACK_COUNT=8` alors que les masques adressent 16 entities.

Classification : `P1 / LOST INPUT`

---

# 17. Calibration pilotée depuis UI

UI pilote directement :

- start ;
- process ;
- save Flash.

Potential ownership issue avec ingress Hall.

Classification : `AMBIGUOUS`

Ne pas patcher sans preuve.

---

# 18. Saturation d’autres queues / drops

Round 3 a relevé des pertes possibles sur :

- Hall queue 64 ;
- encoder AUDIO 32 ;
- NoteFx 31 utilisables ;
- intents CONTROL ;
- fallback clipboard `SHIFT+COPY`.

Classification : `P1 / LOST INPUT`

---

# 19. Snapshot UI périmé pour arbitrage Hall / encoder

Round 4.

CONTROL/UI peuvent observer des états de mode/modifier différents.

Cas :

- `SHIFT/TRACK + Hall` peut partir au clavier au lieu de l’UI ;
- `SHIFT + encoder` peut utiliser un binding périmé.

Classification : `P1 — stale context`

---

# 20. Événements Hall sans contexte/epoch

Round 5.

Un Hall capturé sous mode KBD peut être traité après passage concurrent en mode SEQ.

Le live event ne transporte pas suffisamment le contexte logique de capture.

Classification : `P1 — STALE-CONTEXT`

---

# 21. Double-tap SHIFT+STEP mesuré au dequeue

Round 5.

Double-tap calculé avec temps de traitement UI (`HAL_GetTick`) au lieu du vrai instant d’entrée.

Conséquence :

- comportement dépendant du lag UI.

Classification : `P1 — STALE-CONTEXT`

---

# 22. `seq_edit held[]` non purgé à sortie de mode/page

Round 5.

Un STEP peut rester considéré held après sortie de SEQ si le release survient hors du contexte attendu.

Classification : `P1 — HELD-STATE`

---

# 23. SHIFT+REC dépend de l’ordre de drain UI

Round 5.

Si SHIFT est relâché avant traitement de REC :

- combo peut devenir REC normal ;
- l’ordre des événements ne représente pas forcément l’ordre logique de capture.

Classification : `P1 — STALE-CONTEXT`

---

# 24. Encoder traité avant certaines releases UI

Round 5.

Exemple :

- Hall/STEP relâché ;
- encoder proche ;
- UI traite encoder avant release ;
- ancienne macro/ancien STEP encore considéré actif.

Classification : `P1 — STALE-CONTEXT`

---

# 25. États partagés CONTROL/UI

Round 3 cross-domain :

```text
button_states
enc_accumulated_delta
```

partagés sans protocole propre.

Risques :

- lost update ;
- deltas écrasés ;
- reset concurrent.

Classification : `P1`

---

# 26. UI lit certains états Storage internes

Déjà trouvé :

- `g_progress`
- `g_present`
- `g_project_save`
- `g_sd_preview.gain`
- pointeurs internes pools sample/wavetable/multi/global.

Certains cas restent `AMBIGUOUS`, d’autres sont des leaks cross-domain confirmés.

Ne pas recompter sans nouvelle structure.

---

# 27. Zones explicitement jugées propres

À ne pas rouvrir sans preuve :

- mapping physique des boutons après restauration ;
- STEP/TRIG unicité ;
- Hall acquisition 24 touches ;
- track buttons ;
- page buttons ;
- PLAY/REC simples ;
- SHIFT/TRACK/BACK navigation modale ;
- mute/scenes/macros ;
- COPY/PASTE/CLEAR/p-locks ;
- seq edit/roll/euclid/PLAY hors problèmes connus ;
- track family/type ;
- project/pattern/patch intents ;
- browser async principal ;
- MIDI FX via CONTROL intents ;
- popups/navigation explicite hors ownership calibration ;
- aucune nouvelle limite 8/16/24 au Round 5 ;
- aucun nouveau legacy produit au Round 5.

---

# 28. Règles pour les prochains audits

Ne compter comme nouveau que :

- nouvelle structure ;
- nouveau combo ;
- nouvelle limite ;
- nouvelle queue ;
- nouveau stale-context ;
- nouvelle mutation métier directe ;
- nouveau comportement legacy.

Mettre à jour ce document à la fin du round.

---

# 29. Nouvelles découvertes à ajouter

```text
## ROUND N

### ID — classification
- Input / structure :
- Callchain :
- Cause :
- Comportement attendu :
- Comportement réel :
- Impact :
```

## ROUND 6

### R6-UI-ENC-01 — P1 / LOST INPUT
- Input / structure : encoder delta coalesce par `encoder_consume_delta()`, avec drain pouvant atteindre 4 detents par tick.
- Callchain : `encoder_consume_delta()` -> `ui_core_tick()` -> handlers encoder des pages Keyboard, CFG, REC CFG et SEQ.
- Cause : plusieurs handlers traitent uniquement le signe de `delta` et appliquent une seule transition, alors que la valeur retournee represente plusieurs detents consommes.
- Comportement attendu : chaque detent consomme doit produire la transition correspondante, ou rester dans un backlog equivalent.
- Comportement reel : un delta de magnitude 2 a 4 est consomme en une fois, mais une seule transition est appliquee.
- Impact : perte d'input et vitesse de navigation incoherente sur les enums/pages concernes. Occurrences representatives : `ui_page_template_keyboard.c`, `ui_page_template_cfg.c`, `ui_page_template_rec_cfg.c`, `ui_page_template_seq.c`.

### R6-UI-ENC-02 — P1 / FUNCTIONAL REGRESSION
- Input / structure : encoder 0 sur la page MACRO.
- Callchain : `encoder_consume_delta()` -> dispatch encodeurs de `ui_core.c` -> handlers template / macro interaction -> aucun appel a `ui_page_template_macro_handle_encoder()`.
- Cause : le handler de page existe et emet `CONTROL_MACRO_SET_HALL_MODE`, mais il n'est pas inclus dans la chaine de dispatch active.
- Comportement attendu : l'encoder 0 de la page MACRO bascule le mode Hall via l'intent macro.
- Comportement reel : la page MACRO ne consomme pas ce delta fonctionnellement ; l'interaction macro est reset a l'entree et n'est donc pas armee, puis le fallback parametre ne dispose d'aucun parametre.
- Impact : les detents de l'encoder 0 sur MACRO sont sans effet ; le changement Switch/Scene par encoder est indisponible.

### R6-UI-STALE-01 — P1 / STALE-CONTEXT
- Input / structure : SHIFT + PAGE dans Name Edit et certaines vues Settings.
- Callchain : event `UI_EVENT_BUTTON_PRESS` mis en queue -> handler de page au dequeue -> lecture directe de `button_down(BTN_SHIFT)`.
- Cause : le modifier SHIFT n'est pas capture avec l'event PAGE ; le handler relit l'etat physique courant, potentiellement apres le release de SHIFT.
- Comportement attendu : l'action depend de l'etat SHIFT au moment de la capture du PAGE press.
- Comportement reel : le meme PAGE press peut devenir shift ou non-shift selon l'etat au dequeue : cancel/commit/espace/backspace dans Name Edit, et apply/copy/refresh dans Settings.
- Impact : combos intermittents et actions produit incorrectes apres latence ou release rapide.

## ROUND 7

### R7-UI-DISPATCH-01 — NEW INSTANCE OF KNOWN RULE / DISPATCH INCOMPLET
- Input / structure : encodeurs des vues MOD MATRIX, MULTI et SLEW.
- Callchain : delta encoder -> boucle de dispatch de `ui_core_tick()` -> fallback `ui_param_handle_encoder_with_context()` ; `ui_page_template_mod_handle_event()` n'est jamais appele avec `UI_EVENT_ENCODER`.
- Cause : les banques de parametres de ces vues sont entierement `PARAM_COUNT`, tandis que le handler custom qui emet les intents MOD n'a aucun producteur d'evenement encoder actif.
- Comportement attendu : chaque detent modifie le slot/source/destination/depth ou le parametre MULTI/SLEW via l'intent CONTROL_MOD correspondant.
- Comportement reel : le delta est consomme puis aucun parametre n'est applique ; les detents sont sans effet.
- Impact : edition utilisateur indisponible sur MOD MATRIX, MULTI et SLEW.

### R7-UI-DISPATCH-02 — NEW INSTANCE OF KNOWN RULE / DISPATCH INCOMPLET
- Input / structure : encodeurs des vues Audio FX ROUTING et SPATIAL.
- Callchain : delta encoder -> boucle de dispatch de `ui_core_tick()` -> fallback parametre avec banque `PARAM_COUNT` ; `ui_page_midi_fx_handle_event()` n'est jamais appele avec `UI_EVENT_ENCODER`.
- Cause : la logique custom `audio_fx_control_set_*()` est attachee au handler d'evenement, mais `ui_event_from_inputs()` ne produit que des evenements bouton/Hall et `ui_core_tick()` traite les encodeurs par appels directs.
- Comportement attendu : les encodeurs changent position de filtre, ordre, modes spatiaux ou routage selon la vue.
- Comportement reel : aucun changement metier n'est produit par les detents sur ces vues.
- Impact : les reglages Audio FX ROUTING/SPATIAL sont inaccessibles par encodeur.

### R7-UI-DELTA-01 — NEW INSTANCE OF KNOWN RULE / STALE-CONTEXT
- Input / structure : `g_ui_settings.encoder_accum[]` dans Settings.
- Callchain : detents partiels -> `ui_page_settings_handle_encoder()` -> changement de vue par `ui_page_settings_push()` ou `ui_page_settings_back()` -> prochains detents reutilisant le meme accumulateur.
- Cause : l'accumulateur est remis a zero a l'entree de Settings, mais pas lors des transitions entre vues Settings.
- Comportement attendu : un delta partiel doit etre annule ou rester lie a la vue qui l'a capture.
- Comportement reel : 1 a 3 detents restants dans une vue peuvent completer un pas dans la vue suivante.
- Impact : selection ou valeur modifiee dans le mauvais menu apres une navigation Settings.

## ROUND 8

### R8-UI-DISPATCH-01 - NEW INDEPENDENT BUG / OWNERSHIP HALL SHIFT
- Input / structure : `SHIFT + Hall` en mode KBD, notamment les Hall blanches et les fonctions secondaires historiques (SAVE, routage FX, looper, etc.).
- Callchain : `ui_core_hall_ui_claim_mask()` -> snapshot d'arbitrage -> `hall_keyboard_bridge_process()` -> `keyboard_runtime_process_hall_timed()` -> `keyboard_input_shortcut_press()`.
- Cause : le claim UI ne couvre plus `shift_down` et l'ancien appel `ui_hall_mode_flow_handle_shift_hall_action()` a ete retire. Le nouveau dispatch des fonctions secondaires ne couvre que `SHIFT + STEP`.
- Comportement attendu : Hall et STEP restent deux surfaces distinctes ; en KBD, `SHIFT + Hall` doit declencher la fonction secondaire correspondante.
- Comportement reel : une Hall blanche sous SHIFT n'est pas mise en queue pour l'UI et devient une note chromatique ; les actions secondaires Hall non implementees dans `keyboard_input_trigger_black_shortcut()` deviennent inaccessibles.
- Impact : note incorrecte et regression fonctionnelle/accessibilite sur les combinaisons `SHIFT + Hall`, independamment du stale-context deja connu.

### R8-UI-STALE-01 - NEW INSTANCE OF KNOWN RULE / STALE-CONTEXT
- Input / structure : release STEP apres un press SEQ, avec activation de SHIFT ou TRACK entre les deux.
- Callchain : press -> `ui_core_seq_transport_handle_seq_mode_event()` -> `CONTROL_SEQ_STEP_PRESS` -> `seq_edit_step_press()` ; release -> `ui_core_handle_step_context_event()`.
- Cause : le nouveau stage contextuel consomme tout release STEP lorsque `shift_down` ou `track_select_armed` est actif au dequeue, sans tenir compte du contexte du press.
- Comportement attendu : le release doit parvenir a l'owner qui a accepte le press, ou purger explicitement son etat held.
- Comportement reel : `CONTROL_SEQ_STEP_RELEASE` n'est pas emis ; `g_seq_hold_state.pending[]`/`held[]` peut rester actif jusqu'a une purge ulterieure.
- Impact : etat SEQ stale et short-action/hold-action incoherent lors d'une transition de modifier ; instance du probleme held-state/stale-context deja repertorie.
