$ErrorActionPreference = 'Stop'

function Invoke-RetireStep([bool]$retiring, [int]$fifoFree,
                           [bool]$publishAccepted) {
    if (-not $retiring) { return 'idle' }
    if ($fifoFree -eq 0) { return 'pending' }
    if (-not $publishAccepted) { return 'failed' }
    return 'stop-committed'
}

if ((Invoke-RetireStep $false 0 $false) -ne 'idle') {
    throw 'unused Sampler RAM must already be quiescent'
}
if ((Invoke-RetireStep $true 0 $false) -ne 'pending' -or
    (Invoke-RetireStep $true 1 $true) -ne 'stop-committed') {
    throw 'used Sampler RAM must survive backpressure and commit its STOP later'
}
if ((Invoke-RetireStep $true 1 $false) -ne 'failed') {
    throw 'a genuinely rejected STOP must remain a detected failure'
}

$pools = @(
    'Src/Sampler/sampler_ram_pool.c',
    'Src/Sampler/Wavetable/wavetable_publication.inc',
    'Src/Sampler/multi_sample_pool.c'
)

foreach ($pool in $pools) {
    $source = Get-Content -Raw $pool
    if ($source -notmatch 'control_rt_publication_free\(\) == 0U\) continue;') {
        throw "$pool does not defer retirement on FIFO backpressure"
    }
    if ($source -notmatch 'retire_invariant_failed = 1U') {
        throw "$pool no longer detects a rejected STOP with available capacity"
    }
}

$quiesce = Get-Content -Raw 'Src/Storage/project_load_quiesce.c'
if ($quiesce -notmatch 'sample_page_lease_control_all_released\(\) != 0U' -or
    $quiesce -notmatch 'sample_cache_has_pending_sd_work\(\) == 0U' -or
    $quiesce -notmatch 'sampler_ram_pool_retire_idle\(\) != 0U') {
    throw 'Project Load no longer waits for leases, SD cache work, and RAM retirement'
}

$ram = Get-Content -Raw 'Src/Sampler/sampler_ram_pool.c'
if ($ram -notmatch 'CONTROL_AUDIO_PARAM_RAM_RESOURCE_STOP' -or
    $ram -notmatch 'CONTROL_AUDIO_RESOURCE_RETIRE_GRACE_FRAMES' -or
    $ram -notmatch 'sampler_ram_pool_finalize_clear\(i\)') {
    throw 'Sampler RAM retire ordering contract changed'
}
if ($ram -notmatch 'void sampler_ram_pool_init\(void\)[\s\S]*?' +
        'sampler_ram_pool_initialize_empty\(1U\);' -or
    $ram -match 'void sampler_ram_pool_init\(void\)[\s\S]*?' +
        'sampler_ram_pool_reset_quiesced\(\)') {
    throw 'Sampler RAM boot init must not inspect retained NOLOAD slot state'
}

Write-Output 'Project Load retire contract: PASS'
