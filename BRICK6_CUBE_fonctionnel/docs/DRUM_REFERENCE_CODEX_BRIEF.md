# Drum reference brief for Codex

## But
Document de travail pour un agent Codex.

Objectif :
- comprendre la logique DSP probable des machines de référence Machinedrum `TRX` et `EFM`
- éviter les optimisations locales aveugles
- guider une future réécriture/rationalisation des moteurs drum actuels dans le repo
- rester compatible avec les invariants runtime/UI existants du projet

---

## 1. Contexte repo actuel à respecter

### Invariants repo
- projet **track-aware**
- autorité runtime centrale = `track_runtime`
- family `Drum` distincte de `Synth`
- pas de mapping parallèle caché UI/runtime
- pas de redesign gratuit
- pas de double autorité sur l’état structurant
- `PLAY` mono/poly piloté par l’autorité runtime centrale
- remaps logiques/physiques explicites uniquement

### Families/types actuels côté projet
- `Synth`: `DX7`, `MonoB`, `TB3`
- `Drum`: `TRX BD`, `TRX Claves`, `TRX HiHat`, `TRX Snare`, `FM Kick`, `FM Snare`, `FM Tom`, `FM Rimshot`, `FM Clap`, `FM Cowbell`, `FM Cymbal`

### Architecture audio actuelle du projet
- STM32H743
- IRQ audio sans RTOS
- pas d’allocation dynamique dans l’audio
- logique souhaitée déjà présente dans le repo : snapshot de paramètres par bloc + smoothing dans le DSP

### Problèmes actuels côté projet
- artefacts sur changements rapides de paramètres
- master volume se comporte comme un gain boostable au lieu d’un atténuateur unity-max
- CPU drum actuel jugé trop élevé (~10% pour un seul drum selon constat utilisateur)

---

## 2. Ce que disent les mémoires Chalmers/Machinedrum

## 2.1 Philosophie générale
Les deux mémoires convergent vers la même logique :
- priorité absolue au **temps réel dur**
- le but n’est pas un moteur général élégant, mais un son **musicalement convaincant sous contrainte CPU/mémoire stricte**
- les modèles sont volontairement **spécialisés**, avec peu de paramètres vraiment utiles
- on simplifie agressivement tant que le résultat perceptif reste bon

Conséquence importante :
> la Machinedrum ne semble pas pensée comme une collection de gros moteurs riches et indépendants, mais comme quelques noyaux spécialisés, calibrés pour des familles sonores

---

## 2.2 Contraintes matérielles TRX explicites

D’après le mémoire TRX :
- plateforme Machinedrum = 1 Coldfire + 2 DSP Motorola 56303
- un DSP génère les 16 sons drum en parallèle
- l’autre gère effets + mixage
- chaque unité génératrice doit produire **32 samples** par appel
- budget max : **3200 cycles** par appel
- mémoire par voix :
  - **128 bytes** persistants entre appels
  - **512 bytes** scratch partagé, non persistant

Implication directe :
- tout choix algorithmique doit être pensé à partir du **worst-case borne**
- un moteur “simple conceptuellement mais trop cher” est inacceptable

---

## 2.3 Contraintes FM explicites

D’après le mémoire FM :
- implémentation finale en assembleur DSP56303
- objectif explicite : éviter la haute complexité de calcul
- 8 contrôles max par modèle utilisateur
- pour les cymbales, le mémoire cite une contrainte de l’ordre de **~130 instructions machine par sample**

Implication :
- les contrôles sont choisis pour leur **rendement musical**, pas pour exposer toutes les libertés du moteur

---

## 3. Architecture de synthèse qui ressort des docs

## 3.1 Regroupement en 3 familles de noyaux TRX
Le mémoire TRX regroupe les sons TR-808 / TRX en 3 familles structurelles :

1. **1 oscillateur**
   - typiquement : bass drum, toms, congas
2. **2 oscillateurs**
   - typiquement : snare, rimshot, cowbell, claves
3. **groupe bruit / bruit filtré**
   - typiquement : hihat, cymbales, maracas

Conclusion forte pour le repo :
- la bonne cible n’est probablement pas `11 moteurs vraiment distincts`
- la bonne cible est plutôt `quelques noyaux pauvres + calibrations/mappings par modèle`

---

