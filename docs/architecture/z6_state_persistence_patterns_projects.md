# Z6 — État, persistence, patterns et projects

Z6 décrit les snapshots, les banques SD et les règles de restore du produit actuel. Les implémentations `pattern_live`, `pattern_sd_bank`, `project_v1`, `patch_v1` et `kit_v1` restent l'autorité.

## Versions courantes

| Format | Version actuelle | Scope |
|---|---:|---|
| Pattern | v5 | état Play/Special, globals et slots p-lock compacts |
| Project | v5 | projet, pattern embarqué, scènes Macro et slots p-lock compacts |
| Patch | v3 | une Play Track, Play-only |
| Kit | v3 | snapshot de kit selon les rôles compatibles |

Les noms techniques `PatternSaveV1`, `ProjectSaveV1`, `PatchSaveV1` et `KitSaveV1`, ainsi que les API `*_v1`, ne sont pas renommés dans cette passe. Ils ne changent pas les versions de fichier indiquées ci-dessus. Les payloads d'une autre version sont refusés par validation stricte ; aucune migration n'est ajoutée.

## Contrat final des p-locks compacts

Les six sets conservent leurs ordinals et utilisent directement le slot compact comme identité runtime et stockage :

| Set | Ordinal | Capacité | Offset runtime |
|---|---:|---:|---:|
| ENV | 0 | 25 | 0 |
| TONE | 1 | 21 | 25 |
| PLAY | 2 | 16 | 46 |
| MOD | 3 | 12 | 62 |
| MIDI_FX | 4 | 16 | 74 |
| MIX | 5 | 4 | 90 |
| **Total** |  | **94 slots/track** |  |

Pattern, Project, Clipboard, Undo/Redo et le modèle séquenceur stockent donc `set_id + param_slot` avec `param_slot` compact. Les slots historiques, les tables de traduction et toute migration d'anciens fichiers sont volontairement absents. Pattern/Project v5 acceptent uniquement le format courant ; un en-tête ou un payload incompatible est rejeté.

Les slots TONE sont résolus par les tables du moteur actif. Chaque moteur couvert par `track_runtime` fournit au plus 21 paramètres ; un slot hors table du moteur courant est refusé et l'état TONE est revalidé lors d'un changement de moteur.

Décision Looper : `ARM`, `LEN`, `PLAY` et `XFADE` sont p-lockables ; `STRETCH`, `PITCH` et `GRAIN` sont exclus. Cette allowlist est l'autorité unique du caractère p-lockable. Les exclusions des autres paramètres structurels restent centralisées dans l'interface séquenceur.

Les limites musicales ne changent pas : 64 steps par track, 32 locks maximum par step Play, 16 locks maximum par step Special, mêmes pools Play/Special et aucune allocation dynamique.

## Mesure finale RAM_D2

Le budget isolé des anciens états/mappings p-lock était de 80 573 B :

| Symbole / groupe | Avant | Après | Zone après |
|---|---:|---:|---|
| `g_seq_param_state` → `g_seq_param_runtime_state` | 71 680 B | 5 264 B | RAM_D2 |
| `g_seq_param_mix_state` | 224 B | 0 B | supprimé |
| `g_seq_param_base_valid_bits` | 2 247 B | 165 B | RAM_D2 |
| `g_seq_param_runtime_locked_bits` | 2 247 B | 165 B | RAM_D2 |
| `g_seq_param_id_to_slot` | 1 615 B | 0 B RAM | Flash const |
| `g_seq_param_slot_to_id` | 2 560 B | 0 B RAM | Flash const |
| **Total p-lock** | **80 573 B** | **5 594 B** | **gain 74 979 B** |

La comparaison des sections `RAM_D2` des maps de référence et finales donne :

| Variante | Avant | Après | Delta |
|---|---:|---:|---:|
| Release Low-Cost | 232 576 B | 153 504 B | −79 072 B |
| Release Premium | 243 424 B | 168 224 B | −75 200 B |

Les tables de mapping compactes ajoutent 852 B en Flash (`const`) et ne consomment plus de RAM. Le critère Low-Cost de 74 900 B récupérés est atteint.

## Ownership et identité

Chaque track persistée est identifiée par `role + ordinal` issu de `track_topology`. Avant toute mutation, le restore vérifie la variante, la présence du rôle, la compatibilité de la famille/type, les capacités et les ressources exclusives.

- les huit Play Tracks restent indépendantes ;
- les Special restent Input, Looper, FX et Master ;
- Pattern/Project stockent des séquences Play et Special distinctes ;
- Patch capture et applique une Play Track seulement ;
- Kit capture les états de son scope et ne convertit pas une Special en Play ;
- le binding audio, les voix, les playheads et les autres états transitoires sont reconstruits par les autorités runtime.

Master porte les globals reverb, delay et compresseur. Les valeurs reverb Mutable `WET`, `SIZE`, `DECAY`, `PRED`, `DAMP`, `HPF`, `LPF` et `SMEAR` utilisent les matrices globales existantes et sont capturées/restaurées comme les autres globals Master. FX porte les quatre MacroFX. Looper et Input conservent leurs rôles fixes. Aucun fichier ne déduit ces ownerships d'un index de lane physique.

## Classification des paramètres

