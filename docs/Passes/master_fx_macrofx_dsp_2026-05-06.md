# Passe - Master/FX MacroFX DSP legers

## FX branches
- `DRIVE`
- `CRUSH`
- `RING`
- `CHOP`
- `PUMP`
- `COMB`
- `WOBBLE`
- `ECHO`
- `FREEZE`
- `STUTTER`
- `TALK`
- `PITCH`

Le type `OFF` et les types inconnus restent des no-op audio exacts.

## Mapping LVL/A/B
- `DRIVE`: `LVL` intensite drive + dry/wet, `A=TONE`, `B=SHAPE`
- `CRUSH`: `LVL` profondeur dry/wet, `A=BITS`, `B=RATE`
- `RING`: `LVL` quantite dry/wet, `A=FREQ`, `B=COLOR`
- `CHOP`: `LVL` profondeur de decoupe, `A=RATE`, `B=SHAPE`
- `PUMP`: `LVL` profondeur de duck, `A=RATE`, `B=REL`
- `COMB`: `LVL` profondeur dry/wet, `A=TUNE`, `B=FB`
- `WOBBLE`: `LVL` profondeur dry/wet, `A=RATE`, `B=DEPTH`
- `ECHO`: `LVL` quantite wet, `A=TIME`, `B=FB`
- `FREEZE`: `LVL` quantite wet/freeze, `A=TIME`, `B=HOLD`
- `STUTTER`: `LVL` quantite wet/repeat, `A=SIZE`, `B=RATE`
- `TALK`: `LVL` quantite formant, `A=VOWL`, `B=TONE`
- `PITCH`: `LVL` quantite pitch, `A=SEMI`, `B=FINE`

## Comportement DSP
- Insertion master dans `brick6_audio_runtime_dsp()` apres `mixer_process()`, avant preview SD, blend `Master/Buffer` et tap master final.
- `DRIVE`: soft/hard/fold leger avec compensation de gain et clamp de sortie.
- `CRUSH`: quantification simple + sample-hold borne, sans oversampling.
- `RING`: oscillateur phase local, couleur sine approx -> triangle -> square doux -> dirt, DC block leger.
- `CHOP`: tremolo/gate tempo-sync avec smoothing de gain.
- `PUMP`: enveloppe synthetique tempo-sync, gain minimum borne et smoothing.
- `COMB`: delay mono ultra-court interpole, tune smooth, feedback borne a `0.80` et feedback filtre.
- `WOBBLE`: delay court module par LFO local, interpolation lineaire, profondeur bornee a environ 24 ms max.
- `ECHO`: un core delay mono par slot, une seule boucle feedback filtree sombre, lecture principale interpolee et decorrelation pseudo-stereo legere par seconde lecture courte.
- `FREEZE`: reutilise le meme core delay mono que `ECHO`; la montee de `LVL` ferme progressivement l'entree de boucle, maintient la texture via feedback `HOLD` borne et relache sans coupure brutale.
- `STUTTER`: reutilise le core delay mono comme historique court; `SIZE` choisit une fenetre recente bornee, `RATE` avance la lecture de boucle, avec crossfade court aux points de boucle et release lissee.
- `TALK`: banque de formants fixes A/E/I/O/U par SVF leger, morph discret/continu par `VOWL`, decalage sombre -> nasal/brillant par `TONE`, gain borne.
- `PITCH`: dual delay/granular simple mono, deux lectures fenetrees croisees, plage limitee autour de +/-12 demi-tons plus fine tune, wet dose par `LVL`.

## Core delay choisi
- Core cree dans `fx_master_macro.c`: un buffer mono statique par slot, place en `AUDIO_COLD_SDRAM`.
- Taille par slot: `48000` samples, soit environ 1 seconde a 48 kHz.
- Reset de type: pas de clear massif en IRQ; `delay_filled` est remis a zero pour rendre les lectures silencieuses jusqu'a remplacement naturel de l'historique.
- Lecture: interpolation lineaire; ecriture: clamp local de la boucle.

