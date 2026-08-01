# Plan de ménage de printemps du dépôt

Audit statique du 1er août 2026. Cette passe ne modifie ni le firmware, ni les documents canoniques, ni le registre global. Le code réel est pris comme autorité. Aucun build n'a été lancé : le livrable est un plan, pas une validation de patch.

## 1. Verdict

Le dépôt est fonctionnellement plus cohérent que sa nomenclature ne le laisse penser : la topologie Play/Special, l'autorité de binding `track_runtime`, les moteurs actuels et les formats de sauvegarde v3 sont déjà réels. La dette principale est une superposition de trois générations : topologie uniforme configurable, ensembles historiques doublons, puis produit Play/Special avec `ENV` et MIDI FX.

Les écarts les plus dangereux ne sont pas esthétiques :

- `ENV` est encore nommé `COLORS`, tandis que VCA reste classé `MIX` et ENV3 `MOD`. Cette fragmentation traverse p-lock, modulation, clipboard et persistence.
- la Special FX et ses quatre MacroFX portent encore massivement le nom `MASTER_FX`, alors que Master et FX sont deux rôles fixes distincts ;
- plusieurs paramètres actuels réutilisent les valeurs numériques de tombstones MIX physiques. Des branches de persistence compensent ces collisions ;
- `AGENTS.md`, plusieurs passages de `AGENT.md`, le README et les documents Z empilent des contrats anciens et courants ;
- quelques sous-systèmes sont prouvés sans producteur ou sans consommateur (`control_events`, `control_router`, champs `linked`, aliases temporaires).

À préserver :

- `track_topology.[ch]` comme autorité des 8 Play et des Specials dépendantes de la variante ;
- `track_runtime` comme projection unique famille/type/capacités ;
- la séparation identité logique / lane physique ;
- les moteurs produit `Prism`, `Wave`, `Stack`, `DELUGE` et leurs noms d'implémentation justifiés (`Braids`, `Daisy`) ;
- l'ARP comme modèle valide de MIDI FX et comme sérigraphie physique, sans réintroduire un ensemble ARP autonome ;
- les états DSP VCA dans le mixer : l'erreur est le domaine logique, pas le backend d'exécution.

## 2. Dictionnaire canonique du produit actuel

| Concept produit actuel | Nom utilisateur | Nom interne actuel | Nom interne recommandé | Propriétaire logique | Emplacement recommandé | Anciens noms à éliminer ou conserver |
|---|---|---|---|---|---|---|
| Configuration de track | CFG | `UI_TEMPLATE_FAMILY_CFG`, `PARAM_CFG_*` | identique | configuration structurante de Play / transport global selon le paramètre | UI CFG, `track_state`, `synth_polyphony`, transport | conserver CFG ; sortir VOICES/SPREAD du domaine PLAY |
| Enveloppes et filtre | ENV | `COLORS`, fichier `ui_page_template_filter`, domaines COLORS/MIX/MOD | `ENV`, `ui_page_template_env`, domaine et set p-lock ENV | état sonore de track | UI ENV, `track_sound_state`; apply vers filtre, mixer VCA et `mod_env3` | éliminer COLORS ; ne pas renommer les backends filtre/VCA/ENV3 |
| Timbre/moteur | TONE | TONE | identique | moteur de la track ou rôle Special | UI TONE, `track_tone_sound_state`, backends moteur | conserver |
| Modulation | MOD | MOD | identique | LFO, matrice, Multi, Slew | UI MOD, `track_sound_state`, `mod_*` | retirer ENV3 du propriétaire MOD |
| Mix de track | MIX | MIX | identique | niveau, pan, sends et mute track-aware | UI MIX, `track_sound_state`, mixer | retirer VCA du domaine/set MIX ; éliminer les lanes MIX historiques après découplage des IDs |
| Jeu de voix | PLAY | PLAY | identique | séquenceur des 4 voix d'une Play track | UI PLAY, `seq_model`, scheduler | conserver ; interdit aux Specials |
| Effets MIDI | MIDI FX | `MIDI_FX`, modèle `ARP`, alias capacité `ARPEGGIATOR` | `MIDI_FX`; conserver `ARP` uniquement comme modèle/commande physique | pipeline NoteFx de la Play track | `NoteFx`, page MIDI FX, persistence dédiée | supprimer l'alias de capacité et les contrats d'ancien ensemble ARP |
| Effets globaux Master | Master | reverb/delay/comp sous TONE | `MASTER_*` explicite | Special Master, état global | TONE Master, mixer/effects globaux | conserver Master pour ces effets uniquement |
| Chaîne quatre MacroFX | FX | `MASTER_FX1..4`, `master_fx`, `fx_master_macro` | `MACRO_FX1..4`, `macro_fx`; nom DSP précisant « master bus » si utile | Special FX | TONE FX, état de la track FX, processeur post-mix | éliminer MasterFX comme identité propriétaire ; conserver la notion master-bus seulement dans le DSP |
| Boucle audio | Looper | Special Looper projetée en `Sampler/Looper` | `LOOPER` côté rôle ; représentation interne explicitement documentée tant qu'elle simplifie le binding | Special Looper | TONE/ROUT, `brick6_looper_runtime`, route matrix | conserver temporairement la projection Sampler/Looper ; ne pas la réexposer comme type Play |
| Tracks musicales | Play | familles Off/Synth/Sampler/Drum/MIDI/External | identique | `track_topology` + `track_state` | indices 0..7 | éliminer les anciens contrats de 14 tracks uniformément configurables |
| Tracks réservées | Special | Master, Looper, Input1..N, FX | rôles fixes avec projection famille/type interne | identique, avec noms de projection séparés des choix utilisateur | `track_topology`, `track_runtime` | conserver les représentations Input/Looper utiles au runtime ; supprimer les filtres Patch qui les présentent comme patches Play |
| Formats persistés courants | Pattern/Project/Patch/Kit | types et fonctions `*V1` mais versions de fichier 3 | `*_current` ou nom sans génération après décision atomique | Storage | Z6 / `Src/Storage` | éliminer le mensonge V1 ; ne pas confondre symbole C et version de fichier |

Les noms `Braids` et `Daisy` sont des noms d'implémentation amont, pas des concepts produit obsolètes. `Prism` reste le nom produit du wrapper Braids ; `Wave`, `Stack` et `DELUGE` sont des moteurs distincts actuellement exposés. Aucun renommage n'est recommandé dans ces cas.

## 3. Carte complète des ensembles

### CFG

