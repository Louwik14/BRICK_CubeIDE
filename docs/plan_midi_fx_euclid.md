# Plan d'audit et d'action — MIDI FX EUCLID

## 1. Verdict

Le dépôt possède déjà une base MIDI FX exploitable, mais elle n'est pas encore une chaîne de quatre slots. Elle est actuellement un pipeline d'événements unitaires qui recherche au plus un slot `ARP`, retient ses notes dans une instance ARP, puis envoie directement les sorties vers le terminal commun.

`EUCLID` est compatible avec la cible produit à condition d'étendre le contrat du pipeline avant d'ajouter son algorithme. Le point bloquant n'est pas le calcul du masque : ce sont l'ordre réel des slots, l'identité des occurrences répétées et la production différée de Note On/Off sans chemin direct vers les moteurs.

Architecture retenue :

```text
source clavier / MIDI / STEP
        -> entrée NoteFx
        -> slot 1 -> slot 2 -> slot 3 -> slot 4
        -> sortie post-FX commune
        -> dispatcher MIDI + moteur local
```

Chaque slot conserve un état fixe. Un FX génératif publie des événements dans le même contrat que les événements entrants ; les événements différés reprennent la chaîne au slot suivant. Le terminal reste unique et devient le futur point de capture post-FX.

L'implémentation recommandée pour cette première version est : `LENGTH` de 1 à 64, `PULSE` de 0 à `LENGTH`, masque déterministe pré-calculé, notes strictement actives, durée d'une occurrence égale à une position `DIV`, et paramètres Euclid non p-lockables tant que les règles de changement de cycle ne sont pas stabilisées. Une seule instance `EUCLID` est autorisée par piste ; l'instance `ARP` existante reste unique. Cet encadrement permet `ARP -> EUCLID` et `EUCLID -> ARP` sans file ni fan-out non borné.

L'audit est statique. Aucun code fonctionnel, aucune structure persistée et aucun build n'est modifié par cette passe.

## 2. Décisions produit figées

Les décisions suivantes sont suffisamment étayées par le code actuel pour être utilisées directement par l'agent d'implémentation.

| Sujet | Décision v1 | Justification / autorité |
|---|---|---|
| Disponibilité | Seulement sur les pistes autorisées par `TRACK_CAPABILITY_MIDI_FX` et `TRACK_CAPABILITY_NOTES` | `Inc/Core/track_topology.h:52-59`, `Src/Core/track_topology.c:5-16`, `Src/Core/track_runtime.c:1466-1485` |
| Modèle | `OFF = 0`, `ARP = 1`, `EUCLID = 2` ; un seul EUCLID par piste | L'ID est une extension append-only de `note_fx_model_t`; l'ARP est déjà rendu unique par `note_fx_state.c:65-78` |
| Paramètres | Slot Euclid : `LENGTH | PULSE | DIV | MODEL` | La banque fixe de quatre paramètres est dans `Src/UI/pages/ui_page_midi_fx.c:9-28` et `NOTE_FX_PARAM_COUNT` vaut 4 (`Inc/NoteFx/note_fx_state.h:8-10`) |
| LENGTH | Entier 1..64, défaut 16 | 64 est la longueur maximale du pattern (`SEQ_MAX_STEPS`) et un masque de 64 bits ne coûte que 8 octets ; le domaine existant est déjà entier/enum |
| PULSE | Entier 0..LENGTH, défaut 4 | `0` fournit explicitement un cycle silencieux ; l'invariant est vérifiable à chaque entrée, restauration et override |
| Correction | Si `LENGTH` devient inférieur à `PULSE`, appliquer `PULSE = min(PULSE, LENGTH)` et enregistrer la valeur effectivement appliquée | Aucun setter MIDI FX actuel ne garantit une relation entre deux paramètres (`Src/NoteFx/note_fx_state.c:15-22`); l'implémentation doit fermer cette lacune |
| DIV | Domaine musical partagé avec les divisions existantes, conversion en période Q16 au runtime | Les divisions ne doivent pas être redéfinies dans Euclid ; le code actuel duplique déjà une table ARP locale (`Src/NoteFx/note_fx_engine.c:100-106`) et des labels séquence (`Src/Param/param_registry_catalog.c:82`, `:203`) |
| Note courte | Une note n'est rejouée que tant que son Note On n'a pas reçu de Note Off | C'est la seule sémantique qui ne transforme pas silencieusement un jeu live en latch et elle correspond à l'ARP actuel (`Src/NoteFx/note_fx_engine.c:75-87`) |
| Accord | Toutes les notes actives de l'instance sont rejouées ensemble sur un pulse, dans l'ordre stable d'entrée | L'ARP accepte déjà au plus 16 sources (`Inc/NoteFx/note_fx_arp.h:6-20`); Euclid reprend cette borne sans déduplication par occurrence |
| Note Off généré | Off à `NoteOn + période(DIV)`, avec Off avant On au même sample | La durée d'entrée ne suffit pas pour un FX qui retrigger une note tenue ; la paire doit être autonome et différée |
| Phase | Position 0 au premier matériau actif après `PLAY`; incrément une fois par position ; reset sur STOP, clear de pattern, changement de modèle, mute/panic | Le runtime séquence possède une timeline partagée et un nettoyage transport (`Src/Seq/seq_runtime_exec.c:407-430`, `Src/Seq/seq_play_scheduler.c:809-822`) |
| Mute | Suspendre, fermer les occurrences et vider les notes Euclid ; le cycle ne progresse pas sous mute | Aligné sur le raccord existant `track_mute -> note_fx_pipeline_suspend_track` validé par `tests/note_fx_pipeline_validation.ps1` |
| P-lock | `MODEL` reste p-lockable selon le contrat existant ; `LENGTH`, `PULSE`, `DIV` EUCLID sont refusés en p-lock dans v1 | Le domaine MIDI FX rend actuellement tous les paramètres p-lockables (`Src/Seq/seq_param_iface.c:436-462`), mais aucun contrat de phase/motif n'existe encore |
| Format | Pas de nouveau chemin de stockage ; les 4 octets de slot, le snapshot et `PatternSaveV1` restent l'autorité | `PatternSaveV1` contient déjà `note_fx[8]` (`Inc/Storage/pattern_live_ram.h:92-100`) ; l'ajout d'un enum ne change pas la forme |
| Live Record futur | Aucun changement dans cette passe ; le terminal commun est le point de capture à documenter | Le terminal actuel est `note_fx_pipeline_terminal` (`Src/NoteFx/note_fx_pipeline.c:15-25`) |

Le choix `PULSE = 0` est intentionnel : il est utile pour désactiver le rendu sans changer de modèle et évite d'introduire une convention cachée `PULSE >= 1`. Il ne doit produire ni Note On ni Note Off, mais les notes entrantes restent suivies tant que le slot est actif.

## 3. Cartographie de l'existant

### 3.1 Paramètres, modèle et autorité

