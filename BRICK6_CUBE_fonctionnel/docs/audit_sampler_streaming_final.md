# Audit technique final — moteur audio STM32H743 vers sampler disk-streaming polyphonique

## Statut du document

Ce document consolide :

- l’audit initial de l’architecture audio actuelle,
- les compléments d’analyse techniques,
- les décisions d’architecture finales retenues avant implémentation.

Il a pour but de servir de **base de référence unique** pour l’implémentation
du sampler streaming.

Il ne propose pas de réécrire le moteur audio temps réel.  
Il fixe une architecture cible réaliste, robuste et exploitable sur **STM32H743 + SDRAM + SDMMC + FATFS**.

---

# 1. Objectif produit

Construire un sampler avec les caractéristiques suivantes :

- jusqu’à **576 samples** dans un projet,
- jusqu’à **24 voices simultanées**,
- lecture **WAV stéréo 48 kHz**,
- DSP **float**, traitement **block-based**,
- streaming depuis **carte SD** via **FATFS**,
- déclenchement perçu comme **instantané**,
- fonctionnement **stable** sans underrun,
- aucune lecture disque dans l’IRQ audio,
- aucune allocation dynamique dans le DSP.

## Contraintes de conception

- **Chemin IRQ audio inchangé autant que possible**
- **Streaming uniquement dans la superloop**
- **Producteur disque / consommateur DSP**
- **Architecture déterministe**
- **Compatibilité avec 24 voices sans tempête de lectures SD**

---

# 2. Architecture actuelle — état des lieux

## 2.1 Vue d’ensemble

Architecture observée actuellement :

```text
SD / FATFS (main loop)
    └─ app_sample_boot_init()
        └─ stream_manager_start(path)
            └─ audio_streamer_start(path)

Main loop
    ├─ engine_tasklet_poll()
    ├─ brick6_app_process()
    │   └─ stream_manager_process()
    │       └─ audio_streamer_process()   // f_read / f_lseek ici
    ├─ USB / MIDI / UI
    └─ autres tâches non IRQ

IRQ DMA SAI RX half/full
    └─ audio_process_block_int32()
        ├─ audio_io_unpack()
        ├─ my_dsp() via dsp_engine
        │   └─ stream_manager_get_frame() // lecture ring uniquement
        └─ audio_io_pack()
```

## 2.2 Modules identifiés

### Moteur audio bas niveau
- `Src/Audio/audio.c`
- Gère :
  - DMA ping-pong RX/TX,
  - callbacks IRQ SAI/DMA,
  - appel du moteur float bloc.

### Frontière DSP float
- `Src/Audio/audio_float.c`
- `Src/Audio/dsp_engine.c`
- Rôle :
  - conversion int24/int32 ↔ float,
  - dispatch vers le callback DSP applicatif.

### Callback DSP applicatif
- `Src/Core/brick6_app_init.c`
- `my_dsp()`
- Injecte actuellement le flux streamé dans une track.

### Streaming SD actuel
- `Src/Storage/audio_streamer.c`
- Actuellement :
  - un seul streamer global,
  - un seul `FIL`,
  - un seul ring buffer interleavé stéréo.

### Façade stream manager
- `Src/Streaming/stream_manager.c`
- Wrapper très léger vers `audio_streamer`.

### WAV loader / parser
- `Src/Storage/wav_loader.c`
- `Src/Storage/wav_parser.c`
- Rôle :
  - lecture de métadonnées,
  - validation format,
  - chargement RAM dans certaines variantes/tests.

### Sampler / voice actuel
- `Src/Audio/sampler.c`
- Voix RAM simple, sans vrai streaming multi-voix.

### Ring buffers
- Ring principal streaming dans `audio_streamer.c`
- `sd_audio_block_ring.c` existe mais n’est pas branché au pipeline actuel.

---

# 3. Lecture du système actuel

## 3.1 Ce qui est déjà bon

