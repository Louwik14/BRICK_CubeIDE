# Audit DSP MonoB

## Chemin réel d'une note MonoB dans l'IRQ audio

1. `HAL_SAI_RxHalfCpltCallback()` / `HAL_SAI_RxCpltCallback()` ouvrent la fenêtre de mesure CPU, puis appellent `process_half()`. Le budget hard realtime est un bloc fixe de 64 frames à 48 kHz, soit ~1,33 ms par demi-buffer.  
2. `process_half()` appelle `audio_process_block_int32()` sur le demi-buffer DMA courant.  
3. `audio_process_block_int32()` dépile jusqu'à 8 événements contrôle, appelle `sd_recorder_audio_block_begin()`, puis exécute successivement `audio_io_unpack()`, `audio_dsp_process()` et `audio_io_pack()`.  
4. `audio_dsp_process()` ne fait aucun DSP lui-même: il délègue au callback applicatif `my_dsp()` via `dsp_engine_process_block()`.  
5. `my_dsp()` sélectionne le moteur synth runtime. Si la track 3 est active, il calcule un buffer mono `synth_mono[]` avec `monob_synth_process_block()` quand le type courant est `MonoB`, puis duplique ce buffer en `tracks[3].L/R`.  
6. `my_dsp()` traite ensuite la track 0 avec `voice_manager_process()`, applique un sanitize `isfinite`, pousse un tap recorder `TRACK_RAW`, puis appelle `mixer_process()`.  
7. `mixer_process()` balaie toutes les tracks actives, applique filtre de track + inserts + fader/pan + sends + routing, pousse trois taps recorder par track, somme vers `bus_main` / `bus_cue`, traite les send FX actifs, puis recopie `bus_main` vers `tracks[0]` et `bus_cue` vers `tracks[1]`.  
8. De retour dans `my_dsp()`, un tap `MASTER` est capturé, `live_recorder_write()` est appelé, puis `live_recorder_read()` et un crossfade live/rec sont appliqués à `tracks[0]`. Avec `xfade = 0`, cette étape reste traversée même si la branche recorder est neutre audio.  
9. `audio_io_pack()` reconvertit `tracks[0]` et `tracks[1]` vers le TDM TX int24.  

## Coût réellement imputable à MonoB

### 1. Coût moteur oscillateurs

Le coeur oscillateur `monob_osc_bank_process()` est déjà compact:

- bail-out immédiat si aucun oscillateur n'est actif ;
- incréments de phase recalculés uniquement quand la fréquence de base change ;
- mélange limité à la liste compacte `active_indices[]` ;
- formes d'onde très simples, sans table ni interpolation coûteuse.

Conclusion: le bank osc n'est plus le principal suspect. Son coût augmente surtout avec le nombre d'oscillateurs actifs, mais il ne justifie pas à lui seul une impression de "gros CPU" sur une note mono.  

### 2. Coût runtime synth autour du moteur

`monob_synth_process_block()` ajoute autour du bank osc:

- enveloppe d'amplitude calculée par sample ;
- garde `base_frequency_hz > 0 && amp_env > 0` avant appel osc ;
- filtre ladder optionnel, avec:
  - enveloppe filtre calculée par sample ;
  - cutoff recalculé par sample ;
  - `fx_filter_ladder_moog_set_cutoff()` appelé par sample ;
  - `fx_filter_ladder_moog_process_sample()` appelé par sample.

Lecture audit:

- **filtre OFF**: le coût MonoB spécifique reste surtout `amp_env + osc_bank + écriture mono`. C'est raisonnable.  
- **filtre ON**: le coût bascule rapidement côté filtre, pas côté oscillateurs. Le ladder + update cutoff sample par sample est clairement le poste le plus cher du moteur MonoB.  

### 3. Coût copie / mix / routing ajouté par l'intégration MonoB

Une note MonoB ne sort pas directement au DAC. Elle paie aussi:

