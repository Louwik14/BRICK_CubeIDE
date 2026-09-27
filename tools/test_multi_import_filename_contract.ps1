$ErrorActionPreference = 'Stop'

$source = Get-Content -Raw 'Src/Sampler/multi_sample_import.c'

foreach ($required in @(
    "*p == ' '",
    'return 3U;',
    'filename_root_authoritative = (filename_metadata >= 2U)',
    'filename_velocity_valid = (filename_metadata <= 2U)',
    'fallback_root_available == 0U',
    'return MULTI_SAMPLE_IMPORT_ZONE_LIMIT;')) {
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
# duplicate; the importer duplicate-zone validation must not be bypassed.
$a = Parse-LeadingRoot '60 Trill A.wav'
$b = Parse-LeadingRoot '60 Trill B.wav'
if (($a.Root -ne $b.Root) -or ($a.VelLow -ne $b.VelLow) -or
    ($source -notmatch 'MULTI_SAMPLE_IMPORT_DUPLICATE_ZONE')) {
    throw 'true duplicate-zone rejection was lost'
}

Write-Output 'Multi import filename contract: PASS'
