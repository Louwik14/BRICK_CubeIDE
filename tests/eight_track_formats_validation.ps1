$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$pattern = Get-Content -Raw (Join-Path $repo 'Inc/Storage/pattern_live_ram.h')
$project = Get-Content -Raw (Join-Path $repo 'Inc/Storage/project_v1.h')
$kit = Get-Content -Raw (Join-Path $repo 'Inc/Storage/kit_v1.h')
$patch = Get-Content -Raw (Join-Path $repo 'Inc/Storage/patch_v1.h')
$snapshot = Get-Content -Raw (Join-Path $repo 'Inc/Core/track_snapshot.h')

if ($pattern -notmatch 'pattern_v1_track_seq_t tracks\[SEQ_TRACK_COUNT\]') { throw 'Pattern is not homogeneous' }
if (($pattern + $project + $kit + $patch + $snapshot) -match 'track_topology_identity_t|topology_role|topology_ordinal|special_sequence') { throw 'format identity/role residue' }
if ($project -notmatch 'PROJECT_V1_FILE_VERSION\s+6U') { throw 'Project version not bumped' }
if ($kit -notmatch 'KIT_V1_TRACK_MAX\s+SEQ_TRACK_COUNT') { throw 'Kit capacity is not eight tracks' }

'eight_track_formats_validation=PASS pattern=8 project=v6 kit=8 patch=current track_snapshot=homogeneous'