- la copie mono -> stéréo `tracks[3].L/R` dans `my_dsp()` ;
- le passage complet de la track 3 dans `mixer_process()` ;
- les boucles gain/pan sur la track 3 ;
- les boucles de routing vers `bus_main` / `bus_cue` ;
- éventuellement les inserts track 3 si un slot est assigné ;
- éventuellement les sends track 3 si un send FX est actif ;
- trois appels `sd_recorder_capture_tap_block()` pour la track 3.

Par défaut, inserts et sends track 3 sont à zéro / `-1`, donc ils ne portent pas le coût principal. En revanche, la duplication mono->stéréo puis le passage dans le mixer sont des coûts structurels systématiques dès que la track 3 est active.  

### 4. Coût structurel du pipeline, souvent perçu à tort comme "coût MonoB"

Même si MonoB joue seul, l'IRQ continue aussi à payer:

- `audio_io_unpack()` sur les entrées tracks 0..2 ;
- `voice_manager_process()` sur la track 0 ;
- sanitize `isfinite` sur la track 0 ;
- insert track 0 par défaut (`mixer_set_track_insert_slot(0U, 0U, 2)`) donc compresseur Daisy sur la track 0 ;
- zéroing des bus mixer et des send buffers à chaque bloc ;
- `live_recorder_write()` ;
- `live_recorder_read()` + crossfade live/rec même avec `xfade = 0` ;
- `audio_io_pack()` pour la sortie ;
- `sd_recorder_audio_block_begin()` à chaque bloc.

Ce point est central: une partie visible du CPU "quand MonoB joue" n'est pas du tout le moteur MonoB, mais le coût fixe de la pipeline audio actuelle, toujours traversée dans l'IRQ.  

## Sections coûteuses par ordre pratique

### A. Coûts les plus susceptibles de dominer

1. **Filtre ladder MonoB quand activé**  
   Le filtre est le poste MonoB le plus cher restant, surtout parce que cutoff + process sont refaits à chaque sample.  

2. **Pipeline structurelle hors MonoB**  
   Voice manager track 0, compresseur sur track 0, live recorder, pack/unpack, mixer et taps recorder restent payés même si l'utilisateur écoute surtout MonoB.  

3. **Mixer de la track 3**  
   Ce n'est pas énorme comparé au filtre, mais c'est un coût systématique: copie mono->stéréo, fader/pan, routing, taps.  

### B. Coûts présents mais secondaires

4. **Osc bank MonoB**  
   Coût réel, mais désormais proportionné et déjà bien resserré.  

5. **Recorder taps**  
   Les appels sont très souvent des sorties rapides hors état `RECORDING`, donc coût faible mais non nul.  

6. **Conversion int24 <-> float**  
   Coût structurel modéré, pas spécifique à MonoB.  

## Travail inutile ou évitable identifié

### 1. Zéro-fill de la track 3 dans `audio_io_unpack()`

La track 3 est une source interne. `audio_io_unpack()` la remettait à zéro à chaque bloc alors qu'elle n'est jamais alimentée par le TDM entrant et que `my_dsp()` la réécrit quand la track synth est active.  

=> **Optimisation locale appliquée**: suppression du `memset` systématique de `tracks[3].L/R` dans `audio_io_unpack()`.  

Gain attendu:

- petit mais réel ;
- payé à chaque IRQ ;
- sans risque fonctionnel sur le chemin MonoB, puisque la track 3 n'est pas une entrée TDM.  

### 2. Live recorder neutralisé mais quand même traversé

Dans `my_dsp()`:

- `xfade` vaut littéralement `0.0f` ;
- `gain_rec = sinf(0) = 0` ;
- `gain_live = cosf(0) = 1`.

Le bloc fait pourtant encore:

- `live_recorder_read()` ;
- une boucle de mix par sample ;
- deux appels trigonométriques par bloc.

C'est du coût structurel neutre audio. Le gain potentiel existe, mais la modification toucherait le pipeline global, pas seulement MonoB. À recommander si l'objectif devient l'allègement global de l'IRQ.  

