# Plan MIDI FX : trois slots et modèle EUCLID

Document d’audit et de planification pour le HEAD `3767b8805` (`sampler: finalize native mono audio paths`). Cette passe ne modifie aucun code fonctionnel. Les étapes ci-dessous sont destinées à un agent d’exécution appelé avec :

> Lis `docs/plan_midi_fx_3_slots_euclid.md` et go étape X.

Le nettoyage NoteFx/scheduler de référence est l’omnibus `bbeaab1fb` (`stable`). Les commits placés entre cet omnibus et le HEAD courant concernent le sampler/audio mono ; aucune correction NoteFx identifiée dans cette passe ne les rend obsolètes.

## État après interruption de l’étape 3

Contrôle effectué sur le HEAD réel `7253ef172` (`perf: fuse Multi sampler VCA and accumulation`), sans considérer les rapports comme une preuve. Les commits ciblés sont `752c1faf4` pour l’étape 1, `1d1cdaa75` pour l’étape 2 et `3c9002bc0` pour la passe initiale de l’étape 3. Le commit sampler/audio `7253ef172` et les autres modifications déjà présentes hors périmètre sont conservés.

- **Étape 1 : PARTIELLE.** Le sample live passe par le marqueur owner audio, la garde de génération FX au terminal, l’admission mono par occurrence, le compteur d’overflow clock et la suppression des wrappers terminal sont bien présents. Restent la preuve des admissions des moteurs internes exposant encore des API `void`, la politique de defaults au restore (`D-017`), les mesures et `D-019` : les sources/scripts NoteFx référencés par `tests/CMakeLists.txt` manquent au checkout.
- **Étape 2 : CONFORME sur la structure.** `NOTE_FX_SLOT_COUNT=3`, `PARAM_COUNT=319`, p-lock MIDI FX `12`, runtime p-lock `90`, UI S1..S3 et recherches négatives S4 sont cohérents. Aucun appel audio ni alias S4 n’a été ajouté. Les preuves dynamiques restent dépendantes de `D-019`.
- **Étape 3 : finalisée après correction.** Les changements UI, p-lock, Pattern/Project v7, snapshots, clipboard et priorité `MODEL` avant paramètres dépendants sont conservés. Toute extension Undo/Redo des paramètres ou modèles MIDI FX a été retirée : `undo_v2` reste séquence-only.

La passe corrective 1R est maintenant engagée avant toute étape 4 et avant toute implémentation EUCLID. Elle n’implémente ni modèle, ni masque, ni runtime EUCLID.

## État après corrective 1R

Contrôle effectué sur le HEAD réel `2a4cb752d` (`perf: specialize common SVF filter paths`). La correction D-017 est appliquée : un restore qui change le modèle charge le tuple de defaults du modèle cible pour chaque slot, tandis qu’un restore dans le même modèle conserve les valeurs valides. Le scheduler expose désormais explicitement son adapter d’admission interne : pour les anciens backends `void`, l’admission signifie une lease interne bornée par le scheduler et ne prétend pas être un acquittement matériel.

Le CMake des tests ne référence plus des sources absentes. Un test de restore et une validation statique de l’adapter, du terminal indépendant, de la file owner, du seam sample et du compteur clock sont enregistrés. Le test C est compilable et linkable avec la toolchain H743 ; l’exécution hôte et la matrice dynamique des quatre combinaisons/interleavings restent à fournir, ainsi que les mesures DWT sur les deux cibles.

## 1. Verdict

### `NON PRÊT POUR EUCLID — 1R PARTIELLE`

Le HEAD fournit une base exploitable pour planifier les trois slots et EUCLID, mais il ne faut pas poser EUCLID sur le socle sans fermer d’abord plusieurs dettes du nettoyage :

- les backends internes historiques restent `void`, mais le terminal passe désormais par un adapter d’admission explicite (`seq_play_scheduler_admit_internal_note()`) dont la sémantique de lease interne est documentée ; la saturation réelle et l’acquittement backend restent à mesurer (D-014) ;
- le chemin live publie désormais `NOTE_FX_SAMPLE_TIME_AUDIO_OWNER` et l’owner résout le sample au retrait de la commande ; la trace temporelle réelle et sa marge restent à exécuter (D-009) ;
- l’autorité de génération courante est contrôlée à l’admission d’un nouvel On FX et les wrappers terminal pitch-based ont été supprimés ; la matrice stale reste à exécuter (D-004/D-018) ;
- le pending clock externe est désormais 32 bits et compte ses overflows ; la borne de rattrapage reste 4 pulses par bloc et sa marge H743 reste à mesurer (D-010) ;
- la validation dynamique des interleavings owner, des réserves Off, des quatre combinaisons d’admission et des capacités H743 n’est pas démontrée ;
- le test C de restore est compilable/linkable avec la toolchain H743, mais aucun compilateur hôte n’est disponible dans l’environnement courant pour exécuter le binaire sur Windows ;
- la preuve dynamique des quatre combinaisons d’admission, des interleavings owner et des capacités H743 n’est pas encore démontrée ;
- les defaults cibles sont maintenant imposés par `note_fx_state_restore_track()` quand le modèle restauré diffère du modèle courant, conformément à D-017.

Les briques déjà présentes et réutilisables sont néanmoins réelles : événement canonique de 32 octets, stages ordonnés `0 → 1 → 2 → 3 → terminal 4`, owner audio et file fixe de commandes, ledger par occurrence au terminal, admission MIDI séparée par destination, réservation de paires NoteFx, budgets ouverts au niveau demi-buffer et mute STEP non destructif. Ces garanties doivent être prouvées par tests, pas seulement conservées parce que les documents les déclarent.

Conséquence d’exécution : l’étape 1 est un prérequis obligatoire. Les étapes 2 et 3 peuvent ensuite retirer le quatrième slot avant l’ajout du modèle, mais aucun runtime EUCLID ne doit être introduit avant la clôture des dettes requises de l’étape 1.

## 2. État du nettoyage NoteFx/scheduler

### Sources confrontées

L’audit a recoupé `Inc/NoteFx/*`, `Src/NoteFx/*`, `Src/Seq/seq_play_scheduler.c`, `Src/Seq/seq_runtime*.c`, `Src/Keyboard/keyboard_engine.c`, `Src/MIDI/midi.c`, les moteurs internes appelés par le terminal, le registre de paramètres et `track_runtime`, l’interface p-lock, la page MIDI FX, snapshots/clipboard/Pattern/Project/Undo, les tests présents dans l’arbre, les documents Z1/Z4, `docs/plan_note_fx_scheduler_cleanup.md` et `docs/audits/note_fx_scheduler_cleanup_final_audit.md`.

### Vérification par ancien écart

| Dette / contrat | État du code courant | Qualification pour EUCLID |
|---|---|---|
| Événement canonique, occurrence, provenance, stage | `note_event_t` contient sample, piste, destination, note, vélocité, type, provenance, stage, token, occurrence, génération. | Structure présente ; test de bout en bout encore requis. |
| Identité source et retrigger | ARP et ledgers utilisent token + génération ; le terminal conserve `(track, occurrence_id, generation)`. | Présent structurellement ; vérifier l’autorité de génération au nouvel On et les backends. |
| Timestamp audio | STEP utilise le sample d’événement ; le live prend le sample de timeline avant consommation de la commande. | D-009 ouverte ; prérequis obligatoire. |
| Owner runtime | `g_note_fx_commands[32]`, mutations owner audio, snapshots de piste et budgets de demi-buffer existent. | Présent structurellement ; interleavings et temps de section critique non mesurés. |
| Chaîne | `note_fx_pipeline_stage_emit()` continue au stage suivant ; seul le stage terminal appelle le scheduler. | La chaîne courante à trois slots est `0 → 1 → 2 → 3 → terminal` ; tests d’ordre et de continuation encore requis. |
| Note Off et réserves | Fermetures globales avant les nouveaux On, réserve Off et owned conservé si refus. | Contrat présent ; exhaustion et retry non prouvés. |
| Terminal | Masques interne/MIDI distincts, ledger exact et MIDI USB avec réserve Off/génération de connexion. | Adapter interne explicite ; admission réelle des backends `void` et matrice dynamique encore à mesurer (D-014). |
| Clock/source switch | `seq_runtime_set_clock_source()` demande la transition `SOURCE_SWITCH` avant de modifier l’autorité clock. | Correctif structurel présent ; matrice dynamique et pending saturé à tester. |
| Mute | `MUTE_TRIGS` suspend les nouveaux STEP sans purge des occurrences ou deadlines existantes. | Conforme à la décision produit ; tester ARP et futur EUCLID. |
| Defaults | `note_fx_state_set_param()` réinitialise le slot lors d’un changement de MODEL ; restore applique les defaults cibles lors d’un changement de modèle, avec une API exacte réservée au rollback transactionnel. | D-017 fermée structurellement ; matrice restore/clipboard/p-lock encore à exécuter. |
| Admission moteur | Les appels internes historiques restent `void`, mais passent par `seq_play_scheduler_admit_internal_note()` et une lease explicite bornée. | D-014 traitée structurellement ; saturation/backend réel à mesurer avant EUCLID. |
| Tests | Le CMake courant ne référence que le test C de restore, sa validation statique et des fichiers présents. | D-019 assainie ; exécution hôte et matrice dynamique restent ouvertes. |
| Documentation/code mort | Wrappers `seq_play_scheduler_dispatch_terminal_note[_to_channel]` supprimés ; les audits historiques sont complétés par des addenda datés. | D-018 traitée ; consolidation documentaire conservée. |