## Strategie ECHO pseudo-stereo
- Un seul feedback path mono.
- Une lecture interpolee principale pilote le retour wet.
- Une seconde lecture decorrelee de 31 samples cree un ecart L/R leger hors boucle feedback.
- Le feedback est filtre par un simple one-pole sombre avant reinjection.

## Strategie FREEZE
- `TIME` choisit la taille de boucle comme division musicale bornee.
- `HOLD` mappe un feedback de release court a quasi infini, sans atteindre `1.0`.
- `LVL` pilote a la fois la quantite wet et une gate lissee qui reduit l'entree dans la boucle pendant le gel.
- Release progressif: la gate et le wet smoothing redonnent l'entree live et laissent decroitre la boucle.

## Limites connues
- ROUT Master/FX reste UI-only local et ne route pas encore des sources audio.
- Tempo sync lit le tempo courant interne/externe valide, avec clamp 40-300 BPM.
- Changement de type reinitialise les etats locaux courts du slot; pas de crossfade inter-type dedie.
- `ECHO` et `FREEZE` sont bornes a environ 950 ms / 800 ms selon le type, pas a une ligne multi-secondes.
- `WOBBLE`, `COMB`, `ECHO` et `FREEZE` utilisent un core mono; la stereo est conservee par dry/wet et une decorrelation legere pour `ECHO`, mais pas par deux lignes independantes.
- Pas de FILTER, pas de REVERB, pas de REVERSE.
- `STUTTER` n'est pas un looper complet: pas de random, pas de multi-grain, pas de pitch.
- `TALK` n'est pas un vocoder: pas d'analyse vocale, pas de FFT.
- `PITCH` est une V1 performance: latence courte, mono wet, qualite volontairement limitee.

## Risques IRQ/feedback
- Quatre slots `ECHO/FREEZE` simultanes ajoutent des lectures/ecritures SDRAM par sample; le design evite un vrai dual delay stereo complet.
- Tous les feedbacks sont bornes sous `1.0`; `COMB` et `ECHO/FREEZE` filtrent la reinjection.
- Le reset de type evite les nettoyages longs en IRQ mais produit un historique vide le temps que le delay se remplisse.
- `STUTTER` et `PITCH` ajoutent des lectures delay par sample; `TALK` ajoute trois formants SVF par canal et par slot actif.
- `STUTTER/PITCH` gardent un historique quand `LVL=0` afin de pouvoir entrer sans capture vide, mais ne modifient pas la sortie audio tant que le wet est nul.

## Fichiers touches
- `Inc/Audio/fx_master_macro.h`
- `Src/Audio/fx_master_macro.c`
- `Src/Core/brick6_audio_runtime.c`
- `docs/architecture/ARCHITECTURE_GLOBAL.md`
- `docs/architecture/z1_audio_hard_rt_mix.md`
- `docs/architecture/z2_track_runtime_authority.md`
- `docs/architecture/z3_param_modulation_control.md`
- `docs/architecture/z5_ui_navigation_interaction.md`
- `docs/Passes/master_fx_macrofx_dsp_2026-05-06.md`

## Statut build/check
- Build non lance: demande utilisateur explicite de ne jamais builder.
- `git diff --check`: OK.

## Prochaine passe recommandee
- Formaliser l'autorite persistable de ROUT Master/FX avant de cabler la selection pass/bypass par track.

## Audit local 2026-05-06
- Build utilisateur: OK.
- Build Codex: non lance, conformement a la consigne utilisateur.
- Inspection statique includes/prototypes/signatures: OK; `brick6_audio_runtime.c` inclut `Audio/fx_master_macro.h`, les prototypes publics correspondent aux definitions, `fx_master_macro.c` inclut `Seq/seq_runtime_control.h` pour les queries tempo externe.
- Insertion verifiee: `fx_master_macro_process_block()` reste post-`mixer_process()`, sur `tracks[0]`, avant preview SD, blend `Master/Buffer` et tap master final.
- Chaine verifiee: les 4 slots sont traites dans l'ordre `FX1 -> FX2 -> FX3 -> FX4`, avec lecture `type/level/macro_a/macro_b` du meme index de slot.
- ROUT verifie: aucune modification de `ui_core_runtime_bridge`, `ui_hall_mode_projection`, `led_rgb` ou `brick6_master_buffer_set_source_enabled`; aucun routing audio reel ajoute.

