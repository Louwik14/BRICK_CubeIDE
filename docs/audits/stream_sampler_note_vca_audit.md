# Audit du contrat musical et VCA du Stream Sampler

## Périmètre

Audit statique réalisé sur `main_doublemcu_monocore`, `HEAD d3414fda8e71725cf73de5d96228a8a93a1f23df`. Le code réel est l'autorité. Aucun firmware, ID, format persistant, chemin DMA/cache ou dimensionnement de page n'est modifié par cette passe.

## 1. Verdict

### Cause exacte du mauvais suivi des notes

Le Stream Sampler reçoit bien la note MIDI canonique, mais la perd au démarrage du clip :

1. `brick6_sampler_runtime_trigger_note_velocity(track, note, velocity)` copie la note dans `g_sampler_voice[track].note` (`Src/Core/brick6_sampler_runtime.c:2883-2892`).
2. `brick6_sampler_runtime_trigger()` reconnaît le type Stream et appelle `brick6_sampler_runtime_clip_start_playback()` (`:2810-2846`).
3. `brick6_sampler_runtime_clip_start_playback()` construit `step_q16` uniquement depuis le ratio temporel du clip (`:1360-1388`), puis écrase explicitement la note de voix avec `60U` (`:1414-1436`).
4. Aucun calcul `note jouée - note racine` n'existe dans ce chemin.

La cause est donc un **abandon de la note au seam Stream/clip**, suivi d'un plan de lecture indépendant de la note. Ce n'est ni un défaut du clavier, ni un défaut du scheduler général.

### Clavier et séquenceur

La cause est commune et strictement en aval des deux sources. Les deux chemins passent par `note_fx_pipeline_submit()`, convergent vers `seq_play_scheduler_dispatch_terminal_note_to_channel()`, puis appellent le même `brick6_sampler_runtime_trigger_note_velocity(track, note, velocity)` (`Src/NoteFx/note_fx_pipeline.c:115-127`, `Src/Seq/seq_play_scheduler.c:678-695`, `:626-649`). Pour une même valeur MIDI, aucune conversion différente ne subsiste à l'entrée du Stream.

Le clavier peut naturellement produire une valeur MIDI différente selon racine de gamme, gamme, ordre, omnichord et octave. Ce sont des transformations musicales explicites avant la note canonique, pas une convention d'octave propre au Stream.

### Cause des notes basses

Il n'existe dans le Stream actuel ni soustraction de racine, ni cast unsigned du delta, ni index de table, ni clamp inférieur du delta. Les notes basses échouent parce que **toutes les notes échouent à piloter le ratio**. La tessiture basse rend le défaut particulièrement manifeste : une note sous la hauteur native reste à la hauteur native au lieu d'obtenir un ratio inférieur à 1.

Le scheduler conserve correctement `0..127`; il ne présente aucun underflow démontré sur ce chemin.

### Note racine

Le Stream ne possède **aucune autorité de note racine**. La valeur `60U` affectée à `voice->note` au démarrage est une constante de bookkeeping et n'entre dans aucune formule de pitch. Elle ne constitue donc pas un contrat musical C4.

Comparaisons :

- Sampler RAM : racine implicite 60 dans `note - 60` (`Src/Core/brick6_sampler_runtime.c:1548-1560`) ;
- Sampler Multi : racine explicite par zone, avec soustraction signée `(int16_t)note - (int16_t)root_note` (`Src/Sampler/multi_sample_pool.c:379-386`) ;
- Stream : aucune racine persistée ou résolue et aucune soustraction.

La correction ne doit pas inventer silencieusement C3 ou C4. Il faut d'abord formaliser une autorité produit. Si l'on choisit de conserver la convention historique des samplers simples, il faudra rendre `60` explicite et testable comme racine du Stream, sans nouvel ID si une constante de contrat suffit au produit.

### Sample rate

Le plan Stream ne multiplie pas le ratio de lecture par `desc->sample_rate / sample_rate_audio`. Le défaut est latent dans la formule, mais la surface produit actuelle l'empêche de se manifester : `sample_pool_load()` et `sample_cache_wav_format_supported()` refusent tout WAV non 48 kHz (`Src/Sampler/sample_pool.c:471-483`, `Src/Sampler/sample_cache.c:640-653`) et l'UI propose une conversion vers 48 kHz (`Src/UI/pages/ui_page_settings.c:1565-1595`).