Le plan de nettoyage historique indique que les anciens problèmes « premier ARP », ledger `[track][note]`, budget recréé par sous-segment et mute destructif ont été corrigés. Le code courant confirme les stages, les ledgers et le budget demi-buffer, mais le verdict reste conditionné aux tests et aux deux défauts de contrat relevés ci-dessus. `docs/plan_midi_fx_euclid.md` n’est pas modifié par ce livrable.

## 3. Décisions produit figées

- Chaque piste Play possède exactement trois slots MIDI FX : 1, 2 et 3.
- L’ancien slot 4 sort du domaine MIDI FX. Il est seulement réservé, dans une éventuelle topologie supérieure, à un futur FX audio. Aucun moteur, paramètre, page, insert mixer, modèle audio ou ID réaffecté n’est créé ici.
- EUCLID est disponible dans chacun des trois slots et reçoit STEP, clavier interne, MIDI externe et les sorties des modèles précédents compatibles. Il ne connaît ni moteur sonore ni sortie MIDI.
- La chaîne cible est `source → MIDI FX 1 → MIDI FX 2 → MIDI FX 3 → terminal post-FX commun`.
- Chaque slot garde quatre contrôles logiques : paramètre 1, paramètre 2, paramètre 3, MODEL. Pour EUCLID : `LENGTH`, `PULSE`, `DIV`, `MODEL=EUCLID`.
- Aucun `ROTATE`, `GATE`, page supplémentaire, second niveau ou p-lock Euclid de `LENGTH`, `PULSE`, `DIV` en V1. MODEL conserve le comportement p-lock actuel, avec une politique model-aware centralisée.
- `LENGTH` vaut 1..64, défaut 16. `PULSE` vaut 0..LENGTH, défaut 4. Si la longueur diminue sous PULSE, l’autorité publiée vaut `min(PULSE, LENGTH)`.
- `DIV` utilise l’autorité canonique de divisions musicales existante et le défaut `1/16`.
- `PULSE=0` laisse le modèle actif mais produit un cycle silencieux.
- Seules les notes dont le Note On n’a pas reçu le Note Off correspondant alimentent EUCLID. Il n’y a ni latch, ni exception STEP, ni replay au démute.
- Chaque occurrence EUCLID dure exactement une période de DIV. À sample égal, un Off est traité avant un nouvel On.
- Les trois instances peuvent être EUCLID simultanément si les bornes et mesures H743 le permettent. Toute limite inférieure doit être mesurée et documentée.
- Les admissions interne et MIDI restent indépendantes ; un Off vise uniquement les destinations qui ont admis l’On.
- Les formats courants V1 sont réécrits de façon cohérente. Aucune migration historique Pattern/Project/Patch/Kit n’est requise ni à créer.
- Live Record post-FX reste hors périmètre ; seul le seam terminal commun doit rester observable par un futur chantier.

## 4. Cartographie actuelle des slots MIDI FX

### Autorités et consommateurs

| Zone | Fichier / symbole courant | Rôle et consommateurs | Hypothèse actuelle |
|---|---|---|---|
| Contrat de cardinalité | `Inc/NoteFx/note_fx_state.h`, `NOTE_FX_SLOT_COUNT` | Dimensionne l’état par piste, les boucles d’initialisation et les appels state/pipeline/engine. | 3 slots dans le HEAD contrôlé. |
| État de base | `note_fx_track_state_t.value[track][slot][param]` | Base persistée dans Pattern, copiée dans snapshots et utilisée par restore/duplication. | `sizeof(note_fx_track_state_t)=12` octets par piste. |
| Modèles | `note_fx_model_t`, `g_note_fx_model_defaults` | Defaults, clamp, validation, reset de MODEL. | OFF et ARP seulement ; aucun EUCLID. |
| Runtime | `Src/NoteFx/note_fx_engine.c:g_slot[8][NOTE_FX_SLOT_COUNT]` | ARP, owned, génération et deadline par instance. | Map Release/Premium : `0x3240` = 12 864 octets, soit 24 instances de 536 octets ; pas de masque Euclid. |
| Overrides | `note_fx_pipeline.c:g_note_fx_override_valid/value` | Valeurs de base/p-lock/runtime envoyées à l’owner. | Map : `0x60` = 96 octets par tableau, 192 cumulés pour 8×3×4. |
| Pipeline | `note_fx_pipeline_stage_emit()` et `note_fx_pipeline_process()` | Stage source, continuations, budget ON/OFF, terminal. | Stage source + trois FX + terminal stage 3. |
| Paramètres | `Param/param_store.h`, `PARAM_MIDI_FX_S1...S4_*` | IDs contigus, `PARAM_COUNT`, registre et `track_runtime`. | 16 IDs MIDI FX après les paramètres persistants. |
| Registre | `Src/Param/param_registry_catalog.c` | Quatre descripteurs par slot, labels RATE/STYLE/RANGE/MODEL. | Quatre pages et deux modèles. |
| Runtime param | `Src/Core/track_runtime.c` | Domaine et capacité MIDI FX pour tous les S1..S3. | Les trois slots sont dans le même domaine ; aucun S4 fonctionnel. |
| P-lock | `Inc/Seq/seq_types.h`, `Src/Seq/seq_param_iface.c` | Pool compact, inverse mapping, offsets, flags base/runtime et apply lock. | 12 positions MIDI FX, offset 74, runtime total 90 ; map : état `0xB40` = 2 880 octets, flags `0x5A` = 90 octets. |
| UI | `Src/UI/pages/ui_page_midi_fx.c` | Navigation SLOT1..SLOT3 ; l’infrastructure générique peut garder une quatrième subpage non adressable. | Trois slots affichés et adressables. |
| Clipboard | `Src/UI/ui_core_clipboard.c`, `track_snapshot.c` | Clipboard page/ensemble par IDs et clipboard piste par `track_snapshot_t`. | Les IDs et la structure snapshot portent encore S4. |
| Persistence | `Inc/Storage/pattern_live_ram.h:PatternSaveV1`, `project_v1.h` | `note_fx[8]`, indirectement inclus dans Pattern/Project ; chargement vérifie `sizeof(PatternSaveV1)`. | Mesure compilée : `PatternSaveV1.note_fx=96` octets ; `sizeof(PatternSaveV1)=110 332` octets. |
| Reset / duplication | `pattern_live_ram.c`, `track_snapshot.c`, `param_registry*` | Capture, normalize, reset et application à l’owner. | Boucles `NOTE_FX_SLOT_COUNT` et état complet à revalider. |
| Undo/Redo | `Src/Storage/undo_v2.c` | Snapshots structurels des steps. | Aucun état NoteFx de base pris en charge actuellement. |
| Capacité de piste | `track_topology` et `TRACK_CAPABILITY_MIDI_FX` | Détermine les pistes Play autorisées. | La capacité « possède des MIDI FX » reste pertinente ; elle ne doit pas encoder le nombre 4. |

### État d’exécution observé

Le chemin actuel vérifié est :

```text
STEP / clavier / MIDI
  → événement canonique stage 0
  → slot 0 (stage 0)
  → slot 1 (stage 1)
  → slot 2 (stage 2)
  → slot 3 (stage 3)
  → terminal stage 4
```

Un événement non transformé est continué au stage suivant. Un ARP produit des événements FX différés et les transmet en aval ; aucun appel direct moteur/MIDI n’est dans `Src/NoteFx`. Le target à trois slots gardera exactement la même mécanique avec les stages FX `0,1,2` et le terminal `3`.

## 5. Passage de 4 à 3 slots

### Règle d’architecture

Le changement doit être une réduction réelle de cardinalité, pas un quatrième slot caché. La source de vérité devient `NOTE_FX_SLOT_COUNT=3`. Toutes les boucles MIDI FX, tableaux, mapping, snapshots, reset, UI et validations doivent s’arrêter à 3. Les quatre subpages éventuelles de l’infrastructure UI générique ne constituent pas un slot MIDI FX : la famille MIDI FX ne doit plus leur associer d’ID S4 ni les rendre sélectionnables.

Les anciens IDs S4 ne sont pas réutilisés pour l’audio. Comme le projet est en prototypage, la table V1 courante est recompilée et les payloads qui ne correspondent plus à `sizeof(PatternSaveV1)` sont refusés par le chargeur courant ; aucune migration ou conversion silencieuse de l’ancien slot 4 n’est ajoutée.

### Quantification relevée sur le HEAD contrôlé

Les valeurs ci-dessous viennent des types compilés et des maps Release Low-Cost/Premium disponibles ; aucune économie non mesurée n’est présentée comme une mesure.