L’architecture actuelle possède déjà les propriétés essentielles d’un moteur
audio embarqué robuste :

- le **DSP tourne en IRQ audio**,
- la **lecture SD se fait hors IRQ**,
- le **ring buffer sépare temps réel et disque**,
- la **conversion audio est centralisée**,
- le **moteur float** est déjà en place.

C’est une très bonne base pour évoluer vers un sampler streaming.

## 3.2 Ce qui manque pour la cible

Il manque encore :

- un **sample pool** de 576 entrées,
- un **voice manager** 24 voices,
- un **pool de streamers** (jusqu’à 24),
- un **scheduler SD unique**,
- une gestion explicite **ATTACK → STREAM**,
- une discipline stricte sur les **unités des ring buffers**,
- une politique produit sur la **fragmentation FATFS**.

---

# 4. Architecture cible validée

## 4.1 Vue d’ensemble

Architecture finale retenue :

```text
Sample pool (576)
    ├─ metadata WAV
    ├─ attack cache RAM (~20 ms)
    └─ infos de lecture

Voices (24 max)
    ├─ sample_id
    ├─ playback_position
    ├─ gain
    ├─ state = ATTACK / STREAM
    └─ streamer_id

Streamers (24 max)
    ├─ ring buffer
    ├─ FIL
    ├─ read_pos / write_pos
    ├─ file_pos
    └─ état

Stream manager (main loop only)
    ├─ choisit le streamer à servir
    ├─ fait un refill borné
    └─ garantit qu’une seule lecture disque est en cours

DSP callback (IRQ audio)
    ├─ pour chaque frame
    ├─ pour chaque voice active
    │   ├─ ATTACK -> lecture RAM
    │   └─ STREAM -> lecture ring
    └─ mixage vers bus / sorties
```

## 4.2 Principe général

- **Chaque sample du projet** possède une **attaque préchargée en RAM**.
- **Chaque voice active** joue d’abord l’attaque RAM.
- Ensuite la voice bascule sur un **streamer disque**.
- Le streamer lit le WAV séquentiellement depuis la SD dans un **ring buffer**.
- Le DSP ne lit **jamais** directement la SD.

---

# 5. Décisions d’architecture finales

## 5.1 Attack cache pour les 576 samples

Décision retenue :

- **oui**, chaque sample du projet possède une attaque RAM,
- durée nominale : **20 ms**.

### Pourquoi

Si l’utilisateur peut déclencher **n’importe lequel** des 576 samples à tout moment
et que l’on veut un comportement type sampler hardware, alors il faut garantir que
le début du sample soit disponible **immédiatement**, sans dépendre de la latence SD.

### Taille mémoire

Pour 20 ms à 48 kHz stéréo float :

- `48000 * 0.02 = 960 frames`
- `960 * 2 canaux * 4 bytes = 7680 bytes ≈ 7.5 KB`

Pour 576 samples :

- `576 * 7680 bytes ≈ 4.22 MB`

Décision validée : **acceptable en SDRAM 32 MB**.

---

## 5.2 24 voices actives max

Décision retenue :

- **24 voices** maximum actives simultanément,
- **24 streamers max**.

### Important

- `576 samples` = catalogue du projet
- `24 voices` = polyphonie
- `24 streamers` = maximum pratique de lectures simultanées logiques

---

## 5.3 Un streamer par voice active

Décision retenue :

- **un streamer par voice active**.

C’est l’architecture la plus simple, la plus lisible et la plus robuste pour
un sampler hardware embarqué.

---

# 6. Structures cibles

## 6.1 Sample pool

Structure conceptuelle cible :

```c
typedef struct
{
    char path[64];

    uint32_t data_offset;
    uint32_t length_frames;
    uint32_t bytes_per_frame;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;

    float *attack_cache;
    uint32_t attack_frames;

    uint8_t valid;
} sample_desc_t;
```

Table :

```c
sample_desc_t sample_pool[576];
```

