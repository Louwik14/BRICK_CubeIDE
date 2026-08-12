# Audit local des IDs de paramètres — préparation 4C

État audité : `559bc74a5` (ownership ENV) puis `31a860f58` (VOICES/SPREAD en CFG), avec le code local courant. Cette passe ne modifie ni firmware, ni ID, ni format.

## 1. Verdict

**Recommandation : `PARTIAL CLEANUP`.**

Les collisions sont des collisions de noms et de classification historique, pas des collisions de descriptors ni un bug runtime démontré. Un entier ne possède qu'un descriptor effectif et les chemins produit emploient le sens moderne de cet entier. Les compensations de persistence empêchent actuellement les cinq globals reverb placés dans la plage MIX physique d'être filtrés comme tombstones. Elles sont fragiles, mais aucune corruption sur un payload courant valide n'a été reproduite.

Après 4A/4B :

- aucune branche fonctionnelle ne dépend encore du sens physique `PARAM_MIX_TRACKx_*` ; les quatre contrôles MIX produit sont `PARAM_MIX_LEVEL/PAN/SEND1/SEND2`, plus `PARAM_MIX_MUTE` track-aware ;
- `PARAM_CFG_POLY_VOICES` et `PARAM_CFG_POLY_SPREAD` sont bien `CFG`, propriété de `synth_polyphony`, non p-lockables, non modulables et non assignables aux macros ; le fallback Pattern `mix -> sound` de VOICES a été supprimé ;
- les valeurs numériques peuvent être conservées tout en faisant des symboles modernes les entrées canoniques de l'enum, en remplaçant les slots morts par des noms réservés neutres et en retirant les aliases historiques ;
- une renumérotation globale n'est pas justifiée avant H747 : elle invaliderait surtout des layouts et mappings persistés, sans gain CPU/RAM démontré et avec une forte redondance probable avec le portage ;
- le nettoyage rentable maintenant est symbolique et classificatoire, avec retrait du fantôme granular en conservant six slots numériques réservés. Toute compaction réelle est à reporter au moment d'une rupture de formats décidée pour une autre raison.

### Contrôle d'unicité des descriptors

Le prétraitement Low-Cost du catalogue donne **321 initializers désignés, tous uniques**, pour `PARAM_COUNT == 323`. Il n'existe donc aucun double descriptor actif à un même index. Les deux index sans initializer sont `PARAM_SAMPLER_CLIP_QUANT` et `PARAM_SAMPLER_CLIP_BEAT_RESYNC`, hors périmètre 4C ; ils valent zéro-initialisé et ne créent pas de collision. Les aliases ci-dessous sont résolus avant compilation : ils ne créent jamais une deuxième entrée de registre.

## 2. Table exhaustive des IDs concernés

Légende formats : `P4` = Pattern v4, `Pr4` = Project v4, `Pa3` = Patch v3, `TS/U` = snapshot Track et Undo/Redo RAM. « layout » signifie que l'ordinal existe dans les matrices `PARAM_PERSIST_COUNT`, sans valeur valide produite. Les globals reverb appartiennent à la page TONE du rôle Master, mais restent `domain NONE` dans `track_runtime_get_param_rule`; leur caractère global est codé localement dans Pattern.

