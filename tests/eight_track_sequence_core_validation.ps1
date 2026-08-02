$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

$topology = Get-Content -Raw (Join-Path $repo 'Inc/Core/track_topology.h')
$snapshot = Get-Content -Raw (Join-Path $repo 'Inc/Seq/seq_step_snapshot.h')
$undo = Get-Content -Raw (Join-Path $repo 'Src/Storage/undo_v2.c')
$clipboard = Get-Content -Raw (Join-Path $repo 'Src/Seq/seq_clipboard.c')
$model = Get-Content -Raw (Join-Path $repo 'Src/Seq/seq_model.c')

if ($topology -notmatch '#define TRACK_TOPOLOGY_TRACK_COUNT 8U') { throw 'track count is not 8' }
if ($snapshot -match 'SNAPSHOT_ROLE|uint8_t role|uint8_t action') { throw 'snapshot still has heterogeneous payload' }
if ($undo -match 'track_topology_identity_t|pending_track_identity|track_identity') { throw 'undo still stores topology identity' }
if ($clipboard -match 'source_identity|track_topology_identity') { throw 'step clipboard still stores topology identity' }
if ($model -notmatch 'pool\[SEQ_TRACK_COUNT\]\[SEQ_PLOCK_POOL_CAP_PER_TRACK\]') { throw 'sequencer pool is not homogeneous' }

'eight_track_sequence_core_validation=PASS slots=8 snapshot=homogeneous undo_identity=index clipboard_identity=none'