Le contrat de pitch corrigé doit tout de même porter explicitement le facteur de sample rate, afin que le lecteur reste juste si cette restriction est un jour levée. Tant qu'elle reste active, les tests 44,1/96 kHz doivent vérifier le refus/conversion, et les tests unitaires du calcul doivent injecter des métadonnées de rates différents.

### Contrat VCA

Le Stream est volontairement exclu du VCA commun :

- `track_runtime_supports_vca_gate()` retourne 0 pour Stream/Looper (`Src/Core/track_runtime.c:712-722`) ;
- les paramètres VCA sont alors `BLOCKED_TRANSITIONAL` (`:2198-2213`) ;
- la page ENV masque la sous-page et ses quatre paramètres (`Src/UI/pages/ui_page_template_env.c:306-332`) ;
- clavier et scheduler n'appellent pas le gate mixer ;
- sans `mixer_track_vca_note_on()`, `vca_enabled` reste à zéro et le gain VCA est effectivement bypassé (`Src/Audio/mixer.c:600-626`, `:2905-2998`).

Cette exclusion est cohérente avec le contrat actuel de clip/transport, mais incohérente avec la cible produit d'un sampler mélodique joué par notes. La recommandation est de faire rejoindre le Stream au **VCA commun du mixer**, comme le Sampler RAM, sans ADSR interne supplémentaire.

### Niveau de refonte recommandé

Refonte locale mais non réductible à une ligne :

- petite correction structurée du calcul de ratio et de la conservation de la note ;
- activation de la capacité VCA Stream dans `track_runtime` et dans la transition runtime ;
- adaptation séparée du cycle Note Off/release du lecteur Stream ;
- aucun changement du scheduler général, de FatFs, DMA, cache ou tailles de pages ;
- aucun changement de format nécessaire pour le VCA.

## 2. Chaîne clavier

### Clavier Hall produit

```text
capteur Hall
-> keyboard_runtime_process_hall(hall_index:uint8_t, pressed, velocity:uint8_t)
-> keyboard_input_process_hall()
-> mapping Premium: kbd_input_mapper_process() -> ui_keyboard_app_note_button()
   ou mapping Low-Cost: keyboard_input_lowcost_{seq,chromatic}_note()
-> note MIDI uint8_t 0..127
-> keyboard_input_note_on_sink(note, velocity)
-> keyboard_engine_note_on_for_track(active_track, note, velocity)
-> note_fx_pipeline_submit(track, note, velocity, ON, sample_time)
-> seq_play_scheduler_dispatch_terminal_note_to_channel()
-> seq_play_scheduler_emit_engine_note()
-> brick6_sampler_runtime_trigger_note_velocity(track, note, velocity)
```

### Transformations avant la note canonique

- Racine clavier : `60 + root_index % 12`, soit C4..B4 selon la convention d'affichage MIDI du projet (`Src/Keyboard/keyboard_params.c:42-43`). Elle configure la disposition du clavier, pas la racine du sample.
- Octave : `int8_t octave_shift`, borné à `-4..+4`; calcul en `int16_t`, puis clamp final `0..127` (`Src/Keyboard/ui_keyboard_app.c:256-267`, `:432-452`).
- Gamme naturelle : offsets de gamme depuis la racine ; ordre des quintes : quantification explicite vers la gamme (`:153-215`, `:228-253`, `:280-302`).
- Omnichord/chords : intervalles ajoutés en `int16_t`, clamp final `0..127` (`:305-381`).
- Low-Cost, mode SEQ : seules les touches blanches produisent les degrés de gamme ; mode KEYBOARD/autre vue musicale : position chromatique (`Src/Keyboard/keyboard_input.c:97-142`, `:240-259`, `:298-351`).
- Note Off : la note effectivement émise est mémorisée avec son owner track, puis restituée sans recalcul (`:355-389`).

`keyboard_engine_note_on_for_track()` conserve `note` et `velocity` en `uint8_t`, valide la Play Track, puis les transmet tels quels au pipeline NoteFx (`Src/Keyboard/keyboard_engine.c:661-707`). Le chemin synthétique conforme (Prism, Stack, Wave ou DELUGE) consomme ensuite exactement cette même note terminale dans `seq_play_scheduler_emit_engine_note()` ; la divergence Stream ne commence qu'après cet appel commun.

### Hall hors mode note

