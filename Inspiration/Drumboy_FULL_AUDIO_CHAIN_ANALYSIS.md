# Drumboy Full Audio Chain Analysis

## Resume court

Constat code Drumboy: le chemin audio principal Drumboy est un pipeline sample-par-sample, global, majoritairement mono jusqu'a la reverb. Les 10 layers lisent des samples RAM convertis en mono 24-bit signe, sont sommes en `int32_t`, passent par LPF global, Param EQ global, Filter 1, Filter 2, Effect 1, Effect 2, puis Reverb. La stereo reelle apparait uniquement en sortie de reverb via un delay "surround" droite; le signal gauche reste le signal reverb courant. La sortie finale est envoyee en I2S 32-bit frame / 24-bit codec vers SGTL5000.

Interpretation: l'architecture est simple et performable pour une drum machine mono globale, mais elle ne fournit ni headroom garanti, ni sommation bornee, ni vraie stereo par layer/FX. Les controles dry/wet peuvent additionner dry + wet jusqu'a 2.0 selon les blocs.

Recommandation BRICK6: reutiliser les idees de tables de parametres discretes, double-buffer RAM par layer/sample, transitions de mix, et routage serie explicite; eviter la sommation mono globale non bornee, les allocations SDRAM adressees en dur, les fonctions math lourdes par sample, et les effets globaux sans autorite track-aware.

## Schema texte du flux reel

```text
Timer play
  -> Controller::interruptPlay()
  -> arme LayerPlayData pour 10 layers

I2S TX complete IRQ, a chaque sample stereo DMA:
  -> Controller::interruptAudioSend()
     ecrit le sample precedent dans dac.i2s_data[0..3]
  -> Controller::interruptAudioMetronome()
  -> Controller::interruptAudioSong()
     10 layers mono RAM 24-bit -> somme mono int32 -> system.volumeFloat
  -> Controller::interruptAudioLpf()
     LPF global mono optionnel
  -> Controller::interruptAudioEq()
     Param EQ global mono: low shelf -> high shelf -> 4 peaks
  -> Controller::interruptAudioFilter()
     Filter 1 mono -> Filter 2 mono
  -> Controller::interruptAudioEffect()
     Effect 1 mono -> Effect 2 mono, limit alert/auto volume
  -> Controller::interruptAudioReverb()
     mono input << 8 -> reverb mono -> L direct, R surround delay
```

Note d'ordre: `interruptAudioSend()` est appele avant le calcul du nouveau sample dans `HAL_I2S_TxCpltCallback()` (`Core/Src/main.cpp`). Le DMA transmet le contenu courant de `dac.i2s_data`, puis les callbacks calculent le prochain sample.

## Tableau global par bloc

| Bloc | Source exacte | Mono/stereo reel | Format numerique | Ordre | Gain/mix/limites |
| --- | --- | --- | --- | --- | --- |
| Layers | `Controller::interruptPlay`, `Controller::interruptAudioSong`; `Global.h` `Layer`, `LayerPlayData` | Mono | Samples SDRAM 24-bit signe dans 3 octets; calcul `int32_t` + `float` multipliers | 10 layers sommes avant FX | `0.80 * layer volume^2 * beat volume^2 * norm`, puis `system.volumeFloat`; pas de clip par layer ni bus |
| LPF | `Controller::interruptAudioLpf`; `Global.h` `Lpf` | Mono | `int32_t` + coefficients `float` | Avant Param EQ | dry/wet fixe du LPF; incertain cote produit car hors schema demande mais present dans code |
| Param EQ | `Controller::interruptAudioEq`; `Global.h` `Eq` | Mono | `int32_t` states + coefficients `float` | Apres LPF, avant filters | 6 biquads serie; bypass par gain zero; transition active dry/wet |
| Filter 1 | `Controller::processAudioFilter(0)` | Mono | `int32_t` states + coefficients `float` | Serie, avant Filter 2 | dry + wet tables 0..1; slope 6 dB ajoute demi dry |
| Filter 2 | `Controller::processAudioFilter(1)` | Mono | Idem | Serie, avant FX | Idem |
| Effect 1 | `Controller::processAudioEffect(0)`; `Global.h` `Effect` | Mono | `int32_t`, `float`, buffers mono | Serie, avant Effect 2 | dry/wet 0..1; transitions; certains effets limitent localement |
| Effect 2 | `Controller::processAudioEffect(1)` | Mono | Idem | Serie, avant reverb | Slots independants par etat et buffers; memes algos |
| Reverb | `Controller::interruptAudioReverb`; `Global.h` `Reverb` | Entree mono, sortie pseudo-stereo | Entree `audioEffect << 8`; buffers `int32_t` | Dernier bloc avant sortie | dry/wet 0..1, inputData * 0.015, L direct, R delay surround |
| Output | `Controller::interruptAudioSend`, `Dac::initialize`, `MX_I2S3_Init` | Stereo I2S | 2 x 32-bit split en 4 demi-mots `uint16_t`; SGTL5000 24-bit | Sortie finale | applique `volumeLeftFloat`/`volumeRightFloat`; pas de clamp avant cast |