| Ressource | Référence 4 slots | HEAD contrôlé à 3 slots | Mesure / variation |
|---|---:|---:|---:|
| État NoteFx par piste | HEAD : `sizeof(note_fx_track_state_t)=16` | HEAD contrôlé : `sizeof(note_fx_track_state_t)=12` | 4 octets/piste, 32 octets pour 8 pistes. |
| `PatternSaveV1.note_fx` | 128 octets | 96 octets | 32 octets par objet Pattern ; `sizeof(PatternSaveV1)=110 332` octets mesuré. |
| `track_snapshot_t.note_fx` | 16 octets | 12 octets | `sizeof(track_snapshot_t)=17 900` octets mesuré ; 4 octets par snapshot. |
| Runtime `g_slot` | comparaison historique non mesurée ici | map Release/Premium : 24 × 536 = 12 864 octets (`0x3240`) | `sizeof(note_fx_slot_runtime_t)=536` octets déduit de `g_slot[8][3]` et confirmé par map. |
| Overrides pipeline | 2 × 8 × 4 × 4 octets | 2 × 8 × 3 × 4 = 192 octets | économie mesurée par dimensions : 64 octets. |
| P-lock runtime state | comparaison historique non mesurée ici | 90 × 8 × 4 = 2 880 octets (`0xB40`) | flags base/runtime : 90 octets chacun (`0x5A`). |
| Positions p-lock MIDI FX | 16 par piste | 12 par piste | 4 adresses par piste, soit 32 adresses logiques. |

Les lectures de map après la correction courante donnent `RAM_D2=102 144` octets
pour Release Low-Cost et `RAM_D2=108 640` octets pour Premium. Aucun map propre
du HEAD quatre slots n’est disponible dans l’arbre pour calculer une différence
totale fiable ; seule la variation par dimensions/types ci-dessus est affirmée.
| IDs paramètres MIDI FX | S1..S4 = 16 | S1..S3 = 12 | 4 IDs et 4 descripteurs ; `PARAM_COUNT` attendu 323 → 319 si aucun autre tail change. |

Le runtime Euclid ajoutera ses champs par instance ; l’économie du slot retiré ne doit donc pas être annoncée comme une marge nette avant mesure de l’implémentation. Le budget doit comparer le target complet « 3 slots dont 0..3 EUCLID » au HEAD courant.

### Ordre de suppression

1. Réduire les constantes et structures de données ; supprimer les IDs S4 au lieu de les laisser morts.
2. Recalculer les offsets p-lock et `PARAM_COUNT` par assertions ; aucune position réservée S4 ne doit rester dans les masques ou inverse tables.
3. Réduire les boucles et synchronisations engine/pipeline/restore/reset/snapshot.
4. Refaire la famille UI et ses IDs uniquement pour S1..S3.
5. Vérifier le payload Pattern/Project V1 courant, les tailles de snapshots, clipboard et duplication.
6. Ajouter les recherches négatives et tests qui échouent sur toute référence fonctionnelle à `S4`, `SLOT4` ou à une capacité MIDI FX égale à 4. Les références documentant explicitement le futur emplacement audio peuvent rester.

## 6. Architecture cible des trois slots

Par piste, le runtime fixe possède trois instances indépendantes :

```text
source/stage 0
  → instance MIDI FX 1
  → instance MIDI FX 2
  → instance MIDI FX 3
  → terminal post-FX commun
```

Chaque instance possède son propre modèle, configuration effective, génération, phase, deadline, ledger de notes strictement actives et ledger de sorties possédées. Aucun état de phase ou de notes actives n’est partagé entre slots, même si les paramètres sont appliqués dans une commande de piste atomique.

Le type `note_event_t` reste le contrat entre les étapes : sample absolu, piste, destination, note, vélocité, kind, provenance, stage, source token, occurrence et génération. Les événements générés par EUCLID prennent le chemin de continuation déjà utilisé par les événements ARP. Ils ne rebouclent jamais au stage 0 et ne contournent jamais le terminal.

Le propriétaire audio reste le seul écrivain des phases, ledgers, deadlines et owned. UI, param registry, p-lock, restore, clipboard et transport publient des commandes fixes bornées. Le terminal demeure l’autorité de l’admission globale ; EUCLID ne possède ni quota polyphonique global ni file de sortie.

Le slot audio futur n’existe pas dans cette architecture. Il est seulement une réservation documentaire/topologique éventuelle, indépendante du tableau MIDI FX et des IDs présents dans le format courant.

## 7. Modèle et paramètres Euclid

### Schéma logique

Le schéma de base reste quatre octets logiques par slot :

| Position | ARP actuel | EUCLID V1 | Règle |
|---|---|---|---|
| Paramètre 1 | RATE | LENGTH | Euclid : entier 1..64, défaut 16. |
| Paramètre 2 | STYLE | PULSE | Euclid : entier 0..LENGTH, défaut 4. |
| Paramètre 3 | RANGE | DIV | Euclid : ordinal de l’autorité de division, défaut 1/16. |
| MODEL | ARP/OFF | EUCLID | Sélecteur générique. |

La base appliquée, et non la valeur brute reçue par une UI ou un p-lock, est l’autorité. Toute application de `LENGTH` recalcule immédiatement `PULSE=min(PULSE,LENGTH)` avant publication. Cette valeur effective est celle relue par UI, sauvegardée, copiée et dupliquée ; les paramètres MIDI FX restent hors Undo/Redo.

Le catalogue `Seq/seq_division_catalog` est la seule autorité de labels, rapports et conversion sample. Le runtime ne doit pas copier la table ARP actuelle dans EUCLID. Le défaut doit être exprimé par l’ordinal canonique correspondant à `1/16`, pas par une valeur sample ou un index local non documenté.

### Defaults et changement de modèle

Le registre des modèles doit devenir une autorité centrale : domaine, defaults complets, clamp inter-paramètres, labels et politique de p-lock. Un changement de MODEL construit un snapshot complet du slot cible :

```text
ARP personnalisé → EUCLID → LENGTH=16 / PULSE=4 / DIV=1/16
EUCLID personnalisé → ARP → defaults ARP
```

Aucune valeur personnalisée de l’ancien modèle ne doit être conservée dans une banque parallèle ni réinterprétée. Restore, clipboard, duplication, p-lock MODEL et paramètre direct doivent passer par cette même normalisation.

### Paramètre Euclid et changement de modèle : deux contrats distincts

Une modification de `LENGTH`, `PULSE` ou `DIV` à l’intérieur du modèle EUCLID est une modification de configuration du même slot : elle conserve le modèle, applique le clamp concerné, reconfigure le runtime selon la section 9 et ne doit jamais être traitée comme un changement de modèle. En V1, ces paramètres ne sont pas p-lockables et aucune modification de paramètre ou de modèle MIDI FX n’est enregistrée dans Undo/Redo.

Une modification de `MODEL` est une transition de modèle : elle charge exclusivement les defaults complets du modèle cible, ferme et purge le runtime du slot concerné selon la politique de la section 12, puis publie un état normalisé. Elle est p-lockable selon le contrat central, mais reste elle aussi hors Undo/Redo. Dans les deux cas, save/load, reset, duplication et clipboard transportent l’état de base normalisé ; aucun runtime n’est persisté.

## 8. Sémantique des notes strictement actives

Une instance EUCLID garde un tableau fixe borné de sources actives, au minimum de capacité 16 comme l’ARP actuel. Une entrée contient :

- `source_token` ou identité équivalente du producteur ;
- provenance KEY, STEP, MIDI ou FX ;
- hauteur et vélocité ;
- génération de la source ;
- occurrence/source de fermeture et destination nécessaires à la paire exacte ;
- état actif.

Un Note On crée ou met à jour une entrée seulement si son identité n’est pas déjà active. Un Note Off retire exactement l’entrée correspondant au token et à la génération, jamais toutes les notes de même hauteur. Deux producteurs ou deux retriggers de même pitch restent donc distincts.

À chaque pulse, EUCLID échantillonne uniquement les entrées encore actives à ce sample. Une note longue peut être choisie à plusieurs pulses ; une note courte peut ne croiser aucun pulse ou un seul ; un accord est traité entrée par entrée ; un relâchement retire la note des pulses suivants. Lorsque la dernière source disparaît, l’instance cesse de planifier des pulses et revient à un état idle. Il n’existe aucun latch implicite.

Les sources STEP, clavier et MIDI utilisent le même contrat de fermeture avec leur provenance propre. Le mute STEP bloque l’admission des nouveaux On STEP à l’entrée scheduler, mais ne retire pas une entrée déjà active ; les sources live gardent leurs politiques distinctes.

## 9. Masque, phase et horloge

### Masque

La recherche dans le HEAD n’a trouvé ni `EUCLID`, ni `Bjorklund`, ni masque rythmique existant réutilisable. L’implémentation à prévoir est une fonction pure et testable de type `euclid_build_mask(length, pulse)` dans un module NoteFx dédié.

Contrat V1 :

- résultat fixe `uint64_t` ou équivalent sans allocation ;
- `length` bornée à 1..64 et `pulse` normalisé à 0..length ;
- `pulse=0` donne zéro bit ; `pulse=length` donne les `length` premiers bits à 1 ;
- algorithme déterministe, borné et pré-calculé hors chemin chaud ;
- convention canonique : le premier pulse est ramené à la position 0 ;
- aucune rotation configurable et aucun calcul Bjorklund/division dans le traitement de chaque pulse ;
- recalcul seulement sur changement effectif de LENGTH ou PULSE.

Le test de masque doit fixer des vecteurs de référence pour les longueurs et pulses représentatifs, y compris 1, 2, 3, 4, 8, 16, 64, zéro, égalité et clamp. La convention choisie doit être écrite dans le test, pas déduite d’une forme visuelle.