La projection de mode filtre l'injection avant le sink. MUTE conserve une vue musicale de passthrough ; les raccourcis noirs Low-Cost sont consommés et n'émettent pas de note. Aucun de ces modes n'ajoute une convention de pitch propre au Stream.

## 3. Chaîne séquenceur

### Représentation

Une note n'est pas un champ direct de `seq_step_t`. Une Play Step porte `trig`, `roll` et une liste de p-locks (`Inc/Seq/seq_model.h:18-38`). Les quatre voix PLAY utilisent les IDs :

```text
PARAM_SEQ_PLAY_V1_NOTE ... PARAM_SEQ_PLAY_V4_NOTE
```

Chaque descripteur est un entier `0..127`, pas `1`, défaut `60` (`Src/Param/param_registry_catalog.c:208-223`). Le p-lock stocke la valeur encodée dans `seq_value16_t`; l'encodage/décodage applique la plage du descripteur sans sentinel musical (`Src/Seq/seq_param_iface.c:928-1019`).

Il n'existe pas de valeur note vide : l'absence d'un p-lock NOTE fait utiliser la base PLAY de la track, et à défaut la valeur 60. L'absence de son est représentée par `trig == 0` ou `velocity == 0`, pas par une note sentinelle.

### Projection scheduler

```text
seq_model step + p-lock/base PLAY
-> seq_play_scheduler_get_play_locked_or_default()
-> decode float borné 0..127
-> (uint8_t)(note_f + 0.5f)
-> seq_play_scheduler_push_note_retrigs()
-> seq_play_scheduler_evt_t.note:uint8_t
-> seq_play_scheduler_audio_event_t.note:uint8_t
-> note_fx_pipeline_submit()
-> terminal commun
-> brick6_sampler_runtime_trigger_note_velocity(track, note, velocity)
```

La conversion finale est visible dans `Src/Seq/seq_play_scheduler.c:1122-1129`; la queue conserve la note en `uint8_t` (`:56-67`, `:347-399`). Il n'existe ni transpose global, ni transpose de track, ni changement d'octave dans cette chaîne.

### Notes directes, chords, rolls, arp et NoteFx

- Les accords sont les quatre valeurs NOTE des quatre voix PLAY ; le scheduler les traite indépendamment dans `0..127`.
- Le roll répète la même note, sans la transposer.
- Le NoteFx ARP est commun au clavier et au séquenceur. Sa plage ajoute uniquement des octaves positives en `uint16_t`; au-dessus de 127 il retombe sur la note source (`Src/NoteFx/note_fx_arp.c:54-78`). Il ne provoque aucun underflow bas.
- Le live-record capture les notes MIDI canoniques via les mêmes IDs PLAY.

Le scheduler appelle le VCA mixer seulement quand `resolved.supports_vca_gate != 0`; pour le Stream actuel, le Note Off appelle donc `brick6_sampler_runtime_note_off(track)` directement (`Src/Seq/seq_play_scheduler.c:554-565`, `:626-649`).

## 4. Calcul de pitch

### Formule actuelle du Stream

En mode stretch OFF :

```text
played_note                      -> copiée puis ignorée/écrasée à 60
clip.pitch_semitones             -> ignoré si stretch_mode != SHIFTER
timing_ratio                     -> 1.0
sample-rate correction           -> absente (entrée produit forcée à 48 kHz)
step_q16                         = round(1.0 * 65536) = 65536
```

En mode sync/stretch sans shifter :

```text
timing_ratio = clamp(source_duration / target_duration, 0.5, 2.0)
             ou clamp(project_bpm / source_bpm, 0.5, 2.0)
step_q16     = round(timing_ratio * 65536)
```

En mode SHIFTER :

```text
pitch_ratio      = clamp(2^(clip_pitch / 12), 0.5, 2.0)
reader step_q16  = timing_ratio_q16
shifter correction = pitch_ratio / timing_ratio
```

Dans les trois cas, la note jouée n'apparaît pas. Les fonctions sont `brick6_sampler_runtime_clip_resolve_timing_ratio_q16()`, `brick6_sampler_runtime_clip_pitch_ratio_q16()`, `brick6_sampler_runtime_clip_configure_shifter()` et `brick6_sampler_runtime_clip_start_playback()` (`Src/Core/brick6_sampler_runtime.c:1105-1133`, `:1180-1311`, `:1314-1457`).

### Types, plages, clamps et unités

