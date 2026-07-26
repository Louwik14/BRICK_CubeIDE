# Z2 - Track Runtime Authority

## 1. Périmètre
Zone opérationnelle:
- Inc/Core/track_state.h
- Src/Core/track_state.c
- Src/Core/track_runtime.c
- Inc/Core/track_runtime.h

Zone élargie pour preuve de contrats:
- Inc/UI/ui_core.h (surface de mutation / mirror lecture family/type)
- Src/UI/ui_core.c (invalidation explicite)
- Src/Param/param_registry.c
- Src/Seq/seq_play_scheduler.c
- Src/Keyboard/keyboard_engine.c
- Src/Core/brick6_audio_runtime.c
- Src/Mod/mod_lfo_v1.c
- Src/Storage/pattern_live_ram.c

Exclusions:
- Inc/Core/runtime_target.h: shim legacy inline, non consommé par le code in-tree Src/*.

## 2. Autorité(s) de vérité
Autorité principale:
- track_state: autorite par-track pour family/type/midi (source structurelle).
- track_runtime_refresh_all(): recalcule la projection runtime complete a partir du track_state.

Autorités secondaires dans la zone:
- track_runtime_invalidate_all(): invalide globalement (dirty flag).
- track_runtime_bind_ctx(): décide engine/instance/bind_state/reason (appel interne).
- track_runtime_get_param_rule() + track_runtime_get_effective_param_status(): autorité de statut param runtime.
- `track_runtime_tone_slot_to_param()` / `track_runtime_tone_param_to_slot()`: autorité du mapping TONE local au `track_runtime_type` effectif.

Il n’existe pas de seconde autorité active in-tree pour ce binding runtime.

## 3. API entrantes
Initialisation / mutation:
- track_runtime_init()
- track_runtime_invalidate_all()
- track_runtime_refresh_track()
- track_runtime_refresh_all()

Lecture / résolution:
- track_runtime_get_ctx()
- track_runtime_get_descriptor()
- track_runtime_resolve_track()
- track_runtime_is_ui_ensemble_available()
- track_runtime_is_audio_routable()
- track_runtime_get_mix_target_track()
- track_runtime_get_logical_track_for_mix_track()
- track_runtime_resolve_filter_target_track()
- track_runtime_get_midi_channel_1_16()
- track_runtime_get_midi_channel_zero_based()
- track_runtime_get_midi_source()
- track_runtime_get_effective_param_status()
- track_runtime_get_param_rule()
- track_runtime_get_voice_mode()
- track_runtime_get_play_voice_count()

Callers principaux:
- UI (ui_core, ui_page_template_play)
- Param (param_registry)
- Seq (seq_play_scheduler, seq_param_iface, seq_output_guard, seq_live_rec_capture)
- Audio runtime (brick6_audio_runtime)
- Keyboard (keyboard_engine)
- Modulation (mod_lfo_v1)
- Storage apply (pattern_live_ram)

## 4. API sortantes
Dépendances sortantes de Z2:
- ui_get_track_family(track)
- ui_get_track_type(track)
- ui_track_family_is_input(family)

Z2 dépend de `track_state` pour construire son état effectif.

## 5. États structurants possédés
- g_track_runtime_ctx[SEQ_TRACK_COUNT] (track_runtime_ctx_t)
  - Possède: track_id, mix_track_id, family, type, engine, instance_id, bind_state, bind_reason, flags.
  - Écriture: refresh_all/init uniquement.
  - Lecture: tous les consommateurs inter-zones via get_ctx et helpers.

- g_track_runtime_refresh_needed (dirty flag global)
  - Écriture: init/invalidate_all/refresh_all.
  - Lecture: refresh_track uniquement.

- g_track_runtime_logical_track_by_mix_track[MIXER_MAX_TRACKS]
  - Ecriture: Z2 reconstruit la table apres refresh global ou local.
  - Lecture: hot path mixer via `track_runtime_get_logical_track_for_mix_track()`.
  - Role: projection inverse O(1) `mix_track -> logical_track`, sans scan dans l'IRQ.

## 6. Flux runtime
1) Source config:
- refresh_all lit family/type/midi depuis `track_state`.

2) Invalidation:
- les mutations structurelles de `track_state` appellent invalidate_all via les call sites autorisés.

3) Refresh:
- refresh_track(track) fait full refresh si dirty.
- refresh_all recalcule toutes les tracks.
- Les helpers/getters de lecture ne déclenchent pas de refresh implicite.

4) Binding:
- map UI family/type -> runtime family/type
- allocation mix_track
- bind engine/instance avec quotas et reasons
- `Synth/Wave` est le seul type Synth actif; il suit le meme contrat stable que les engines per-track: `instance_id == track_id` et `BRICK6_BRAIDS_MAX_INSTANCES == SEQ_TRACK_COUNT`. Il n'y a plus d'allocation dynamique par ordre de scan; une autre track qui change de family/type ne peut pas deplacer l'instance Wave d'une track existante.
- Reset Wave: reset local par `runtime_instance` seulement quand l'owner reel de l'instance `track_id` change, puis re-projection immediate des params TONE depuis `track_tone_sound_state` pour le nouvel owner; une instance inchangee n'est ni reset ni replay inutilement.
- binding Drum: `instance_id` stable par track logique (`instance_id == track_id`), pour eviter toute migration d'etat inter-track lors des reconfigurations de cardinalite Drum
- calcul flags capabilities

5) Consommation:
- Audio: sélection des engines à rendre + mix target.
- Param: autorisation/blockage de domaines par track.
- Seq/Keyboard/Mod: routing, voice mode, filter/mix target.

6) Timing:
- boot + changements UI + appels runtime des consommateurs.

## 7. Invariants à ne pas casser
- Unicité d’autorité de binding: uniquement track_runtime.
- Séparation track logique vs lane physique mixer.
- Invalidation explicite; refresh explicite par les call sites avant lecture runtime.
- Interdiction de refresh implicite dans les wrappers de convenance (UI/Param/Seq/Audio): la discipline reste centralisee au call site appelant.
- Résolution strictement track-aware.
- Le contrat "track -> capacités -> ensembles UI exposables" est matérialisé dans Z2 (`track_runtime_descriptor_t` + `ui_ensemble_mask`) et consommé par Z5 sans redécision distribuée.
- Les couches d'exécution (scheduler/param apply) lisent le channel MIDI via Z2 (`track_runtime_get_midi_channel_*`) au lieu d'un couplage direct à l'état UI.
- Le resolver structurel pur est explicite: `track_runtime_resolve_track()` renvoie une vue résolue (descriptor + cibles runtime valides) sans logique UI contextuelle.
- Pour le set `TONE`, le mapping `slot -> param` est local au type runtime effectif; il ne doit pas être dupliqué dans Z4/Z5/Z3.

## 8. Dépendances inter-zones
Entrées de Z2:
- Z5 UI Interaction (surface de mutation family/type)
- Z5 `ui_system_sync_internal` orchestre la reconfig runtime (invalidate/enables/notify) sans porter de sync UI.

Sorties de Z2:
- Z1 Audio Hard-RT Mix (engine/mix target/routability)
- Z3 Param Control (status/domain gating)
- Z4 Seq Clock Scheduler (play status/gating/routing)
- Z5 UI (labels/disponibilité pages)
- Z6 Persistence (refresh après restore snapshot)

## 9. Dette technique observée
- Couplage direct a UI en voie de retrait: `track_state` porte la verite family/type/midi.
- Discipline refresh: explicite côté call sites (refresh_track/refresh_all), sans auto-refresh dans les getters/helpers.
- Présence d’un shim legacy runtime_target potentiellement confus en doc, bien que hors chemin opérationnel.

## 10. Impact éventuel sur la cartographie globale
- Z2 confirme comme noyau d'autorite transversal.
- `track_state` devient la source de verite structurelle par track.
- Pas de split necessaire.

## 11. Contrat MIDI (passe 2 bornee)
- Source de verite structurelle: `track_state` porte family/type/midi; Z5 expose les edits et les labels.
- Mapping runtime explicite: `TRACK_RUNTIME_FAMILY_MIDI` + `TRACK_RUNTIME_TYPE_MIDI`.
- Binding runtime MIDI:
  - `bind_state=BOUND`,
  - `engine=TRACK_RUNTIME_ENGINE_NONE`,
  - `mix_track_id=TRACK_RUNTIME_MIX_TRACK_NONE`.
- Capacites runtime MIDI:
  - `CAN_PLAY` autorise (PLAY/MOD),
  - `CAN_FILTER` et `CAN_SYNTH` non autorises.
- Effets fonctionnels:
  - `track_runtime_is_audio_routable()==0`,
  - `track_runtime_get_mix_target_track()==0`,
  - `track_runtime_resolve_filter_target_track()==0`.
- Invariants inchanges:
  - invalidation explicite `track_runtime_invalidate_all`,
  - refresh explicite `track_runtime_refresh_track/all`,
  - pas de seconde autorite runtime.
- Hors perimetre passe 2:
  - emission Program Change (live/start transport/changement pattern/plock conditionnel) non implementee,
  - dependance explicite a un chantier separe Z3/Z4 (parametres MIDI Program/CC + sequencing conditionnel).

## 12. Contrat de stabilite mix-target (runtime)
- `track_runtime_refresh_all` preserve prioritairement le `mix_track_id` precedent de chaque track quand la lane reste disponible.
- Les lanes fixes Input proto (`Input1..Input3` -> lanes mixer 0..2) sont reservees et ne peuvent plus etre allouees aux tracks non-Input; les engines/musical tracks prennent les lanes dynamiques restantes.
- Objectif contractuel: limiter les rebinding de lane qui cassent la continuite runtime per-lane (MIX/VCA/sends/COLORS) lors des changements family/type.
- Lorsqu'un rebind de lane reste necessaire, la migration/reconciliation du runtime per-lane est une etape explicite aval; Z2 ne garantit pas a lui seul la coherence du state DSP si cette passe n'est pas executee.

## 13. Contrat Sampler v0
- Nouvelle identite runtime branchee:
  - `TRACK_RUNTIME_FAMILY_SAMPLER`,
  - `TRACK_RUNTIME_ENGINE_SAMPLER`,
  - `TRACK_RUNTIME_TYPE_RAM` est l'alias produit de `TRACK_RUNTIME_TYPE_ONE_SHOT` (alias runtime du sampler actuel).
- Binding:
  - autorite conservee dans `track_runtime`,
  - le backend Sampler existant reste reutilise tel quel.
- Gate note/mix:
  - le helper central `track_runtime_supports_vca_gate()` arme le gate VCA pour `Sampler/RAM`, y compris quand `Slice Count` active le slicing grille; note-on ouvre l'attaque VCA et note-off declenche la release VCA.
  - `Sampler/Stream` et `Sampler/Looper` sont exclus du gate VCA mixer.
  - `Sampler/Stream` utilise `brick6_sampler_runtime_note_off()` pour son contrat `Trig`/`Launch`; il ne passe pas par le gate VCA mixer.
- Invariants conserves:
  - pas de pipeline audio parallele,
  - pas de seconde autorite runtime,
  - le futur moteur Sampler reste track-aware et non global.
- Slice RAM:
  - `Slicer` n'est plus un type Track CFG visible; les anciens configs `Sampler/Slicer` sont normalises en `Sampler/RAM`,
  - `Slice Count` est un parametre RAM global, non p-lockable: `Off` garde RAM normal, `2..64` active le slicing grille dans la fenetre globale `Start/End`,
  - `Start`, `End`, `Tune` et `Gain` restent globaux, sans etat par-slice,
  - la grille de slices est reconstruite hors IRQ lors des changements de sample/compteur/fenetre.

## 14. Contrat produit Synth / Sampler
- La famille `Synth` ne porte plus le sampler produit.
- La famille `Sampler` expose `RAM`, `Stream`, `Looper` et `Multi`; `RAM` est porte par l'alias legacy `OneShot`.
- Compat restore: un ancien couple `family=Synth` + `type=Sampler` est remappe explicitement vers `family=Sampler` + `type=RAM` avant le bind runtime.

## 15. Contrat Passe 1 - Descriptor structurel explicite
- Z2 expose un descriptor runtime stable par track:
  - identité runtime (family/type/engine/bind_state/bind_reason),
  - capacités runtime (`flags`),
  - canal MIDI runtime,
  - ensembles UI exposables (`ui_ensemble_mask`).
- Ce descriptor ne remplace pas encore un resolver structurel dédié, mais devient la première autorité explicite partagée entre:
  - Z5 (disponibilité d'ensembles/pages),
  - Z3/Z4 (chemins d'exécution qui ne doivent plus relire UI directement pour des décisions structurelles simples).

## 16. Contrat Passe 2 - Resolver structurel pur
- Nouvelle frontière Z2 explicite:
  - `track_runtime_descriptor_t`: identité/capacités/canal/ensembles exposables.
  - `track_runtime_resolved_track_t`: descriptor + cibles runtime résolues (`mix/filter`), support gate VCA, source/canal MIDI.
- Intention:
  - une fois résolu côté Z2, les call-sites Z3/Z4 consomment cette résolution sans redécider localement les mêmes règles structurelles.
- Migration effective passe 2:
  - Z4 scheduler note-engine consomme `track_runtime_resolve_track`.
  - Z4 live-rec source MIDI consomme `track_runtime_get_midi_source`.
  - Z3 résolution cibles filter/drive pour apply runtime consomme `track_runtime_resolve_track`.
## 17. Contrat queries strictes
- Les queries de projection ne declenchent plus de refresh implicite.
- `track_runtime_get_cached_synth_usage`, `track_runtime_get_descriptor`, `track_runtime_get_revision`, `track_runtime_get_track_revision`, `track_runtime_is_ui_ensemble_available` et `track_runtime_resolve_track` restent des lectures pures.
- `track_runtime_refresh_track` et `track_runtime_refresh_all` sont des commandes explicites de maintenance, appelees avant la query par le call site qui en a besoin.

## 18. Contrat projection UI
- Surface de projection UI recommandee:
  - `track_runtime_get_descriptor`
  - `track_runtime_resolve_track`
  - `track_runtime_is_ui_ensemble_available`
  - `track_runtime_is_audio_routable`
  - `track_runtime_get_mix_target_track`
  - `track_runtime_get_logical_track_for_mix_track`
  - `track_runtime_resolve_filter_target_track`
  - `track_runtime_get_midi_channel_1_16`
  - `track_runtime_get_midi_channel_zero_based`
  - `track_runtime_get_midi_source`
  - `track_runtime_get_effective_param_status`
  - `track_runtime_get_play_voice_count`
  - `track_runtime_get_play_voice_count_from_descriptor`
- Garde technique:
  - `track_runtime_get_track_revision` et `track_runtime_get_revision` servent de gardes de coherence uniquement, apres refresh explicite au bord du consumer.
- Escape hatch:
  - `track_runtime_get_ctx` reste disponible pour les consumers internes qui ont besoin du descriptor runtime complet, mais n'est pas la surface UI preferentielle.
- Politique de refresh:
  - le refresh reste explicite au bord du consumer UI, jamais dans les getters.

## 19. Contrat p-lock / Macro
- Source d'autorite p-lockable: `seq_param_iface_param_to_slot(track,set,param)` puis `seq_param_iface_param_is_supported(track,set,param)`, avec le set derive du domaine fourni par `track_runtime_get_param_rule()`.
- Contrat produit: tout parametre p-lockable est assignable au Hall Mode Macro; Z3 consomme cette meme autorite et ne maintient pas de table d'exclusion MACRO separee.
- Les params FILTER ADSR (`EG Amt`, `Atk`, `Dec`, `Sus`, `Rel`) sont des params `COLORS` / ressource `FILTER`, comme `Cutoff` et `Resonance`; ils sont p-lockables et macro-assignables quand le filter target runtime est autorise.
- Les anciens params `COLORS/CRUNCH` (`Drive`, `Bits`, `Rate`, `Rate2`) ne sont plus dans le domaine COLORS effectif et ne doivent plus recevoir de rule p-lock/macro.

## 20. Contrat Master/FX MacroFX
- Nouvelle identite structurelle: `TRACK_RUNTIME_FAMILY_MASTER` + `TRACK_RUNTIME_TYPE_MASTER_FX`.
- Binding runtime: `TRACK_RUNTIME_ENGINE_NONE`, bind `BOUND`; Z2 reste l'autorite d'identite, tandis que l'insert DSP master est execute en Z1 via les params TONE stockes.
- Ensembles exposes: `CFG`, `COLORS`, `TONE`, `MOD`, `MIX`, `VCA`, `KEYBOARD`, `ARP/ROUT`, `SEQ`; `PLAY` reste masque.

## 21. Contrat refresh local de track
- `track_runtime_invalidate_track(track)` invalide uniquement la track cible; `track_runtime_invalidate_all()` reste reserve aux restore/load/init globaux.
- `track_runtime_refresh_track(track)` reconstruit uniquement le contexte runtime de la track cible quand seul son dirty local est pose.
- `track_runtime_refresh_track(track)` ne fait full refresh que si un dirty global est pose et que l'appel n'est pas en IRQ.
- `track_runtime_refresh_if_dirty()` ne lance pas de `track_runtime_refresh_all()` depuis l'IRQ audio; un dirty observe en IRQ est ignore pour le bloc courant et compte comme diagnostic.
- Les instances Wave restent stables par track logique (`instance_id == track_id`); un refresh local ne reset que l'instance dont l'owner reel change.
- Les params `PARAM_MASTER_FX1_*` a `PARAM_MASTER_FX4_*` sont des params `TONE` track-aware stockes; `DRIVE`, `CRUSH`, `RING`, `CHOP`, `PUMP`, `COMB`, `WOBBLE`, `FREEZE`, `STUTTER` et `COLOR` sont consommes par le DSP master.

## 21. Contrat Drum Plaits direct

- La family/type UI `Drum` reste bindee par Z2 pour les types produit actuels.
- Types Drum valides: `TRACK_RUNTIME_TYPE_DRUM_TRX_BD` comme slot reserve/futur silencieux, et `TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG` comme moteur actif.
- `TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG` est le premier type Drum propre; il reste resolu par `track_runtime`, expose `TRACK_RUNTIME_ENGINE_DRUM`, et mappe en Z1 vers `DRUM_MODEL_ID_BD_ANALOG`.
- `TRACK_RUNTIME_TYPE_DRUM_TRX_BD` peut etre resolu et audio-routable, mais l'execution Z1 le force vers `DRUM_MODEL_ID_NONE` et produit zero.
- Le mapping TONE de `BD_ANALOG` est local au `track_runtime_type` effectif via `track_runtime_tone_slot_to_param()` / `track_runtime_tone_param_to_slot()`.
- Les anciens IDs/types Drum ne sont plus conserves; un type numerique inconnu passe par la validation catalogue generique.
- Les anciens IDs/types `TB3` et `DX7` ne sont plus conserves dans les enums UI/runtime et ne sont pas remappes au restore.
- Aucune nouvelle autorite Drum n'est introduite: PLAY, TONE, COLORS, MIX, MOD et VCA restent resolus par les autorites track-runtime existantes.

## 22. Contrat MIX p-lock/mod hors Master
- Les params MIX track-aware `PARAM_MIX_LEVEL`, `PARAM_MIX_PAN`, `PARAM_MIX_SEND1`, `PARAM_MIX_SEND2` sont autorises uniquement pour les tracks audio non-`Master` disposant d'une lane mixer effective.

## 23. Contrat Sampler/Looper skeleton
- `Sampler/Looper` est un nouveau type dans la famille existante `Sampler`; aucune nouvelle family n'est introduite.
- Z2 expose `TRACK_RUNTIME_TYPE_LOOPER`, bind `BOUND` avec `TRACK_RUNTIME_ENGINE_LOOPER`: le playback est audio-routable via une lane mixer normale, sans sample_pool projet et sans slot Sampler detourne.
- Le hook Z1 record Looper reste un producteur externe pilote par ROUT + writer actif; le playback transient est l'executant runtime dedie `brick6_looper_runtime`.
- `brick6_looper_runtime` garde l'autorite playback par track Looper et utilise des ids page-cache transients hors `sample_pool` (`SAMPLE_PAGE_CACHE_LOOPER_ID_BASE + track_id`) uniquement comme cles RAM/SD internes.
- `ARM/LEN/PLAY` sont autorises uniquement pour `Sampler/Looper`; ils restent hors p-lock PLAY et hors destination LFO tant que le workflow record musical n'est pas branche.
- Le mode `ARP` brut est projete en vue `ROUT` pour `Sampler/Looper`; le routing selectionne des tracks logiques sources et ne cree pas d'autorite audio speciale.
- `PLAY=Off/Auto` reste un parametre de workflow: il ne passe pas par le scheduler note, il pilote seulement l'armement playback Looper au transport via le runtime dedie.
- `Sampler/Looper` n'expose pas et n'applique pas VCA; son niveau reste porte par `PARAM_MIX_LEVEL`.

## 24. Contrat retrait buffer master

- La family `Master` ne conserve plus que le type produit `Master/FX`.
- Le type runtime buffer master, son engine dedie et sa ressource param dediee sont retires.
- Le XFade Looper est maintenant `PARAM_LOOPER_XFADE`, mappe dans les slots TONE de `Sampler/Looper`; son etat DSP reste `audio_xfade`.

## 25. Contrat Sampler/Multi PLAY

- `Sampler/Multi` reste bind via `TRACK_RUNTIME_ENGINE_SAMPLER`, sans second backend runtime.
- Z2 expose une capacite PLAY polyphonique de 4 voix pour `TRACK_RUNTIME_TYPE_MULTI`: les quatre sous-pages PLAY `V1..V4` deviennent autorisees et p-lockables.
- Cette capacite est une projection de grammaire sequencer uniquement; les limites audio restent portees par `brick6_sampler_runtime` (`4` voix Multi par track, `16` voix Multi globales).

## 26. Contrat Apply Kit V1

- L'apply Kit complet passe par une mutation bulk `track_state` puis par le pipeline structurel `param_registry`, qui invalide et rafraichit explicitement `track_runtime`.
- Z2 reste l'unique autorite de binding apres apply; aucune autorite Kit parallele ne conserve family/type, mix target ou capacites runtime.

## 27. Contrat voice group / Patch Poly

- `track_state` porte l'autorite des roles `TRACK_VOICE_GROUP_ROLE_SOLO`, `TRACK_VOICE_GROUP_ROLE_MASTER`, `TRACK_VOICE_GROUP_ROLE_SLAVE`.
- Un groupe valide est contigu: un `MASTER` suivi de `SLAVE` a droite; un `SLAVE` ne peut exister que si sa gauche est `MASTER` ou `SLAVE`.
- Z2 expose seulement les queries/projections `track_runtime_get_voice_group_role`, `track_runtime_get_voice_group_effective_master` et `track_runtime_collect_voice_group_members`; ces getters ne creent pas de groupe et ne rafraichissent pas implicitement le runtime.
- Patch Poly v2 consomme ce modele comme source structurelle: capture depuis master/slaves officiels et apply polyX uniquement vers un groupe cible deja declare de meme largeur.
- Z2 ne stocke aucune reference a un Patch et ne devient pas une autorite de persistence Patch.
- Projection UI: une track `SLAVE` ne publie pas l'ensemble `PLAY`; le `MASTER` reste le seul point d'edition PLAY du groupe.

## Addendum 2026-07-17 - lot 4B catalogue input low-cost

- Le catalogue produit est variant-aware a la compilation: premium conserve `Input1..Input4` comme families produit, tandis que low-cost expose uniquement `Input1`.
- En low-cost, `track_runtime_input_family_mix_track()` ne mappe que `Input1 -> mix lane 0`; `Input2..4` restent des valeurs enum historiques mais ne sont plus des families input disponibles ni des ressources routables.
- La reservation de lanes fixes d'entree suit `UI_AUDIO_INPUT_PROTO_WIRED_COUNT`: low-cost reserve uniquement la lane 0, premium garde les lanes proto 0..2.

## Addendum 2026-07-23 - fermeture des sources input low-cost

- Le catalogue low-cost n'expose que `Input1`; les valeurs historiques `Input2`, `Input3` et `Input4` restent reservees pour la compatibilite des donnees partagees mais ne sont ni selectionnables ni routables.
- Le shim `runtime_target` et la recherche de ressource libre du clipboard appliquent la cardinalite physique de la variante; ils ne peuvent donc pas reintroduire une source input absente.
- La variante premium conserve ses quatre familles input et tous ses mappings existants.

## Addendum 2026-07-25 - identite Synth/Stack

- La family `Synth` expose maintenant deux types distincts: `Wave` et `Stack`.
- `Wave` reste l'identite historique `TRACK_RUNTIME_TYPE_WAVE`, bindee a `TRACK_RUNTIME_ENGINE_WAVE` et au runtime Braids historique `brick6_braids_runtime`; aucune semantique Wave n'est renommee, migree ou reutilisee comme alias Stack.
- `Stack` est une nouvelle identite runtime separee: `TRACK_RUNTIME_TYPE_STACK` bindee par Z2 a `TRACK_RUNTIME_ENGINE_STACK`, avec ownership stable par track logique (`instance_id == track_id`).
- Cette passe ajoute seulement l'identite et le binding structurel Stack. Aucun kernel, runtime audible, parametre TONE Stack, persistence Stack ou chemin de rendu Z1 Stack n'est encore branche.

## Addendum 2026-07-25 - ownership runtime Stack v0

- `TRACK_RUNTIME_ENGINE_STACK` possede maintenant un runtime dedie initialise et resetable, avec instances stables par track logique (`instance_id == track_id`).
- Les chemins note du clavier, du scheduler PLAY et du panic output guard dispatchent Stack vers `brick6_stack_runtime_*`; Wave conserve son dispatch historique vers `brick6_braids_runtime_*`.
- Les resets d'ownership Stack sont separes des resets Wave et ne reappliquent aucun parametre Wave.
- Les commandes Stack hors IRQ passent par la file `brick6_stack_runtime_submit_*`; Z1 les draine via `brick6_stack_runtime_process_commands_from_audio()` avant le rendu Stack.

## Addendum 2026-07-25 - catalogue Stack v0

- Le catalogue modele Stack appartient au runtime Stack, pas a Wave; il ne renomme ni ne reutilise l'identite `TRACK_RUNTIME_TYPE_WAVE`.
- Le changement de modele Stack passe par `brick6_stack_runtime_set_slot_model()` ou par la commande `brick6_stack_runtime_submit_slot_model()`, qui resolvent et stockent le renderer hors boucle sample.

## Addendum 2026-07-25 - slots TONE Stack

- `TRACK_RUNTIME_TYPE_STACK` declare sa propre table de slots TONE dans `track_runtime_tone_slots_stack[]`: niveaux OSC1..3, bruit, puis MODEL/TUNE/TIMBRE/COLOR/PARAM3 pour chaque slot.
- Cette table alimente l'autorisation track-aware, les p-locks TONE et la validation des destinations de modulation continues; elle ne modifie pas `track_runtime_tone_slots_wave[]`.
- Les resets d'ownership Stack reappliquent les bases TONE Stack via `param_backend_reapply_tone_stack_runtime()` apres reset runtime, separement du chemin Wave.

## Addendum 2026-07-25 - config voice group SPREAD/LINK

- `track_state` porte maintenant deux champs de configuration par master de voice group: `voice_group_spread` (`0..1`, defaut `0`) et `voice_group_link` (`OFF/ON`, defaut `OFF`).
- Ces champs appartiennent au modele master/slaves existant: les membres sont resolus par `track_runtime_collect_voice_group_members()` dans l'ordre stable master puis slaves contigus, plafonne a 8 par les consommateurs UI/param.
- Z2 ne cree aucune nouvelle autorite de groupe: les roles `SOLO/MASTER/SLAVE` restent la structure, et SPREAD/LINK sont seulement des attributs de la master effective.