`ui_page_template_cfg.c` expose `PARAM_CFG_TRACK`, `PARAM_CFG_TRACK_TYPE`, MIDI channel/source et `PARAM_CFG_POLY_VOICES/SPREAD`; les réglages REC globaux partagent certaines pages. Famille/type aboutissent à `track_state` puis à la projection `track_runtime`. VOICES/SPREAD aboutissent à `synth_polyphony`. Les paramètres transport/REC aboutissent au séquenceur ou à l'état global correspondant. Les changements structurants sont exclus des p-locks et passent par snapshots/undo. Pattern stocke la configuration de track et les globals ; Project enveloppe Pattern ; Patch ne capture que les Play tracks ; Kit capture la structure de l'ensemble des rôles utiles. README, Z2, Z3, Z5 et Z6 décrivent cette chaîne. Écart : VOICES/SPREAD réutilisent des IDs MIX et sont classés PLAY alors que l'UI exclut explicitement VOICES du p-lock (`ui_param.c:121`, `:1086`).

### ENV

Le bouton produit ENV ouvre aujourd'hui `UI_PAGE_TEMPLATE_COLORS`; `ui_page_template_filter.c` affiche les pages `ENV 1/2` et `ENV 2/2`. Ses banques contiennent filtre, ADSR filtre, ADSR VCA, ADSR ENV3 et retriggers FLT/VCA/MOD. L'état canonique est regroupé dans `track_sound_state`. Les apply aboutissent respectivement au filtre, au mixer/VCA et à `mod_env3`.

La chaîne se fracture ensuite : `track_runtime_get_param_rule()` classe filtre en COLORS, VCA en MIX, ENV3 en MOD ; `seq_param_iface.c` range donc VCA dans les slots MIX et ENV3 dans MOD. `ui_param`, `param_macro`, `mod_destination_catalog`, `track_snapshot`, Pattern et Kit consomment ces domaines. Clipboard/clear résolvent la famille de template COLORS. La correction canonique est un domaine logique ENV unique, tout en gardant trois backends d'exécution. Les IDs numériques du set p-lock doivent rester stables pendant le renommage mécanique ; la reclassification sémantique doit être une étape distincte et testée.

### TONE

`ui_page_template_tone.c` résout des tables par famille/type/rôle. Les IDs moteur sont projetés par `track_runtime_tone_slot_to_param`; l'état track-aware vit dans `track_tone_sound_state`, puis les apply appellent Prism/Braids, Wave, Stack, DELUGE, Sampler, Drum, MIDI/External, Looper ou MacroFX. Le set p-lock est TONE et sa validité dépend du runtime effectif. Clipboard/undo capturent le template ou le snapshot track. Pattern, Patch, Kit et Project transportent l'état de ton selon leur scope. Master utilise ici les effets globaux ; FX utilise les quatre MacroFX. L'écart majeur est uniquement le nom propriétaire `MASTER_FX` de ces derniers.

### MOD

La page MOD expose LFO, matrice, Multi et Slew ; `track_sound_state`, `mod_lfo`, `mod_matrix` et leurs backends constituent l'autorité. Les destinations sont filtrées via `mod_destination_catalog` et les règles runtime. Le set p-lock MOD, le clipboard de template, undo et la persistence Pattern/snapshot suivent ce domaine. ENV3 et `PARAM_ENV_RETRIG_MOD` sont encore classés ici alors qu'ils sont désormais présentés dans ENV : ils doivent sortir du propriétaire logique MOD, sans déplacer le moteur `mod_env3`.

### MIX

La page MIX expose niveau, pan et sends track-aware ; le mute suit l'autorité dédiée et le mixer. `track_sound_state` est la source canonique persistable. Le set p-lock MIX contient quatre slots produit mais aussi l'ADSR VCA historique. Pattern possède un bloc `mix`, tandis que les snapshots track/Kit/Patch transportent l'état agrégé. Après migration ENV, MIX doit ne contenir que les contrôles de mixage logique. Les anciens `PARAM_MIX_TRACKx_*` ne sont pas l'API de cette page : ce sont des lanes physiques/tombstones dont plusieurs valeurs sont recyclées par des paramètres sans rapport.

### PLAY

Les pages PLAY présentent quatre voix par Play track. `seq_model` porte bases et locks ; le scheduler construit l'exécution. Le set p-lock PLAY et les fonctions de clipboard/undo séquence manipulent ce modèle. Pattern/Project persistent la séquence ; Patch ne doit pas capturer les steps. Les Specials n'ont pas cette capacité. VOICES/SPREAD sont de la configuration moteur, pas des voix PLAY, malgré leur domaine runtime actuel.

### MIDI FX

`ui_page_midi_fx.c` expose 16 IDs génériques répartis sur quatre slots. `note_fx_state` est canonique ; le pipeline NoteFx et ses moteurs, dont `note_fx_arp`, exécutent la chaîne. Le set p-lock est `SEQ_PLOCK_SET_MIDI_FX`. Les snapshots track, Pattern et Project ont un stockage NoteFx dédié ; Patch/Kit ne l'incluent pas via le tableau générique. Le bouton physique ARP ouvre cette page sur Play et sert de contexte ROUT sur Special. ARP reste un modèle MIDI FX, pas une capacité ou un ensemble autonome.

### Master et FX

Master et FX sont deux Specials fixes. Master expose reverb, delay et compresseur globaux ; leur état/apply vit dans les backends globaux et leur persistence dans les globals Pattern/Project. FX expose quatre MacroFX ; leur état vit dans `track_tone_sound_state.master_fx`, leurs IDs sont `PARAM_MASTER_FX1_*..4_*` et leur exécution post-mix dans `fx_master_macro`. Ces paramètres suivent TONE, p-lock/clipboard de la track FX et snapshots persistants. Le comportement est juste, le nom de propriétaire est faux. Il faut préserver explicitement la distinction « propriétaire FX » / « insertion sur master bus ».

### Looper

La Special Looper est fixe. `track_runtime` la projette actuellement avec une représentation interne Sampler/Looper ; TONE expose ARM/LEN/PLAY/STRETCH/PITCH/GRAIN/XFADE et ROUT utilise le bouton physique ARP. `track_tone_sound_state.looper`, `brick6_looper_runtime`, le mixer et la route matrix forment la chaîne. Les paramètres de commande non déterministes ou structurels sont exclus de la carte p-lock dans `seq_param_iface`; les paramètres autorisés doivent rester une allowlist explicite. Pattern/Project et snapshots transportent paramètres/routes, pas l'état audio transitoire. Les filtres Patch `INPUT`/`LOOPER` sont fantômes, car `patch_v1_capture_track()` refuse toute track non-Play.

### Tracks Play et Special

