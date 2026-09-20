$ErrorActionPreference = 'Stop'
$source = Get-Content 'Src/Keyboard/keyboard_note_lifecycle.c' -Raw
if ($source -notmatch 'state->ordered_sample \+ 1U') {
    throw 'Lifecycle ordering implementation is missing.'
}

function Test-Lifecycle([object[]]$events) {
    $active = @{}
    $events | Sort-Object sample, @{ Expression = 'on'; Ascending = $true } |
        ForEach-Object { $active[$_.note] = $_.on }
    return $active
}

$events = [System.Collections.Generic.List[object]]::new()
$sample = 480L
foreach ($note in 60,64,65) { $events.Add([pscustomobject]@{note=$note;on=1;sample=$sample++}) }
$sample = 528L
for ($i=0; $i -lt 64; $i++) {
    foreach ($event in @(@(72,1),@(72,0),@(74,1),@(74,0))) {
        $events.Add([pscustomobject]@{note=$event[0];on=$event[1];sample=$sample++})
    }
}
$active = Test-Lifecycle $events
if (!$active[60] -or !$active[64] -or !$active[65] -or $active[72] -or $active[74]) {
    throw 'Case A failed.'
}

$events.Clear(); $sample = 1000L
for ($round=0; $round -lt 16; $round++) {
    foreach ($note in 48..55) {
        $events.Add([pscustomobject]@{note=$note;on=1;sample=$sample++})
        $events.Add([pscustomobject]@{note=$note;on=0;sample=$sample++})
    }
}
$active = Test-Lifecycle $events
foreach ($value in $active.Values) { if ($value) { throw 'Case B failed.' } }
'keyboard note lifecycle: PASS'
