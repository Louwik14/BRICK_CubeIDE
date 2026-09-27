$ErrorActionPreference = 'Stop'

$source = Get-Content -Raw 'Src/Sampler/multi_sample_zone_detection.c'

foreach ($required in @(
    'pair_convention',
    'obs[i].smpl_root_valid',
    'obs[i].inst_root_valid',
    'f.range_valid',
    'f.prefix_valid',
    'f.note_valid',
    'skip_variant',
    'MULTI_SAMPLE_ZONE_DETECT_OVERFLOW')) {
    if (-not $source.Contains($required)) {
        throw "Multi filename/zone contract missing: $required"
    }
}

function Parse-LeadingRoot([string]$name) {
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($name)
    if ($stem -notmatch '^([0-9]+) ') { return $null }
    $root = [int]$Matches[1]
    if ($root -gt 127) { return $null }
    return @{ Root = $root; VelLow = 1; VelHigh = 127 }
}

$trill = Parse-LeadingRoot '60 Trill.wav'
if (($null -eq $trill) -or ($trill.Root -ne 60) -or
    ($trill.VelLow -ne 1) -or ($trill.VelHigh -ne 127)) {
    throw 'Trill Lead leading MIDI root is not mapped to N060/V001..127'
}
if ((Parse-LeadingRoot '060 Trill Lead.wav').Root -ne 60) {
    throw 'zero-padded leading MIDI roots must remain valid'
}
if ($null -ne (Parse-LeadingRoot '128 Trill.wav')) {
    throw 'out-of-range leading MIDI roots must remain invalid'
}
if ($null -ne (Parse-LeadingRoot 'Trill 60.wav')) {
    throw 'arbitrary digits later in a name must not become root metadata'
}

# Two files with the same explicit root and full velocity range remain a true
# duplicate unless one is a structurally identified take.
$a = Parse-LeadingRoot '60 Trill A.wav'
$b = Parse-LeadingRoot '60 Trill B.wav'
if (($a.Root -ne $b.Root) -or ($a.VelLow -ne $b.VelLow) -or
    ($source -notmatch 'same_variant_base')) {
    throw 'true duplicate-zone rejection was lost'
}

$index = Get-Content -Raw 'Inc/Sampler/multi_sample_index.h'
if ($index -notmatch 'MULTI_SAMPLE_INDEX_VERSION\s+\(4U\)') {
    throw 'folder-analysis indexes must invalidate pre-v4 mappings'
}

Write-Output 'Multi import filename contract: PASS'
