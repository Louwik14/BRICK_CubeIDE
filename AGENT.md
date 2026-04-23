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
- L’autorité de binding reste `track_runtime`.
- Le mapping `UI track -> mix target runtime` est explicite et unique.
- Le mapping `UI track -> cible DSP physique` ne doit jamais être supposé implicite.
- Les getters/checks runtime ne doivent pas déclencher de refresh implicite, sauf exception explicitement auditée et documentée.
- L’invalidation reste explicite (`track_runtime_invalidate_all`) et le refresh est demandé explicitement par les appelants autorisés.
- Ne jamais créer une seconde autorité parallèle pour un état déjà porté par le runtime, le mixer ou un backend audio existant.
- Le futur dual-core se prépare par seams explicites, pas par une infrastructure centrale artificielle.

---

## 5. Modèle tracks actuel

### Cardinalités
- 14 tracks logiques côté UI / runtime / séquenceur / logique mixer
- `MAX_TRACKS` côté DSP ingress physique reste distinct
- les concepts “track logique” et “lane physique audio” sont distincts

### Layout logique actuel
- 8 tracks musicales flexibles
- 4 tracks orientées input
- 2 slots réservés master/global

### Families actuelles
- `Off`
- `Input1`
- `Input2`
- `Input3`
- `Input4`
- `Synth`
- `Drum`
- `Master`

### Types actuels

#### Pour `InputX`
- `Audio`
- `Hybrid`

#### Pour `Synth`
- `Sampler`

#### Pour `Drum`
- `TRX BD`
- `TRX Claves`
- `TRX HiHat`
- `TRX Snare`
- `FM Kick`
- `FM Snare`
- `FM Tom`
- `FM Rimshot`
- `FM Clap`
- `FM Cowbell`
- `FM Cymbal`

#### Pour `Master`
- `Buffer`

### Notes
- `Off` = vraie désactivation runtime.
- `Input1..4` = ressources physiques exclusives produit.
- modèle produit : 4 entrées stéréo physiques visées (`Input1..4`)
- proto actuelle : 3 entrées stéréo effectivement câblées côté front-end DSP
- `Input4` reste une ressource valide côté modèle produit
- `Hybrid` = track audio + chemin note/midi amené à évoluer
- une vraie family `MIDI` viendra plus tard
- `runtime_target` est un shim legacy de compat, hors chemin opérationnel in-tree
- `Master` est une family spéciale non standard
- `Master/Buffer` est une vraie identité runtime track-aware, pas un mode global générique

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
- `Input Audio vs Hybrid` -> Z2 + Z3 + Z5
- `Master/Buffer` -> Z1 + Z2 + Z3 + Z4 + Z5
- bug track-aware transversal -> commencer par Z2
- bug après load/restore -> Z6 puis zones impactées

---

## 7. Master/Buffer : invariants à respecter

`Master/Buffer` est une intégration spéciale encore en stabilisation.

### Identité
- family `Master`
- type `Buffer`
- instance unique dans le projet
- traitée comme une vraie identité de track dans le runtime et l’UI

### Backend
- réutilise le backend buffer / recorder existant
- pas de second recorder concurrent
- pas de double backend buffer parallèle
- tout patch doit vérifier l’autorité réelle entre wrapper `Master/Buffer` et backend recorder existant

### Contrôles attendus
- `TRACK + REC` :
  - cible l’unique instance `Master/Buffer`
  - ne doit pas dépendre du focus courant d’une autre track
- `TRACK + SHIFT + REC` :
  - clear du contenu buffer
- `ARP` sur `Master/Buffer` :
  - devient `ROUT`
  - sert à choisir quelles tracks logiques alimentent le buffer
- `KBD` :
  - reste conservateur / classique tant qu’aucune logique dédiée supplémentaire n’est stabilisée

### TONE sur `Master/Buffer`
#### Page 1
- `Rec Len`
- `Q Rec`
- `Q Play`
- `Rate`

#### Page 2
- `Fade In`
- `Fade Out`
- `XFade`

### Discipline de debug sur `Master/Buffer`
Pour tout bug buffer, distinguer explicitement :
1. capacité max allouée
2. longueur cible d’enregistrement
3. longueur réellement enregistrée valide
4. longueur de loop appliquée au recorder
5. longueur réellement lue
6. conditions de start/stop rec
7. conditions de start/stop play
8. éventuel désalignement écriture / lecture / loop

Ne jamais déclarer “corrigé” sans traçabilité dans le code réel.

---

## 8. Où brancher les choses

- choix family/type d’une track : `CFG`
- paramètres filtre / couleur audio : `COLORS`
- paramètres moteur sonore / oscillateurs : `TONE`
- jeu clavier : `KEYBOARD`
- arpégiateur : `ARP`
- paramètres dédiés `Master/Buffer` : domaine contextuel `TONE` de la track `Master/Buffer`
- routage des sources buffer : contexte `ROUT` via réemploi de `ARP`

Ne pas créer un nouvel ensemble si un ensemble existant est déjà le bon point d’entrée.

---

## 9. UI : règles à ne pas casser

### Résolution
L’UI se résout par contexte, pas page par page isolée.

Toujours penser :
- ensemble demandé
- track active
- family
- type
- capacité runtime effective
- identité spéciale éventuelle (`Master/Buffer`)

### Ensembles importants
- `CFG`
- `COLORS`
- `TONE`
- `MOD`
- `MIX`
- `VCA`
- `KEYBOARD`
- `ARP`

### Hall modes
- `TRACK` maintenu puis `HALL 0..7` = sélection track
- `SHIFT + HALL 9` = `KEYBOARD`
- `SHIFT + HALL 10` = `ARP`
- `SHIFT` doit être pressé avant le hall
- `ARP` est un sous-mode du `KEYBOARD`
- `ARP` ne doit jamais casser l’activation explicite de `KEYBOARD`
- sur `Master/Buffer`, le comportement visible peut être `ROUT` mais l’invariant structurel `ARP` / `KEYBOARD` reste intact

### MUTE / PATTERN
- `SHIFT + -` => `PATTERN RECALL`
- `TRACK + -` => `PATTERN STORE`
- quick mute et prepare mute ne doivent pas être cassés

### MOD
- les destinations LFO doivent venir d’une liste explicite validée runtime
- autorisés uniquement : domaines `COLORS` et `TONE` réellement valides pour la track
- interdit : domaine `PLAY`
- filtrage strictement track-aware, sans fallback cross-engine

---

## 10. LEDs et remaps

### Boutons param
- toutes vertes
- sauf le bouton de l’ensemble UI actif : blanc
- si l’ensemble actif n’a pas de bouton param associé : toutes vertes

### Mapping boutons param
- `BTN_PARAM_1` -> `COLORS`
- `BTN_PARAM_2` -> `TONE`
- `BTN_PARAM_3` -> `MOD`
- `BTN_PARAM_4` -> `MIX`
- `BTN_PARAM_5` -> `PLAY` uniquement pour families moteur `Synth` ou `Drum`
- `BTN_PARAM_6` -> `VCA` uniquement pour families moteur `Synth` ou `Drum` ou `Hybrid`
- `BTN_PARAM_8` -> bouton spécial `TRACK`

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
- `Synth/Sampler` : paste direct (non exclusif)
- `Input1..4` : priorité à un input libre, sinon move-on-paste
- après move réussi, le clipboard reste chaînable

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
10. ne casse pas `KEYBOARD` / `ARP`
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
