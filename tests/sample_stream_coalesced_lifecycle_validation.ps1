$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$manager = Get-Content (Join-Path $root 'Src/Sampler/sample_stream_manager.c') -Raw
$cache = Get-Content (Join-Path $root 'Src/Sampler/sample_page_cache.c') -Raw

if ($cache -notmatch 'sample_page_cache_find_window_owner_key') {
    throw 'The page cache cannot select a remaining owner for a shared page.'
}
if ($manager -notmatch 'sample_page_cache_release_window_owner\(owner_kind, owner_id, owner_generation\);[\s\S]*sample_page_cache_find_window_owner_key\(pending->key,[\s\S]*pending->owner_kind = remaining_owner.owner_kind;') {
    throw 'release_owner does not reassign coalesced pending work after releasing its lock.'
}
if ($manager -notmatch 'sample_stream_manager_repair_queued_pages\(\);[\s\S]*sample_stream_manager_pick_next') {
    throw 'The SD service does not repair queued pages before selecting work.'
}
if ($manager -notmatch 'sample_stream_manager_find_pending_key\(desc->key, page_index\) == 0[\s\S]*sample_stream_manager_note_pending_key\(desc->key') {
    throw 'Active-state duplicate guards can still suppress orphan repair.'
}

$owners = @('voice-a', 'voice-b')
$pendingOwner = 'voice-a'
$owners = @($owners | Where-Object { $_ -ne $pendingOwner })
if ($owners.Count -eq 0) {
    $pendingOwner = $null
} else {
    $pendingOwner = $owners[0]
}
if ($pendingOwner -ne 'voice-b') {
    throw 'Shared pending work was not retained and reassigned to the remaining owner.'
}
$queued = $true
$pending = ($null -ne $pendingOwner)
if ($queued -and -not $pending) {
    throw 'A demanded QUEUED page was left without pending SD work.'
}

Write-Output 'sample stream coalesced lifecycle validation: PASS'