### Phase et deadlines

Règle recommandée et déterministe :

1. La phase est un entier zéro-based dans `[0, LENGTH)` propre à l’instance.
2. Lorsqu’une instance idle reçoit son premier Note On actif, la phase devient 0 et le premier examen du masque est ancré au sample du Note On.
3. À chaque deadline, le owner lit le bit de la phase courante, génère éventuellement les On pour les sources actives, puis avance la phase modulo LENGTH et programme la deadline suivante avec la période actuelle de DIV.
4. `PULSE=0` avance la phase et les deadlines normalement mais ne génère aucune occurrence.
5. L’ajout ou le retrait d’une source ne réinitialise pas la phase tant qu’au moins une source reste active. Après extinction complète, l’instance redevient idle et le prochain Note On recommence à la phase 0. Le mute seul ne réinitialise rien.
6. Un changement de LENGTH ou PULSE ferme les sorties de cette instance au sample de configuration, invalide ses anciennes deadlines, normalise PULSE, reconstruit le masque, remet la phase à 0 et peut reprendre au même sample avec les sources toujours strictement actives ; les Off de fermeture précèdent les nouveaux On.
7. Un changement de DIV applique la même fermeture/invalidation, conserve les sources encore actives, remet la phase à 0 et programme le premier examen au sample de configuration avec la nouvelle période ; l’ancienne échéance ne peut plus produire d’événement.
8. Chaque occurrence stocke son `off_sample` au moment de l’On. Un changement de tempo ne déplace pas un Off déjà possédé ; il influence les deadlines futures. Une source clock changeante passe d’abord par la transition scheduler validée, jamais par une horloge parallèle Euclid.

Cette règle évite le retrigger caché, garantit Off-before-On au sample de reconfiguration et rend chaque deadline absolue, bornée et stale-rejectable. Si la mesure ou le produit impose une autre ancre de reprise, elle doit être décidée avant l’étape 6 et ajoutée aux vecteurs de référence.

## 10. Note On/Off et fermetures

Pour chaque bit actif du masque et chaque source active :

1. l’owner réserve un child occurrence/token et un crédit de fermeture ;
2. il émet un Note On FX avec `stage=slot+1`, provenance FX, parent/source token, occurrence et génération valides ;
3. le downstream continue l’événement au slot suivant ou au terminal ;
4. l’instance garde le child dans son ledger owned avec la période exacte et `off_sample` ;
5. à `off_sample`, elle émet le Note Off correspondant au même child occurrence/génération, avec priorité sur tout nouvel On au même sample ;
6. l’owned n’est libéré qu’après un acquittement de fermeture ou un rejet stale explicitement prouvé.

Un Note On refusé par le budget ou par toutes les destinations ne crée pas de ledger actif orphelin. Un refus d’une destination terminale n’annule pas l’autre ; le terminal mémorise le masque réellement admis et n’envoie ensuite l’Off qu’à ces destinations. Le moteur interne doit toutefois retourner une admission réelle avant l’étape Euclid : le retour supposé `1` des APIs `void` n’est pas acceptable.

La génération d’instance et la génération de piste doivent être comparées au point d’admission. Une fermeture stale peut être absorbée sans toucher une occurrence nouvelle ; un ancien Off ne doit jamais couper une occurrence qui réutiliserait un pitch ou un token numérique après wrap. Les compteurs de refus, stale, orphan et pending closure doivent être bornés et observables en diagnostic.

## 11. Chaîne, multi-Euclid et saturation

### Chaîne

La continuation obligatoire est :

```text
stage 0 source → slot 1
stage 1 → slot 2
stage 2 → slot 3
stage 3 → terminal post-FX
```

Un slot OFF, un événement passthrough et un événement généré suivent le même stage contract. Aucun généré n’est renvoyé au slot 1 et aucun slot ne passe directement au terminal. L’identité parent/child et la génération restent dans l’événement complet.

### EUCLID → EUCLID

Chaque instance conserve masque, phase, sources actives et owned séparés. Le Note On produit par le premier EUCLID devient une source FX strictement active du second jusqu’à son Off exact ; il peut donc être pulsé ou supprimé par le second sans partager son état. Le test doit couvrir trois EUCLID chaînés, ARP avant EUCLID, EUCLID avant ARP et modèles passthrough entre eux.

Avec 16 sources par instance, le maximum structurel nominal est 16 sorties simultanées par instance, 48 par piste pour trois EUCLID, avant de compter les autres sources/occurrences terminales. Le terminal courant est borné à 64 occurrences par piste et le pipeline à 8 On générés par piste, 32 Off réservés et 32 commandes par demi-buffer. Ces chiffres ne prouvent pas que le worst case est admissible : trois Euclid à DIV rapide peuvent dépasser le quota On, la file scheduler, le ledger terminal ou le temps H743.

### Saturation

La saturation doit être explicite et sans note pendante :

- réserver atomiquement l’On et sa fermeture avant publication ;
- refuser la paire complète si la réserve ou le downstream ne peut pas la porter ;
- conserver les sources strictement actives pour un pulse ultérieur, sans inventer de latch ni réémettre rétroactivement ;
- donner priorité aux Off déjà possédés et permettre des retries bornés ;
- compter les refus par piste, slot, cause et destination ;
- vérifier que la saturation ne pousse pas EUCLID à appeler directement le moteur/MIDI ni à changer la polyphonie globale.

Aucun plafond « un seul Euclid » ne doit être ajouté pour simplifier le problème. Si trois instances par piste ou huit pistes ne tiennent pas la borne H743, la limitation doit être le résultat de mesures reproductibles, avec la combinaison qui échoue et la marge obtenue documentées.

## 12. UI, p-locks et changement de modèle

### UI

La famille MIDI FX normale expose SLOT1, SLOT2 et SLOT3 uniquement, avec quatre contrôles logiques par slot. La navigation ne doit jamais former l’ID S4 ni accéder à une quatrième subpage MIDI FX. L’infrastructure générique pouvant contenir quatre subpages n’est pas une autorité de cardinalité ; elle ne doit pas rendre une quatrième subpage MIDI FX sélectionnable.

Le rendu de la page est model-aware : RATE/STYLE/RANGE pour ARP, LENGTH/PULSE/DIV pour EUCLID. Aucun contrôle ROTATE/GATE, aucune page Euclid supplémentaire et aucun label de futur audio n’est ajouté dans ce chantier. Les valeurs affichées sont l’état effectif normalisé, notamment PULSE après clamp.

### P-locks

Le domaine compact MIDI FX passe de 16 à 12 positions. Les quatre IDs S4, leurs inverse mappings, assertions, masks et capacité runtime disparaissent. Une recherche fonctionnelle négative doit prouver qu’aucun slot 4 n’est addressable, restaurable ou appliqué.

Le refus Euclid de LENGTH/PULSE/DIV doit être central et model-aware, par exemple dans l’API de support/apply p-lock de `seq_param_iface`, avec piste, slot et modèle effectif disponibles. Le visuel ne peut être qu’un reflet. Un p-lock refusé ne modifie ni la base, ni le runtime, ni le masque. MODEL reste p-lockable selon le contrat existant et déclenche la transaction de changement de modèle.

### Mute et changement de modèle

Le mute STEP reste un mute de trigs : nouveaux On STEP bloqués, sources déjà acceptées et leurs deadlines conservées, Off transmis, aucune purge/génération/retrigger au démute. Clavier et MIDI live ne sont pas assimilés au mute STEP.

Un changement de modèle ferme les occurrences possédées de l’ancien modèle, invalide ses deadlines, purge uniquement le runtime du slot concerné, charge les defaults complets de la cible et publie la configuration atomiquement. Il ne purge pas les deux autres slots et ne réinjecte pas silencieusement les notes déjà consommées ; une nouvelle source est nécessaire après la reconfiguration. Les changements LENGTH/PULSE/DIV suivent la politique moins destructive de la section 9 et conservent les sources réellement actives.

## 13. Persistance, clipboard et Undo/Redo

Le format courant persiste uniquement les valeurs de base des trois slots : 3 × 4 octets par piste. Aucun masque, phase, deadline, source active, owned, token, génération runtime, file ou compteur de saturation ne doit entrer dans Pattern, Project, Patch, Kit, snapshot ou clipboard.

`PatternSaveV1.note_fx`, `track_snapshot_t.note_fx`, capture/apply, duplication et reset doivent utiliser la nouvelle structure. Project V1 transporte Pattern par composition ; les vérifications de taille et buffers temporaires doivent suivre `sizeof(PatternSaveV1)`. Un ancien payload quatre slots n’est pas migré : il est incompatible avec le format V1 courant et doit être rejeté/initialisé selon le contrat de chargement existant, jamais interprété en recréant S4.

Le clipboard piste/ensemble/page doit copier les trois slots par les APIs communes et repasser par la normalisation modèle. L’ordre de collage d’un slot est MODEL/defaults puis valeurs autorisées du modèle, sans réintroduire les valeurs du modèle source. Le clipboard séquence ne doit pas inventer une persistance de runtime Euclid.

L’Undo v2 reste limité aux snapshots de steps. Les paramètres et modèles MIDI FX, y compris `MODEL`, `LENGTH`, `PULSE` et `DIV`, sont explicitement hors Undo/Redo ; aucune transaction NoteFx et aucune capture du runtime ne doit être ajoutée. Cette exclusion ne s’applique pas aux contrats de save/load, reset, duplication et clipboard, qui restent obligatoires et utilisent l’état de base normalisé.