`track_topology.[ch]` définit 14 slots de stockage, 8 Play et des Specials fixes. Low-Cost expose 12 rôles actifs ; Premium 14. `track_state` configure uniquement Play et représente les Specials pour les consommateurs communs ; `track_runtime` est la projection autoritaire ; `seq_model` porte des modèles hétérogènes. Le clipboard track applique les règles de rôle, Pattern persiste identité rôle/ordinal, Patch est Play-only, Kit peut capturer l'ensemble. Les anciens groupes master/slave et `SEQ LINK` ne subsistent plus comme comportement actif ; seuls des champs scheduler `linked` toujours nuls et la documentation historique restent.

## 4. Registre du ménage

### CLEAN-DOC-001 — `STALE DOC` — contrat d'instructions contradictoire

- Ancien concept : 14 tracks configurables, families Input/Hybrid/Master, anciens ensembles de navigation et persistence « version 1 ».
- Produit actuel : 8 Play configurables, Specials fixes, ENV, MIDI FX, fichiers v3.
- Preuves : `AGENTS.md` décrit encore Input4/Hybrid/Master configurable et l'ancien clavier/ARP ; `AGENT.md` emploie encore COLORS/VCA et contient des passages Master historiques ; `tests/play_special_storage_validation.ps1:86-89` exige quatre versions 3.
- Références actives : ces fichiers pilotent les futures passes et peuvent provoquer une réintroduction de dette.
- Effet : risque de patch fonctionnel incorrect, pas de bug runtime direct.
- Portée : rendre `AGENT.md` canonique, réduire `AGENTS.md` à un contrat cohérent ou un renvoi non ambigu ; aligner README et architecture globale.
- Risque : faible si le code reste l'autorité ; ne pas effacer les invariants hard-RT valides.
- Tests/docs : revue croisée AGENT/AGENTS/README/Z2/Z3/Z5/Z6 ; recherche des termes interdits.
- Dépendances : préalable à tout autre ménage.

### CLEAN-NAME-001 — `RENAME` — `COLORS` vers `ENV`

- Ancien concept : ensemble de coloration/filtre.
- Produit actuel : ENV, contenant FLT, VCA et ENV3.
- Preuves : `ui_page_template_filter.c` affiche ENV mais résout `UI_TEMPLATE_FAMILY_COLORS`; symboles actifs dans `ui_template_page.h`, `ui_page_manager.h`, `track_runtime.h`, `seq_param_iface.h`, navigation, clipboard, modulation et persistence.
- Effet : ambiguïté transverse ; aucune panne si le renommage conserve valeurs d'enum et de set.
- Portée : enums, fonctions, variables, tests et documents ; fichier renommé seulement dans CLEAN-MOVE-001.
- Risque : ordinal persistant des sets p-lock et pages ; conserver explicitement les valeurs.
- Tests : résolution bouton ENV, disponibilité par track, p-lock round-trip, clipboard/undo, recherche `COLORS` hors historique.
- Dépendances : après CLEAN-DOC-001, avant CLEAN-OWNER-001/002.

### CLEAN-OWNER-001 — `WRONG OWNER` et `BUG` — VCA encore propriétaire MIX

- Ancien concept : VCA hébergé dans MIX.
- Produit actuel : ADSR VCA dans ENV.
- Preuves : page ENV contient `PARAM_VCA_ATTACK..RELEASE`; `track_runtime.c:1867-1871` les classe avec retrig VCA dans MIX ; `seq_param_iface.c` inclut VCA dans la table MIX ; Pattern sépare `sound`/`mix` selon ce domaine.
- Références actives : UI p-lock, `param_macro`, clipboard, snapshot, Pattern, Kit, modulation.
- Effet : p-locks et copie/clear sont attachés à un ensemble qui n'est plus visible comme propriétaire ; le bloc de persistence dépend du passé.
- Portée : rattacher `PARAM_VCA_ATTACK..RELEASE` et `PARAM_ENV_RETRIG_VCA` au domaine logique et au set p-lock ENV, les retirer de MIX, puis faire utiliser ce propriétaire unique par p-lock, modulation, clipboard/clear/undo et persistence. L'état canonique et l'apply DSP VCA restent dans le mixer : le backend physique n'est pas déplacé.
- Risque : changement de disposition de p-lock et incompatibilité des snapshots v3 existants ; aucune rétrocompatibilité n'étant requise, faire une rupture explicite et atomique.
- Tests : mapping chaque paramètre ENV, capture/apply Pattern, locks ENV et MIX indépendants, clipboard page/ensemble.
- Dépendances : CLEAN-NAME-001 et suppression atomique du chemin VCA autonome définie par CLEAN-DEAD-001.

### CLEAN-OWNER-002 — `WRONG OWNER` — ENV3 encore propriétaire MOD

- Ancien concept : enveloppe de modulation présentée avec MOD.
- Produit actuel : ENV3 et retrig MOD visibles dans ENV.
- Preuves : `ui_page_template_filter.c:29,41` les expose ; `track_runtime.c:1922-1926` les classe MOD ; `param_registry.c` applique toujours ENV3 au backend `mod_env3`, ce qui est légitime.
- Effet : même fracture p-lock/clipboard/persistence que VCA, sans nécessité de déplacer le moteur.
- Portée : domaine logique/set ENV uniquement ; conserver `mod_env3` comme backend.
- Risque/tests/dépendances : identiques à CLEAN-OWNER-001 ; tester aussi le catalogue des destinations et les retriggers.

### CLEAN-DEAD-001 — `DONE`, `DUPLICATE` — ancien chemin VCA autonome supprimé

- Ancien concept : VCA accessible comme ensemble autonome.
- Produit actuel : VCA appartient exclusivement à ENV, sur Premium comme sur Low-Cost. Les paramètres VCA restent dans la page interne ENV.
- Effet corrigé : le bouton Premium ouvre ENV directement sur sa sous-page VCA; le clipboard, le clear et l'undo réutilisent désormais le propriétaire ENV.
- Validation : la famille, la page et l'ensemble runtime autonomes ont été supprimés; les paramètres VCA, leur état et leur backend mixer restent présents.
- Dépendances : CLEAN-NAME-001 ; doit précéder CLEAN-OWNER-001 et CLEAN-MOVE-001.

### CLEAN-NAME-002 — `RENAME` — MasterFX vers MacroFX propriétaire FX