| ID | Symbole canonique produit | Ancien symbole même valeur | Domaine / propriétaire actuel | Descriptor et apply/runtime | P-lock | Modulation | Formats / tests directs | Statut |
|---:|---|---|---|---|:---:|:---:|---|---|
| 0 | aucun | `PARAM_GRAN_DENSITY` | aucun | descriptor Gran Density ; `apply_gran_density` no-op | non | non | P4/Pr4 layout ; aucun test | granular fantôme |
| 1 | aucun | `PARAM_GRAN_PITCH` | aucun | descriptor Gran Pitch ; apply no-op | non | non | P4/Pr4 layout ; aucun test | granular fantôme |
| 2 | aucun | `PARAM_GRAN_MIX` | aucun | descriptor Gran Mix ; apply no-op | non | non | P4/Pr4 layout ; aucun test | granular fantôme |
| 3 | aucun | `PARAM_GRAN_FREEZE` | aucun | descriptor Gran Freeze ; apply no-op | non | non | P4/Pr4 layout ; aucun test | granular fantôme |
| 4 | aucun | `PARAM_GRAN_SPREAD` | aucun | descriptor Gran Spread ; apply no-op | non | non | P4/Pr4 layout ; aucun test | granular fantôme |
| 5 | aucun | `PARAM_GRAN_STEREO` | aucun | descriptor Gran Stereo ; apply no-op | non | non | P4/Pr4 layout ; aucun test | granular fantôme |
| 6 | aucun | `PARAM_MIX_TRACK0_GAIN` | aucun | descriptor legacy `T0 Gain`, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 7 | `PARAM_MIX_REVERB_DIGITAL_DECAY` | `PARAM_MIX_TRACK1_GAIN` | global TONE Master / reverb Digital | descriptor Decay ; apply DSP actif | non | non | P4/Pr4 global ; tests rôle Special | collision sémantique, actif unique |
| 8 | `PARAM_MIX_REVERB_DIGITAL_DAMP` | `PARAM_MIX_TRACK2_GAIN` | global TONE Master / reverb Digital | descriptor Damp ; apply DSP actif | non | non | P4/Pr4 global | collision sémantique, actif unique |
| 9 | `PARAM_MIX_REVERB_DIGITAL_HPF` | `PARAM_MIX_TRACK3_GAIN` | global TONE Master / reverb Digital | descriptor HPF ; apply DSP actif | non | non | P4/Pr4 global | collision sémantique, actif unique |
| 10 | `PARAM_MIX_REVERB_DIGITAL_LPF` | `PARAM_MIX_TRACK0_PAN` | global TONE Master / reverb Digital | descriptor LPF ; apply DSP actif | non | non | P4/Pr4 global | collision sémantique, actif unique |
| 11 | aucun | `PARAM_MIX_TRACK1_PAN` | aucun | descriptor legacy `T1 Pan`, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 12 | aucun | `PARAM_MIX_TRACK2_PAN` | aucun | descriptor legacy `T2 Pan`, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 13 | aucun | `PARAM_MIX_TRACK3_PAN` | aucun | descriptor legacy `T3 Pan`, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 14 | `PARAM_MIX_MUTE` | `PARAM_MIX_TRACK0_MUTE` | MIX par track / `track_mute` + `track_sound_state.mix_mute` | descriptor Mute, apply catalogue nul ; apply track-aware actif | non | non | P4/Pr4 mix, TS/U, Pa3/K3 agrégés | alias historique, actif unique |
| 15 | `PARAM_MIX_REVERB_MODEL` | `PARAM_MIX_TRACK1_MUTE` | global TONE Master / sélection reverb | descriptor Model ; apply DSP actif | non | non | P4/Pr4 global ; `special_track_role_validation` | collision sémantique, actif unique |
| 16 | `PARAM_CFG_POLY_VOICES` | `PARAM_MIX_TRACK2_MUTE` | CFG par track / `synth_polyphony` | descriptor VOICES ; apply spécial registre actif | non | non | P4/Pr4 sound, TS/U, Pa3/K3 cardinalité ; tests CFG/voice | collision sémantique, actif unique |
| 17 | `PARAM_CFG_POLY_SPREAD` | `PARAM_MIX_TRACK3_MUTE` | CFG par track / `synth_polyphony` | descriptor SPREAD ; apply spécial registre actif | non | non | P4/Pr4 sound, TS/U, K3 ; exclu Pa3 ; test CFG | collision sémantique, actif unique |
| 18 | aucun | `PARAM_MIX_TRACK0_ROUTE` | aucun | descriptor legacy T0 Route, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 19 | aucun | `PARAM_MIX_TRACK1_ROUTE` | aucun | descriptor legacy T1 Route, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 20 | aucun | `PARAM_MIX_TRACK2_ROUTE` | aucun | descriptor legacy T2 Route, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 21 | `PARAM_DRUM_MD_MODEL` | `PARAM_MIX_TRACK3_ROUTE` | TONE par track / `track_tone_sound_state.md` et Drum MD | descriptor MODEL ; apply track-aware actif | oui | non | P4/Pr4 sound+p-lock, TS/U, Pa3/K3 ; pas de test ID dédié | collision sémantique, actif unique |
| 22 | `PARAM_DRUM_MD_P1` | `PARAM_MIX_TRACK0_INSERT0` | TONE Drum MD | descriptor P1 ; backend actif | oui | oui, si slot du profil | mêmes formats MD | collision sémantique, actif unique |
| 23 | `PARAM_DRUM_MD_P2` | `PARAM_MIX_TRACK0_INSERT1` | TONE Drum MD | descriptor P2 ; backend actif | oui | oui, si slot du profil | mêmes formats MD | collision sémantique, actif unique |
| 24 | `PARAM_DRUM_MD_P3` | `PARAM_MIX_TRACK1_INSERT0` | TONE Drum MD | descriptor P3 ; backend actif | oui | oui, si slot du profil | mêmes formats MD | collision sémantique, actif unique |
| 25 | `PARAM_DRUM_MD_P4` | `PARAM_MIX_TRACK1_INSERT1` | TONE Drum MD | descriptor P4 ; backend actif | oui | oui, si slot du profil | mêmes formats MD | collision sémantique, actif unique |
| 26 | `PARAM_DRUM_MD_P5` | `PARAM_MIX_TRACK2_INSERT0` | TONE Drum MD | descriptor P5 ; backend actif | oui | oui, si slot du profil | mêmes formats MD | collision sémantique, actif unique |
| 27 | `PARAM_DRUM_MD_P6` | `PARAM_MIX_TRACK2_INSERT1` | TONE Drum MD | descriptor P6 ; backend actif | oui | oui, si slot du profil | mêmes formats MD | collision sémantique, actif unique |
| 28 | `PARAM_DRUM_MD_P7` | `PARAM_MIX_TRACK3_INSERT0` | TONE Drum MD | descriptor P7 ; backend actif | oui | oui, si slot du profil | mêmes formats MD | collision sémantique, actif unique |
| 29 | `PARAM_DRUM_MD_P8` | `PARAM_MIX_TRACK3_INSERT1` | TONE Drum MD | descriptor P8 ; backend actif | oui | oui, si slot du profil | mêmes formats MD | collision sémantique, actif unique |
| 30 | aucun | `PARAM_MIX_TRACK0_SEND0` | aucun | descriptor legacy T0 Send0, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 31 | aucun | `PARAM_MIX_TRACK0_SEND1` | aucun | descriptor legacy T0 Send1, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 32 | aucun | `PARAM_MIX_TRACK1_SEND0` | aucun | descriptor legacy T1 Send0, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 33 | aucun | `PARAM_MIX_TRACK1_SEND1` | aucun | descriptor legacy T1 Send1, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 34 | aucun | `PARAM_MIX_TRACK2_SEND0` | aucun | descriptor legacy T2 Send0, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 35 | aucun | `PARAM_MIX_TRACK2_SEND1` | aucun | descriptor legacy T2 Send1, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 36 | aucun | `PARAM_MIX_TRACK3_SEND0` | aucun | descriptor legacy T3 Send0, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 37 | aucun | `PARAM_MIX_TRACK3_SEND1` | aucun | descriptor legacy T3 Send1, apply nul | non | non | P4/Pr4 layout | mort / tombstone |
| 174 | `PARAM_MIX_REVERB_DAMP` | `PARAM_MIX_DELAY_SWING` | global TONE Master / reverb Mutable | descriptor Damp ; apply DSP actif | non | non | P4/Pr4 global | collision sémantique, actif unique |
| 175 | `PARAM_MIX_REVERB_SMEAR` | `PARAM_MIX_DELAY_ACCENT` | global TONE Master / reverb Mutable | descriptor Smear ; apply DSP actif | non | non | P4/Pr4 global | collision sémantique, actif unique |

