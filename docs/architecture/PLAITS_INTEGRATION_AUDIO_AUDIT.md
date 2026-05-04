# Plaits Integration Audio Audit

> Historique: `Plaits` a ete remplace cote produit par `Opal`.
> Ce document ne decrit plus le moteur produit courant.
> Le DSP Plaits peut rester une dependance interne d'Opal.

## 1. Scope

Ce document est un audit cible sur Plaits et sur la chaine audio BRICK necessaire a son integration. Il complete `docs/architecture/NEW_DSP_ENGINE_MANUAL.md` sans le specialiser.

Cette passe est strictement documentaire:
- pas d'implementation Plaits
- pas de patch code BRICK
- pas de refonte d'architecture
- objectif: decider si Plaits v1 doit entrer par un seam mono existant, ou si la preservation de `OUT/AUX` justifie un seam local dual-output

Sources auditees:
- `docs/architecture/NEW_DSP_ENGINE_MANUAL.md`
- `docs/architecture/z1_audio_hard_rt_mix.md`
- `docs/architecture/z2_track_runtime_authority.md`
- `docs/architecture/z3_param_modulation_control.md`
- code BRICK: `Src/Core/brick6_audio_runtime.c`, `Src/Audio/mixer.c`, [`Inc/Audio/mixer.h`](/C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6/Inc/Audio/mixer.h), `Src/Core/brick6_sampler_runtime.c`, `Inc/Core/brick6_sampler_runtime.h`, `Src/Audio/audio.c`, `Src/Audio/audio_float.c`, `Src/Audio/drum_synth.cpp`, `Src/Sampler/voice_manager.c`, `Src/Seq/seq_runtime.c`, `Src/Seq/seq_play_scheduler.c`, `Src/Core/track_runtime.c`, `Src/Param/param_registry.c`, `Src/Param/param_registry_backends.c`, `Src/Param/param_registry_tone_backends.c`
- code Plaits in-tree sous `mutable_instruments/plaits/...`

## 2. Current product decisions

Decisions deja prises ou explicites dans la mission:
- Plaits reste dans la family `Synth`
- Plaits est envisage comme un nouveau `type` dans `Synth`
- Plaits reste strictement track-aware
- pas de mode global separe
- hypothese produit de base: une instance Plaits par track logique
- hypothese de depart: monophonique par track, sauf preuve code forte contraire
- `OUT` et `AUX` ne doivent pas etre assimiles automatiquement a `L/R`
- en phase proto, aucune retrocompatibilite format projet/pattern n'est requise si le format evolue ensuite

Decision structurante cote architecture:
- toute integration future doit conserver `track_runtime` comme autorite de binding
- tout apply parametre doit rester dans le chemin `param_registry`
- le rendu doit entrer par un seam audio existant ou un seam local minimal
- le mixer reste l'autorite de sommation/routing

## 3. Plaits OUT/AUX model audit

### 3.1 Common engine contract

Le contrat commun Plaits est explicite dans [`mutable_instruments/plaits/dsp/engine/engine.h`](/C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6/mutable_instruments/plaits/dsp/engine/engine.h): chaque moteur expose `Render(const EngineParameters&, float* out, float* aux, size_t size, bool* already_enveloped)`.

Constats:
- le moteur Plaits est concu autour d'une production `OUT` et `AUX` simultanee
- il n'existe pas, dans l'API commune auditée, de mode generique "OUT-only"
- supprimer `AUX` en calcul ne serait pas une optimisation gratuite: cela demanderait un audit moteur par moteur et probablement des branches specifiques

Conclusion de contrat:
- Plaits calcule conceptuellement `OUT` et `AUX` ensemble
- BRICK peut choisir de ne pas consommer `AUX`
- BRICK ne peut pas supposer que `AUX` soit un simple canal droit stereo

### 3.2 Model table