## 14. Point futur Live Record post-FX

Live Record post-FX est hors chantier : aucune capture, réinjection, mode REC, bounce ou nouvelle piste n’est ajoutée.

Le seul contrat à préserver est l’unicité du terminal post-FX. Les On/Off EUCLID y arrivent comme événements canoniques après le troisième slot, avec occurrence, génération, sample et provenance. Le seam `note_fx_pipeline_terminal()` / dispatch terminal doit rester une frontière stable et observable par un futur Live Record sans appel direct depuis EUCLID.

## 15. Risques et invariants

| Invariant | Risque actuel / risque d’implémentation | Oracle attendu |
|---|---|---|
| Cardinalité réelle = 3 | S4 oublié dans une enum, un switch, une table ou une taille. | Assertions, `rg` négatif fonctionnel, test UI/p-lock/persistence. |
| Aucun runtime persistant | Ajouter phase/owned dans Pattern par facilité. | `sizeof`/inspection des structures et restore runtime reconstruit. |
| Identité exacte | Revenir à pitch ou note comme clé dans un nouveau modèle. | Deux mêmes pitches, retrigger, sources simultanées, Off exact. |
| Strict-active | Latcher la dernière note après son Off ou réinjecter au changement de modèle. | Note courte/longue, accord relâché partiellement, modèle reconfiguré. |
| Off exact | Deadline globale ou ancien Off non invalidé. | `off_sample=on_sample+period(DIV)`, stale rejection et Off-before-On. |
| Phase par instance | Partager la phase entre slots ou pistes. | Trois Euclid à paramètres différents, changement d’une seule instance. |
| Masque déterministe | Rotation implicite ou calcul chaud non borné. | Vecteurs purs, coût hors pulse, 64 bits fixes. |
| Owner unique | UI/live/restore touchent directement les ledgers. | Recherche négative, interleavings, queue high-water. |
| Terminal unique | EUCLID appelle moteur/MIDI ou contourne le stage. | Recherche négative et trace stage 0..3→terminal. |
| Admission indépendante | `BOTH` exige les deux destinations ou invente une admission. | Quatre combinaisons internal/MIDI et ledger/Off par masque. |
| Mute non destructif | Mute assimilé à stop/panic. | Mute long avec source STEP active, Off, démute sans retrigger. |
| Reconfiguration ciblée | Changer un modèle nettoie les trois slots ou conserve une valeur ARP. | Deux slots actifs, changement du troisième, defaults complets. |
| Budget demi-buffer | Budget recréé par sous-segment ou pair sans réserve Off. | 64 frames, fragmentation maximale, saturation et retries. |
| H743 temps réel | Le worst case 3×8 Euclid est supposé. | DWT max/p99, marge, IRQ audio et absence d’underrun Low-Cost/Premium. |

## 16. Plan d’action par étapes

Les étapes fonctionnelles requièrent les builds `Release Low-Cost` et `Release Premium`. `TestPremium` est explicitement exclu. Chaque étape doit conserver les modifications hors périmètre et ne committer que son propre périmètre.

### Étape 1 — Fermer les prérequis résiduels du nettoyage

- **Objectif :** rendre le contrat NoteFx/scheduler assez fiable pour recevoir un générateur ; aucune implémentation EUCLID.
- **Dettes/prérequis :** D-004, D-009, D-010, D-014, D-017 résiduelle, D-018, D-019 ; validations dynamiques D-001/D-002/D-007/D-008/D-011/D-012/D-013/D-015.
- **Fichiers/symboles probables :** `Src/Seq/seq_play_scheduler.c`, `Inc/Seq/seq_play_scheduler.h`, `Src/Keyboard/keyboard_engine.c`, `Src/Seq/seq_runtime_exec.c`, adapters/admissions des moteurs, `Src/MIDI/midi.c`, `Src/NoteFx/note_fx_pipeline.c`, `Src/NoteFx/note_fx_state.c`, wrappers terminal, `tests/CMakeLists.txt`, sources/scripts de tests, audits Z1/Z4.
- **Changements précis :** acquittement réel des backends internes ou adapter d’admission fixe ; sample d’application live explicite ; garde de génération au terminal ; compteur de clock abandonnée/coalescée non saturant silencieusement ; normalisation transactionnelle des defaults également sur restore ; suppression des wrappers morts après migration ; restauration et enregistrement des tests réellement présents ; correction des documents historiques contradictoires.
- **Invariants :** aucune admission supposée, aucune fermeture pitch-only, aucun ancien Off autorisé à couper une génération nouvelle, aucune perte clock non comptée, aucune mutation runtime hors owner.
- **Hors périmètre :** trois slots, EUCLID, audio FX, Live Record et toute migration de format.
- **Dépendances :** aucune ; cette étape bloque l’intégration du générateur.
- **Validations statiques/dynamiques :** recherche des appels moteur `void`, trace sample STEP/live, tests des quatre admissions, queue owner/interleavings, source switch, pending clock, restore/model, paire 0/1/2, Off réserve, transitions STOP/PANIC/MUTE.
- **Builds :** Release Low-Cost, Release Premium ; CTest et scripts effectivement présents, sans fabriquer de succès pour des fichiers absents.
- **Critères de fin :** aucun D-014 ou D-009 critique/haut non traité sur le chemin EUCLID ; matrice dynamique enregistrée ; docs Z1/Z4 et final audit cohérents avec le HEAD ; verdict de préparation réécrit dans le changelog de l’étape.
- **Documentation :** audits de dette et architecture scheduler/terminal, pas `docs/plan_midi_fx_3_slots_euclid.md` sauf mise à jour du statut d’exécution.
- **Commit recommandé :** `fix: close residual note fx scheduler debts`.

### Étape corrective 1R — Rouvrir les preuves de nettoyage avant EUCLID

- **Objectif :** fermer les écarts réellement constatés de l’étape 1 sans réécrire son commit et sans ajouter EUCLID.
- **État :** PARTIELLE ; D-017 est fermée structurellement, D-019 est assainie pour les fichiers présents et D-014 possède un adapter explicite, mais les preuves dynamiques et matérielles restent ouvertes.
- **Actions réalisées :** adapter d’admission interne documenté dans le seam scheduler ; defaults cibles imposés sur restore de modèle ; CMake réduit aux tests présents et validation statique enregistrée ; test C de restore ajouté ; builds Release Low-Cost/Premium et empreintes mémoire relevés. `Release` : FLASH 1 098 780, DTCMRAM 104 064, RAM_D1 416 320, RAM_D2 102 144 octets ; `Premium` : FLASH 1 086 184, DTCMRAM 105 088, RAM_D1 469 536, RAM_D2 108 640 octets.
- **Reste :** exécuter le test C avec une toolchain hôte, exercer les quatre combinaisons `internal/MIDI` et les interleavings owner sur un harness dynamique, puis relever DWT/high-water sur Low-Cost et Premium.
- **Critères de fin :** test host compilable et exécutable depuis le checkout, preuve dynamique des quatre combinaisons et interleavings owner, restore modèle conforme, mesures Low-Cost/Premium enregistrées. Le premier et le second critère restent ouverts dans l’environnement courant. Aucun runtime EUCLID n’est requis pour fermer 1R.
- **Dépendances :** aucune ; 1R bloque les étapes 4 à 10.

### Étape 2 — Réduire les autorités de données à trois slots

- **Objectif :** supprimer le slot 4 des constantes, structures, runtime et IDs de base.
- **Dettes/prérequis :** étape 1 ; audit des références S4.
- **Fichiers/symboles probables :** `Inc/NoteFx/note_fx_state.h`, `Src/NoteFx/note_fx_state.c`, `Inc/NoteFx/note_fx_engine.h`, `Src/NoteFx/note_fx_engine.c`, `Inc/NoteFx/note_fx_pipeline.h`, `Src/NoteFx/note_fx_pipeline.c`, `Inc/Param/param_store.h`, `Src/Core/track_runtime.c`, `Src/Param/param_registry_catalog.c`.
- **Changements précis :** `NOTE_FX_SLOT_COUNT=3` ; supprimer S4 de l’enum et des descripteurs ; recalculer `PARAM_COUNT`/assertions ; réduire `g_slot`, overrides, sync, reset, commandes et boucles ; conserver la capacité Play MIDI FX sans valeur numérique 4 ; réserver le futur audio uniquement dans la documentation.
- **Invariants :** aucun tableau MIDI FX indexable par 3 ; aucun ID S4 dans le format courant ; modèle, defaults et reset valides sur S1..S3 ; aucun appel direct audio créé.
- **Hors périmètre :** modèle EUCLID, masque, nouvelle UI, migration historique.
- **Dépendances :** étape 1.
- **Validations statiques/dynamiques :** assertions cardinalité/enum/param count ; recherches `S4/SLOT4` ; init/reset/capture runtime sur huit pistes ; compilation des consommateurs.
- **Builds :** Release Low-Cost, Release Premium.
- **Critères de fin :** l’autorité de données et le compilateur ne connaissent plus de quatrième MIDI FX fonctionnel ; les tailles prévisionnelles sont relevées dans la map.
- **Documentation :** inventaire de cardinalité et architecture de données.
- **Commit recommandé :** `refactor: reduce midi fx data authorities to three slots`.

