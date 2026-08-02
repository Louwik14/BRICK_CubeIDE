$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pipeline = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_pipeline.c')
$engine = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_engine.c')
$state = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_state.c')
$header = Get-Content -Raw (Join-Path $root 'Inc/NoteFx/note_fx_event.h')

$sourceStart = $engine.IndexOf('note_fx_result_t note_fx_engine_source')
$sourceEnd = $engine.IndexOf('static uint64_t rate_period')
$sourceBody = $engine.Substring($sourceStart, $sourceEnd - $sourceStart)
if ($pipeline -match 'g_note_fx_runtime_arp_slot' -or $sourceBody -match 'for \(uint8_t slot') {
    throw 'stage execution still selects a first ARP slot'
}
if ($pipeline -notmatch 'note_fx_pipeline_stage_emit' -or
    $pipeline -notmatch 'note_fx_engine_stage_source\(event, event->stage') {
    throw 'continuation does not resume at the next stage'
}
if ($engine -notmatch 'event->stage != slot' -or
    $engine -notmatch 'forwarded\.stage = \(uint8_t\)\(slot \+ 1U\)' -or
    $engine -notmatch '\.stage = \(uint8_t\)\(slot \+ 1U\)') {
    throw 'stage boundaries are not explicit'
}
if ($pipeline -notmatch 'event->stage >= NOTE_EVENT_STAGE_TERMINAL') {
    throw 'terminal is not reserved for stage four'
}
if ($header -notmatch 'NOTE_EVENT_STAGE_TERMINAL 4U') {
    throw 'four-stage contract is missing'
}
if ($state -match 'arp_seen|previous.*NOTE_FX_MODEL_ARP') {
    throw 'canonical state still enforces unique ARP'
}

Write-Output 'note_fx_stage_chain_validation=PASS stages=0-3 terminal=4 continuation=bounded first_arp=absent'
