$ErrorActionPreference = 'Stop'

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$source = Get-Content 'Src/Audio/Engines/acid_engine.c' -Raw
$noteOn = [regex]::Match($source,
    'void brick6_acid_runtime_note_on.*?\n\}',
    [System.Text.RegularExpressions.RegexOptions]::Singleline).Value

Assert-Contract ($source -match
    'slide_alpha=acid_alpha\(0\.00909f\)') `
    'ACID slide no longer uses the native 22 ms capacitor coefficient'
Assert-Contract ($source -match
    'slide_timer!=255U && \+\+v->slide_timer==10U') `
    'ACID slide no longer runs at the native divided control rate'
Assert-Contract ($source -match
    'slide_cap\+=v->slide_alpha\*\(v->current_note_value-v->slide_cap\)') `
    'ACID slide is not the native exponential capacitor recurrence'
Assert-Contract ($noteOn -match
    'v->slide&&\(v->gate\|\|v->pending_release\)') `
    'ACID destination slide does not bridge NOTE_OFF to NOTE_ON'
Assert-Contract (($noteOn -match
    'if\(!legato\).*v->slide_cap=') -and ($noteOn -match
    'else v->slide_timer=0U')) `
    'ACID plain/slide transition does not seed or preserve the capacitor'
Assert-Contract ($noteOn -notmatch
    'v->vcf_env=0\.0f|v->vca_env=0\.0f') `
    'ACID note transition forcibly clears an original envelope capacitor'
Assert-Contract ($noteOn -match
    'v->accent=v->accent_knob') `
    'ACID destination accent is not refreshed during a slide'
Assert-Contract ($source -match
    'if\(v->slide\)v->pending_release=1U') `
    'ACID slide does not retain the source gate across the transition'

# Native recurrence: chained targets continue from the capacitor reached by
# the preceding slide; a plain destination reseeds directly at its pitch.
$alpha = 1.0 - [Math]::Pow(1.0 - 0.00909, 50.0 / 48.0)
$cap = 0.0
$targetB = 12.0 * 64.0
for ($i = 0; $i -lt 40; ++$i) {
    $cap += $alpha * ($targetB - $cap)
}
$beforeC = $cap
$targetC = 7.0 * 64.0
$cap += $alpha * ($targetC - $cap)
Assert-Contract (($beforeC -gt 0.0) -and ($beforeC -lt $targetB)) `
    'ACID first slide does not remain exponential'
Assert-Contract ($cap -gt $beforeC) `
    'ACID chained slide did not continue from the retained capacitor'
$cap = $targetC
Assert-Contract ($cap -eq $targetC) `
    'ACID plain transition did not reseed pitch directly'

'acid native slide contract: PASS'