- Les quatre slots sont représentés par 16 paramètres contigus `PARAM_MIDI_FX_S1_*` à `PARAM_MIDI_FX_S4_*` (`Inc/Param/param_store.h:336-351`), avec un `value[4][4]` par piste (`Inc/NoteFx/note_fx_state.h:20-23`).
- Le mapping paramètre -> slot/paramètre est arithmétique (`Src/NoteFx/note_fx_state.c:38-49`). Toute extension doit préserver cet ordre, les 16 positions de p-lock `SEQ_PLOCK_SET_MIDI_FX` (`Src/Seq/seq_param_iface.c:99-115`, `:184-213`) et l'assertion `SEQ_PARAM_MIDI_FX_SLOT_COUNT == 16` (`Inc/Seq/seq_types.h:27-42`).
- L'état de base MIDI FX est hors du tableau `PARAM_PERSIST_COUNT`, qui s'arrête à `PARAM_MIDI_FX_S1_PARAM1` (`Inc/Param/param_store.h:427-432`). Il est néanmoins persisté séparément dans `PatternSaveV1`, capturé/restauré par `pattern_live.c` et dans les snapshots de piste (`Inc/Storage/pattern_live_ram.h:92-100`, `Inc/Core/track_snapshot.h:46-67`).
- `note_fx_state_set_param()` clamp les valeurs indépendamment et force `OFF` pour une valeur de modèle inconnue (`Src/NoteFx/note_fx_state.c:15-22`, `:81-104`). Il n'effectue aucune normalisation inter-paramètres et ne connaît que l'unicité ARP.
- Les valeurs par défaut actuelles sont `{2, 0, 1, OFF}` (`Src/NoteFx/note_fx_state.c:9-13`) : elles signifient aujourd'hui `RATE=1/16`, `STYLE=ORDER`, `RANGE=1`. Pour EUCLID, le même tableau de quatre octets doit recevoir une normalisation par modèle ; la valeur `2` devient `LENGTH=16`, `0` devient `PULSE=4` pour le slot EUCLID, et `1` reste l'index DIV convenu. Cette projection doit être centralisée, pas dispersée dans l'UI.

### 3.2 Pipeline d'événements

Les sources convergent bien vers NoteFx :

- le clavier appelle `note_fx_pipeline_submit()` (`Src/Keyboard/keyboard_engine.c:381-388`);
- le scheduler appelle la même fonction (`Src/Seq/seq_play_scheduler.c:1396-1443`);
- l'audio appelle `note_fx_pipeline_process()` et segmente jusqu'à la prochaine deadline (`Src/Audio/audio.c:119-121`, `:270`, `Inc/NoteFx/note_fx_pipeline.h:9-12`).

La limite architecturale est précise : `note_fx_pipeline_submit()` ne porte qu'une note et `note_fx_engine_source()` cherche le premier slot ARP (`Src/NoteFx/note_fx_engine.c:63-73`). Les quatre slots ne sont donc pas exécutés dans leur ordre. L'événement sans ARP part directement au terminal, et l'événement ARP différé part aussi directement au terminal.

Le runtime ARP comporte, par slot, 16 sorties possédées, une génération, un `next_sample` et un token (`Src/NoteFx/note_fx_engine.c:5-20`). `release_slot()` envoie des Off mais l'API terminal ignore `token` et `generation` (`Src/NoteFx/note_fx_pipeline.c:15-25`). Cela protège partiellement contre un événement stale interne, mais pas contre deux occurrences de même hauteur au terminal.

### 3.3 Scheduler, horloge et terminal

- La période ARP est calculée à partir de `samples_per_step_q16` dans `note_fx_engine.c:100-106`; c'est une horloge partagée, mais sa table n'est pas commune avec `PARAM_SEQ_DIV`.
- `note_fx_engine_process()` scanne 8 pistes et 4 slots, avec un budget local de 8 émissions par piste et par appel (`Src/NoteFx/note_fx_engine.c:109-149`). Le budget est recréé par sous-segment audio ; il n'est pas un budget de demi-buffer. Ce point est confirmé par `docs/audits/z1_hard_rt_debt_audit.md` et `docs/audits/z4_scheduler_clock_midi_debt_audit.md`.
- Le scheduler réserve actuellement les Note On et Note Off séparément dans une file fixe de 512 événements (`Src/Seq/seq_play_scheduler.c:347-400`). L'audit Z4 confirme qu'une seule case libre peut laisser passer l'On sans son Off. Euclid ne doit pas aggraver cette asymétrie : ses couples seront réservés atomiquement dans la nouvelle file différée ou dans une capacité propre fixe.
- Le terminal commun appelle MIDI puis le moteur local (`Src/Seq/seq_play_scheduler.c:682-699`). Aucun FX ne doit appeler `midi_note_on/off()` ou une API moteur directement.
- L'audit Z4 confirme aussi que l'horodatage donné à NoteFx pour un événement audio est actuellement la fin du bloc (`docs/audits/z4_scheduler_clock_midi_debt_audit.md`, ticket Z4-004). L'intégration Euclid doit consommer le sample d'application réel, sinon sa première phase sera décalée.

### 3.4 UI et capacités de piste

- Il n'existe qu'une page MIDI FX, avec quatre subpages fixes (`Src/UI/pages/ui_page_midi_fx.c:9-31`); aucune page Euclid n'est nécessaire.
- Le rendu dynamique utilise `RATE/STYLE/RANGE/MODEL` et les labels ARP (`Src/UI/pages/ui_page_midi_fx.c:105-152`). Il faut conserver ce rendu pour ARP et projeter `LEN/PULS/DIV/EUCLID` pour EUCLID.
- L'UI MIDI FX n'est enregistrée que pour les familles admissibles et exclut le looper (`Src/UI/pages/ui_page_midi_fx.c:171-191`). Le runtime ajoute l'ensemble selon la capacité MIDI FX (`Src/Core/track_runtime.c:594-621`). Euclid ne doit pas contourner ces deux autorités.

### 3.5 P-lock, Undo, snapshots et clipboard

- Tous les paramètres MIDI FX sont actuellement mappés au set p-lock MIDI FX (`Src/Seq/seq_param_iface.c:184-213`) et l'overlay runtime est appliqué par `note_fx_pipeline_apply_runtime_param()` (`Src/Seq/seq_param_iface.c:866-875`).
- Le changement de modèle ferme le runtime avant mutation (`Src/Param/param_registry.c:1617-1639`). Les autres paramètres ne ferment pas les sorties ; cette différence deviendrait dangereuse pour `LENGTH/PULSE/DIV` si elles changeaient une phase active.
- L'Undo paramètre passe par `param_registry_apply_track_value()` (`Src/Storage/undo_v2.c:373-423`); l'Undo snapshot stocke déjà les `note_fx_track_state_t` (`Inc/Storage/undo_v2.h:100-108`). Le changement de modèle ARP note aussi le slot déplacé dans l'UI (`Src/UI/ui_param.c:1494-1507`).
- Le clipboard de piste capture/restaure `track_snapshot_t` (`Src/UI/ui_core_clipboard.c:409-433`, `:459-511`). Le clipboard de page/ensemble lit les paramètres par ID (`Src/UI/ui_core_clipboard.c:514-555`). Les mêmes chemins suffisent après ajout de la validation EUCLID.

### 3.6 Recherche obligatoire et résultats négatifs

Recherches effectuées dans `Src`, `Inc`, `tests` et `docs` :

| Recherche | Résultat |
|---|---|
| `EUCLID`, `BJORK`, `BJORKLUND` | Aucun moteur, masque, ID ou prototype réutilisable trouvé |
| `ARP`, gestion d'accord et retrigger | `note_fx_arp_t`, 16 sources, déduplication par hauteur, phase entière et runtime ARP décrits ci-dessus |
| scheduler de Note On/Off différés | `seq_play_scheduler_push_note_pair()` et `g_seq_play_events[512]`; paire non atomique confirmée |
| divisions | labels `g_seq_div_labels`, conversion `track_div` 1/2/4/8 et table privée ARP |
| `HARMONY`, `GATE`, `PROBABILITY` comme modèles MIDI FX | Aucun modèle ni ID actuel ; les combinaisons demandées sont donc des contrats futurs, non des tests de régression exécutables aujourd'hui |
| anciens IDs ou paramètres Euclid morts/réservés | Aucun ID Euclid ; les IDs réservés généraux de `param_store.h:356-390` ne doivent pas être recyclés |
| tests existants | `tests/note_fx_arp_test.c`, `tests/note_fx_runtime_test.c`, `tests/note_fx_pipeline_validation.ps1`, `tests/note_fx_plock_validation.ps1`, `tests/note_fx_persistence_validation.ps1` |