## Bugs trouves/corriges
- Bug trouve: les slots `OFF` ou types non implementes passaient quand meme par le clamp final `[-1.20, +1.20]`, ce qui pouvait modifier un master deja au-dessus de cette borne alors que le slot devait etre no-op.
- Correction: seuls les types DSP actifs (`DRIVE`, `CRUSH`, `RING`, `CHOP`, `PUMP`) entrent dans la boucle de traitement; `OFF` et types non implementes retournent immediatement sans toucher l'audio.
- Bug trouve: un changement de type ne reinitialisait qu'une partie des etats locaux du slot.
- Correction: reset local complet des phases, holds, smoothing de gain et DC blocker au changement de type.

## Etat no-op / OFF / LVL=0
- `Master/FX` absent: aucun traitement applique.
- Slots `OFF`: no-op exact, pas de clamp ni gain parasite.
- Types non implementes: no-op exact, pas de silence force ni gain parasite.
- Types actifs avec `LVL=0`: bypass exact une fois le wet smoothing retombe a zero; si le slot etait actif juste avant, extinction courte lissee.
- `LVL` max: traitement audible, sortie bornee par le clamp local uniquement pendant un traitement actif.

## Risques restants
- Le bypass apres baisse brutale de `LVL` reste lisse, donc pas strictement sample-identique pendant quelques samples si un effet etait actif juste avant.
- Les formes `DRIVE` et `RING` restent des approximations legeres locales, non alignees bit-a-bit sur les references Drumboy.
- ROUT Master/FX reste UI-only local, non persiste et non cable au routing audio reel.

## Audit dependances temporelles RING/CRUSH/STUTTER 2026-05-06
- `RING`: normal apres correction. Aucune lecture transport, mesure, pattern, playhead ni tempo; seule phase locale `ring_phase` avance par sample selon `FREQ`. `COLOR` est discret `SIN/TRI/SQR/DIRT` et ne depend pas de la mesure.
- `CRUSH`: normal. Aucune lecture transport, mesure, pattern, playhead ni tempo; `BITS` est discret `16..4bit`, `RATE` reste un sample-hold interne continu `1..96x` porte par `crush_count`.
- `STUTTER`: normal et contractuel. Depend uniquement du BPM courant pour convertir `SIZE` en duree de fenetre et de `RATE` pour l'avance de lecture; aucune phase globale, position mesure/pattern ou reset a la mesure n'est lu. La capture reste locale au slot et a l'activation, avec historique delay borne.
- Correction appliquee pendant cet audit: ajout de la position `DIRT` pour `RING COLOR`, alignee UI/DSP/edition discrete.
- Coherence discrete appliquee: `DRIVE SHAPE`, `RING COLOR`, `TALK VOWL` et `PITCH SEMI` sont re-quantifies dans le DSP, afin que le stockage raw `0..127` canonise par l'UI donne les positions sonores attendues.
- Risques restants: `STUTTER` reste sensible aux changements de tempo parce que `SIZE` est une division rythmique; ce n'est pas un recalage mesure mais la duree de la prochaine capture suit le BPM courant.

## Statut check audit local
- `git diff --check`: OK.
- Build complet: non lance.

## Deuxieme serie DSP 2026-05-06
- FX branches dans cette passe: `COMB`, `WOBBLE`, `ECHO`, `FREEZE`.
- Fichiers touches: `Src/Audio/fx_master_macro.c`, `docs/architecture/z1_audio_hard_rt_mix.md`, `docs/architecture/z2_track_runtime_authority.md`, `docs/architecture/z3_param_modulation_control.md`, `docs/architecture/z5_ui_navigation_interaction.md`, `docs/Passes/master_fx_macrofx_dsp_2026-05-06.md`.
- Inspection statique includes/prototypes/signatures: OK.
- `git diff --check`: OK.
- Build final: non lance, a faire cote utilisateur.

