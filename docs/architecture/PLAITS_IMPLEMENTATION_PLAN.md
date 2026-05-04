# Plaits Implementation Plan

> Historique: `Plaits` a ete remplace cote produit par `Opal`.
> Ce document ne decrit plus le moteur produit courant.
> Le DSP Plaits peut rester une dependance interne d'Opal.

## 1. Scope

Ce document decoupe l'integration Plaits en passes courtes, executables une par une, sans refonte large.

Hypotheses verrouillees pour v1:
- Plaits = nouveau `type` dans la family `Synth`
- integration strictement track-aware
- une seule instance physique Plaits v1
- au plus une track logique configuree en `Synth/Plaits`
- monophonique par track
- politique audio v1 = `OUT-only mono`
- `AUX` ignore en v1
- aucun mode output
- aucun seam stereo/dual-output
- injection audio via `mixer_submit_external_mono()`

Seams a conserver:
- `track_runtime` pour le binding
- `track_tone_sound_state` pour l'etat canonique TONE
- `param_registry` pour le catalogue et l'apply
- `brick6_audio_runtime_dsp()` pour le rendu
- `mixer_submit_external_mono()` pour l'injection

## 2. Execution policy

Chaque etape ci-dessous doit pouvoir etre executee seule.

Regles:
- ne pas sauter d'etape dependante
- verifier le build a chaque etape qui touche le code
- ne pas elargir vers Sampler stereo, `AUX`, ou dual-output
- si une etape revele un conflit architectural reel, la passe suivante doit le documenter avant de continuer

## 3. Steps

### Etape 1 - Ajouter l'identite produit Plaits et le binding runtime minimal

Objectif:
- declarer Plaits comme nouveau `type` de `Synth`
- faire en sorte que `track_runtime` sache resoudre une track `Synth/Plaits` sans logique audio encore branchee

Fichiers probables a modifier:
- `Src/UI/ui_track_catalog.c`
- `Src/Core/track_state.c`
- `Inc/Core/track_runtime.h`
- `Src/Core/track_runtime.c`
- fichiers enum/catalogue associes s'ils sont separes

Patch attendu:
- ajout du type produit `Plaits` cote catalogue UI/config track
- extension des tables `family/type` et labels associes
- projection `track_runtime` vers un `engine`/capability adapte a Plaits
- conservation du meme schema track-aware que `Synth/Sampler`

Verifications / build checks:
- build compile sans trous d'enum ni switch incomplet
- verification que `Synth/Plaits` est selectionnable/resolu cote runtime
- verification qu'aucune autre family/type n'est regresse

Dependance:
- aucune

Criteres de fin:
- une track peut porter l'identite `Synth/Plaits`
- `track_runtime` la resout proprement
- aucun rendu, aucun parametre Plaits encore requis

### Etape 2 - Ajouter l'etat canonique TONE Plaits

Objectif:
- reserver dans l'etat canonique track-aware les champs TONE v1 necessaires a Plaits

Parametres couverts:
- `Model`
- `Coarse Frequency`
- `Harmonics`
- `Timbre`
- `Morph`
- `LPG response`
- `LPG / envelope decay`
- `Frequency range`

Fichiers probables a modifier:
- `Inc/Core/track_tone_sound_state.h`
- `Src/Core/track_tone_sound_state.c`
- eventuellement `Src/Core/track_sound_state.c` si le wiring global passe par la

Patch attendu:
- ajout d'un bloc/canon Plaits dans le state TONE
- defaults stables pour chaque parametre
- helpers get/set/reset si le module en utilise deja
- aucun apply runtime encore branche

Verifications / build checks:
- build compile
- verification des defaults sur reset/init track
- verification qu'aucun autre moteur `Synth` ou `Drum` n'est casse dans son etat TONE

Dependance:
- Etape 1

Criteres de fin:
- l'etat canonique Plaits existe
- ses defaults sont definis
- aucun parametre UI/runtime n'est encore expose

### Etape 3 - Ajouter le catalogue `param_registry` Plaits

Objectif:
- declarer les `param_id` Plaits v1 dans le catalogue track-aware

Fichiers probables a modifier:
- `Src/Param/param_registry_catalog.c`
- `Src/Param/param_registry.c`
- headers associes aux `param_id` si necessaire

