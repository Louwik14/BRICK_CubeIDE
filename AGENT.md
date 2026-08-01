# AGENT.md

## Rôle

Ce fichier guide Codex pour travailler efficacement dans ce repo.

Objectif :
- comprendre les invariants structurants du projet
- auditer le code réel avant modification
- modifier proprement sans casser les autorités runtime
- brancher les features au bon endroit
- garder `AGENT.md` et `ARCHITECTURE_GLOBAL.md` à jour si la structure évolue

Ce fichier n’est pas la doc d’architecture complète.
La carte globale vit dans `ARCHITECTURE_GLOBAL.md`.
Le détail local vit dans `docs/architecture/z*.md`.
Le `readme.md` reste la source produit à croiser avec le code réel.

---

## 1. Format de travail attendu

Mode d’exécution:
- ne pas expliquer en cours de passe ce qui est en train d’être fait
- ne pas annoncer les étapes, scans, lectures, vérifications ou intentions
- ne pas produire de messages de progression, de plan, ni de commentaires intermédiaires
- travailler silencieusement jusqu’au résultat final
- réponse finale minimale uniquement:
  1. verdict court
  2. micro-patch ou absence de patch
  3. docs mises à jour ou "aucune mise à jour doc requise"
  4. dépendances hors zone touchées, si applicable, en une ligne
- aucun commentaire intermédiaire
- aucun “je vais”, “je vérifie”, “je poursuis”, “scan ciblé”, “lecture”, “audit en cours”
- pas de narration de la passe
- si tu n’as pas encore le résultat final, ne réponds pas

- ne pas dupliquer dans la réponse le contenu d’un document qui vient d’être produit

Si un document est produit, la réponse finale doit être minimale :
1. fichiers éventuellement élargis hors périmètre initial, avec raison courte
2. confirmation que le document a été produit
3. impact éventuel sur la cartographie globale en 1 à 3 lignes max

Si la passe traite un bug :
4. cause trouvée : oui/non

En fin de passe, indiquer obligatoirement :
- documents mis à jour
- ou `aucune mise à jour doc requise`

---

## 2. Philosophie de travail

- commencer par `ARCHITECTURE_GLOBAL.md` pour identifier la bonne zone
- lire ensuite le ou les documents `docs/architecture/z*.md` pertinents
- utiliser la cartographie pour éviter de repartir de zéro à chaque passe
- auditer ensuite uniquement le code réel nécessaire dans la zone visée et ses dépendances utiles
- étendre l’existant avant de créer
- réutiliser un backend ou sous-système existant avant d’en introduire un nouveau
- éviter les doubles autorités sur un même état runtime
- ne pas confondre logique, runtime et physique : utiliser des remaps explicites
- penser le projet comme une séparation nette entre :
  - état canonique / contrôle
  - projection runtime
  - exécution
- préparer les seams pour un futur split dual-core par découpage propre, pas par bus ou IPC prématuré
- refuser les nœuds centraux ambigus et les autorités partagées implicites
- pas de refonte gratuite
- pas de malloc dans le runtime critique
- garder CPU et worst-case bornés
- priorité absolue : stabilité audio temps réel

---

## 3. Ordre d’autorité

Pour comprendre ou modifier le projet :

1. code réel
2. `docs/architecture/z*.md`
3. `ARCHITECTURE_GLOBAL.md`
4. `AGENT.md`
5. `readme.md`

Règles :
- en cas de conflit, le code réel prime
- les docs servent à cartographier le réel, pas à l’imposer
- ne jamais patcher uniquement pour “faire coller” le code à une doc

### Politique sauvegardes prototype
- le projet est en phase de prototypage
- la retrocompatibilite des projets, patterns et presets n'est pas requise
- les formats persistés courants Pattern et Project sont en version 4; Kit et Patch restent en version 3
- une évolution de structure modifie le format courant; aucune migration, conversion ni rétrocompatibilité ne doit être ajoutée sans demande explicite
- les anciens payloads incompatibles sont refusés par validation stricte; ils peuvent devenir illisibles sans traitement particulier
- toute modification de version est explicite et cohérente sur le format concerné

