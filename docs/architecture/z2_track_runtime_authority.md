# Z2 - Track Runtime Authority

## 1. Périmètre
Zone opérationnelle:
- Src/Core/track_runtime.c
- Inc/Core/track_runtime.h

Zone élargie pour preuve de contrats:
- Inc/UI/ui_core.h (API source family/type)
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
- track_runtime_refresh_all(): recalcule le contexte runtime complet de toutes les tracks.

Autorités secondaires dans la zone:
- track_runtime_invalidate_all(): invalide globalement (dirty flag).
- track_runtime_bind_ctx(): décide engine/instance/bind_state/reason (appel interne).
- track_runtime_get_param_rule() + track_runtime_get_effective_param_status(): autorité de statut param runtime.

Il n’existe pas de seconde autorité active in-tree pour ce binding runtime.

## 3. API entrantes
Initialisation / mutation:
- track_runtime_init()
- track_runtime_invalidate_all()
- track_runtime_refresh_track()
- track_runtime_refresh_all()

Lecture / résolution:
- track_runtime_get_ctx()
- track_runtime_is_audio_routable()
- track_runtime_get_mix_target_track()
- track_runtime_get_logical_track_for_mix_track()
- track_runtime_resolve_filter_target_track()
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

Z2 dépend de la config UI pour construire son état effectif.

## 5. États structurants possédés
- g_track_runtime_ctx[SEQ_TRACK_COUNT] (track_runtime_ctx_t)
  - Possède: track_id, mix_track_id, family, type, engine, instance_id, bind_state, bind_reason, flags.
  - Écriture: refresh_all/init uniquement.
  - Lecture: tous les consommateurs inter-zones via get_ctx et helpers.

- g_track_runtime_refresh_needed (dirty flag global)
  - Écriture: init/invalidate_all/refresh_all.
  - Lecture: refresh_track et get_logical_track_for_mix_track.

## 6. Flux runtime
1) Source config:
- refresh_all lit family/type depuis UI.

2) Invalidation:
- changements structurants UI (family/type) appellent invalidate_all.

3) Refresh:
- refresh_track(track) fait full refresh si dirty.
- refresh_all recalcule toutes les tracks.

4) Binding:
- map UI family/type -> runtime family/type
- allocation mix_track
- bind engine/instance avec quotas et reasons
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
- Invalidation explicite; pas de refresh implicite généralisé.
- Résolution strictement track-aware.
- Master/Buffer reste un bind runtime dédié (family master/type buffer).

## 8. Dépendances inter-zones
Entrées de Z2:
- Z5 UI Interaction (source family/type)

Sorties de Z2:
- Z1 Audio Hard-RT Mix (engine/mix target/routability)
- Z3 Param Control (status/domain gating)
- Z4 Seq Clock Scheduler (play status/gating/routing)
- Z5 UI (labels/disponibilité pages)
- Z6 Persistence (refresh après restore snapshot)

## 9. Dette technique observée
- Couplage direct à UI comme fournisseur de vérité family/type.
- Discipline refresh non homogène:
  - get_ctx/get_effective_param_status ne refresh pas.
  - get_logical_track_for_mix_track refresh implicitement si dirty.
- Présence d’un shim legacy runtime_target potentiellement confus en doc, bien que hors chemin opérationnel.

## 10. Impact éventuel sur la cartographie globale
- Z2 confirmé comme noyau d’autorité transversal.
- Pas de split nécessaire.
- Master/Buffer ne justifie pas une zone séparée: il consomme la politique de bind de Z2.