| Étape | Symbole/type | Plage/clamp | Unité/arrondi |
|---|---|---|---|
| Note source | `uint8_t note` | appelants : 0..127 | MIDI |
| Note de voix | `uint8_t voice->note` | écrasée à 60 | MIDI, inutilisée par Stream |
| Pitch clip | `float pitch_semitones` | -12..+12 | demi-tons |
| Timing | `float ratio` puis `uint32_t timing_ratio_q16` | 0.5..2.0 | Q16, `+0.5f` |
| Pitch shifter | `float ratio` puis `uint32_t pitch_ratio_q16` | 0.5..2.0 | Q16, `+0.5f` |
| Pas lecteur | `uint32_t play_plan.step_q16` | timing uniquement | frames source/frame sortie, Q16 |
| Pas flottant miroir | `float step_signed` | positif | `step_q16 / 65536` |
| Sample rate source | `uint32_t desc->sample_rate` | produit actuel : 48000 | Hz, non consommé par le pas |

Reverse n'est pas exposé dans le plan Stream courant (`direction=0`). Stretch et shifter modifient le timing/pitch du clip mais ne doivent pas, dans la cible, supprimer silencieusement la transposition de note.

### Exemples actuels, stretch OFF, clip pitch 0, fichier 48 kHz

Faute de racine Stream, la colonne « racine » ci-dessous utilise seulement la constante observée 60 comme point de comparaison, pas comme contrat validé.

| Entrée | Delta qui devrait être évalué | Ratio musical attendu si racine 60 | Ratio actuel | `step_q16` actuel |
|---|---:|---:|---:|---:|
| 60 | 0 | 1.000000 | 1.000000 | 65536 |
| 48 | -12 | 0.500000 | 1.000000 | 65536 |
| 59 | -1 | 0.943874 | 1.000000 | 65536 |
| 61 | +1 | 1.059463 | 1.000000 | 65536 |
| 72 | +12 | 2.000000 | 1.000000 | 65536 |
| 0, minimum séquenceur | -60 | 0.031250 | 1.000000 | 65536 |
| 127, maximum | +67 | 47.945826 | 1.000000 | 65536 |

La limite Q16 générique existante `brick6_sampler_runtime_ratio_to_q16()` est `0.03125..32` (`:1534-1545`) : avec une racine 60, elle couvrirait exactement la note 0 vers le bas mais saturerait avant le ratio théorique de la note 127. La plage musicale cible doit donc être décidée et testée explicitement ; le clamp ne doit jamais être appliqué au delta signé avant `powf`.

### Formule cible recommandée

Après définition explicite de `root_note` :

```text
int16_t note_delta = (int16_t)played_note - (int16_t)root_note
float semitones = (float)note_delta + clip_pitch
float musical_ratio = 2^(semitones / 12)
float sample_rate_ratio = source_sample_rate / audio_sample_rate
float read_ratio = sample_rate_ratio * musical_ratio
```

Le contrat stretch doit ensuite être explicite :

- repitch/timing couplé : composer le ratio temporel et le ratio musical dans le lecteur ;
- time-stretch découplé : le lecteur porte le timing et le shifter corrige vers le `musical_ratio` final, sur le modèle actuel `pitch_correction = desired_pitch / timing_ratio` ;
- aucun mode ne peut remplacer `played_note` par une constante.

Le retrigger doit reconstruire `pitch_ratio_q16`, `play_plan.step_q16`, `step_signed` et la correction shifter à partir de la nouvelle note avant de rendre le premier bloc. Le code actuel reconstruit le plan, mais avec une note constante ; il ne conserve donc pas littéralement un ancien ratio, il reconstruit toujours le mauvais ratio indépendant de la note.

## 5. Cycle de vie Stream

### Cycle actuel

```text
Note On
-> note copiée dans voice
-> clip_start_playback
-> ancien flux: tail declick 16 samples + stop/reset reader
-> nouveau plan timing-only, note forcée à 60
-> sample_cache_start_voice_at + bind reader
-> rendu Stream vers buffer stéréo
-> mixer_submit_external_stereo
-> filtre / volume / pan, VCA bypassé
-> Note Off
   gate mode: clip_stop_playback immédiat + tail declick 16 samples
   launch mode: Note Off ignoré
```

Le declick n'est pas une enveloppe musicale : `brick6_sampler_runtime_begin_declick_tail()` copie le dernier échantillon et le décroît sur 16 samples ; le retrigger ajoute aussi un fade-in de 16 samples (`Src/Core/brick6_sampler_runtime.c:35-37`, `:410-527`).

