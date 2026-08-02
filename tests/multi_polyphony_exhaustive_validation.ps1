$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot

function Read-RepoFile([string]$relativePath) {
    return Get-Content -Raw (Join-Path $repo $relativePath)
}

function Require-Match([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) {
        throw $message
    }
}

function Forbid-Match([string]$text, [string]$pattern, [string]$message) {
    if ($text -match $pattern) {
        throw $message
    }
}

$contract = Read-RepoFile 'Inc\Core\brick6_sampler_multi_contract.h'
$runtimeHeader = Read-RepoFile 'Inc\Core\brick6_sampler_runtime.h'
$runtime = Read-RepoFile 'Src\Core\brick6_sampler_runtime.c'
$dspHeader = Read-RepoFile 'Inc\Audio\multi_voice_dsp.h'
$dsp = Read-RepoFile 'Src\Audio\multi_voice_dsp.c'
$format = Read-RepoFile 'Inc\Sampler\sample_audio_format.h'
$registry = Read-RepoFile 'Src\Param\param_registry.c'
$trackRuntime = Read-RepoFile 'Src\Core\track_runtime.c'
$audioRuntime = Read-RepoFile 'Src\Core\brick6_audio_runtime.c'
$mixer = Read-RepoFile 'Src\Audio\mixer.c'
$pool = Read-RepoFile 'Src\Sampler\multi_sample_pool.c'
$synthHeader = Read-RepoFile 'Inc\Core\synth_polyphony.h'

# Global budget, independent DSP ownership, and synth regression guard.
Require-Match $contract 'BRICK6_SAMPLER_MULTI_MAX_VOICES\s+\(8U\)' 'Multi global capacity is not eight'
Require-Match $runtimeHeader 'SAMPLER_MULTI_MAX_GLOBAL_VOICES\s+\(BRICK6_SAMPLER_MULTI_MAX_VOICES\)' 'Runtime does not use the frozen Multi capacity'
Require-Match $dspHeader 'MULTI_VOICE_DSP_SLOT_COUNT\s+BRICK6_SAMPLER_MULTI_MAX_VOICES' 'DSP pool is not tied to the Multi capacity'
Require-Match $dspHeader '_Static_assert\(MULTI_VOICE_DSP_SLOT_COUNT == 8U' 'DSP pool eight-slot assertion is missing'
Require-Match $dspHeader 'MULTI_VOICE_DSP_SLOT_SIZE_BYTES \(608U\)' 'Measured DSP slot size is missing'
Require-Match $dsp 'sizeof\(g_multi_voice_dsp_pool\)[\s\S]*MULTI_VOICE_DSP_SLOT_COUNT \* MULTI_VOICE_DSP_SLOT_SIZE_BYTES' 'DSP pool size assertion is missing'
Require-Match $dsp 'multi_voice_dsp_validate_ownership' 'DSP ownership validator is missing'
Require-Match $synthHeader 'SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET\s+16' 'Synth global budget changed'
Forbid-Match $runtime 'SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET|synth_polyphony_' 'Multi sampler is coupled to the synth allocator'

# Runtime identity, stale-generation rejection, and deterministic teardown.
Require-Match $contract 'voice_index[\s\S]*generation' 'Multi handle does not carry a generation'
Require-Match $dsp 'handle\.generation != 0U' 'Multi handle does not require a non-zero generation'
Require-Match $dsp 'owner_voice_index == handle\.voice_index[\s\S]*owner_generation == handle\.generation' 'DSP release is not generation-safe'
Require-Match $runtime 'brick6_sampler_runtime_note_off_multi_track_note_token' 'Token Note Off API is missing'
Require-Match $runtime 'voice->event_token != event_token' 'Token Note Off does not reject stale or unrelated occurrences'
Require-Match $runtime 'sample_stream_manager_release_owner[\s\S]*generation' 'Stream owner release is not generation-keyed'
Require-Match $runtime 'brick6_sampler_runtime_multi_release_dsp_slot' 'Multi teardown does not release DSP ownership'
Require-Match $runtime 'sample_voice_reader_stop\(&voice->reader\)' 'Forced Multi teardown does not stop the reader'
Require-Match $runtime 'brick6_sampler_runtime_multi_release_inactive_stream_owners' 'Deferred owner release service is missing'

