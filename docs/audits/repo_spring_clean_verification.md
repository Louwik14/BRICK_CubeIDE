# Vérification finale du grand ménage du dépôt

Date de vérification : 2026-08-01  
Branche : `main_doublemcu_monocore`  
HEAD vérifié : `cb4632e1e` (`sampler: remove redundant contiguous scratch clear`)  
Base distante : `origin/main_doublemcu_monocore` à `72ff28110`, HEAD en avance de 11 commits.

## 1. Verdict final

**VALIDÉ AVEC RÉSERVES NON BLOQUANTES.** Le code produit et les deux variantes Release correspondent à la topologie, aux ownerships, aux formats et au registre attendus après le ménage. Aucun reliquat produit actif des concepts supprimés et aucune régression locale attribuable au chantier n'ont été trouvés.

Les seules réserves sont deux validations déjà en échec avant le début du chantier (`stack_morph_validation` et `synth_voice_budget_validation`) et deux formulations textuelles devenues obsolètes. Elles ne bloquent pas la préparation du portage H747, mais doivent rester visibles comme dette de validation/documentation.

La passe a aussi vérifié les modifications non commitées déjà présentes dans le worktree, notamment la suppression des wrappers Macro bank/slot et leur renommage scène/lock. Elles compilent dans les deux variantes. Aucun commit ni push n'a été effectué.

## 2. Commits et chantiers vérifiés

| Commit | Chantier | Résultat |
|---|---|---|
| `559bc74a5` | ownership ENV unifié | conforme |
| `31a860f58` | VOICES/SPREAD déplacés vers CFG | conforme |
| `678ee2e45` | audit des IDs | plan recoupé avec le code final |
| `d2a585fee` | retrait de la surface granular | conforme |
| `9917470c7` | noms canoniques des IDs sans renumérotation | conforme |
| `61e49896e` | classification persistence explicite | conforme |
| `cfc3b020a` | retrait de la source granular TestPremium | conforme ; aucun build TestPremium demandé |
| `697957e78` | module UI ENV | conforme |
| `3a5694b6b` | optimisation Release de `wav_audio_codec.c` | préservée |
| `2fb82b343` | consolidation documentaire | globalement conforme, réserves textuelles ci-dessous |
| `cb4632e1e` | retrait du clear du scratch contiguous | préservé |

## 3. Matrice des contrats vérifiés

