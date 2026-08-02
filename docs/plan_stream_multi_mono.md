# Plan d’action — Stream/Multi mono natif

## 0. Règles d’exécution de ce document

Ce document est découpé en étapes indépendantes. Un agent qui reçoit :

```text
Lis docs/plan_stream_multi_mono.md et exécute uniquement l’étape N.
```

doit exécuter uniquement l’étape demandée, puis s’arrêter après son retour standard. Il ne doit pas commencer l’étape suivante automatiquement.

Chaque étape validée produit un commit local dédié, sans push.

Builds autorisés :

```text
Release Low-Cost
Release Premium
```

Le chantier ne demande aucun build `TestPremium`.

Le présent document est un plan. Aucun code, test, instrumentation ou commit n’est réalisé par sa création.

---

## 1. Verdict à HEAD

Le chantier est faisable et justifié avec un seul pool physique statique de pages de 16 Kio.

Architecture retenue :

```text
slot physique fixe de 16 Kio
├── mono    : 4096 floats, 4096 frames
└── stéréo  : 4096 floats, 2048 frames L/R interleaved
```

Contrat produit définitif :

```text
présocle mono    : 2 pages
présocle stéréo  : 4 pages
fenêtre mono     : 2 pages
fenêtre stéréo   : 4 pages
```

À 48 kHz et vitesse 1×, mono et stéréo couvrent donc environ 8192 frames, soit 170,67 ms.

Le WAV mono ne gagnera pas de débit SD brut : la source contient déjà un seul canal. Le gain vient de la suppression de la duplication FLOAT32 L/R, de la diminution des lectures SDRAM du reader, de la diminution des pages réservées pour une même durée et de la réduction du travail de gestion de pages par seconde.

Le socle cache, refill et reader est commun. Les intégrations Stream et Multi sont séparées. Le Multi est homogène : un instrument est entièrement mono ou entièrement stéréo.

### Décisions importantes

- Un instrument Multi mixte mono/stéréo est refusé à l’import ou à l’ajout de zone.
- Aucune conversion automatique de sample à chaque lecture audio.
- Aucun filtre ou VCA individuel par voix Multi dans ce chantier.
- Chaque voix Multi conserve néanmoins son reader, son play plan, son format et son état de lecture propres.
- Le format d’une voix active est immuable. Une nouvelle sélection UI ne modifie que les prochains déclenchements.
- Les WAV stéréo conservent leur chemin actuel.
- Le Sampler RAM mono terminé n’est pas modifié.
- Le Looper et les autres utilisateurs stéréo du page cache restent stéréo.

---

## 2. Audit vérifié à HEAD

### 2.1 Géométrie actuelle

Les constantes actuelles sont dans `Inc/Sampler/sample_page_cache_config.h` :

```c
SAMPLE_PAGE_FRAMES              2048
SAMPLE_PAGE_CHANNELS            2
SAMPLE_PAGE_FRAME_STRIDE_FLOATS 2
SAMPLE_PAGE_BYTES_PER_FRAME     8
SAMPLE_PAGE_BYTES               16384
```

Le tableau physique est actuellement déclaré dans `Src/Sampler/sample_page_cache.c` comme `2048 × 2 floats` par slot. Cette mémoire fait déjà 16 Kio ; elle devra être exprimée en capacité physique de floats, sans augmenter la RAM.

Les hypothèses dispersées à éliminer ou rendre explicites sont notamment :

- `frame / SAMPLE_PAGE_FRAMES` dans `sample_cache.c`, `sample_play_plan.c`, `sample_stream_manager.c`, `sample_voice_reader.c` et `sample_page_cache.c` ;
- `page * SAMPLE_PAGE_FRAMES` ;
- `frame * SAMPLE_PAGE_FRAME_STRIDE_FLOATS` ;
- pointeurs `base + 1` pour le canal droit ;
- `block_align` confondu avec le stride FLOAT32 ;
- `SAMPLE_PAGE_MIN_READY_PAGES` calculé une fois pour 8192 frames ;
- fenêtre Multi fixe de 4 pages ;
- fenêtre voix classique fixe de 4 pages ;
- buffers L/R obligatoires dans `brick6_audio_runtime.c` ;
- scratch de conversion dimensionné implicitement comme une page stéréo ;
- codec mono écrivant deux floats identiques.

Les consommateurs partagés détectés à HEAD sont :

- Stream : `sample_cache.c`, `sample_page_cache.c`, `sample_stream_manager.c`, `sample_stream_backend_contiguous.c`, `sample_play_plan.c`, `sample_voice_reader.c` ;
- Multi : `multi_sample_loader.c`, `multi_sample_index.c`, `multi_sample_import.c`, `multi_sample_pool.c`, `brick6_sampler_runtime.c` ;
- Looper stéréo : `brick6_looper_runtime.c`, avec enregistrement raw PCM24 stéréo explicite ;
- autres calculs de coût ou de préparation : `sample_pool.c`, `project_v1.c`, `ui_page_settings.c`.

Les constantes stéréo historiques ne doivent pas être supprimées brutalement tant que ces consommateurs ne sont pas soit migrés vers un helper de format, soit explicitement conservés en stéréo.

### 2.2 Stream classique actuel

Chemin HEAD :

```text
WAV parser / wav_info_t
→ sample_cache_desc_t
→ présocle classic
→ sample_stream_manager
→ backend contigu ou lecteur FatFs persistant
→ scratch PCM
→ wav_audio_codec
→ page READY
→ sample_cache
→ sample_voice_reader
→ brick6_sampler_runtime
→ buffers L/R
→ mixer_submit_external_stereo
```

Symboles principaux :

- `sample_page_cache_register_stream_sample_key()` ;
- `sample_stream_manager_request_range_key_alloc()` ;
- `sample_stream_manager_page_deadline_frames()` ;
- `sample_cache_prepare()` et les fonctions `sample_cache_*` de fenêtre ;
- `sample_voice_reader_begin_segment()` et `sample_voice_reader_commit_segment()` ;
- `sample_voice_reader_mix_fwd_1x()` ;
- `sample_voice_reader_mix_rev_1x()` ;
- `sample_voice_reader_mix_pitch_fwd_linear()` ;
- `sample_voice_reader_mix_pitch_rev_linear()` ;
- `brick6_sampler_runtime_render_stream_track()` ;
- `brick6_audio_runtime.c::brick6_render_sampler_tracks()`.

