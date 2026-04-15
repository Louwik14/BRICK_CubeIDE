# Z3 - Param authority matrix
Statut documentaire: Annexe utile (non canonique de zone).
Autorite: le document canonique de zone reste la source de verite.


Date: 2026-04-14
Scope: Z3 write authority only (from proven code paths in `param_registry`, `param_store`, `mod_lfo_v1`, `seq_param_iface`, `pattern_live_ram`, `ui_param`, `ui_core`, `control_router`).

## Family matrix

| Family | Operative authority (current) | Target single API | Allowed temporary exceptions |
|---|---|---|---|
| `global-only` | `param_set(id, value)` | `param_set` | `param_registry_apply_track_value` only when rule is `GLOBAL_ALLOWED` (falls back to `param_set`) |
| `track-aware` | `param_registry_apply_track_value(id, track, value)` | `param_registry_apply_track_value` | `param_registry_apply_track_value_rt_fast` only for modulation ticks; UI may still mirror to `param_store.active[]` |
| `LFO-owned` | `mod_lfo_v1_set_track_param(track, lfo, field, value)` for config + `param_registry_apply_track_value_rt_fast(dest, track, modulated)` for runtime write | Config: `mod_lfo_v1_set_track_param`; Runtime modulation: `param_registry_apply_track_value_rt_fast` | `param_registry_apply_track_value(PARAM_LFO*)` tolerated only as compatibility shim (it reroutes to `mod_lfo_v1_set_track_param`) |
| `legacy-physical` | `param_set` + internal `param_apply_legacy_mix_track_value` for `PARAM_MIX_TRACK0..3_*` | keep `param_set` (no new authority) | None; this path is still alive in code and writes physical mixer tracks directly |

## Operational rules per family

### `global-only`
- Authorized write API: `param_set`.
- Temporarily tolerated API: `param_registry_apply_track_value` only when explicit global fallback path is taken.
- `active[]` role: runtime-facing global value store (not only UI mirror).
- Z2 dependency: not required for direct `param_set`; only indirect when entering through track-aware API fallback.
- RT path allowed: no dedicated RT fast path.

### `track-aware`
- Authorized write API: `param_registry_apply_track_value`.
- Temporarily tolerated API: `param_set` only for truly global params; not a valid authority for track-scoped semantics.
- `active[]` role: UI mirror for current edit context; runtime truth is track runtime/caches/filter state.
- Z2 dependency: required (`track_runtime_get_param_rule`, status/bind checks, refresh path).
- RT path allowed: only via `param_registry_apply_track_value_rt_fast` when source is modulation.

### `LFO-owned`
- Authorized write API:
- LFO config authority: `mod_lfo_v1_set_track_param`.
- LFO modulation authority: `param_registry_apply_track_value_rt_fast`.
- Temporarily tolerated API: `param_registry_apply_track_value(PARAM_LFO*)` as compatibility entrypoint.
- `active[]` role: UI/config mirror; not modulation runtime truth.
- Z2 dependency: config path not Z2-authorized; modulation destination apply uses Z2 rule/domain gate in `_rt_fast`.
- RT path allowed: yes, modulation only.

### `legacy-physical`
- Authorized write API: `param_set` (legacy range handled in `param_apply_legacy_mix_track_value`).
- Temporarily tolerated API: none identified.
- `active[]` role: global value mirror + source for subsequent reads; runtime is applied immediately to physical mixer tracks.
- Z2 dependency: not required.
- RT path allowed: no explicit RT fast path.

## Explicit decisions requested

1. Restore LFO final single authority to keep
- Decision: keep `mod_lfo_v1_set_track_param` as final config authority.
- Consequence: `param_registry_apply_track_value(PARAM_LFO*)` should be treated as temporary compatibility path only.
- Evidence: current `param_registry_apply_track_value` reroutes `PARAM_LFO*` to `mod_lfo_v1_set_track_param`.

2. Track-scoped UI params and `param_store.active[]`
- Decision: for track-scoped params, `param_store.active[]` is UI mirror, not runtime truth.
- Runtime truth remains per-track apply state (runtime cache/filter state/engine state) driven by `param_registry_apply_track_value`.

3. Base write vs RT modulation coexistence
- Decision: expected contract is layered authority:
- base authority = `param_registry_apply_track_value` (UI/seq/restore).
- transient override authority = `param_registry_apply_track_value_rt_fast` (LFO tick), with base restore on destination release.
- No broad patch required for this pass.

4. `control_router_set_param`
- Decision from current code evidence: currently inactive in local `Src/` callers.
- Status: tolerated dormant debt, not active authority.
- Action level: mark as cleanup candidate (later), not urgent runtime authority issue.

## Ambiguities remaining

- Exact long-term ownership of `param_store.active[]` for mixed global/track-scoped UI reads is still partially implicit; code shows dual use (global truth + track-scoped mirror).
- Whether legacy-physical range should remain first-class or be folded later cannot be concluded from Z3-local evidence only.
- If any caller exists outside current scanned `Src/` tree for `control_router_set_param`, that would change its status; not proven here.

## Smallest structural patch recommended

1. In `pattern_live_apply_snapshot`, remove the redundant second LFO write path (`param_registry_apply_track_value(PARAM_LFO*)`) and keep only `mod_lfo_v1_set_track_param` for restore config.
2. Add one short contract comment near `ui_param_set_active_track_value` stating: `param_store_set_active` is a UI mirror for track-scoped params, not track runtime authority.
3. Add one short contract comment near `control_router_set_param` marking it as legacy/dormant until a real caller is reintroduced.

No code patch applied in this pass.