---

## 6.2 Voices

Structure conceptuelle cible :

```c
typedef enum
{
    VOICE_OFF = 0,
    VOICE_ATTACK,
    VOICE_STREAM
} voice_state_t;

typedef struct
{
    uint16_t sample_id;
    uint16_t streamer_id;

    uint32_t playback_position;
    float gain_l;
    float gain_r;

    voice_state_t state;
    uint8_t active;
} voice_t;
```

Table :

```c
voice_t voices[24];
```

---

## 6.3 Streamers

Structure conceptuelle cible :

```c
typedef enum
{
    STREAMER_IDLE = 0,
    STREAMER_ACTIVE,
    STREAMER_EOF,
    STREAMER_ERROR
} streamer_state_t;

typedef struct
{
    FIL fp;

    uint32_t data_offset;
    uint32_t data_size;
    uint32_t file_data_pos;
    uint32_t bytes_per_frame;

    uint32_t read_pos;   // frames stéréo
    uint32_t write_pos;  // frames stéréo
    uint32_t ring_capacity_frames;

    float *ring;         // interleaved stereo float

    uint32_t fill_frames;

    uint32_t low_wm;
    uint32_t critical_wm;
    uint32_t target_fill;
    uint32_t high_wm;

    uint32_t last_service_tick;

    streamer_state_t state;
    uint8_t sample_id;
    uint8_t voice_id;
} streamer_t;
```

Table :

```c
streamer_t streamers[24];
```

---

# 7. Scheduler streaming — décision finale

## 7.1 Décision retenue

Le scheduler retenu n’est **pas** le modèle complexe à score pondéré.

Décision finale :

- scheduler **simple, déterministe, min-fill**,
- éventuellement départagé par **ancienneté de service**.

## 7.2 Algorithme retenu

À chaque passage dans la superloop :

1. scanner les streamers actifs,
2. trouver celui dont le ring est le plus vide,
3. faire **un refill**,
4. sortir.

Option :
- faire un **2e refill** uniquement si un streamer est sous un seuil critique.

Pseudo-code cible :

```c
streamer_t *worst = NULL;
uint32_t lowest_fill = UINT32_MAX;

for(i = 0; i < streamer_count; i++)
{
    if(streamers[i].state != STREAMER_ACTIVE)
        continue;

    if(streamers[i].fill_frames < lowest_fill)
    {
        lowest_fill = streamers[i].fill_frames;
        worst = &streamers[i];
    }
}

if(worst && worst->fill_frames < worst->low_wm)
{
    refill_streamer(worst);
}
```

## 7.3 Pourquoi ce choix

Avec seulement **24 streamers maximum** :

- la complexité est négligeable,
- le comportement est très lisible,
- la fairness est suffisante en pratique,
- la robustesse est meilleure qu’avec un scheduler trop sophistiqué.

C’est typiquement ce qui est fait dans beaucoup de samplers hardware.

## 7.4 Fairness

La fairness minimale retenue :

- départage par **dernier service le plus ancien** si deux streamers ont le même fill.

Cela suffit.

---

# 8. Stratégie FATFS — décision finale

## 8.1 Problème

Deux stratégies étaient possibles :

### A — handles fichiers persistants
- garder les fichiers ouverts pendant la vie du streamer

### B — open/close à chaque refill
- ouvrir, lire, fermer à chaque chunk

## 8.2 Décision retenue

Décision finale : **A — handles persistants par streamer actif**.

## 8.3 Pourquoi

Ouvrir / fermer un fichier à chaque refill :

- augmente la latence,
- augmente le jitter,
- ajoute des accès FAT supplémentaires,
- rend le comportement moins déterministe.

Un sampler hardware doit préférer :

- ouvrir au démarrage du streamer,
- lire tant que la voice est active,
- fermer à la fin.

## 8.4 Contraintes FATFS

À vérifier dans la configuration projet :

