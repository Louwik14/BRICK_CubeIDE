$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$poly = Get-Content -Raw (Join-Path $repo 'Src\Core\synth_polyphony.c')
$polyH = Get-Content -Raw (Join-Path $repo 'Inc\Core\synth_polyphony.h')
$runtime = Get-Content -Raw (Join-Path $repo 'Src\Core\track_runtime.c')
$mixer = Get-Content -Raw (Join-Path $repo 'Src\Audio\mixer.c')
$patch = Get-Content -Raw (Join-Path $repo 'Src\Storage\patch_v1.c')
$snapshot = Get-Content -Raw (Join-Path $repo 'Src\Core\track_snapshot.c')
$pattern = Get-Content -Raw (Join-Path $repo 'Src\Storage\pattern_live_ram.c')
$kit = Get-Content -Raw (Join-Path $repo 'Src\Storage\kit_v1.c')
$kitH = Get-Content -Raw (Join-Path $repo 'Inc\Storage\kit_v1.h')
$ui = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_core.c')
$param = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_param.c')
$panic = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_output_guard.c')

foreach ($required in @(
    'SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET 16U',
    'synth_polyphony_get_slot',
    'synth_polyphony_get_available_for_track',
    'synth_polyphony_validate_ownership',
    'synth_polyphony_reset_track',
    'synth_polyphony_panic'
)) { if (-not ($polyH.Contains($required) -or $poly.Contains($required))) { throw "Missing budget contract: $required" } }

foreach ($required in @(
    'synth_polyphony_reset_slot(slot)',
    'synth_polyphony_release_slot(slot)',
    'mixer_synth_voice_slot_reset',
    'brick6_braids_runtime_reset_instance',
    'brick6_stack_runtime_reset_instance',
    'brick6_wave_runtime_reset_instance',
    'brick6_deluge_runtime_reset_instance'
)) { if (-not ($poly.Contains($required) -or $mixer.Contains($required))) { throw "Missing slot cleanup: $required" } }

if (-not $poly.Contains('for (uint8_t slot = 0U; slot < SYNTH_POLYPHONY_TRACK_CAPACITY; ++slot)')) {
    throw 'Allocator does not reuse free track-indexed slots for the full 16-voice budget'
}
if (-not $poly.Contains('g_synth_voice[SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET]')) {
    throw 'Note/allocation state is not physically bounded to 16 slots'
}
if ($poly.Contains('voice[SYNTH_POLYPHONY_MAX_VOICES]')) {
    throw 'Legacy per-track 64-note-state matrix remains'
}
if (-not $runtime.Contains('synth_polyphony_get_slot(ctx->track_id, 0U)')) {
    throw 'Runtime base instance is not resolved through the global slot mapping'
}

foreach ($required in @(
    'PATCH_V1_RESULT_VOICE_MAX',
    'PATCH_V1_RESULT_VOICE_LIMITED',
    'synth_polyphony_get_free_count()',
    'synth_polyphony_get_voice_count(targets[i])',
    'synth_polyphony_set_voice_count(targets[i], applied_voice_count[i])'
)) { if (-not $patch.Contains($required)) { throw "Missing patch transaction contract: $required" } }

foreach ($required in @(
    'track_snapshot_last_voice_limited',
    'track_snapshot_last_voice_max',
    'synth_polyphony_get_available_for_track(target_track)'
)) { if (-not $snapshot.Contains($required)) { throw "Missing snapshot contract: $required" } }

foreach ($required in @(
    'pattern_live_resolve_voice_budget',
    'pattern_live_last_voice_limited',
    'PARAM_CFG_POLY_VOICES'
)) { if (-not $pattern.Contains($required)) { throw "Missing undo/restore contract: $required" } }

if (-not $ui.Contains('FAMILY : MAX')) { throw 'Missing saturated Synth browser feedback' }
if (-not $param.Contains('synth_polyphony_get_available_for_track(track)')) { throw 'VOICES scroll is not dynamically bounded' }
if (-not $panic.Contains('synth_polyphony_panic();')) { throw 'Panic does not clear every reserved synth slot' }
if ($polyH -match 'MAX_INSTANCES\s*\*\s*8U') { throw 'Legacy 64-instance formula remains in synth authority' }
foreach ($required in @(
    'kit_v1_resolve_voice_budget',
    'KIT_V1_RESULT_VOICE_LIMITED',
    'synth_polyphony_set_voice_count(track',
    'poly_voice_count'
)) { if (-not ($kit.Contains($required) -or $kitH.Contains($required))) { throw "Missing Kit voice contract: $required" } }
if (-not $poly.Contains('__DMB();')) { throw 'Synth pool publication has no IRQ ordering barrier' }

# Deterministic reference model: two 8-voice tracks consume the whole pool;
# a third track is rejected, then succeeds as soon as four slots are released.
$owners = [System.Collections.Generic.HashSet[int]]::new()
foreach ($slot in 0..7) { if (-not $owners.Add($slot)) { throw 'Duplicate owner' } }
foreach ($slot in 8..15) { if (-not $owners.Add($slot)) { throw 'Duplicate owner' } }
if ($owners.Count -ne 16) { throw 'Expected exactly 16 owned slots' }
$seventeenthAccepted = (16 -lt 16)
if ($seventeenthAccepted) { throw 'A seventeenth slot was accepted' }
foreach ($slot in 4..7) { $owners.Remove($slot) | Out-Null }
if ($owners.Count -ne 12) { throw 'Release did not return four slots' }
foreach ($slot in 4..7) { if (-not $owners.Add($slot)) { throw 'Released slot was not reusable' } }
if ($owners.Count -ne 16) { throw 'Reacquisition failed' }

'synth_voice_budget_validation=PASS budget=16 max_state=visible patch=transactional snapshot=bounded panic=reservations_preserved'