## Details par bloc

### 1. Generation et mix des 10 layers

Constat code Drumboy:
- Declenchement temporel: `Controller::interruptPlay()` parcourt `kLayerLibrarySize == 10` layers. Quand un `BeatMicro.interval` egale `playInterval`, il arme `sD.layerData[layer.number].beatData`.
- Source audio: `LayerPlayData::beatData.ramAddress` pointe vers `kRamLayerAddressLibrary[layer.number][layer.playSampleSector]`.
- Banque RAM: `Global.h` reserve 10 layers x 2 secteurs de `kSampleSize = 240000` samples, `kSampleByteSize = kSampleSize * 3`, soit environ 720 kB par secteur, 1.44 MB par layer, 14.4 MB pour 10 layers.
- Chargement sample: `Controller::layerInst_setSampleLoaded()` accepte WAV PCM/float 8/16/24/32-bit, mono ou stereo, plusieurs frequences. Le fichier est converti hors audio en mono 24-bit SDRAM.
- Stereo source: les WAV stereo sont acceptes (`nbrChannels == 2`) mais le chargement utilise `offsetSize = coefSamplerate * coefChannel * coefBps` et lit un seul sample par frame, sans sommation L/R visible. Le canal conserve semble le premier interleave. Toute stereo de source est donc perdue dans le chemin prouve.
- Resampling: pour < 44100 Hz, duplication simple (`writeCount` 2/3/4/6). Pour >= 44100 Hz, decimation par `offsetSize` avec `coefSamplerate` 1/2/4. Pas d'interpolation au chargement.
- Lecture runtime: `Controller::interruptAudioSong()` lit `sdram_read24BitAudio()` avec interpolation lineaire quand `counter` est fractionnaire, applique `volumeMultiplier`, puis ajoute `audioSong += audioLayer`.
- Reverse/speed: `counter` avance ou recule de `kLayerSpeedDataLibrary[speed].increment` de 0.5 a 2.0.

Mono/stereo reel:
- Layers runtime: mono.
- Pas de pan par layer prouve.
- Pas de bus stereo avant reverb.

Niveau et bornage:
- `volumeMultiplier = kLayerVolumeCoef * layer volume * beat volume`, ou avec normalisation `* normMultiplier`.
- Les volumes utilisent `kFloatDataLibrary[x].pow2Multiplier`; definition exacte non relue dans cette passe, donc courbe exacte incertaine, mais les appels prouvent une courbe non lineaire par pas de 5%.
- Normalisation: `normMultiplier = 8388607.0f / limitData` apres scan min/max du sample charge. Si `limitData == 0`, protection non vue: risque de division par zero ou valeur non finie, incertain selon donnees d'entree.
- Sommation: `audioSong += audioLayer` sur 10 layers, puis `audioSong *= system.volumeFloat`. Aucun clamp/saturateur avant LPF/EQ.
- Limiteur: seulement apres Effect 2, si `audioEffect` depasse `INT24_MAX/MIN`, `limitAlertShowFlag = true` et `system_setVolume(system.volume - 1)` si limiter actif. Ce n'est pas un limiteur sample-accurate; c'est un auto-trim de volume.

CPU/memoire:
- Runtime sample: jusqu'a 10 lectures SDRAM 24-bit par sample, interpolation possible: 2 lectures + multiplications par layer actif.
- Memoire forte mais simple: samples precharges en SDRAM, pas de lecture SD dans IRQ audio.

