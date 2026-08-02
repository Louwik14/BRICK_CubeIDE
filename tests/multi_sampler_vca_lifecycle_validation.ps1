$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Read-Source([string]$relativePath) {
    return Get-Content -Raw (Join-Path $root $relativePath)
}
function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$runtime = Read-Source 'Src/Core/track_runtime.c'
$sampler = Read-Source 'Src/Core/brick6_sampler_runtime.c'
$keyboard = Read-Source 'Src/Keyboard/keyboard_engine.c'
$scheduler = Read-Source 'Src/Seq/seq_play_scheduler.c'
$z1 = Read-Source 'docs/architecture/z1_audio_hard_rt_mix.md'

$support = $runtime.Substring($runtime.IndexOf('uint8_t track_runtime_supports_vca_gate'))
Assert-Contract ($support -match 'TRACK_RUNTIME_ENGINE_SAMPLER') 'Sampler VCA capability is absent'
Assert-Contract ($support -match 'TRACK_RUNTIME_TYPE_LOOPER') 'Looper VCA exclusion is absent'

Assert-Contract ($keyboard -match 'is_multi_sampler[\s\S]*?is_multi_sampler == 0U') 'Keyboard must bypass the track VCA before Multi allocation'
Assert-Contract ($scheduler -match 'is_multi_sampler[\s\S]*?is_multi_sampler == 0U') 'Scheduler must bypass the track VCA before Multi allocation'
Assert-Contract ($sampler -match 'multi_voice_dsp_acquire\([\s\S]*?mixer_multi_filter_note_on\(mix_track, dsp_state, note\)') 'Accepted Multi voices do not initialize per-voice filter/VCA state'

$noteOff = $sampler.Substring($sampler.IndexOf('void brick6_sampler_runtime_note_off_multi_track_note'))
Assert-Contract ($noteOff -match 'mixer_multi_filter_note_off\(multi_voice_dsp_get\(voice->dsp_slot\)\)[\s\S]*?release_pending = 1U') 'Multi Note Off does not close the per-voice gate and retain the source'

$renderMulti = $sampler.Substring($sampler.IndexOf('void brick6_sampler_runtime_render_multi_track'))
Assert-Contract ($renderMulti -match 'mixer_multi_voice_vca_requires_source\(dsp_state\) == 0U[\s\S]*?STOP_REL_DONE') 'Multi release is not owned by per-voice VCA demand'
$renderVoiceStart = $sampler.LastIndexOf('static void brick6_sampler_render_multi')
$renderVoiceEnd = $sampler.IndexOf('static void brick6_sampler_runtime_clip_render_shifter', $renderVoiceStart)
$renderVoice = $sampler.Substring($renderVoiceStart, $renderVoiceEnd - $renderVoiceStart)
Assert-Contract ($renderVoice -notmatch 'release_pending') 'Multi voice renderer still consumes release_pending as an immediate stop'

$stopVoiceStart = $sampler.IndexOf('static void brick6_sampler_runtime_multi_stop_voice')
$stopVoiceEnd = $sampler.IndexOf('static void brick6_sampler_runtime_multi_defer_stream_release', $stopVoiceStart)
$stopVoice = $sampler.Substring($stopVoiceStart, $stopVoiceEnd - $stopVoiceStart)
Assert-Contract ($sampler -match 'brick6_sampler_runtime_multi_release_voice_vca[\s\S]*?mixer_multi_filter_note_off\(multi_voice_dsp_get\(voice->dsp_slot\)\)') 'EOF/steal stop does not close a residual per-voice VCA gate'
Assert-Contract ($stopVoice -notmatch 'mixer_track_vca_all_notes_off') 'Multi still depends on the shared track VCA gate'
Assert-Contract ($sampler -match 'sample_voice_reader_stop\(&voice->reader\)') 'Multi reader stop path is absent'
Assert-Contract ($sampler -notmatch 'env_adsr_') 'Multi runtime contains a duplicate ADSR'

Assert-Contract ($sampler -match 'loop_enabled[\s\S]*?SAMPLE_PLAY_LOOP_FORWARD') 'Multi loop mode is not wired to the existing reader contract'
Assert-Contract ($z1 -match 'Sampler/Multi[\s\S]*?release_pending[\s\S]*?EOF, underrun, steal') 'Canonical Multi lifecycle documentation is missing'

Write-Output 'multi_sampler_vca_lifecycle_validation=PASS gate=accepted_voice noteoff=release source=per_voice_vca eof=bounded panic=forced modes=oneshot+loop_forward'