- Ancien concept : chaîne d'effets attachée à « Master/FX ».
- Produit actuel : quatre MacroFX appartenant à la Special FX ; Master possède ses effets globaux.
- Preuves : gating topologique sur FX dans `track_runtime`; pourtant `PARAM_MASTER_FX1_*`, `track_tone_sound_state.master_fx`, helpers `ui_param_master_fx_*`, `fx_master_macro.[ch]`, route et diagnostics `master_fx` restent actifs.
- Effet : confusion d'autorité et documentation contradictoire, comportement audio actuel correct.
- Portée : paramètres, état, UI, backends, diagnostics, tests et docs. Renommer le fichier DSP seulement si le nouveau nom précise réellement l'insertion master-bus.
- Risque : forte propagation mais mécanique ; conserver les IDs numériques et le layout persistant.
- Tests : quatre slots, quantification TYPE/A/B, routage FX, capture/restore, test audio existant.
- Dépendances : topologie inchangée ; précède toute MOVE associée.

### CLEAN-NAME-003 — `DONE` — capacité MIDI FX sans alias ARP

- Ancien concept : ARP comme capacité/ensemble.
- Produit actuel : MIDI FX, dont ARP est un modèle.
- Preuves : `TRACK_CAPABILITY_MIDI_FX` est consommé par `track_topology.c`, le switch runtime et le test de topologie. Le pipeline `note_fx_arp` est réellement actif.
- Effet corrigé : aucun alias de capacité ne suggère un chemin ARP autonome.
- Portée réalisée : utiliser uniquement `TRACK_CAPABILITY_MIDI_FX`; conserver le modèle ARP et le bouton physique.
- Risque : faible, suppression mécanique sans ordinal.
- Tests : `track_topology_validation.ps1`, navigation MIDI FX et ROUT Special.
- Dépendances : aucune.

### CLEAN-BUG-001 — `WRONG OWNER` — VOICES/SPREAD classés PLAY

- Ancien concept : réemploi opportuniste du domaine et d'IDs disponibles.
- Produit actuel : configuration de polyphonie du moteur Synth dans CFG, non p-lockable.
- Preuves : UI CFG les expose ; `track_runtime.c:1892` les classe PLAY ; `PARAM_CFG_POLY_*` aliasent des mutes MIX ; Pattern possède même une compatibilité `mix` vers `sound`; l'UI exclut VOICES des locks.
- Effet : surface latente de p-lock/automation et persistence artificielle, même si des gardes UI masquent le cas normal.
- Portée : propriétaire CFG/non-lockable explicite ; stockage Pattern canonique unique ; IDs propres dans CLEAN-OWNER-003.
- Risque : budget de voix et restore ; préserver `synth_polyphony` comme autorité.
- Tests : budget voix, Pattern/Project/Kit restore, absence dans maps p-lock/modulation.
- Dépendances : séparer correction de domaine et renumérotation.

### CLEAN-OWNER-003 — `ALIAS`, `WRONG OWNER` — IDs actuels superposés aux tombstones MIX

- Ancien concept : lanes physiques `PARAM_MIX_TRACK0..3_*` utilisées comme réserve numérique.
- Produit actuel : paramètres track-aware, Drum MD, polyphonie et reverb globale.
- Preuves : `param_store.h:361-381` aliasent mute, VOICES/SPREAD, MD MODEL/P1..P8 et reverb à des IDs MIX ; `param_registry_catalog.c` donne aux mêmes indices leur sens moderne ; `pattern_live_ram.c` contient des exceptions de plage pour les globals reverb et une migration poly `mix` vers `sound`.
- Références actives : registre, runtime, p-lock, Pattern/Project/Patch/Kit, undo, macros, modulation, tests.
- Effet : plages mensongères et branches spéciales ; risque de filtrer ou restaurer un paramètre selon son ancien sens.
- Portée : attribuer un ID unique à chaque concept actuel, supprimer les tombstones physiques sans consommateur, reconstruire les tables désignées et supprimer les exceptions. Conserver seulement les paramètres MIX globaux réellement utilisés.
- Risque : élevé et sémantique ; IDs présents dans tous les payloads et slots p-lock. Aucun simple search/replace.
- Tests : unicité ID/descriptor, classification exhaustive, round-trip de chaque format, p-lock, clipboard/undo, deux variantes.
- Dépendances : après domaines ENV/CFG stabilisés ; étape indépendante et réversible avec invalidation explicite des anciens fichiers si nécessaire.

### CLEAN-DEAD-002 — `DEAD` — granular sans backend produit

- Ancien concept : FX granular.
- Produit actuel : aucune page/track/capacité ne l'expose.
- Preuves : `PARAM_GRAN_*` et descriptors restent compilés ; les apply sont no-op ; l'unique banque UI par défaut est marquée invalide ; `fx_granular.cpp` est explicitement exclu du build ; `FX_GRANULAR` subsiste dans le pool.
- Consommateur utile : aucun chemin produit démontré ; le pool et les IDs sont une surface fantôme, pas une feature.
- Portée : retirer UI fallback, paramètres/descriptors/wrappers puis décider suppression ou déplacement hors build du backend source et de l'enum pool.
- Risque : renumérotation des IDs ; faire avec CLEAN-OWNER-003, pas avant.
- Tests : recherche zéro, construction pool, persistence et registres.
- Dépendances : CLEAN-OWNER-003.

### CLEAN-DEAD-003 — `DEAD` — `control_router`

- Ancien concept : routeur générique de contrôles, avec aliases granular et `PARAM_JUNO_*`.
- Produit actuel : édition directe via registre/UI.
- Preuves : `control_router_set_param` n'a aucun appel ; seules déclaration, définition et documentation le référencent.
- Consommateur utile : aucun.
- Portée : retirer `Inc/Param/control_router.h`, `Src/Param/control_router.c`, includes/docs.
- Risque : faible ; vérifier les outils hors arbre avant suppression.
- Tests : build/link et recherche du symbole.
- Dépendances : peut précéder les renommages.

### CLEAN-DEAD-004 — `DEAD` — file `control_events`

- Ancien concept : file de changements de paramètres consommée en IRQ.
- Produit actuel : aucun producteur.
- Preuves : init au boot et pop/drain dans `audio_float.c`; `control_event_push` n'a aucun appel hors sa définition. La file est donc toujours vide.
- Consommateur utile : aucun événement ne peut atteindre le consommateur.
- Portée : retirer `.h/.c`, init et boucle de drain IRQ.
- Risque : faible en arbre ; vérifier une éventuelle API BSP externe avant suppression.
- Tests : build des deux variantes, recherche du symbole, test audio de non-régression.
- Dépendances : indépendante ; bénéfice de vérité de code, pas revendiqué comme optimisation hard-RT.

### CLEAN-DEAD-005 — `DEAD` — résidus scheduler et types temporaires