Le codec mono actuel dans `Src/Storage/wav_audio_codec.c` convertit une valeur PCM puis l’écrit dans `dst[0]` et `dst[1]`. Le cache conserve donc deux floats par frame même pour un WAV mono.

Le chemin stéréo doit rester inchangé fonctionnellement : même page de 2048 frames, même interleaving, même reader stéréo, mêmes buffers et même mixer.

### 2.3 Multi actuel

Chemin HEAD :

```text
index Multi
→ multi_sample_index_apply_to_pool()
→ multi_loader_calc_prep_budget()
→ multi_sample_pool
→ register_stream_sample_key()
→ pages initiales demandées et pinnées
→ sample_stream_manager
→ résolution note/vélocité
→ play plan
→ voix globale
→ fenêtre voix et fenêtre loop
→ sample_voice_reader
→ brick6_sampler_render_multi()
→ accumulation L/R
→ mixer stéréo
```

Symboles principaux :

- `multi_sample_index_validate()` ;
- `multi_sample_import.c` pour l’import WAV et les zones ;
- `multi_loader_sample_required_pages()` ;
- `multi_loader_calc_prep_budget()` ;
- `multi_sample_pool_resolve_source()` ;
- `brick6_sampler_runtime_multi_prefetch_voice()` ;
- `brick6_sampler_runtime_multi_release_voice_pages()` ;
- `brick6_sampler_render_multi()` ;
- `brick6_sampler_runtime_render_multi_track()`.

Le Multi actuel accepte séparément des samples mono et stéréo et les fait passer par une représentation page stéréo. Le nouveau contrat supprimera cette ambiguïté au niveau de l’instrument : toutes les zones d’un instrument devront partager le même format interne mono/stéréo.

Le Multi conserve ses propriétaires, générations et fenêtres propres. Le nouveau rendu mono ne doit pas fusionner l’état des voices avant le reader : l’accumulation n’intervient qu’après le rendu de chaque voix.

### 2.4 Préchargement, fenêtres et deadlines actuels

À HEAD :

- `SAMPLE_PREP_MIN_READY_FRAMES = 8192` ;
- Classic : 4 pages de 2048 frames ;
- Multi : 4 pages de fenêtre ;
- lookahead : fenêtre moins une page ;
- `SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES` réserve deux fenêtres de quatre pages par voix ;
- Multi calcule ses pages avec `SAMPLE_PAGE_FRAMES` ;
- les deadlines Stream calculent les frontières à partir de `page_index * 2048` et de `(page_index + 1) * 2048` ;
- les demandes coalescées sont actuellement indexées par key et numéro de page, sans snapshot explicite de format.

Le futur code devra garder 8192 frames comme durée contractuelle, mais dériver le nombre de pages :

```text
pages(format, 8192) = ceil(8192 / frames_per_page(format))
mono   = 2
stéréo = 4
```

Pour le pool de fenêtres voix, conserver une réservation physique statique au pire cas stéréo. Chaque propriétaire mono n’utilisera que deux pages de sa partition fixe. Il ne faut pas récupérer dynamiquement les deux pages restantes selon l’état chaud du système.

### 2.5 Mixer actuel

Le mixer possède déjà :

- `mixer_begin_external_mono_native()` ;
- `mixer_commit_external_mono_native()` ;
- `mixer_submit_external_mono_native()` ;
- `mixer_track_filter_process_block_mono()` ;
- `mixer_lane_run_mono_native_path()`.

Le chemin mono-native est autorisé pour les filtres de piste compatibles et pour `FX_SAT`. Les autres inserts forcent la promotion stéréo. L’ordre musical des inserts n’est pas modifié.

`brick6_audio_runtime.c` utilise actuellement `sampler_tmp_l` et `sampler_tmp_r` pour Stream et Multi. Le raccordement futur doit choisir le format au niveau du bloc :

- source entièrement mono et piste compatible : accumulation mono ;
- source stéréo ou Multi stéréo : chemin stéréo actuel ;
- Multi mono : aucune accumulation L/R intermédiaire ;
- aucun Multi mixte mono/stéréo n’est autorisé.

---

## 3. Architecture cible

### 3.1 Type de format commun

Créer un contrat commun Stream/Multi, distinct du contrat RAM si cela évite un couplage inutile. Le type doit au minimum distinguer :

```c
FLOAT32_MONO
FLOAT32_STEREO_INTERLEAVED
```

Les helpers uniques sont obligatoires :

- `frames_per_page(format)` ;
- `stride_floats(format)` ;
- `bytes_per_float_frame(format)` ;
- `page_index_from_frame(format, frame)` ;
- `page_start_frame(format, page)` ;
- `required_page_count(format, frame_count)` ;
- `presocle_pages(format)` ;
- `window_pages(format)`.

Les helpers utilisent des intermédiaires 64 bits et ne mélangent jamais `wav_info.block_align` avec le stride FLOAT32.

### 3.2 Page physique et descripteur

Chaque page conserve :

- key ;
- sample ID ;
- page index ;
- format ;
- start frame ;
- frame count logique ;
- stride ;
- pointeur sur le slot physique ;
- état ;
- pins ;
- use count ;
- window pins ;
- génération ;
- last touch.

Le cache utilise un seul pool et une seule politique d’éviction. Une page mono et une page stéréo peuvent occuper des slots voisins sans distinction de pool.

Le format doit être copié dans `pending`, `sample_page_load_target_t`, les spans et les références ou être validé contre le descripteur immuable du sample. Une référence ancienne doit être refusée si sa génération, son key, son page index ou son format ne correspondent plus.

### 3.3 Identité de page

Le `sample_audio_key_t` actuel reste une identité d’objet (`domain + object_id`) si le format est immuable pendant la vie du sample.