### 2. LPF global hors schema demande

Constat code Drumboy:
- `Controller::interruptAudioLpf()` est appele entre `interruptAudioSong()` et `interruptAudioEq()`.
- `Global.h` `Lpf` calcule un biquad LPF a Q 0.707, coefficients `float`, etats `int32_t dataIn/out[3]`.

Interpretation:
- Ce bloc n'etait pas dans le schema produit a verifier, mais le code le prouve dans le flux reel.
- Role probable: filtrage global simple avant EQ. Comportement UI exact incertain dans cette analyse.

### 3. Param EQ

Constat code Drumboy:
- Source: `Controller::interruptAudioEq()`, `Global.h` `Eq`.
- Type: chaine de 6 biquads mono:
  low shelf, high shelf, peak 1, peak 2, peak 3, peak 4.
- Coefficients: `Eq::calculateCoef()` utilise les formules RBJ-like avec `tan`, `pow`, `sqrt`, `fabs`; calcul hors callback audio lors des setters/initialisation.
- Plages: frequence 10 Hz..20 kHz, Q 1..8, gain -24..+12 dB, gain zero index `kEqGainZero = 24`.
- Bypass partiel: chaque shelf/peak est copie direct si son gain == zero; l'EQ complet bypass si `eq.active` false et pas de transition.

Mono/stereo reel:
- Mono uniquement: un seul etat par filtre, un seul input `audioLpf`, un seul output `audioEq`.

Niveau/headroom:
- Les boosts peuvent augmenter le niveau; pas de pre-gain ni clamp EQ.
- `EqGenTransition` peut crossfader entre input et output via `activeWet`/`activeDry`.

CPU/memoire:
- Jusqu'a 6 biquads mono par sample.
- Etats: 6 x (`dataIn[3]`, `dataOut[3]`) en `int32_t` + coefficients `float`.

### 4. Filter 1 et Filter 2

Constat code Drumboy:
- Source: `Controller::interruptAudioFilter()` appelle `processAudioFilter(0, audioEq)`, puis `processAudioFilter(1, outputA)`.
- Routing reel: serie strict, pas parallele.
- Types: `FIL_LPF`, `FIL_HPF`, `FIL_BPF`, `FIL_BSF`; `FIL_OFF` existe dans enum, mais min type UI vaut 1.
- Coefficients: biquad mono avec `tan`, Q depuis `kFilterResDataLibrary` 0.7..5.5, frequence 10 Hz..20 kHz.
- Mix: `data = filtered * wetFloat + input * dryFloat`. Si `slope == 0`, `data = data/2 + input/2`, ce qui donne une pente plus douce par melange avec dry plutot qu'un vrai changement d'ordre.

Mono/stereo reel:
- Mono, un etat par slot.

Niveau/headroom:
- Dry et wet viennent de tables 0..1 par pas de 0.05. Rien n'impose dry + wet <= 1 dans le process.
- `limitMix = true` et `limitMixData = 30` existent dans `Filter`, mais la preuve de contrainte est dans les setters/transitions, pas dans le process. Le mix runtime ne clippe pas.

CPU/memoire:
- 2 biquads mono par sample si actifs.
- Cout faible et borne; coefficients calcules hors audio.

### 5. Effect 1 et Effect 2

Constat code Drumboy:
- Source: `Controller::interruptAudioEffect()` appelle `processAudioEffect(0, audioFilter)`, puis `processAudioEffect(1, outputA)`.
- Routing reel: serie strict.
- Slots: deux instances `Effect effect[2]`. Chaque slot possede ses propres structures d'etat, `flangerBuffer`, `delayAddress`, `chorusAddress`.
- Buffers SDRAM separes: `RAM_DELAY_0/1` de 96000 samples `int32_t`, `RAM_CHORUS_0/1` de 24000 samples alloues en adresse SDRAM mais le process chorus utilise `kChorusBufferSize = 5000`.
- Effets disponibles: Delay, Chorus, Flanger, Phaser, Compressor, Expander, Overdrive, Distortion, Bitcrusher.
- Dry/wet: la plupart utilisent `data = wetSignal * effect_.wetFloat + input * effect_.dryFloat`; compressor/expander utilisent `eMix` comme wet et dry derive dans setters, a confirmer dans `effect_setEData`.