- `FF_FS_LOCK`
- nombre maximum de fichiers ouverts
- mémoire disponible pour les `FIL`

Si la configuration FATFS limite le nombre de fichiers ouverts simultanément,
il faudra ajuster cette option.

Mais l’architecture finale retenue reste :

- **handles persistants**.

---

# 9. Fragmentation FATFS — politique produit

## 9.1 Problème

Le débit moyen de la carte SD ne suffit pas à garantir la stabilité.

Le vrai danger vient de :

- fragmentation des fichiers,
- spikes de latence `f_read`,
- sauts de clusters FAT.

## 9.2 Politique produit retenue

Décision finale :

### Discipline média
- exiger une **carte formatée** pour les imports,
- favoriser la **copie en lot** des samples,
- éviter les suppressions / réécritures répétées.

### Vérification logicielle
- check de fragmentation / contiguïté au boot ou à l’import si possible,
- marquage éventuel d’un sample ou projet en **mode dégradé** si fragmentation excessive.

### Protection runtime
- ring buffers suffisamment grands,
- instrumentation `f_read` max / P99,
- watchdog de starvation.

## 9.3 Conclusion

La stratégie correcte est :

- **média propre** + **protections logicielles**.

La discipline utilisateur seule ne suffit pas.

---

# 10. Chunk size de lecture — décision finale

## 10.1 Valeurs envisagées

- 512 frames
- 1024 frames
- 2048 frames

## 10.2 Décision retenue

Décision finale :

- **chunk nominal = 1024 frames stéréo**
- **mode urgence = 2 × 1024 possible** si nécessaire

## 10.3 Pourquoi

### 512 frames
- trop de calls FATFS,
- overhead plus élevé.

### 2048 frames
- refill plus lourd,
- moins réactif en forte polyphonie.

### 1024 frames
- bon compromis :
  - efficacité SDMMC,
  - overhead FATFS raisonnable,
  - conversion PCM → float encore légère.

## 10.4 Taille en octets

WAV 24-bit stéréo :

- `6 bytes / frame`

Donc :

- `1024 frames = 6144 bytes`

Très bonne taille pour SDMMC + FATFS.

---

# 11. Ring buffers — décision finale

## 11.1 Valeurs envisagées

- 2048 frames
- 4096 frames
- 8192 frames

## 11.2 Décision retenue

Décision finale :

- **2048** : minimum technique
- **4096** : **recommandé produit**
- **8192** : mode robuste / cartes médiocres

### Recommandation finale par défaut

- **4096 frames par streamer**

## 11.3 Justification

À 48 kHz :

- `4096 / 48000 ≈ 85 ms`

Cela donne une marge confortable face à :

- spikes SD,
- fluctuations de superloop,
- overhead FATFS.

## 11.4 Watermarks de départ

Pour un ring de 4096 frames :

- `critical_wm = 512`
- `low_wm = 1024`
- `target_fill = 3072`
- `high_wm = 4096`

Décision validée.

---

# 12. Règle fondamentale ring buffer — normative

## 12.1 Règle stricte

Décision finale :

- `read_pos` et `write_pos` sont **toujours exprimés en frames stéréo**
- l’index mémoire se calcule par :
  - `idx = frame_index * channels`
- ici :
  - `channels = 2`

## 12.2 Pourquoi c’est vital

Cette règle évite :

- erreurs de vitesse,
- erreurs de pitch,
- underruns fantômes,
- décalages d’unités,
- bugs silencieux très difficiles à diagnostiquer.

## 12.3 Règle pratique

Une frame =

- L + R

Donc :

```c
read_pos++;
write_pos++;
idx = read_pos * 2;
```

Jamais :

```c
read_pos += 2;
```

sauf si l’on saute volontairement des frames, ce qui n’est **pas** le design cible.

## 12.4 Action recommandée

Ajouter en debug :

- assertions de cohérence,
- compteurs de fill,
- vérification `used <= capacity - 1`.

---

