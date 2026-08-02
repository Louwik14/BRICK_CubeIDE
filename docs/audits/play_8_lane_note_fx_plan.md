# Audit et plan d’implémentation — PLAY séquenceur permanent 8 lanes

## 1. Verdict

La cible est compatible avec l’architecture actuelle, mais elle ne se limite pas à ajouter quatre boutons. Le code possède aujourd’hui quatre groupes de paramètres PLAY, quatre sous-pages dynamiques, un scheduler à quatre voix et un pipeline NoteFx qui transporte des événements unitaires. Les lanes sont donc actuellement confondues avec la capacité de restitution du moteur.

L’implémentation recommandée conserve le modèle compact actuel : chaque lane est représentée par quatre paramètres PLAY et ses p-locks, sans ajouter un tableau de notes parallèle. Cela permet de conserver 64 steps, 32 p-locks PLAY maximum par step et les pools existants. La séparation à réaliser est :

```text
séquence 8 lanes permanente -> groupe d’événements (lane/source)
                            -> NoteFx
                            -> admission de destination
                            -> restitution locale ou MIDI/External
```

Le risque principal est l’identité des notes simultanées : deux lanes peuvent produire la même hauteur, et l’implémentation actuelle ne possède qu’un token `[track][note]`. Le second risque est l’utilisation de `VOICES` pour décider si une lane est stockable, éditable ou persistable. Ces deux responsabilités doivent être découplées avant d’exposer les lanes 5 à 8.

Ce document est un audit et un plan. Aucun firmware n’est modifié par cette mission.

## 2. Contrat produit proposé

### 2.1 Séquence et interface

- Chaque Play Track possède toujours huit lanes PLAY indépendantes.
- `PLAY 1/2` affiche les lanes 1 à 4.
- `PLAY 2/2` affiche les lanes 5 à 8.
- Les deux pages sont toujours accessibles sur un Play Track, quel que soit le moteur, le réglage `VOICES` ou la disponibilité momentanée du pool DSP.
- Les Specials ne reçoivent ni notes PLAY ni MIDI FX.
- Chaque lane conserve `NOTE`, `VEL`, `LEN` et `MICTIM`.
- Un step reste limité à 32 p-locks PLAY : huit lanes × quatre paramètres.
- La page de séquence et la page PLAY sont deux notions distinctes : la première reste liée aux pages de longueur du pattern, la seconde devient une projection UI permanente des huit lanes.

### 2.2 `VOICES` et changements de moteur

`VOICES` décrit la capacité de restitution locale, jamais la capacité de la séquence. Une modification de `VOICES`, de moteur ou de destination ne modifie donc pas le Pattern, ses lanes, ses p-locks, le presse-papier ou l’undo.

- Une baisse de capacité limite les admissions futures et libère/termine les notes actives selon le chemin de transition déjà existant.
- Une hausse de capacité permet de nouvelles admissions, sans réécriture des steps.
- Un changement vers MIDI/External rebinde la destination sans filtrer les lanes stockées.
- Si le pool global est momentanément insuffisant, les événements sont admis dans l’ordre déterministe des lanes jusqu’à la capacité disponible. Les suivants sont refusés sans token et sans Note Off ultérieur. Aucun vol de voix aléatoire ne doit être introduit dans cette admission ; le stealing interne éventuel du moteur reste une responsabilité séparée.

### 2.3 Routage final

Sans FX séquentiel, toutes les lanes sont soumises au point d’entrée commun puis la destination applique sa capacité. En mono local, la règle produit proposée est lane 1 prioritaire ; les lanes 2 à 8 restent séquencées et observables, mais ne sont pas rendues simultanément. En polyphonie locale, les lanes 1 à `N` sont admises, avec `N` égal à la capacité effective de la destination.