## 4. Écarts avec la cible

| Cible | État actuel | Écart à fermer |
|---|---|---|
| Quatre slots ordonnés | Un seul ARP recherché, indépendamment de sa position | Introduire un passage slot par slot et un contexte de continuation |
| EUCLID modèle 2 | Enum ne contient que OFF/ARP | Ajouter ID, labels, clamping et unicité Euclid |
| Trois paramètres EUCLID | Descripteurs génériques RATE/STYLE/RANGE | Descripteurs par modèle, domaine `LEN/PULS/DIV` dans la banque inchangée |
| Notes maintenues et accords | ARP sait mémoriser 16 notes mais par hauteur seulement | État EUCLID fixe avec hauteur, vélocité, source/token et compte d'activité |
| Notes courtes | Aucun contrat hors ARP | Documenter et tester strictement actif ; pas de latch implicite |
| Génération différée | `next_sample` existe mais les sorties vont directement au terminal | Événements différés réinjectés dans le slot suivant |
| Note Off exact | token/generation internes, terminal et output guard essentiellement par hauteur | Faire traverser l'identité d'occurrence jusqu'au terminal et fermer exactement une occurrence |
| Horloge DIV unique | table ARP et domaine séquence distincts | créer une table/catalogue musical canonique et des conversions |
| Phase transport | cleanup général existe ; Euclid n'existe pas | définir reset, suspension, pattern change et reconfiguration |
| P-lock | MIDI FX entierement p-lockable par domaine | refuser explicitement les 3 paramètres EUCLID en v1, ou implémenter un contrat complet ultérieurement |
| Live Record post-FX | capture actuelle en amont/chemins séparés, pas de capture terminale | conserver le terminal commun comme seam ; hors chantier |

## 5. Sémantique recommandée des notes entrantes

### 5.1 État par slot

Chaque slot EUCLID possède un état statique de capacité fixe :

```text
active[16] : { valid, note, velocity, input_token, source_generation }
phase      : 0..63
mask       : uint64_t
length     : 1..64
pulse      : 0..length
div        : index du catalogue partagé
next_sample
active_output[16] : { valid, note, token, generation, off_sample }
```

`active[]` est l'état musical courant, non l'état persisté. La note est unique par couple source/identité ; un Note On répété pour la même identité met à jour la vélocité sans créer une seconde source. Deux notes de même hauteur issues de sources distinctes doivent rester distinctes si le contrat d'entrée leur donne des tokens différents.

Au maximum 16 entrées sont mémorisées, comme l'ARP. Un 17e Note On est rejeté avec un compteur de saturation ; aucun Note Off artificiel ne doit être généré pour une entrée jamais admise.

### 5.2 Note On, Note Off et note courte

- Note On accepté : insérer ou mettre à jour la source ; si la liste était vide et que le transport est actif, ancrer `next_sample` au sample de l'événement et remettre la phase à 0.
- Note Off : supprimer uniquement la source portant la même identité ; si plusieurs occurrences ont été générées à partir de cette source, fermer leurs sorties possédées, puis ne plus les rejouer.
- Une source relâchée avant le pulse suivant ne produit rien à ce pulse. Il n'y a pas de mémoire du dernier accord après disparition de toutes les sources.
- Un accord tenu est le snapshot des 16 entrées actives au moment du pulse ; l'ordre est l'ordre stable de la table, ou l'ordre explicite porté par l'événement si le contrat de chaîne l'ajoute.
- Une nouvelle note ou un nouveau chord modifie la matière pour le pulse suivant. Le pulse courant ne doit pas parcourir un tableau en cours de mutation : la source est publiée avant le traitement, ou copiée dans un buffer fixe de 16 entrées.

Cette politique est moins permissive pour les notes STEP très courtes qu'un latch, mais elle est déterministe, compatible avec le comportement ARP, sûre pour le jeu live et ne crée pas de note fantôme. La documentation utilisateur devra indiquer qu'une note STEP doit chevaucher au moins un pulse Euclid pour être entendue.

### 5.3 Comparaison des options écartées

| Option | Compatibilité actuelle | Risque | Décision |
|---|---|---|---|
| Strictement actif | Directement compatible avec `note_fx_arp_note_on/off()` | Un STEP court peut ne produire aucun pulse | Retenue v1 |
| Dernier accord latché | Pourrait rendre les STEP continus | Note tenue relâchée qui continue, reset ambigu, comportement live surprenant | Rejetée v1 |
| Réutiliser l'ARP | Réutilisable seulement pour stockage simple | L'ARP déduplique par hauteur, ne porte pas la durée Euclid et n'est pas une chaîne ordonnée | Extraire une abstraction d'identité bornée, pas réutiliser l'algorithme tel quel |

## 6. Architecture temporelle recommandée

### 6.1 Horloge

Euclid doit être alimenté par `note_fx_pipeline_process()` depuis la même timeline audio que l'ARP. Le scheduler séquence reste l'autorité de la cadence musicale (`samples_per_step_q16`), et Euclid ne crée aucune IRQ, aucun timer, aucun traitement par sample dans l'IRQ audio.

Le catalogue partagé doit exposer, pour chaque division : label, ratio Q16 et éventuellement valeur brute persistée. `PARAM_SEQ_DIV` doit continuer à présenter son sous-ensemble actuel `OFF/1/2/1/4/1/8`, tandis que `EUCLID DIV` utilise le sous-ensemble musical nécessaire, probablement celui déjà représenté par la table ARP `1/4, 1/8, 1/16, 1/32` et leurs triplets. La table doit avoir une seule autorité ; les indices historiques sont convertis à la frontière.

La période d'une position est calculée une fois par reconfiguration ou changement de tempo :

```text
position_period_q16 = shared_division_period(samples_per_step_q16, div)
position_period     = max(1, round(position_period_q16 / 65536))
```

Le chemin chaud ne fait pas de division musicale et ne recalcule pas Bjorklund. Le changement de tempo met à jour la période sans reconstruire le masque.

### 6.2 Phase et transport

| Événement | Action Euclid |
|---|---|
| `PLAY` | phase 0 ; si des notes sont déjà actives, premier pulse au sample de reprise ; sinon attendre le prochain Note On |
| `STOP` | fermer chaque sortie, vider les sources, invalider les échéances et remettre phase/motif à l'état initial |
| `CONTINUE` après STOP | même état propre qu'un nouveau départ, phase 0 ; aucune note stale ne survit |
| changement de pattern | cleanup de tous les slots de la piste avant restauration ; phase 0 |
| changement de modèle | fermeture immédiate, purge du runtime du slot, puis configuration du nouveau modèle |
| `OFF` | fermeture et purge du slot ; les trois valeurs restent stockées pour une prochaine sélection |
| changement `LENGTH/PULSE/DIV` base autorisé | fermer les occurrences, normaliser, reconstruire le masque et repartir phase 0 |
| p-lock EUCLID v1 | refus avant exécution ; aucun effet partiel |
| mute | suspendre et purger ; pas d'avancement de phase |
| solo | suivre le pont mute/suspend existant ; aucun chemin spécial Euclid |
| panic | fermeture idempotente de toutes les occurrences et purge des sources |
| changement de piste affichée | aucun effet sur le runtime d'une autre piste |
| note entrante à l'arrêt | conserver uniquement le suivi de source strictement nécessaire ; ne pas générer ; ancrer à `PLAY` si elle est toujours active |
| note relâchée à l'arrêt | supprimer la source ; aucun Note Off généré si aucune occurrence n'est active |