- Ancien concept : séquence liée/groupée et transition `seq_param8_t`.
- Produit actuel : scheduler mono-track sans groupes.
- Preuves : `seq_play_scheduler` copie `linked`, mais le contexte l'initialise toujours à 0 et aucune lecture fonctionnelle n'existe ; `typedef seq_param_slot_t seq_param8_t` n'a aucun consommateur.
- Portée : champs/assignations `linked` et alias de type ; ne pas toucher au linked Kit de Pattern, concept distinct et actif.
- Risque : faible ; attention à ne pas rechercher/supprimer tous les mots `linked` globalement.
- Tests : tests scheduler/Play, build, recherche ciblée des symboles.
- Dépendances : aucune.

### CLEAN-DEAD-006 — `DEAD` — métadonnée `dirty_pending_persist`

- Ancien concept : sauvegarde différée d'un Pattern.
- Produit actuel : aucun lecteur de ce drapeau.
- Preuves : champ déclaré, écrit à 1 après store et remis à 0 à l'init ; aucune condition ne le consulte.
- Portée : champ et écritures uniquement.
- Risque : faible ; ne pas modifier la politique réelle de store.
- Tests : Pattern store/load et recherche ciblée.
- Dépendances : aucune.

### CLEAN-DEAD-007 — `DEAD` — exclusions CMake de fichiers absents et résultats Kit TODO

- Ancien concept : anciens backends recorder et opérations Kit non implémentées.
- Produit actuel : fichiers absents ; apply/rename/delete Kit implémentés.
- Preuves : exclusions `live_recorder.c`, `recorder_transport.c`, `brick6_recorder_runtime.c` sans fichier correspondant ; `KIT_V1_RESULT_*_TODO` n'a aucun site de retour, seulement enum et libellé.
- Portée : règles d'exclusion et valeurs/libellés TODO.
- Risque : faible ; contrôler scripts externes et ordinals exposés uniquement en interne.
- Tests : configure/build, opérations Kit.
- Dépendances : aucune.

### CLEAN-DEAD-008 — `DEAD` — filtres Patch Input/Looper

- Ancien concept : patches pour anciennes tracks configurables Input/Looper.
- Produit actuel : Patch Play-only ; Specials fixes.
- Preuves : UI Patch conserve des filtres INPUT/LOOPER ; `patch_v1_capture_track()` refuse une track non-Play ; Master/Hybrid ont déjà disparu de cette UI.
- Effet : filtres accessibles sans objet produit valide ou métadonnées anciennes inutilement acceptées.
- Portée : filtres, labels et validation des familles/types Patch limitée aux Play.
- Risque : anciens fichiers SD volontairement invalidés ; documenter cette absence de compatibilité.
- Tests : capture/apply pour toutes les familles Play, refus Specials, navigation filtres.
- Dépendances : après contrat doc Play/Special.

### CLEAN-NAME-004 — `RENAME` — symboles persistence V1 alors que le format courant est v3

- Ancien concept : première génération des structs et modules.
- Produit actuel : Pattern/Project/Patch/Kit version 3.
- Preuves : `PATTERN_VERSION`, `PROJECT_V1_FILE_VERSION`, `PATCH_SD_FILE_VERSION`, `KIT_SD_FILE_VERSION` valent 3 ; types/fonctions/fichiers restent `PatternSaveV1`, `ProjectSaveV1`, `patch_v1`, `kit_v1`; le commentaire de `PARAM_PERSIST_COUNT` dit encore « until step 7 » alors que NoteFx est persisté séparément.
- Effet : commentaires de migration devenus contrat apparent ; pas de bug runtime.
- Portée : nommer les types/modules `current` ou sans suffixe ; séparer clairement version de fichier et génération d'API ; corriger commentaires.
- Risque : propagation mécanique très large, sans changer layout/version dans la même étape.
- Tests : tailles/layouts statiques, quatre round-trips, tests v3 existants.
- Dépendances : après corrections sémantiques de persistence ; ne jamais mêler rename et changement de format.

### CLEAN-NAME-005 — `ALIAS` — compatibilité Macro devenue scènes/locks

- Ancien concept : banques/slots Macro.
- Produit actuel : scènes et locks de Macro.
- Preuves : `PROJECT_V1_MACRO_BANK_COUNT` alias de `MACRO_SCENE_COUNT`; `project_v1_macro_get_slot/set_slot` ignorent l'argument `macro` et transfèrent vers la scène 0 ; seuls les chemins clipboard consomment encore ces wrappers.
- Effet : API mensongère et argument sans effet.
- Portée : migrer le clipboard vers API scène/lock, puis supprimer aliases, typedefs et wrappers.
- Risque : clipboard/undo Macro ; préserver exactement la scène ciblée.
- Tests : copie/colle/clear de locks et round-trip Project.
- Dépendances : avant renommage global persistence.

### CLEAN-MOVE-001 — `MOVE` — page filtre vers propriétaire ENV

- Ancien placement : `ui_page_template_filter.[ch]` contient désormais filtre, VCA, ENV3 et retriggers.
- Propriétaire actuel : ensemble ENV.
- Justification : le fichier n'est plus seulement un filtre ; le déplacement rend le scope réel sans changer d'architecture.
- Portée : renommer fichier/API en `ui_page_template_env`; conserver les sous-composants et backends à leur place.
- Risque : règles CMake glob et includes ; faible après CLEAN-NAME-001/OWNER.
- Tests : toutes variantes de template par famille/type et calibration return-page.
- Dépendances : exécuter après CLEAN-NAME-001, la suppression effective du chemin autonome CLEAN-DEAD-001 et CLEAN-OWNER-001/002.

### CLEAN-DOC-002 — `STALE DOC` — README et architecture globale mélangent familles et rôles

- Ancien concept : Input/Master/Looper comme choix de famille/type et « Master/FX » unique.
- Produit actuel : Play configurables et Specials fixes Master/Looper/Input/FX.
- Preuves : README juxtapose encore familles Input/Master/Looper avec le layout fixe ; architecture globale mentionne un adaptateur « Master/FX MacroFX » alors que Z2/code séparent Master et FX.
- Portée : présenter une seule table produit, distinguer noms de rôle et projections internes.
- Risque : aucun runtime ; conserver les différences Low-Cost/Premium exactes.
- Dépendances : après noms MacroFX et contrat topologique.

### CLEAN-DOC-003 — `STALE DOC` — documents Z empilés