# Homogeneous format and deterministic reverse/ping-pong policy.
Require-Match $format 'sample_audio_format_is_valid' 'FLOAT32 mono/stereo format validator is missing'
Require-Match $runtime 'brick6_sampler_runtime_multi_voice_format_compatible' 'Multi format compatibility guard is missing'
Require-Match $runtime 'voice->play_plan\.format == instrument->format' 'Per-voice format is not checked against its instrument'
Require-Match $pool 'out_source->loop_mode = SAMPLE_PLAY_LOOP_NONE' 'Multi reverse/loop defaults are not deterministic'
Require-Match $pool 'out_source->reverse = 0U' 'Multi reverse is not explicitly disabled'
Require-Match $runtime 'play_plan\.direction = 0U[\s\S]*play_plan\.loop_mode = BRICK6_SAMPLER_LOOP_NONE' 'Multi play plan does not reject reverse/ping-pong implicitly'
Require-Match $runtime 'common_plan\.loop_mode == \(uint8_t\)SAMPLE_PLAY_LOOP_FORWARD' 'Only the documented forward loop reserves a loop owner'

# VOICES and SPREAD boundaries, stable rank, and live updates.
Require-Match $runtime 'if \(count < 1U\)[\s\S]*count = 1U[\s\S]*if \(count > SAMPLER_MULTI_MAX_VOICES_PER_TRACK\)[\s\S]*count = SAMPLER_MULTI_MAX_VOICES_PER_TRACK' 'VOICES clamp is not enforced'
Require-Match $runtime 'while \(brick6_sampler_runtime_multi_active_count_for_track\(track_id\) > count\)' 'VOICES reduction does not evict excess voices'
Require-Match $runtime 'brick6_sampler_runtime_multi_oldest_track' 'VOICES reduction/steal is not oldest-first'
Require-Match $runtime 'brick6_sampler_runtime_multi_reindex_spread\(track_id\)' 'Voice ranks are not deterministically reindexed'
Require-Match $runtime 'if \(spread < 0\.0f\)[\s\S]*spread = 0\.0f[\s\S]*else if \(spread > 1\.0f\)[\s\S]*spread = 1\.0f' 'SPREAD clamp is not enforced'
Require-Match $runtime 'voice->spread_index / \(float\)\(count - 1U\)[\s\S]*\* spread' 'SPREAD does not use the stable rank formula'
Require-Match $runtime 'g_sampler_multi_track_state\[track_id\]\.spread > 0\.0f' 'SPREAD live stereo promotion guard is missing'
$spreadSetter = $runtime.Substring($runtime.IndexOf('void brick6_sampler_runtime_set_multi_spread'))
$spreadSetter = $spreadSetter.Substring(0, $spreadSetter.IndexOf('float brick6_sampler_runtime_get_multi_spread'))
Forbid-Match $spreadSetter 'brick6_sampler_runtime_multi_stop' 'Changing SPREAD resets active voices'
Require-Match $registry 'brick6_sampler_runtime_set_multi_voice_count\(track' 'VOICES registry setter does not route Multi'
Require-Match $registry 'brick6_sampler_runtime_set_multi_spread\(track' 'SPREAD registry setter does not route Multi'
Require-Match $trackRuntime 'brick6_sampler_runtime_get_multi_voice_count' 'Track runtime still hardcodes Multi voice count'

# Per-voice DSP/VCA before sum, source/resource release, and post-sum track path.
Require-Match $runtime 'mixer_multi_filter_process\(ctx->mix_track_id[\s\S]*brick6_sampler_runtime_multi_apply_spread[\s\S]*out_l\[frame\] \+= voice_l\[frame\]' 'Multi does not filter, spread, then sum per voice'
Require-Match $runtime 'mixer_multi_voice_vca_requires_source\(dsp_state\) == 0U[\s\S]*brick6_sampler_runtime_multi_stop_voice' 'VCA idle does not release the voice'
Require-Match $runtime 'if \(voice->active == 0U\)[\s\S]*mixer_multi_voice_vca_requires_source\(dsp_state\)' 'EOF-before-release does not retain only the DSP tail'
Require-Match $audioRuntime 'mixer_begin_external_mono_native|mixer_begin_external_stereo' 'Multi external publication paths are missing'
Require-Match $mixer 'MIXER_EXTERNAL_FORMAT_MULTI_STEREO|MIXER_EXTERNAL_FORMAT_MULTI_MONO' 'Mixer does not identify the Multi external source'

# The same contract must remain visible to the existing static lifecycle check.
Require-Match (Read-RepoFile 'tests\multi_sampler_vca_lifecycle_validation.ps1') 'release_pending' 'Existing Multi lifecycle validation is not present'

Write-Output 'multi_polyphony_exhaustive_validation=PASS scenarios=mono+stereo+voices1/2/4/8+global8+noteoff+release+oneshot+loop+reverse-rejected+zones+spread+panic+transport+instrument-change+cold-start'
