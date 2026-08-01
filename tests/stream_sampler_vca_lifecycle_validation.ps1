$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Read-Source([string]$relativePath) {
    return Get-Content -Raw (Join-Path $root $relativePath)
}
function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$runtime = Read-Source 'Src/Core/track_runtime.c'
$transition = Read-Source 'Src/Param/param_registry_transition.c'
$sampler = Read-Source 'Src/Core/brick6_sampler_runtime.c'
$keyboard = Read-Source 'Src/Keyboard/keyboard_engine.c'
$scheduler = Read-Source 'Src/Seq/seq_play_scheduler.c'
$mixer = Read-Source 'Src/Audio/mixer.c'
$env = Read-Source 'Src/UI/pages/ui_page_template_env.c'

$support = $runtime.Substring($runtime.IndexOf('uint8_t track_runtime_supports_vca_gate'))
Assert-Contract ($support -match 'TRACK_RUNTIME_TYPE_STREAM') 'Stream VCA capability is absent'
Assert-Contract ($support -match 'TRACK_RUNTIME_TYPE_LOOPER') 'Looper VCA exclusion is absent'
Assert-Contract ($transition -match 'track_runtime_supports_vca_gate\(ctx\)') 'VCA neutralization does not use the runtime authority'
Assert-Contract ($env -match 'track_runtime_supports_vca_gate') 'ENV/VCA page is not capability-gated'

Assert-Contract ($keyboard -match 'mixer_track_vca_note_on\(mix_track, note, velocity\)') 'Keyboard VCA Note On is not common'
Assert-Contract ($keyboard -match 'mixer_track_vca_note_off\(mix_track, note\)') 'Keyboard VCA Note Off is not common'
Assert-Contract ($scheduler -match 'mixer_track_vca_note_on\(resolved\.mix_track_id, note, velocity\)') 'Scheduler VCA Note On is not common'
Assert-Contract ($scheduler -match 'mixer_track_vca_note_off\(resolved\.mix_track_id, note\)') 'Scheduler VCA Note Off is not common'
Assert-Contract ($scheduler -match 'brick6_sampler_runtime_note_off_note\(track, note\)') 'Scheduler does not notify Stream release lifecycle'

$noteOff = $sampler.Substring($sampler.IndexOf('void brick6_sampler_runtime_note_off(uint8_t track_id)'))
Assert-Contract ($noteOff -match 'play_mode != 0U[\s\S]*?return;') 'Launch Note Off is no longer ignored'
Assert-Contract ($noteOff -match 'release_pending = 1U') 'Gate Note Off does not enter bounded release state'
Assert-Contract ($sampler -match 'voice->release_pending = 0U[\s\S]*?clip_start_playback') 'Retrigger does not clear Stream release state'
Assert-Contract ($sampler -match 'mixer_track_vca_requires_source\(ctx->mix_track_id\)') 'Stream source lifetime does not follow mixer VCA demand'
Assert-Contract ($sampler -match 'sample_voice_reader_stop\(&voice->reader\)') 'Stream reader stop path is absent'
Assert-Contract ($sampler -match 'brick6_sampler_runtime_clip_release_slot\(ctx->track_id\)') 'Stream cache/shifter owner release path is absent'

Assert-Contract ($sampler -match 'brick6_sampler_runtime_clip_stop_playback\(track_id\)[\s\S]*?sample_id = sample_id') 'Sample change does not stop the previous Stream'
Assert-Contract ($sampler -match 'void brick6_sampler_runtime_stop\(uint8_t track_id\)') 'Forced Stream stop path is absent'
Assert-Contract ($sampler -match 'brick6_sampler_runtime_stop_transport_clips') 'Transport/panic Stream stop path is absent'
Assert-Contract ($mixer -match 'env_adsr_process_step\(&g_track_filters\[t\]\.vca_env\)') 'Mixer VCA processing path is absent'
Assert-Contract ($sampler -notmatch 'env_adsr_') 'Stream runtime contains a duplicate ADSR'

Write-Output 'stream_sampler_vca_lifecycle_validation=PASS capability=stream_only looper=excluded noteoff=release launch=ignored source=vca_demand forced_stop=bounded'