Les plages MIX historiques sont donc intégralement couvertes : gain `6..9`, pan `10..13`, mute `14..17`, route `18..21`, inserts `22..29`, sends `30..37`. `PARAM_MIX_SEND0_FX` et `PARAM_MIX_SEND1_FX` (`38..39`) ne sont pas des aliases `TRACKx`; ils sont actifs, uniques et hors collision. Les contrôles MIX track-aware modernes `40..43` sont également uniques.

## 3. Consommateurs transverses

| Couche | Type de consommation | Contrat / impact d'un changement d'ID |
|---|---|---|
| `param_store.h` | valeur et layout | enum `0..322`, aliases préprocesseur, `PARAM_PERSIST_COUNT=307`; source de toutes les dimensions persistées |
| catalogue/descriptors | symbole C + initializer indexé | un descriptor par entier ; plusieurs entrées utilisent encore le vieux symbole comme désignateur malgré leur sens moderne |
| `track_runtime_get_param_rule` | symbole C | MD=TONE, mute=MIX, VOICES/SPREAD=CFG ; granular, reverb globale et tombstones restent domain NONE |
| apply Param | symbole C + plages contiguës MD | dispatch custom polyphonie/mute/MD ; wrappers reverb globaux ; `MODEL..P8` et soustractions `P1` supposent la contiguïté 21..29 |
| p-locks | domaine puis mapping construit par ordre numérique | MD MODEL/P1..P8 ont des slots TONE persistés ; CFG et globals refusés ; une réorganisation TONE peut changer la signification d'un `param_slot` Pattern sans modifier sa taille |
| modulation | domaine, whitelist et plages MD | P1..P8 seulement, selon cardinalité du profil ; MODEL, CFG, globals, granular et tombstones refusés |
| macros | domaine puis mapping p-lock | MD p-lockable donc macro-assignable ; MIX produit autorisé ; Project persiste en plus le `param_id_t` brut dans chaque lock de scène |
| clipboard page/ensemble | symbole C copié en RAM | tableaux `params[PARAM_COUNT]`; intersection par ID ; aucune version SD |
| snapshot Track / Undo | layouts agrégés + locks RAM | `track_sound_state`, `track_tone_sound_state`, voix/spread et p-lock slots ; redémarrage suffit pour invalider, mais tous les parcours doivent rester cohérents pendant une session |
| Pattern v4 | layout persistant + slots | matrices `sound`, `mix`, `globals` indexées par `PARAM_PERSIST_COUNT`; p-locks stockent `set_id + param_slot`; reverb globales et branches de plage actives |
| Project v4 | layout persistant + valeur brute | embarque `PatternSaveV1` et stocke les IDs bruts des locks Macro ; double motif d'invalidation |
| Patch v3 | layout agrégé, pas d'ID brut | MD et mute dans états agrégés, cardinalité voix séparée ; boucle de reapply parcourt les symboles courants ; pas de reverb globale ni granular |
| tests | symboles et valeurs attendues | `cfg_polyphony_ownership_validation.ps1` fige explicitement 16/17 et les aliases ; `synth_voice_budget_validation.ps1`, `special_track_role_validation.ps1`, `env_ownership_validation.ps1` couvrent des frontières connexes ; aucun test exhaustif MD/reverb/granular n'existe |
| outils | parsing enum/layout | `tools/patch_bank/generate_musical_patch_bank.ps1` parse `PARAM_COUNT` et encode le layout Patch agrégé ; tout changement exige régénération/validation de banque |