Le format est cependant obligatoire dans le contrat d’identité de page :

```text
(key, page_index, generation, format)
```

Si un key peut être réenregistré alors qu’un pending ou une référence ancienne existe encore, il faut ajouter un epoch de registration ou intégrer le format dans la clé effective. Il est interdit de compter uniquement sur le numéro de page.

### 3.4 Multi homogène

L’instrument Multi porte un format unique persisté dans ses métadonnées/index.

Choix retenu pour la première version : refus déterministe, sans conversion.

Règles :

1. Le premier sample définit le format candidat de l’instrument.
2. Toutes les zones suivantes doivent avoir le même nombre de canaux source.
3. L’index stocke explicitement le format de l’instrument.
4. `multi_sample_index_validate()` vérifie le format de l’instrument et celui de chaque sample.
5. Une zone incompatible est refusée avec une erreur dédiée.
6. Aucun sample n’est converti à chaque rendu audio.
7. Le loader ne réserve aucune page avant que l’homogénéité de l’instrument soit validée.

Les anciens index peuvent être invalidés : aucune couche legacy n’est ajoutée.

### 3.5 Future insertion filtre/VCA par voix

Le chantier ne crée aucun filtre ou VCA individuel Multi.

Il doit néanmoins préserver le point d’insertion suivant :

```text
reader d’une voix
→ fade/gain de voix
→ futur filtre/VCA de voix
→ accumulation mono ou stéréo de l’instrument
```

Chaque voix conserve donc son reader, son cursor, son play plan, son format, sa génération et son dernier échantillon indépendants.

---

## 4. Étapes d’exécution

## Étape 1 — Contrat de format, géométrie et métadonnées

### But

Créer le contrat commun de format et supprimer l’ambiguïté entre format WAV source, format FLOAT32 interne et taille physique d’une page.

### Périmètre autorisé

- définitions de format Stream/Multi ;
- helpers de géométrie ;
- ajout des champs format/stride/frames-per-page dans les métadonnées ;
- cartographie des consommateurs de `SAMPLE_PAGE_FRAMES` ;
- documentation des invariants.

### Hors périmètre

- aucune conversion PCM ;
- aucun nouveau kernel ;
- aucune intégration mixer ;
- aucun changement du Sampler RAM mono ;
- aucun changement du Looper fonctionnel ;
- aucun changement UI, séquenceur ou p-lock.

### Fichiers et symboles concernés

- `Inc/Sampler/sample_page_cache_config.h` ;
- `Inc/Sampler/sample_page_cache.h` ;
- `Inc/Sampler/sample_cache.h` ;
- `Inc/Sampler/sample_stream_manager.h` ;
- `Inc/Sampler/sample_play_plan.h` ;
- `Inc/Sampler/sample_audio_key.h` ;
- `Inc/Sampler/multi_sample_pool.h` ;
- `Inc/Sampler/multi_sample_index.h` ;
- `Inc/Sampler/sample_voice_reader.h` ;
- `Inc/Core/brick6_sampler_runtime.h`.

### Modifications attendues

- Ajouter le type de format commun.
- Ajouter les helpers uniques demandés.
- Définir `SAMPLE_PAGE_BYTES = 16 Kio` comme invariant physique.
- Définir mono à 4096 frames et stéréo à 2048 frames.
- Ajouter format, stride et géométrie aux descripteurs nécessaires.
- Ajouter un format d’instrument Multi.
- Définir l’epoch de registration si le key peut être réutilisé.
- Conserver les constantes stéréo existantes comme alias explicites uniquement pour les consommateurs non migrés.
- Produire une liste statique de tous les appels restant dépendants de 2048 ou de 4 pages.

### Invariants

- aucune taille physique de slot ne change ;
- aucune allocation dynamique ;
- WAV source inchangé ;
- stéréo reste le comportement par défaut des anciens consommateurs ;
- format d’un sample enregistré immuable ;
- une voix active conserve son format ;
- RAM et Looper ne changent pas musicalement.

### Validations ciblées

- recherche statique de `SAMPLE_PAGE_FRAMES`, `* 2`, `stride 2`, `8U`, `4U` dans les modules concernés ;
- vérification des tailles avec assertions compile-time ;
- vérification que les champs de format sont présents dans sample, page, pending, target, play plan, reader, ref et voice ;
- revue des consommateurs Looper/RAM pour confirmer leur maintien stéréo.

### Builds

- Release Low-Cost ;
- Release Premium.

### Critère de réussite

Le contrat est centralisé, documenté et compilable sans modifier le comportement stéréo. Toute géométrie future peut être obtenue par un helper de format unique.

### Retour attendu de l’agent

```text
1. Verdict: PASS/FAIL + résumé du contrat ajouté
2. Patch: fichiers et symboles modifiés
3. Tests/builds: validations et résultats Release Low-Cost/Premium
4. Mémoire ou comportement: tailles de structures et invariants stéréo
5. Dette restante: occurrences intentionnelles de constantes stéréo
6. Commit: hash et message local, sans push
```

### Commit

```text
sampler: define Stream Multi sample format contract
```

## Étape 2 — Cache et calculs de pages dépendants du format

### But

Faire coexister mono et stéréo dans le même cache physique avec les bons numéros de page, bornes, pages partielles, pins, générations, LRU et fenêtres.

### Périmètre autorisé

- cache Stream/Multi ;
- spans et références ;
- play plan ;
- requêtes de plages ;
- préfetch et calculs de fenêtres ;
- deadlines dérivées des frontières de page.

### Hors périmètre

- décodage PCM ;
- kernels de rendu ;
- mixer ;
- modification du Looper ou du RAM résident ;
- réduction opportuniste de la réserve physique ;
- warm-up ou allocation dynamique.

### Fichiers et symboles concernés

- `Src/Sampler/sample_page_cache.c` ;
- `Src/Sampler/sample_play_plan.c` ;
- `Src/Sampler/sample_cache.c` ;
- `Src/Sampler/sample_stream_manager.c` ;
- `Src/Sampler/sample_stream_backend_contiguous.c` pour la cible de page ;
- `Src/Sampler/sample_pool.c` pour les calculs explicitement stéréo ;
- `Src/Core/brick6_looper_runtime.c` pour les appels à conserver stéréo ;
- `Src/Storage/project_v1.c` et `Src/UI/pages/ui_page_settings.c` uniquement si leurs calculs concernent le page cache Stream/Multi.