Avec ARP, les huit sources peuvent entrer dans le groupe ARP. L’ARP les sérialise ensuite selon son contrat et sa capacité de sortie, y compris lorsque la destination finale est mono. Une entrée MIDI ou External ne doit pas être réduite par le pool de voix du synthé local ; elle suit la capacité de sa propre destination.

## 3. Cartographie de l’existant

| Couche | État actuel | Autorité / point d’entrée |
|---|---|---|
| UI PLAY | Quatre sous-pages `V1` à `V4`, masquées par la capacité runtime | `Src/UI/pages/ui_page_template_play.c`, `ui_page_template_play_subpage_enabled()` |
| Paramètres | 16 paramètres PLAY : quatre paramètres pour chacune des voix 1 à 4 | `Inc/Param/param_store.h`, `Src/Param/param_registry_catalog.c` |
| Mapping compact | 16 slots PLAY, offsets compacts et état runtime par slot | `Src/Seq/seq_param_iface.c`, `Inc/Seq/seq_types.h` |
| Modèle séquence | 64 steps, chaque step porte une liste compacte de p-locks ; aucune note-array dédiée | `Inc/Seq/seq_model.h`, `Src/Seq/seq_model.c` |
| Pool Play | 8 pools de 1024 entrées ; 32 p-locks PLAY maximum par step | `seq_runtime_project_data_t`, `SEQ_PLAY_STEP_MAX_LOCKS` |
| Validation | Un slot PLAY devient non supporté si sa voix dépasse `track_runtime_get_play_voice_count()` | `Src/Core/track_runtime.c`, `seq_param_iface_slot_is_supported()` |
| Scheduler | `SEQ_PLAY_SCHEDULER_VOICE_COUNT == 4`, contexte et identifiants V1–V4 | `Src/Seq/seq_play_scheduler.c` |
| NoteFx | Événement unitaire sans lane, source, groupe ou multiplicité de notes | `Inc/NoteFx/note_fx_engine.h`, `note_fx_pipeline_submit()` |
| ARP | Jusqu’à 16 sources, mais déduplication par hauteur et association Note Off par note | `Src/NoteFx/note_fx_arp.c` |
| Terminal | MIDI puis moteur local ; aucune admission générique post-NoteFx | `seq_play_scheduler_dispatch_terminal_note_to_channel()` |
| Tokens | Un token actif par couple track/note dans le scheduler | `g_seq_play_active_event_token` |
| Protection sortie | Compteurs de notes, indépendants du token scalaire | `Src/Seq/seq_output_guard.c` |
| Live record | Quatre voix candidates, pending et association par hauteur/source/canal | `Src/Seq/seq_live_rec_capture.c` |
| Pattern/Project | Format courant v5, p-locks compacts, pas de migration historique | `Src/Storage/pattern_live_ram.c`, `Inc/Storage/project_v1.h` |
| Clipboard/undo | Déjà compacts et dimensionnés par `PARAM_COUNT`/p-locks, mais filtrent par support runtime | `Src/Seq/seq_clipboard.c`, `Src/Storage/undo_v2.c` |

### 3.1 Ce que signifie actuellement « quatre voix »

Le modèle ne contient pas un tableau de quatre notes par step. Les quatre lanes sont quatre groupes de paramètres PLAY (`NOTE`, `VEL`, `LEN`, `MICTIM`) et leurs p-locks dans le pool compact. Il faut donc étendre le mapping et les paramètres, pas créer une seconde représentation qui divergerait lors des copies, snapshots et sauvegardes.

Le code courant mélange cependant quatre sujets :

1. la validité d’un paramètre dans le format de séquence ;
2. la présence du paramètre dans l’UI ;
3. la capacité du moteur à restituer la note ;
4. la capacité effective du pool global.

La première étape doit introduire une requête de validité séquence indépendante de la capacité de lecture. `VOICES` pourra ensuite continuer à produire une capacité de destination sans invalider un p-lock permanent.

### 3.2 Invariants existants à préserver

