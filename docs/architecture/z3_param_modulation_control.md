# Z3 — Paramètres, modulation et contrôle

Z3 définit le registre des paramètres, leur ownership logique, les p-locks, la modulation et l'application track-aware. Les symboles et tables du code courant sont l'autorité.

## Autorité du registre

`param_store` définit les IDs et leurs ordinaux stables. `param_registry` définit les descriptors, bornes, valeurs, affichage et apply. `track_runtime_get_param_rule()` fournit pour chaque paramètre courant : domaine, ressource, cardinalité et statut. Les consumers ne reconstruisent pas ces règles par plage numérique.

La chaîne d'écriture est :

`UI / séquence / modulation / Macro → validation runtime → apply backend → base canonique → notification explicite`.

Les transitions structurelles passent par les commandes dédiées et les snapshots ; elles ne sont pas déguisées en p-locks.

## Domaines actuels

| Domaine | Ownership produit | Contenu courant |
|---|---|---|
| `CFG` | configuration Play | famille/type, MIDI, `VOICES`, `SPREAD` |
| `ENV` | enveloppes et filtre | FLT, VCA, ENV3, retriggers |
| `TONE` | moteur/rôle | synth, sampler, drum, Looper, Master ou FX |
| `MOD` | modulation | LFO, Matrix, Multi, Slew |
| `MIX` | mix track-aware | level, pan, sends, mute |
| `PLAY` | séquence/jeu | voix PLAY et commandes de jeu |
| `MIDI_FX` | modèle MIDI FX | quatre slots par Play Track |

ENV est le propriétaire logique unique des paramètres filtre, VCA, ENV3 et de leurs retriggers. Le paramètre peut utiliser une ressource interne différente : le VCA est appliqué au backend mixer et ENV3 au backend `mod_env3`. Cette différence d'exécution ne crée ni ensemble VCA ni domaine MIX/MOD autonome.

Les paramètres Master globaux reverb, delay et compresseur sont classés explicitement comme globals. Les paramètres MacroFX sont `TONE` mais sont valides uniquement pour le rôle topologique FX. `MIX` ne possède pas les MacroFX.

`CFG_POLY_VOICES` et `CFG_POLY_SPREAD` appartiennent à CFG, sont appliqués par `synth_polyphony`, ne sont ni p-lockables ni modulables et ne sont pas une capacité PLAY.

## P-locks

Les sets courant sont `ENV`, `TONE`, `PLAY`, `MOD`, `MIDI_FX` et `MIX`. La résolution `set + slot ↔ param_id` est track-aware et vérifie le domaine, la capacité, le type de track et le statut effectif.

- ENV contient les paramètres FLT, VCA, ENV3 et retriggers ;
- TONE contient les contrôles moteur réellement supportés ;
- MOD n'accepte que les destinations explicitement valides pour la track ;
- MIX contient seulement les contrôles de mixage ;
- CFG, globals et paramètres structurels non autorisés sont refusés ;
- MIDI FX utilise son mapping fixe de seize IDs génériques par Play Track.

Un p-lock MIDI FX modifie l'overlay runtime de la track cible, jamais les bases `note_fx_state`. Le changement de modèle nettoie l'overlay avant de restaurer les bases ; aucune Special ne possède de state MIDI FX.

Le scheduler applique les locks non-PLAY aux boundaries prévues et les locks PLAY dans le chemin PLAY. Toute application et toute restauration identifie explicitement la track logique cible.

## Modulation

Les destinations LFO/Matrix proviennent d'une allowlist runtime construite pour la track active. Seuls les domaines produits réellement supportés par cette track sont éligibles : principalement ENV, TONE et les contrôles MIX autorisés par le contrat courant. CFG, PLAY structurel et MIDI FX ne deviennent pas des destinations par fallback.

Le moteur de modulation applique les overlays sans remplacer la base canonique. La libération restaure la base puis les sources encore actives dans un ordre borné. Les backends VCA, ENV3, filtre et moteurs restent propriétaires de leur exécution.

## Persistence, clipboard et Macro

La persistence utilise la classification explicite de `pattern_live` : global, track-aware ou réservé. Une ancienne plage d'IDs MIX n'est jamais utilisée pour décider du stockage. Les IDs réservés sont refusés par le registre, les p-locks, la modulation et les writes de persistence.

Les formats courants sont Pattern v4, Project v4, Patch v3 et Kit v3. Les snapshots agrégés réappliquent les paramètres par leurs symboles et leurs règles actuelles ; Patch reste Play-only.

Le clipboard et l'undo opèrent par intersection d'IDs compatibles avec le domaine et le scope. Ils capturent les valeurs canoniques et les locks autorisés, jamais les états audio transitoires. Les scènes Macro/locks Project utilisent l'API scène/lock courante ; les anciens noms d'API persistence contenant `V1` restent inchangés.

La projection des quatre pots utilise `param_macro_set_amount()` et `param_macro_sync_scene_sources()` ; les sources de scène sont synchronisées sans recréer une banque Macro parallèle.

## IDs réservés et granular

Les IDs `PARAM_RESERVED_000` à `PARAM_RESERVED_005` sont conservés pour stabilité. Les réserves `006`, `011..013`, `018..020` et `030..037` couvrent également des ordinaux sans ownership produit. Elles ont des descriptors neutres et sont inertes.

Le granular produit est supprimé : pas de famille, capacité, page, p-lock, modulation, backend compilé ou slot FX actif. Les six premiers ordinaux ne sont pas compactés dans cette passe afin de ne pas déplacer les IDs suivants.

## MacroFX et représentations internes

Les quatre MacroFX sont des paramètres TONE appartenant au rôle FX. Le backend `fx_master_macro` est une insertion master-bus DSP ; le nom est techniquement légitime et ne doit pas être interprété comme `Master` propriétaire.

Les backends `mixer`/VCA et `mod_env3` sont également des représentations internes légitimes de l'owner ENV. Aucun renommage cosmétique de ces backends n'est requis.

## Historique utile

`COLORS`, VCA comme propriétaire MIX, ENV3 comme propriétaire MOD, les lanes physiques MIX et le granular actif décrivent des contrats précédents ou des tombstones d'IDs. Ils ne décrivent pas le produit courant et ne doivent pas être réintroduits comme couches de résolution.
