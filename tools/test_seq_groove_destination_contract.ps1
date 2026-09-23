$ErrorActionPreference = 'Stop'

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$engine = Get-Content 'Src/Seq/seq_engine.c' -Raw
$timing = Get-Content 'Src/Seq/seq_timing.c' -Raw
$adapter = Get-Content 'Src/Audio/audio_note_engine_adapter.c' -Raw
$port = Get-Content 'Src/Seq/seq_engine_port_h743.c' -Raw

# Destination type is deliberately absent from the source -> finalizer path.
# Consequently poly, TB303 and ACID consume the same finalized timestamp and
# velocity; only the final audio adapter dispatch differs.
$terminal = [regex]::Match($engine,
    'static void fx_terminal\(.*?\n\}',
    [System.Text.RegularExpressions.RegexOptions]::Singleline).Value
Assert-Contract ($terminal -match 'seq_timing_finalize') `
    'terminal events no longer cross the common Groove finalizer'
Assert-Contract ($terminal -notmatch 'TB303|ACID|MONO|track_exec') `
    'destination-specific branch leaked into Groove finalization'
Assert-Contract ($engine -match 'walker_resume_batch\(g_seq_source_cohort,event_count') `
    'step notes bypass the common NoteFX/Groove walker'
Assert-Contract ($timing -match 'e->sample_abs=final_start') `
    'Groove timing is not committed to the canonical note event'
Assert-Contract ($timing -match 'e->velocity=\(uint8_t\)v') `
    'Groove velocity is not committed to the canonical note event'
Assert-Contract ($timing -match 'd\*p->quantize') `
    'Quantize amount no longer transforms the source position'
Assert-Contract ($timing -match 'p->timing\*p->global') `
    'Timing no longer includes Global Amount'
Assert-Contract ($timing -match 'p->random\*global_random\(p->global\)') `
    'Random no longer includes Global Amount'
Assert-Contract ($timing -match
    '\(p->velocity<0\?-p->velocity:p->velocity\)\*p->global') `
    'Velocity no longer includes Global Amount'
Assert-Contract ($port -match 'due_sample = block->start_sample \+ offset') `
    'MIDI no longer consumes the finalized terminal timestamp'

foreach ($family in @('TB303', 'ACID')) {
    Assert-Contract ($adapter -match "TRACK_RUNTIME_ENGINE_$family") `
        "$family destination dispatch is missing"
}
Assert-Contract (([regex]::Matches($adapter,
    'runtime_note_on\(instance, note, velocity\)')).Count -ge 2) `
    'mono destinations no longer consume finalized velocity'

# Scheduled timing classes move; live immediate notes intentionally retain
# capture time while still receiving the velocity field.
Assert-Contract ($timing -match
    'if\(e->timing_class!=NOTE_EVENT_TIMING_SCHEDULED\)return') `
    'live-immediate temporal bypass changed'

# Slide remains a transition parameter ordered before OFF/PARAM/ON at a given
# sample. Groove changes note times, not the mono engine's legato state.
Assert-Contract ($engine -match 'PARAM_TB303_SLIDE') `
    'TB303 slide transition contract is missing'
Assert-Contract ($engine -match 'PARAM_ACID_SLIDE') `
    'ACID slide transition contract is missing'
Assert-Contract ($engine -match 'SEQ_ENGINE_EVENT_TRANSITION_PARAM') `
    'slide is no longer published as a transition parameter'

'seq Groove destination contract: PASS'