- Un Play Track possède le scheduler, les notes, le clavier, le MIDI FX, le live record, le mute, les p-locks, les snapshots et la persistance.
- Un Special ne possède pas de notes PLAY ni de MIDI FX.
- La réservation atomique Note On + Note Off du scheduler est la règle de sûreté `Z4-002` et doit rester indivisible.
- Le pipeline clavier et le pipeline scheduler convergent déjà vers NoteFx.
- Les formats actuels sont compacts et stricts ; aucune migration historique n’est demandée dans cette mission.

## 4. Contrat d’événement de groupe

### 4.1 Limite du contrat actuel

`note_fx_pipeline_submit(track, note, velocity, is_note_on, sample_time)` ne porte qu’un événement isolé. Deux appels au même sample peuvent former implicitement un accord pour l’ARP si le traitement n’a pas encore commencé, mais il n’existe ni frontière de groupe, ni lane source, ni ordre explicite.

L’ARP déduplique les sources de même hauteur et cherche les sorties à fermer par `source_note`. Cette sémantique perd l’identité lorsque deux lanes jouent la même note, ou lorsqu’une lane est répétée avant la fin de la note précédente.

### 4.2 Extension minimale recommandée

Ajouter un petit contexte borné, transmis avec chaque événement :

- `group_id` ou identifiant de tranche de step ;
- `lane_id` sur 0..7 ;
- ordre stable de lane dans le groupe ;
- origine (`SEQUENCER`, `KEYBOARD`, `LIVE_REC`, éventuellement `NOTE_FX`) ;
- identifiant de source pour apparier Note On et Note Off ;
- destination/canal déjà résolus lorsque le contrat l’exige.

Le scheduler doit pouvoir soumettre un batch borné avec une frontière explicite (`begin/count/end`, ou une API équivalente). Il n’est pas nécessaire de construire une infrastructure générale de graphes : le groupe représente seulement les notes issues d’un même step/événement logique.

L’ARP doit conserver cette identité au lieu de dédupliquer uniquement par hauteur. Ses sorties doivent posséder leur propre identité de sortie et fermer exactement la note qu’elles ont ouverte. Les capacités actuelles de 16 sources et 16 sorties peuvent rester bornées, sous réserve d’un test couvrant huit sources identiques.

### 4.3 Effet des familles FX

- ARP/Hold doit consommer le groupe complet avant d’appliquer son séquençage.
- Un futur Harmony/Voicing devra recevoir le groupe et produire des enfants référencés par le parent ; il ne doit pas réinventer l’identité à partir de la hauteur seule.
- Probability, Gate, Groove et transformations note-wise doivent préserver `group_id`, `lane_id` et l’identité source lorsqu’ils laissent passer un événement.
- Aucun nouveau modèle MIDI FX n’est dans le périmètre de ce plan.

## 5. Admission terminale et tokens

L’admission doit être placée après NoteFx et avant l’appel au moteur local ou à la sortie MIDI. Une note refusée ne réserve aucun token et ne génère aucun Note Off. Une note admise réserve son couple Note On/Note Off de façon atomique, comme dans le scheduler actuel.

| Destination | Capacité de référence | Sans FX séquentiel | Avec ARP | Token / Note Off |
|---|---:|---|---|---|
| Synth mono | 1 | Lane 1 prioritaire | ARP sérialise | Uniquement pour les sorties admises, identité source complète |
| Synth local poly | `N` effectif, 1..8, éventuellement réduit par le pool global | Lanes 1..`N` | ARP puis admission simultanée | Réservation atomique par sortie ; dépassement refusé sans orphan |
| Sampler/Drum/autre local | Contrat propre au moteur ; ne pas déduire automatiquement du synth `VOICES` | Selon le contrat, projection mono si moteur mono | ARP ou contrat propre | Pair exact par identité de sortie |
| MIDI | 8 lanes, sous réserve du contrat explicite du périphérique/canal | Les 8 | Toutes les sorties ARP admissibles | Identité lane/source conservée jusqu’au Note Off |
| External | 8 sorties MIDI ; gate local séparé de la capacité synth | Les 8 côté MIDI | Toutes les sorties ARP admissibles | Pas de plafonnement par le pool synth local |