| Domaine | Contrat constaté | Preuve/validation | Verdict |
|---|---|---|---|
| Topologie | 8 Play ; 4 Special Low-Cost et 6 Premium ; rôles issus de `track_topology` | `track_topology_validation`, `special_track_role_validation` | conforme |
| Runtime | `track_runtime` projette familles, types, capacités et bindings ; Specials fixes | mêmes validations et builds | conforme |
| Séquence | aucune topologie master/slave, aucun `SEQ LINK` actif, scheduler mono-track | `sequence_track_models_validation`, `seq_play_scheduler_pair_validation`, recherches ciblées | conforme |
| Ensembles | CFG, ENV, TONE, MOD, MIX, PLAY, MIDI FX seulement | code UI/runtime, `env_ownership_validation` | conforme |
| ENV | FLT, VCA, ENV3 et retriggers sous ENV ; backend VCA mixer et ENV3 modulation conservés | `env_ownership_validation` : `env_slots=25/256`, MIX=4, MOD=12 | conforme |
| CFG | VOICES/SPREAD sous CFG, non p-lockables et non modulables | `cfg_polyphony_ownership_validation` | conforme |
| UI ENV | BTN6 Premium ouvre ENV/VCA ; module `ui_page_template_env.[ch]` ; ancien module absent | recherches et builds | conforme |
| Master/FX | Master porte les globals ; FX porte quatre MacroFX ; rôles distincts | `special_track_role_validation` | conforme |
| MIDI FX/ARP | capacité unique MIDI FX ; ARP limité au modèle/moteur/marquage/Hold/ROUT autorisés | trois validations `note_fx_*`, topologie | conforme |
| Patch | capture/apply Play-only ; toutes les Specials refusées | `play_special_storage_validation`, inspection `patch_v1.c` | conforme |
| Clipboard/snapshot/undo | rôles stricts, clear et undo/redo présents ; ENV/CFG/MIDI FX capturés selon leur scope | `play_special_storage_validation`, `note_fx_persistence_validation`, `env_ownership_validation` | conforme statiquement |
| Macro scènes | wrappers et aliases bank/slot absents du code actif ; API scène/lock compilée | recherche négative, deux builds Release | conforme |
| Formats | Pattern v4, Project v4, Patch v3, Kit v3 ; validation stricte version/taille | `play_special_storage_validation`, `env_ownership_validation`, `cfg_polyphony_ownership_validation` | conforme |
| Rejets anciens | Pattern/Project v3 refusés par l'égalité stricte version/taille ; aucune migration ajoutée | inspection des validateurs SD | conforme |
| Registre | `PARAM_COUNT == 323`, `PARAM_PERSIST_COUNT == 307`, MD MODEL/P1..P8 contigus, valeurs inchangées | asserts, `param_reserved_slots_validation`, tests CFG/MD | conforme |
| Réservés | `PARAM_RESERVED_000..005` et réserves MIX inertes, cachés UI, non p-lockables, non modulables, apply nul | `param_reserved_slots_validation` | conforme |
| Granular | aucune surface, aucun type FX, aucun backend `fx_granular`/`fx_clouds` | recherche négative et CMake | conforme |
| Persistence | globals/track-aware/réservés classés explicitement ; 9 globals reverb actifs ; aucune décision par plage physique MIX 6..37 | `pattern_persistence_classification_validation` : 0..306, 43 globals, 21 réservés | conforme |
| Drum MD | MODEL/P1..P8 contigus et backends/tests conservés | `md_dsp_validation`, `md_trx_bd_validation`, test registre | conforme |
| Streamer SD | `wav_audio_codec.c` finit en `-O3` sans `-O0` dans les commandes Release des deux variantes ; aucun `memset` du scratch ; DMA lit tous les secteurs couvrant `sector_offset + source_bytes` ; invalidations avant/après DMA inchangées dans `sd_diskio.c` | compile commands, diff des commits, inspection ciblée | conforme |

Les tests disponibles vérifient les chemins de capture/restore et les layouts de manière statique. Il n'existe pas dans le dépôt de banc hôte autonome exécutant sur fichier quatre round-trips binaires complets P4/Pr4/Pa3/K3 ; ce manque de banc dynamique est une dette de couverture, pas un défaut de format observé.

## 4. Recherches négatives

Recherche effectuée hors `build/`, `Inspiration/`, rapports d'audit et passes historiques.

Absents du code produit actif :

- `UI_TEMPLATE_FAMILY_COLORS`, `SEQ_PLOCK_SET_COLORS` ;
- `UI_TEMPLATE_FAMILY_VCA`, `TRACK_RUNTIME_UI_ENSEMBLE_VCA` ;
- `PARAM_MASTER_FX`, `TRACK_CAPABILITY_ARPEGGIATOR` ;
- tous les aliases `PARAM_MIX_TRACK0_` à `PARAM_MIX_TRACK3_` ;
- `PARAM_GRAN_`, `FX_GRANULAR`, `fx_granular.*`, `fx_clouds.*` ;
- `control_router`, `control_events`, `seq_param8_t`, `dirty_pending_persist` ;
- `ui_page_template_filter` comme fichier/symbole actif ;
- aliases, typedefs et wrappers Macro bank/slot ;
- scheduler `linked` produit et résultats Kit `*_TODO` liés au chantier.

Occurrences restantes justifiées :

- `ui_page_template_filter` dans Z5 est explicitement historique ;
- `PARAM_MASTER_FX` dans `special_track_role_validation.ps1` est une sentinelle négative ;
- `COLORS` dans Z1/Z3 décrit explicitement le retrait historique ;
- `fx_master_macro` est le nom technique du processeur post-mix/master-bus ;
- `linked_kit` concerne les Kits liés et non l'ancienne séquence groupée ;
- ARP reste présent uniquement dans les contextes produit autorisés.

## 5. Builds et tests

### Builds demandés

| Build | Commande | Résultat |
|---|---|---|
| Release Low-Cost | `cmake --build build/Release --parallel 4` | PASS, aucun travail restant |
| Release Premium | `cmake --build build/Premium --parallel 4` | PASS, aucun travail restant |

