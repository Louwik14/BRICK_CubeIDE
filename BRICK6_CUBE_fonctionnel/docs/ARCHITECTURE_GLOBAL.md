## 1. Objet du document

Ce document est la référence de travail haut niveau du projet pour Codex.

Il décrit :
- le but du firmware
- les grandes briques de la groovebox
- l’architecture audio
- l’architecture UI
- l’architecture track/family/type
- les hall modes
- les moteurs de synthèse
- les règles de modification du projet

Règle importante :
- toute passe qui modifie l’architecture, les familles/types de tracks, les hall modes, les chemins runtime, les ensembles UI, ou un comportement structurant du projet doit aussi mettre à jour ce fichier dans la même passe.

---

## 2. Vue d’ensemble du projet

Projet firmware embarqué audio/UI sur STM32H743.

Caractéristiques générales :
- audio temps réel par blocs
- écran OLED 128x32
- code mixte C / C++
- architecture track-aware déjà avancée
- UI basée sur des ensembles/pages template
- groovebox pensée pour faire cohabiter audio, synthèse, jeu clavier et plus tard MIDI / séquenceur

Le projet ne doit pas être redessiné gratuitement.
La logique générale existante doit être prolongée proprement, pas remplacée.

---

## 3. Matériel et I/O principales

### 3.1 MCU
- STM32H743

### 3.2 Affichage
- OLED 128x32

### 3.3 Entrées de jeu / contrôle
- halls / hall buttons
- boutons param
- boutons `+` / `-`
- `SHIFT`
- potentiomètres via mux
- encoder
### 3.4 Audio
- I/O audio en TDM / int24
- conversion en float pour le traitement DSP
- repack en sortie TDM

---

## 4. Pipeline audio global

Le pipeline global suit cette logique :

1. interruption DMA audio
2. traitement d’un demi-buffer
3. unpack des entrées audio TDM vers buffers float
4. callback DSP applicatif
5. moteur mixer / routing / bus / inserts / sends
6. pack des sorties float vers TDM int24

Le DSP applicatif alimente ensuite le mixeur principal.

Le pipeline est déjà modulaire et doit être conservé.
Les optimisations CPU doivent privilégier :
- bypass évidents
- court-circuits sûrs
- suppression de traitements neutres
sans casser l’architecture.

---

## 5. Architecture track / family / type

Le projet est track-aware.

### 5.1 Track active
Il existe une notion de track active côté UI.

Sélection :
- maintenir `SHIFT`
- puis appuyer sur `HALL 0..7`

Règle stricte :
- `SHIFT` doit être pressé avant le hall
- l’ordre inverse n’est pas voulu
- les événements UI liés à cette logique doivent être consommés pour éviter les effets parasites sur le chemin note/runtime

### 5.2 Families de track actuelles

Le paramètre `CFG > Track` expose actuellement :

- `Off`
- `Input1`
- `Input2`
- `Input3`
- `Input4`
- `Synth`

À terme :
- une vraie family `MIDI` sera ajoutée quand la partie séquenceur / MIDI track sera intégrée

### 5.3 Sens de chaque family

#### Off
- track inactive
- ne doit pas traverser inutilement le runtime audio
- vraie désactivation runtime, pas simple mute UI

#### Input1..4
- représentent des ressources physiques explicites
- ces ressources sont exclusives entre tracks
- une même ressource `InputX` ne doit pas être affectée à plusieurs tracks en même temps

#### Synth
- family des moteurs de synthèse internes
- partageable selon la logique runtime existante

### 5.4 Types actuels

#### Pour InputX
- `Audio`
- `Hybrid`

#### Pour Synth
- `DX7`
- `MonoB`

### 5.5 Sens de Hybrid
`Hybrid` doit être compris comme :
- une track input avec audio
- et un chemin note / MIDI à compléter / faire évoluer
- cette architecture existe pour préparer l’intégration plus riche des chemins note-capables, notamment avec l’arrivée ultérieure des MIDI tracks

---

## 6. Header runtime des tracks

Le header UI n’est plus hardcodé.
Il doit refléter la vraie config runtime de la track active.

Exemples de labels compacts :
- `Off`
- `In1`
- `In2 Hyb`
- `DX7`
- `MonoB`

Toute évolution des families/types doit conserver cette logique de label runtime compact.

---

## 7. Architecture UI générale

L’UI est organisée autour de templates / ensembles / pages.

Le principe n’est pas de changer les noms d’ensembles selon le moteur, mais de garder des ensembles stables dont le contenu dépend :
- de la family de track
- du type de track
- du contexte runtime

### 7.1 Ensembles stables existants
Au minimum, l’architecture s’appuie aujourd’hui sur :
- `CFG`
- `FILTER`
- `TONE`
- `KEYBOARD`
- `ARP`

D’autres ensembles viendront plus tard.

### 7.2 Résolution template
La résolution d’une famille template dépend de :
- ensemble demandé
- track active
- family de track
- type de track

Cette logique est déjà en place et doit être conservée.

---

## 8. Ensemble CFG

`CFG` est l’ensemble de configuration de base par track.