---

## 4. Invariants système globaux

Le projet est **track-aware**.

Toujours raisonner avec :
- track active
- identité logique de track
- family de track
- type de track
- runtime associé
- capacité réelle du backend ciblé

La bonne lecture du projet est :
- la logique canonique décide
- la projection runtime traduit selon la track active et ses capacités
- l'exécution consomme de façon bornée

Cette séparation doit rester visible dans les nouveaux seams.

Ne pas ajouter une feature “globale” si elle dépend en réalité :
- d’une track active
- d’une identité runtime spéciale
- d’un backend exclusif déjà existant

### Contraintes temps réel
- audio en IRQ
- pas de RTOS
- pas d’allocation dynamique dans le chemin critique
- pas d’appel bloquant
- usage CPU prédictible
- comportement borné en worst-case

### Autorités runtime
- `track_topology` est l'autorité des rôles, de leur présence et des cardinalités selon la variante.
- L’autorité de binding reste `track_runtime`.
- `track_runtime` est la projection autoritaire des familles, types, capacités et bindings.
- Le mapping `UI track -> mix target runtime` est explicite et unique.
- Le mapping `UI track -> cible DSP physique` ne doit jamais être supposé implicite.
- Les getters/checks runtime ne doivent pas déclencher de refresh implicite, sauf exception explicitement auditée et documentée.
- L’invalidation reste explicite (`track_runtime_invalidate_all`) et le refresh est demandé explicitement par les appelants autorisés.
- Ne jamais créer une seconde autorité parallèle pour un état déjà porté par le runtime, le mixer ou un backend audio existant.
- Le futur dual-core se prépare par seams explicites, pas par une infrastructure centrale artificielle.

---

## 5. Modèle tracks actuel

### Cardinalités
- topologie produit autoritative : 12 tracks Low-Cost (`8 Play + 4 Special`) et 14 tracks Premium (`8 Play + 6 Special`)
- le stockage commun Low-Cost/Premium conserve une capacité de 14 slots; la sélection/navigation expose 12 tracks Low-Cost et 14 Premium
- `MAX_TRACKS` côté DSP ingress physique reste distinct
- les concepts “track logique” et “lane physique audio” sont distincts

### Layout logique actuel
- 8 Play Tracks
- Special Tracks fixes : Master, Looper, Input et FX
- Low-Cost : Master + Looper + Input1 + FX
- Premium : Master + Looper + Input1 + Input2 + Input3 + FX
- les Special affichent une identité CFG fixe et ne sont jamais proposées comme families/types convertibles; Master et FX sont dérivés directement du rôle topologique, sans adaptateur family/type UI partagé
- Master porte les effets globaux reverb, delay et compresseur via `TONE`; FX porte exclusivement les quatre slots MacroFX; `MIX` reste limité au mix par track

### Families configurables des Play Tracks
- `Off`
- `Synth`
- `Sampler`
- `Drum`
- `MIDI`
- `External`

### Types actuels

#### Pour `MIDI`
- `MIDI`

#### Pour `External`
- `External`

#### Pour `Synth`
- `Prism`
- `Wave`
- `Stack`
- `DELUGE`

#### Pour `Sampler`
- `RAM`
- `Stream`
- `Multi`

#### Pour `Drum`
- `TRX BD`
- `BD Analog`

### Notes
- `Off` = vraie désactivation runtime.
- le produit possède au maximum trois entrées physiques : Input1 en Low-Cost, Input1..3 en Premium
- `Input4` n'existe ni dans la topologie ni dans les enums de configuration.
- `MIDI` est une Play Track de notes sans chemin audio local.
- `External` est une Play Track MIDI + audio qui réserve exactement une entrée physique; `track_input_ownership` est l'unique autorité de cette réservation.
- une entrée réservée par `External` reste visible sur sa Special fixe avec `USED Pn`, sans second monitoring ni choix automatique d'une autre entrée.
- le type historique `Input/Hybrid` et le shim `runtime_target` sont supprimés.
- les rôles Special ne sont jamais des families de Play Track sélectionnables.
- `Braids`, `Daisy` et `note_fx_arp` sont des noms internes légitimes; ne pas les renommer par cosmétique.