La consommation par plage numérique réellement trouvée se limite à :

- `PARAM_MIX_TRACK0_GAIN..PARAM_MIX_TRACK3_SEND1` dans deux branches de `pattern_live_ram.c` ;
- `PARAM_DRUM_MD_MODEL..PARAM_DRUM_MD_P8` dans runtime/UI/backend audio ;
- `PARAM_DRUM_MD_P1..PARAM_DRUM_MD_P8` et `id - PARAM_DRUM_MD_P1` dans UI, registre, backend et modulation.

Aucun accès par littéral numérique `6..37`, `174` ou `175` n'a été trouvé dans le firmware, les tests ou les outils ciblés.

## 4. Branches compensatoires historiques

| Branche | État après 4A/4B | Suppression | Comportement protégé |
|---|---|---|---|
| `pattern_live_is_global_param_useful`: exception initiale pour MODEL + quatre paramètres Digital aux IDs 7..10/15 | active | sans renumérotation, si ces IDs reçoivent une classification globale canonique ou une table explicite | capture des globals reverb malgré leur ancien emplacement MIX |
| même fonction : rejet de toute la plage `6..37`, sauf `PARAM_MIX_MUTE` | active | sans renumérotation, via statut explicite par ID plutôt qu'une plage historique | empêche les lanes physiques mortes d'être sauvées comme globals |
| `pattern_live_is_reverb_global_tombstone` | active | sans renumérotation avec la même classification canonique | autorise les cinq globals reverb lors du restore |
| `pattern_live_transition_reapply`: filtre de plage `6..37`, exception mute + helper reverb | active | sans renumérotation | évite de rejouer les tombstones comme globals tout en restaurant la reverb |
| fallback VOICES `mix.track_valid -> sound` | **supprimé en 4B** | déjà supprimé | ancienne ownership PLAY/MIX |
| restore MIX non conditionné par domaine | **supprimé en 4B** | déjà supprimé | ancien stockage indifférencié |
| effacement de `mix.track_valid` pour VOICES/SPREAD après copie du snapshot | actif mais défensif | sans renumérotation après validation/canonicalisation explicite du payload | empêche un payload v4 mal formé de garder une trace MIX, bien qu'elle ne soit plus appliquée |
| tables MIX à huit slots contenant VCA | **supprimées en 4A** | déjà supprimé | ancien ownership VCA=MIX ; le set MIX ne contient plus que 4 contrôles produit |
| descriptors désignés par anciens noms MIX | actif, non fonctionnel | sans renumérotation | stabilité des valeurs uniquement ; aucun comportement physique |
| plages contiguës MD et calculs `id-P1` | actives, mais non compensatoires MIX | seulement en gardant MD contigu ou en remplaçant par table | ordre MODEL puis P1..P8, profils, p-lock MODEL prioritaire |
| migrations de fichiers anciennes | absentes | sans objet | les versions/taille sont strictes ; aucune conversion MIX historique n'est encore exécutée |