## Audit serie 2 2026-05-06
- Statut: aucune anomalie locale bloquante trouvee sur `COMB`, `WOBBLE`, `ECHO` et `FREEZE`.
- OFF et types non implementes (`PITCH`, `TALK`, `STUTTER`) restent no-op exacts: retour avant boucle de traitement, sans clamp master ni write delay.
- `LVL=0`: si le slot est deja silencieux, retour exact sans read/write delay; si un effet etait actif juste avant, extinction lissee jusqu'au bypass.
- Changement de type: reset local du slot confirme (`wet`, phases, holds, delay write/fill, feedback LP, freeze gate, DC blockers), sans toucher les autres slots.
- Slots: etat independant confirme via `g_slots[4]` et `g_delay[4][48000]`; chaine `FX1 -> FX2 -> FX3 -> FX4` confirmee par la boucle slot croissante.
- Lecture params: `type`, `level`, `macro_a`, `macro_b` sont lus depuis le meme index de slot `track_tone_sound_state.master_fx`.
- Bornes delay: `delay_samples` clamp dans `[1, 47998]`, lecture avec interpolation `i0/i1` modulo `48000`, ecriture modulo `48000`; pas de read/write hors bornes observe.
- Division par zero: sample rate force a `48000` si invalide; BPM borne `40..300`; `COMB` borne sa frequence avant division effective.
- Feedback: `COMB <= 0.80`, `ECHO <= 0.74`, `FREEZE <= 0.998` avec ecriture de boucle clamp local `[-1.15, 1.15]`.

## Bugs serie 2 trouves/corriges
- Aucun bug local evident corrige dans le code pendant cet audit.
- Correction documentaire uniquement: ajout du statut d'audit serie 2 et des checks OFF/no-op/LVL0/feedback.

## Risques IRQ/feedback restants serie 2
- Les quatre slots en delay actif peuvent cumuler des acces SDRAM par sample; pas de build/mesure CPU effectuee dans cette passe.
- `LVL=0` preserve le bypass audio, mais ne vide pas l'historique delay si le type ne change pas; une remontee de `LVL` peut reutiliser une queue precedente.
- `FREEZE` reste volontairement proche de l'infini (`0.998`) mais borne et clamp; validation longue duree a faire sur cible.

## Prochaine passe recommandee serie 2
- Validation cible utilisateur: ecoute longue `COMB` feedback haut, `FREEZE` hold haut, quatre slots delay actifs, puis mesure CPU/IRQ.

## Troisieme serie DSP 2026-05-06
- FX branches dans cette passe: `STUTTER`, `TALK`, `PITCH`.
- Strategie `STUTTER`: historique mono court par slot dans le core delay existant, capture d'une fenetre recente a l'activation, lecture bouclee avec crossfade court fixe, `SIZE` borne a environ 10-500 ms selon tempo clamp 40-300 BPM, `RATE` borne a multiplicateurs simples.
- Strategie `TALK`: formants A/E/I/O/U fixes/morphables via trois SVF par canal, `VOWL` choisit/morphe la voyelle, `TONE` deplace les formants et la brillance, sans vocoder/FFT/analyse.
- Strategie `PITCH`: V1 dual delay/grain simple, deux lectures croisees fenetrees, plage limitee `SEMI` +/-12 demi-tons plus `FINE` +/-100 cents, mono wet melange au dry.
- Limites qualite: `STUTTER` n'est pas un looper multi-grain, `TALK` est volontairement caricatural, `PITCH` privilegie stabilite/cout plutot que qualite studio et peut produire couleur/latence.
- Risques CPU/buffer: quatre slots actifs peuvent cumuler acces SDRAM delay, lectures interpolees et formants; aucun read hors buffer attendu grace aux clamps/modulo, mais mesure cible requise.
- Etat OFF/no-op/LVL=0: `OFF` et types inconnus retournent sans toucher l'audio; `LVL=0` annule la sortie wet, avec historique maintenu pour `STUTTER/PITCH` seulement; changement de type reset les etats locaux du slot.
- Fichiers touches: `Src/Audio/fx_master_macro.c`, `docs/architecture/z1_audio_hard_rt_mix.md`, `docs/architecture/z2_track_runtime_authority.md`, `docs/architecture/z3_param_modulation_control.md`, `docs/architecture/z5_ui_navigation_interaction.md`, `docs/Passes/master_fx_macrofx_dsp_2026-05-06.md`.
- Inspection statique includes/prototypes/signatures: OK.
- `git diff --check`: OK.
- Build final: non lance, a faire cote utilisateur.