| Model | Evidence | OUT semantics | AUX semantics | Classification | `OUT/AUX` safe as `L/R`? | CPU / memory implication |
| --- | --- | --- | --- | --- | --- | --- |
| Virtual Analog | `dsp/engine/virtual_analog_engine.cc::Render` | mix principal / variante osc sync selon mode | variante sync, osc2 ou variante "monster sync" selon mode | dual-output variant | Non | Deux buffers toujours servis par l'engine |
| Waveshaping | `dsp/engine/waveshaping_engine.cc::Render` | signal plie principal | variante sinus / overtone derive | dual-output variant | Non | Deux sorties calculees |
| FM | `dsp/engine/fm_engine.cc::Render` | carrier / voix principale | sub oscillator | dual-output by-product | Non | Deux sorties calculees |
| Grain | `dsp/engine/grain_engine.cc::Render` | signal principal HP / texture finale | oscillateur `z` / composant separe | dual-output derived | Non | Deux buffers intermediaires utiles |
| Wavetable | `dsp/engine/wavetable_engine.cc::Render` | sortie principale | variante quantized / bit-reduced | dual-output variant | Non | Deux sorties calculees |
| Chord | `dsp/engine/chord_engine.cc::Render` | somme principale des voix | sous-ensemble de voix / distribution alternative | dual-output split voices | Non | Deux accumulateurs utiles |
| Speech | `dsp/engine/speech_engine.cc::Render` | composante principale selon sous-mode | composante alternative ou complementaire selon sous-mode | dual-output variant | Non | Deux sorties selon sous-mode |
| Swarm | `dsp/engine/swarm_engine.cc::Render` | accumulation principale | accumulation parallele | dual-output, role exact secondaire | Non | Deux accumulateurs utilises |
| Noise | `dsp/engine/noise_engine.cc::Render` | sortie LP/HP/multimode | somme de bandes BP | dual-output spectral split | Non | Deux sorties calculees |
| Particle | `dsp/engine/particle_engine.cc::Render` | sortie principale diffusee / filtree | accumulation separee | dual-output derived | Non | Deux buffers utiles |
| String | `dsp/engine/string_engine.cc::Render` + `dsp/physical_modelling/string_voice.cc::Render` | corde resonante principale | excitation/bruit ajoute dans `aux` | dual-output, non-stereo | Non | `aux` transporte un composant distinct |
| Modal | `dsp/engine/modal_engine.cc::Render` + `dsp/physical_modelling/modal_voice.cc::Render` | resonateur principal | excitation/mallet feed dans `aux` | dual-output, non-stereo | Non | `aux` transporte un composant distinct |
| Additive | `dsp/engine/additive_engine.cc::Render` | banque harmonique principale | banque organ / harmonics alternatifs | dual-output variant | Non | Deux banques calculees |
| Bass Drum | `dsp/engine/bass_drum_engine.cc::Render` | kick analogique | kick synthetique | dual-output two-model | Non | Deux rendus calcules |
| Snare Drum | `dsp/engine/snare_drum_engine.cc::Render` | snare analogique | snare synthetique | dual-output two-model | Non | Deux rendus calcules |
| Hi Hat | `dsp/engine/hi_hat_engine.cc::Render` | hi-hat 1 | hi-hat 2 | dual-output two-model | Non | Deux rendus calcules |
| Phase Distortion | `dsp/engine2/phase_distortion_engine.cc::Render` | oscillateur sync principal | oscillateur free-running | dual-output variant | Non | Deux sorties calculees |
| Six Op | `dsp/engine2/six_op_engine.cc::Render` | buffer FM principal | copie identique de `out` | effectively mono duplicated | Oui, mais seulement parce qu'ils sont identiques | Conserver `AUX` n'apporte pas de contenu distinct ici |
| Wave Terrain | `dsp/engine2/wave_terrain_engine.cc::Render` | terrain output principal | derive sinusoide separe | dual-output derived | Non | Deux sorties calculees |
| String Machine | `dsp/engine2/string_machine_engine.cc::Render` | voie gauche-like apres filtrage/cross-mix | voie droite-like apres filtrage/cross-mix | stereo-like | Pas comme regle globale Plaits; oui pour ce modele seulement | Deux canaux ont une vraie valeur musicale |
| Chiptune | `dsp/engine2/chiptune_engine.cc::Render` | voix principale / arp/chord | basse / sidekick | dual-output sidekick | Non | Deux sorties calculees |
| Virtual Analog VCF | `dsp/engine2/virtual_analog_vcf_engine.cc::Render` | low-pass | high-pass | dual-output filter split | Non | Deux sorties calculees |