---

## 6. Comment s’orienter dans le projet

Avant toute passe :
- identifier la zone via `ARCHITECTURE_GLOBAL.md`
- lire le ou les `z*.md` pertinents
- ensuite seulement auditer le code réel local

Raccourcis utiles :
- boot / init / superloop -> Z0
- IRQ audio / mix / DMA / pipeline bloc -> Z1
- family / type / bind runtime / mix target -> Z2
- paramètres / apply / staging / modulation -> Z3
- transport / tempo / scheduler / live-rec seq -> Z4
- UI / halls / navigation / pages / clipboard -> Z5
- save/load / patterns / projects / restore -> Z6

Cas transverses fréquents :
- `Input Special vs External Play` -> Z2 + Z3 + Z5
- bug track-aware transversal -> commencer par Z2
- bug après load/restore -> Z6 puis zones impactées

---



## 7. Master

- Master et FX ne sont pas des families convertibles; leur identité vient exclusivement de `track_topology`.
- Ne pas recréer de type, backend, page ou chemin runtime buffer pour `Master`.
- Master est l'owner UI des effets globaux reverb, delay et compresseur; leurs autorités DSP/param restent globales.
- Les quatre slots MacroFX appartiennent à la Special FX, jamais à Master malgré l'adaptateur de configuration partagé.
- Le XFade produit restant appartient à `Sampler/Looper` via `PARAM_LOOPER_XFADE` et `audio_xfade`.

---

## 8. Où brancher les choses

- choix family/type d’une track : `CFG`
- paramètres filtre, VCA et ENV3 exposés au produit : `ENV`
- paramètres moteur sonore / oscillateurs : `TONE`
- jeu clavier : `KEYBOARD`
- MIDI FX : raccourci physique `ARP`, page `MIDI FX`
- routage Looper et routage UI-only MacroFX : contexte `ROUT` via réemploi de `ARP`

Les symboles internes actifs de l'ensemble produit utilisent désormais `ENV`; FLT, VCA, ENV3 et leurs retriggers appartiennent tous au domaine et au set p-lock `ENV`. La reconstruction des IDs de paramètres reste une dette planifiée. Le renommage propriétaire `MASTER_FX` vers `MACRO_FX` est réalisé sans renumérotation.

Ne pas créer un nouvel ensemble si un ensemble existant est déjà le bon point d’entrée.

---

## 9. UI : règles à ne pas casser

### MIDI FX p-lock
- Le set `SEQ_PLOCK_SET_MIDI_FX` utilise le mapping fixe 0..15 des seize IDs generiques.
- Son overlay runtime ne modifie jamais les bases `note_fx_state`; MODEL nettoie avant changement et la restauration ne rejoue aucun evenement source.
- Les bases MIDI FX appartiennent au Pattern/Project et au snapshot Track, jamais aux Patchs/Kits; aucun etat runtime MIDI FX n'est persiste.

### Résolution
L’UI se résout par contexte, pas page par page isolée.

Toujours penser :
- ensemble demandé
- track active
- family
- type
- capacité runtime effective

### Ensembles importants
- `CFG`
- `ENV`
- `TONE`
- `MOD`
- `MIX`
- `PLAY`
- `MIDI FX`

`ENV` est l'ensemble produit unique qui regroupe FLT, VCA et ENV3, sur Low-Cost comme Premium. Le backend VCA du mixer et `mod_env3` dans la modulation restent légitimes; ils ne créent pas d'ensemble autonome.

### Hall modes
- `TRACK` maintenu puis `HALL 0..7` = sélection track
- `SHIFT + HALL 9` = `KEYBOARD`
- `SHIFT + HALL 10` = ouverture de `MIDI FX`
- `SHIFT` doit être pressé avant le hall
- `MIDI FX` n'est pas un hall mode et ne doit jamais modifier le mode musical `KEYBOARD`/`SEQ`

### MUTE / PATTERN
- `SHIFT + -` => `PATTERN RECALL`
- `TRACK + -` => `PATTERN STORE`
- quick mute et prepare mute ne doivent pas être cassés