Symboles prioritaires :

- `g_sample_page_data` ;
- `sample_page_cache_stream_page_frame_count_key()` ;
- `sample_page_cache_try_acquire_page_key()` ;
- `sample_page_cache_begin_read_block_key()` ;
- `sample_play_plan_frames_to_page_span()` ;
- `sample_stream_manager_page_deadline_frames()` ;
- `sample_stream_manager_request_range_key_alloc()` ;
- `sample_cache_try_acquire_span()` ;
- `sample_cache_inspect_voice_block()`.

### Modifications attendues

- Dimensionner le tableau statique selon `SAMPLE_PAGE_BYTES / sizeof(float)` par slot.
- Remplacer les calculs de page par les helpers de format.
- Adapter `start_frame`, `frame_count`, `page_offset` et les bornes reverse.
- Ajouter la validation du format dans les page refs et load targets.
- Conserver les états READY/PENDING/ERROR, pins, `use_count`, `window_pin_count`, génération et LRU.
- Calculer le présocle avec 2 pages mono et 4 pages stéréo.
- Calculer les fenêtres voix avec 2 pages mono et 4 pages stéréo.
- Garder une partition physique statique au pire cas stéréo pour les fenêtres réservées par voix.
- Adapter les deadlines pitchées aux frontières 4096 ou 2048 frames.
- Conserver les demandes coalescées par key/page, avec contrôle de format et de génération.
- Vérifier que les pages partagées par plusieurs voix incrémentent et libèrent correctement les pins.

### Invariants

- page physique toujours 16 Kio ;
- mono page jamais interprétée comme stéréo ;
- stéréo toujours 2048 frames interleaved ;
- aucun slot partiellement partagé entre deux pages logiques ;
- aucune éviction d’une page pinnée ou d’une génération active ;
- aucun changement de calcul pour les samples explicitement stéréo ;
- durée de présocle et de fenêtre proche de 8192 frames ;
- les pages voisines et les limites reverse restent sûres.

### Validations ciblées

- samples mono/stéréo de 1, 2047, 2048, 2049, 4095, 4096 et 4097 frames ;
- première et dernière page ;
- début de sample non aligné ;
- reverse sur frontière ;
- boucle et ping-pong sur frontière ;
- deux voices partageant le même sample ;
- page mono et page stéréo dans des slots voisins ;
- éviction après libération des pins ;
- pending ancien après réinscription d’un key ;
- vérification des budgets de slot, marge et fenêtres.

### Builds

- Release Low-Cost ;
- Release Premium.

### Critère de réussite

Les pages mono couvrent 4096 frames, les pages stéréo 2048 frames, et aucun chemin Stream/Multi ne calcule une frontière avec une constante globale non justifiée.

### Retour attendu de l’agent

```text
1. Verdict: PASS/FAIL + état de la géométrie mono/stéréo
2. Patch: fichiers, helpers et consommateurs migrés
3. Tests/builds: frontières, partage, éviction, Release Low-Cost/Premium
4. Mémoire ou comportement: taille physique du pool et réserves de fenêtres
5. Dette restante: consommateurs explicitement stéréo
6. Commit: hash et message local, sans push
```

### Commit

```text
sampler: make Stream Multi page geometry format aware
```

## Étape 3 — Conversion WAV et refill mono

### But

Convertir le PCM mono en une seule valeur FLOAT32 par frame et remplir une page mono de 4096 floats sans duplication L/R.

### Périmètre autorisé

- codec PCM16/PCM24/PCM32 déjà supporté ;
- backend contigu ;
- fallback FatFs ;
- scratch PCM ;
- publication READY ;
- offset et consommation source.

### Hors périmètre

- changement du format des WAV sur SD ;
- raw PCM24 Looper, qui reste stéréo ;
- interpolation ;
- reader ;
- mixer ;
- changement de débit ou de stratégie SD physique.

### Fichiers et symboles concernés

- `Src/Storage/wav_audio_codec.c` ;
- `Inc/Storage/wav_audio_codec.h` ;
- `Src/Sampler/sample_page_cache.c` ;
- `Src/Sampler/sample_stream_manager.c` ;
- `Src/Sampler/sample_stream_backend_contiguous.c` ;
- `Inc/Sampler/sample_page_cache.h`.

Symboles prioritaires :

- `wav_audio_codec_select_pcm_decode_block()` ;
- décodeurs PCM16/24/32 mono et stéréo ;
- `sample_page_cache_decode_page()` ;
- `sample_stream_manager_decode_wav_page()` ;
- `sample_stream_manager_decode_raw_pcm24_page()` ;
- `sample_stream_backend_decode_pcm_page()`.

### Modifications attendues

- Ajouter un contrat de décodage mono produisant un float par frame.
- Conserver les décodeurs stéréo et leur sortie interleaved inchangés.
- Écrire la destination selon le format de la cible, pas selon `SAMPLE_PAGE_FRAME_STRIDE_FLOATS` global.
- Conserver le calcul source `start_frame * wav_block_align`.
- Conserver la consommation source `frame_count * wav_block_align`.
- Vérifier que le scratch contigu couvre la plus grande charge source supportée par une page physique.
- Garder le scratch FatFs statique et ses limites de blocs.
- Ne pas modifier la publication `READY`, les erreurs, les retries ou les generations.
- Garder le raw PCM24 explicitement stéréo.

### Invariants

- PCM16 mono : une conversion et un store par frame ;
- PCM24 mono : une conversion et un store par frame ;
- PCM stéréo : deux valeurs et même interleaving qu’avant ;
- `block_align` source reste la seule unité de lecture fichier ;
- aucune lecture hors bloc ou frame partielle ;
- page partielle correctement bornée ;
- aucune page n’est publiée READY avant conversion complète.

### Validations ciblées

