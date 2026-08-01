$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$source = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_play_scheduler.c')
$header = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_play_scheduler.h')

if ($source -notmatch '(?s)static uint8_t seq_play_scheduler_push_note_pair.*?seq_play_scheduler_enter_critical\(\).*?SEQ_PLAY_SCHEDULER_EVENT_CAP - g_seq_play_event_count\) < 2U.*?seq_play_scheduler_alloc_event_token\(\).*?SEQ_PLAY_SCHEDULER_EVT_NOTE_ON.*?SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF.*?return 1U;') {
    throw 'note pair reservation is not atomic and ordered'
}
if ($source -match '(?s)seq_play_scheduler_push_note_pair.*?seq_play_scheduler_push\(note_on_sample_time') {
    throw 'note pair still delegates to independent pushes'
}
if ($source -notmatch 'queue_overflow_drop_count \+= 2U;' -or $source -notmatch 'note_pair_overflow_drop_count\+\+') {
    throw 'pair overflow diagnostics are incomplete'
}
if ($header -notmatch 'note_pair_overflow_drop_count') {
    throw 'pair overflow diagnostic is not exposed'
}

function Reserve-NotePair([int]$count, [int]$capacity, [int]$nextToken) {
    if (($capacity - $count) -lt 2) {
        return [pscustomobject]@{ accepted = $false; count = $count; token = $nextToken; events = @(); dropped = 2 }
    }
    $token = $nextToken + 1
    return [pscustomobject]@{
        accepted = $true; count = $count + 2; token = $token; dropped = 0
        events = @([pscustomobject]@{ type = 'on'; token = $token }, [pscustomobject]@{ type = 'off'; token = $token })
    }
}

$full = Reserve-NotePair 512 512 7
if ($full.accepted -or $full.count -ne 512 -or $full.token -ne 7 -or $full.events.Count -ne 0 -or $full.dropped -ne 2) { throw '0-place pair rejection failed' }
$one = Reserve-NotePair 511 512 7
if ($one.accepted -or $one.count -ne 511 -or $one.token -ne 7 -or $one.events.Count -ne 0 -or $one.dropped -ne 2) { throw '1-place pair rejection failed' }
$two = Reserve-NotePair 510 512 7
if (-not $two.accepted -or $two.count -ne 512 -or $two.events.Count -ne 2 -or $two.events[0].type -ne 'on' -or $two.events[1].type -ne 'off' -or $two.events[0].token -ne $two.events[1].token) { throw '2-place pair insertion order failed' }
$normal = Reserve-NotePair 0 512 0
if (-not $normal.accepted -or $normal.count -ne 2) { throw 'normal pair insertion failed' }
$retrig = Reserve-NotePair $normal.count 512 $normal.token
if (-not $retrig.accepted -or $retrig.count -ne 4 -or $retrig.events[0].type -ne 'on' -or $retrig.events[1].type -ne 'off') { throw 'retrig pair insertion failed' }

Write-Output 'seq_play_scheduler_pair_validation=PASS zero=reject one=reject two=ordered retrig=ordered normal=ordered orphan_on=none'
