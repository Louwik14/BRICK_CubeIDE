$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
function Read-Source([string]$relativePath) {
    return Get-Content -Raw (Join-Path $root $relativePath)
}
function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$gate = Read-Source 'Src/Storage/sd_access_gate.c'
$gateHeader = Read-Source 'Inc/Storage/sd_access_gate.h'
$catalog = Read-Source 'Src/Storage/wav_loader.c'
$browser = Read-Source 'Src/UI/pages/ui_page_settings.c'
$manager = Read-Source 'Src/Sampler/sample_stream_manager.c'
$cache = Read-Source 'Src/Sampler/sample_cache.c'
$runtime = Read-Source 'Src/Core/brick6_sampler_runtime.c'
$z1 = Read-Source 'docs/architecture/z1_audio_hard_rt_mix.md'
$z5 = Read-Source 'docs/architecture/z5_ui_navigation_interaction.md'

$busyLabel = $gate.Substring($gate.IndexOf('const char *sd_access_gate_busy_label(void)'))
Assert-Contract ($busyLabel -match 'current_owner') 'Busy label does not inspect the current gate owner'
Assert-Contract ($busyLabel -match 'streaming_critical_active') 'Busy label does not inspect active Stream policy'
Assert-Contract ($busyLabel -match 'SD_ACCESS_CLIENT_NONE') 'Idle busy label is not NONE'
Assert-Contract ($busyLabel -notmatch 'sd_access_gate_last_owner\(\)') 'Historical last owner still drives the busy label'
Assert-Contract ($gateHeader -match 'sd_access_gate_last_owner') 'Diagnostic last-owner API was removed'

Assert-Contract ($catalog -match 'sd_access_gate_streaming_critical_active\(\)') 'Catalog guard does not protect active Stream windows'
Assert-Contract ($catalog -match 'sd_access_gate_try_acquire\(SD_ACCESS_CLIENT_PREVIEW\)') 'Catalog does not arbitrate through the SD gate'
Assert-Contract ($catalog -match 'g_wav_catalog_last_sd_busy = 0U') 'Catalog busy result is not reset per operation'
Assert-Contract ($browser -match 'wav_loader_catalog_last_sd_busy\(\)') 'Browser does not consume the catalog busy result'
Assert-Contract ($browser -match 'sample_dir\[WAV_LOADER_CATALOG_PATH_MAX\]') 'Browser path buffer is not bounded by the catalog contract'
Assert-Contract ($catalog -match 'depth > 8U') 'Catalog recursion is not bounded'
Assert-Contract ($browser -match 'sample_browser_parent_or_exit') 'Browser parent navigation path is absent'

Assert-Contract ($manager -match 'sample_page_cache_release_window_owner\(owner_kind, owner_id, owner_generation\)') 'Stream owner release is absent'
Assert-Contract ($manager -match 'sample_page_cache_cancel_queued_page_key') 'Stream release does not cancel queued pages'
Assert-Contract ($manager -match 'sample_stream_manager_repair_queued_pages\(\)') 'Queued-page orphan repair is absent'
Assert-Contract ($manager -match 'sd_access_gate_set_streaming_critical\(sample_page_cache_has_window_locks\(\)\)') 'Stream policy is not derived from active page locks'
Assert-Contract ($cache -match 'sample_cache_release_pending_stream_owners') 'Deferred underrun owner release is absent'
Assert-Contract ($cache -match 'sample_stream_manager_has_pending_sd_work') 'Pending SD work is checked after service'

Assert-Contract ($runtime -match 'release_pending = 1U') 'Stream VCA release state is absent'
Assert-Contract ($runtime -match 'mixer_track_vca_requires_source\(ctx->mix_track_id\) == 0U') 'Stream stop is not tied to VCA source demand'
Assert-Contract ($runtime -match 'sample_voice_reader_stop\(&voice->reader\)') 'Stream reader stop is absent'
Assert-Contract ($runtime -match 'play_mode != 0U[\s\S]*?return;') 'Launch Note Off is not ignored'
Assert-Contract ($runtime -match 'brick6_sampler_runtime_stop_transport_clips') 'Forced transport stop path is absent'

Assert-Contract ($z1 -match 'streaming_critical[\s\S]*?release') 'Z1 does not document Stream policy release'
Assert-Contract ($z5 -match 'SD STREAM') 'Z5 does not document the browser lock feedback'

Write-Output 'sd_stream_browser_lock_validation=PASS authority=active_gate_or_window_lock historical_last_owner=diagnostic_only browser_depth=bounded stream_release=reader+pages+pending gate=launch_aware'
