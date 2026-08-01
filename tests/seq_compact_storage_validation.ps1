$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$patternHeader = Get-Content -Raw (Join-Path $root 'Inc\Storage\pattern_live_ram.h')
$pattern = Get-Content -Raw (Join-Path $root 'Src\Storage\pattern_live_ram.c')
$patternBank = Get-Content -Raw (Join-Path $root 'Src\Storage\pattern_sd_bank.c')
$projectHeader = Get-Content -Raw (Join-Path $root 'Inc\Storage\project_v1.h')
$projectBank = Get-Content -Raw (Join-Path $root 'Src\Storage\project_sd_bank.c')
$model = Get-Content -Raw (Join-Path $root 'Src\Seq\seq_model.c')
$edit = Get-Content -Raw (Join-Path $root 'Src\Seq\seq_edit.c')
$clipboard = Get-Content -Raw (Join-Path $root 'Src\Seq\seq_clipboard.c')
$undo = Get-Content -Raw (Join-Path $root 'Src\Storage\undo_v2.c')

function Assert-Storage([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Assert-Storage ($patternHeader -match '(?s)set_id;\s*/\* Current-format compact slot; legacy slot numbers are not accepted\. \*/\s*seq_param_slot_t param_slot;') 'pattern p-lock slot contract is not explicit'
Assert-Storage ($patternBank -match '#define\s+PATTERN_VERSION\s+5U[\s\S]*hdr->version == PATTERN_VERSION') 'pattern loader does not reject non-current format'
Assert-Storage ($projectHeader -match '#define\s+PROJECT_V1_FILE_VERSION\s+5U') 'project compact format version missing'
Assert-Storage ($projectBank -match 'hdr->version == PROJECT_V1_FILE_VERSION') 'project loader does not reject non-current format'

Assert-Storage ($pattern -match 'pattern_live_seq_block_validate_plock_slots') 'pattern slot validation missing'
Assert-Storage ($pattern -match 'seq_param_iface_slot_to_param\(track') 'pattern load does not validate compact set/slot'
Assert-Storage ($pattern -match 'pattern_live_seq_block_validate_plock_slots\(seq\)[\s\S]*seq_model_init_defaults\(\)') 'pattern slot validation is not before model mutation'
Assert-Storage ($model -match 'seq_param_iface_slot_is_supported\(track, set_id, param_slot\)') 'model p-lock writes accept invalid compact slots'
Assert-Storage ($edit -match 'seq_param_iface_slot_is_supported\(track, set_id, param_slot\)') 'Undo/Redo p-lock apply accepts invalid compact slots'
Assert-Storage ($clipboard -match 'seq_param_iface_slot_is_supported\(target_track, lock->set_id, lock->param_slot\)') 'clipboard paste does not validate compact slots'
Assert-Storage ($undo -match 'seq_param_iface_is_param_supported\(track, set_id, param_slot\)') 'Undo/Redo recording does not validate compact slots'

Write-Output 'seq_compact_storage_validation=PASS pattern=compact-v5 project=compact-v5 load_validation=yes clipboard=yes undo_redo=yes legacy_rejected=yes'