La documentation Z3 parlant de tombstones « load-only » ou de migration surestime donc le code actuel : il n'existe plus de loader physique MIX. Il reste un filtre de layout courant et des exceptions reverb, pas une compatibilité d'ancien format.

## 5. Granular

- IDs/descriptors compilés : six IDs `0..5`, six descriptors et six déclarations de wrappers.
- Apply : les six fonctions `apply_gran_*` consomment uniquement `(void)v`; aucun état ni DSP n'est modifié.
- UI : la seule banque apparaît comme valeur d'initialisation de `g_ui_param`, mais `valid=0`; les templates produit la remplacent avant toute édition utile.
- Pool : `FX_GRANULAR` reste dans `fx_type_t`; `fx_pool_activate_slot()` le refuse immédiatement (`return 0`).
- Backend : `Src/Audio/fx_granular.cpp` et son header existent, mais le `.cpp` est explicitement retiré de `SRC_AUDIO_SRC` par CMake.
- Tests/outils : aucun test ni outil actif ne référence les paramètres ou le backend granular.
- Dépendance numérique : supprimer physiquement les six premières entrées décale tous les IDs suivants, `PARAM_PERSIST_COUNT`, `PARAM_COUNT`, les matrices Pattern/Project, les IDs Macro Project et potentiellement les slots p-lock par domaine. Supprimer seulement les symboles produit en laissant `0..5` réservés ne décale rien.

Conclusion granular : **suppression complète du produit et du code compilé, avec conservation temporaire de six slots réservés neutres**. Le source backend peut être supprimé ou archivé sous `Inspiration/` lors d'une passe dédiée ; le conserver dans `Src/Audio` sans build n'est pas justifié. La compaction des six ordinaux est reportée.

## 6. Scénarios de nettoyage

| Critère | A — pas de renumérotation | B — renumérotation minimale | C — reconstruction complète |
|---|---|---|---|
| Portée | rendre canoniques les symboles modernes aux mêmes ordinaux ; renommer les morts en réservés ; supprimer aliases, ranges historiques et granular produit | déplacer les 19 concepts modernes recyclés (mute, 2 CFG, 9 MD, 7 reverb), laisser les autres ordinaux | réordonner/compacter tout le registre, supprimer granular et tombstones |
| Gain sémantique | élevé : un nom canonique par valeur et plus de logique MIX mensongère | moyen à élevé, mais les numéros eux-mêmes ne donnent aucun contrat utile | élevé sur le papier, faible gain produit supplémentaire par rapport à A |
| Ordre de grandeur | ~45 symboles, 12–18 fichiers, 4–8 tests | ~19 IDs déplacés, 20–30 fichiers, 8–12 tests/fixtures | 323 IDs audités, 35–60 fichiers, outils et toutes les fixtures |
| Formats | **conserver P4/Pr4/Pa3/K3** | **P5 + Pr5**, invalidation franche ; Pa3/K3 conservables si leurs structs agrégés ne changent pas | **P5 + Pr5** au minimum ; Pa3/K3 restent techniquement compatibles si structs inchangés, sinon bump conjoint explicite à v4 |
| Tests requis | unicité/coverage registre, classification de chaque réservé, capture/restore reverb+CFG+MD, p-lock MD, macro, clipboard/undo, deux variantes | A + round-trip/rejet P4/Pr4, mapping de chaque p-lock, locks Macro bruts, fichiers mal formés | B + matrice exhaustive des 323 IDs, banque Patch, tous domaines et outils |
| Corruption silencieuse | faible si chaque ordinal conserve exactement son sens courant | élevée sans bump : globals/locks Macro changent d'index et slots MD peuvent viser un autre TONE | très élevée sans invalidation globale et couverture exhaustive |
| Revert | facile, mécanique et atomique par sous-étape | moyen ; exige revert code + fixtures/formats | difficile ; gros commit transversal |
| Redondance H747 | faible : clarifie le seam à porter | forte | très forte |
| Gain CPU/RAM | aucun revendiqué | matrices Pattern/Project grossissent si IDs ajoutés ; aucun gain | compaction pourrait réduire des matrices, mais aucun bénéfice mesuré ne justifie le risque |