- Ancien concept : groupes master/slave, SEQ LINK, clavier/ARP autonome, pages/families retirées, migrations successives.
- Produit actuel : les sections finales ne forment pas toujours un contrat unique lisible.
- Preuves : Z3/Z5/Z6 contiennent couches historiques contradictoires ; Z4 conserve de longs contrats groupes/linked absents du code ; Z1 emploie encore Master/FX.
- Portée : réécrire les sections canoniques au présent et déplacer l'historique utile dans un changelog séparé. Ne pas modifier Z1/Z4 pendant la présente passe d'audit.
- Risque : perdre une contrainte encore active ; chaque suppression documentaire doit être recroisée au code et aux tests.
- Dépendances : dernier lot documentaire, après code nettoyé.

## 5. Code mort et surfaces fantômes

### Suppression prouvée sûre

| Élément | Preuve | Action |
|---|---|---|
| `control_router` | aucun appel de `control_router_set_param` | supprimer module et aliases |
| `seq_param8_t` | aucune référence hors typedef | supprimer alias |
| scheduler `linked` | toujours initialisé à 0, copié, jamais lu | supprimer champs/assignations ciblés |
| `dirty_pending_persist` | écrit, jamais lu | supprimer champ/écritures |
| exclusions CMake recorder absentes | aucune source correspondante | supprimer entrées |
| résultats Kit `*_TODO` | aucun site de retour, opérations présentes | supprimer valeurs/libellés |

### Surface encore compilée mais inaccessible

| Élément | État | Action |
|---|---|---|
| `control_events` | init et consommateur IRQ compilés, zéro producteur | vérifier frontière BSP puis supprimer tout le chemin |
| paramètres granular | descriptors et no-op apply compilés, banque UI invalide, backend exclu | supprimer avec reconstruction des IDs |
| filtres Patch Input/Looper | UI/validation présents, capture non-Play interdite | limiter Patch aux familles Play |
| page test boutons Low-Cost | code compilé sous garde fonctionnelle à 0 | conserver comme outil si une procédure usine le réclame, sinon déplacer sous build test |

### Stub volontaire

- envois MIDI USB host/DIN actuellement explicites : conserver tant que la matrice produit les identifie comme sorties prévues ; les renommer en stub uniquement si leur statut est ambigu ;
- `usb_host_tasklet_poll_bounded` sans appel produit : conserver seulement si l'intégration Board future est documentée ; sinon ticket de suppression séparé ;
- page de test paramètres et hooks de test audio : conserver dans les builds de validation tant qu'ils ont des consommateurs tests.

### Tombstone nécessaire

- aucun tombstone numérique n'est nécessaire au nom d'une rétrocompatibilité non démontrée ;
- jusqu'à CLEAN-OWNER-003, les plages MIX recyclées doivent toutefois rester intactes pour éviter une renumérotation partielle ;
- les valeurs de rôle/storage 14 slots peuvent rester stables même si Low-Cost n'active que 12 rôles.

### Décision produit requise

- suppression physique ou archivage hors build de `fx_granular.cpp` ;
- statut externe de `control_events` et `usb_host_tasklet_poll_bounded` ;
- politique explicite d'invalidation des fichiers v3 lors de la reconstruction des IDs.

## 6. Renommages recommandés

| Renommage | Symboles/fichiers | Propagation | Risque persistence/p-lock/tests | Nature |
|---|---|---|---|---|
| COLORS → ENV | enums UI/runtime, set p-lock, navigation, clipboard, modulation, persistence, tests | toutes les couches de la carte ENV | conserver les ordinals pendant ce lot | mécanique |
| MASTER_FX → MACRO_FX | 16 IDs, état `master_fx`, helpers UI/backend, routes, diagnostics | TONE FX, snapshots, audio tests | conserver IDs et layout | mécanique avec clarification propriétaire/insertion |
| alias de capacité ARP → MIDI_FX | topology/runtime/test | capacité seulement | alias supprimé sans impact d'ordinal | terminé |
| `*V1` API → courant | structs, fichiers, fonctions Pattern/Project/Patch/Kit | Storage, undo, tests, crash/monkey | ne changer ni version ni layout | mécanique, lot large isolé |
| Macro bank/slot → scene/lock | wrappers Project et clipboard | UI clipboard, persistence Project | vérifier scène exacte | sémantique localisée |
| `PARAM_PERSIST_COUNT` | borne générique ambiguë | registre/persistence | aucun ordinal si macro seulement renommée | commentaire et nom sémantique |

Renommages rejetés comme cosmétiques : Braids interne sous Prism, ressources Braids de Stack, composants Daisy, moteurs Wave/Stack/DELUGE, `note_fx_arp` comme modèle, et « master bus » dans le nom d'un processeur DSP réellement post-mix.

## 7. Déplacements recommandés

1. Renommer `ui_page_template_filter.[ch]` en `ui_page_template_env.[ch]` après unification du domaine. Le fichier possède réellement tout ENV ; ce n'est pas une réorganisation esthétique.
2. Ne pas déplacer `mod_env3` hors de `Mod` : ENV est son propriétaire UI, mais son moteur d'exécution reste une modulation.
3. Ne pas déplacer l'état/apply VCA hors du mixer : le mixer est son backend physique valide.
4. Ne déplacer `fx_master_macro` que si le nouveau nom explicite à la fois propriétaire FX et insertion master-bus ; sinon renommer seulement API/état produit.
5. Si le granular est conservé à titre expérimental, le sortir du build produit et le placer dans une zone expérimentale documentée ; sinon le supprimer avec ses surfaces.

## 8. Nettoyage documentaire

1. Faire de `AGENT.md` le contrat courant, supprimer les règles COLORS/VCA/topologie uniforme et corriger la politique de version. Réduire `AGENTS.md` à des instructions compatibles, sans second modèle produit.
2. Réécrire dans le README une table Play/Special unique, les ensembles actuels CFG/ENV/TONE/MOD/MIX/PLAY/MIDI FX, puis Master/FX/Looper par rôle.
3. Dans Z2/Z3/Z5/Z6, conserver un seul contrat au présent. Déplacer les migrations utiles vers une section historique courte ou un changelog ; supprimer les couches contradictoires.
4. Corriger `ARCHITECTURE_GLOBAL.md` seulement lorsqu'une frontière/autorité est effectivement stabilisée, notamment Master contre FX et ENV.
5. Traiter Z1 et Z4 dans une passe documentaire séparée : remplacer Master/FX ancien dans Z1 et extraire l'historique groupes/SEQ LINK de Z4, sans mélanger ce travail au présent audit ni au registre global.
6. Remplacer les commentaires de persistence datés (« until step 7 », V1 courant, versions anciennes) par le contrat exact de ce qui est stocké génériquement ou séparément.

## 9. Plan d'exécution

Chaque étape ci-dessous peut être demandée par `Go étape X` et doit produire un commit/revert indépendant.

### Étape 1 — Corriger les instructions dangereusement fausses