# 13. Transition ATTACK → STREAM

## 13.1 Contrat retenu

La transition doit être **sample-accurate** et sans clic.

Décision finale :

Au trigger :

1. la voice démarre immédiatement en **ATTACK**,
2. le streamer démarre sur :
   - `data_offset + attack_frames * bytes_per_frame`
3. le scheduler remplit le ring pendant l’attaque.

À la fin de l’attaque :

- si le ring est prêt :
  - bascule directe en `STREAM`
- sinon :
  - fallback très court sans accès disque en IRQ

## 13.2 Fallback autorisé

Fallback minimal accepté :

- maintien du dernier sample,
- ou micro-fade très court,
- ou silence très bref piloté par politique explicite,

mais **jamais** de lecture disque dans l’IRQ.

## 13.3 Décision finale

Le design principal doit viser :

- **bascule directe sans fallback dans le cas normal**,
- fallback seulement comme sécurité.

---

# 14. Cadence réelle de la superloop

## 14.1 Point important

Le streaming dépend de la fréquence d’appel de :

- `stream_manager_process()`

Cette fonction tourne en superloop et partage le CPU avec :

- USB,
- MIDI,
- UI,
- autres tâches non IRQ.

## 14.2 Décision retenue

- appeler le streaming **tôt** dans la boucle,
- instrumentation obligatoire du `dt` entre appels,
- watchdog de starvation recommandé.

## 14.3 Cadence cible

Objectif pratique :

- `stream_manager_process()` appelé au moins toutes les **1 à 2 ms** en régime normal.

## 14.4 Recommandation d’ordonnancement

Exemple recommandé :

```text
while(1)
{
    stream_manager_process();
    engine_tasklet_poll();
    brick6_app_process_non_stream();
    usb/midi/ui;
}
```

L’idée est simple :

- **streaming avant UI**.

---

# 15. Débit SD — estimation finale

## 15.1 Payload audio brut

Pour une voice stéréo 48 kHz 24-bit :

- `48000 * 6 bytes = 288000 bytes/s`
- soit ≈ `281 KB/s`

Pour 24 voices :

- `288000 * 24 = 6.9 MB/s`

## 15.2 Décision finale

Ce chiffre est correct comme **pire cas payload brut**.

Avec overhead FATFS + marge produit :

- viser **>= 10 MB/s utile**
- recommandé **>= 12 MB/s utile**
- idéalement davantage si disponible

## 15.3 Faisabilité H743

Avec **SDMMC 4-bit** sur H743 et une bonne carte SD :

- **oui**, c’est réaliste.

## 15.4 Ce qu’il faudra vraiment mesurer

Mesures de qualification :

- débit utile moyen,
- latence `f_read` max,
- P95 / P99 latence,
- underruns par minute à 24 voices.

---

# 16. Estimation mémoire finale

## 16.1 Attack caches

- `576 samples`
- `960 frames`
- `2 canaux`
- `4 bytes`

=> ≈ `4.22 MB`

## 16.2 Ring buffers

Pour `24 streamers`, ring `4096 frames`, stéréo float :

- `4096 * 2 * 4 = 32768 bytes` par streamer
- ≈ `32 KB`
- × 24 = ≈ `768 KB`

## 16.3 Metadata

- ≈ `46 à 55 KB`

## 16.4 Voices + streamers + états

- coût faible comparé aux caches et rings,
- négligeable à l’échelle de la SDRAM.

## 16.5 Total

Total ordre de grandeur :

- ≈ `5.0 à 5.3 MB`

## 16.6 Conclusion

Avec une **SDRAM 32 MB**, le design est **largement viable**.

---

# 17. Coût CPU du mixer 24 voices

## 17.1 Calcul simple

- `24 voices`
- `64 frames`
- `2 canaux`

=> `24 * 64 * 2 = 3072 contributions` par bloc

## 17.2 Évaluation

Pour un STM32H743 FPU :