`brick6_sampler_runtime_note_off()` arrête immédiatement le reader en mode gate, ou ne fait rien si `play_mode != 0` (`:3846-3865`). `brick6_sampler_runtime_render_stream_track()` arrête également la source si l'état clip n'est plus PLAYING, et libère naturellement à fin de fichier ou underrun (`:5789-5849`, `:4418-4564`). Aucun état Stream n'attend une fin de VCA.

### Contraintes streaming

- Le rendu Stream est mono-voix par track (`g_sampler_voice[track]`). Un retrigger vole/repart cette voix avec le declick existant.
- Les pages sont demandées par le reader/cache tant que la voix reste active. Continuer pendant la release n'introduit pas un nouveau chemin DMA ; cela prolonge seulement l'activité existante et son budget SD pour la durée de la release.
- Une fin de fichier pendant la release doit arrêter/libérer la source normalement ; le VCA peut finir en silence.
- Un underrun conserve la politique actuelle d'arrêt borné. Il ne faut pas retenir artificiellement pages ou reader après erreur.
- Un retrigger pendant release doit clore/voler proprement l'ancienne source via le declick existant, reconstruire le pitch, réarmer le VCA selon `PARAM_ENV_RETRIG_VCA`, puis réserver le nouveau début de flux.
- Le coût maximal supplémentaire est un Stream mono qui continue pendant sa release ; aucune polyphonie Stream nouvelle n'est recommandée.

### Cycle cible

```text
Note On
-> calcul complet du pitch depuis la note canonique
-> démarrage/retrigger du reader
-> mixer_track_vca_note_on
-> source -> filtre -> VCA commun (une fois) -> volume/pan
Note Off
-> mixer_track_vca_note_off
-> marquer la voix Stream release_pending, sans stopper le reader
-> continuer source/pages tant que VCA requiert une source
-> EOF/underrun: libération anticipée normale, VCA finit en silence
-> VCA IDLE: stop reader, libération cache/pages, retour IDLE
```

`mixer_track_vca_requires_source()` est réutilisable comme autorité de tail (`Src/Audio/mixer.c:2276-2285`), comme pour les synthés. Pour le Stream, son appel doit être combiné avec un état `release_pending`; il ne doit pas empêcher EOF/underrun de libérer une source devenue inutile.

## 6. Intégration VCA

### Comparaison directe

| Aspect | Sampler RAM | Sampler Multi | Stream actuel | Stream cible recommandée |
|---|---|---|---|---|
| Note clavier | MIDI canonique vers `trigger_note_velocity` | MIDI canonique vers résolveur de zone | MIDI canonique reçue puis perdue | Même note canonique conservée jusqu'au ratio |
| Note séquenceur | Même endpoint terminal | Même endpoint terminal | Même endpoint terminal, puis perdue | Identique au clavier, aucun patch scheduler |
| Note racine | 60 implicite | explicite par zone | absente ; 60 de bookkeeping | autorité explicite décidée/documentée |
| Calcul pitch | `(srcRate/48k)*2^((note-60+tune)/12)` | `2^((note-root)/12)`, rates importés forcés à 48 kHz | timing de clip seulement ; pitch clip seulement en shifter | delta signé + tune/clip + sample-rate, composé explicitement avec stretch |
| Note Off | gate VCA ; source gardée | `release_pending`, mais source arrêtée au bloc suivant | stop/declick immédiat ou ignoré en launch | gate off VCA, reader maintenu pendant release |
| Source pendant release | oui, jusqu'à VCA IDLE | non, contrat Multi distinct/défectueux | non | oui si disponible et VCA active |
| VCA mixer | oui, mono | oui au mixer mais tail inaudible après stop source | non armé, donc bypass | oui, exactement une fois |
| ENV visible | oui | oui | VCA masquée | oui, banque ADSR existante |
| Persistence | structures génériques existantes | idem | champs présents mais inaccessibles | réutilisation sans changement de format |

Le Multi est uniquement une comparaison : son arrêt de source au début de `brick6_sampler_render_multi()` (`Src/Core/brick6_sampler_runtime.c:4134-4152`) ne doit pas être copié dans le Stream.

### Modifications nécessaires lors de la passe de correction

