# TRXBassDrum — analyse ciblée et cible simplifiée low-CPU

## 1) Analyse de l’implémentation actuelle (uniquement TRXBassDrum)

Implémentation actuelle résumée :

- Source principale : un sinus (`sin`) à fréquence instantanée `pitch + ramp * rampEnv * 1000`.
- Deux enveloppes multiplicatives (`env`, `rampEnv`) mises à jour à chaque échantillon avec `exp`.
- Couche harmonique optionnelle via `tanh(sineOut * 3)`.
- Burst de bruit optionnel durant ~10 ms (`rand`) modulé par `env`.
- Soft-clip optionnel via `tanh(value * (1 + clip * 5))`.

Le moteur expose 8 paramètres :
`pitch`, `decay`, `ramp`, `rampDecay`, `start`, `noise`, `harmonics`, `clip`.

## 2) Identification précise

### 2.1 Invariants sonores à préserver absolument

1. **Transitoire lisible** au déclenchement (impact kick).
2. **Descente de pitch** (pitch sweep) responsable du caractère TRX-like du kick.
3. **Corps grave stable** (fondamental nette, non baveuse).
4. **Décroissance énergétique cohérente** (decay musical exploitable).
5. **Option de texture d’attaque** (un minimum de bruit/attaque utile, sans imposer du bruit permanent).

### 2.2 Éléments structurels probablement les plus coûteux

Par sample, le moteur peut faire :

- `exp` pour `env` + `exp` pour `rampEnv` (**2 exponentielles/sample**),
- `sin` pour l’oscillateur,
- `tanh` harmonique si `harmonics > 0`,
- `rand` + normalisation flottante pendant la fenêtre d’attaque si `noise > 0`,
- `tanh` de clip si `clip > 0`.

Même avec branches, le pire cas cumule plusieurs fonctions transcendantes dans la boucle audio.

### 2.3 Paramètres à fort rendement musical

- `pitch` : règle immédiatement l’assise du kick.
- `decay` : contrôle macro de longueur/poids.
- `ramp` + `rampDecay` : forment la signature de sweep (impact + “drop”).
- `start` (attack) : influence l’attaque/perception du punch.

### 2.4 Paramètres secondaires / redondants / trop libres

- `harmonics` + `clip` : deux chemins de non-linéarité séparés, potentiellement redondants en perception (les deux ajoutent présence/roughness).
- `noise` en continu 0..1 peut être trop libre pour un kick TRX-style ; musicalement, 2–3 régimes suffisent souvent.
- `start` jusqu’à 2.0 (gain direct) peut encourager des zones extrêmes compensées ensuite par clip.

### 2.5 Ce qui tourne au sample-rate et devrait pouvoir en sortir

- Calcul des coefficients d’enveloppe (`exp(-1/(tau*Fs))`) actuellement refait à chaque sample.
- Mapping/borning de paramètres : possible au changement paramètre ou au trigger.
- Fenêtre de bruit (10 ms) : la durée en samples peut être pré-calculée au trigger.
- Gains dérivés de `clip` / `harmonics` : pré-calculables au control-rate/trigger.

## 3) Comparaison à la logique low-cost de référence

### 3.1 Déjà aligné

- Noyau simple centré sur une seule voix tonale.
- Concept percussif clair (attaque + sweep + decay).
- Peu d’états internes (phase, envs, etc.), pas de structures lourdes.

### 3.2 Trop riche / trop flexible

- Double saturation (`harmonics` + `clip`) superposée.
- Plages très larges pour certains contrôles (ex. `start` jusqu’à 2.0, bruit continu complet).
- Multiplication de “couleurs” qui peuvent être condensées en un seul axe de caractère.

### 3.3 Manque de bornage

- Pas de quantification/étagement perceptif pour les zones peu utiles.
- Pas de mode structurel explicite (ex. kick propre vs kick sale) ; tout est continu.

### 3.4 Ce qui devrait être figé/simplifié