### 3.3 Audit conclusions on OUT/AUX semantics

Constats forts issus du code:
- `OUT/AUX` n'ont pas une signification stable a travers tous les modeles
- la majorite des modeles n'utilisent pas `AUX` comme canal droit stereo
- `AUX` sert souvent de:
  - variante timbrale
  - sous-oscillateur
  - voix secondaire
  - signal d'excitation
  - split spectral
  - implementation parallele analogique/synthetique
- `StringMachineEngine` est le cas stereo-like le plus net audite
- `SixOpEngine` est le cas oppose: `AUX` est simplement un duplicat de `OUT`

Reponse directe aux questions produit:
- Peut-on traiter `OUT/AUX` comme `L/R`? Non, pas generiquement.
- `OUT/AUX` ont-ils toujours la meme signification? Non.
- Certains modeles utilisent-ils `AUX` comme variante, sidekick, by-product, voix secondaire ou composant separe? Oui, c'est le cas majoritaire.
- Certains modeles sont-ils stereo-like? Oui, mais ce n'est pas la semantique dominante de Plaits.

### 3.4 Is AUX optional from a compute standpoint?

Avec le code present:
- l'API commune impose `out` et `aux`
- les engines audites calculent leurs flux dans le meme `Render`
- aucun seam commun n'a ete observe pour "ne calculer que `OUT`"

Implications:
- BRICK peut ignorer `AUX` a la consommation
- BRICK ne doit pas compter sur un gain CPU significatif simplement en cessant de router `AUX`
- un vrai mode "OUT-only compute" demanderait un patch Plaits cible, donc hors perimetre et non recommande pour une v1

### 3.5 Are stereo-like models numerous enough to justify dual-output v1?

Verdict d'audit:
- non, pas en l'etat
- le cas stereo-like evident est surtout `StringMachineEngine`
- le reste du catalogue audite est principalement dual-output non-stereo

Conclusion:
- la preservation de `AUX` est musicalement interessante
- elle ne justifie pas, a elle seule, une generalisation immediate `OUT=L` / `AUX=R` dans BRICK v1

## 4. BRICK audio path audit

### 4.1 Real runtime path

Chemin audio observe:
1. `Src/Audio/audio.c::process_half()` collecte les evenements bloc via `seq_runtime_audio_collect_block_events()`
2. ces evenements sont appliques par `seq_runtime_audio_apply_event()`
3. `audio_process_block_int32()` est ensuite appelee
4. `Src/Audio/audio_float.c::audio_process_block_int32()` fait `audio_io_unpack()` -> callback DSP float -> `audio_io_pack_ramped()`
5. la callback DSP float est `brick6_audio_runtime_dsp()` via `Src/Core/brick6_app_init.c::audio_set_float_callback(brick6_audio_runtime_dsp)`

### 4.2 What `brick6_audio_runtime_dsp()` actually does

Dans `Src/Core/brick6_audio_runtime.c::brick6_audio_runtime_dsp()`:
- clear des external inputs via `mixer_external_inputs_clear()`
- rendu des tracks Drum:
  - `drum_synth_process_block_for_instance(...)`
  - injection dans le mixer par `mixer_submit_external_mono(ctx->mix_track_id, drum_tmp, frames)`
- rendu des tracks Sampler:
  - `brick6_sampler_runtime_render_track(...)`
  - injection dans le mixer par `mixer_submit_external_mono(ctx->mix_track_id, sampler_tmp, frames)`
- modulation bloc via `mod_lfo_v1_process_block()`
- rendu du moteur clavier/voices via `voice_manager_process(tracks[0].L, tracks[0].R, frames)`
- sommation/routing principal via `mixer_process(tracks, frames)`
- ajout playback master-buffer via `brick6_master_buffer_render_playback(...)` puis sommation sur `tracks[0].L/R`

### 4.3 Mono, stereo or mixed?

La chaine BRICK actuelle est mixte:
- seam d'injection moteur pour Drum/Sampler: mono
- internals mixer: stereo
- sorties hardware: stereo
- voice manager existant: stereo, mais hors seam `mixer_submit_external_mono()`

Autrement dit:
- les moteurs "externes track-aware" aujourd'hui entrent par un seam mono
- le mixer transporte ensuite du stereo
- il n'existe pas aujourd'hui de seam explicite et utilise pour soumettre un moteur track-aware dual-output