## 3.2 Famille 1 oscillateur
Caractéristiques observées dans le mémoire TRX :
- identité du son portée en grande partie par **l’attaque**
- attaque parfois échantillonnée très courte puis mélangée au corps synthétique
- corps tonal basé sur une **sinusoïde tabulée**
- pitch decay / ramp très important
- paramètres typiques : pitch, decay, ramp amount, ramp decay, hardness/start, noise, harmonic content, clip

Lecture pratique :
- le corps peut rester très simple
- il faut investir le coût DSP surtout dans le **transitoire** et le **pitch contour**

---

## 3.3 Famille 2 oscillateurs
Caractéristiques observées dans le mémoire TRX :
- l’identité sonore vient surtout de **l’intervalle entre les 2 tons**
- cet intervalle doit rester **constant**, même si la fréquence globale évolue
- la snare ajoute du **bruit filtré** pour représenter le timbre métallique
- paramètres typiques : pitch, decay, snappy/noise, tone balance, tune interval, bump, clip

Lecture pratique :
- les rapports internes portent le son
- il faut éviter de rendre indépendants trop de degrés de liberté qui détruisent l’identité

---

## 3.4 Famille bruit / cymbales
Caractéristiques observées dans le mémoire TRX :
- la priorité est donnée au **filtrage du bruit**
- pas d’exigence de phase linéaire puisque l’entrée est du bruit
- donc choix assumé de **filtres IIR** pour coût moindre
- low-pass : 24 dB/oct, fréquence et résonance contrôlées
- high-pass : deux étages 12 dB/oct, dont un fixe et un balayable

Lecture pratique :
- pour cette famille, la psychoacoustique prime sur la pureté formelle
- ne pas surpayer des FIR/solutions “propres” si l’IIR fait le job perceptif

---

## 4. Ce que disent les docs FM sur les noyaux EFM

## 4.1 Position générale
Le mémoire FM ne décrit pas une énorme grammaire FM universelle.
Il construit des algorithmes **étroits**, adaptés à chaque famille, puis les compacte pour l’assembleur DSP.

Logique implicite :
- exploration hors cible
- puis réduction vers une forme exécutable très bornée

---

## 4.2 Feedback FM comme outil de coût réduit
Le mémoire insiste sur l’intérêt du **feedback** :
- faible feedback : sinus enrichie
- plus de feedback : forme plus agressive / pseudo-saw
- très fort feedback : comportement bruité

Lecture pratique :
- le feedback remplace utilement des générateurs plus coûteux
- pour des drums, il sert surtout à créer **attaque**, **brillance**, **rugosité**, **pseudo-bruit**

---

## 4.3 Kick / tom / snare FM
### Kick FM
- un noyau FM simple suffit si l’attaque est bien traitée
- le “click” initial compte énormément
- le mémoire décrit un usage où le modulateur rétro-couplé peut aller jusqu’au bruit au début du son

### Snare FM
- combine corps tonal + bruit via noise generator + high-pass
- certaines enveloppes théoriquement possibles sont retirées si elles n’apportent pas assez musicalement

### Tom FM
- très proche du kick
- même noyau, autre calibration
- high-pass final
- variation possible de phase initiale pour le click

Conclusion pratique :
- plusieurs modèles peuvent partager un **même noyau** avec simple recalibrage

---

## 4.4 Clap / rimshot / cowbell
### Clap
- obtenu via modulateur rétro-couplé fortement pour produire un bruit résonant
- enveloppe multi-impulsions pour imiter plusieurs mains légèrement désynchronisées
- high-pass final

### Rimshot
- son composite
- mélange d’une logique proche clap + composante plus tonale/snare-like

### Cowbell
- inspirée du TR-808
- au lieu de deux carrés dissonants bruts, utilisation de **deux paires FM** avec feedback sur la carrier
- ratio interne important, fixé à environ **1.48**
- enveloppe de volume = rapide + lente, additionnées

Conclusion pratique :
- les sons “complexes” restent construits avec peu de briques
- l’identité vient de rapports fixes, enveloppes bien choisies et feedback

---

## 4.5 Cymbales / hihats FM
Point majeur du mémoire FM :
- TR-808 utilise 6 carrés dissonants
- refaire cela en FM stricte coûterait **12 opérateurs**
- jugé trop cher sous contrainte temps réel
- approximation retenue : **4 paires FM seulement**
- et utilisation de **phase modulation** plutôt que la variante plus coûteuse, pour gagner quelques cycles
- high-pass utilisé **2 fois en série** car perceptivement meilleur
- enveloppe pouvant saturer/plateauer au début pour adoucir/épaissir l’attaque