### Étape 3 — Réduire UI, p-locks, persistance, clipboard et contrat hors Undo

- **Objectif :** rendre toutes les surfaces utilisateur et de stockage cohérentes avec S1..S3.
- **Dettes/prérequis :** D-017, D-019 ; étape 2.
- **Fichiers/symboles probables :** `Src/UI/pages/ui_page_midi_fx.c`, `Src/UI/ui_core_clipboard.c`, `Inc/Seq/seq_types.h`, `Inc/Seq/seq_param_iface.h`, `Src/Seq/seq_param_iface.c`, `Inc/Storage/pattern_live_ram.h`, `Src/Storage/pattern_live_ram.c`, `Inc/Core/track_snapshot.h`, `Src/Core/track_snapshot.c`, `Src/Storage/project_sd_bank.c`, `Src/Storage/pattern_sd_bank.c`, `Src/Storage/undo_v2.c` et leurs tests.
- **Changements précis :** navigation/labels/rendering limitées à 3 ; p-lock 16→12, offsets/runtime flags recalculés ; S4 non mapable/non restaurable ; Pattern/Project V1 et snapshots redimensionnés ; clipboard via état normalisé avec `MODEL` avant ses paramètres dépendants ; paramètres et modèles MIDI FX explicitement hors Undo/Redo.
- **Invariants :** aucun slot 4 dans UI, p-lock, masque, sauvegarde, duplication ou restauration ; ancien payload quatre slots non migré ; la valeur effective clamped est copiée et restaurée.
- **Hors périmètre :** modèle EUCLID et moteur audio futur.
- **Dépendances :** étape 2.
- **Validations statiques/dynamiques :** tailles et round-trip V1 ; copy/paste page/track/ensemble ; duplication/reset ; absence d’entrée NoteFx dans Undo/Redo ; tentative S4 et payload ancien rejetés ; navigation trois slots.
- **Builds :** Release Low-Cost, Release Premium.
- **Critères de fin :** `SEQ_PARAM_MIDI_FX_SLOT_COUNT=12`, runtime total et flags cohérents, Pattern/Project/snapshot round-trip vert, aucune valeur S4 appliquée.
- **Documentation :** architecture persistence/UI et contrat de format courant.
- **Commit recommandé :** `refactor: align midi fx ui plocks and persistence to three slots`.

### Étape 4 — Ajouter le modèle EUCLID et l’autorité de defaults

- **Objectif :** décrire EUCLID dans le registre/modèle sans encore générer de notes.
- **Dettes/prérequis :** D-017 ; étapes 2 et 3.
- **Fichiers/symboles probables :** `Inc/NoteFx/note_fx_state.h`, `Src/NoteFx/note_fx_state.c`, `Src/Param/param_registry_catalog.c`, `Src/Core/track_runtime.c`, catalogue `Seq/seq_division_catalog`, `seq_param_iface`.
- **Changements précis :** ajouter `NOTE_FX_MODEL_EUCLID` ; schéma model-aware param1/2/3 ; LENGTH 1..64 défaut16, PULSE 0..LENGTH défaut4, DIV canonique défaut1/16 ; clamp inter-paramètre et snapshot complet ; labels et defaults atomiques ; modèle cible dans les APIs de support.
- **Invariants :** passage ARP↔EUCLID charge exclusivement les defaults cibles ; `PULSE=0` est valide ; PULSE ne dépasse jamais LENGTH ; aucune table de division locale ; aucun stockage par modèle.
- **Hors périmètre :** masque, phase, Note On/Off et UI de génération.
- **Dépendances :** étapes 2–3.
- **Validations statiques/dynamiques :** defaults exacts `16/4/1/16`, clamp LENGTH/PULSE, restore invalide, model aller/retour, registry labels/ranges, défaut DIV canonique.
- **Builds :** Release Low-Cost, Release Premium.
- **Critères de fin :** un snapshot d’état de slot peut exprimer OFF, ARP et EUCLID de façon transactionnelle et normalisée, sans effet audio/terminal.
- **Documentation :** registre de modèles et catalogue de divisions.
- **Commit recommandé :** `feat: define euclid midi fx model defaults`.

### Étape 5 — Implémenter l’algorithme et le masque borné

- **Objectif :** fournir un masque Euclid pur, déterministe et hors chemin chaud.
- **Dettes/prérequis :** convention de masque ; étape 4.
- **Fichiers/symboles probables :** nouveau `Inc/NoteFx/note_fx_euclid.h`, `Src/NoteFx/note_fx_euclid.c`, tests unitaires et `tests/CMakeLists.txt`.
- **Changements précis :** `euclid_build_mask(length,pulse)` avec uint64/fixe, convention premier bit 0, clamp et bornes ; aucun calcul de division/Bjorklund dans la boucle audio ; recomputation uniquement à la modification LENGTH/PULSE.
- **Invariants :** masque identique pour les mêmes entrées, zéro/plein correct, bits au-delà de LENGTH nuls, coût maximal borné et testable host.
- **Hors périmètre :** phase runtime, sources, terminal, rotation et paramètres supplémentaires.
- **Dépendances :** étape 4.
- **Validations statiques/dynamiques :** vecteurs de référence L/P, toutes bornes 1..64, P0, P=L, P>L normalisé, recherche de division/calcul lourd dans le hot path.
- **Builds :** Release Low-Cost, Release Premium et test host du module.
- **Critères de fin :** test pur vert, masque stable sur cible et aucune sortie de note produite par le module seul.
- **Documentation :** convention mathématique du masque et exemples de vecteurs.
- **Commit recommandé :** `feat: add bounded euclidean rhythm mask`.

### Étape 6 — Ajouter le runtime strict-actif, phase et deadlines

- **Objectif :** intégrer EUCLID comme instance runtime indépendante par slot.
- **Dettes/prérequis :** owner/timestamp/admission de l’étape 1 ; étapes 4–5.
- **Fichiers/symboles probables :** `Inc/NoteFx/note_fx_engine.h`, `Src/NoteFx/note_fx_engine.c`, pipeline owner/diagnostics et nouveau runtime Euclid.
- **Changements précis :** ledger fixe des sources avec token/provenance/note/velocity/génération/fermeture ; phase/mask/deadline propres à chaque slot ; première note phase 0 ; active strict-only ; PULSE 0 silencieux ; règles de reconfiguration section 9 ; période absolue et owned avec off_sample.
- **Invariants :** aucun latch, mêmes pitchs distincts, accord partiel correct, phase non partagée, pas de deadline stale, aucune mutation main/UI directe.
- **Hors périmètre :** admission terminale nouvelle, UI et migration.
- **Dépendances :** étapes 1, 4 et 5.
- **Validations statiques/dynamiques :** note longue/courte, accord, relâchement partiel, sources KEY/STEP/MIDI/FX, PULSE0, changement LENGTH/PULSE/DIV, tempo, split de bloc et phase de trois instances.
- **Builds :** Release Low-Cost, Release Premium.
- **Critères de fin :** une instance produit uniquement des pulses pour des sources encore actives, avec deadlines monotones et phase conforme aux vecteurs.
- **Documentation :** runtime EUCLID, timeline et règles de phase.
- **Commit recommandé :** `feat: add strict active euclid runtime`.

### Jalon 6M — Mesure runtime précoce avant intégration terminale

Ce jalon est obligatoire après la phase minimale de l’étape 6, avant toute implémentation de l’étape 7 et avant le fan-out terminal complet. Il ne justifie aucune capacité par hypothèse : relever `sizeof` et la map du runtime Euclid, puis instrumenter DWT max/p99, cycles, high-water, IRQ audio, marge et placement mémoire pour une instance, trois instances sur une piste, huit pistes à une instance, `PULSE=LENGTH`, division rapide et 16 sources strict-actives. La mesure doit aussi couvrir la reconfiguration `LENGTH/PULSE/DIV` et le split de demi-buffer.

Le résultat est un point de décision : conserver, réduire ou refuser une borne doit être motivé par la mesure et sa marge H743. Les mesures de terminal, USB, saturation et trois EUCLID sur huit pistes restent à l’étape 9 ; aucune intégration terminale complète ne doit précéder ce jalon.

### Étape 7 — Générer les paires Note On/Off et continuer la chaîne

- **Objectif :** faire passer chaque occurrence EUCLID par les slots aval et le terminal commun.
- **Dettes/prérequis :** D-004/D-014 fermées ; étapes 1, 2 et 6.
- **Fichiers/symboles probables :** `note_fx_engine.c`, `note_fx_pipeline.c`, `note_fx_event.h`, `seq_play_scheduler.c`, output ledger et tests de stage.
- **Changements précis :** child occurrence par source/pulse ; stage+1 ; owned/off_sample ; réservation On+Off ; Off-first au même sample ; stale generation ; terminal ledger et admissions indépendantes sans API de sortie dans EUCLID.
- **Invariants :** un On admis a une fermeture déterministe ; aucune fermeture sur pitch seul ; aucun stage sauté/rebouclé ; aucune destination refusée ciblée par un Off.
- **Hors périmètre :** nouvelle polyphonie et Live Record.
- **Dépendances :** étapes 1, 2 et 6.
- **Validations statiques/dynamiques :** Off exact `On+period(DIV)`, Off-before-On, retrigger, stale old Off, ARP avant/après, passthrough, terminal internal-only/MIDI-only/both/refus.
- **Builds :** Release Low-Cost, Release Premium.
- **Critères de fin :** trace complète source→slot1→slot2→slot3→terminal pour EUCLID et zéro note pendante après fermeture ou transition.
- **Documentation :** contrat d’événement, terminal et stages.
- **Commit recommandé :** `feat: route euclid occurrences through midi fx chain`.