- WAV PCM16 mono/stéréo ;
- WAV PCM24 mono/stéréo ;
- dernière page partielle ;
- offsets non alignés secteur ;
- backend contigu ;
- fallback FatFs avec une, deux et plusieurs lectures scratch ;
- erreur de lecture ;
- validation des bytes source, bytes FLOAT32 écrits et frame count ;
- recherche statique des écritures `dst[1]` dans les chemins mono.

### Builds

- Release Low-Cost ;
- Release Premium.

### Critère de réussite

Une page mono READY contient uniquement 4096 samples FLOAT32 mono au maximum. Les WAV stéréo produisent exactement la même représentation et les mêmes offsets qu’avant.

### Retour attendu de l’agent

```text
1. Verdict: PASS/FAIL + conversion mono/stéréo
2. Patch: codec, refill et scratch modifiés
3. Tests/builds: PCM16/24, FatFs, contigu, Release Low-Cost/Premium
4. Mémoire ou comportement: bytes source, floats écrits, page finale
5. Dette restante: raw stéréo, interpolation et mesures matérielles
6. Commit: hash et message local, sans push
```

### Commit

```text
sampler: decode streamed mono pages without channel duplication
```

## Étape 4 — Reader et kernels mono

### But

Lire les pages mono sans fabriquer de canal droit et préserver exactement les positions, boucles, reverse, ping-pong, underruns et fades actuels.

### Périmètre autorisé

- cursor de page ;
- segments audio ;
- kernels mono ;
- dispatch par format ;
- rendu individuel d’une voice ;
- changement de page et réacquisition.

### Hors périmètre

- vraie interpolation ;
- suppression générale des pages voisines ;
- filtre/VCA par voix Multi ;
- mixer ;
- changement de comportement musical.

### Fichiers et symboles concernés

- `Inc/Sampler/sample_voice_reader.h` ;
- `Src/Sampler/sample_voice_reader.c` ;
- `Inc/Sampler/sample_cache.h` ;
- `Src/Sampler/sample_cache.c` ;
- `Inc/Core/brick6_sampler_runtime.h` ;
- `Src/Core/brick6_sampler_runtime.c`.

Symboles prioritaires :

- `sample_voice_reader_begin_segment()` ;
- `sample_voice_reader_commit_segment()` ;
- `sample_voice_reader_acquire_audio_page()` ;
- `sample_voice_reader_prepare_pitch_forward_segment()` ;
- `sample_voice_reader_prepare_pitch_reverse_segment()` ;
- les quatre kernels `sample_voice_reader_mix_*` ;
- `brick6_sampler_render_sample()` ;
- `brick6_sampler_render_multi()`.

### Modifications attendues

- Ajouter les variantes mono forward 1×, reverse 1×, pitch forward et pitch reverse.
- Adapter les pointeurs de segment et le stride au format capturé.
- Faire le dispatch mono/stéréo avant la boucle par frame.
- Réutiliser la logique de commit pour loop, ping-pong, reverse et changement de page.
- Conserver les générations sur les références courante et voisine.
- Produire une sortie mono individuelle pour une voice mono avant accumulation Multi.
- Conserver un point d’insertion documenté après gain/fade et avant accumulation pour le futur filtre/VCA par voix.
- Conserver les tails stéréo à cette étape si cela réduit le risque.
- Laisser la dette d’acquisition voisine séparée, sauf blocage technique.

### Invariants

- aucune interpolation nouvelle ;
- même position Q16 et même progression de step ;
- mêmes bornes de début/fin ;
- mêmes transitions loop/ping-pong ;
- même statut `NOT_READY`, `UNDERRUN` et `DONE` ;
- une voice active ne change pas de format ;
- une page voisine ne peut pas être consommée avec une génération périmée.

### Validations ciblées

- forward 1× mono/stéréo ;
- pitch inférieur et supérieur à 1 ;
- reverse ;
- loop forward ;
- ping-pong ;
- changement de direction ;
- frontières 2048/4096 ;
- dernière page partielle ;
- sample d’une ou deux frames ;
- underrun pendant changement de page ;
- voice stop et voice steal.

### Builds

- Release Low-Cost ;
- Release Premium.

### Critère de réussite

Le mono produit les mêmes valeurs musicales que l’ancien mono L/R dupliqué, sans lecture du canal droit et sans branchement de format dans la boucle par frame.

### Retour attendu de l’agent

```text
1. Verdict: PASS/FAIL + kernels et modes couverts
2. Patch: reader, cursor, segments et runtime modifiés
3. Tests/builds: modes, frontières, underrun, Release Low-Cost/Premium
4. Mémoire ou comportement: buffers de sortie et invariants de position
5. Dette restante: pages voisines, interpolation, filtre/VCA par voix
6. Commit: hash et message local, sans push
```

### Commit

```text
sampler: add native mono Stream Multi reader kernels
```

## Étape 5 — Raccordement au mixer mono-native

### But

Acheminer une sortie mono Stream/Multi vers le chemin mono-native déjà validé du mixer, sans modifier l’ordre musical des inserts.

### Périmètre autorisé

- sélection du format de rendu par bloc ;
- accumulation mono ;
- suppression des copies L/R inutiles pour les sources mono ;
- raccordement `mixer_begin_external_mono_native()` et commit associé ;
- promotion stéréo contrôlée si nécessaire.

### Hors périmètre

- modification de l’ordre des inserts ;
- nouveaux effets mono ;
- filtre ou VCA par voix Multi ;
- correction du contrat VCA Stream existant ;
- changement du pan musical.

### Fichiers et symboles concernés

- `Src/Core/brick6_audio_runtime.c` ;
- `Inc/Audio/mixer.h` ;
- `Src/Audio/mixer.c` uniquement si une adaptation d’API est nécessaire ;
- `Src/Audio/fx_chain.c` et `Inc/Audio/fx_chain.h` uniquement pour vérifier la compatibilité existante.

Symboles prioritaires :

- `brick6_render_sampler_tracks()` ;
- `mixer_begin_external_mono_native()` ;
- `mixer_commit_external_mono_native()` ;
- `mixer_submit_external_mono_native()` ;
- `mixer_lane_run_mono_native_path()` ;
- `mixer_track_supports_mono_native_path()`.

