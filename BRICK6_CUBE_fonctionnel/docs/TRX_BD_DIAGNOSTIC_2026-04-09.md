# Diagnostic ciblé TRXBassDrum (2026-04-09)

## 1) Noise / attaque bruitée

### Comportement actuel réel
- Le bruit est un bruit blanc uniforme par échantillon via `rand()` normalisé dans `[-1, 1]`.
- Il est injecté uniquement pendant une fenêtre fixe de `kNoiseAttackSamples = 479` échantillons (~10 ms à 48 kHz).
- Son niveau est multiplié par `noise` puis par l'enveloppe globale `env`.
- Il n'y a ni filtrage, ni enveloppe dédiée au bruit, ni corrélation temporelle (donc texture granuleuse/cheap).

### Problème observé
- Le caractère peut être trop "spray"/granuleux et peu musical, car bruit blanc brut + `rand()` sample-à-sample.
- Le bruit participe surtout à la lisibilité de l'attaque/transient et pas au corps tonal.

### Cause technique probable
- Source de bruit non filtrée et non modelée perceptivement.
- Durée fixe 10 ms, indépendante du contexte.
- Amplitude pilotée par l'enveloppe principale (pas d'attaque dédiée).

### Correction locale recommandée
- Oui: simplification locale possible sans perdre la lisibilité.
- Proposition locale minimale: remplacer le bruit blanc sample-à-sample par un bruit "pauvre" mais plus stable:
  1. générer une valeur aléatoire toutes les N samples (sample-and-hold court, ex. N=4..8),
  2. appliquer un 1-pole HP très simple (ou dérivée `x[n]-x[n-1]`) pour garder l'attaque,
  3. conserver la fenêtre de 10 ms et le paramètre `noise`.
- Cette version est plus cheap en coût et en timbre, mais garde le rôle de transient.

## 2) Hauteur réelle / descente dans le grave

### Comportement actuel réel
- Fréquence instantanée de l'oscillateur:
  - `f_inst = pitch + ramp*1000*rampEnv` (Hz), avec `rampEnv` qui décroît exponentiellement.
- Donc la composante de départ est toujours **au-dessus** de la base (sweep uniquement positif).
- Le paramètre `Pitch` est mapé en Hz via `50 * 2^(semitones/12)` puis multiplié par le facteur de note MIDI.
- La phase est reset à 0 sur trigger.

### Vérification C1 (~32.70 Hz)
- Le moteur peut atteindre C1 si `Pitch`/note MIDI donnent ~32.7 Hz et si `Sweep` est faible.
- Mais avec `Sweep > 0`, l'attaque tonale démarre plus haut que la base, donc le "punch" perçu reste plus haut.

### Problème observé
- Constat utilisateur cohérent: la queue descend bas, mais l'attaque tonale reste plus haute.

### Cause technique probable
- Sweep uniquement positif (offset fréquentiel additif).
- Échelle du sweep large (`ramp*1000 Hz`), donc même de petites valeurs remontent fortement le départ.

### Correction locale recommandée
- Oui.
- Correction locale la plus utile: permettre un sweep bipolaire (au moins proche de 0 côté positif), ou réduire fortement son gain (ex. 1000 -> 300/200 Hz).
- Option non destructive: conserver le paramètre existant, mais introduire un mapping non linéaire qui donne une large zone utile proche de 0 pour faciliter les kicks bas.

## 3) Rôle réel du paramètre Attack

### Comportement actuel réel
- `PARAM_DRUM_TRX_BD_ATTACK` est routé vers l'index 4 du modèle.
- Dans `TRXBassDrum`, l'index 4 pilote `start`.
- `start` multiplie toute la composante tonale `sineOut * env * start`.
- Donc ce paramètre agit comme un gain d'excitation/corps, pas comme un temps d'attaque.

### Conséquence DSP réelle
- Aucune enveloppe d'attaque (pas de montée temporelle dédiée).
- Pas de contrôle direct de transient time.
- Impact surtout sur niveau global initial du corps tonal pendant toute la décroissance (via `env`).

### Nommage
- Oui, nom actuel trompeur (`Attack` ≠ attack time).

### Correction locale recommandée
- Oui.
- Deux voies locales possibles:
  1. **Sans changer DSP**: renommer conceptuellement en `Start Level`/`Body Level`.
  2. **Avec légère DSP locale**: faire de `Attack` un vrai temps de montée (mini-envelope d'attaque 0->1 sur 0.2..10 ms) appliquée au corps tonal.
- Priorité pragmatique: d'abord clarifier/remapper le sens, puis ajuster DSP si besoin.

## Ordre optimal des corrections locales
1. **Pitch/Sweep (descente grave réelle du punch)**: impact musical majeur et directement lié au besoin C1.
2. **Attack (naming/comportement)**: évite une ergonomie trompeuse et aligne le contrôle utilisateur.
3. **Noise simplification**: utile pour le caractère, mais secondaire une fois pitch/punch corrigés.