Le scénario B n'a pas de « concepts encore réellement en collision » au sens runtime : chaque valeur n'a qu'un sens actif. Il ne résout donc pas un bug que A laisserait présent. Sa seule justification serait une rupture volontaire d'identité numérique, insuffisante ici.

## 7. Recommandation d'exécution

Exécuter **A sous forme de `PARTIAL CLEANUP`**, sans compaction :

1. **Granular produit** : retirer la banque UI par défaut, les descriptors et wrappers no-op, `FX_GRANULAR`, header/backend de `Src/Audio`; garder les ordinaux `0..5` sous six noms `PARAM_RESERVED_*` avec descriptors inertes explicites si le registre exige une couverture totale.
2. **Canonisation sans déplacement** : déclarer directement aux ordinaux existants `PARAM_MIX_MUTE`, `PARAM_CFG_POLY_*`, `PARAM_DRUM_MD_*` et les sept contrôles reverb ; retirer les `#define` aliases. Remplacer chaque slot réellement mort de `6..37` par un réservé neutre, sans ancien label MIX.
3. **Classification persistence** : remplacer les deux comparaisons de plage MIX et le helper « reverb tombstone » par une propriété/table explicite `track/global/reserved`. Ne pas réintroduire de fallback `mix -> sound`.
4. **Plages MD** : conserver MODEL puis P1..P8 contigus aux mêmes valeurs, ou remplacer toutes les arithmétiques par une table dans une sous-étape distincte ; ne pas mélanger ce choix avec granular.
5. **Validation** : ajouter un test qui énumère 0..`PARAM_COUNT-1`, prouve l'unicité des descriptors actifs, l'inertie des réservés, la classification persistence, les exclusions CFG, les neuf p-locks MD et leurs destinations de modulation ; faire les round-trips P4/Pr4/Pa3/K3 et les deux variantes.

Versions à conserver pour ce plan : **Pattern v4, Project v4, Patch v3**. Aucun layout, aucune taille, aucun mapping p-lock et aucun sens d'ordinal courant ne doit changer. Si une sous-étape ne peut respecter cette condition, elle sort du scénario A et doit être reportée comme rupture P5/Pr5, avec rejet strict des anciens payloads plutôt qu'une compatibilité partielle.

Dette à conserver avant H747 : les ordinaux historiques dispersés, les six slots granular réservés, les slots MIX réservés et le fait que les globals reverb ne sont pas encore déclarés `GLOBAL_ALLOWED` par Z2. Cette dette est documentaire et de lisibilité ; elle ne justifie pas à elle seule une reconstruction numérique.

## Validation de l'audit

- base validée : commits `559bc74a5` et `31a860f58` présents en tête d'historique ;
- descriptors : 321 initializers uniques, aucun doublon ; deux trous sans rapport signalés explicitement ;
- comparaisons de plages : deux plages MIX persistence et les plages contiguës MD recensées ; aucun littéral numérique caché trouvé ;
- versions confirmées dans le code : Pattern **v4**, Project **v4**, Patch **v3** ;
- VOICES/SPREAD confirmés CFG, ressource SYNTH, non p-lockables, non modulables et sans fallback Pattern MIX après `31a860f58`.