Conclusion forte :
- la machine de référence accepte une **approximation structurelle volontaire** si elle garde la signature sonore
- c’est probablement un meilleur guide que de coller littéralement à une topologie d’origine coûteuse

---

## 5. Oscillateurs, tables, band-limiting

## 5.1 Ce qui est explicite
### Mémoire FM
Le mémoire FM dit explicitement :
- oscillateurs implémentés via **table sinus de 32768 valeurs 24-bit**
- pas d’interpolation retenue
- l’auteur explique qu’une table encore plus grande serait meilleure pour le SNR
- interpolation possible en théorie, mais rejetée car trop coûteuse
- l’indexation et le wrapping sont rendus très efficaces par les registres index/modulo du DSP56303

### Mémoire TRX
Le mémoire TRX dit explicitement :
- les tons sont générés à partir d’une **table sinus 24-bit de 4096 points**
- cette table est une **ressource partagée** de la Machinedrum
- sa taille est imposée par le fait qu’elle est utilisée par plusieurs parties de la machine et placée dans une mémoire partagée limitée

## 5.2 Ce qui n’est pas explicite
- je n’ai pas vu de description d’un **band-limiting d’oscillateur** dans le mémoire FM
- je n’ai pas vu de mention d’interpolation dans la version retenue
- je n’ai pas de preuve dans ces docs que les machines EFM finales shipping aient finalement migré de `32768` vers la table globale `4096`

## 5.3 Conclusion prudente à donner à Codex
- **TRX documenté** : table sinus globale 4096 points, partagée
- **EFM documenté dans le mémoire** : table sinus dédiée 32768 points, sans interpolation, pas de band-limiting explicite décrit
- **incertitude réelle** : le mémoire FM ne prouve pas à lui seul si la version produit finale a conservé exactement cette taille de table

Consigne :
> ne pas affirmer comme un fait que toutes les machines Machinedrum FM finales utilisaient forcément 4096 points ; l’état documenté ici est `32768 dans le mémoire FM`, `4096 partagé dans le mémoire TRX`, point

---

## 6. Bruit, filtrage, retrig, paramètres

## 6.1 Génération de bruit
### FM
- bruit via pseudo-aléatoire type Fibonacci / addition modulo
- choix simple et très léger

### TRX
- bruit généré localement par instance
- intérêt probable : éviter la corrélation inter-voix

Consigne pratique :
- préférer des générateurs de bruit très légers, déterministes, état minimal

---

## 6.2 Paramètres runtime
Point très important dans le mémoire TRX :
- les paramètres utilisateurs ne doivent pas être lus seulement au trig
- ils doivent être **pris en compte continuellement** pendant les différentes phases du son

Cela converge avec la direction déjà présente dans le repo :
- shadow params côté UI
- snapshot côté audio par bloc
- smoothing DSP localisé

Consigne pratique :
- ne pas figer tout le son au `note on`
- appliquer les changements de paramètres sur les segments/enveloppes/étages concernés pendant la vie du son

---

## 6.3 Retrig
Le mémoire TRX décrit un problème classique :
- si on redémarre une sinusoïde avant extinction complète, un click apparaît
- solution retenue : mini phase d’extinction avant retrig, avec conservation du niveau précédent pour contribuer au coup suivant

Consigne pratique :
- éviter le reset brutal d’état oscillateur/enveloppe sur retrig
- préférer micro-fade / short decay / residual carry selon famille de son

---

## 6.4 Volume, distorsion, accent
Le mémoire TRX dit explicitement qu’il est souhaitable de **décorréler** distorsion et volume.

Implication pour le repo :
- ne pas laisser `master volume` servir de boost colorant implicite
- `volume max = unity`
- accent / drive / clip doivent être des dimensions séparées

---

## 7. Principes de design à appliquer dans ce repo

## 7.1 Direction générale recommandée
Avant toute micro-optimisation :
1. réduire le nombre de topologies réelles
2. identifier les invariants qui portent le timbre
3. figer les rapports internes quand ils portent l’identité
4. réallouer le coût CPU vers les transitoires et filtres perceptivement décisifs

---

## 7.2 Cible architecture probable
Cible plus proche de l’esprit Machinedrum :
- `drum_core_single_osc`
- `drum_core_dual_osc`
- `drum_core_noise`
- éventuellement `drum_core_fm_cluster` si besoin strict

Puis mapper les 11 types drum dessus avec :
- ratios fixes
- courbes d’enveloppes spécifiques
- filtres dédiés
- options de feedback / clip / transient propres

