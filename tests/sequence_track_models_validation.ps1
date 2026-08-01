$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$compiler = (Get-Command arm-none-eabi-gcc -ErrorAction Stop).Source
$include = Join-Path $repo 'Inc'
$stubs = Join-Path $repo 'tests\stubs'
$model = Join-Path $repo 'Src\Seq\seq_model.c'

foreach ($variant in @('BRICK6_VARIANT_LOWCOST', 'BRICK6_VARIANT_PREMIUM')) {
    & $compiler '-std=gnu11' '-Wall' '-Werror' '-fsyntax-only' "-D$variant" "-I$stubs" "-I$include" $model
    if ($LASTEXITCODE -ne 0) { throw "seq_model compile failed for $variant" }
}

$types = Get-Content -Raw (Join-Path $include 'Seq\seq_types.h')
$header = Get-Content -Raw (Join-Path $include 'Seq\seq_model.h')
$implementation = Get-Content -Raw $model
$keyboardEngine = Get-Content -Raw (Join-Path $repo 'Src\Keyboard\keyboard_engine.c')
$scheduler = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_play_scheduler.c')
$outputGuard = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_output_guard.c')
$runtimeHeader = Get-Content -Raw (Join-Path $include 'Seq\seq_runtime.h')
$edit = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_edit.c')
$pattern = Get-Content -Raw (Join-Path $repo 'Src\Storage\pattern_live_ram.c')

foreach ($contract in @(
    '#define SEQ_PLAY_STEP_MAX_LOCKS 32U',
    '#define SEQ_PLAY_PLOCK_POOL_CAP_PER_TRACK 1024U',
    '#define SEQ_SPECIAL_STEP_MAX_LOCKS 16U',
    '#define SEQ_SPECIAL_PLOCK_POOL_CAP_PER_TRACK 512U',
    'SEQ_SPECIAL_ACTION_NONE',
    'SEQ_SPECIAL_ACTION_TRIGGER'
)) {
    if (-not ($types.Contains($contract) -or $header.Contains($contract))) {
        throw "Missing sequence model contract: $contract"
    }
}

if (-not $implementation.Contains('play_pool[TRACK_TOPOLOGY_PLAY_TRACK_COUNT][SEQ_PLAY_PLOCK_POOL_CAP_PER_TRACK]')) {
    throw 'Play p-lock pool is not separated.'
}
if (-not $implementation.Contains('special_pool[TRACK_TOPOLOGY_SPECIAL_TRACK_COUNT][SEQ_SPECIAL_PLOCK_POOL_CAP_PER_TRACK]')) {
    throw 'Special p-lock pool is not separated.'
}
if (-not $implementation.Contains('return track_topology_is_active(track);')) {
    throw 'Low-Cost storage-only slots can address the Special pool.'
}
if (-not $implementation.Contains('(seq_model_track_is_play(track) == 0U) && (set_id == (uint8_t)SEQ_PLOCK_SET_PLAY)')) {
    throw 'Special tracks still accept PLAY note locks.'
}
if (-not $implementation.Contains('seq_model_toggle_special_action')) {
    throw 'Extensible Special action field is missing.'
}
if (-not $edit.Contains('seq_model_toggle_special_action(track, step)')) {
    throw 'Special step gesture is not routed to the action field.'
}
if (-not $keyboardEngine.Contains('g_kbd_rec_track_note_count[TRACK_TOPOLOGY_PLAY_TRACK_COUNT]')) {
    throw 'Keyboard note ownership is still allocated for Special tracks.'
}
if ($keyboardEngine.Contains('g_keyboard_engine_group_note_track')) {
    throw 'Keyboard voice-group note ownership is still present.'
}
if (-not $scheduler.Contains('TRACK_CAPABILITY_NOTES')) {
    throw 'Scheduler note capability guard missing.'
}
if ($scheduler.Contains('g_seq_play_active_event_token[SEQ_TRACK_COUNT]') -or
    -not $scheduler.Contains('g_seq_play_active_event_token[TRACK_TOPOLOGY_PLAY_TRACK_COUNT]')) {
    throw 'Scheduler note ownership is still allocated for Special tracks.'
}
if (-not $outputGuard.Contains('note_counts[TRACK_TOPOLOGY_PLAY_TRACK_COUNT][128U]')) {
    throw 'Output note guard is still allocated for Special tracks.'
}
if (-not $runtimeHeader.Contains('active_locks_special[TRACK_TOPOLOGY_SPECIAL_TRACK_COUNT][SEQ_SPECIAL_STEP_MAX_LOCKS]')) {
    throw 'Special active automation locks are not bounded to 16.'
}
if (-not $pattern.Contains('seq_model_get_track_plock_capacity(track)')) {
    throw 'Legacy Pattern adapter does not validate heterogeneous capacities.'
}

'sequence_track_models_validation=PASS play=64x32/1024 special=64x16/512 notes=play_only action=extensible'
