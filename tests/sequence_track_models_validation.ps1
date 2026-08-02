$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$types = Get-Content -Raw (Join-Path $repo 'Inc/Seq/seq_types.h')
$model = Get-Content -Raw (Join-Path $repo 'Src/Seq/seq_model.c')
$runtime = Get-Content -Raw (Join-Path $repo 'Inc/Seq/seq_runtime.h')
if ($types -notmatch '#define SEQ_TRACK_COUNT\s+TRACK_TOPOLOGY_TRACK_COUNT') { throw 'track count authority mismatch' }
if ($types -notmatch '#define SEQ_STEP_MAX_LOCKS 32U') { throw 'step lock capacity mismatch' }
if ($types -notmatch '#define SEQ_PLOCK_POOL_CAP_PER_TRACK 1024U') { throw 'pool capacity mismatch' }
if ($model -notmatch 'pool\[SEQ_TRACK_COUNT\]\[SEQ_PLOCK_POOL_CAP_PER_TRACK\]') { throw 'heterogeneous pool' }
if ($runtime -notmatch 'active_locks\[SEQ_TRACK_COUNT\]\[SEQ_STEP_MAX_LOCKS\]') { throw 'heterogeneous runtime locks' }
if (($types + $model + $runtime) -match 'SEQ_SPECIAL|special_pool|active_locks_special') { throw 'Special model residue' }
'sequence_track_models_validation=PASS tracks=8 steps=64 locks=32 pool=1024 homogeneous=yes'
