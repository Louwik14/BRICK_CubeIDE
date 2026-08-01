$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
function Read-Source([string]$relativePath) {
    return Get-Content -Raw (Join-Path $root $relativePath)
}
function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}
function Get-FunctionTail([string]$source, [string]$signature) {
    $start = $source.IndexOf($signature)
    Assert-Contract ($start -ge 0) "Missing function: $signature"
    return $source.Substring($start)
}
function Get-CaseBlock([string]$source, [string]$param) {
    $start = $source.IndexOf("case ${param}:")
    Assert-Contract ($start -ge 0) "Missing backend case: $param"
    $end = $source.IndexOf("case ", $start + 6)
    $default = $source.IndexOf("default:", $start + 6)
    if (($end -lt 0) -or (($default -ge 0) -and ($default -lt $end))) { $end = $default }
    if ($end -lt 0) { $end = $source.Length }
    return $source.Substring($start, $end - $start)
}

$runtime = Read-Source 'Src/Core/track_runtime.c'
$dispatcher = Get-FunctionTail (Read-Source 'Src/Param/param_registry_tone_backends.c') 'uint8_t param_backend_apply_track_value'
$mixBackend = Get-FunctionTail (Read-Source 'Src/Param/param_registry_backends.c') 'uint8_t param_backend_apply_mix_track'
$registry = Read-Source 'Src/Param/param_registry.c'
$iface = Read-Source 'Src/Seq/seq_param_iface.c'
$macro = Read-Source 'Src/Param/param_macro.c'
$envPage = Read-Source 'Src/UI/pages/ui_page_template_env.c'
$transition = Read-Source 'Src/Param/param_registry_transition.c'

$vca = @(
    'PARAM_VCA_ATTACK', 'PARAM_VCA_DECAY', 'PARAM_VCA_SUSTAIN',
    'PARAM_VCA_RELEASE', 'PARAM_ENV_RETRIG_VCA'
)
$stateFields = @{
    PARAM_VCA_ATTACK = 'vca_attack'
    PARAM_VCA_DECAY = 'vca_decay'
    PARAM_VCA_SUSTAIN = 'vca_sustain'
    PARAM_VCA_RELEASE = 'vca_release'
    PARAM_ENV_RETRIG_VCA = 'env_retrig_vca'
}
$setters = @{
    PARAM_VCA_ATTACK = 'mixer_set_track_vca_attack'
    PARAM_VCA_DECAY = 'mixer_set_track_vca_decay'
    PARAM_VCA_SUSTAIN = 'mixer_set_track_vca_sustain'
    PARAM_VCA_RELEASE = 'mixer_set_track_vca_release'
    PARAM_ENV_RETRIG_VCA = 'mixer_set_track_vca_retrigger_hard'
}

foreach ($param in $vca) {
    $rule = [regex]::Match($runtime, "(?s)case ${param}:.*?rule\.domain = TRACK_RUNTIME_PARAM_DOMAIN_(\w+);.*?rule\.resource = TRACK_RUNTIME_RESOURCE_(\w+);")
    Assert-Contract $rule.Success "Missing runtime rule: $param"
    Assert-Contract ($rule.Groups[1].Value -eq 'ENV') "$param is not owned by ENV"
    Assert-Contract ($rule.Groups[2].Value -eq 'MIX') "$param does not use RESOURCE_MIX"
    Assert-Contract ($iface -match "TRACK_RUNTIME_PARAM_DOMAIN_ENV[\s\S]*SEQ_PLOCK_SET_ENV") 'ENV p-lock mapping is missing'
    Assert-Contract ($macro -match 'case TRACK_RUNTIME_PARAM_DOMAIN_ENV:[\s\S]*SEQ_PLOCK_SET_ENV') 'Macro ENV p-lock mapping is missing'

    $case = Get-CaseBlock $mixBackend $param
    Assert-Contract ($case -match 'track_sound_state_get\(track\)') "$param does not update track_sound_state"
    Assert-Contract ($case -match "state->${stateFields[$param]}") "$param canonical field is not written"
    Assert-Contract ($case -match "${setters[$param]}\(") "$param mixer setter is not called"
    Assert-Contract ($case -match 'update_base_state != 0U') "$param base-state update is not guarded"

    Assert-Contract ($registry -match "case ${param}:[\s\S]*?state->${stateFields[$param]}") "$param getter is not backed by track_sound_state"
    Assert-Contract ($transition -match $param) "$param is absent from lane reapply"
}