Mono/stereo reel par effet:
- Delay: mono.
- Chorus: mono, malgre deux taps modules internes; les deux taps sont sommes en un seul `dataChorus`.
- Flanger: mono.
- Phaser: mono.
- Compressor: mono.
- Expander: mono.
- Overdrive: mono.
- Distortion: mono.
- Bitcrusher: mono.

Niveau/headroom:
- Delay: `playData = input + *playPtr`, puis record `input * level + delayed * feedback`; pas de saturation.
- Chorus/Flanger: feedback ecrit dans buffer; pas de clamp.
- Phaser: pas de clamp.
- Compressor/Expander: dB calcule via `log10(abs(input/INT24_MAX))`; si input vaut 0, `log10(0)` produit -inf puis est clamp par comparaison? En C/C++, la comparaison avec -inf fonctionne, mais le chemin exact reste a considerer fragile.
- Overdrive: normalise par `INT24_MAX`, applique gain, waveshaper cubique, clippe `out_double` a [-1,1], puis filtre tone LPF.
- Distortion: normalise, gain, `atanf`, threshold, filtre tone LPF; pas de clamp final explicite apres filtre.
- Bitcrusher: sample-hold, limite clip/fold selon seuil, masque resolution 24-bit. Le mode par defaut est fold. Le `while` de fold est non borne par iteration fixe.
- Alerte de niveau globale: apres Effect 2 seulement, test `audioEffect >= INT24_MAX || audioEffect <= INT24_MIN`, puis auto-baisse du volume systeme si limiter actif.

CPU/memoire:
- Delay: cout faible, mais SDRAM externe par sample.
- Chorus: deux lectures interpolees SDRAM + modulation par sample.
- Flanger: petit buffer local 250 samples, interpolation.
- Phaser: couteux, recalcule `sin`, `cos` et coefficients par sample.
- Compressor/Expander: couteux, `log10` + `pow` par sample.
- Overdrive/Distortion: cout moyen/eleve, waveshaper + biquad; distortion utilise `atanf`.
- Bitcrusher: cout general faible mais `while` de fold non borne.

### 6. Reverb

Constat code Drumboy:
- Source: `Controller::interruptAudioReverb()`, `Global.h` `Reverb`.
- Entree: `int32_t input = (audioEffect << 8)`. Le pipeline passe de 24-bit nominal a 32-bit avant reverb/sortie.
- Pre-delay: buffer mono `preDelayBuffer[4500]`.
- Tank: 8 comb filters paralleles avec damping (`combDecay1/2`) puis 4 allpass en serie.
- Reduction d'entree: `inputData = reverbInput * 0.015`.
- Mix: `data = reverb.apass4Out * wetFloat + input * dryFloat`.
- Stereo: `output_L = audioReverb`; `output_R = surroundBuffer[surround_playInterval]`. Le surround buffer recoit `audioReverb`, avec retard 0..20 ms.
- Si reverb inactive: `output_L = input`, `output_R = input`.

Mono/stereo reel:
- Entree et tank mono.
- Sortie pseudo-stereo par retard droite, pas vraie reverb stereo decorrelee par double tank.

Niveau/headroom:
- Le scaling 0.015 reduit fortement l'entree du tank.
- Le dry reste en 32-bit apres `<< 8`.
- Pas de clamp avant sortie I2S.

Memoire:
- Pre-delay: 4500 x 4 = 18 kB.
- Surround: 900 x 4 = 3.6 kB.
- Comb: (1116+1188+1277+1356+1422+1491+1557+1617) x 4 = environ 48 kB.
- Allpass: (225+556+441+341) x 4 = environ 6.3 kB.
- Total reverb buffers: environ 76 kB, hors etats scalaires.

### 7. Sortie finale DAC/audio

Constat code Drumboy:
- `Controller::interruptAudioSend()` additionne le metronome a gauche/droite: `(audioReverb_L/R + audioMetronome) * system.volumeLeftFloat/rightFloat`.
- Les samples 32-bit sont split en deux `uint16_t` par canal et places dans `dac.i2s_data[0..3]`.
- `Dac::initialize()` lance `HAL_I2S_Transmit_DMA(&hi2s3, (uint16_t*)i2s_data, 4)`.
- `MX_I2S3_Init()` configure I2S3 master TX, Philips, 32-bit data frame, audio freq 44K, alignement 24-bit right.
- SGTL5000: `I2S_CTRL_REG` DLEN configure a 24 bits; DAP active dans `Dac::initialize()`, mais EQ/bass/surround codec sont des fonctions non activees dans le flux audio logiciel analyse.