Existant minimal :
- page 1 :
  - `Track`
  - `Type`
  - `-`
  - `-`

Rôle :
- choisir la family principale de la track
- puis le type associé

Le contenu du `Type` dépend du `Track`.

Exemples :
- `Track = Input1` -> `Type = Audio` ou `Hybrid`
- `Track = Synth` -> `Type = DX7` ou `MonoB`

---

## 9. Ensemble FILTER

### 9.1 Tracks audio
Pour les tracks `InputX` en type `Audio`, `FILTER` expose :
- `Off`
- `EQ3`
- `LP BI`
- `HP BI`
- `BP BI`

Organisation actuelle :
- `EQ3`
  - page 1 : `Type`, `Low`, `Mid`, `High`
- `LP BI / HP BI / BP BI`
  - page 1 : `Type`, `Cutoff`, `Res`, `-`
  - autres pages selon l’évolution du runtime

L’ancien SVF audio a été abandonné.
Le biquad CMSIS est la base retenue pour les tracks audio.

### 9.2 MonoB
`MonoB` a un `FILTER` propre à son moteur, distinct des tracks audio.
Le filtre ladder moog-like est propre à `MonoB`.

Le runtime filtre de `MonoB` expose au minimum :
- on/off
- cutoff
- resonance
- filter EG amount
- enveloppe filtre

Le filtre `MonoB` et les filtres audio ne doivent pas être mélangés architecturalement.

---

## 10. Ensemble TONE

`TONE` est l’ensemble générique prévu pour les moteurs sonores / synthés.

Ce nom est volontairement générique pour permettre des variantes selon les moteurs.

### 10.1 Cas MonoB
Pour `MonoB`, `TONE` sert actuellement à piloter le bank d’oscillateurs.

Organisation actuelle typique :
- page 1 : choix des waves / off pour osc1, osc2, osc3, sub
- page 2 : ranges / octaves
- page 3 : detunes
- page 4 : mix

`MonoB` utilise :
- 3 oscillateurs principaux
- 1 sub
- moteur monophonique mono

---

## 11. Widgets UI

Une banque de widgets modulaire existe.

Point de conception important :
- le choix du widget n’est pas stocké dans `param_registry`
- il reste côté page / renderer / logique UI

Raison :
- un même paramètre peut être affiché différemment selon le contexte
- on ne veut pas figer l’UI dans les métadonnées du paramètre

Cette règle doit être conservée.

---

## 12. Hall modes

Les hall buttons servent à plusieurs usages selon le hall mode.

Le petit label de hall mode affiché dans le header peut être :
- `SEQ`
- `KBD`
- `ARP`

Et il peut intégrer l’offset de transposition du keyboard :
- `KBD`
- `KBD+1`
- `KBD-1`
- `ARP`
- `ARP+1`
- `ARP-1`

Important :
- ce `+X / -X` correspond à la transposition du keyboard
- pas à la transposition propre de l’ARP

### 12.1 Règle générale
Les halls ont des comportements différents selon le hall mode courant.
Le hall mode doit être géré proprement, via une logique robuste de dispatch, sans micro-patchs fragiles.

### 12.2 Hall mode SEQ
- mode séquenceur / step-oriented
- comportement existant à conserver tant qu’il n’est pas explicitement refondu

### 12.3 Hall mode KEYBOARD
Activation :
- `SHIFT` maintenu
- puis `HALL 9` côté utilisateur (index 8)

Comportement :
- simple tap : active `KEYBOARD`
- double tap : active `KEYBOARD` + ouvre l’UI `KEYBOARD`

### 12.4 Hall mode ARP
Activation :
- `SHIFT` maintenu
- puis `HALL 10` côté utilisateur (index 9)

Comportement :
- simple tap : active `ARP`
- double tap : active `ARP` + ouvre l’UI `ARP`

`ARP` est un sous-mode fonctionnel du keyboard :
- il utilise le keyboard comme source de notes
- il ne remplace pas le keyboard
- il ne doit jamais casser l’activation explicite de `KEYBOARD`

La logique de dispatch entre `KEYBOARD` et `ARP` doit rester table-driven / factorisée / robuste.

---

## 13. KEYBOARD

Le mode `KEYBOARD` est un hall mode de jeu.

Il sert d’entrée de notes pour les tracks note-capables.

### 13.1 Paramètres du keyboard
L’UI keyboard contient au minimum :
- gamme / scale
- root
- omnichord on/off
- note order
- chord override
- octave shift / transposition clavier

### 13.2 Omnichord
Deux comportements principaux existent :

#### Omnichord Off
- les halls servent surtout de clavier scalaire
- l’organisation des groupes de notes suit la logique keyboard existante

#### Omnichord On
- une zone sert aux notes
- une zone sert aux accords / combinaisons d’accords
- logique inspirée de l’omnichord

### 13.3 Tracks concernées
Le keyboard doit fonctionner pour les tracks qui acceptent des notes, selon l’état actuel du projet :
- `Synth`
- `Hybrid`
- et plus tard `MIDI` quand la MIDI track sera créée proprement