Patch attendu:
- ajout des parametres TONE Plaits au catalogue
- bornes, defaults, labels, domaine, page/ordre d'exposition
- mapping explicite vers le state canonique Plaits
- ne pas rendre tous les parametres p-lockables/modulables par defaut
- chaque support p-lock/modulation doit etre explicite et justifie dans le catalogue

Verifications / build checks:
- build compile
- verification que chaque `param_id` est resoluble dans le registre
- verification des bornes/defaults exposes
- verification absence de collision avec ids existants

Dependance:
- Etape 2

Criteres de fin:
- les 8 parametres Plaits v1 existent dans `param_registry`
- ils pointent vers l'etat canonique track-aware correct

### Etape 4 - Ajouter le runtime wrapper Plaits minimal

Objectif:
- introduire un runtime wrapper Plaits par track logique, sans encore le rendre audible dans le mixer si cela complique la passe

Fichiers probables a modifier:
- nouveau module runtime Plaits sous `Src/Core/` ou `Src/Audio/`
- header associe sous `Inc/Core/` ou `Inc/Audio/`
- `Src/Core/brick6_app_init.c`
- `Src/Core/track_runtime.c`

Patch attendu:
- structure runtime statique par track logique
- init/reset explicites
- stockage de l'etat runtime minimum:
  - modele courant
  - frequence coarse/frequency range
  - harmonics/timbre/morph
  - lpg response / decay
  - etat gate/note mono
- aucune allocation dynamique

Verifications / build checks:
- build compile
- verification init avant demarrage audio
- verification reset/rebind propre quand une track change de type

Dependance:
- Etape 3

Criteres de fin:
- le runtime wrapper Plaits existe
- son etat interne minimal est reservable par track
- pas encore besoin d'un rendu final audible si la passe suivante s'en charge

### Etape 5 - Ajouter le backend apply Plaits

Objectif:
- brancher l'ecriture d'un parametre canonique Plaits vers le runtime wrapper Plaits, meme si le moteur audio n'est pas encore final

Fichiers probables a modifier:
- `Src/Param/param_registry_backends.c`
- `Src/Param/param_registry_tone_backends.c`
- futur header runtime Plaits minimal si necessaire
- `Src/Core/track_runtime.c`

Patch attendu:
- ajout du dispatch apply pour les parametres Plaits
- conversion valeur canonique -> valeur runtime Plaits
- table ou switch track-aware local au backend TONE
- aucun chemin parallele hors `param_registry`

Verifications / build checks:
- build compile
- verification qu'un write `param_registry_apply_track_value(...)` atteint bien le backend Plaits
- verification qu'aucun autre backend moteur n'est regresse

Dependance:
- Etape 4

Criteres de fin:
- chaque parametre Plaits v1 a un chemin apply complet jusqu'au runtime cible
- l'architecture d'apply reste entierement dans Z3

### Etape 6 - Brancher le rendu audio Plaits `OUT-only mono`

Objectif:
- rendre Plaits dans `brick6_audio_runtime_dsp()` et injecter uniquement `OUT` via le seam mono existant

Fichiers probables a modifier:
- module runtime Plaits ajoute a l'etape 5
- `Src/Core/brick6_audio_runtime.c`

Patch attendu:
- rendu bloc Plaits par track runtime active
- buffer temporaire mono local type `float tmp[AUDIO_BLOCK_SIZE]`
- `AUX` n'est ni expose, ni route, ni mixe en v1
- si l'API Plaits le calcule, `AUX` est ignore localement
- submit via `mixer_submit_external_mono(track_id, tmp, frames)`
- aucun seam stereo, aucun `L/R`, aucun mode output

Verifications / build checks:
- build compile
- verification qu'une track `Synth/Plaits` produit bien de l'audio
- verification qu'aucune track `Sampler/Drum` n'est regresse
- verification que le mix reste stable avec plusieurs tracks

Dependance:
- Etape 5

Criteres de fin:
- Plaits est audible en `OUT-only mono`
- l'injection audio passe exclusivement par le seam mono existant

### Etape 7 - Brancher notes/gates/trigs depuis sequencer et keyboard

Objectif:
- connecter les evenements musicaux au runtime Plaits mono track-aware

Fichiers probables a modifier:
- `Src/Seq/seq_runtime.c`
- `Src/Seq/seq_play_scheduler.c`
- `Src/Keyboard/keyboard_engine.c`
- `Src/Core/track_runtime.c`
- module runtime Plaits