### 4.4 Mixer evidence

Dans `Src/Audio/mixer.c`:
- external buffers par track: `g_external_track_l[MIXER_MAX_TRACKS][AUDIO_BLOCK_SIZE]` et `g_external_track_r[...]`
- `mixer_submit_external_mono()` ecrit la meme source mono dans `L` et `R`
- `mixer_process()` travaille sur des bus stereo:
  - main `bus_main_l/r`
  - cue `bus_cue_l/r`
  - sends `send_l/r`
  - retours FX stereo
  - taps stereo

Dans [`Inc/Audio/mixer.h`](/C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6/Inc/Audio/mixer.h):
- aucune API publique de submit stereo n'est exposee
- le seul seam externe moteur observe est `mixer_submit_external_mono(...)`

Conclusion:
- le mixer sait deja transporter deux canaux par track
- mais l'API d'injection moteur exposee et utilisee aujourd'hui est mono-only

### 4.5 How Drum and Sampler inject audio today

Drum:
- rendu bloc local dans `Src/Core/brick6_audio_runtime.c`
- source intermediaire mono `drum_tmp[AUDIO_BLOCK_SIZE]`
- submit via `mixer_submit_external_mono()`

Sampler:
- rendu bloc local dans `Src/Core/brick6_audio_runtime.c`
- `brick6_sampler_runtime_render_track()` remplit un buffer mono `sampler_tmp`
- submit via `mixer_submit_external_mono()`

Voice manager:
- `Src/Sampler/voice_manager.c::voice_manager_process(float* out_l, float* out_r, uint32_t frames)`
- ecrit directement dans le bus sortie stereo principal, sans passer par le seam d'injection track-aware Drum/Sampler

### 4.6 Taps, recorder, master buffer

Le rendu mixer aval est stereo:
- les taps post-insert/post-fader/send sont stereo dans `mixer_process()`
- `brick6_master_buffer_submit_track_post_fader(source_track, L, R, frames)` recoit deja du stereo
- le playback du master buffer est additionne sur `tracks[0].L/R`

Point important:
- le support stereo existe apres injection
- ce qui manque pour un moteur track-aware dual-output est un seam d'entree explicite, pas un moteur de sommation stereo entierement absent

## 5. Integration options for Plaits output

### Option A - OUT-only mono v1

Mode:
- Plaits calcule `OUT/AUX` comme le code source le fait deja
- BRICK ne consomme que `OUT`
- injection via le chemin mono existant type Drum/Sampler

Analyse:
- complexite: minimale
- cout IRQ BRICK: minimal cote transport/mixage
- risque architecture: tres faible
- compatibilite track-aware: directe
- compatibilite avec Drum/Sampler: maximale
- impact UI/params/persistence: nul a faible
- perte sonore: reelle sur les modeles ou `AUX` porte une vraie matiere musicale

Note importante:
- ignorer `AUX` a la consommation ne veut pas dire gagner tout le cout DSP Plaits de `AUX`
- cela evite surtout d'ajouter une voie de transport/mixage dans BRICK

### Option B - OUT/AUX mixdown mono

Mode:
- Plaits calcule `OUT/AUX`
- BRICK forme un mono controle:
  - `OUT only`
  - `AUX only`
  - `OUT+AUX`

Analyse:
- complexite: faible a moyenne
- cout IRQ: legerement superieur a A
- risque architecture: faible
- gain musical: meilleur que A si un mode de selection/mix est expose
- risques sonores:
  - desequilibre de niveau
  - pertes de phase ou de lisibilite
  - besoin probable d'un parametre produit du type `Output Source`
- compatibilite architecture: bonne tant que la sortie soumise reste mono

Limite:
- la politique "mixdown mono" n'est pas universelle musicalement
- certains modeles perdent du sens si `AUX` est additionne sans controle

### Option C - OUT/AUX dual-output into stereo path

Mode:
- conserver `OUT` et `AUX` comme deux canaux moteur
- ne pas les renommer automatiquement `L/R`
- utiliser un seam local stereo pour entrer dans le mixer

