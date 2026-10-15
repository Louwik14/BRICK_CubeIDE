$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Compact-MinMax([object[]]$Bins) {
    $out = [System.Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt $Bins.Count; $index += 2) {
        $right = [math]::Min($index + 1, $Bins.Count - 1)
        $out.Add([pscustomobject]@{
            Min = [math]::Min($Bins[$index].Min, $Bins[$right].Min)
            Max = [math]::Max($Bins[$index].Max, $Bins[$right].Max)
            First = $Bins[$index].First
            Last = $Bins[$right].Last
        })
    }
    return $out.ToArray()
}

# Pairwise reductions used by rec_source_waveform must preserve transients at
# every temporal scale.
$bins = for ($index = 0; $index -lt 4096; $index++) {
    [pscustomobject]@{ Min = 0; Max = 0; First = $index; Last = $index }
}
$bins[17].Max = 32767
$bins[3001].Min = -32768
for ($level = 0; $level -lt 5; $level++) {
    $bins = Compact-MinMax $bins
    Assert-True (($bins.Max -contains 32767) -and ($bins.Min -contains -32768)) `
        "peak lost at compaction level $level"
    Assert-True (($bins[0].First -eq 0) -and
        ($bins[$bins.Count - 1].Last -eq 4095)) `
        "temporal endpoints lost at compaction level $level"
}

$root = Split-Path -Parent $PSScriptRoot
$capture = Get-Content -Raw (Join-Path $root 'Src/Storage/rec_source_waveform.c')
$storage = Get-Content -Raw (Join-Path $root 'Src/Storage/audio_recorder_storage.c')
$page = Get-Content -Raw (Join-Path $root 'Src/UI/pages/AudioRec/ui_audio_rec_page.inc')
$render = Get-Content -Raw (Join-Path $root 'Src/UI/pages/AudioRec/ui_audio_rec_take_editor.inc')
$source = Get-Content -Raw (Join-Path $root 'Src/Storage/rec_source.c')
$service = Get-Content -Raw (Join-Path $root 'Src/Storage/waveform_service.c')
$contract = Get-Content -Raw (Join-Path $root 'Inc/Storage/rec_source_waveform.h')

Assert-True ($capture.Contains('g_rec_source_waveform.levels[0].frames_per_bin = 1U;')) `
    'REC waveform no longer starts at one PCM frame per bin'
Assert-True ($capture.Contains('(void)frame_limit;')) `
    'planned duration is influencing initial REC waveform resolution'
Assert-True ($capture.Contains('REC_SOURCE_WAVEFORM_LIVE_FRAMES_PER_BIN')) `
    'progressive waveform no longer owns a bounded recent-detail window'
Assert-True ($capture.Contains('live->last = mono;')) `
    'progressive waveform no longer preserves a continuous trace endpoint'
Assert-True ($storage.Contains('runtime->recorder.committed_tail / AUDIO_RECORDER_BYTES_PER_FRAME')) `
    'waveform visibility is no longer tied to SD committed frames'
Assert-True ($storage.Contains('AUDIO_RECORDER_STORAGE_TAKE_READY')) `
    'final waveform drain does not cover TAKE_READY'
Assert-True ($source.Contains('rec_source_waveform_captured_frames() != frame_count')) `
    'REC_SOURCE CURRENT can publish an incoherent partial waveform'
Assert-True ($capture.Contains('rec_source_waveform_build_level_t')) `
    'REC waveform build levels are no longer separated from final summaries'
Assert-True ($capture.Contains('final_level->min') -and
    $capture.Contains('final_level->max')) `
    'REC waveform finalization no longer copies the compact min/max summary'
Assert-True (-not $contract.Contains('int16_t first[REC_SOURCE_WAVEFORM_BINS]')) `
    'final REC waveform summaries still retain first[]'
Assert-True (-not $contract.Contains('int16_t last[REC_SOURCE_WAVEFORM_BINS]')) `
    'final REC waveform summaries still retain last[]'
Assert-True ($source.Contains('memset(&candidate->waveform, 0, sizeof(candidate->waveform));')) `
    'recycled REC_SOURCE slots can retain a stale waveform summary'
Assert-True (-not $source.Contains('building->waveform.ready == 0U')) `
    'REC_SOURCE finalization can still reuse a stale ready summary'
Assert-True ($source.Contains('rec_source_waveform_commit();')) `
    'REC waveform construction can end before the generation commit succeeds'
Assert-True ($source.Contains('current->waveform.generation == current->key.generation')) `
    'REC_SOURCE exposes waveform data without generation validation'
Assert-True ($page.Contains('state.phase == SAMPLE_CAPTURE_PHASE_SAVED')) `
    'saved REC mode does not restore the live scope owner'
Assert-True (-not $page.Contains('(state.take_valid == 0U)')) `
    'stale take validity still masks the live REC BUS scope'
Assert-True ($render.Contains('state->phase == SAMPLE_CAPTURE_PHASE_TAKE_READY')) `
    'recorded overview is not limited to the post-record review state'
Assert-True ($render.Contains('previous_min_y')) `
    'progressive waveform no longer renders a connected peak contour'
Assert-True ($service.Contains('rec_source_current_path(&path, &current)')) `
    'REC waveform identity still depends on the playback projection'
Assert-True (-not $service.Contains('(source == 0) || (rec_source_current_snapshot(&current)')) `
    'REC summary is still gated by Streamer playback readiness'
Assert-True ($service.Contains('summary->frames_per_bin <= frames_per_pixel')) `
    'resident REC overview no longer bypasses redundant WAV reconstruction'

Write-Output 'REC waveform lifecycle regressions: PASS'
