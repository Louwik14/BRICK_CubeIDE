# Z3 - Cartographie opérationnelle des écritures param (passe ciblée)

Date: 2026-04-14
Périmètre: chemins d'écriture réellement appelés pour Z3 (sans ré-audit global).

## Chemins réels d'écriture

| Entrée réelle (caller) | API appelée | Track-aware | Store/Staging touché | Apply | Dépendance explicite Z2 | RT / non-RT |
|---|---|---|---|---|---|---|
| `ui_param_set_active_track_value` (`Src/UI/ui_param.c`) param non track-scoped | `param_set` | Non | `active` via `param_store_set_active` | Immédiat (`desc->apply`) | Non directe | Non-RT (UI) |
| `ui_param_set_active_track_value` (`Src/UI/ui_param.c`) param track-scoped | `param_registry_apply_track_value` + `param_store_set_active` | Oui (track actif UI) | `active` touché (pas staging) | Immédiat (dispatch runtime) | Oui (`track_runtime_get_param_rule`, refresh/status dans apply) | Non-RT (UI) |
| UI clipboard/clear/paste (`Src/UI/ui_core.c`) | `param_registry_apply_track_value` (batch begin/end) | Oui | Pas de staging explicite; cache runtime/état filtre potentiellement touchés selon domaine | Immédiat (batch ne diffère pas l'apply, réduit surtout refresh) | Oui | Non-RT (UI) |
| P-lock apply/restore (`seq_param_iface_apply_lock`, `seq_param_iface_restore_base` via `seq_boundary_engine`) | `param_registry_apply_track_value` | Oui | Pas de staging/store global explicite; runtime cache/état filtre selon domaine | Immédiat à la frontière de step | Oui (`seq_param_iface_is_param_supported` + rule/status track_runtime) | Path séquenceur (cadencé runtime; pas API UI) |
| Modulation LFO (`mod_lfo_process_control_tick`) | `param_registry_apply_track_value_rt_fast` | Oui | Ne touche pas `param_store`; touche runtime effectif/cache couleurs | Immédiat (control tick) | Oui (`track_runtime_get_param_rule` dans `_rt_fast`, support dest) | RT (appelé depuis `brick6_audio_runtime_dsp`) |
| Restore pattern/projet (`pattern_live_apply_snapshot` appelé par `project_v1_apply_snapshot/load/boot restore`) - domaine track | `param_registry_apply_track_value` | Oui | Pas de staging; runtime/cache/état filtre selon domaine | Immédiat pendant apply snapshot | Oui | Non-RT (load/apply) |
| Restore pattern/projet - global | `param_set` | Non | `active` touché | Immédiat (`desc->apply`) | Non directe | Non-RT |
| Restore pattern/projet - LFO | `mod_lfo_v1_set_track_param` puis `param_registry_apply_track_value(PARAM_LFO*)` | Oui | Pas de staging/store global | Immédiat (config LFO) | Faible (mapping LFO dans Z3, pas rule Z2 pour la config elle-même) | Non-RT |
| `control_router_set_param` (`Src/Param/control_router.c`) | `param_set` + `param_store_set_staging` + `param_store_commit_if_block_advanced` | Non | `active` + `staging` + commit bloc | Mixte: immédiat (`param_set`) puis différé (commit bloc) | Non directe | Non-RT (si appelé) |
| `param_store_commit_if_block_advanced` (`Src/Param/param_store.c`) | boucle `param_set(staging[i])` | Non | lit staging, réécrit active | Différé (à l'avance de bloc audio) | Non directe | Non-RT (fonction de service/routeur) |

## Notes de preuve ciblées

- `param_set` fait clamp + `param_store_set_active` + `desc->apply` (et legacy mix direct).  
- `param_registry_apply_track_value` fait clamp, route LFO via `mod_lfo_v1_set_track_param`, puis dispatch selon règles/runtime track (avec fallback `param_set` si `GLOBAL_ALLOWED`).  
- `param_registry_apply_track_value_rt_fast` est un chemin court sans refresh lourd, utilisé par modulation.
- `control_router_set_param` est défini mais aucun caller trouvé dans `Src/` sur cette passe.

## Doubles chemins / ambiguïtés d'autorité

1. `PARAM_LFO*` en restore (`pattern_live_apply_snapshot`): double écriture de la même vérité de config LFO
- chemin A: `mod_lfo_v1_set_track_param`
- chemin B: `param_registry_apply_track_value(PARAM_LFO*)` qui reroute vers `mod_lfo_v1_set_track_param`
- effet: redondance explicite, même autorité finale Z3 LFO.

2. Paramètres track-scoped UI: runtime track + store global actif
- `ui_param_set_active_track_value` fait `param_registry_apply_track_value(..., track actif)` puis `param_store_set_active(param, clamped)`.
- effet: coexistence d'une vérité runtime track-aware et d'un miroir global `active[]` pour le même `param_id`.

3. Coexistence base value vs valeur modulée en runtime
- base: UI/seq/restore via `param_registry_apply_track_value`
- modulation: écrasement temporaire via `param_registry_apply_track_value_rt_fast`
- effet: deux producteurs valides sur la même cible runtime (arbitrage temporel par tick LFO + restore base lors release).

4. `control_router_set_param`: immédiat + différé sur le même param
- appelle `param_set` immédiatement, puis staging/commit bloc pouvant réécrire via `param_set`.
- en plus, pas de caller local trouvé: chemin potentiellement dormant mais ambigu s'il est activé.

## Prochaine passe recommandée

1. Établir une matrice d'autorité par famille de paramètres Z3 (`global-only`, `track-aware`, `LFO-owned`) et marquer le rôle exact de `param_store.active` (source UI uniquement vs vérité runtime).  
2. Trancher et documenter le chemin unique de restore LFO (`mod_lfo_v1_set_track_param` seul ou via `param_registry_apply_track_value` seul) pour supprimer la redondance.  
3. Vérifier si `control_router_set_param` doit être supprimé, branché, ou borné (sinon maintenir explicitement comme chemin inactif).  
4. Ajouter un petit jeu de tests d'intégration ciblés sur collisions d'autorité: UI + plock, restore + LFO, modulation + restore base.