Etat actuel du code:
- aucun seam public explicite n'existe aujourd'hui
- le plus petit seam local compatible serait d'ajouter une API parallele a `mixer_submit_external_mono()`, par exemple un submit stereo pointant sur `g_external_track_l/r`

Analyse:
- complexite: moyenne
- cout IRQ BRICK: augmente
- impact mixer: local mais reel
- impact sends/cue/taps/recorder/master buffer: structurellement compatible une fois injecte, car l'aval est deja stereo
- compatibilite architecture track-aware: bonne si le binding reste `track_runtime` et si le mixer reste l'autorite
- risque principal: sur-interpreter `AUX` comme un canal stereo alors qu'il est souvent une seconde sortie non-stereo

Condition minimale pour que ce soit propre:
- ne pas documenter `OUT/AUX` comme `L/R` au niveau produit
- documenter un mode "dual-output preserved" propre a Plaits
- garder le changement local au seam d'injection, sans introduire de bus generique supplementaire

### Option D - model-dependent output policy

Mode:
- certains modeles en `OUT-only`
- certains en `OUT/AUX`
- politique variable selon le modele

Analyse:
- complexite runtime/UI: elevee
- risque de comportement imprévisible: eleve
- lisibilite produit: faible
- cout de maintenance: eleve
- utilite musicale: reelle mais trop chere pour une v1

Verdict:
- option non recommandee pour v1
- elle couple trop le comportement de sortie a la table modele par modele

## 6. CPU / IRQ / memory impact

### 6.1 Plaits compute cost

Le cout Plaits se decompose en deux parties:
- cout DSP interne du modele
- cout de transport/mixage dans BRICK

Audit code:
- l'API et les implementations auditees calculent `OUT/AUX` ensemble
- ne pas router `AUX` dans BRICK n'annule pas automatiquement son cout de calcul Plaits

Conclusion:
- la grosse economie potentielle d'une v1 mono n'est pas dans Plaits lui-meme
- elle est surtout dans BRICK: moins de copies, moins de submit, moins de bande passante memoire, moins de travail de mix

### 6.2 BRICK transport cost

Option mono:
- un buffer moteur par track a injecter
- duplication mono vers `L/R` faite une fois dans `mixer_submit_external_mono()`

Option dual-output:
- deux buffers moteurs a transporter ou un submit stereo equivalent
- plus d'ecritures memoire sur `g_external_track_l/r`
- plus de lecture/mixage per-track dans `mixer_process()`
- plus de pression worst-case si plusieurs tracks Plaits tournent en meme temps

### 6.3 Hard-RT risk

Dans le contexte BRICK:
- la contrainte hard-RT ne vient pas seulement du DSP moteur
- elle vient aussi du worst-case aggregate:
  - plusieurs tracks actives
  - sequencer audio-block events
  - modulation
  - mixer
  - sends / FX / cue / taps / master buffer

Donc:
- une integration dual-output augmente le cout fixe BRICK par track Plaits
- ce cout est probablement raisonnable pour une track, mais devient un sujet worst-case si Plaits est multiplie par plusieurs tracks logiques

### 6.4 Memory observations

Impact memoire local attendu:
- buffers render Plaits: au minimum `out` + `aux`
- si BRICK preserve `AUX`, il faut un chemin stereo local jusqu'au mixer
- aucune allocation dynamique n'est admissible dans ce chemin

Conclusion:
- mono v1 minimise la pression memoire et la bande passante
- dual-output est faisable seulement si les buffers restent statiques/stack-bounded et si la taille bloc reste maitrisee

## 7. Recommendation

### 7.1 Recommended v1

Recommendation:
- integrer Plaits v1 en `OUT-only mono`
- ne pas mapper `OUT/AUX` vers `L/R`
- conserver la possibilite documentee d'un seam dual-output v2 local au mixer si une justification musicale forte apparait ensuite

Justification:
- le seam track-aware moteur existant est mono et deja utilise par Drum/Sampler
- la majorite des modeles Plaits n'utilisent pas `AUX` comme un vrai canal stereo
- un mapping `OUT=L`, `AUX=R` serait faux semantiquement pour une grande partie du catalogue
- la preservation immediate de `AUX` augmenterait le cout transport/mix BRICK avant d'avoir une preuve produit que cette complexite est rentable

