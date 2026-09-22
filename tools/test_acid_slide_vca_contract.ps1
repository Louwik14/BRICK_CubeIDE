$ErrorActionPreference = 'Stop'

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$engineHeader = Get-Content 'Inc/Seq/seq_engine.h' -Raw
$engine = Get-Content 'Src/Seq/seq_engine.c' -Raw
$port = Get-Content 'Src/Seq/seq_engine_port_h743.c' -Raw
$executor = Get-Content 'Src/Audio/audio_command_executor.c' -Raw
$mixerIo = Get-Content 'Src/Audio/Mixer/mixer_track_io.inc' -Raw
$backend = Get-Content 'Src/Param/param_registry_backends.c' -Raw

Assert-Contract ($engineHeader -match
    'SEQ_ENGINE_EVENT_TRANSITION_PARAM\s*=\s*0,\s*SEQ_ENGINE_EVENT_NOTE_OFF') `
    'transition parameters are not ordered before NOTE_OFF'
Assert-Contract ($engine -match 'PARAM_TB303_SLIDE' -and
    $engine -match 'PARAM_ACID_SLIDE' -and
    $engine -match 'SEQ_ENGINE_EVENT_TRANSITION_PARAM') `
    'ACID/TB303 SLIDE is not transition-scoped'
Assert-Contract ($port -match 'SEQ_ENGINE_EVENT_TRANSITION_PARAM' -and
    $executor -match 'SEQ_ENGINE_EVENT_TRANSITION_PARAM') `
    'transition parameters are not transported/applied as parameters'

function Invoke-Transition([bool]$oldGate, [bool]$destinationSlide) {
    # Runtime class order: transition parameter, old NOTE_OFF, new NOTE_ON.
    $slide = $destinationSlide
    $gate = $oldGate
    $pending = $false
    if ($gate) {
        if ($slide) { $pending = $true } else { $gate = $false }
    }
    $legato = $slide -and ($gate -or $pending)
    $gate = $true
    $pending = $false
    return [pscustomobject]@{ Gate = $gate; Legato = $legato; Slide = $slide }
}

foreach ($engineName in 'ACID','TB303') {
    $normal = Invoke-Transition $true $false
    Assert-Contract (-not $normal.Legato -and $normal.Gate) `
        "${engineName}: non-slid destination retrigger contract failed"

    $slide = Invoke-Transition $true $true
    Assert-Contract ($slide.Legato -and $slide.Gate) `
        "${engineName}: destination slide did not preserve gate"

    $chain = Invoke-Transition $slide.Gate $true
    Assert-Contract ($chain.Legato -and $chain.Gate) `
        "${engineName}: consecutive slide failed"

    $exit = Invoke-Transition $chain.Gate $false
    Assert-Contract (-not $exit.Legato -and $exit.Gate) `
        "${engineName}: slide exit remained sticky"

    $wrap = Invoke-Transition $exit.Gate $true
    Assert-Contract ($wrap.Legato -and $wrap.Gate) `
        "${engineName}: pattern wrap destination slide failed"
}

Assert-Contract ($mixerIo -match
    'if \(filter->vca_note_enabled == 0U\)\s*return;') `
    'ENV VCA OFF does not suppress NOTE_ON'
Assert-Contract ($mixerIo -match
    'filter->vca_note_enabled == 0U\) \|\| \(filter->vca_enabled == 0U\)') `
    'ENV VCA OFF does not suppress NOTE_OFF'
Assert-Contract ($mixerIo -match 'filter->vca_env_value = 1\.0f') `
    'ENV VCA OFF does not establish unity bypass gain'
Assert-Contract ($backend -match 'mixer_set_track_vca_note_enabled') `
    'ENV VCA parameter is not connected to note-envelope enable'

'acid/tb303 slide and VCA contract: PASS'
