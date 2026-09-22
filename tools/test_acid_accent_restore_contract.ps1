$ErrorActionPreference = 'Stop'

$source = Get-Content -Raw 'Src/Audio/Engines/acid_engine.c'

foreach ($required in @(
    'v->accent_knob=accent;v->accent=accent',
    'v->vcf_decay_coeff=acid_decay_coeff(accent>0.0f?0.9972f')) {
    if (-not $source.Contains($required)) {
        throw "ACID accent restore contract missing: $required"
    }
}

Write-Output 'ACID accent restore contract: PASS'
