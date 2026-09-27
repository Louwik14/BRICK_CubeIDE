$ErrorActionPreference = 'Stop'

$lifecycle = Get-Content -Raw 'Src/Sampler/PageCache/sample_page_cache_lifecycle.inc'
$clearKey = [regex]::Match(
    $lifecycle,
    'void sample_page_cache_clear_key\(sample_audio_key_t key\)(?<body>[\s\S]*?)uint8_t sample_page_cache_cancel_reserved_page_key')
if (-not $clearKey.Success) {
    throw 'sample_page_cache_clear_key implementation not found'
}

$body = $clearKey.Groups['body'].Value
$dropOwner = $body.IndexOf('g_sample_page_desc[i].static_resident = 0U;')
$loading = $body.IndexOf('state == SAMPLE_PAGE_LOADING')
$contractual = $body.IndexOf('sample_page_cache_page_is_contractual')
if (($dropOwner -lt 0) -or ($loading -lt 0) -or ($contractual -lt 0) -or
    ($dropOwner -gt $loading) -or ($dropOwner -gt $contractual)) {
    throw 'key teardown must withdraw static ownership before loading/lease handling'
}

$pool = Get-Content -Raw 'Src/Sampler/multi_sample_pool.c'
if ($pool -notmatch 'sample_global_pool_clear_backend\(SAMPLE_GLOBAL_KIND_MULTI, instrument_id\);[\s\S]*?sample_page_cache_clear_key\(key\);') {
    throw 'Multi teardown no longer clears catalogue accounting before cache keys'
}

$loader = Get-Content -Raw 'Src/Sampler/multi_sample_loader.c'
if ($loader -notmatch 'multi_loader_bulk_prepare_plan_pages[\s\S]*?MULTI_SAMPLE_LOAD_NOT_ENOUGH_CACHE') {
    throw 'CACHE FULL admission path changed; update this regression contract'
}

Write-Output 'Multi cache release contract: PASS'