### Étape 8 — Transitions, changement de modèle et p-locks refusés

- **Objectif :** appliquer les politiques de vie produit sans perdre l’identité ni purger les autres slots.
- **Dettes/prérequis :** D-008, D-015 et politique D-017 ; étapes 3, 4, 6 et 7.
- **Fichiers/symboles probables :** transitions NoteFx/scheduler, `seq_param_iface.c`, `note_fx_state.c`, clipboard/restore et UI model callbacks.
- **Changements précis :** fermeture ciblée du slot au changement de modèle ; génération/runtime purge slot-only ; defaults transactionnels ; changement DIV/L/P avec Off puis rephase ; refus central des p-locks Euclid L/P/DIV ; MUTE_TRIGS non destructif ; STOP/PANIC/source/pattern/destination destructifs selon leur politique.
- **Invariants :** aucun ancien Off ne coupe un nouveau On ; mute ne purge ni phase ni ledger ; démute ne retrigger pas ; aucun p-lock refusé ne modifie l’état ; changement d’un slot ne nettoie pas les deux autres.
- **Hors périmètre :** capture post-FX, audio FX et migration historique.
- **Dépendances :** étapes 3, 4, 6 et 7.
- **Validations statiques/dynamiques :** model aller/retour, defaults, P-lock refusés, MUTE/UNMUTE, STOP, PANIC, source clock, pattern replace, destination rebind, double transition idempotente.
- **Builds :** Release Low-Cost, Release Premium.
- **Critères de fin :** matrice des transitions verte et aucune occurrence/échéance obsolète observable au terminal.
- **Documentation :** politiques de transition, p-lock model-aware et mute.
- **Commit recommandé :** `fix: make euclid model transitions occurrence safe`.

### Étape 9 — Multi-Euclid, saturation et budgets H743

- **Objectif :** autoriser trois EUCLID par piste et mesurer le worst case avant de fixer une éventuelle limite.
- **Dettes/prérequis :** D-011/D-012/D-013 et admissions de l’étape 1 ; étapes 6–8.
- **Fichiers/symboles probables :** pipeline budget/diagnostics, scheduler terminal/queues, `note_fx_engine.c`, `midi.c`, instrumentation DWT et scripts de mesure.
- **Changements précis :** fan-out fixe par instance, quotas compatibles avec 3×8 pistes, réserves Off, refus complets, high-water et compteurs par slot/track/cause ; ajuster uniquement après mesure les quotas ou capacités nécessaires.
- **Invariants :** coût borné par demi-buffer de 64 frames ; aucune allocation ; aucune rafale externe non bornée ; Off prioritaire ; aucune note pendante après saturation ; plusieurs EUCLID permis tant que la mesure le permet.
- **Hors périmètre :** changement arbitraire de polyphonie globale, audio FX et nouveau transport.
- **Dépendances :** étapes 6–8 et admissions de l’étape 1.
- **Validations statiques/dynamiques :** trois Euclid chaînés, 8 pistes, 16 sources/instance, DIV rapide, masque plein/zéro, queue à 0/1/2 places, terminal 64, USB plein/reconnecté, fragmentation maximale de demi-buffer.
- **Builds/mesures :** Release Low-Cost et Release Premium ; DWT max/p99, marge IRQ, cycles, high-water, underrun, mémoire map. Pas de TestPremium.
- **Critères de fin :** borne combinatoire documentée, quotas validés ou limitation mesurée explicitement justifiée ; aucune hypothèse `void`/queue pleine restante.
- **Documentation :** budget Z1/Z4, résultats H743 et capacité réelle EUCLID.
- **Commit recommandé :** `perf: validate euclid fanout and half buffer budgets`.

### Étape 10 — Validation finale et consolidation documentaire

- **Objectif :** fermer le plan, les tests et la documentation sur le HEAD réellement livré.
- **Dettes/prérequis :** D-018/D-019 et toutes les sorties des étapes 1–9.
- **Fichiers/symboles probables :** tests NoteFx/runtime/UI/persistence, `tests/CMakeLists.txt`, docs architecture/Z1/Z4, audit final et ce plan pour son statut d’exécution.
- **Changements précis :** enregistrer tous les tests comportementaux ; recherches négatives S4/first-ARP/pitch-only/direct output/runtime persisté/hot-path lourd ; réconcilier les audits ; conserver le seam Live Record non implémenté ; vérifier les tailles et les builds finaux.
- **Invariants :** le code, les tests et les docs décrivent trois slots, EUCLID et les mêmes limites ; aucun code audio ou Live Record ajouté ; aucune modification hors périmètre.
- **Hors périmètre :** nouvelle fonctionnalité après EUCLID, migration historique et refactor global.
- **Dépendances :** étapes 1–9.
- **Validations statiques/dynamiques :** matrice complète section 17, CTest réellement présent, Release Low-Cost/Premium, session H743, absence de notes pendantes et trace terminal unique.
- **Builds :** Release Low-Cost, Release Premium ; ne pas demander TestPremium.
- **Critères de fin :** verdict final `PRÊT POUR PLAN EUCLID` n’est permis qu’avant implémentation si les prérequis sont fermés, ou verdict d’implémentation conforme si l’étape 10 clôture après code ; aucun S4 fonctionnel.
- **Documentation :** audits, architecture, budgets, changelog de format V1 et statut de ce plan. Ne pas documenter un slot audio comme implémenté.
- **Commit recommandé :** `docs: consolidate three slot euclid validation`.

## 17. Matrice de validations

| Domaine | Scénario | Résultat attendu | Niveau |
|---|---|---|---|
| Cardinalité | Init/reset huit pistes | Trois états et trois runtimes, aucune instance index 3 MIDI FX. | Host + static |
| Cardinalité | Compilation des IDs | S1..S3 seulement, `PARAM_COUNT` et offsets cohérents. | Build |
| Cardinalité | Navigation | SLOT1/2/3 sélectionnables, SLOT4 absent/non adressable. | Host/UI |
| Cardinalité | P-lock | 12 positions, aucune adresse S4 dans mapping/masks. | Host + static |
| Cardinalité | Pattern/Project/snapshot | Round-trip V1 de trois slots ; payload quatre slots rejeté, sans migration. | Host/target |
| Cardinalité | Clipboard/duplication | Trois slots et valeurs effectives normalisées, jamais S4. | Host/UI |
| Masque | L/P de toutes bornes | déterminisme, premier pulse bit 0, bits hors longueur nuls. | Host |
| Masque | PULSE=0 | cycle silencieux, modèle toujours actif, phase bornée. | Host |
| Masque | PULSE=LENGTH | pulse à chaque position. | Host |
| Strict-active | Note longue | plusieurs pulses jusqu’au Note Off exact. | Runtime |
| Strict-active | Note courte sans pulse | aucune occurrence, pas de latch. | Runtime |
| Strict-active | Note courte qui croise un pulse | une occurrence au plus selon sample exact. | Runtime |
| Strict-active | Accord partiellement relâché | seules les notes restantes alimentent les pulses. | Runtime |
| Strict-active | Même pitch/retrigger/producteurs | tokens/générations distincts, Off appariés. | Runtime |
| Phase | Ajout/retrait source | phase conservée tant qu’une source reste active ; reset seulement idle→nouvelle source. | Runtime |
| Phase | Changement LENGTH/PULSE | sorties fermées, masque reconstruit, phase 0, Off avant nouvel On. | Runtime |
| Horloge | Changement DIV | owned fermés, deadlines anciennes stale, période nouvelle exacte. | Runtime |
| Configuration | Paramètre EUCLID LENGTH/PULSE/DIV | même modèle, clamp/reconfiguration selon section 9, aucun p-lock V1 et aucune entrée Undo/Redo. | Host/UI + Runtime |
| Configuration | Changement MODEL | transition de modèle distincte, defaults complets cibles, purge du slot seul, aucune entrée Undo/Redo. | Host/UI + Runtime |
| Horloge | Tempo/clock source | Off existants inchangés, futures deadlines cohérentes, pas de double clock. | Target |
| Paires | Off/On au même sample | Off terminal et owned traité avant nouvel On. | Runtime |
| Paires | Saturation 0/1/2 places | paire refusée sans On orphelin, compteur exact. | Host |
| Chaîne | EUCLID dans chaque slot | stage complet 0→1→2→3→terminal, aucun bypass/boucle. | Integration |
| Chaîne | ARP avant/après EUCLID | provenance/occurrence et durée correctes. | Integration |
| Chaîne | Trois EUCLID | phases/ledgers indépendants et trace terminale unique. | Integration |
| Admission | internal accepté/MIDI refusé | ledger interne seul, Off interne seul. | Host/USB |
| Admission | MIDI accepté/internal refusé | ledger MIDI seul, Off MIDI seul. | Host/USB |
| Admission | deux acceptés | deux masques mémorisés, deux fermetures. | Host/USB |
| Admission | deux refusés | aucun ledger/Note Off fantôme. | Host/USB |
| Transitions | STOP/PANIC | fermeture bornée, purge/génération après acquittement, zéro note pendante. | Runtime/target |
| Transitions | MUTE/UNMUTE STEP | nouveaux STEP bloqués, owned/phase conservés, aucun retrigger. | Runtime |
| Transitions | clavier/MIDI pendant mute | comportement live distinct conservé. | Integration |
| Transitions | changement de modèle | slot seul fermé/purgé, defaults cible, autres slots inchangés. | Runtime |
| P-lock | Euclid L/P/DIV | refus central, état base/runtime inchangé. | Host/UI |
| P-lock | Euclid MODEL | changement transactionnel et defaults cibles. | Host/UI |
| Persistance | save/load | `16/4/1/16` et valeurs clamped round-trip, aucun runtime. | Host/target |
| Undo | paramètres/modèles MIDI FX | aucune transaction NoteFx, aucun runtime capturé ; save/load, reset, duplication et clipboard restent testés séparément. | Static + Host/UI |
| Charge | 8 pistes × 3 Euclid × 16 sources | quotas, high-water, cycles et refus documentés. | H743 |
| Charge | DIV rapide / masque plein | absence d’underrun et fermetures exactes. | H743 |
| Charge | USB/moteur saturés | admissions réelles, Off prioritaires, aucune note pendante. | H743 |
| Seam futur | trace terminal post-FX | EUCLID observable au terminal sans Live Record implémenté. | Integration |