Le mono doit avoir une règle déclarée et testable. Le scheduler ne doit plus allouer d’abord un token pour ensuite découvrir que le moteur ne peut pas accepter la note. Le token actuel indexé par `[track][note]` doit devenir une table bornée d’événements actifs, un slot par note admise, ou une structure équivalente capable de représenter plusieurs occurrences de même hauteur. Les compteurs de `seq_output_guard` peuvent rester count-based pour le panic, mais ils ne remplacent pas cette identité.

## 6. Modifications de données et d’exécution

### 6.1 Paramètres et mapping

Ajouter les 16 paramètres permanents `V5..V8_NOTE/VEL/LEN/MICTIM` dans le bloc PLAY, leurs descripteurs et leur mapping compact. Le nombre de slots PLAY passe de 16 à 32. Les offsets ENV/TONE/PLAY/MOD/MIDI_FX/MIX et les assertions associées doivent être recalculés de manière centralisée.

La validation séquence doit accepter les 32 slots sur un Play Track même lorsque le moteur est mono. La validation de restitution reste une seconde requête, capable de dire « lane stockée mais actuellement non admise ».

Ne pas réutiliser les IDs réservés existants. Le compteur de paramètres persistants, les tables de registry, les tables de paramètre-vers-slot et les statuts runtime doivent être mis à jour ensemble.

### 6.2 Modèle et scheduler

Étendre le contexte du scheduler à huit items et ses constantes/identifiants. Garder `seq_step_t`, les pools de 1024 entrées et la limite de 32 p-locks tant que le mapping compact est conservé.

Le nombre maximal d’événements produits par un step croît linéairement :

```text
2 × 8 × (1 + nombre de retriggers par lane)
```

La capacité de queue actuelle de 512 doit être auditée avec les retriggers et les événements MIDI éventuels. Si elle est insuffisante, augmenter explicitement la capacité ou définir une admission bornée ; ne jamais tronquer un Note On sans supprimer atomiquement son Note Off associé.

Le scheduler doit porter lane, groupe et source jusqu’à NoteFx. Les sorties ARP doivent ensuite être traitées comme de nouvelles identités de sortie, avec une relation vers leur source.

### 6.3 Live record et clavier

Le live record doit passer de quatre lanes à huit candidates, écrire les quatre paramètres de la lane sélectionnée et conserver une association pending par source identifiée, non par hauteur seule. Deux notes identiques simultanées doivent être enregistrables et fermables séparément.

Le clavier reste sur le pipeline NoteFx commun. La sélection d’une lane de capture et l’identité d’une note enregistrée ne doivent pas dépendre de la capacité `VOICES`.

### 6.4 Pattern, Project, snapshot, clipboard et undo

Le format courant doit être rendu cohérent avec les 16 nouveaux paramètres :

- étendre les tableaux persistants et les validations de `PatternSaveV1` ;
- mettre à jour la taille stricte du payload et le header/version de Project ;
- conserver les 64 steps et les 32 p-locks PLAY par step ;
- mettre à jour les snapshots de track pour capturer/restaurer les valeurs PLAY de base ainsi que les p-locks ;
- laisser le clipboard paramètre dimensionné par `PARAM_COUNT`, mais supprimer le filtrage runtime des lanes valides ;
- laisser les deltas undo compacts par set/slot tout en reconnaissant les slots PLAY 16..31.

Le format actuel est v5 et ne possède pas de politique de migration historique. Le plan recommande donc une évolution du format courant et une invalidation stricte des anciens payloads si nécessaire, sans ajouter de convertisseur ancien.

### 6.5 UI

