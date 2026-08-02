$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$header = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_division_catalog.h')
$catalog = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_division_catalog.c')
$engine = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_engine.c')
$uiPage = Get-Content -Raw (Join-Path $root 'Src/UI/pages/ui_page_midi_fx.c')
$paramCatalog = Get-Content -Raw (Join-Path $root 'Src/Param/param_registry_catalog.c')
$wrappers = Get-Content -Raw (Join-Path $root 'Src/Param/param_registry_apply_wrappers.c')
$uiParam = Get-Content -Raw (Join-Path $root 'Src/UI/ui_param.c')
$state = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_state.c')
$stateHeader = Get-Content -Raw (Join-Path $root 'Inc/NoteFx/note_fx_state.h')

if ($header -notmatch 'SEQ_DIVISION_ARP_COUNT 8U' -or
    $header -notmatch 'ratio_q16' -or
    $catalog -notmatch 'SEQ_DIVISION_RATIO_Q16\(8U, 3U\)' -or
    $catalog -notmatch '"1/32T"\s*,\s*1U\s*,\s*3U') {
    throw 'canonical Q16 division catalog is incomplete'
}
if ($catalog -notmatch 'seq_division_track_div_from_ui' -or
    $catalog -notmatch 'seq_division_track_div_to_ui' -or
    $catalog -notmatch 'seq_division_period_samples') {
    throw 'track conversion or period API is missing'
}
if ($engine -match 'numerator\[|denominator\[' -or
    $engine -notmatch 'seq_division_period_samples') {
    throw 'NoteFx keeps a private division conversion table'
}
if ($uiPage -match 'rate_labels' -or $uiPage -notmatch 'seq_division_arp_label') {
    throw 'ARP virtual UI labels are not canonical'
}
if ($paramCatalog -match 'g_seq_div_labels|g_midi_fx_rate_labels' -or
    $paramCatalog -notmatch 'seq_division_track_labels' -or
    $paramCatalog -notmatch 'seq_division_arp_labels') {
    throw 'parameter catalog owns a duplicate division label table'
}
if ($wrappers -match 'seq_div_ui_to_runtime' -or
    $uiParam -match 'ui_param_seq_div_ui_to_runtime|ui_param_seq_div_runtime_to_ui' -or
    $wrappers -notmatch 'seq_division_track_div_from_ui' -or
    $uiParam -notmatch 'seq_division_track_div_to_ui') {
    throw 'boundary conversion is duplicated outside the catalog'
}
if ($state -notmatch 'g_note_fx_model_defaults' -or
    $state -notmatch 'note_fx_state_normalize_track' -or
    $stateHeader -notmatch 'note_fx_state_normalize_track') {
    throw 'model-aware defaults or central normalization is missing'
}

Write-Output 'seq_division_catalog_validation=PASS arp=8 ternary=4 q16=canonical ui_conversion=single persistence_ordinals=unchanged state_normalization=central'