Le changement de paramètre en direct doit être traité comme une transition transactionnelle : snapshot des sorties, émission de leurs Off, changement de valeurs, normalisation `PULSE <= LENGTH`, reconstruction du masque, publication du nouveau runtime. Il ne faut jamais laisser une occurrence utiliser une ancienne échéance avec une nouvelle génération.

### 6.3 Masque et algorithme

Aucun Bjorklund/Euclid existant n'a été trouvé. La recommandation est un générateur borné de type Bjorklund calculé uniquement lorsque `LENGTH` ou `PULSE` change, avec cas directs pour `PULSE=0` et `PULSE=LENGTH`.

Le résultat est stocké dans un `uint64_t`; la phase lit un bit, avance modulo `LENGTH`, et ne fait aucune division dans le chemin chaud. L'orientation doit être figée et testée : motif Bjorklund canonique, rotation minimale pour que le premier pulse soit à la position 0, aucune rotation configurable. Cela donne une phase reproductible sans introduire `ROTATE`.

L'implémentation doit :

- accepter `LENGTH=1` et `PULSE=0/1`;
- accepter toutes les combinaisons `0 <= PULSE <= LENGTH`;
- produire exactement `PULSE` bits à 1;
- ne dépendre d'aucun ordre de parcours non déterministe;
- ne faire qu'un travail borné à 64 positions et à quelques tableaux locaux fixes;
- fournir une fonction de test pure `euclid_build_mask(length, pulse)` indépendante de l'audio.

Le calcul direct par `floor(i * PULSE / LENGTH)` serait suffisant mais donnerait une orientation implicite et une division par position si mal placé. Le masque pré-calculé est préférable : 8 octets par slot, soit 256 octets pour 32 slots, coût négligeable face aux buffers existants.

## 7. Architecture des Note On/Off

### 7.1 Identité

Le contrat actuel contient `sample`, `token`, `generation`, piste, note, vélocité, destination et type (`Inc/NoteFx/note_fx_engine.h:11-14`), mais `note_fx_pipeline_submit()` ne reçoit pas le token du scheduler et le terminal le perd. L'extension minimale doit introduire une identité d'occurrence complète :

```text
source_id       : identité de la note entrante
occurrence_id   : nouveau token pour chaque Note On généré
slot_generation : invalidation de transition
parent_id       : lien optionnel vers l'événement précédent de la chaîne
stage/next_slot : position dans les quatre slots
```

Le token n'est pas persisté. Il sert uniquement à apparier chaque Off au On qui l'a créé et à refuser un événement stale après reset. Une hauteur MIDI seule est insuffisante : `seq_play_scheduler` ne possède actuellement qu'un token `[track][note]` (`Src/Seq/seq_play_scheduler.c:1422-1437`) et l'ARP associe aussi ses sorties par hauteur source (`Src/NoteFx/note_fx_engine.c:75-85`). Le plan doit remplacer cette hypothèse pour le chemin NoteFx, sans toucher aux formats audio.

### 7.2 Durée et fermeture

Pour chaque pulse actif :

1. fermer les occurrences précédentes dont l'échéance est atteinte ;
2. si le bit du masque est 1, allouer jusqu'à 16 occurrences dans l'ordre stable ;
3. publier chaque On avec son `occurrence_id`;
4. programmer l'Off à `on_sample + position_period` dans la file différée fixe;
5. si Off et On ont le même sample, émettre Off avant On.

La durée ne provient pas de la note entrante : une note tenue n'a pas de fin connue. Elle ne doit pas être `jusqu'au prochain pulse`, car cela rend la durée dépendante du motif et complique les transitions ; elle ne doit pas être une fraction fixe cachée sans autorité de paramètre. La période `DIV` est la seule durée temporelle canonique disponible.

Une reconfiguration ferme tout `active_output[]` avant d'accepter le nouveau motif. Cela élimine les anciens Off qui pourraient couper une occurrence plus récente après un changement de DIV. Une occurrence refusée faute de capacité ne publie ni On ni Off.

### 7.3 File différée et chaîne

Le plan recommande une petite file statique par piste ou une file globale à quota par piste, réservée atomiquement pour les couples On/Off. Elle ne doit pas réutiliser silencieusement la file scheduler 512 sans audit de ses producteurs, car cette file ne représente aujourd'hui que les événements séquence et ne transporte pas le contexte de stage.

Contrat minimal de continuation :

```text
note_fx_emit(event, next_slot)
    if next_slot < 4: note_fx_slot_process(next_slot, event)
    else:             note_fx_pipeline_terminal(event)