1. `track_runtime_supports_vca_gate()` : exclure seulement Looper, plus Stream.
2. `param_registry_neutralize_vca_runtime_if_invalid()` : considérer Stream comme sampler VCA valide (`Src/Param/param_registry_transition.c:362-397`).
3. UI ENV : aucune nouvelle page ; la résolution existante affichera automatiquement VCA dès que la capacité sera vraie.
4. Clavier/scheduler : aucune nouvelle API de gate ; les appels existants suivront automatiquement `supports_vca_gate`.
5. Stream Note Off : ne plus appeler le stop immédiat lorsque le VCA commun porte la release ; marquer le lifecycle source.
6. Render Stream : stopper le reader lorsque la release est terminée, tout en conservant les arrêts EOF/underrun.
7. Conserver le declick 16 samples pour steal/retrigger/arrêt forcé ; ne pas le superposer comme une seconde enveloppe de Note Off normale.

Le correctif `d3414fda8` a déjà rendu le backend MIX accessible aux paramètres VCA de domaine ENV. Une fois la capacité Stream autorisée, `PARAM_VCA_ATTACK/DECAY/SUSTAIN/RELEASE` et `PARAM_ENV_RETRIG_VCA` peuvent réutiliser `param_backend_apply_mix_track()` sans backend Stream spécifique (`Src/Param/param_registry_tone_backends.c:60-119`, `Src/Param/param_registry_backends.c:1500-1561`).

### UI, p-lock, modulation, clipboard, undo et persistence

- `track_sound_state_t` contient déjà A/D/S/R et retrigger VCA, avec defaults (`Inc/Core/track_sound_state.h:62-67`, `Src/Core/track_sound_state.c:43-48`).
- ENV possède déjà les quatre slots VCA et le p-lock set ENV ; rendre le paramètre autorisé suffit à la surface existante.
- Les destinations de modulation VCA existent et projettent directement vers le mixer (`Src/Mod/mod_destination_catalog.c:94-98`, `:386-400`). Leur liste track-aware les rendra disponibles avec la capacité.
- Clipboard/clear et snapshots capturent/réappliquent les paramètres par ID et `track_sound_state`; l'undo fondé sur snapshots les transporte déjà.
- `track_snapshot_t`, Patch v3 et Kit v3 embarquent déjà `track_sound_state_t`; Pattern v4 et Project v4 transportent les snapshots/sets ENV existants.
- Aucun nouvel ID, champ ou changement Pattern v4 / Project v4 / Patch v3 / Kit v3 n'est requis pour activer le VCA Stream.

Le pitch racine est le seul point susceptible d'appeler une décision de données. La recommandation atomique minimale est une racine de contrat constante explicitement nommée, donc sans format. Si le produit exige une racine réglable par sample/track, ce serait un chantier de paramètre et persistence séparé, à ne pas introduire implicitement dans le correctif VCA.

## 7. Plan d'implémentation

### Lot A — contrat et tests de note canonique

- Ajouter des tests statiques/unitaires prouvant que clavier et séquenceur transmettent la même valeur MIDI terminale au Stream.
- Formaliser le type de delta en `int16_t`/`int32_t` avant soustraction.
- Nommer l'autorité de racine Stream ; ne pas ajouter de paramètre persistant sans décision produit.

Ce lot peut être atomique avec le lot B si la racine reste une constante de contrat.

### Lot B — pitch Stream

- Empêcher `clip_start_playback()` d'écraser la note déclenchante.
- Centraliser un calcul `played note -> semitones signés -> ratio musical -> correction sample-rate -> Q16` réutilisable au retrigger.
- Composer ce ratio avec chaque mode stretch selon une table de comportement explicite.
- Recalculer `play_plan.step_q16`, `step_signed`, `pitch_ratio_q16` et la correction shifter à chaque Note On.
- Garder reverse, stretch et pitch clip orthogonaux à l'identité de note.
- Ne pas modifier le scheduler.

Ce lot doit rester séparé du VCA pour isoler la validation de justesse musicale.

### Lot C — capacité VCA commune

- Autoriser Stream dans `track_runtime_supports_vca_gate()` et la transition VCA.
- Laisser UI, registre, p-locks et modulation réutiliser leurs surfaces existantes.
- Vérifier que le mixer applique le gain une seule fois sur la lane stéréo Stream.

Ce lot peut être atomique au niveau capacité/UI/backend, mais ne doit pas être livré sans le lot D : sinon Note Off coupe la source avant la release.