- Objectif : une seule description exécutable du produit actuel.
- Fichiers probables : `AGENT.md`, `AGENTS.md` uniquement.
- Autorisé : topologie Play/Special, ENV/MIDI FX, rôles Master/FX/Looper, versions courantes.
- Interdit : modifier firmware, registre global, Z1/Z4, inventer le dual-core.
- Vérification : diff documentaire et recherches de contrats contradictoires.
- Docs : les deux fichiers d'instructions.
- Dépendances : aucune ; revert indépendant complet.

### Étape 2A — Supprimer les morts certains sans impact d'ID

- Objectif : retirer scheduler `linked`, `seq_param8_t`, `dirty_pending_persist`, exclusions CMake absentes et résultats Kit TODO.
- Fichiers probables : `seq_play_scheduler.*`, `seq_types.h`, `pattern_live_ram.c`, `CMakeLists.txt`, `kit_v1.[ch]`, tests associés.
- Autorisé : suppressions strictement prouvées, ajustement de tests.
- Interdit : groupes/linked Kit actifs, persistence layout, paramètres, hard-RT.
- Tests/builds : configure et build Low-Cost/Premium ; tests scheduler, Pattern et Kit.
- Docs : Z4/Z6 seulement si un contrat courant les mentionne ; Z4 dans passe séparée si nécessaire.
- Dépendances : étape 1 ; revert indépendant.

### Étape 2B — Supprimer les modules sans flux produit

- Objectif : retirer `control_router` puis `control_events` après contrôle de la frontière BSP.
- Fichiers probables : `Inc/Src/Param/control_*`, boot, `audio_float.c`, CMake/docs.
- Autorisé : suppression de la chaîne entière si aucun producteur externe n'existe.
- Interdit : remplacer par une nouvelle queue ou modifier le traitement audio.
- Tests/builds : builds deux variantes, tests audio/paramètres, recherche zéro symboles.
- Docs : Z0/Z1/Z3 si leur contrat courant cite ces modules ; Z1 séparément.
- Dépendances : étape 1 ; chaque module revertable séparément.

### Étape 2C — Retirer les filtres Patch fantômes

- Objectif : rendre Patch strictement Play-only dans UI et validation.
- Fichiers probables : page Patch assign, `patch_v1.[ch]`, tests Z5/Z6.
- Autorisé : supprimer Input/Looper des filtres et métadonnées admises.
- Interdit : changer Kit ou le binding des Specials.
- Tests/builds : capture/apply toutes familles Play, refus de chaque Special, builds deux variantes.
- Docs : README, Z5, Z6.
- Dépendances : étape 1 ; revert indépendant.

### Étape 3A — Renommage mécanique COLORS vers ENV

- Objectif : aligner tous les noms sans changer l'ownership.
- Fichiers/symboles : enums UI/runtime, `SEQ_PLOCK_SET_COLORS`, navigation, clipboard, modulation, snapshots, persistence, tests.
- Autorisé : rename mécanique avec valeurs numériques explicites inchangées.
- Interdit : déplacer VCA/ENV3 entre domaines ou renommer le fichier.
- Tests/builds : deux variantes, UI navigation/LED, p-lock/clipboard/Pattern, recherche zéro `COLORS` hors historique.
- Docs : Z3/Z5 et README.
- Dépendances : étape 1 ; revert indépendant.

### Étape 3B — Renommage mécanique MasterFX vers MacroFX

- Objectif : propriétaire FX explicite, comportement inchangé.
- Fichiers/symboles : `PARAM_MASTER_FX*`, état, helpers UI/Param/Audio, route, diagnostics, tests.
- Autorisé : rename avec IDs/layout inchangés.
- Interdit : changer chaîne DSP, ordre des quatre slots ou effets Master globaux.
- Tests/builds : deux variantes, audio test MacroFX, TONE FX, p-lock, snapshots.
- Docs : README, Z1/Z2/Z3/Z5/Z6 ; mise à jour Z1 dans passe dédiée.
- Dépendances : étape 1 ; revert indépendant.

### Étape 3C — Supprimer aliases de transition sûrs

- Objectif : capacité ARPEGGIATOR et wrappers Macro bank/slot mensongers.
- Fichiers probables : topology/runtime/tests, `project_v1.[ch]`, clipboard.
- Autorisé : remplacer par MIDI_FX et scene/lock.
- Interdit : supprimer modèle ARP, bouton ARP ou données Macro.
- Tests/builds : topology, NoteFx/ARP, clipboard Macro, Project round-trip.
- Docs : Z2/Z4/Z5/Z6 selon contrat ; Z4 séparément.
- Dépendances : étapes 1 et API scène disponible ; deux sous-commits revertables.

### Étape 3D — Rediriger Premium vers ENV et supprimer le chemin VCA autonome — TERMINÉE

- Objectif : aligner Premium sur Low-Cost avec ENV comme unique propriétaire UI de VCA.
- Fichiers/symboles : mapping du bouton Premium, navigation et retour de page, LEDs, résolution ENV et scopes clipboard/clear/undo.
- Autorisé : faire ouvrir au bouton Premium directement la page VCA interne à ENV si le ciblage existant est simple, sinon ENV selon son mécanisme normal ; supprimer ensuite famille/page/ensemble/scope VCA autonomes. Conserver `PARAM_VCA_*`, l'état VCA et le backend mixer.
- Interdit : supprimer les paramètres VCA avant leur rattachement ENV, laisser momentanément le bouton sans cible, créer un ensemble de remplacement ou réaffecter le bouton à une autre fonction produit.
- Atomicité/revert : installer la redirection du bouton et supprimer le chemin autonome dans le même changement ; un revert restaure les deux côtés ensemble. La suppression ne doit jamais précéder la redirection validée.
- Tests/builds : navigation du bouton VCA Premium ; ouverture correcte d'ENV et accès aux paramètres VCA ; LEDs et retour de page/calibration ; clipboard/clear/undo sous le seul scope ENV ; builds Low-Cost et Premium ; recherche zéro des symboles autonomes et équivalents, hors paramètres/état/backend DSP VCA légitimes.
- Docs : README et Z5.
- Dépendances : étape 3A ; précède 4A et 5A.

### Étape 4A — Unifier le propriétaire logique ENV