```

Un événement différé d'EUCLID reprend à `next_slot`, jamais à la tête de la chaîne. Le callback terminal reste le seul consommateur MIDI/moteur. La file doit être vidée et ses Off émis avec l'ancienne génération lors de STOP, panic, mute, modèle, pattern et piste.

Le terminal futur devra offrir une surcharge ou un contexte qui conserve l'identité ; le dispatcher existant (`seq_play_scheduler_dispatch_terminal_note_to_channel`) peut rester le dernier adaptateur vers les APIs actuelles, mais la table de voix/token doit être corrigée avant de déclarer l'absence de notes pendantes.

### 7.4 Interaction avec le moteur local et MIDI externe

Le nombre d'entrées Euclid n'est pas limité par `VOICES` au moment du stockage. L'admission terminale reste responsable de la capacité de destination : au plus 8 voix synth prévues par le chantier de polyphonie, tandis qu'une sortie MIDI externe peut conserver jusqu'à la limite explicitement définie du contrat MIDI FX. La réduction doit être stable et se faire avant l'appel moteur ; elle ne doit jamais produire un On sans Off.

Les appels existants `midi_note_on/off()` et `seq_play_scheduler_emit_engine_note()` ne retournent pas d'acquittement suffisant (`docs/audits/z4_scheduler_clock_midi_debt_audit.md`, sections Z4-002/Z4-003). L'étape d'intégration doit donc fermer les couples au niveau du contrat local avant le terminal et instrumenter les drops ; elle ne doit pas prétendre que l'acceptation USB ou moteur est garantie.

## 8. Intégration dans la chaîne MIDI FX

### 8.1 Ordre réel

L'API future doit porter explicitement `slot_index` et continuer l'événement vers le slot suivant. Un slot `OFF` transmet l'événement inchangé. Un slot ARP consomme ses sources et produit ses sorties au prochain deadline. Un slot EUCLID consomme l'ensemble strictement actif et produit zéro ou plusieurs sorties sur ses pulses. Chaque sortie porte le prochain stage.

L'architecture ne doit pas devenir un graphe général : quatre appels séquentiels, fan-out maximal fixe et retour terminal unique suffisent. L'ordre `[0,1,2,3]` est l'ordre produit. Aucun modèle ne doit rechercher un slot par type comme le fait actuellement l'ARP à `note_fx_engine.c:68-71`.

### 8.2 Matrice des combinaisons demandées

Les modèles HARMONY, GATE et PROBABILITY n'existent pas encore dans le HEAD. Les lignes ci-dessous définissent le contrat à préserver pour leur arrivée ; elles ne constituent pas une extension de cette passe.

| Ordre | Comportement attendu | Compatibilité v1 | Risque / validation |
|---|---|---|---|
| `ARP -> EUCLID` | ARP séquence les notes entrantes ; EUCLID pulse la note actuellement sortie par ARP | Supporté après chaîne et identités ; les notes ARP courtes peuvent disparaître avant le pulse Euclid suivant | Contrat strict-actif explicite ; vérifier fan-out limité à 16 |
| `EUCLID -> ARP` | EUCLID produit un accord rythmique ; ARP sérialise les notes reçues | Supporté après réinjection par stage | Vérifier qu'un Off Euclid ferme la source correspondante ARP sans vider les autres |
| `HARMONY -> EUCLID` | Harmony produit un groupe enfant ; Euclid rejoue le groupe actif | Modèle absent ; compatible seulement si Harmony porte parent/child IDs | Cap à 16 notes et admission locale à 8 voix |
| `EUCLID -> HARMONY` | Euclid produit des parents ; Harmony produit des enfants à fermer par parent | Modèle absent ; contrat possible | Risque de multiplication ; cap d'enfants obligatoire, Off parent/enfants atomiques |
| `GATE -> EUCLID` | Gate laisse passer/supprime des sources avant mémoire Euclid | Modèle absent ; aucune hypothèse dans Euclid | Tester suppression avant pulse et absence d'entrée zombie |
| `EUCLID -> GATE` | Gate filtre les On/Off Euclid en conservant les tokens | Modèle absent ; doit être un filtre d'événements, pas un moteur direct | Tester Off garanti même si Gate supprime un On |
| `PROBABILITY -> EUCLID` | Probability décide quelles sources alimentent Euclid | Modèle absent ; compatible si la décision est stable par occurrence | Tester drop avant pulse et répétabilité sous seed |
| `EUCLID -> PROBABILITY` | Probability décide chaque occurrence produite | Modèle absent ; le token doit survivre au drop | Tester qu'un On refusé n'a pas d'Off et qu'un On accepté en a exactement un |

La validation v1 doit également refuser explicitement une seconde instance EUCLID sur la même piste. Elle ne doit pas l'activer silencieusement ou prendre le premier slot comme le code ARP actuel.

### 8.3 Futur point de capture post-FX

Le point à préserver est `note_fx_pipeline_terminal()` avant son adaptation finale au dispatcher. Le futur Live Record pourra s'abonner à cet événement terminal, avec track, canal, note, vélocité, type, sample et identité. Aucun appel direct au moteur ne doit être ajouté à Euclid ; sinon le rendu Euclid ne serait pas capturable avec le reste des FX et créerait un double chemin.

## 9. Bornes et budgets

### 9.1 Bornes fonctionnelles

| Ressource | Borne recommandée |
|---|---:|
| Pistes | 8 Play, aucune Special/audio pure |
| Slots | 4 par piste |
| EUCLID actif | 0 ou 1 par piste |
| ARP actif | 0 ou 1 par piste, convention existante |
| Sources mémorisées par EUCLID | 16 |
| Notes produites par pulse | 16 avant admission de destination |
| Active outputs simultanées par EUCLID | 16 |
| LENGTH | 1..64 |
| PULSE | 0..LENGTH |
| Masque | 64 bits |
| Profondeur de chaîne | 4 slots, pas de récursion externe |
| Allocations | aucune dynamique |

La borne `16` est cohérente avec `NOTE_FX_MAX_OUTPUTS` et `NOTE_FX_ARP_MAX_SOURCES`. La borne de synthèse à 8 ne doit pas être injectée dans la mémoire musicale Euclid ; elle appartient à l'admission locale et au chantier de polyphonie.

### 9.2 Coût estimatif à mesurer

- Masques : 32 slots × 8 octets = 256 octets.
- Sources Euclid : 32 slots × 16 entrées × au moins 4 octets (note, vélocité, validité, index) avant alignement ; prévoir le token et la génération dans le budget exact `sizeof`.
- Sorties actives : 32 slots × 16 identités, avec échéance 64 bits et token 32 bits ; mesurer en D2/DTCM selon le placement actuel de NoteFx.
- Au pulse maximal, un slot peut publier 16 On et programmer 16 Off. Un track avec ARP + EUCLID doit être borné par un quota partagé ; le budget historique de 8 émissions par track et appel (`note_fx_engine.c:113-127`) est insuffisant et ne doit pas être simplement multiplié sans mesure.
- Le plan d'implémentation doit retenir un budget par demi-buffer, pas par sous-segment. Toute émission refusée doit suivre une politique déterministe : fermer d'abord les occurrences échues, puis refuser l'occurrence complète On+Off restante.

Les coûts réels doivent être mesurés sur H743 dans les builds `Release Low-Cost` et `Release Premium`. `TestPremium` n'est pas requis.

## 10. Risques et invariants

### Invariants obligatoires

1. `0 <= PULSE <= LENGTH <= 64` à toute frontière publique.
2. Une source admise a au plus une entrée active par identité.
3. Tout Note On généré a exactement un Off, sauf fermeture panic qui est explicitement idempotente.
4. Aucun Off d'une génération précédente ne peut fermer une occurrence nouvelle.
5. Une sortie générée traverse tous les slots suivants et n'appelle jamais directement un moteur.
6. `PULSE=0` ne produit aucun événement.
7. Le nombre de sources, sorties, événements différés et profondeur de chaîne est borné par des constantes compilées.
8. Les événements d'un même sample sont ordonnés Off avant On pour une même identité/hauteur.
9. Le runtime phase/mask/active outputs n'est ni persisté ni copié dans Pattern, Project, clipboard ou Undo.
10. Le changement de modèle, de pattern, de piste, de mute, de transport ou de paramètre ferme avant d'invalider.
11. La division musicale possède une seule table canonique.
12. Les pistes sans capacités adéquates ne peuvent ni exposer ni modifier les paramètres MIDI FX.
13. Une saturation refuse une occurrence complète, sans Note Off orphelin.
14. Le terminal commun est le seul point qui atteint MIDI et moteurs et reste identifiable pour le futur Live Record.

### Risques à traiter par les étapes

Les risques confirmés ou plausibles sont : notes pendantes lors d'un reset/stop, anciens Off par hauteur, re-déclenchements qui se chevauchent, accord modifié pendant un pulse, note courte sans pulse, saturation de 16 sources, explosion `EUCLID -> HARMONY` future, ordre de slots actuellement faux, changement de modèle pendant lecture, changement DIV sous une occurrence active, dérive due à une seconde horloge, budget recréé par sous-segment, incohérence UI/runtime/persistance, et événement généré qui contourne les slots suivants.

Chaque risque doit avoir au moins un test dans la matrice de la section 12. Un compteur de drop ou de saturation est recommandé pour le diagnostic, mais ne remplace pas l'invariant de fermeture.

## 11. Plan d'action par étapes

Chaque étape part du HEAD courant, ne modifie pas les changements parallèles, produit un commit local unique et ne pousse rien. Les noms de fichiers ci-dessous sont les points d'entrée probables ; l'agent doit confirmer les lignes sans élargir le périmètre.

### Étape 1 — Contrat ordonné et propriétaire du runtime NoteFx

**Objectif.** Transformer le pipeline « un slot ARP implicite » en chaîne ordonnée de quatre slots, avec un propriétaire d'exécution clair et un contexte qui peut reprendre au slot suivant.

**Fichiers et symboles probablement concernés.** `Inc/NoteFx/note_fx_engine.h`, `Src/NoteFx/note_fx_engine.c`, `Inc/NoteFx/note_fx_pipeline.h`, `Src/NoteFx/note_fx_pipeline.c`, `Src/Audio/audio.c`, `Src/Seq/seq_play_scheduler.c`, éventuellement `Inc/Seq/seq_play_scheduler.h`; `note_fx_engine_source`, `note_fx_engine_process`, `note_fx_pipeline_terminal`, `note_fx_pipeline_submit`, `note_fx_pipeline_frames_until_deadline`.

**Changements précis attendus.** Ajouter `stage/next_slot`, source/parent identity et une continuation bornée ; faire passer chaque événement dans les slots 0..3 ; faire transiter les sorties différées au stage suivant ; conserver un seul terminal ; transmettre le sample d'application réel ; introduire un owner unique de la mutation runtime ou une file fixe de commandes si l'audit Z1 reste applicable.

**Invariants à préserver.** Sources clavier et scheduler convergent ; OFF traverse la même chaîne que ON ; aucun appel moteur depuis un slot ; 4 slots maximum ; pas d'allocation ; cleanup idempotent ; `OFF` est transparent.

**Hors périmètre.** Aucun EUCLID, aucune nouvelle UI, aucune modification de Live Record, aucun modèle HARMONY/GATE/PROBABILITY.

**Dépendances.** Aucune étape précédente ; dépend des contrats actuels de `seq_play_scheduler` et de `track_runtime`.

**Validations ciblées.** Tests de passage OFF, ordre observé avec un FX filtre temporaire de test, source clavier/scheduler, deadline audio, reset de quatre slots ; builds Release Low-Cost et Release Premium.

**Recherches négatives.** Vérifier qu'il ne reste aucun `find first ARP` utilisé comme routage général, aucun callback direct d'un slot vers `midi_note_*` ou moteur, et aucune allocation dynamique.

**Critères de fin.** Un événement marqué stage 0 atteint exactement les stages suivants jusqu'au terminal, les événements différés sont exécutés au bon sample, les tests NoteFx existants restent verts.

**Documentation à mettre à jour.** Ajouter la cartographie et le contrat dans `docs/architecture/z4_seq_clock_scheduler.md` ou le document NoteFx existant, sans plan Live Record.

**Commit recommandé.** `refactor: order midi fx slots`

### Étape 2 — Modèle, IDs et catalogue des divisions

**Objectif.** Ajouter `EUCLID` et rendre les trois paramètres du slot interprétables par modèle sans casser le stockage contigu.

**Fichiers et symboles probablement concernés.** `Inc/NoteFx/note_fx_state.h`, `Src/NoteFx/note_fx_state.c`, `Inc/Param/param_store.h`, `Src/Param/param_registry_catalog.c`, `Src/Param/param_registry.c`, `Inc/Seq/seq_types.h`, `Src/Seq/seq_param_iface.c`, `Inc/Seq/seq_param_iface.h`, catalogue partagé à créer sous `Inc/Seq`/`Src/Seq`.

**Changements précis attendus.** Ajouter l'ID enum EUCLID append-only ; labels et domaines `LENGTH 1..64`, `PULSE 0..64`, DIV canonique ; normaliser par slot et modèle ; appliquer `PULSE=min(PULSE,LENGTH)` dans set/restore/runtime override ; empêcher une seconde instance EUCLID ; préserver les 16 positions p-lock et les IDs persistants existants.

**Invariants à préserver.** Aucun ID réservé réutilisé ; `PARAM_COUNT` et offsets cohérents ; valeurs inconnues -> OFF ou valeur sûre ; seules pistes MIDI FX + notes autorisées ; ARP reste unique ; les valeurs de slot restent quatre octets.

**Hors périmètre.** Aucun calcul Bjorklund, aucune phase, aucun événement généré, aucun rendu UI final.

**Dépendances.** Étape 1 pour le modèle générique et le reset de slot.

**Validations ciblées.** Tests de clamp, défauts, second EUCLID, changement OFF/ARP/EUCLID, restauration snapshot ; assertions param/p-lock ; script de persistance existant.

**Recherches négatives.** Vérifier qu'il n'existe qu'une table de divisions, qu'aucun domaine DIV concurrent n'est ajouté et qu'aucun runtime `phase/mask/owned` n'entre dans une structure de Pattern.

**Critères de fin.** Les valeurs Euclid se lisent et s'écrivent par les APIs track existantes, chaque valeur invalidée est normalisée, le format mémoire courant reste inchangé.

**Documentation à mettre à jour.** Section paramètres de `docs/architecture/z3_param_modulation_control.md` et ce plan si les valeurs exactes du catalogue diffèrent de la recommandation.

**Commit recommandé.** `feat: add euclid midi fx model data`

### Étape 3 — Algorithme Euclid et masque borné

**Objectif.** Implémenter et tester le calcul déterministe du masque sans coût de division dans le chemin chaud.

**Fichiers et symboles probablement concernés.** `Inc/NoteFx/note_fx_euclid.h`, `Src/NoteFx/note_fx_euclid.c`, `Inc/NoteFx/note_fx_engine.h`, `Src/NoteFx/note_fx_engine.c`, `tests/note_fx_euclid_test.c`, `tests/CMakeLists.txt`.

**Changements précis attendus.** Ajouter `euclid_build_mask(length,pulse)`, cas P0/P=L, Bjorklund borné avec orientation canonique documentée, `uint64_t` mask, reconstruction uniquement au changement de L/P, compteurs exacts de bits.

**Invariants à préserver.** Toutes les combinaisons valides donnent exactement P pulses ; aucune écriture hors 64 bits ; résultat identique à paramètres identiques ; pas de modulo/division par position en runtime.

**Hors périmètre.** Source notes, scheduler, UI, p-lock et terminal.

**Dépendances.** Étape 2 pour les domaines clampés.

**Validations ciblées.** `P=0`, `P=1`, `P=L`, L impaires, L=1, motifs de référence E(3,8), E(5,16), répétabilité sur 1000 reconstructions ; test CMake unitaire.

**Recherches négatives.** Aucun appel à `malloc/calloc/realloc`, aucune division/modulo par `LENGTH` dans la fonction de lecture du masque, aucun générateur aléatoire.

**Critères de fin.** Le test pur passe sur LowCost/host et le masque peut être lu par l'engine sans recalcul par pulse.

**Documentation à mettre à jour.** Ajouter le choix d'orientation et les exemples dans `docs/plan_midi_fx_euclid.md` et éventuellement `docs/architecture/z4_seq_clock_scheduler.md`.

**Commit recommandé.** `feat: add bounded euclid rhythm mask`

### Étape 4 — Notes actives, phase et transitions

**Objectif.** Ajouter l'état runtime EUCLID pour une note, un accord, les notes courtes et la phase transport.

**Fichiers et symboles probablement concernés.** `Inc/NoteFx/note_fx_euclid.h`, `Src/NoteFx/note_fx_euclid.c`, `Src/NoteFx/note_fx_engine.c`, `Src/NoteFx/note_fx_pipeline.c`, `Src/Seq/seq_runtime_exec.c`, `Src/Seq/seq_play_scheduler.c`, `Src/Core/track_mute.c`, transitions snapshot/pattern.

**Changements précis attendus.** Table fixe de 16 sources avec identité, velocity et validité ; table de 16 sorties ; phase 0..63 ; ancrage au premier Note On actif ; strict-active Note Off ; reset PLAY/STOP/CONTINUE/pattern/mute/panic/model ; pas de phase sous mute ; copie stable avant pulse.

**Invariants à préserver.** Note Off source ne ferme pas une autre source de même hauteur ; accord au plus 16 ; note courte ne latch pas ; runtime non persisté ; aucune génération si transport suspendu ; nettoyage complet.

**Hors périmètre.** UI et p-lock ; admission moteur finale ; capture Live Record.

**Dépendances.** Étapes 1 à 3.

**Validations ciblées.** note tenue, accord, note ajoutée/retirée au milieu du cycle, note courte entre pulses, note au STOP, PLAY/STOP/CONTINUE, pattern change, mute/solo/panic, huit pistes et quatre slots.

**Recherches négatives.** Aucune copie de phase dans Pattern/Project/Undo/clipboard ; aucun latch global ; aucun tableau variable par piste.

**Critères de fin.** Les sources et phase sont déterministes, bornées et complètement purgées sur toutes les transitions ; aucun événement généré n'est encore envoyé au terminal directement.

**Documentation à mettre à jour.** Ajouter la machine d'état et la matrice transport dans `docs/architecture/z4_seq_clock_scheduler.md`.

**Commit recommandé.** `feat: add euclid note and phase runtime`

### Étape 5 — Génération Note On/Off et continuation aval

**Objectif.** Produire des couples autonomes, différés et identifiés, puis les envoyer aux slots suivants.

**Fichiers et symboles probablement concernés.** `Inc/NoteFx/note_fx_engine.h`, `Src/NoteFx/note_fx_engine.c`, `Src/NoteFx/note_fx_pipeline.c`, `Src/Seq/seq_play_scheduler.c`, `Src/Seq/seq_output_guard.c`, éventuellement nouveau module de queue fixe `Src/NoteFx`.

**Changements précis attendus.** Token d'occurrence et génération par On/Off ; échéance `On + DIV`; ordre Off/On au même sample ; réservation atomique du couple ; file différée fixe ; reprise au `next_slot`; terminal enrichi de l'identité ; admission locale stable à 8 voix synth et borne MIDI explicite.

**Invariants à préserver.** zéro note pendante, zéro Off obsolète, aucun doublon incontrôlé, aucun On sans Off accepté, aucun callback direct moteur/MIDI, quota par demi-buffer et non par sous-segment.

**Hors périmètre.** Modèles futurs HARMONY/GATE/PROBABILITY, modification complète de l'USB MIDI, Live Record.

**Dépendances.** Étapes 1 et 4 ; dépend des bornes de la section 9.

**Validations ciblées.** pulse dense, P=L, changement DIV avec occurrences actives, saturation de file à 0/1/2 places, même note répétée, ARP avant/après, absence d'Off tardif après model/mute/panic, point terminal unique.

**Recherches négatives.** `midi_note_on`, `midi_note_off`, `*_runtime_note_on/off` absents des modules Euclid ; aucune file non bornée ; aucun token basé uniquement sur hauteur.

**Critères de fin.** Chaque occurrence retenue produit une paire identifiable, la paire traverse les stages restants, et la mesure ne révèle pas de dépassement du budget audio fixé.

**Documentation à mettre à jour.** Mettre à jour `docs/audits/z4_scheduler_clock_midi_debt_audit.md` avec le contrat de paire NoteFx corrigé, sans traiter le futur Live Record.

**Commit recommandé.** `feat: schedule euclid note pairs through midi fx`

### Étape 6 — UI dynamique sans nouvelle page

**Objectif.** Exposer `LEN | PULS | DIV | EUCLID` dans la page existante, sans changer la composition des quatre slots.

**Fichiers et symboles probablement concernés.** `Src/UI/pages/ui_page_midi_fx.c`, `Src/Param/param_registry_catalog.c`, `Src/UI/ui_param.c`, `Src/UI/ui_renderer_template.c` si le nom de descriptor est rendu directement, tests UI statiques existants.

**Changements précis attendus.** Labels et valeurs par modèle ; clamp visuel ; modèle EUCLID ; affichage `-` quand OFF ; invalidation après changement de modèle/paramètre ; encoder inactif pour les trois params quand OFF ; conservation des quatre subpages et de l'ordre imposé.

**Invariants à préserver.** ARP conserve RATE/STYLE/RANGE ; aucune page supplémentaire ; page inaccessible sur Special/looper non admissible ; UI ne devient jamais l'autorité runtime.

**Hors périmètre.** P-lock EUCLID actif, nouvelle navigation, changement de format.

**Dépendances.** Étape 2 pour les descripteurs et étape 5 pour les transitions sûres.

**Validations ciblées.** affichage OFF/ARP/EUCLID, valeurs limites, édition en lecture, changement de piste, mute/solo, clipboard de page/ensemble, LowCost/Premium.

**Recherches négatives.** Aucun accès UI direct au `phase`, `mask`, `active[]` ; aucune nouvelle page ou famille UI ; aucune chaîne de labels dupliquée dans plusieurs fichiers.

**Critères de fin.** Les quatre paramètres affichent exactement `LENGTH | PULSE | DIV | EUCLID` sous Euclid et l'édition passe par les APIs track existantes.

**Documentation à mettre à jour.** `docs/architecture/z5_ui_navigation_interaction.md` et la section UI de ce plan.

**Commit recommandé.** `feat: expose euclid midi fx controls`

### Étape 7 — Persistance, clipboard, Undo/Redo et politique p-lock

**Objectif.** Rendre les valeurs EUCLID cohérentes dans tous les chemins de base et refuser proprement les p-locks EUCLID v1.

**Fichiers et symboles probablement concernés.** `Inc/Storage/pattern_live_ram.h`, `Src/Storage/pattern_live_ram.c`, `Inc/Core/track_snapshot.h`, `Src/Core/track_snapshot.c`, `Inc/Storage/undo_v2.h`, `Src/Storage/undo_v2.c`, `Src/UI/ui_core_clipboard.c`, `Src/Seq/seq_param_iface.c`, tests `note_fx_*`.

**Changements précis attendus.** Normaliser au capture/restore/default ; conserver seulement les quatre octets de base ; ne jamais sérialiser runtime ; faire enregistrer les valeurs effectivement clampées dans Undo ; conserver duplication et clipboard de track/page ; rendre `seq_param_iface_is_param_plockable()` faux pour les trois paramètres EUCLID et fournir une réponse stable au p-lock refusé ; ne pas modifier le format V1 si aucun `sizeof` ne change.

**Invariants à préserver.** Pattern/Project/snapshot/clipboard/Undo restaurent le même état de base ; runtime est nettoyé avant restauration ; aucun p-lock partiellement appliqué ; aucune compatibilité historique ajoutée.

**Hors périmètre.** Capture post-FX Live Record, migration de vieux projets, paramètres Euclid supplémentaires.

**Dépendances.** Étapes 2, 4, 6.

**Validations ciblées.** sauvegarde/rechargement, track snapshot, duplication, clipboard, Undo/Redo, p-lock refusé pour L/P/DIV, modèle p-lockable si conservé, valeur P clampée après L diminué.

**Recherches négatives.** `phase`, `mask`, `active_output`, token et échéance absents de tous les payloads ; aucune modification incohérente de `PROJECT_V1_FILE_VERSION` ; aucune valeur p-lock Euclid qui contourne l'interface.

**Critères de fin.** Un aller-retour complet base -> Pattern/Project/snapshot/clipboard/Undo restitue les mêmes 4 valeurs normalisées et le même modèle, avec runtime vide puis reconfiguré.

**Documentation à mettre à jour.** `docs/architecture/z6_state_persistence_patterns_projects.md` et la matrice de compatibilité p-lock.

**Commit recommandé.** `feat: persist euclid midi fx state safely`

### Étape 8 — Interactions, budgets et validation finale

**Objectif.** Fermer les interactions multi-FX, la capacité des pistes et les preuves de non-régression avant livraison.

**Fichiers et symboles probablement concernés.** tests `note_fx_runtime_test.c`, nouveaux tests Euclid/chain, scripts `note_fx_pipeline_validation.ps1`, `note_fx_plock_validation.ps1`, `note_fx_persistence_validation.ps1`, `tests/CMakeLists.txt`, instrumentation NoteFx/audio, documentation d'architecture concernée.

**Changements précis attendus.** Ajouter les tests de matrice, diagnostics de saturation/overflow, mesures de cycles et high-water par demi-buffer, vérification 8 pistes × 4 slots, validation capacité synth 8 et sortie MIDI, et correction uniquement des écarts découverts dans le périmètre Euclid.

**Invariants à préserver.** Aucun refactor opportuniste ; aucun changement Special/Master/Slave ; aucun TestPremium ; builds Release Low-Cost et Release Premium obligatoires.

**Hors périmètre.** Implémentation des modèles absents, Live Record post-FX, bounce/réinjection, migration historique.

**Dépendances.** Étapes 1 à 7.

**Validations ciblées.** Toute la matrice section 12, tests host, scripts statiques et tests manuels matériel sur les deux variantes.

**Recherches négatives.** `git diff` limité aux fichiers de l'étape ; absence de code fonctionnel non lié ; absence de push ; absence de modifications dans le worktree hors scope.

**Critères de fin.** Toutes les validations automatisées passent, les budgets mesurés sont sous le contrat retenu, aucun risque critique de note pendante reste ouvert, et un commit final local contient uniquement les corrections de l'étape.

**Documentation à mettre à jour.** Documents d'architecture listés dans les étapes précédentes ; ce plan passe en archive de décision, sans plan Live Record détaillé.

**Commit recommandé.** `test: validate euclid midi fx integration`

## 12. Matrice de validations

| # | Cas | Attendu automatisé | Attendu matériel / diagnostic |
|---:|---|---|---|
| 1 | Note tenue, E(3,8), E(5,16), E(1,1) | masque et On/Off répétés exactement | phase stable, aucun jitter hors tolérance |
| 2 | Accord tenu de 2, 8 et 16 notes | toutes les notes actives par pulse, ordre stable | aucun double/voix pendante |
| 3 | Nouvelle note pendant cycle | pulse suivant utilise l'ensemble courant | changement audible au boundary défini |
| 4 | Relâchement avant pulse suivant | note absente du prochain pulse, Off exact | aucune note fantôme |
| 5 | `PULSE=0` | zéro On/Off généré | silence sans erreur |
| 6 | `PULSE=LENGTH` | On à chaque position, exactement L pulses/cycle | densité stable |
| 7 | baisse de LENGTH sous PULSE | PULSE clampé à LENGTH, Undo reflète la valeur réelle | aucune transition pendante |
| 8 | changement DIV pendant lecture | reconfiguration ferme puis repart phase 0 ; pas de p-lock v1 | pas de double retrigger |
| 9 | changement modèle pendant lecture | Off complet, runtime purgé, nouveau modèle propre | aucune note bloquée |
| 10 | STOP, PLAY, CONTINUE, panic | reset phase/sources/sorties conforme | aucun son/MIDI stale |
| 11 | changement pattern | ancien track generation éliminé, nouveau motif propre | pas de note du pattern précédent |
| 12 | mute et solo | suspend/cleanup existant réutilisé | cycle arrêté sous mute, reprise déterministe |
| 13 | ARP avant/après Euclid | stage order réel, fan-out borné | note courte ARP documentée |
| 14 | Harmony avant/après Euclid | test contractuel désactivé tant que modèle absent ; test d'intégration après modèle | parent/enfant exacts |
| 15 | Gate avant/après Euclid | même condition : contrat future, pas de chemin implicite | Off conservés malgré suppression |
| 16 | Probability avant/après Euclid | même condition : seed/occurrence stable | drops sans Off orphelin |
| 17 | quatre slots actifs | chaque stage observé une fois, ordre stable | aucune boucle/récursion |
| 18 | huit pistes Play | bornes mémoire/émissions/phase par piste | mesure H743 sous deadline |
| 19 | sauvegarde/rechargement | modèle et valeurs normalisées identiques, runtime non persisté | replay propre |
| 20 | duplication, clipboard, Undo/Redo | base et p-locks autorisés cohérents ; Euclid L/P/DIV p-lock refusés | UI synchronisée |
| 21 | absence de notes pendantes | invariant token/occurrence après tous les resets | panic et compteur zéro |
| 22 | capacités de piste | Special, Master, looper exclu et audio pur refusés | aucune page/paramètre Euclid exposé |
| 23 | point post-FX | callback terminal reçoit chaque événement normal avec track/stage/token | seam identifiable sans modifier Live Record |

Tests automatisés à ajouter : `note_fx_euclid_test.c` pour le masque et les bornes, puis extension de `note_fx_runtime_test.c` pour sources/phase/identités ; un test de chaîne doit enregistrer chaque stage et le terminal. Les scripts statiques existants doivent vérifier la présence du terminal commun, l'absence de runtime dans les snapshots et la capacité de piste.

Les builds requis après les étapes fonctionnelles sont `Release Low-Cost` et `Release Premium`. Les tests matériels doivent utiliser au minimum clavier interne, entrée MIDI, STEP, sortie MIDI externe et moteur local ; `TestPremium` reste hors périmètre.

## 13. Point d'intégration futur du Live Record post-FX

Le futur Live Record doit pouvoir consommer une projection des événements au niveau de `note_fx_pipeline_terminal()` après le dernier slot et avant ou pendant l'adaptateur terminal commun. Cette projection doit inclure piste, canal, note, vélocité, type, sample, token/parent et génération.

Cette passe ne modifie pas `Src/Seq/seq_live_rec_capture.c`, `seq_live_rec_session.c` ni les règles de capture. Elle exige seulement que Euclid n'appelle pas un moteur parallèle, que ses événements restent ordinaires et que la chaîne fournisse un point de sortie unique. La capture, la prévention des doubles déclenchements, la réinjection, le bounce et la désactivation automatique des FX sont explicitement hors chantier.

## 14. Dettes ou décisions restant ouvertes

1. Mesurer sur H743 le budget réel d'une émission Euclid de 16 notes, avec ARP avant/après, sur les deux variantes.
2. Choisir le placement mémoire exact des tables runtime après `sizeof` et map, sans allocation.
3. Finaliser la table musicale canonique : conserver tous les triplets ARP ou limiter EUCLID au sous-ensemble séquence ; le choix doit suivre l'autorité centrale, pas l'affichage actuel.
4. Définir le contrat d'admission MIDI externe si un périphérique ne peut pas absorber 16 notes simultanées.
5. Résoudre les défauts généraux Z4-002/Z4-003/Z4-004 avant de considérer la garantie hard-RT complète ; Euclid ne doit pas les masquer.
6. Confirmer si `CONTINUE` doit toujours repartir à zéro après STOP ou si un futur transport non destructif mérite une phase persistée en runtime ; la recommandation de ce plan reste reset propre.
7. Définir ultérieurement les parent/child tokens lorsque HARMONY ou une autre expansion polyphonique sera implémentée.
8. Décider dans une passe séparée si LENGTH/PULSE/DIV deviennent p-lockables ; cette décision demandera des règles de phase, de masque, d'Undo et de coût par step, absentes aujourd'hui.

## 15. Ordre recommandé d'exécution

Exécuter strictement :

```text
1. Contrat ordonné et owner NoteFx
2. Modèle, IDs et divisions
3. Masque Euclid
4. Sources, phase et transitions
5. Note On/Off et continuation aval
6. UI
7. Persistance, clipboard, Undo/Redo et p-lock
8. Interactions, budgets et validation finale
```

Après chaque étape, vérifier le diff et créer un commit contenant uniquement l'étape. Aucun push. Ne jamais commencer une étape suivante avec une paire Note On/Off non atomique, un terminal direct, un runtime persistant ou une sémantique de note courte non documentée.