### Modifications attendues

- Ajouter une décision de format au niveau de la piste et du bloc.
- Stream mono compatible : rendre vers le buffer mono du mixer.
- Stream stéréo : conserver les buffers L/R actuels.
- Multi mono homogène : accumuler les voices mono dans un buffer mono.
- Multi stéréo : conserver l’accumulation stéréo actuelle.
- Si le mixer ne peut pas rester mono-native à cause d’un insert, promouvoir explicitement au point existant.
- Garder les buffers statiques et déterministes.
- Ne pas introduire de buffer par voice ni d’allocation dynamique.

### Invariants

- l’ordre filter/VCA/pan/inserts du mixer ne change pas ;
- `FX_SAT` reste compatible selon le contrat actuel ;
- tout insert incompatible provoque une promotion contrôlée ;
- les sources stéréo ne passent jamais par le chemin mono ;
- aucun Multi mixte n’existe ;
- le futur point filtre/VCA par voix reste avant accumulation.

### Validations ciblées

- Stream mono avec mixer mono-native ;
- Stream stéréo avec chemin stéréo ;
- Multi mono ;
- Multi stéréo ;
- filtre de piste OFF/EQ3/biquad ;
- insert SAT ;
- insert stéréo incompatible ;
- pan et VCA ;
- confirmation de l’absence de `sampler_tmp_l/r` sur le chemin mono.

### Builds

- Release Low-Cost ;
- Release Premium.

### Critère de réussite

Un rendu mono compatible atteint `mixer_begin_external_mono_native()` et le mixer effectue une seule chaîne mono jusqu’au point de promotion prévu.

### Retour attendu de l’agent

```text
1. Verdict: PASS/FAIL + chemin mixer utilisé
2. Patch: runtime/mixer modifiés
3. Tests/builds: mono, stéréo, inserts, pan, Release Low-Cost/Premium
4. Mémoire ou comportement: buffers L/R conservés ou supprimés
5. Dette restante: filtre/VCA par voix et VCA Stream existant
6. Commit: hash et message local, sans push
```

### Commit

```text
sampler: route native mono sources through mixer
```

## Étape 6 — Intégration du Stream classique

### But

Faire fonctionner le chemin complet WAV mono → page mono → reader mono → accumulation mono → mixer mono-native, tout en conservant le Stream stéréo.

### Périmètre autorisé

- métadonnées Stream ;
- préparation classic ;
- cache complet et cache paginé ;
- pending/priorités/deadlines ;
- rendu Stream ;
- intégration audio runtime.

### Hors périmètre

- Multi ;
- index et zones Multi ;
- filtre/VCA par voix ;
- interpolation ;
- refonte du scheduler ;
- changement UI ou séquenceur.

### Fichiers et symboles concernés

- `Inc/Sampler/sample_cache.h` ;
- `Src/Sampler/sample_cache.c` ;
- `Inc/Sampler/sample_page_cache.h` ;
- `Src/Sampler/sample_page_cache.c` ;
- `Inc/Sampler/sample_stream_manager.h` ;
- `Src/Sampler/sample_stream_manager.c` ;
- `Inc/Core/brick6_sampler_runtime.h` ;
- `Src/Core/brick6_sampler_runtime.c` ;
- `Src/Core/brick6_audio_runtime.c`.

Symboles prioritaires :

- `sample_cache_prepare()` ;
- `sample_cache_try_acquire_span()` ;
- `sample_cache_inspect_voice_block()` ;
- `sample_cache_read_voice()` ;
- `brick6_sampler_runtime_render_stream_track()` ;
- `brick6_sampler_runtime_render_track()` ;
- `brick6_render_sampler_tracks()`.

### Modifications attendues

- Capturer le format interne lors de l’enregistrement Stream.
- Adapter le cache complet aux frames logiques mono sans dupliquer L/R.
- Utiliser 2 pages mono ou 4 pages stéréo pour le présocle.
- Utiliser 2 pages mono ou 4 pages stéréo pour la fenêtre active.
- Adapter les lookahead et deadlines au nombre réel de frames/page.
- Ajouter le format au play plan et à la voice runtime.
- Rendre mono dans un buffer mono et stéréo dans les buffers actuels.
- Garder la sélection UI indépendante des voices déjà actives.

### Invariants

- un sample Stream enregistré ne change pas de format ;
- un nouveau sample sélectionné ne modifie pas une voice déjà active ;
- mono et stéréo ont environ 8192 frames de sécurité à vitesse 1× ;
- le WAV stéréo reste strictement sur son ancien chemin ;
- les pins, owners, générations et requêtes coalescées restent sûrs ;
- aucun warm-up n’est requis.

### Validations ciblées

- cold start mono ;
- cold start stéréo ;
- pitch inférieur et supérieur à 1 ;
- reverse ;
- loop ;
- ping-pong ;
- sample court ;
- changement de sample UI pendant une voice active ;
- voice stop, steal et tail ;
- page eviction et pending ancien ;
- vérification de la durée de présocle et de fenêtre ;
- comparaison audio mono avec l’ancien mono L/R dupliqué.

### Builds

- Release Low-Cost ;
- Release Premium.

### Critère de réussite

Le Stream mono suit entièrement le chemin mono natif. Le Stream stéréo produit le même résultat et consomme le même format de page qu’avant.

### Retour attendu de l’agent

```text
1. Verdict: PASS/FAIL + état Stream mono/stéréo
2. Patch: cache, runtime, reader et audio runtime
3. Tests/builds: cold start, modes de lecture, générations, Release Low-Cost/Premium
4. Mémoire ou comportement: présocle, fenêtre, buffers et pages
5. Dette restante: Multi, interpolation, filtre/VCA par voix
6. Commit: hash et message local, sans push
```

### Commit

```text
sampler: integrate native mono classic Stream
```

## Étape 7 — Intégration du Multi homogène mono ou stéréo

### But

Faire bénéficier le Multi du socle mono tout en imposant un format unique par instrument.