- charge de mixage pure **modérée**
- largement faisable

## 17.3 Point de vigilance réel

Le vrai coût CPU viendra surtout de :

- accès SDRAM,
- pression cache,
- FX déjà présents,
- branchements ATTACK/STREAM.

## 17.4 Décision finale

Le mix 24 voices **n’est pas un problème structurel** pour H743.

Ordre de grandeur réaliste :

- ~`5–12 % CPU` pour le mix voix seul selon implémentation mémoire.

---

# 18. Phases de migration retenues

## Phase 0 — instrumentation / sécurité
- mesurer `f_read` max / P99,
- mesurer cadence superloop,
- clarifier les unités de ring,
- harmoniser les commentaires bloc 64.

## Phase 1 — sample_pool
- créer `sample_pool[576]`
- indexation WAV
- metadata seulement

## Phase 2 — attack cache
- précharger ~20 ms par sample
- stockage SDRAM

## Phase 3 — voice manager
- pool fixe 24 voices
- trigger immédiat en ATTACK

## Phase 4 — multi-streamers
- `streamers[24]`
- ring individuel
- `FIL` persistant

## Phase 5 — stream manager réel
- scheduler min-fill
- un refill à la fois
- instrumentation

## Phase 6 — intégration DSP
- lecture ATTACK / STREAM
- mix multi-voice
- transition sample-accurate

## Phase 7 — robustesse / qualification
- tests 24 voices
- cartes lentes
- fragmentation
- UI/USB/MIDI actifs
- objectif : 0 underrun sur durée de validation choisie

---

# 19. Risques résiduels

Même avec l’architecture finale, les risques suivants restent à surveiller :

1. **latence extrême de certaines cartes SD**
2. **fragmentation FAT excessive**
3. **superloop ponctuellement bloquée par USB/UI**
4. **unités frames/samples réintroduites incorrectement**
5. **pression cache/SDRAM avec FX lourds**
6. **limites FATFS si configuration handles inadéquate**

Aucun de ces risques n’invalide l’architecture ; ils nécessitent simplement
de l’instrumentation et de la qualification.

---

# 20. Conclusion finale

## Design final validé

Le design retenu est :

- `sample_pool[576]`
- attack cache RAM pour chaque sample
- `voices[24]`
- `streamers[24]`
- scheduler **min-fill** en superloop
- **handles FATFS persistants**
- chunk nominal **1024 frames**
- ring buffer **4096 frames**
- positions de ring en **frames stéréo uniquement**
- **aucune I/O disque dans l’IRQ audio**
- transition **ATTACK → STREAM** sample-accurate

## Évaluation globale

Cette architecture est :

- réaliste,
- robuste,
- compatible avec un STM32H743,
- cohérente avec ce qui se fait dans les samplers hardware modernes.

## Verdict

**Architecture prête pour implémentation.**

---

# 21. Fichiers clés à modifier en priorité

1. `Src/Storage/audio_streamer.c`
2. `Src/Streaming/stream_manager.c`
3. `Src/Audio/sampler.c` ou futur voice manager
4. `Src/Core/brick6_app_init.c`
5. `Src/Storage/wav_parser.c`
6. `Src/Storage/wav_loader.c`
7. `Src/Audio/audio_float.c` uniquement pour intégration minimale côté DSP
8. `Inc/.../memory_layout.h` selon placement SDRAM

---

# 22. Notes d’implémentation

## 22.1 Ce qu’il ne faut pas faire

- faire des `f_read` dans l’IRQ audio
- faire des allocations dynamiques dans le DSP
- mélanger frames et samples dans les rings
- ouvrir/fermer les fichiers à chaque refill
- surcharger le scheduler inutilement

## 22.2 Ce qu’il faut faire

- tout préallouer
- instrumenter très tôt
- mesurer la SD réelle, pas seulement le débit théorique
- garder le chemin temps réel simple
- faire évoluer l’existant au lieu de le réécrire