Niveau/headroom:
- `system.volumeFloat` global est applique avant LPF/EQ; `volumeLeftFloat/rightFloat` sont reappliques en sortie selon pan. A pan centre, les deux valent 1.0.
- Pas de saturation explicite avant cast `uint16_t`; tout depassement repose sur representation binaire envoyee au codec.

## Tableau mono/stereo reel

| Element | Reel | Preuve | Commentaire |
| --- | --- | --- | --- |
| Samples charges | Convertis mono | `layerInst_setSampleLoaded()` lit une seule valeur par frame interleave | Stereo WAV accepte mais non conserve comme stereo |
| 10 layers | Mono | `audioLayer`, `audioSong` scalaires | Pas de pan layer |
| Metronome | Mono puis ajoute L/R | `audioMetronome` scalaire ajoute aux deux sorties | Apres reverb |
| LPF | Mono | `audioLpf` scalaire | Bloc hors schema demande |
| Param EQ | Mono | `audioEq` scalaire | 6 biquads serie |
| Filter 1/2 | Mono | `processAudioFilter` scalaire | Serie |
| Effect 1/2 | Mono | `processAudioEffect` scalaire | Serie; chorus/flanger ont modulation mono |
| Reverb tank | Mono | un seul jeu comb/allpass | Pas de double tank stereo |
| Reverb output | Pseudo-stereo | L direct, R surround delay | Stereo creee par delay droite |
| Output I2S | Stereo | `dac.i2s_data[0..3]` | Deux canaux SGTL5000 |

## Gestion des niveaux et limites

Constat code Drumboy:
- Echelle nominale: `INT24_MAX = 8388607`, `INT24_MIN = -8388608`.
- Samples RAM: stockage 24-bit signe dans 3 octets.
- Layers: volume layer et volume microstep multiplies avant somme; coefficient global layer `0.80`.
- Volume systeme: table quadratique `0.0..1.0`, appliquee a `audioSong` avant LPF/EQ.
- Pan sortie: table qui maintient un cote a 1.0 et attenue l'autre; a centre L/R = 1.0.
- Param EQ, filters et FX peuvent augmenter le niveau sans reserve automatique.
- Reverb reduit son entree wet par `0.015`, mais son dry reste a l'echelle 32-bit apres shift.
- Limiteur: detection apres FX, auto baisse du volume systeme, pas un limiter audio instantane.

Interpretation:
- Le vrai headroom est empirique: volume initial 75%, layer coef 0.80, volume systeme initial 75% -> `system.volumeFloat = 0.5625`. Cela aide, mais ne garantit pas l'absence d'overflow si plusieurs layers normalises, EQ boosts, dry/wet additifs et feedback sont combines.
- Les conversions `int32_t` apres multiplications float peuvent depasser 24-bit avant la detection.

## Gestion dry/wet/send

Constat code Drumboy:
- Il n'y a pas de send reverb separe prouve: la reverb est en insert global final sur tout `audioEffect`.
- Filters: dry/wet par slot, serie.
- Effects: dry/wet par slot, serie.
- Reverb: dry/wet final, puis sortie pseudo-stereo.
- Transitions: `interruptTransition()` ajuste progressivement dry/wet avec pas `0.0001` pour systeme, filters, effects, reverb.

Interpretation:
- Le schema est "insert chain globale", pas "mixer avec sends".
- Dry/wet peut servir de crossfade, mais sans normalisation constante-power ni contrainte stricte dans le process.

## Choix CPU/memoire

Avantages CPU/memoire:
- Tout le sample audio layer est precharge en SDRAM; pas de SD dans IRQ audio.
- Pas de graphe dynamique dans le callback; ordre fixe.
- Slots FX fixes et etats prealloues.
- Coefficients EQ/filter/tone sont recalcules hors sample pour la plupart des blocs.

