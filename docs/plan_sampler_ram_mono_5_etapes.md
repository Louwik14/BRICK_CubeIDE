# Plan d’action — Sampler RAM mono natif

## Objectif

Ajouter un chemin mono natif au Sampler RAM :

```text
WAV mono
→ FLOAT32_MONO au chargement
→ stockage mono en SDRAM
→ lecteur RAM mono
→ enveloppe / gain / filtre mono
→ promotion stéréo uniquement au pan ou avant un traitement incompatible
→ mixeur stéréo
```

Les WAV stéréo conservent leur chemin actuel.

Le projet est en prototypage :

- aucune compatibilité avec les anciens projets, patterns, patches ou formats internes ;
- le format courant peut être invalidé ou réinitialisé ;
- aucune migration historique ni couche legacy ;
- chaque étape validée crée un commit local dédié, sans push ;
- seuls les builds Release Low-Cost et Release Premium sont requis.

Hors périmètre : Streamer SD, Multi Sampler, futur Sampler Group, séquenceur, p-locks, modulation, interpolation, allocation dynamique et architecture générale des pistes.

## Contrat cible

```c
typedef enum
{
    SAMPLER_RAM_FORMAT_NONE = 0,
    SAMPLER_RAM_FORMAT_FLOAT32_MONO,
    SAMPLER_RAM_FORMAT_FLOAT32_STEREO_INTERLEAVED
} sampler_ram_format_t;
```

| Format | Canaux | Octets/frame | Frames/page de 16 Kio |
|---|---:|---:|---:|
| Mono | 1 | 4 | 4096 |
| Stéréo | 2 | 8 | 2048 |

Le pool existant reste inchangé : pages physiques de 16 Kio, aucun second pool, aucune taille variable, aucun changement du cache Stream/Multi.

---

# Étape 1 — Format, allocation et chargement mono

## But

Ajouter le format mono et charger les WAV mono sans duplication L/R.

## Travaux

- Ajouter `FLOAT32_MONO` et expliciter le format stéréo.
- Centraliser les calculs :
  - octets/frame ;
  - frames → octets ;
  - octets → pages ;
  - coût physique aligné.
- Utiliser des intermédiaires 64 bits.
- WAV mono :
  - `channels = 1` ;
  - `bytes_per_frame = 4` ;
  - une seule valeur float écrite par frame.
- WAV stéréo :
  - chemin actuel inchangé ;
  - `channels = 2` ;
  - `bytes_per_frame = 8`.
- Adapter la waveform overview au mono.
- Capturer le format ou le stride dans le slot et la voix.
- Ne pas modifier Stream/Multi.

## Fichiers principaux

- `Inc/Sampler/sampler_ram_pool.h`
- `Src/Sampler/sampler_ram_pool.c`
- `Inc/Sampler/sample_page_cache_config.h`
- `Src/Sampler/sample_page_cache.c`
- `Inc/Storage/wav_parser.h`
- `Src/Storage/wav_parser.c`
- `Src/Core/brick6_sampler_runtime.c`
- `Inc/Core/brick6_sampler_runtime.h`
- tests ciblés.

## Validations

- PCM16/PCM24 mono ;
- PCM16/PCM24 stéréo inchangés ;
- nombre exact de frames ;
- valeurs float ;
- aucune duplication L/R mono ;
- coûts pour 1, 2048, 2049, 4096 et 4097 frames ;
- refus des formats non supportés ;
- builds Release Low-Cost et Premium.

## Critère

Un WAV mono de `F` frames occupe `F × 4` octets logiques et `ceil(F / 4096)` pages.

## Commit

```text
sampler: store RAM WAV mono natively
```

Commit local dédié, sans push.

---

# Étape 2 — Kernels de rendu RAM mono

## But

Ajouter des kernels mono spécialisés pour tous les modes RAM.

## Cas à couvrir

- forward 1× ;
- forward pitché ;
- reverse ;
- shot ;
- loop forward ;
- ping-pong 1× et pitché ;
- slicing ;
- fin de sample/région ;
- fallback borné.

## Travaux

Les kernels mono lisent :

```c
sample = data[frame_index];
```

Le choix mono/stéréo est effectué une fois au déclenchement ou par bloc, jamais par frame.

Conserver strictement :

- position Q16 ;
- Start/End/Loop ;
- reverse ;
- ping-pong ;
- slicing ;
- fade de démarrage ;
- politique actuelle sans interpolation.

## Fichiers principaux

- `Src/Core/brick6_sampler_runtime.c`
- `Inc/Core/brick6_sampler_runtime.h`
- tests de rendu RAM.

## Validations

- tous les modes ci-dessus ;
- samples de 1 et 2 frames ;
- pas supérieur à une frame ;
- bornes invalides ;
- comparaison avec une référence stéréo aux deux canaux identiques ;
- builds Release Low-Cost et Premium.

## Critère

Tous les modes RAM mono utilisent un renderer mono spécialisé et reproduisent le comportement musical de l’ancien mono dupliqué.

## Commit

```text
sampler: add RAM mono render kernels
```

Commit local dédié, sans push.

---

# Étape 3 — Mixer mono-native, filtre mono et pan tardif

## But

Conserver le signal mono jusqu’au dernier point raisonnable.

## Chemin attendu

```text
renderer RAM mono
→ filtre mono
→ VCA / gain / mute mono
→ promotion stéréo si nécessaire
→ pan
→ bus stéréo
```

Utiliser le chemin mono-native existant du mixer.

## Règle sur les inserts