### Périmètre autorisé

- index Multi ;
- import et ajout de zone ;
- pool et loader ;
- budget de présocle ;
- résolution note/vélocité ;
- voices Multi ;
- fenêtres voice/loop ;
- accumulation mono ou stéréo selon l’instrument.

### Hors périmètre

- Multi mixte mono/stéréo ;
- conversion automatique des samples ;
- filtre ou VCA individuel par voice ;
- interpolation ;
- modification du séquenceur, des p-locks ou de l’UI hors erreur/état minimal de format ;
- augmentation du présocle au-delà de 2/4 pages.

### Fichiers et symboles concernés

- `Inc/Sampler/multi_sample_index.h` ;
- `Src/Sampler/multi_sample_index.c` ;
- `Inc/Sampler/multi_sample_pool.h` ;
- `Src/Sampler/multi_sample_pool.c` ;
- `Src/Sampler/multi_sample_import.c` ;
- `Src/Sampler/multi_sample_loader.c` ;
- `Src/Core/brick6_sampler_runtime.c` ;
- `Inc/Core/brick6_sampler_runtime.h`.

Symboles prioritaires :

- `multi_sample_index_validate()` ;
- sérialisation/désérialisation de l’index ;
- `multi_sample_pool_resolve_source()` ;
- `multi_loader_calc_prep_budget()` ;
- `multi_loader_sample_required_pages()` ;
- `multi_sample_pool_add_zone()` ;
- `brick6_sampler_runtime_multi_prefetch_voice()` ;
- `brick6_sampler_runtime_multi_release_voice_pages()` ;
- `brick6_sampler_render_multi()` ;
- `brick6_sampler_runtime_render_multi_track()`.

### Modifications attendues

- Ajouter le format unique dans l’index et l’instrument.
- Invalider l’ancien index si nécessaire ; aucune couche legacy.
- Déterminer le format au premier sample et le persister.
- Refuser toute zone ou sample incompatible avant allocation/réservation.
- Ajouter une erreur explicite de mismatch de format.
- Calculer les pages de présocle avec 2 pages mono ou 4 pages stéréo.
- Calculer les fenêtres voice et loop avec 2 ou 4 pages selon l’instrument.
- Conserver owner, trigger order, génération, pins et libération différée.
- Rendre chaque voice dans son format avant l’accumulation de l’instrument.
- Accumuler uniquement en mono pour un Multi mono.
- Conserver le chemin stéréo pour un Multi stéréo.
- Garder le point d’insertion futur filtre/VCA par voice entre rendu individuel et accumulation.
- Refuser le rechargement destructif d’un instrument tant que ses voices actives utilisent encore l’ancien format, ou créer un nouvel epoch sans réutiliser l’ancien key actif.

### Invariants

- un instrument Multi est entièrement mono ou entièrement stéréo ;
- aucune voice Multi ne change de format pendant sa vie ;
- les zones d’un même instrument partagent le format de l’instrument ;
- deux voices gardent des readers et états indépendants ;
- mono utilise 2 pages de présocle et de fenêtre ;
- stéréo utilise 4 pages ;
- l’état de loop possède les mêmes garanties que l’état de voice ;
- le chemin stéréo actuel est conservé ;
- aucun traitement individuel supplémentaire n’est ajouté.

### Validations ciblées

- import d’un instrument entièrement mono ;
- import d’un instrument entièrement stéréo ;
- ajout d’une zone incompatible et refus propre ;
- ancien index invalidé proprement ;
- budget de pages avec samples courts et longs ;
- notes, vélocités, root notes et pitch ;
- plusieurs voices partageant un sample ;
- loops et fenêtres loop ;
- voice stop, steal, owner release et génération ;
- Multi mono vers mixer mono-native ;
- Multi stéréo vers mixer stéréo ;
- changement de sélection UI pendant une voice active ;
- vérification qu’aucun buffer L/R n’est créé pour l’accumulation mono.

### Builds

- Release Low-Cost ;
- Release Premium.

### Critère de réussite

Un Multi mono complet fonctionne avec deux pages de présocle et de fenêtre, un Multi stéréo fonctionne comme avant, et toute tentative de composition mixte est refusée avant chargement audio.

### Retour attendu de l’agent

```text
1. Verdict: PASS/FAIL + format d’instrument et résultat de validation homogène
2. Patch: index, import, loader, pool et runtime
3. Tests/builds: mono, stéréo, mismatch, budgets, voices, Release Low-Cost/Premium
4. Mémoire ou comportement: pages, fenêtres, buffers et accumulation
5. Dette restante: filtre/VCA par voice, interpolation, pages voisines
6. Commit: hash et message local, sans push
```

### Commit

```text
sampler: integrate homogeneous mono Multi instruments
```

## Étape 8 — Cycle de vie, validations finales, nettoyage et documentation

### But

Fermer les risques de génération, format, pins, tails et réutilisation, puis documenter le contrat réellement livré.

### Périmètre autorisé

- validations de cycle de vie ;
- assertions et diagnostics ciblés non permanents ;
- nettoyage des chemins morts ;
- documentation du contrat Stream/Multi mono ;
- rapport mémoire et comportement ;
- builds finaux.

### Hors périmètre

- instrumentation permanente ;
- optimisation SD asynchrone ;
- vraie interpolation ;
- suppression des pages voisines ;
- filtre/VCA par voice Multi ;
- modification musicale des inserts ;
- modification du scheduler, du séquenceur, des p-locks ou de l’UI non nécessaire.

### Fichiers et symboles concernés

- `Src/Sampler/sample_page_cache.c` ;
- `Src/Sampler/sample_stream_manager.c` ;
- `Src/Sampler/sample_cache.c` ;
- `Src/Sampler/sample_voice_reader.c` ;
- `Src/Core/brick6_sampler_runtime.c` ;
- `Src/Core/brick6_audio_runtime.c` ;
- `Src/Sampler/multi_sample_loader.c` ;
- `Src/Sampler/multi_sample_pool.c` ;
- `docs/plan_stream_multi_mono.md` ;
- documentation d’architecture/audit concernée, uniquement si une mise à jour est nécessaire.