- Fusionner la couleur non-linéaire vers **un seul étage** (avec drive borné),
- Borner l’attaque (`start`) et/ou la dériver de la vélocité,
- Borner le bruit à une enveloppe d’attaque courte fixe + intensité limitée,
- Pré-calculer toutes constantes d’enveloppe et de gains dérivés hors boucle sample.

## 4) Version cible simplifiée (TRXBassDrum uniquement)

### 4.1 Structure DSP minimale proposée

1. **Oscillateur sinus unique** (phase accumulateur).
2. **Pitch env unique** appliquée à la fréquence (descente exponentielle ou pseudo-exp bornée).
3. **Amp env unique** pour le corps.
4. **Attack transient minimal** : click/noise court (fenêtre fixe pré-calculée).
5. **Un seul étage de saturation** post-mix (drive borné).

Forme générale :
`out = saturate( (sin(phase) * amp_env * level) + attack_burst )`.

### 4.2 État minimal

- `phase`
- `amp_env`
- `pitch_env`
- `attack_counter` (samples restants)
- constantes pré-calculées au trigger :
  - `amp_env_coef`
  - `pitch_env_coef`
  - `base_phase_inc`
  - `sweep_amount_hz`
  - `drive_gain`

### 4.3 Paramètres utilisateur à conserver

Conserver 5 contrôles principaux (haut rendement) :

- `Pitch`
- `Decay`
- `Sweep Amount`
- `Sweep Decay`
- `Attack` (niveau transitoire)

### 4.4 Paramètres à figer / borner

- Remplacer `harmonics` + `clip` par **un unique `Drive` borné**.
- `Noise` : soit fixé à un comportement interne léger, soit borné fortement (0..petit max utile).
- `Attack/start` : borner à une plage perceptivement utile (éviter >1.2 par exemple).
- Limiter `Sweep Amount` à une zone musicale (éviter extrêmes coûteux/inutiles).

### 4.5 Calculs à déplacer hors sample-rate

Au minimum hors sample-rate (param change, trigger, control tick) :

- coefficients d’enveloppes,
- conversion paramètres -> gains/Hz bornés,
- durée de burst d’attaque en samples,
- constantes de saturation (`drive_gain`, éventuel `output_trim`).

### 4.6 Petites briques utilitaires éventuellement utiles (sans généralisation)

- helper local “`compute_env_coef(time_s)`”,
- helper local “`clamp_and_map_param`”,
- helper local “`fast soft sat`” (une seule implémentation simple).

Portée volontairement locale à TRXBassDrum (pas de core commun).

## 5) Plan d’action concret (TRXBassDrum seulement)

### 5.1 Modifications structurelles à faire d’abord

1. **Sortir les `exp` du sample-rate** : coefficients calculés au trigger / update param.
2. **Réduire la topologie** à : sinus + transient court + saturation unique.
3. **Fusionner harmonics/clip** en un seul axe de drive borné.
4. **Borner strictement** plages de sweep/attack/noise aux zones musicales utiles.

### 5.2 Modifications secondaires ensuite

1. Discrétiser légèrement certains réglages secondaires (steps perceptifs),
2. Stabiliser le niveau de sortie inter-réglages (trim simple),
3. Optimiser éventuellement la source de transient (noise vs click déterministe) selon écoute/CPU.

### 5.3 Points de vigilance sonore

- Ne pas perdre la lisibilité de l’attaque,
- Ne pas aplatir excessivement la dynamique du sweep,
- Éviter les artefacts de saturation sur faibles niveaux,
- Conserver un grave propre (pas de dérive DC, pas de bave en fin de decay).

### 5.4 Ce qu’il ne faut surtout pas dégrader

- identité kick : impact initial + chute de pitch + corps grave,
- plage musicale de pitch/decay,
- stabilité de niveau et répétabilité des triggers,
- coût CPU borné même en modulation des paramètres pertinents.