Ne pas modifier l’ordre ou la nature des inserts dans cette première version.

Si un insert ne supporte pas le mono :

```text
mono
→ promotion stéréo propre
→ insert stéréo existant
```

Ne pas déplacer silencieusement une saturation non linéaire avant le pan. L’optimisation mono des inserts reste un chantier séparé.

## Fichiers principaux

- `Src/Core/brick6_audio_runtime.c`
- `Inc/Audio/mixer.h`
- `Src/Audio/mixer.c`
- `Inc/Audio/fx_chain.h`
- `Src/Audio/fx_chain.c`

## Validations

- pan gauche/centre/droite ;
- filtre OFF/LP/HP/BP/EQ3 ;
- VCA/gain/mute ;
- promotion contrôlée avant un insert stéréo ;
- sends/routing ;
- régression stéréo ;
- builds Release Low-Cost et Premium.

## Critère

Un WAV mono compatible reste mono jusqu’au pan. Un traitement incompatible provoque une seule promotion contrôlée.

## Commit

```text
sampler: route RAM mono through native mixer path
```

Commit local dédié, sans push.

---

# Étape 4 — Cycle de vie, génération et declick

## But

Sécuriser le format mono pendant toute la vie de la voix.

## Travaux

Couvrir :

- trigger ;
- stop et note-off ;
- panic et transport stop ;
- slot libéré ou réutilisé ;
- changement de génération ;
- fade-in ;
- declick tail ;
- enchaînements mono→stéréo et stéréo→mono.

Privilégier une solution simple et sûre.

Les tails peuvent rester temporairement stéréo si cela réduit le risque. Le but n’est pas d’optimiser quelques frames de transition, mais d’éviter clics, pointeurs périmés et incohérences de format.

## Fichiers principaux

- `Src/Core/brick6_sampler_runtime.c`
- `Inc/Core/brick6_sampler_runtime.h`
- `Src/Core/brick6_audio_runtime.c`
- `Src/Sampler/sampler_ram_pool.c`

## Validations

- stop en forward/reverse/loop/ping-pong ;
- note-off ;
- panic/transport stop ;
- slot libéré pendant la voix ;
- slot réutilisé avec un autre format ;
- génération invalide ;
- mono suivi de stéréo et inversement ;
- slicing puis réutilisation ;
- builds Release Low-Cost et Premium.

## Critère

Aucune voix ne lit un ancien slot et aucun changement de format ne provoque de clic ou de sortie incohérente.

## Commit

```text
sampler: secure RAM mono voice lifecycle
```

Commit local dédié, sans push.

---

# Étape 5 — Validation finale, nettoyage et documentation

## But

Valider le chemin complet, retirer les hypothèses stéréo obsolètes et documenter le contrat courant.

## Matrice finale

- PCM16/PCM24 mono ;
- PCM16/PCM24 stéréo ;
- frames et valeurs float exactes ;
- absence de duplication mono ;
- forward, pitch, reverse, shot, loop, ping-pong, slicing ;
- fin de sample/région ;
- fade/declick ;
- pan ;
- filtres ;
- stop/panic/release ;
- génération invalide et slot réutilisé ;
- refus des formats non supportés ;
- aucune régression stéréo ;
- Stream/Multi inchangés ;
- aucune interpolation nouvelle.

## Nettoyage

Rechercher les hypothèses :

```text
channels == 2
FLOAT32_INTERLEAVED
frame * 2
8 octets/frame
```

Justifier les occurrences restantes comme appartenant au chemin stéréo ou au cache Stream/Multi.

Supprimer :

- duplication mono résiduelle ;
- checks stéréo RAM obsolètes ;
- commentaires faux ;
- code transitoire inutilisé.

## Documentation

Documenter :

- les deux formats internes ;
- 4096 frames mono/page ;
- pages physiques de 16 Kio inchangées ;
- dispatch hors boucle ;
- filtre mono et pan tardif ;
- points de promotion stéréo ;
- inserts restant stéréo ;
- absence d’interpolation ;
- Stream/Multi hors périmètre ;
- absence volontaire de compatibilité historique.

## Mesure

Aucune étape séparée de mesure.

Le rapport final inclut seulement :

- coûts mémoire pour quelques longueurs représentatives ;
- tailles finales des structures modifiées ;
- confirmation de compatibilité du pool ;
- estimation qualitative du gain IRQ ;
- rappel que la mesure IRQ réelle est réalisée sur matériel par l’utilisateur.

## Critère

Chemin mono natif complet, chemin stéréo inchangé, tests/builds réussis, code transitoire retiré et documentation cohérente.

## Commit

```text
sampler: finalize native RAM mono path
```

Commit local dédié, sans push.

---

# Critère de réussite global

1. Les WAV mono sont stockés en `FLOAT32_MONO`.
2. Une page contient jusqu’à 4096 frames mono.
3. Aucune duplication L/R mono en SDRAM.
4. Tous les modes RAM possèdent un chemin mono correct.
5. Filtre et VCA restent mono jusqu’au point de promotion.
6. Les traitements incompatibles provoquent une promotion unique et contrôlée.
7. Les tails et changements de génération sont sûrs.
8. Le Sampler RAM stéréo ne régresse pas.
9. Stream/Multi restent inchangés.
10. Aucune interpolation ou allocation dynamique n’est ajoutée.
11. Aucune compatibilité historique inutile n’est ajoutée.
12. Chaque étape produit un commit local dédié, sans push.
13. Seuls les builds Release Low-Cost et Release Premium sont requis.