Le template générique actuel ne fournit que quatre labels et quatre subpages. Il faut ajouter une projection PLAY à deux pages permanentes, sans créer deux ensembles de paramètres ou deux scopes d’undo.

Les scopes existants doivent rester cohérents :

- copie de page : les quatre paramètres de la lane affichée ;
- copie de track : les huit lanes et leurs p-locks ;
- copie paramètre : les IDs PLAY correspondants ;
- clipboard séquence : les p-locks du step, sans dépendance à `VOICES`.

### 6.6 Estimation mémoire

Les valeurs suivantes sont des deltas de planification, à confirmer par `sizeof` pendant l’implémentation :

| Objet | Delta estimé |
|---|---:|
| Slots compacts PLAY | +16 slots ; pool de p-locks inchangé |
| Deux bitmaps runtime `14 × slots` | +28 octets par bitmap, soit +56 octets |
| `g_seq_param_runtime_state` | environ +896 octets en D2 |
| Une valeur + validité persistante | environ +1 120 octets par `PatternSaveV1` |
| Cinq buffers `PatternSaveV1` en SDRAM | environ +5 600 octets |
| Deux snapshots Pattern par payload undo | environ +2 240 octets par snapshot |
| Undo Premium, quatre snapshots | environ +8 960 octets |
| Undo LowCost, un snapshot | environ +2 240 octets |
| Clipboard paramètre | environ +192 octets au total pour deux scopes |

L’empreinte exacte dépend de l’insertion des IDs, de l’alignement et des structures qui embarquent `PatternSaveV1`. Aucun accroissement du pool de voix DSP n’est requis pour stocker huit lanes.

## 7. Plan atomique par étapes

Chaque étape ci-dessous doit être traitée comme une PR ou un commit isolé. Le passage à l’étape suivante se fait uniquement après les tests de l’étape et le `Go étape X` explicite du responsable.

### Étape 1 — Canonicaliser huit lanes et deux pages PLAY

**Objectif.** Rendre V1..V8 permanentes dans le paramétrage, le mapping et l’UI.

**Fichiers probables.** `Inc/Param/param_store.h`, `Src/Param/param_registry_catalog.c`, `Inc/Seq/seq_types.h`, `Src/Seq/seq_param_iface.c`, `Inc/UI/ui_template_page.h`, `Src/UI/pages/ui_page_template_play.c`, `Src/Core/track_runtime.c` et les renderer/UI mappings correspondants.

**Autorisé.** Ajouter les IDs/descripteurs V5..V8, étendre les slots, introduire la distinction validité séquence/capacité runtime, exposer PLAY 1/2 et PLAY 2/2.

**Interdit.** Modifier le scheduler, NoteFx, les moteurs, les Specials ou les valeurs de `VOICES`.

**Tests.** Registry/mapping 32 slots, deux pages visibles sur mono et poly, Special sans PLAY, p-locks V5..V8 acceptés indépendamment de `VOICES`.

### Étape 2 — Persistance, clipboard, undo et live record

**Objectif.** Rendre les huit lanes éditables, copiables, undoables et enregistrables.

**Fichiers probables.** `Src/Seq/seq_live_rec_capture.c`, `Inc/Seq/seq_live_rec_capture.h`, `Inc/Storage/pattern_live_ram.h`, `Src/Storage/pattern_live_ram.c`, `Inc/Storage/project_v1.h`, `Src/Seq/seq_clipboard.c`, `Src/UI/ui_core_clipboard.c`, `Inc/Storage/undo_v2.h`, `Src/Storage/undo_v2.c`, snapshots track et tests associés.

**Autorisé.** Étendre les payloads et la version courante, les scopes de copie, les pending live record et les associations par lane/source.

**Interdit.** Changer le routage terminal, l’ARP ou la capacité DSP.

**Tests.** Round-trip Pattern/Project, snapshot/clipboard/undo, p-locks 0..31, capture de huit lanes et deux notes identiques simultanées.

