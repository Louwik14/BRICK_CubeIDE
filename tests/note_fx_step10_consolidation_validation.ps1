$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

function Read-RepoFile([string] $relativePath)
{
    return Get-Content -Raw (Join-Path $root $relativePath)
}

$stateHeader = Read-RepoFile 'Inc/NoteFx/note_fx_state.h'
$engineHeader = Read-RepoFile 'Inc/NoteFx/note_fx_engine.h'
$engine = Read-RepoFile 'Src/NoteFx/note_fx_engine.c'
$pipelineHeader = Read-RepoFile 'Inc/NoteFx/note_fx_pipeline.h'
$pipeline = Read-RepoFile 'Src/NoteFx/note_fx_pipeline.c'
$eventHeader = Read-RepoFile 'Inc/NoteFx/note_fx_event.h'
$schedulerHeader = Read-RepoFile 'Inc/Seq/seq_play_scheduler.h'
$scheduler = Read-RepoFile 'Src/Seq/seq_play_scheduler.c'
$cmake = Read-RepoFile 'tests/CMakeLists.txt'
$plan = Read-RepoFile 'docs/plan_midi_fx_3_slots_euclid.md'

$sourceFiles = Get-ChildItem -Path (Join-Path $root 'Inc'),
    (Join-Path $root 'Src'), (Join-Path $root 'tests') -Recurse -File |
    Where-Object { $_.Extension -in @('.c', '.h') } |
    ForEach-Object { Get-Content -Raw $_.FullName }
$sourceText = $sourceFiles -join "`n"

if ($stateHeader -notmatch '#define NOTE_FX_SLOT_COUNT 3U') {
    throw 'MIDI FX cardinality is not fixed to three slots'
}
if ($sourceText -match 'PARAM_MIDI_FX_S4|SEQ_PARAM_MIDI_FX_S4|SLOT4') {
    throw 'functional S4 identifier remains in code or tests'
}
if ($engineHeader -notmatch 'NOTE_FX_EUCLID_MAX_SOURCES' -or
    $engineHeader -notmatch 'NOTE_FX_EUCLID_MAX_OWNED' -or
    $engineHeader -notmatch 'note_fx_engine_slot_diag') {
    throw 'EUCLID fixed capacities or per-slot diagnostics are missing'
}
if ($engine -notmatch 'NOTE_FX_DIAG_CAUSE_SOURCE_CAPACITY' -or
    $engine -notmatch 'NOTE_FX_DIAG_CAUSE_OWNED_CAPACITY' -or
    $engine -notmatch 'note_fx_diag_record_emit_reject') {
    throw 'EUCLID saturation causes are not instrumented'
}
$processStart = $engine.IndexOf('void note_fx_engine_process')
$cleanupStart = $engine.IndexOf('void note_fx_engine_cleanup')
if (($processStart -lt 0) -or ($cleanupStart -le $processStart)) {
    throw 'engine process boundary cannot be inspected'
}
$processBody = $engine.Substring($processStart, $cleanupStart - $processStart)
if ($processBody -match 'euclid_build_mask|malloc\s*\(|calloc\s*\(|realloc\s*\(|free\s*\(') {
    throw 'hot EUCLID processing contains mask construction or allocation'
}
if ($engine -match 'midi_(note|send|tx)|audio_output|HAL_I2S_Transmit') {
    throw 'NoteFx engine owns an external output path'
}
if ($pipelineHeader -notmatch 'NOTE_FX_HALF_BUFFER_FRAMES 64U' -or
    $pipelineHeader -notmatch 'NOTE_FX_HALF_ON_QUOTA_PER_TRACK 8U' -or
    $pipelineHeader -notmatch 'NOTE_FX_HALF_OFF_RESERVE 32U' -or
    $pipelineHeader -notmatch 'NOTE_FX_PIPELINE_MAX_STAGE_FANOUT') {
    throw 'half-buffer and fixed fan-out contracts are missing'
}
if ($pipeline -notmatch 'generated_on_admitted' -or
    $pipeline -notmatch 'generated_off_refused' -or
    $pipeline -notmatch 'CLOSURE_RESERVED') {
    throw 'pipeline generated On/Off admission diagnostics are missing'
}
if ($eventHeader -notmatch 'NOTE_EVENT_STAGE_TERMINAL_HANDOFF' -or
    $eventHeader -notmatch 'note_event_is_terminal_handoff') {
    throw 'terminal handoff contract is missing'
}
if ($schedulerHeader -notmatch 'terminal_high_water' -or
    $schedulerHeader -notmatch 'terminal_off_retry_count' -or
    $scheduler -notmatch 'SEQ_OUTPUT_GUARD_MAX_OCCURRENCES == 64U') {
    throw 'terminal capacity diagnostics or assertion are missing'
}
if ($cmake -notmatch 'note_fx_euclid_mask_test.c' -or
    $cmake -notmatch 'note_fx_euclid_runtime_test.c' -or
    $cmake -notmatch 'note_fx_step10_consolidation_validation.ps1') {
    throw 'step 10 validation is not registered from the checkout'
}
if ($plan -notmatch 'Étape 10' -or
    $plan -notmatch 'mesures DWT/p99' -or
    $plan -notmatch 'aucun S4 fonctionnel') {
    throw 'step 10 status or deferred measurement boundary is undocumented'
}

Write-Output 'note_fx_step10_consolidation_validation=PASS cardinality=3 euclid=bounded terminal=64 hot_path=static-safe measurements=deferred'