Risques CPU/memoire:
- Pipeline entier sample-par-sample dans callback I2S TX complete, pas en bloc.
- Plusieurs appels `float`/math lourds dans le chemin sample selon FX: `sin`, `cos`, `log10`, `pow`, `atanf`.
- Acces SDRAM par sample pour layers, delay, chorus.
- Adresses SDRAM hardcodees.
- Bitcrusher fold utilise une boucle `while` non bornee par compteur fixe.

## Avantages de l'architecture Drumboy

- Flux serie tres lisible et facile a raisonner: layer mix -> EQ -> filters -> FX -> reverb -> output.
- Deux slots FX vraiment separes par etat et buffers.
- Tables de parametres discretes adaptees a une UI hardware.
- Prechargement RAM des samples et conversion au format runtime unique.
- Double secteur par layer pour charger/remplacer sans ecraser immediatement le secteur joue.
- Transitions de dry/wet explicites pour eviter certains clics lors des changements.
- Reverb globale compacte et preallouee.

## Inconvenients / risques

- Mono global jusqu'a la sortie reverb; les samples stereo et FX stereo ne sont pas conserves.
- Pas de pan par layer, pas de bus par layer, pas de sends.
- Headroom non garanti; dry/wet additif et boosts EQ peuvent depasser.
- "Limiter" non audio-rate: il baisse le volume apres detection mais ne clippe/limite pas le sample fautif.
- Fonctions math couteuses dans certains FX par sample.
- Bitcrusher fold non borne.
- Adresses SDRAM fixes et couplage fort au layout memoire.
- Reverb pseudo-stereo par delay, pas vraie stereo.
- Tout est global; pas de separation track-aware comparable a BRICK6.

## Idees reutilisables pour BRICK6

Constat inspire du code:
- Tables discretes de parametres: utiles pour UI hardware, serialization, limites et labels stables.
- Slots FX prealloues avec etat explicite par slot.
- Transitions dry/wet et active/type en crossfade.
- Chargement sample hors IRQ vers format runtime unique.
- Double-buffer/secteur par sample/layer.
- Reverb preallouee sans allocation runtime.

Recommandation BRICK6:
- Reprendre l'approche table-driven, mais avec autorite track-aware et mapping logique/physique explicite.
- Garder le prechargement/cache RAM et le format runtime clair, mais conserver stereo quand la source/track le demande.
- Porter uniquement les DSP dont le cout worst-case est borne; precalculer toutes les fonctions math possibles hors IRQ.
- Transformer les dry/wet en loi bornee ou constant-power selon usage.
- Ajouter saturation/clip explicite par frontiere de format, avec metering clair.

## Idees a eviter pour BRICK6

- Ne pas reprendre un bus mono global unique.
- Ne pas perdre silencieusement la stereo des samples.
- Ne pas utiliser des adresses SDRAM hardcodees comme API.
- Ne pas mettre `pow/log/sin/cos/atan` par sample dans le chemin hard-RT sans budget et alternative.
- Ne pas utiliser de boucle `while` non bornee dans un FX IRQ.
- Ne pas confondre "alert + baisse volume" avec un vrai limiteur.
- Ne pas ajouter des effets globaux qui contournent `track_runtime`, `mixer`, ou les autorites BRICK6 existantes.

## Zones incertaines a relire

- `kFloatDataLibrary`: courbe exacte de `pow2Multiplier` utilisee pour layer/beat volume; les appels sont prouves, la table n'a pas ete detaillee ici.
- Setters `effect_setDData/effect_setEData`, `filter_setDry/Wet`, `reverb_setDry/Wet`: a relire pour prouver toutes les contraintes `limitMix` hors process.
- Comportement complet du LPF global dans l'UI produit: bloc prouve dans le flux, role produit exact incertain.
- DAP SGTL5000: `Dac::initialize()` active le DAP et selectionne les routes; l'impact exact du DAP actif sans EQ/bass/surround logiciellement actives reste incertain sans datasheet/config complete.
- Stereo WAV: le code prouve qu'une seule valeur interleave est lue par frame; le canal exact conserve depend de l'interleave WAV standard et des casts, donc "probablement gauche" mais marque incertain.
- Robustesse normalisation sur sample silencieux: absence de garde vue autour de `8388607.0f / limitData`, mais effet exact depend des donnees et du comportement float/cast ensuite.