La persistence utilise une classification explicite par `param_id` :

1. global ;
2. track-aware ;
3. réservé/inactif.

La classification track-aware est ensuite répartie selon le domaine courant :

- `CFG` dans l'état de configuration/sound approprié ;
- `ENV` pour FLT, VCA, ENV3 et retriggers ;
- `TONE` pour moteurs et surfaces de rôle ;
- `MOD` pour LFO, Matrix, Multi et Slew ;
- `MIX` pour niveau, pan, sends et mute ;
- `PLAY` pour les données de jeu et les p-locks PLAY ;
- `MIDI_FX` pour les bases MIDI FX et leurs locks de Pattern/Project.

`VOICES` et `SPREAD` sont `CFG`, non p-lockables et non modulables. Ils sont persistés avec la configuration de track et réappliqués via `synth_polyphony`.

Les IDs `0..5` sont réservés à l'ancien granular et ne sont pas des paramètres produits. Les autres ordinaux réservés restent inertes. Les anciennes lanes physiques MIX ne sont ni une catégorie persistante ni une règle de fallback. Un paramètre est stocké parce que sa classification courante le permet, pas parce qu'il appartient à une plage historique.

## Pattern et Project

Pattern v5 capture l'état live courant, les valeurs track/global classifiées, les locks par set/slot compact, les séquences, la configuration Play/Special et les bases MIDI FX autorisées. Project v5 enveloppe le pattern, l'état de projet, les autoloads et les scènes Macro/locks.

Le restore des globals reverb passe par `param_set`, donc réapplique leurs callbacks DSP dans le batch existant. Les slots p-lock sont validés par set, capacité et moteur TONE avant application ; Pattern et Project restent v5, Patch et Kit restent v3.

Le restore suit un ordre borné : validation de l'en-tête et du checksum, validation des identités et versions, capture/arrêt des états temporaires nécessaires, application des valeurs canoniques, reconstruction des projections runtime, puis restauration UI/transport autorisée. Une erreur de validation ne réalise aucune mutation partielle.

Avant les validations track-aware et toute mutation, Pattern construit une bijection bornée `identité stockée role+ordinal -> index topology courant`. Cette normalisation en mémoire remappe configuration, sound, mix, séquences, actions Special, routes et tables track-indexées. Project réutilise la même identité embarquée pour `multi[]` et les cibles de locks Macro; Kit normalise ses payloads et résumés par leur identité propre. Une identité absente, dupliquée ou invalide refuse le payload sans fallback par index. Les formats, tailles, offsets et checksums restent inchangés.

Les locks MIDI FX persistent comme `set_id + param_slot + value16` dans Pattern/Project. Les bases `note_fx_state` de huit Play Tracks appartiennent au snapshot musical ; l'état d'exécution MIDI FX est reconstruit et n'est pas écrit comme état audio transitoire.

Les scènes Macro Project sont des scènes et des locks de paramètres. Les quatre pots pointent vers des scènes ; aucune banque Macro ancienne ne constitue un second format ou une seconde autorité.

## Patch et Kit

Patch v3 capture exactement une Play Track et peut être appliqué indépendamment à plusieurs Play Tracks compatibles. Il ne capture ni Special, ni globals Master, ni état runtime MIDI FX.

Kit v3 conserve son snapshot agrégé pour les tracks de son compatibles, avec ENV comme owner logique de FLT/VCA/ENV3, CFG comme owner de `VOICES`/`SPREAD`, et les paramètres TONE/MOD/MIX selon leur classification courante. Les backends mixer VCA et `mod_env3` sont seulement des cibles d'application.

## Séquences

Une Play Track persiste 64 steps et jusqu'à 32 p-locks par step dans son pool Play. Une Special persiste 64 steps et jusqu'à 16 locks dans son pool Special ; ses données sont limitées à longueur/page, action et automatisation non-PLAY.

Les notes, états de voix, files scheduler, fenêtres ARP, historique MacroFX, cache audio, pages sampler et autre état transitoire ne sont pas des payloads persistants. STOP/START, load et restore passent par les guards et snapshots dédiés pour éviter les notes fantômes.

## Entrées, samples et ressources exclusives

Le choix d'entrée d'une Play `External` est persisté avec la configuration et doit rester identique lors d'un restore. Si l'entrée est réservée, le restore ou le paste échoue atomiquement ; il n'existe aucun fallback vers une autre entrée.

Les références sample, Multi, RAM, Wave et Looper stockent leurs métadonnées et identifiants propres. Le chargement audio, la page-cache, les readers et les buffers sont reconstruits hors IRQ par leurs pools ; ils ne sont pas confondus avec le snapshot musical.

## Contrat de refus

Sont refusés : version non courante, checksum invalide, rôle absent, identité incompatible, paramètre réservé, domaine non supporté, p-lock non autorisé, cible non-Play pour Patch, entrée déjà réservée et payload qui tente de restaurer un état runtime transitoire.

## Historique utile

Les références V1 dans les noms de structures/API sont conservées comme interfaces techniques. Pattern/Project v5, Kit/Patch v3, une migration MIX physique ou une persistence granular ne sont pas des formats alternatifs ; ils ne doivent pas être présentés comme des chemins de restore concurrents.
