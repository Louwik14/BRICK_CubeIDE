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
        })
    }
    return $out.ToArray()
}

# Pairwise reductions used by rec_source_waveform must preserve transients at
# every temporal scale.
$bins = for ($index = 0; $index -lt 4096; $index++) {
    [pscustomobject]@{ Min = 0; Max = 0 }
}
$bins[17].Max = 32767
$bins[3001].Min = -32768
for ($level = 0; $level -lt 5; $level++) {
    $bins = Compact-MinMax $bins
    Assert-True (($bins.Max -contains 32767) -and ($bins.Min -contains -32768)) `
        "peak lost at compaction level $level"
}

$root = Split-Path -Parent $PSScriptRoot
$capture = Get-Content -Raw (Join-Path $root 'Src/Storage/rec_source_waveform.c')
$storage = Get-Content -Raw (Join-Path $root 'Src/Storage/audio_recorder_storage.c')
$page = Get-Content -Raw (Join-Path $root 'Src/UI/pages/AudioRec/ui_audio_rec_page.inc')
$render = Get-Content -Raw (Join-Path $root 'Src/UI/pages/AudioRec/ui_audio_rec_take_editor.inc')
$source = Get-Content -Raw (Join-Path $root 'Src/Storage/rec_source.c')

Assert-True ($capture.Contains('g_rec_source_waveform.levels[0].frames_per_bin = 1U;')) `
    'REC waveform no longer starts at one PCM frame per bin'
Assert-True ($capture.Contains('(void)frame_limit;')) `
    'planned duration is influencing initial REC waveform resolution'
Assert-True ($capture.Contains('? g_rec_source_waveform.captured_frames : 1U;')) `
    'active projection is not based on acquired duration'
Assert-True ($storage.Contains('runtime->recorder.committed_tail / AUDIO_RECORDER_BYTES_PER_FRAME')) `
    'waveform visibility is no longer tied to SD committed frames'
Assert-True ($storage.Contains('AUDIO_RECORDER_STORAGE_TAKE_READY')) `
    'final waveform drain does not cover TAKE_READY'
Assert-True ($source.Contains('rec_source_waveform_captured_frames() != frame_count')) `
    'REC_SOURCE CURRENT can publish an incoherent partial waveform'
Assert-True ($page.Contains('state.phase == SAMPLE_CAPTURE_PHASE_SAVED')) `
    'saved REC mode does not restore the live scope owner'
Assert-True (-not $page.Contains('(state.take_valid == 0U)')) `
    'stale take validity still masks the live REC BUS scope'
Assert-True ($render.Contains('state->phase == SAMPLE_CAPTURE_PHASE_TAKE_READY')) `
    'recorded overview is not limited to the post-record review state'

Write-Output 'REC waveform lifecycle regressions: PASS'
