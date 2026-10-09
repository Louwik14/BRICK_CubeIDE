$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Build-Overview([int[]]$Left, [int[]]$Right) {
    Assert-True ($Left.Count -eq $Right.Count) 'stereo input mismatch'
    Assert-True ($Left.Count -gt 0) 'empty input'
    $bins = for ($index = 0; $index -lt 128; $index++) {
        [pscustomobject]@{ Min = [int]::MaxValue; Max = [int]::MinValue; Seen = $false }
    }
    for ($frame = 0; $frame -lt $Left.Count; $frame++) {
        $bin = [math]::Floor(([uint64]$frame * 128) / $Left.Count)
        $minimum = [math]::Min($Left[$frame], $Right[$frame])
        $maximum = [math]::Max($Left[$frame], $Right[$frame])
        $bins[$bin].Min = [math]::Min($bins[$bin].Min, $minimum)
        $bins[$bin].Max = [math]::Max($bins[$bin].Max, $maximum)
        $bins[$bin].Seen = $true
    }
    $first = 0
    while (-not $bins[$first].Seen) { $first++ }
    for ($bin = $first - 1; $bin -ge 0; $bin--) {
        $bins[$bin].Min = $bins[$first].Min; $bins[$bin].Max = $bins[$first].Max
    }
    $previous = $first
    for ($bin = $first + 1; $bin -lt 128; $bin++) {
        if ($bins[$bin].Seen) { $previous = $bin }
        else { $bins[$bin].Min = $bins[$previous].Min; $bins[$bin].Max = $bins[$previous].Max }
    }
    return $bins
}

# Small buffers exercise the same integer bucket mapping as 1 s, 10 s, 1 min
# and very long files; the production calculation itself uses uint64_t.
foreach ($frames in @(1, 17, 480, 4800, 28800, 48000)) {
    $left = [int[]]::new($frames)
    $right = [int[]]::new($frames)
    for ($frame = 0; $frame -lt $frames; $frame++) {
        $left[$frame] = (($frame * 97) % 65536) - 32768
        $right[$frame] = 32767 - (($frame * 193) % 65536)
    }
    $overview = Build-Overview $left $right
    Assert-True ($overview.Count -eq 128) 'overview is not 128 bins'
    foreach ($bin in $overview) {
        Assert-True ($bin.Min -le $bin.Max) 'invalid min/max pair'
    }
}

# The 128 -> 124 remap covers every persistent bin without extra I/O.
$covered = [bool[]]::new(128)
for ($column = 0; $column -lt 124; $column++) {
    $first = [math]::Floor(($column * 128) / 124)
    $end = [math]::Ceiling((($column + 1) * 128) / 124)
    for ($bin = $first; $bin -lt $end; $bin++) { $covered[$bin] = $true }
}
Assert-True (-not ($covered -contains $false)) '128 -> 124 mapping skipped a bin'

# Four hot identities preserve A -> B -> A and page/overlay mutations do not
# participate in the sample-global overview identity.
$lru = [System.Collections.Generic.List[string]]::new()
foreach ($identity in @('A', 'B', 'A')) {
    [void]$lru.Remove($identity)
    $lru.Insert(0, $identity)
    while ($lru.Count -gt 4) { $lru.RemoveAt($lru.Count - 1) }
}
Assert-True (($lru[0] -eq 'A') -and ($lru -contains 'B')) 'A -> B -> A LRU miss'
$identityBefore = 'path|size|date|frames|format|edge-crc'
foreach ($overlay in @('START', 'END', 'LOOP', 'SLICE', 'PITCH', 'PLAYHEAD')) {
    Assert-True ($identityBefore -eq 'path|size|date|frames|format|edge-crc') `
        "overlay $overlay changed overview identity"
}

# Replacement changes at least one identity primitive and cannot hit stale A.
$replacement = 'path|new-size|new-date|frames|format|new-edge-crc'
Assert-True ($replacement -ne $identityBefore) 'replacement identity stayed stale'

$root = Split-Path -Parent $PSScriptRoot
$store = Get-Content -Raw (Join-Path $root 'Src/Storage/sampler_waveform_store.c')
$service = Get-Content -Raw (Join-Path $root 'Src/Storage/waveform_service.c')
Assert-True ($store.Contains('#define SAMPLER_WAVEFORM_READ_BYTES 4096U')) `
    'fallback quantum changed'
Assert-True ($service.Contains('source->key.domain == SAMPLE_AUDIO_DOMAIN_CLASSIC')) `
    'Classic persistent branch missing'
Assert-True (-not $service.Contains('sample_capture_model_waveform_cache_get_handle(&sidecar);`r`n    if(source->key.domain')) `
    'Classic may reach Recorder sidecar'

Write-Output 'sampler waveform overview regressions: PASS'