Les deux `compile_commands.json` confirment `wav_audio_codec.c` avec l'option terminale `-O3` et sans `-O0`.

### Suite PowerShell

Résultat : **20/22 PASS**.

Passent : `cfg_polyphony_ownership`, `engine_output_gain`, `env_ownership`, `external_input_ownership`, `filter_delay`, `hall_lowcost_integration`, `md_dsp`, `md_trx_bd`, `note_fx_persistence`, `note_fx_pipeline`, `note_fx_plock`, `param_reserved_slots`, `pattern_persistence_classification`, `play_special_storage`, `seq_play_scheduler_pair`, `sequence_track_models`, `special_track_role`, `track_mute_authority`, `track_paste_playback`, `track_topology`.

Le runner CTest hôte n'a pas pu être configuré sur cette machine : CMake sélectionne NMake mais `nmake` et un compilateur C hôte ne sont pas installés. Les validations PowerShell demandées et les deux firmwares Release ont néanmoins été exécutés ; aucun build TestPremium n'a été lancé.

## 6. Corrections microscopiques

Aucune correction de code n'a été nécessaire. Le seul fichier créé par cette passe est le présent rapport.

Les modifications source déjà présentes avant la passe, relatives aux noms scène/lock Macro et à la suppression des wrappers bank/slot, ont été préservées sans remaniement.

## 7. Échecs préexistants confirmés

### `stack_morph_validation.ps1`

- Échec reproduit : `Legacy Stack waveform implementation changed`.
- Cause : le test prend le préfixe du fichier avant `brick6_stack_waveform_skew_phase`, puis le compare au fichier **HEAD complet**, lequel contient déjà ce suffixe. Une fois l'ajout commité, la comparaison ne peut plus réussir.
- Antériorité : test et helpers sont déjà présents au commit `c50242457`, ancêtre de `559bc74a5`.
- Classement : attente obsolète du test, indépendante du ménage. Aucun correctif opportuniste appliqué.

### `synth_voice_budget_validation.ps1`

- Échec reproduit : attente littérale `synth_polyphony_get_voice_count(targets[i])` manquante.
- Cause : Patch est devenu mono-track/Play-only et `patch_v1.c` utilise correctement `target`, mais le test ajouté lors de la même transition recherche encore les anciens littéraux multi-cibles `targets[i]` et `applied_voice_count[i]`.
- Antériorité : dérive introduite au commit `23d42678b`, ancêtre de `559bc74a5`.
- Classement : attente obsolète du test, indépendante du ménage. Aucun correctif opportuniste appliqué.

## 8. Dettes restantes hors chantier

| Dette | Impact | Recommandation |
|---|---|---|
| Deux tests préexistants à attentes obsolètes | bruit rouge permanent dans la suite | corriger leurs oracles dans une passe de tests dédiée, sans toucher au DSP ni à la polyphonie |
| Pas de runner hôte disponible sur la machine | les six exécutables C de `tests/CMakeLists.txt` ne sont pas rejoués ici | installer/configurer un compilateur C hôte + Ninja ou NMake |
| Pas de banc dynamique quatre-formats | couverture statique seulement pour plusieurs round-trips | ajouter ultérieurement un banc hôte de sérialisation P4/Pr4/Pa3/K3, sans migration |
| `AGENT.md` dit encore que la « reconstruction des IDs » est planifiée | formulation ambiguë après canonisation sans renumérotation | préciser que la canonisation est terminée et que la compaction reste interdite/reportée |
| commentaire `param_store.h` : domaine MIDI FX « jusqu'à l'étape 7 » | commentaire périmé ; la frontière 307 reste volontaire | reformuler comme frontière persistante Patch/Kit et stockage MIDI FX séparé |

Aucune de ces dettes n'est un changement d'ID/format, une migration persistence, un bug hard real-time, une modification audio/DSP ou une préparation dual-core à effectuer dans cette passe.

## 9. Conclusion H747

**La base fonctionnelle est propre et validée pour préparer le portage H747.** Aucun blocage produit issu du ménage ne reste à résoudre avant ce travail. Les deux échecs préexistants et les lacunes de couverture/documentation ci-dessus doivent être suivis séparément, sans élargir cette passe en redesign, migration ou optimisation.