### MOD
- les destinations LFO doivent venir d’une liste explicite validée runtime
- autorisés uniquement : domaines produits `ENV` et `TONE` réellement valides pour la track
- interdit : domaine `PLAY`
- filtrage strictement track-aware, sans fallback cross-engine

---

## 10. LEDs et remaps

### Boutons param
- toutes vertes
- sauf le bouton de l’ensemble UI actif : blanc
- si l’ensemble actif n’a pas de bouton param associé : toutes vertes

### Mapping boutons param
- `BTN_PARAM_1` -> `ENV`
- `BTN_PARAM_2` -> `TONE`
- `BTN_PARAM_3` -> `MOD`
- `BTN_PARAM_4` -> `MIX`
- `BTN_PARAM_5` -> `PLAY` uniquement pour families moteur `Synth`, `Sampler` ou `Drum`
- `BTN_TRACK` -> bouton spécial `TRACK`

### Règle générale
Pour tout mapping logique/physique :
- boutons
- halls
- leds
- tracks logiques
- lanes DSP physiques
- sources buffer

toujours utiliser des tables explicites.

---

## 11. Clipboard / copy-paste / clear

### Track scope
- `TRACK + COPY` copie la track active sans steps séquenceur
- `TRACK + PASTE` colle la track copiée
- `TRACK + SHIFT + PASTE` clear la track active

### Scope ensemble / page
- `PARAM` maintenu + `COPY/PASTE` => scope ensemble
- bouton page active maintenu + `COPY/PASTE` => scope page

### Compatibilité
- en scope ensemble/page, compatibilité par intersection des `param_id` communs
- pas de matching strict des layouts

### Clear
- `SHIFT + PASTE` en scope ensemble/page clear les paramètres ciblés vers leur minimum

### Ressources exclusives
- `Sampler/RAM` : paste direct (non exclusif)
- `External` : conserve l'entrée physique exacte du snapshot; si elle est déjà réservée, le paste est refusé atomiquement
- aucun paste ou changement de type ne choisit automatiquement une autre entrée

---

## 12. Cardinalités track et chemins legacy

- Les paramètres legacy `PARAM_MIX_TRACKx_*` sont un chemin physique audio et restent limités à `MAX_TRACKS`.
- Les paramètres `PARAM_MIX_*` sans suffixe track suivent la track logique/runtime active.
- Dans `param_registry`, privilégier un dispatch indexé / table-driven pour les paramètres legacy trackés.
- Ne pas réintroduire de dépendance cachée entre index logique de track et lane physique mixer / DSP.
- Toute logique de routing buffer doit distinguer explicitement :
  - track logique source
  - lane physique effective
  - cible recorder/backend

---

## 13. Règles de modification

Quand tu modifies le projet :

1. commence par `ARCHITECTURE_GLOBAL.md` pour identifier la bonne zone
2. lis le ou les documents `z*.md` concernés avant de toucher au code
3. audite ensuite uniquement le code réel nécessaire dans cette zone et ses dépendances directes
4. ne repars pas de zéro si la cartographie existante couvre déjà la zone
5. réutilise avant de créer
6. garde une seule autorité par état structurant
7. garde les remaps explicites
8. respecte la logique track-aware
9. ne casse pas `SHIFT before HALL`
10. ne casse pas `KEYBOARD` / `MIDI FX`
11. ne fais pas de redesign gratuit
12. ne suppose pas qu’un bug est dans la UI si l’état runtime n’est pas tracé
13. ne déclare pas un bug résolu sans preuve code réelle
14. pas de build “pour voir” avant audit si la mission demandée est audit-only

---

## 14. Mise à jour de la doc

Si une passe modifie :
- families / types
- identité d’une track spéciale
- hall modes
- ensembles UI
- routing structurant
- logique runtime majeure
- architecture buffer / recorder
- séparation logique vs physique
- autorités d’un sous-système
- frontière d’une zone
- dépendance inter-zone structurante

alors la passe est considérée comme incomplète tant que la documentation requise n’a pas été mise à jour.