### 7.2 Explicit answers

Est-ce qu'on doit laisser les moteurs stereo en stereo?
- Oui quand le moteur est reellement stereo et que le seam local existe proprement.
- Non par principe generique.
- Dans Plaits, il ne faut pas deduire une politique stereo globale a partir de quelques modeles stereo-like.

Est-ce que Plaits est un vrai moteur stereo pour BRICK?
- Non, pas comme categorie globale.
- C'est principalement un moteur dual-output a semantique variable, avec quelques cas stereo-like.

Est-ce qu'on doit mapper `OUT/AUX` vers `L/R`?
- Non.

Quel est le cout/risk hard-RT d'une integration dual-output?
- Cout BRICK reel mais local: plus de copies, plus de bande passante memoire, plus de travail mixer par track.
- Risque principal: payer ce cout pour une semantique `AUX` qui n'est pas la plupart du temps une vraie stereo image.

Quelle solution est conseillee pour ce projet?
- v1: `OUT-only mono` via le seam mono existant.
- v2 eventuelle: petit seam `mixer_submit_external_stereo(...)` si et seulement si la preservation de `AUX` est justifiee par des essais produit et par une mesure CPU/worst-case.

### 7.3 If preserving AUX later

Conditions a reunir avant une v2 dual-output:
- mesure CPU/worst-case avec plusieurs tracks Plaits
- decision produit explicite sur la signification UI de `AUX`
- seam local stereo limite au mixer et au runtime Plaits
- aucune refonte large, aucun nouveau dispatcher central

## 8. Implementation notes for a future pass

### 8.1 Probable files if OUT-only

Fichiers probables a modifier plus tard:
- `Src/Core/track_runtime.c`
- `Inc/Core/track_runtime.h`
- `Src/UI/ui_track_catalog.c`
- `Src/Core/track_state.c`
- `Src/Core/track_tone_sound_state.c`
- `Inc/Core/track_tone_sound_state.h`
- `Src/Param/param_registry_catalog.c`
- `Src/Param/param_registry.c`
- `Src/Param/param_registry_backends.c`
- `Src/Param/param_registry_tone_backends.c`
- `Src/Core/brick6_audio_runtime.c`
- nouveau runtime Plaits cible, probablement sous `Src/Core/` ou `Src/Audio/`

Seam audio recommande:
- rendu bloc local dans `brick6_audio_runtime_dsp()`
- submit mono via `mixer_submit_external_mono()`

### 8.2 Probable files if dual-output

En plus des fichiers ci-dessus:
- [`Inc/Audio/mixer.h`](/C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6/Inc/Audio/mixer.h)
- `Src/Audio/mixer.c`
- `Src/Core/brick6_audio_runtime.c`

API locale probable:
- `mixer_submit_external_stereo(uint32_t track_id, const float* left, const float* right, uint32_t frames);`

Contraintes:
- aucun changement d'autorite runtime
- aucun bus generique nouveau
- changement local au seam d'injection

### 8.3 Manual audio tests for future implementation

Tests manuels a faire:
- 1 track Plaits seule, tous modeles extremes
- plusieurs tracks Plaits simultanees
- sequencer dense avec p-locks et modulations
- verification cue/main/sends
- verification taps/recorder/master buffer
- verification niveaux et absence de saturation au mixdown
- comparaison `OUT-only` vs `OUT+AUX` sur modeles `String Machine`, `Bass Drum`, `Snare Drum`, `Hi Hat`, `Virtual Analog VCF`

### 8.4 Measures to capture

Mesures utiles avant validation:
- CPU bloc audio au pire cas
- marge IRQ restante
- cout incrementiel par track Plaits
- cout incrementiel du seam dual-output si teste
- impact memoire buffer statique

## 9. Open questions

- Quel sous-ensemble exact de modeles Plaits sera expose produit en premiere passe?
- Faut-il exposer des modeles dont `AUX` porte une grande partie de l'interet sonore si v1 reste `OUT-only`?
- Si une v2 preserve `AUX`, faut-il l'exposer comme dual-output interne, comme source selectable, ou comme mode de sortie specifique Plaits?
- Quelle limite de cardinalite Plaits simultanees est acceptable en worst-case sur la cible H743 une fois les vraies mesures prises?