### Modifications attendues

- Vérifier les chemins stop, steal, owner release et libération différée.
- Vérifier qu’un pending ancien ne peut pas remplir un slot d’un nouveau format.
- Vérifier la validation key/page/génération/epoch.
- Vérifier les tails stéréo issues d’une voice mono.
- Vérifier le changement de sample UI pendant une voice active.
- Supprimer uniquement les duplications L/R devenues mortes sur les chemins mono.
- Documenter les tailles, budgets, fenêtres, points de promotion et dettes repoussées.
- Produire un rapport de taille des structures et de pages libres.
- Ne pas ajouter d’instrumentation permanente.

### Invariants

- aucune fuite de pin ;
- aucun accès à une génération périmée ;
- aucune page mono interprétée comme stéréo ;
- aucune voice active ne change de format ;
- aucun comportement stéréo régressé ;
- aucun warm-up nécessaire ;
- aucun changement de taille physique des pages ;
- aucun ancien projet ou ancien index pris en charge par une couche legacy.

### Validations ciblées

- cold start déterministe ;
- eviction et réutilisation de slot ;
- pending annulé puis key réutilisé ;
- voice stop et voice steal pendant refill ;
- tail après changement de sélection UI ;
- plusieurs voices partageant la même page ;
- Multi loader interrompu puis relancé ;
- erreurs FatFs et timeout ;
- mono/stéréo dans des slots voisins ;
- page finale partielle ;
- Release Low-Cost et Release Premium ;
- revue finale des occurrences `2048`, `2`, `4`, `frame * 2`, `stride 2` dans Stream/Multi.

### Builds

- Release Low-Cost ;
- Release Premium.

### Critère de réussite

Le chantier mono Stream/Multi est documenté, déterministe à froid, sans régression stéréo connue, avec les dettes restantes explicitement séparées.

### Retour attendu de l’agent

```text
1. Verdict: PASS/FAIL + état final du chantier
2. Patch: nettoyage et documentation modifiés
3. Tests/builds: cycle de vie, cold start, générations, Release Low-Cost/Premium
4. Mémoire ou comportement: tailles, pages libres, chemins mono/stéréo
5. Dette restante: liste finale classée
6. Commit: hash et message local, sans push
```

### Commit

```text
sampler: validate and document Stream Multi mono lifecycle
```

---

## 5. Scénarios de validation transverses

Ces scénarios sont répartis dans les étapes précédentes et ne constituent pas une étape supplémentaire :

1. WAV PCM16 mono, sample court.
2. WAV PCM24 mono, sample dépassant 4096 frames.
3. WAV PCM16 stéréo inchangé.
4. WAV PCM24 stéréo inchangé.
5. Page mono/stéréo partielle en fin de sample.
6. Source non alignée sur une frontière secteur.
7. Forward 1×, pitch inférieur à 1 et pitch supérieur à 1.
8. Reverse, loop et ping-pong.
9. Page voisine indisponible et underrun.
10. Deux voices partageant le même sample mono.
11. Stop et voice steal pendant un pending.
12. Réutilisation d’un key avec génération différente.
13. Multi entièrement mono.
14. Multi entièrement stéréo.
15. Ajout d’une zone incompatible refusé.
16. Changement UI pendant une voice active.
17. Piste mono avec filtre compatible.
18. Piste mono avec insert incompatible.
19. Piste stéréo conservant le chemin actuel.
20. Cold start sans warm-up.

La mesure IRQ réelle, le débit physique SD et la consommation CPU matérielle restent à effectuer par l’utilisateur après validation du code. Aucun pourcentage ne doit être inventé à partir d’une lecture statique.

---

## 6. Risques explicitement suivis

- oubli d’un calcul `frame / 2048` dans un chemin reverse ou loop ;
- page mono de 4096 frames écrite dans une cible encore interprétée avec stride 2 ;
- confusion entre `wav_info.block_align` et bytes/frame FLOAT32 ;
- page mono partagée avec une voice stéréo après réutilisation de slot ;
- pending ancien sans format ou epoch ;
- budget Multi calculé avec quatre pages pour tous les formats ;
- fenêtre loop utilisant quatre pages pour une voice mono ;
- réservation physique libérée trop tôt ;
- accumulation L/R créée malgré un Multi mono homogène ;
- promotion stéréo implicite au milieu d’un bloc ;
- tail ou declick utilisant un ancien format ;
- changement UI mutating le descripteur d’une voice active ;
- régression du Looper ou du Sampler RAM à cause d’une constante partagée ;
- modification accidentelle de l’ordre musical des inserts ;
- complexité du cache supérieure au gain obtenu.

---

## 7. Dettes repoussées

- filtre et VCA indépendants par voix Multi ;
- vraie interpolation ;
- suppression ou optimisation de l’acquisition des pages voisines ;
- mono-tail natif ;
- optimisation des transactions SD ;
- mesure IRQ réelle sur matériel ;
- pool mono séparé ;
- deux pages logiques par slot ;
- allocation dynamique ;
- cache opportuniste avec warm-up ;
- changement physique des pages ;
- refonte du scheduler ;
- séquenceur, p-locks et modulation ;
- migration des anciens index/projets ;
- mélange mono/stéréo dans un même Multi ;
- modification musicale des inserts ;
- changement UI non strictement nécessaire au format Multi.

---

## 8. Format obligatoire des retours d’étape

Chaque agent doit terminer exactement avec cette structure courte :

```text
1. Verdict
   PASS ou FAIL, avec une phrase.

2. Patch
   Fichiers modifiés et résumé du changement.

3. Tests/builds
   Tests ciblés exécutés.
   Release Low-Cost: PASS/FAIL.
   Release Premium: PASS/FAIL.

4. Mémoire ou comportement
   Pages, buffers, tailles et invariants observés.

5. Dette restante
   Seulement les dettes non couvertes par l’étape.

6. Commit
   Hash local et message.
   Push: non effectué.
```

Un agent ne doit jamais déclarer l’étape suivante exécutée implicitement. Toute étape non demandée reste inchangée.