### Étape 3 — Transport groupé de huit notes vers NoteFx

**Objectif.** Faire sortir les huit lanes du scheduler avec un contexte de groupe et une identité de source.

**Fichiers probables.** `Src/Seq/seq_play_scheduler.c`, `Inc/NoteFx/note_fx_pipeline.h`, `Src/NoteFx/note_fx_pipeline.c`, `Inc/NoteFx/note_fx_engine.h`, `Src/NoteFx/note_fx_engine.c`.

**Autorisé.** API batch bornée, `group_id`, `lane_id`, source et tokens source, extension des contextes de quatre à huit items.

**Interdit.** Modifier la politique mono/poly terminale ou ajouter des modèles FX.

**Tests.** Huit notes au même sample, p-locks et retriggers, clavier et scheduler sur le même pipeline, groupe vide/complet, génération et suspend.

### Étape 4 — Admission terminale et identité des tokens

**Objectif.** Décider l’acceptation après NoteFx et supprimer les orphan Note Off.

**Fichiers probables.** `Src/Seq/seq_play_scheduler.c`, `Src/Seq/seq_output_guard.c`, `Src/NoteFx/note_fx_pipeline.c`, `Src/NoteFx/note_fx_engine.c`, `Inc/Core/synth_polyphony.h` et les interfaces de destination.

**Autorisé.** Table bornée d’événements actifs, admission par destination, réservation atomique des couples, nettoyage par génération/mute/panic.

**Interdit.** Augmenter le pool DSP, modifier l’algorithme de polyphonie ou introduire du stealing dans l’admission.

**Tests.** Notes identiques sur huit lanes, overflow pair, pool global insuffisant, Note Off tardif, mute/panic, génération invalide, vérification de `Z4-002`.

### Étape 5 — ARP et FX séquentiels group-aware

**Objectif.** Faire respecter l’identité des sources et sorties lorsque ARP/Hold séquentialise un groupe.

**Fichiers probables.** `Inc/NoteFx/note_fx_arp.h`, `Src/NoteFx/note_fx_arp.c`, `Src/NoteFx/note_fx_engine.c` et tests NoteFx.

**Autorisé.** Identifiants de source/sortie, association exacte Note On/Off, conservation de l’ordre lane, traitement des huit sources et des doublons.

**Interdit.** Reconcevoir l’ARP, ajouter Harmony/Voicing ou modifier le stockage.

**Tests.** ARP Hold, ordre up/down/updown/random, huit sources identiques, retrigger, arrêt/reset, budget de huit émissions par bloc et cleanup.

### Étape 6 — Politiques mono/poly, moteurs et destinations

**Objectif.** Appliquer le contrat final selon la destination et les transitions.

**Fichiers probables.** `Src/Core/track_runtime.c`, `Src/Seq/seq_play_scheduler.c`, `Src/Seq/seq_output_guard.c`, `Src/Audio/*` concernés et sorties MIDI/External.

**Autorisé.** Règle lane 1 en mono, admission 1..N en local poly, routage MIDI/External, libération sûre lors des changements de moteur/VOICES.

**Interdit.** Déplacer la capacité dans la séquence, augmenter `SYNTH_POLYPHONY_MAX_VOICES`, ajouter des tracks ou modifier les Specials.

**Tests.** Mono, poly 2/4/8, pool global limité, Sampler/Drum/Multi, MIDI, External, changement de moteur/VOICES pendant note active, Pattern inchangé.

### Étape 7 — Validation et livraison

**Objectif.** Fermer les invariants, mesurer la mémoire et documenter le résultat.

**Fichiers probables.** Tests dédiés, docs `z0` à `z6` touchées par les contrats, notes de release si le projet en possède une.

**Autorisé.** Tests statiques/dynamiques, assertions de taille, builds Release LowCost et Premium.

**Interdit.** Introduire une migration historique, modifier TestPremium ou élargir le périmètre de produit.