### 3. Voice manager / track 0 toujours traversés

`my_dsp()` traite toujours la track 0 si elle est activée, indépendamment du fait que l'utilisateur cherche à évaluer MonoB. Cela inclut le sampler et le compresseur insert track 0 par défaut.  

Ce n'est pas un bug MonoB: c'est un coût de configuration/runtime global.  

### 4. Taps recorder toujours appelés

Les taps track/master sont invoqués même hors enregistrement. Le garde-fou dans `sd_recorder_capture_tap_block()` fait sortir rapidement si l'état n'est pas `RECORDING`, donc le coût reste faible. Ce n'est pas prioritaire.  

## Optimisations restantes pertinentes

### Priorité 1 — ne pas sur-optimiser l'osc bank

Ne pas réouvrir de gros chantier sur `monob_osc_bank`: le retour sur risque devient faible. Le coeur est déjà propre et compact.  

### Priorité 2 — cibler le filtre MonoB

Optimisations encore utiles et localisées:

1. **Éviter `fx_filter_ladder_moog_set_cutoff()` à chaque sample quand la cutoff modulée est constante**  
   Cas évident: `filter_eg_amount == 0`, ou bloc où l'enveloppe est constante.  
   Gain attendu: bon, sans redesign.  

2. **Évaluer une mise à jour cutoff par bloc ou par sous-bloc**  
   À valider à l'écoute, mais c'est l'axe le plus crédible si le filtre reste le point chaud principal.  

3. **Mesurer séparément filtre ON vs OFF**  
   Si l'écart CPU observé en réel suit surtout ce switch, alors la cause est bien le ladder et non les oscillateurs.  

### Priorité 3 — réduire le coût structurel imputé à MonoB

Si l'objectif est l'empreinte CPU "perçue MonoB" et non seulement le coeur synth:

1. **court-circuiter le live-recorder crossfade neutre (`xfade == 0`)** ;  
2. **désactiver / contourner les traitements track 0 non nécessaires dans le preset d'audit** ;  
3. **ne pas faire tourner des inserts/sends track 3 tant qu'ils sont neutres**.  

Ces gains peuvent être visibles, mais ils relèvent d'un assainissement pipeline global plus que d'une optimisation du moteur MonoB lui-même.  

## Sections déjà propres / à ne pas toucher en priorité

- `dsp_engine_process_block()`: simple dispatch, coût négligeable.  
- `process_half()` / callbacks DMA: structure minimale et correcte.  
- `monob_osc_bank_process()`: déjà simplifié, actif uniquement sur les oscillateurs réellement utilisés.  
- `sd_recorder_capture_tap_block()`: bail-out rapide hors enregistrement.  
- gestion inserts/sends du mixer quand les slots restent inactifs: coût quasi nul hors boucle de test.  

## Conclusion décisionnelle

### Verdict synthèse

- **Non**, le CPU perçu comme "coût MonoB" ne vient pas seulement du moteur oscillateurs.  
- **Oui**, une partie importante vient du **pipeline complet payé dans l'IRQ**: mixer, track 3 stéréo, track 0 sampler/comp, recorder/live-recorder, pack/unpack.  
- **Oui**, le **filtre MonoB** reste le plus gros poste spécifiquement attribuable au moteur quand il est activé.  
- **Oui**, le **bank osc est déjà propre** et on approche du maximum raisonnable sans redesign lourd.  

### Décision pratique

- S'il faut encore gagner sur **MonoB lui-même**, il faut viser **le filtre**, pas repartir sur l'osc bank.  
- S'il faut gagner sur le **CPU perçu en situation réelle**, il faut aussi traiter **le coût structurel de pipeline** actuellement imputé à tort à MonoB.  
- En l'état, le chemin n'est **pas sale**, mais il n'est **pas encore isolé**: une note MonoB paie encore une quantité non négligeable de DSP global qui n'est pas intrinsèquement MonoB.  