### Lot D — lifecycle Note Off/release

- Ajouter/réutiliser un état borné `release_pending` sur la voix Stream.
- Sur Note Off, relâcher le gate mixer sans stopper le reader.
- Continuer le rendu et les pages jusqu'à VCA IDLE, EOF ou underrun.
- À VCA IDLE, exécuter le stop/libération existant.
- Conserver le declick pour retrigger/steal/panic/changement de type.
- Définir explicitement le comportement du `play_mode` latched : recommandation, ne pas contourner la release dans le mode mélodique.

Les lots C+D forment une unité fonctionnelle indivisible.

### Lot E — validations

- Tests host/static du ratio et du lifecycle.
- Builds Release Low-Cost et Premium.
- Validation audio matérielle sur SD et mesure de tail/retrigger.

## 8. Tests requis

### Note et pitch

- Même track/sample, même note MIDI depuis clavier et séquenceur : même `step_q16` et même fréquence mesurée.
- Racine, racine -1, racine +1, racine -12, racine +12.
- Note séquenceur minimale 0 et maximale 127 ; aucun wrap, NaN ou ratio nul.
- Notes via les quatre voix/chord, roll et NoteFx ARP ; la note terminale doit rester l'autorité.
- Octave clavier `-4..+4` aux limites MIDI, avec clamp uniquement sur la note canonique finale.
- Retrigger avec deux notes différentes dans le même bloc et sur deux blocs successifs ; aucun ratio ancien.
- Clip pitch négatif/positif combiné avec note sous/au-dessus de la racine.
- Chaque mode stretch : table attendue timing/pitch et preuve que la note n'est jamais ignorée.
- Fichiers 48 kHz : ratio nominal exact. Fichiers 44,1/96 kHz : tant que le contrat import reste inchangé, refus/conversion explicite ; test unitaire du facteur `source_rate/audio_rate` avec des descripteurs synthétiques.

### VCA et lifecycle

- ENV affiche VCA sur Stream ; A/D/S/R modifient `track_sound_state` et le mixer.
- Note On déclenche Attack/Decay/Sustain ; Note Off déclenche Release.
- Release 0 et release longue ; la source reste active seulement autant que nécessaire.
- Fin de fichier pendant Attack, Sustain et Release.
- Underrun pendant release : arrêt borné, pas de page/owner bloqué.
- Retrigger pendant Attack, Sustain et Release, avec retrigger soft/hard.
- Panic/all-notes-off, mute, changement de type et changement de sample : pas de note ou reader bloqué.
- Mesure mixer prouvant une seule application VCA, sans ADSR interne Stream.
- Declick de steal/retrigger conservé, sans tail 16 samples remplaçant la release ADSR.

### État et persistence

- p-lock ENV A/D/S/R et `PARAM_ENV_RETRIG_VCA`, application puis restauration de base.
- Modulation VCA, puis release de modulation sans altérer la base canonique.
- Clipboard page/ensemble, clear, paste et undo sur Stream.
- Roundtrip Pattern v4, Project v4, Patch v3 et Kit v3 avec Stream + ADSR VCA.
- Snapshot/restore et changement RAM <-> Stream : neutralisation/réapplication correcte du gate.

### Intégration

- Builds Release Low-Cost et Premium.
- Validation audio matérielle : pitch au fréquencemètre/analyseur, tail ADSR, retrigger, EOF et charge SD pendant release.
- Non-régression Sampler RAM ; Sampler Multi seulement en comparaison, sans correction opportuniste dans le même patch.

## Validations statiques de l'audit

La chaîne a été recoupée statiquement avec les contrats existants de runtime, ENV/VCA, p-lock et persistence. Les validations ciblées suivantes passent sur le snapshot audité :

- `env_ownership_validation.ps1` : PASS, ENV 25/256, MIX 4, MOD 12, Pattern/Project v4, Kit v3 ;
- `vca_dispatcher_validation.ps1` : PASS, domaine ENV, ressource/backend MIX, état canonique et p-lock ENV ;
- `pattern_persistence_classification_validation.ps1` : PASS ;
- `seq_play_scheduler_pair_validation.ps1` : PASS, paires et retriggers ordonnés.

Aucun build n'est requis pour ce rapport audit-only. Les validations globales `stack_morph_validation` et `synth_voice_budget_validation` sont hors périmètre.