Patch attendu:
- note on/off, gate, trigger et pitch routables vers Plaits
- respect du binding track-aware
- politique mono claire en cas de retrigger/reassign
- aucune voie parallele hors scheduler/keyboard existants

Verifications / build checks:
- build compile
- verification jeu clavier live sur track Plaits
- verification lecture sequencer sur track Plaits
- verification retrigger/gate release

Dependance:
- Etape 6

Criteres de fin:
- Plaits reagit correctement au clavier et au sequencer
- le comportement mono v1 est stable et explicite

### Etape 8 - Exposer la page TONE Plaits dans l'UI

Objectif:
- afficher et editer les parametres TONE Plaits sur la track active sans creer de mode global separe

Fichiers probables a modifier:
- `Src/UI/pages/ui_page_template_tone.c`
- `Src/UI/ui_param.c`
- `Src/UI/ui_template_page.c`
- `Src/UI/ui_core_runtime_bridge.c`
- fichiers UI de labels/layout associes

Patch attendu:
- resolution des pages TONE pour `Synth/Plaits`
- ordre d'affichage des 8 parametres v1
- labels et formatting adaptes
- aucune exposition `AUX`
- aucune page output

Verifications / build checks:
- build compile
- verification navigation TONE sur track Plaits
- verification changement track -> page correcte
- verification absence de regression sur Sampler/Drum/Master Buffer

Dependance:
- Etape 3 minimum, idealement Etape 7

Criteres de fin:
- l'UI TONE Plaits est exploitable
- les edits UI passent bien par `param_registry`

### Etape 9 - Brancher persistence et versioning

Objectif:
- garantir que l'etat Plaits v1 est capture/restaure correctement dans pattern/projet

Fichiers probables a modifier:
- `Inc/Storage/pattern_live_ram.h`
- `Src/Storage/pattern_live_ram.c`
- `Inc/Storage/project_v1.h`
- `Src/Storage/project_v1.c`
- eventuels fichiers de bank SD si le format les traverse

Patch attendu:
- inclusion des parametres/etat canonique Plaits dans les snapshots necessaires
- versioning format si requis par les structures persistantes
- restauration correcte apres load
- aucune retrocompatibilite ou migration legacy n'est requise en phase proto
- bump ou clear format autorise si le layout change

Verifications / build checks:
- build compile
- verification save/load pattern avec track Plaits
- verification save/load project avec track Plaits
- verification coherence des defaults si ancien contenu ou reset

Dependance:
- Etape 8

Criteres de fin:
- un pattern/projet contenant Plaits se restaure correctement
- les parametres TONE Plaits survivent a un cycle save/load

### Etape 10 - Consolidation, build final et checks manuels

Objectif:
- verrouiller la premiere integration complete Plaits v1

Fichiers probables a modifier:
- documentation locale si une divergence avec les docs apparait
- correctifs ponctuels dans les fichiers deja touches

Patch attendu:
- zero nouvelle feature
- uniquement finitions/correctifs de stabilisation si necessaire

Verifications / build checks:
- build complet
- verification selection `Synth/Plaits`
- verification rendu audio `OUT-only mono`
- verification notes/gates clavier + sequencer
- verification p-locks uniquement sur les parametres declares p-lockables
- verification navigation UI TONE
- verification save/load pattern et projet
- verification absence de regression Drum/Sampler/Master Buffer

Dependance:
- Etapes 1 a 9

Criteres de fin:
- Plaits v1 est fonctionnel end-to-end dans les seams existants
- aucune extension stereo/dual-output n'a ete introduite

## 4. Suggested pass order

Ordre recommande pour les futures passes Codex:
1. Etape 1
2. Etape 2
3. Etape 3
4. Etape 4
5. Etape 5
6. Etape 6
7. Etape 7
8. Etape 8
9. Etape 9
10. Etape 10

## 5. Prompt contract for future passes

Prompts prevus ensuite:
- `fais etape 1`
- `fais etape suivante`
- `fais etapes 1 a 3`

Regle d'execution attendue pour ces passes:
- ne traiter que l'etape demandee
- ne pas anticiper l'etape suivante sauf micro-fix strictement necessaire au build
- mettre a jour la documentation uniquement si la passe modifie la cartographie ou revele une divergence reelle
