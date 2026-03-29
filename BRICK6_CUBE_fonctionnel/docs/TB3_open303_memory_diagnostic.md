# TB3 / Open303 — Diagnostic mémoire (V1 voice-only)

## Mesure de base

- `sizeof(rosic::Open303) = 431800` octets (mesuré via compilation hôte).
- `8 x Open303 = 3 454 400` octets (~3.46 MB), avant même le reste du firmware.

## Répartition interne principale

Mesures `sizeof(...)` des blocs majeurs:

- `MipMappedWaveTable`: `213472` octets
- `AcidSequencer`: `3392` octets
- `BlendOscillator`: `80` octets
- `TeeBeeFilter`: `248` octets
- `AnalogEnvelope`: `208` octets
- `DecayEnvelope`: `48` octets
- `BiquadFilter`: `112` octets
- `OnePoleFilter`: `80` octets
- `EllipticQuarterBandFilter`: `96` octets

Dans `Open303`, les deux membres dominants sont:

- `waveTable1` (`MipMappedWaveTable`): `213472` octets
- `waveTable2` (`MipMappedWaveTable`): `213472` octets

=> Les deux wavetables représentent à elles seules ~`426944` octets par instance (la quasi-totalité de `Open303`).

## Pourquoi c'est si gros

`rosic_MipMappedWaveTable` embarque en membre d'instance:

- `prototypeTable[2048]` en `double`
- `tableSet[12][2052]` en `double`

Ce stockage est **dupliqué pour chaque instance** et pour les **deux** wavetables (`waveTable1`, `waveTable2`) d'un `Open303`.

Le séquenceur interne (`AcidSequencer` + `AcidPattern`) est bien présent dans `Open303`, mais son coût mémoire est faible (~3.3 KB par instance) comparé aux wavetables.

## Classification (quoi faire de quoi)

### Doit rester en RAM interne rapide

- États de filtres/enveloppes/intégrateurs/phase oscillator (petits mais audio-rate, mutables à l'échantillon).
- Quelques scalaires runtime (fréquence instantanée, cutoff instantané, etc.).

### Peut aller en SDRAM externe

- Stockage volumineux des tables wavetable (si pas de mutualisation immédiate).
- Éventuels buffers temporaires non IRQ-critiques.

### Peut être mutualisé globalement

- Le contenu des tables `SAW303` / `SQUARE303` (et leurs mipmaps) est identique entre instances à paramètres identiques de génération.
- Architecture cible: pool global de tables read-mostly + références par instance.

### Peut être pré-calculé puis stocké en flash externe / read-only

- Les tables mipmap finales (`tableSet`) pour formes fixes TB-3.
- Alternative: génération offline, stockage binaire en flash externe, copie optionnelle en SDRAM au boot.

### Peut être supprimé en V1 voice-only

- `AcidSequencer` interne et `AcidPattern` associés, si la V1 reste pilotée uniquement par scheduler externe.

### Peut être remplacé par version plus légère

- Passage de tables `double` -> `float` (gros gain mémoire, impact fidélité à évaluer).
- Réduction `tableLength` / `numTables` si budget anti-aliasing acceptable.

## Réalisme nombre d'instances (STM32H743)

Avec le vrai `Open303` actuel (tables par instance en `double`, oversampling=4):

- **8 instances**: non réaliste en RAM interne (même D1+D2+D3), et risqué CPU.
- **4 instances**: toujours trop lourd sans mutualisation des tables, CPU probablement tendu.
- **2 instances**: possible avec placement mémoire agressif (SDRAM) mais à valider CPU/RT.
- **1-2 instances**: cible réaliste immédiate sans refactor profond.

## Plus petite stratégie viable (ordre recommandé)

### Option A (recommandée)
- Réduire temporairement `TB3_SYNTH_MAX_INSTANCES` à `2` (ou `1`) **+** conserver workaround SDRAM.
- Bénéfice mémoire: immédiat et massif.
- Risque CPU/RT: limité (réduction charge voix).
- Complexité: très faible.
- Fidélité Open303: inchangée par voix.

### Option B
- Retirer `AcidSequencer` de `Open303` pour build V1 voice-only (classe/flag compile-time).
- Bénéfice mémoire: faible à modéré (~3.3 KB/instance).
- Risque CPU/RT: faible.
- Complexité: faible à moyenne.
- Fidélité: inchangée sur moteur voix; perte uniquement des fonctions pattern internes.

### Option C
- Mutualiser `MipMappedWaveTable` (2 tables globales partagées entre instances).
- Bénéfice mémoire: **très élevé** (supprime ~426 KB par instance dupliquée).
- Risque CPU/RT: faible si accès read-only cache-friendly.
- Complexité: moyenne (refactor ciblé de ownership des tables).
- Fidélité: inchangée si mêmes valeurs de tables.

### Option D
- Tables précalculées en flash externe/read-only + mapping en mémoire.
- Bénéfice mémoire interne: très élevé.
- Risque CPU/RT: dépend latence bus externe/cache.
- Complexité: moyenne à élevée.
- Fidélité: inchangée si données identiques.

## Recommandation prioritaire

1) Verrouiller V1 à **1-2 instances** immédiatement.  
2) Implémenter ensuite la **mutualisation des 2 MipMappedWaveTable** (gain principal).  
3) En troisième étape, décider si séquenceur interne doit être compilé hors V1.

Cette séquence donne un chemin court, sûr, sans réécrire l'architecture audio globale.