---

## 14. ARP

`ARP` est un hall mode et un sous-système branché sur `KEYBOARD`.

Il ne doit pas dupliquer inutilement le keyboard.
Il doit réutiliser :
- la source de notes keyboard
- la logique LED keyboard
- le runtime de jeu keyboard comme base

### 14.1 UI ARP
L’UI ARP contient 4 pages :

#### Core
- `Hold`
- `Rate`
- `Oct`
- `Pattern`

#### Groove
- `Gate`
- `Swing`
- `Accent`
- `VelAcc`

#### Strum
- `Strum`
- `Offset`
- `-`
- `Trans`

#### Pitch
- `Spread`
- `Dir`
- `Sync`
- `-`

### 14.2 Runtime ARP
Le runtime ARP gère :
- hold réel
- patterns `Up`, `Down`, `UpDn`, `Rnd`, `Chord`
- limite de polyphonie chord à 4 notes
- transposition ARP indépendante
- octave range ARP indépendante
- accent / direction / strum / spread
- sync modes

### 14.3 Sync
Les 3 modes existent :
- `Int`
- `Clock`
- `Free`

État actuel :
- `Int` réellement fonctionnel
- `Clock` préparé proprement pour futur branchage à une vraie clock externe
- `Free` existant comme branche runtime dédiée

### 14.4 LEDs
Le mode `ARP` réutilise exactement la scène LED du `KEYBOARD`.
Il n’a pas de logique LED hall spécifique propre à ce stade.

---

## 15. Synth engines

### 15.1 DX7
`DX7` est un moteur stable côté type `Synth`.
Il reste présent comme moteur synth valide.

### 15.2 MonoB
`MonoB` est un synthé monophonique mono, inspiré des synthés analogiques type Moog.

Caractéristiques principales :
- 3 oscillateurs + 1 sub
- moteur mono
- filtre ladder moog-like dédié
- enveloppe d’amplitude
- `TONE` dédié
- `FILTER` dédié

`MonoB` a été optimisé au fil du projet :
- oscillateurs simplifiés
- bypass runtime quand possible
- filtrage dédié plus léger que les premières versions

---

## 16. LED architecture

L’architecture LED existe déjà et doit rester modulaire.

### 16.1 Boutons param
Règle actuelle :
- toutes les LEDs des boutons param sont vertes
- sauf celle du bouton correspondant à l’ensemble UI actif
- cette LED-là est blanche
- si l’ensemble actif n’est lié à aucun bouton param physique, toutes les LEDs param restent vertes

### 16.2 Halls
Les halls utilisent des scènes spécifiques selon le hall mode.

#### KEYBOARD / ARP
Le mode `ARP` réutilise la scène hall de `KEYBOARD`.

#### Omnichord Off
- première moitié : bleu clair
- deuxième moitié : bleu foncé

#### Omnichord On
- zone notes à droite : bleu
- zone accords : couleurs de groupes cohérentes

### 16.3 Remap
Les correspondances entre :
- boutons logiques
- halls logiques
- LEDs physiques

doivent être explicitement centralisées via des tables de remap.
Il ne faut pas supposer que l’ordre logique = ordre physique.

---

## 17. Potentiomètres / master volume

Les potentiomètres sont lus via mux.

Le master volume global doit être piloté par un pot dédié.
Le comportement attendu est :
- vrai silence à une butée
- volume max propre à l’autre
- aucune double autorité concurrente sur le gain global
- application au vrai point master global de sortie



## 18. Séquenceur / MIDI tracks

Le séquenceur et les vraies MIDI tracks sont prévus plus tard.

À ce stade :
- le projet est préparé pour cette extension
- `Hybrid` existe déjà comme family/type de transition utile
- une vraie family `MIDI` devra être ajoutée proprement quand la partie séquenceur / MIDI track sera attaquée

---

## 19. Principes de modification pour Codex

Quand Codex modifie ce projet, il doit suivre ces règles :

### 19.1 Ne pas redesign gratuitement
Toujours prolonger l’architecture existante avant de proposer une refonte.

### 19.2 Préserver la logique track-aware
Toute nouvelle feature doit respecter :
- track active
- family
- type
- runtime de track
- résolution template

### 19.3 Préserver la séparation des niveaux
Ne pas mélanger inutilement :
- UI
- runtime audio
- LEDs
- paramètres
- drivers

### 19.4 Favoriser les changements locaux
Quand un bug ou une feature peut être traité localement et proprement, éviter les chantiers transverses inutiles.

### 19.5 Éviter les doubles autorités
Pour tout état runtime structurant (gain global, hall mode, runtime synth, etc.), éviter d’avoir plusieurs chemins concurrents qui écrivent le même état sans politique claire.

### 19.6 Préserver les comportements historiques voulus
Exemples :
- `SHIFT` avant hall
- `HALL 9` = `KEYBOARD`
- `HALL 10` = `ARP`
- halls `0..7` sous `SHIFT` pour track select
- `ARP` doit rester un sous-mode du keyboard, pas casser `KEYBOARD`

---

