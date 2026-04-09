# Drum engines — stratégie d’optimisation moteur par moteur

## 1) Validation de la stratégie

La stratégie **moteur par moteur** est la plus adaptée au contexte observé :

- une tentative de convergence FM forcée a déjà montré une **régression CPU**,
- les moteurs Drum ont des structures et coûts intrinsèques différents,
- l’objectif est une logique type machine de référence : noyaux simples, coût borné, peu de degrés de liberté,
- les gains viennent surtout de simplifications locales (par moteur), pas d’une abstraction globale.

Conclusion : on optimise chaque moteur indépendamment, et on ne partage que de **petites briques** prouvées rentables.

## 2) Ordre de traitement recommandé (priorité décroissante)

1. **TRXHiHat**
2. **TRXSnareDrum**
3. **TRXBassDrum**
4. **FmCymbalModel**
5. **FmSnareModel**
6. **FmKickModel**
7. **FmTomModel**
8. **FmClapModel**
9. **TRXClaves**
10. **FmCowbellModel**
11. **FmRimshotModel**

Critères combinés : potentiel de gain CPU, simplicité de réécriture, risque sonore, proximité avec une logique low-cost de référence.

## 3) Qualification concise moteur par moteur

| Moteur | Priorité | Gain CPU potentiel | Risque sonore | Difficulté |
|---|---|---|---|---|
| TRXBassDrum | Haute | Fort | Moyen | Moyenne |
| TRXClaves | Basse | Faible | Faible | Faible |
| TRXHiHat | Haute | Très fort | Moyen | Moyenne |
| TRXSnareDrum | Haute | Fort | Moyen | Moyenne |
| FmKickModel | Moyenne | Moyen à fort | Moyen | Moyenne |
| FmSnareModel | Moyenne-haute | Fort | Moyen à fort | Moyenne à forte |
| FmTomModel | Moyenne | Moyen | Moyen | Moyenne |
| FmRimshotModel | Basse | Faible à moyen | Faible | Faible |
| FmClapModel | Moyenne | Moyen | Moyen | Moyenne |
| FmCowbellModel | Basse | Faible à moyen | Faible | Faible à moyenne |
| FmCymbalModel | Moyenne-haute | Fort à très fort | Moyen à fort | Forte |

## 4) Premier moteur recommandé

**TRXHiHat** est le meilleur premier candidat :

- forte probabilité de coût structurel élevé (composants bruités/multi-voies + filtrage),
- gros potentiel de simplification sans casser l’identité percussive,
- bon compromis impact CPU / risque / délai,
- très aligné avec la logique de référence (génération simple, ratios/structures bornés, modulation parcimonieuse).

## 5) Plan ciblé de simplification — TRXHiHat

### A. À conserver absolument

- l’attaque/transitoire qui donne la lisibilité rythmique,
- la séparation perceptive **closed/open** (ou équivalent decay court/long),
- la couleur spectrale globale (brillante, métallique, contrôlée),
- les invariants musicaux utilisateur (niveau, decay principal, tone/couleur utile).

### B. À simplifier

- réduire le nombre de sources internes simultanées (oscillateurs/noise paths) au minimum utile,
- remplacer les modulations continues non essentielles par des lois plus simples (segments linéaires/exponentiels courts),
- supprimer les interactions de paramètres à faible rendement musical,
- limiter le nombre d’étages de filtrage audio-rate en série.

### C. À figer / borner

- figer certains ratios/offsets internes peu perceptibles,
- borner des plages extrêmes coûteuses (Q très élevé, fréquences inutiles, durées hors usage réel),
- imposer un petit set de structures fixes (2–3 variantes max) plutôt qu’un continuum libre.

### D. À déplacer hors sample-rate

- calcul des coefficients de filtres (ou interpolation lente) au contrôle-rate,
- mappage non-linéaire des paramètres UI vers runtime au déclenchement ou control tick,
- pré-calcul des constantes d’enveloppe à chaque trig/changement paramètre,
- toutes les décisions de routage/structure faites à l’événement, pas dans la boucle audio.

### E. Petites briques partagées éventuellement utiles

- utilitaire commun d’**enveloppe percussive légère** (attaque + decay) sans polymorphisme lourd,
- helper de **smoothing control-rate** minimal,
- helper de conversion paramètre→coefficient (table courte ou approximation simple).

Condition : conserver ces briques triviales, inlineables, sans architecture commune imposée.

### F. Ce qu’il ne faut surtout pas généraliser

- pas de “super-core Drum/FM” unique,
- pas d’API abstraite riche pour couvrir tous les cas,
- pas de mutualisation qui ajoute branchements, indirections ou paramètres génériques inutilisés,
- pas de convergence sonore artificielle entre moteurs hétérogènes.

## 6) Cadre pour la suite

Après TRXHiHat, enchaîner sur **TRXSnareDrum** puis **TRXBassDrum** avec la même méthode :

1. profiler,
2. réduire briques audio-rate,
3. figer/borner,
4. remonter control-rate,
5. valider invariants sonores,
6. mesurer gain IRQ avant moteur suivant.

Aucun patch inter-moteurs global tant qu’un gain réel n’est pas démontré localement.