Idée centrale :
- `modèle utilisateur != gros moteur unique`
- `modèle utilisateur = preset structurel d’un noyau`

---

## 7.3 Simplifications compatibles avec la logique d’origine
À privilégier :
- LUT partagées
- ratios fixes ou semi-fixes
- enveloppes très simples mais bien choisies
- IIR si perceptuellement suffisant
- phase modulation si moins chère que l’autre variante
- feedback pour remplacer des structures plus lourdes
- bruit pseudo-aléatoire minimal
- retrig avec micro-fade

À éviter en première passe :
- sur-exposer des paramètres peu rentables musicalement
- multiplier les noyaux DSP distincts quand un recalibrage suffit
- introduire une surcouche routage/résolution parallèle hors `track_runtime`
- lancer du SIMD/fixed-point/micro-opt tant que la topologie n’est pas rationalisée

---

## 7.4 Lecture spécifique du problème CPU actuel
Si un seul drum coûte ~10% CPU, l’hypothèse la plus probable est :
- topologie actuelle trop proche de moteurs autonomes complets importés
- pas assez proche de la logique Machinedrum originale qui cherche des **familles de noyaux réduites** et des **approximations structurelles contrôlées**

Conclusion de travail :
> la prochaine grosse baisse CPU doit probablement venir d’une **réduction algorithmique**, pas d’une simple chasse aux cycles isolés

---

## 8. Priorités de travail proposées à Codex

## Priorité 1 — audit architecturel
- regrouper les 11 moteurs actuels par topologie réelle
- lister ce qui est strictement spécifique vs ce qui peut devenir paramétrique
- identifier le coût CPU par famille

## Priorité 2 — smoothing et volume
- ajouter smoothing localisé sur gains/volumes/sends/filtres/params continus sensibles
- transformer le master volume en **atténuateur seulement**
- séparer clairement `volume`, `accent`, `drive`, `clip`

## Priorité 3 — rationalisation drum
- factoriser les moteurs actuels importés autour de noyaux communs
- conserver la signature sonore perçue avant de viser la fidélité structurelle totale
- ne pas hésiter à approximer si le gain CPU est fort et la perte perceptive faible

## Priorité 4 — seulement ensuite micro-opt
- LUT/mémoire/cache/branching
- évitement des clears inutiles
- coût des conversions et filtrages
- éventuelle réduction des états persistants

---

## 9. Règles pour Codex pendant cette future passe
- raisonner **track-aware** en permanence
- ne pas casser l’autorité de `track_runtime`
- ne pas recréer de routeur drum parallèle
- ne pas mélanger `Drum` et `Synth`
- ne pas recycler des types `Synth` dans `Drum`
- garder mono/poly piloté au runtime central
- proposer d’abord une simplification topologique, pas un tweak de 3 cycles
- distinguer systématiquement :
  - `gain`
  - `drive/clip`
  - `accent`
  - `pitch contour`
  - `noise content`
- toute hypothèse non explicitement prouvée par les docs doit être marquée **INCERTAIN**

---

## 10. Points explicitement incertains
- si les EFM shipping finals utilisaient toujours exactement la table sinus `32768` du mémoire FM
- si certains moteurs finaux ont ensuite convergé vers la LUT globale `4096`
- la part exacte de l’“empreinte sonore Machinedrum” due à la taille de LUT vs autres choix (feedback, filtres, enveloppes, clipping, ratios)

Ne pas sur-vendre ces points.

---

## 11. Résumé ultra-court pour action
- penser **familles de noyaux**, pas `11 moteurs sacrés`
- penser **approximation contrôlée**, pas fidélité littérale coûteuse
- penser **transitoire + enveloppe + rapports fixes + filtres IIR + feedback**
- penser **snapshot + smoothing + retrig propre**
- penser **volume unity max**, drive séparé
- penser **réduction algorithmique avant micro-opt**

---

## 12. Sources minimales à garder en tête
- `AGENT.md` du repo : invariants track-aware et règles d’intégration
- `readme.md` du repo : philosophie hard real-time, snapshot/smoothing, CPU borné
- mémoire TRX Chalmers 2004 : familles 1 osc / 2 osc / bruit, contraintes CPU/mémoire, LUT globale 4096, retrig, update continue des paramètres
- mémoire FM Chalmers 2000 : FM spécialisée pour drums, feedback, table sinus 32768 sans interpolation retenue, approximation cymbales 4 paires FM au lieu de 6/12 opérateurs