## 18. Budgets et mesures à prévoir

### Bornes logiques actuelles à conserver et vérifier

- demi-buffer : 64 frames ; le budget doit être ouvert une seule fois par demi-buffer, même si l’audio le fragmente en sous-segments ;
- NoteFx actuel : 8 On générés par piste, réserve 32 Off, 32 commandes ; ces nombres sont des points de départ, pas une preuve pour 3×EUCLID ;
- scheduler : capacité 512, collecte par demi-buffer bornée à 128 ;
- source ARP/EUCLID à prévoir : 16 entrées strict-actives par instance ;
- owned à prévoir : au moins 16 par instance pour conserver le fan-out nominal ;
- terminal courant : 64 occurrences par piste ; à comparer au maximum combiné de 48 sorties Euclid et des autres sources ;
- MIDI USB : réserve Off actuelle de 16 et génération de connexion ; vérifier qu’elle reste suffisante sous fan-out.

### Mesures mémoire

Après réduction puis après EUCLID, relever :

1. `sizeof(note_fx_track_state_t)`, `PatternSaveV1`, `track_snapshot_t`, `note_fx_slot_runtime_t` et toute structure de commande/diagnostic ;
2. `.map` Low-Cost et Premium pour `g_slot`, overrides, p-lock state/flags, buffers Pattern/Project/snapshots et module Euclid ;
3. RAM D1/D2/DTCM/SDRAM réelle et différence par rapport au HEAD ;
4. taille du code et des tables de masque ;
5. absence de padding inattendu ou d’alignement qui annulerait les économies annoncées.

### Mesures temps réel

Mesurer sur H743 Low-Cost et Premium avec DWT max et p99, charge IRQ et marqueur audio :

- un, deux et trois EUCLID sur une piste ;
- huit pistes simultanées ;
- 16 sources actives par instance ;
- `PULSE=0`, `PULSE=LENGTH`, DIV rapide et divisions ternaires ;
- trois EUCLID chaînés avec ARP en amont/aval ;
- changement DIV/LENGTH/PULSE avec fermetures au même sample ;
- MUTE/UNMUTE, STOP/PANIC et source switch pendant owned actifs ;
- queue NoteFx/scheduler à saturation et TX USB déconnecté/reconnecté ;
- fragmentation maximale des 64 frames et jusqu’à 4 pulses externes autorisés.

Le critère est une borne max et une marge documentées sous le budget audio existant, sans underrun ni backlog non borné. Le nombre final de sources, le quota ON et la capacité terminale ne doivent être augmentés ou limités qu’à partir de ces mesures et des invariants de fermeture.

## 19. Décisions encore ouvertes

Les décisions produit de la section 3 ne sont pas renégociables. Les seules décisions restantes sont techniques ou de surface :

1. **Sous-ensemble UI de DIV.** Le catalogue canonique contient actuellement huit divisions ARP, dont les ternaires, tandis que certaines pages utilisent un sous-ensemble de quatre divisions. Recommandation : exposer les huit choix canoniques à EUCLID pour éviter une limitation arbitraire et garder `1/16` comme défaut ; si l’ergonomie impose quatre choix, créer un sous-ensemble explicite du catalogue, jamais une table locale.
2. **Capacité finale des quotas/fan-out.** Le target produit autorise trois EUCLID ; le nombre d’On par demi-buffer, le terminal 64 et le budget H743 doivent être confirmés par mesure. Recommandation : commencer avec trois sans plafond spécial, mesurer, puis documenter toute borne nécessaire avec cause et marge.
3. **Undo NoteFx V1 — décision figée.** Les paramètres et modèles MIDI FX restent hors Undo/Redo, sans transaction ni capture du runtime. Save/load, reset, duplication et clipboard restent obligatoires et doivent manipuler l’état de base normalisé.
4. **Ancrage après tempo/clock change sans transition destructive.** La règle proposée conserve les Off déjà planifiés et recalcule les deadlines futures au prochain boundary. Recommandation : garder cette règle et la figer par test H743 ; toute autre ancre doit être motivée par une observation audible et documentée.

Il n’y a pas de décision ouverte sur un slot audio fonctionnel, une migration historique, ROTATE/GATE, le latch, le mute, l’indépendance des admissions ou le seam Live Record : ces points sont déjà figés hors périmètre.

## 20. Ordre recommandé d’exécution

```text
1 prérequis nettoyage
  → 1R preuves correctives (non fermée)
  → 2 autorités de données 4→3
  → 3 UI / p-lock / persistence / clipboard / hors Undo
  → 4 modèle et defaults Euclid
  → 5 masque pur borné
  → 6 runtime strict-actif / phase / deadlines
  → 7 Note On/Off / continuations / terminal
  → 8 transitions / modèle / p-lock refusés
  → 9 multi-Euclid / saturation / budgets H743
  → 10 validation finale / documentation
```

Ne pas sauter l’étape 1 pour livrer un prototype EUCLID qui masque une admission moteur supposée ou un timestamp live ancien. Ne pas réutiliser les IDs/libérations du slot 4 pour audio. Ne pas fusionner les transitions destructives dans l’algorithme de masque : le modèle, le runtime, les fermetures et le terminal doivent rester des responsabilités séparables et testables.

### Journal d’exécution

**Étape 1 — passe corrective du 2026-08-05 : partiellement exécutée.** Le
seam live résout désormais son sample au owner audio ; le terminal refuse les
On FX stale ; l’admission mono est bornée par une occurrence interne active ;
le pending clock externe est 32 bits avec compteur d’overflow ; les wrappers
terminal pitch-based ont été supprimés. Les builds `build/Release` et
`build/Premium` passent.

La fermeture complète de l’étape reste conditionnée à D-019 : les sources et
scripts référencés par `tests/CMakeLists.txt` ne sont pas présents dans le
checkout courant. Aucun test absent n’a été inventé ni transformé en faux
succès. Les validations host/H743, USB/moteur et restore/clipboard/p-lock
restent donc prérequis avant la clôture globale du nettoyage.

**Étape 2 — passe de réduction du 2026-08-05 : exécutée.** L’autorité de
cardinalité est désormais `NOTE_FX_SLOT_COUNT=3` ; les quatre IDs S4 et leur
descripteur ont été supprimés, avec `PARAM_COUNT=319`. Les tableaux et boucles
NoteFx suivent cette borne, le domaine p-lock MIDI FX passe de 16 à 12
positions et le runtime de p-lock de 94 à 90 octets. La famille UI MIDI FX ne
rend plus de quatrième slot sélectionnable. Aucune capacité audio ni alias S4
n’a été créé. Les builds `build/Release` et `build/Premium` passent.

La validation dynamique des snapshots, de la persistance, du clipboard et des
tests host reste reportée à l’étape 3/D-019 ; les références
fonctionnelles S4 absentes du code courant ont été contrôlées par recherche
négative.

**Étape 3 — passe UI/format/clipboard du 2026-08-05 : finalisée après
correction.** Les formats Pattern et Project passent en version 7 et rejettent
les payloads antérieurs ; les structures Pattern, Project et snapshots utilisent
déjà l’état `NOTE_FX_SLOT_COUNT=3`. Les p-locks courants restent bornés à 12
positions et les validations de chargement refusent les slots non supportés. Le
collage MIDI FX applique `MODEL` avant ses paramètres dépendants et la base
normalisée des trois slots est persistée, copiée, dupliquée et réinitialisée.
Les extensions Undo/Redo des paramètres ou modèles MIDI FX ont été retirées ;
`undo_v2` reste séquence-only.

Les builds `build/Release` et `build/Premium` passent. La matrice dynamique
host/round-trip et les tests dédiés restent bloqués par D-019 : les sources
référencées par `tests/CMakeLists.txt` ne sont pas présentes dans ce checkout.