**Tests.** Suite minimale de la section suivante, build Release LowCost et Premium, inspection du diff et de l’empreinte RAM/flash.

## 8. Validation minimale avant intégration

- Paramètres V1..V8 présents dans le registry, le mapping et les p-locks.
- PLAY 1/2 et PLAY 2/2 visibles sur mono, poly, MIDI et External.
- Specials toujours dépourvus de PLAY/MIDI FX.
- Un Pattern contenant huit lanes survit à un changement de `VOICES` et de moteur.
- Pattern/Project, snapshot, clipboard et undo restaurent les valeurs et p-locks des lanes 1..8.
- Live record capture huit lanes et apparie deux occurrences de même hauteur.
- Un step de huit notes atteint NoteFx avec un groupe et huit identités de source.
- Sans FX : mono lane 1, poly lanes 1..N, MIDI/External huit sorties selon leur contrat.
- Avec ARP/Hold : toutes les sources admissibles entrent dans ARP, les sorties ferment exactement les notes qu’elles ont ouvertes.
- Les notes identiques simultanées ne se volent pas leurs tokens.
- Un dépassement de queue ou de pool ne produit jamais un Note On sans son Note Off correspondant, ni un Note Off pour une note refusée.
- Mute, panic, suspend, changement de génération et changement de destination nettoient toutes les identités actives.
- Les retriggers restent bornés et l’émission Note On/Off reste atomique.
- Builds Release LowCost et Premium verts ; aucun build TestPremium requis pour cette mission.

## 9. Hors périmètre

- Ajout de nouveaux modèles MIDI FX ou refonte générale de l’ARP.
- Augmentation du nombre de voix DSP, du nombre de tracks ou du nombre de steps.
- Optimisation CPU non nécessaire au contrat huit lanes.
- Modification du dual-core, des Specials ou du stockage de périphériques externe.
- Migration des anciens formats et conservation d’une histoire de compatibilité.
- Modification des catalogues globaux/historiques de paramètres hors des entrées strictement nécessaires.
- Build ou modification de TestPremium pendant l’audit.

## 10. Références de code et documentation

Sources primaires consultées :

- `Inc/Seq/seq_types.h`, `Inc/Seq/seq_model.h`, `Src/Seq/seq_model.c` ;
- `Src/Seq/seq_param_iface.c`, `Src/Seq/seq_play_scheduler.c`, `Src/Seq/seq_live_rec_capture.c`, `Src/Seq/seq_clipboard.c` ;
- `Src/UI/pages/ui_page_template_play.c`, `Inc/UI/ui_template_page.h`, `Src/UI/ui_template_page.c`, `Src/UI/ui_core_clipboard.c` ;
- `Inc/Param/param_store.h`, `Src/Param/param_registry_catalog.c` ;
- `Src/Core/track_runtime.c`, `Inc/Core/synth_polyphony.h` ;
- `Inc/NoteFx/note_fx_pipeline.h`, `Inc/NoteFx/note_fx_engine.h`, `Src/NoteFx/note_fx_pipeline.c`, `Src/NoteFx/note_fx_engine.c`, `Inc/NoteFx/note_fx_arp.h`, `Src/NoteFx/note_fx_arp.c` ;
- `Src/Seq/seq_output_guard.c`, `Src/Keyboard/keyboard_engine.c` ;
- `Inc/Storage/pattern_live_ram.h`, `Src/Storage/pattern_live_ram.c`, `Inc/Storage/project_v1.h`, `Inc/Storage/undo_v2.h`, `Src/Storage/undo_v2.c` ;
- `docs/architecture/z0_plateforme_cadence.md` à `docs/architecture/z6_state_persistence_patterns_projects.md`.

Les contrats de plateforme, audio temps réel, runtime des tracks, scheduler, UI et persistance devront être amendés uniquement au moment où une étape change leur comportement effectif. Ce rapport est la seule documentation ajoutée par l’audit.