Assert-Contract ($dispatcher -match '(?s)uses_mix_backend.*?rule\.resource == TRACK_RUNTIME_RESOURCE_MIX.*?rule\.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV') 'Dispatcher does not authorize ENV + RESOURCE_MIX'
Assert-Contract ($dispatcher -match '(?s)if \(uses_mix_backend != 0U\).*?param_backend_apply_mix_track') 'Dispatcher does not route RESOURCE_MIX to mixer backend'
Assert-Contract ($dispatcher -notmatch 'id\s*==\s*PARAM_VCA_(ATTACK|DECAY|SUSTAIN|RELEASE)') 'Dispatcher regressed to a VCA exception list'
Assert-Contract ($dispatcher -match 'id == PARAM_ENV_RETRIG_FILTER') 'Existing filter retrigger routing was removed'

Assert-Contract ($runtime -match '(?s)track_runtime_param_is_vca.*?PARAM_ENV_RETRIG_VCA') 'VCA retrigger is absent from VCA runtime capability rules'
Assert-Contract ($runtime -match '(?s)track_runtime_param_is_vca\(param\).*?track_runtime_supports_vca_gate\(ctx\) == 0U') 'Effective VCA status does not use the VCA capability gate'
Assert-Contract ($mixBackend -match '(?s)param_backend_is_vca_param\(id\).*?track_runtime_supports_vca_gate\(ctx\) == 0U') 'Mixer backend lacks the direct-apply VCA capability guard'
Assert-Contract ($mixBackend -match '(?s)PARAM_ENV_RETRIG_VCA.*?mixer_set_track_vca_retrigger_hard') 'VCA retrigger does not reach the mixer setter'

Assert-Contract ($envPage -match 'PARAM_VCA_ATTACK.*PARAM_VCA_RELEASE') 'VCA page no longer exposes the ADSR set'
Assert-Contract ($registry -match 'param_registry_apply_track_edit[\s\S]*?param_registry_apply_track_value') 'Encoder edits do not use the common apply seam'
foreach ($relativePath in @(
    'Src/UI/ui_core_clipboard.c', 'Src/Storage/undo_v2.c', 'Src/Core/track_snapshot.c',
    'Src/Param/param_registry_transition.c', 'Src/Storage/pattern_live_ram.c',
    'Src/Storage/kit_v1.c', 'Src/Storage/patch_v1.c'
)) {
    Assert-Contract ((Read-Source $relativePath) -match 'param_registry_apply_track_value') "$relativePath bypasses the common apply seam"
}

$support = Get-FunctionTail $runtime 'uint8_t track_runtime_supports_vca_gate'
Assert-Contract ($support -match 'track_runtime_ctx_is_sampler_clip_or_looper') 'Stream/Looper VCA exclusion was removed'
Assert-Contract ($support -match 'TRACK_RUNTIME_ENGINE_(DRUM|SAMPLER|PRISM|STACK|WAVE|DELUGE)') 'Supported engine VCA gate is incomplete'
Assert-Contract ($support -match 'TRACK_RUNTIME_FAMILY_EXTERNAL') 'External VCA contract was changed unexpectedly'

Write-Output 'vca_dispatcher_validation=PASS domain=ENV resource=MIX backend=mixer state=canonical readback=yes plock=ENV unsupported_gate=preserved'