- Objectif : FLT, VCA, ENV3 et retriggers appartiennent au domaine/set ENV unique, après disparition du chemin UI VCA autonome.
- Fichiers/symboles : `track_runtime_get_param_rule`, `seq_param_iface`, `ui_param`, `param_macro`, destination catalog, Pattern, Kit, snapshots, clipboard.
- Autorisé : reclassification logique et migration/invalidation v3 explicitement choisie.
- Interdit : déplacer le backend VCA du mixer ou `mod_env3`, recréer une famille/ensemble VCA, optimiser le chemin audio.
- Tests/builds : table exhaustive param→domain→set ; VCA absent du domaine/set MIX et présent dans ENV ; p-locks ENV/MIX/MOD ; modulation ; clipboard/clear/undo sous ENV uniquement ; navigation Premium vers ENV ; quatre formats ; builds Low-Cost/Premium ; recherche zéro des symboles VCA autonomes hors paramètres/état/backend DSP légitimes.
- Docs : Z2/Z3/Z5/Z6, README.
- Dépendances : 3A et 3D ; revert atomique indépendant.

### Étape 4B — Reclasser CFG VOICES/SPREAD

- Objectif : propriétaire CFG non-p-lockable et stockage canonique unique.
- Fichiers/symboles : runtime rule, seq interface, UI guards, Pattern migration, polyphony, undo/tests.
- Autorisé : supprimer les fallbacks de domaine une fois le nouveau contrat validé.
- Interdit : changer budget de voix ou comportement Synth.
- Tests/builds : voice budget, absence maps p-lock/mod, Pattern/Project/Kit restore, deux variantes.
- Docs : Z2/Z3/Z5/Z6.
- Dépendances : étape 1 ; IDs encore inchangés jusqu'à 4C.

### Étape 4C — Reconstruire les IDs et retirer granular/tombstones MIX

- Objectif : un ID par concept actuel, aucune collision sémantique.
- Fichiers/symboles : `param_store.h`, catalogue, runtime/apply, Pattern/Project/Patch/Kit, p-lock, macro/mod, tests ; `PARAM_GRAN_*`, `PARAM_MIX_TRACKx_*` morts.
- Autorisé : nouvelle numérotation atomique, invalidation documentée des anciens fichiers, suppression des branches compensatoires.
- Interdit : conserver une fausse compatibilité partielle, changer DSP ou UI produit.
- Tests/builds : unicité et couverture du registre, round-trip exhaustif, p-lock/clipboard/undo, builds Debug/Release Low-Cost/Premium.
- Docs : Z3/Z6, README si granular était visible, note d'incompatibilité.
- Dépendances : 4A/4B, décision granular ; revert atomique obligatoire.

### Étape 5A — Déplacer la page sous son propriétaire ENV

- Objectif : `ui_page_template_filter` devient `ui_page_template_env`.
- Autorisé : rename fichier/API/includes/CMake/tests.
- Interdit : modifier contenu fonctionnel ou layouts.
- Tests/builds : templates toutes familles/types, navigation/calibration, deux variantes.
- Docs : Z5.
- Dépendances : 3A et 4A ; revert indépendant.

### Étape 5B — Clarifier le nom du processeur MacroFX

- Objectif : distinguer propriétaire FX et insertion master-bus.
- Autorisé : rename fichier/API seulement si le nom résultant ajoute cette information.
- Interdit : déplacement architectural ou nouvelle chaîne audio.
- Tests/builds : tests audio MacroFX et builds deux variantes.
- Docs : Z1/Z2, avec Z1 dans passe dédiée.
- Dépendances : 3B ; étape facultative et revertable.

### Étape 6A — Renommer l'API persistence courante

- Objectif : supprimer le mensonge `V1` sans changer le format v3.
- Fichiers probables : tous modules/types Pattern/Project/Patch/Kit, SD banks, undo, tests.
- Autorisé : rename mécanique, commentaires exacts, asserts de taille.
- Interdit : modifier version, layout, checksum ou sémantique.
- Tests/builds : quatre round-trips, compatibilité interne des fichiers v3 créés avant/après, builds deux variantes.
- Docs : Z6 et architecture globale si noms d'autorité changent.
- Dépendances : 4C ; revert indépendant malgré sa largeur.

### Étape 6B — Consolider les documents canoniques

- Objectif : contrat courant lisible sans couches contradictoires.
- Fichiers : README, Z2/Z3/Z5/Z6, architecture globale ; Z1/Z4 dans une passe séparée ; jamais le registre global sans mission dédiée.
- Autorisé : présent canonique + historique bref séparé.
- Interdit : changer code, réintroduire compatibilité ou anticiper dual-core.
- Vérification : dictionnaire de termes, liens/symboles existants, revue croisée code/tests.
- Dépendances : toutes étapes sémantiques terminées ; revert documentaire indépendant.

### Étape 7 — Vérification finale du dépôt

- Objectif : prouver absence de reliquats et divergence de variantes.
- Autorisé : correctifs microscopiques découverts par les validations, chacun isolé.
- Interdit : nouvelle architecture, optimisation CPU/RAM, correction hard-RT non liée.
- Tests/builds : configure/build Debug et Release Low-Cost/Premium ; suite PowerShell complète ; recherches négatives COLORS, MASTER_FX propriétaire, alias de capacité ARP, IDs alias, groupes/SEQ LINK actifs, granular et modules supprimés ; round-trips storage et clipboard/undo.
- Docs : matrice finale de conformité dans le rapport de l'étape, puis mise à jour des docs seulement si le code final l'exige.
- Dépendances : toutes ; aucun regroupement empêchant un revert ciblé.

## 10. Éléments hors chantier

- Bugs hard-RT déjà enregistrés : accès concurrent SD/audio et dérive d'autorités identifiés par `GLOBAL-001`, `GLOBAL-002`, `Z0-001`, `Z1-001`, `Z1-002`, `Z2-001`, `Z4-001/002/003`, `Z5-001`, `Z6-001/002`. Ils doivent être corrigés dans des passes dédiées, pas opportunément pendant un rename.
- Dual-core : placement des autorités, IPC, partage mémoire/cache, partition des IRQ/tasks et nouvelle topologie d'exécution sont reportés. Seuls des noms propriétaires vrais sont préparés ici.
- Optimisations CPU/RAM : taille des structs persistants, tables DSP, queue removal pour gain de cycles, placement SDRAM/DTCM et compression des catalogues ne sont pas des objectifs de ce plan.
- Redesigns produit : nouveaux moteurs, réactivation granular, nouvelle family MIDI, nombre de tracks, quatrième entrée physique Low-Cost/Premium, nouveau routing, rétrocompatibilité SD et changement de contrôles ne sont pas demandés.
- Noms internes justifiés : Braids/Daisy, ARP comme modèle MIDI FX, projection interne des Specials et notion de master-bus dans le DSP restent hors renommage cosmétique.
- Le registre `docs/audits/global_code_debt_register.md` et les rapports Z1/Z4 ne doivent pas être modifiés par la présente passe.
