# Parametres et controles editables

## Autorites et regles communes

Le catalogue dense contient exactement 311 `param_id_t` (`PARAM_COUNT == 311`). `Src/Param/param_spec_catalog.inc` est la table exhaustive des noms, types, minima, maxima, pas, valeurs par defaut, representation et unite ; `Src/Param/param_registry_catalog.c` fournit les labels enum et politiques de conversion. Cette base ne recopie pas la table afin d'eviter une seconde source de verite : toute valeur chiffree doit etre lue dans cette paire et citee par ID.

Un `Param` n'est accessible que si `param_registry_resolve_track_param()` l'admet pour la topologie, le moteur et son modele. `param_registry_is_plockable()` et `param_registry_is_modulatable()` sont deux decisions independantes. Les valeurs continues sont editees dans l'unite affichee : pas normal `1`, pas SHIFT `0,01`, puis reconversion canonique et clamp. Pour `PARAM_DISPLAY_PERCENT`, cela signifie 1 % / 0,01 % sur une valeur interne normalisee. Enum/bool/int discrets gardent un pas entier, SHIFT compris.

Les p-locks continus encodent toute la plage sur `uint16_t`; les discrets utilisent leur pas. La composition AUDIO est : base CONTROL -> p-lock TEMP -> Scene -> Matrix/LFO/ENV -> clamp -> DSP. Les selecteurs d'asset, structures Track, polyphonie, Matrix, Audio FX routing, Keyboard et donnees SEQ/PLAY sont des controles types, pas des Param.

Sources : `Inc/Param/param_ids.h`, `Src/Param/param_spec_catalog.inc`, `Src/Param/param_registry.c`, `Src/Param/param_registry_value_contract.c`, `Src/Track/track_runtime.c::track_runtime_get_param_rule`, `docs/architecture/z3_param_modulation_control.md` confronte aux sources.

## Catalogue UI par moteur

Les IDs ci-dessous sont ceux reellement exposes par TONE. Le catalogue source donne les plages exactes et les labels enum.

| Contexte | Sous-page | P1 / P2 / P3 / P4 |
|---|---|---|
| STREAM | PLAY | asset local / `SAMPLER_MODE` / `SAMPLER_GAIN` / `SAMPLER_TUNE` |
| STREAM | LOOP | `SAMPLER_START` / `SAMPLER_LENGTH` / `SAMPLER_LOOP_START` / `SAMPLER_SLICE_COUNT` |
| STREAM | SOURCE | `SAMPLER_CLIP_SOURCE` / `SOURCE_BPM` / `STRETCH_MODE` / `SYNC_LENGTH` |
| STREAM | STR | `CLIP_GRAIN` / `SHIFTER_HEADS` / `SHIFTER_WINDOW` / `SHIFTER_DISP` |
| MULTI | INST | asset local / `SAMPLER_GAIN` / `SAMPLER_MULTI_LOOP` / vide |
| PRISM | OSC1 | model / param1 / param2 / amod |
| PRISM | OSC2 | model / param1 / param2 / amod |
| PRISM | COMMON | volume / balance / tune / detune |
| PRISM | MOD/PH | pitch mod1 / phase reset / pitch mod2 / drift |
| WAVE | OSC1/OSC2 | asset local / position / start / length |
| WAVE | COMMON | volume / balance / tune / detune |
| STACK | OSC1/2 | model1 / tune1 / model2 / tune2 |
| STACK | OSC3 | model / timbre / color / tune |
| STACK | MIX | levels osc1/2/3 / noise |
| STACK | PHASE | reset / osc detune / vide / vide |
| ACID | VCO | wave / tune / slide / accent |
| ACID | VCF/VCA | cutoff / resonance / env mod / decay |
| ACID | QUALITY | VCF rate / vide / vide / vide |
| FM global | GLOBAL | algorithm / envelope attack / envelope decay / transpose |
| FM global | OP QUICK | operateur local / freq / level / detune |
| FM global | PITCH R/L | R1..R4 / L1..L4 |
| FM operateur | VOICE | operateur local / level / freq / detune |
| FM operateur | ENV | attack / decay / sustain / release |
| FM operateur | MOD | on / mode / velocity sensitivity / key scaling |
| MIDI | PROG, CC1..3 | program puis 12 controles CC |
| External | MAIN, CC1..3 | gate, program puis 12 controles CC |
| Drum MD | pages dynamiques | model puis P1..P8, labels specialises par modele |

Sources : familles de `Src/UI/pages/ui_page_template_tone.c`; descripteurs dans `Src/Param/param_spec_catalog.inc`; labels dynamiques dans `Src/Param/md_model_catalog.c`, `Src/Param/param_prism_labels.c`, `Src/Param/audio_fx_param_catalog.c`.

## Parametres transversaux exposes

- ENV : filtre Cutoff/Resonance/Morph, ADSR filtre, ADSR VCA, ADSR ENV3, retrigger selon contexte.
- MOD : LFO1..3 Rate/Shape/Trig/Phase ; destination/depth de Matrix sont des controles structures.
- MIX : Mute, Level, Pan, Send1..3 selon role.
- MIDI FX : 16 champs fixes, quatre par stage ; le mode P4 de Generator n'est pas p-lockable.
- Audio FX : MODEL/P1/P2/P3 pour A et B ; seuls P1..P3 sont p-lockables. Les labels/plages de P1..P3 dependent du modele.
- Master : reverb, delay, compresseur et Mod FX via pages TONE du Master.

## Valeurs internes notables

- Sampler Start/Length/Loop, gains et proportions : float normalise ; conversion terminale en frame ou gain.
- Tune Sampler/Prism/Wave/Acid : demi-tons ; FM transpose : centiemes de demi-ton.
- Prism tune terminal Q7 (1/128 st), controles normalises Q15 lorsque le moteur le requiert.
- Stack tune conserve les centiemes de demi-ton ; niveaux/timbre/couleur Q15.
- temps avec `PARAM_DISPLAY_TIME_MS` : secondes canoniques, millisecondes affichees.
- percent : valeur canonique propre au descripteur et transformation `param_value_percent127_*` ; ne pas supposer 0..1 sans lire l'ID.

## Persistance

Les valeurs CONTROL de base sont persistables selon le registre ; TEMP p-lock, Scene active, phases, voix et caches ne le sont pas. Pattern/Patch/Project serialisent des cles stables, jamais l'ordinal C. TONE persiste 26 positions normalisees ; un changement de moteur reinterprete ces positions via le catalogue courant. Les selecteurs d'assets persistent `{kind,path}`.