## Audit serie 3 2026-05-06
- Statut: anomalies locales trouvees et corrigees dans `STUTTER` et dans le reset de slot commun.
- OFF/no-op: `OFF` et types inconnus restent retour immediat sans boucle de traitement, sans clamp final, sans write delay et sans modification audio.
- `LVL=0`: `TALK` retourne au bypass exact une fois le wet nul; `STUTTER` et `PITCH` peuvent continuer a maintenir/remplir l'historique delay, mais la sortie reste dry tant que le wet est nul.
- Changement de type: reset local confirme pour wet, phases, holds, delay write/fill, stutter start/pos/active, pitch phases, et etats formants TALK.
- Slots: etats independants confirmes via `g_slots[4]` et `g_delay[4][48000]`; chaine `FX1 -> FX2 -> FX3 -> FX4` conservee par la boucle slot croissante.
- Lecture params: `type`, `level`, `macro_a`, `macro_b` sont lus depuis le meme index `track_tone_sound_state.master_fx.*[slot]`.
- Bornes buffer: lectures delay generales clamp dans `[1, 47998]`; lectures absolues STUTTER modulo `48000`; ecriture modulo `48000`; `STUTTER` attend `delay_filled >= len + 2` avant capture/lecture.
- Division par zero: sample rate initialise a `48000` si invalide; BPM borne `40..300`; denom pitch borne a `>=0.001`; formant `f` borne.
- NaN/overflow: clamp local durci pour ramener une valeur non ordonnee vers la borne basse; sorties TALK et writes delay restent bornes.

## Bugs serie 3 trouves/corriges
- Bug trouve: le reset de type ne remettait pas `wet` a zero, donc un passage direct d'un type actif vers un autre type actif pouvait appliquer le nouveau DSP avec l'ancien niveau lisse.
- Correction: `fxmm_reset_slot_state()` remet maintenant `wet=0.0f`.
- Bug trouve: `STUTTER` lisait une fenetre glissante basee sur `delay_write`, donc le repeat n'etait pas vraiment gele et pouvait etre reecrit par l'entree pendant l'activation.
- Correction: `STUTTER` capture un `stutter_start` fixe a l'activation, lit `stutter_start + stutter_pos`, et suspend l'ecriture du core delay pendant le repeat actif pour preserver la fenetre gelee.
- Bug trouve: la capture STUTTER pouvait s'armer avant que l'historique soit assez rempli.
- Correction: l'activation STUTTER attend `delay_filled >= len + 2`; sinon le slot continue de remplir l'historique sans lire la fenetre.

## Limites STUTTER/TALK/PITCH serie 3
- `STUTTER`: V1 mono wet, fenetre gelee conservee en suspendant l'ecriture pendant le repeat; ce n'est pas un looper multi-grain et l'historique doit se remplir avant la premiere capture.
- `TALK`: formants SVF simples et caricaturaux; pas de vocoder, pas d'analyse vocale, pas de FFT.
- `PITCH`: dual delay/grain simple mono wet, plage `SEMI` +/-12 demi-tons et `FINE` +/-100 cents; latence courte autour de 45 ms, qualite performance non-studio.

## Risques CPU/buffer restants serie 3
- Quatre slots serie 3 actifs peuvent cumuler lectures SDRAM delay, ecritures delay et six SVF TALK par sample; mesure CPU/IRQ cible requise.
- `PITCH` peut produire coloration/phasage et silence court initial tant que le delay n'est pas assez rempli.
- `STUTTER` preserve le repeat actif mais ne rafraichit plus l'historique pendant le gel; apres release, une nouvelle capture depend du remplissage courant.
- Build complet non lance: build final a lancer cote utilisateur.