Mises à jour obligatoires selon le cas :
- toujours : le `z*.md` concerné
- `ARCHITECTURE_GLOBAL.md` si la frontière d’une zone, une dépendance inter-zone, ou une autorité globale change
- `AGENT.md` si une règle de travail, un invariant global, ou une convention transverse change
- `readme.md` si le comportement produit, les contrôles utilisateur, les families/types exposés, ou le périmètre fonctionnel visible changent

Règles :
- ne jamais terminer une passe structurelle sans mise à jour documentaire
- ne jamais dire qu’une passe est terminée si les fichiers de doc requis n’ont pas été modifiés
- en fin de passe, lister explicitement quels documents ont été mis à jour
- si aucun document n’a été mis à jour, dire explicitement pourquoi ce n’était pas requis

---

## 15. Priorité produit

Ordre de priorité :
1. stabilité audio hard real-time
2. CPU prédictible et borné
3. UX live performable
4. routing flexible mais contrôlé
5. code simple et maintenable

Principe directeur :
- si ça risque la stabilité audio : rejeter
- si ça complique le worst-case : repenser
- si ça aide le jeu live sans casser les invariants : prioriser

## Retrait buffer master

- Ne pas recreer de backend buffer master dedie.
- `audio_xfade` appartient au flux `Sampler/Looper` courant via `PARAM_LOOPER_XFADE`.

## Entree MIDI FX

- Le raccourci physique historique ARP ouvre directement `MIDI FX` sans hall mode ARP, sans double-tap et sans modifier le mode musical `SEQ/KEYBOARD` sous-jacent.
- Les Special qui reutilisent ce bouton conservent leur acces `ROUT`.
- L'ouverture ou la fermeture de MIDI FX ne doit jamais produire de transition sonore.
- `note_fx_state` est l'autorite canonique des bases MIDI FX: huit Play Tracks, quatre slots, quatre valeurs generiques; aucune Special n'alloue cet etat.
- Le domaine runtime `MIDI_FX` reste distinct de PLAY et aucun moteur sonore ne doit dependre de la page affichee.

## Autorite mute

- `track_mute` est l'autorite comportementale unique; la UI et les backends parametres lui deleguent l'application.
- Toute nouvelle family muteable doit declarer sa capacite et une politique explicite audio, MIDI ou contribution FX.
- Master ne recoit pas de mute ordinaire; aucun mute ne doit permettre un replay tardif ou une note bloquee.

## Modeles sequence

- Play: 64 steps, 32 p-locks maximum par step, pool 1024, donnees PLAY et MIDI FX autorisees.
- Special: 64 steps, 16 p-locks maximum par step, pool 512, automatisation non-PLAY et action bornee uniquement.
- Ne jamais allouer d'etat note ou MIDI FX par Special Track; ne pas utiliser le champ action pour introduire Brain ou MIDI FX sans chantier explicite.
- Les formats persistants et les clipboards identifient les tracks par `role + ordinal`; tout paste entre roles incompatibles est refuse avant mutation.
- Une sequence Special persiste uniquement longueur/page, action et automatisation non-PLAY. Aucun adaptateur homogene ne doit etre recree.

## Play Tracks independantes

- Les huit Play Tracks sont des autorites musicales independantes pour clavier, MIDI, modèle MIDI FX `note_fx_arp`, scheduler, live record, mute, parametres, p-locks, sequence, snapshots et persistence.
- Chaque Play Track conserve quatre voix PLAY; la polyphonie et le spread sont exclusivement ceux du moteur via `PARAM_CFG_POLY_VOICES` et `PARAM_CFG_POLY_SPREAD`.
- Patch stocke exactement une piste et peut etre applique independamment a plusieurs pistes selectionnees.


- Clavier, MIDI, MIDI FX, live record, scheduler PLAY et mute adressent exclusivement leur Play Track; aucune distribution ou pile de notes inter-tracks ne doit etre recreee.
- Conserver strictement `PARAM_CFG_POLY_VOICES`, `PARAM_CFG_POLY_SPREAD` et les spreads internes des moteurs.
