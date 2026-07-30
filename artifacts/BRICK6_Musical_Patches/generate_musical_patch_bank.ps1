param(
    [string]$RepoRoot = "",
    [string]$OutputRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
} else {
    $RepoRoot = (Resolve-Path $RepoRoot).Path
}
if (-not (Test-Path (Join-Path $RepoRoot "Inc\Storage\patch_v1.h"))) {
    throw "RepoRoot does not point to the BRICK6 source tree."
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $RepoRoot "artifacts\BRICK6_Musical_Patches"
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$PatchDir = Join-Path $OutputRoot "BRICK\PATCH"
$ZipPath = Join-Path (Split-Path $OutputRoot -Parent) "BRICK6_Musical_Patches_P064-P127.zip"

$HeaderSize = 56
$PayloadSize = 3848
$MemberSize = 952
$MetadataSize = 40
$SoundSize = 260
$ToneSize = 512
$PatchMagic = [uint32]0x54503642
$PatchVersion = [uint16]1
$FamilySynth = [byte]5
$TypePrism = [byte]3
$TypeStack = [byte]11
$TypeDeluge = [byte]13
$Width = [byte]1

function Set-U8([byte[]]$Buffer, [int]$Offset, [int]$Value) {
    $Buffer[$Offset] = [byte]$Value
}
function Set-U16([byte[]]$Buffer, [int]$Offset, [int]$Value) {
    [Array]::Copy([BitConverter]::GetBytes([uint16]$Value), 0, $Buffer, $Offset, 2)
}
function Set-U32([byte[]]$Buffer, [int]$Offset, [uint32]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Buffer, $Offset, 4)
}
function Set-F32([byte[]]$Buffer, [int]$Offset, [single]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Buffer, $Offset, 4)
}
function Get-U16([byte[]]$Buffer, [int]$Offset) {
    return [BitConverter]::ToUInt16($Buffer, $Offset)
}
function Get-U32([byte[]]$Buffer, [int]$Offset) {
    return [BitConverter]::ToUInt32($Buffer, $Offset)
}
function Get-F32([byte[]]$Buffer, [int]$Offset) {
    return [BitConverter]::ToSingle($Buffer, $Offset)
}
function Set-Ascii([byte[]]$Buffer, [int]$Offset, [int]$Capacity, [string]$Text) {
    $bytes = [Text.Encoding]::ASCII.GetBytes($Text)
    if ($bytes.Length -ge $Capacity) {
        throw "Text '$Text' exceeds the $($Capacity - 1)-character persistent field."
    }
    [Array]::Clear($Buffer, $Offset, $Capacity)
    [Array]::Copy($bytes, 0, $Buffer, $Offset, $bytes.Length)
}
function Get-Ascii([byte[]]$Buffer, [int]$Offset, [int]$Capacity) {
    $length = 0
    while (($length -lt $Capacity) -and ($Buffer[$Offset + $length] -ne 0)) {
        $length++
    }
    return [Text.Encoding]::ASCII.GetString($Buffer, $Offset, $length)
}
function Get-Checksum([byte[]]$Data) {
    [uint32]$crc = 5381
    foreach ($b in $Data) {
        $crc = [uint32](([uint64]$crc * 33) -band 0xFFFFFFFFL)
        $crc = [uint32]($crc -bxor [uint32]$b)
    }
    return $crc
}
function Parse-Floats([string]$Text, [int]$Count) {
    $parts = $Text.Split(",")
    if ($parts.Count -ne $Count) {
        throw "Expected $Count floats in '$Text'."
    }
    $values = New-Object single[] $Count
    for ($i = 0; $i -lt $Count; $i++) {
        $values[$i] = [single]::Parse($parts[$i], [Globalization.CultureInfo]::InvariantCulture)
    }
    return $values
}
function Get-ParamIds([string]$HeaderPath) {
    $map = @{}
    $inside = $false
    $next = 0
    foreach ($raw in Get-Content $HeaderPath) {
        $line = ($raw -replace "/\*.*?\*/", "" -replace "//.*$", "").Trim()
        if (-not $inside) {
            if ($line -match "^enum\s*\{") { $inside = $true }
            continue
        }
        if ($line -match "^\};") { break }
        if ($line -match "^(PARAM_[A-Z0-9_]+)\s*(?:=\s*([0-9]+))?\s*,?$") {
            $name = $Matches[1]
            if ($Matches[2]) { $next = [int]$Matches[2] }
            $map[$name] = $next
            $next++
        }
    }
    if (-not $map.ContainsKey("PARAM_COUNT")) {
        throw "Unable to parse the real param_id_t catalogue."
    }
    return $map
}
function Assert-Range([single]$Value, [single]$Min, [single]$Max, [string]$Label) {
    if ([single]::IsNaN($Value) -or [single]::IsInfinity($Value) -or ($Value -lt $Min) -or ($Value -gt $Max)) {
        throw "$Label=$Value is not finite or outside [$Min,$Max]."
    }
}

if (-not [BitConverter]::IsLittleEndian) {
    throw "The B6P serializer requires a little-endian host."
}

$Param = Get-ParamIds (Join-Path $RepoRoot "Inc\Param\param_store.h")

$rows = @'
Velvet Dawn|Pad|Stack|11,12,0|0.52,0.34,0.00|0.00,0.02,0.00|0.30,0.64,0.50|0.42,0.38,0.50|0.56,0.42,0.50|82|18|42|Drift|0.18|0.08|Warm morphing analog pad
Dark Harbor|Pad|Prism|6,12|0.56,0.20|0.50,0.50|0.28,0.38|0.32,0.24|0.08,0.00|62|30|48|Evolve|0.22|0.06|Dark sub and sine pad with slow motion
Glass Veil|Pad|Prism|23,18|0.42,0.25|0.50,0.57|0.62,0.70|0.72,0.58|0.10,0.04|104|12|22|Shimmer|0.28|0.12|Glassy bell partials suspended in air
Amber Poly|Pad|Stack|7,0,3|0.34,0.26,0.20|0.00,0.05,-12.00|0.46,0.36,0.62|0.52,0.42,0.44|0.45,0.52,0.50|76|20|38|Drift|0.18|0.05|Warm triple analog chordless bed
Cloud Choir|Pad|Prism|17,16|0.40,0.24|0.50,0.50|0.58,0.44|0.64,0.58|0.04,0.02|92|16|35|Evolve|0.32|0.10|Breathy vowel choir pad
Slow Aurora|Pad|Deluge|5|0.62|0.00|0.68|0.00|4.00|86|14|28|Shimmer|0.26|0.09|Analog saw pad with a slow opening halo
Blue Distance|Pad|Stack|9,11,3|0.38,0.24,0.16|0.00,12.00,-12.00|0.36,0.22,0.70|0.68,0.40,0.36|0.58,0.64,0.50|88|10|30|Evolve|0.30|0.08|Wide swarm and octave atmosphere
Soft Circuit|Pad|Deluge|3|0.66|0.00|0.38|90.00|-3.00|70|22|42|Pulse|0.16|0.04|Muted analog-square pulse pad
Frozen Lake|Pad|Prism|30,23|0.38,0.22|0.50,0.62|0.42,0.68|0.78,0.66|0.06,0.10|108|18|18|Shimmer|0.34|0.14|Icy spectral line and bell pad
Copper Air|Pad|Stack|12,1,0|0.46,0.25,0.12|0.00,0.03,12.00|0.52,0.34,0.50|0.40,0.62,0.50|0.38,0.55,0.50|80|24|40|Drift|0.20|0.06|Organic trimorph pad with a soft overtone
Moon Current|Pad|Deluge|1|0.68|0.00|0.50|180.00|2.00|96|8|20|Evolve|0.30|0.12|Pure triangle ambient current
Analog Mist|Pad|Prism|14,3|0.38,0.25|0.50,0.50|0.34,0.58|0.48,0.36|0.02,0.00|74|26|44|Drift|0.24|0.07|Swarm and sine-triangle analog mist
Rubber Pick|Pluck|Stack|3,1,0|0.58,0.18,0.00|0.00,12.00,0.00|0.72,0.40,0.50|0.34,0.50,0.50|0.46,0.50,0.50|74|16|72|Pluck|0.06|0.03|Round sub pluck with elastic snap
FM Droplet|Pluck|Prism|20,3|0.52,0.14|0.50,0.62|0.70,0.48|0.60,0.44|0.34,0.04|104|10|62|Pluck|0.12|0.04|Clean FM water-drop pluck
Bright Peg|Pluck|Deluge|4|0.64|0.00|0.50|0.00|0.00|96|8|66|Pluck|0.08|0.03|Bright saw peg with tight filter strike
Wood Tick|Pluck|Stack|1,3,0|0.48,0.20,0.00|0.00,-12.00,0.00|0.28,0.58,0.50|0.24,0.40,0.50|0.38,0.50,0.50|62|12|58|Pluck|0.04|0.02|Dry organic wooden tick
Crystal Pin|Pluck|Prism|23,13|0.40,0.16|0.50,0.69|0.62,0.52|0.74,0.64|0.12,0.02|114|14|48|Shimmer|0.16|0.06|Tiny crystal and ring-mod pin
Soft Pizz|Pluck|Prism|12,6|0.52,0.13|0.50,0.50|0.38,0.30|0.42,0.28|0.02,0.00|68|18|54|Pluck|0.05|0.02|Soft sine pizzicato
Sync Chip|Pluck|Stack|8,6,0|0.46,0.18,0.00|0.00,12.00,0.00|0.64,0.46,0.50|0.36,0.54,0.50|0.52,0.42,0.50|100|20|70|Pulse|0.08|0.03|Percussive square-sync chip
FM Reed|Pluck|Prism|21,16|0.48,0.12|0.50,0.57|0.56,0.42|0.38,0.46|0.26,0.06|86|22|64|Pluck|0.07|0.03|Feedback-FM reed pluck
Mellow Dot|Pluck|Deluge|1|0.70|0.00|0.50|0.00|-2.00|70|6|45|Pluck|0.04|0.01|Gentle triangle dot
Vowel Pick|Pluck|Prism|17,18|0.46,0.12|0.50,0.62|0.64,0.56|0.58,0.48|0.04,0.02|92|16|60|Talk|0.10|0.04|Short articulate vowel pick
Satin Lead|Lead|Prism|3,12|0.55,0.16|0.50,0.50|0.48,0.36|0.42,0.34|0.02,0.00|94|10|22|Express|0.12|0.05|Soft singing sine-triangle lead
Fat Mono|Lead|Stack|7,7,3|0.38,0.30,0.16|0.00,0.08,-12.00|0.52,0.48,0.62|0.46,0.50,0.42|0.50,0.48,0.50|78|20|36|Express|0.06|0.02|Fat three-oscillator analog mono lead
Nasal Wire|Lead|Prism|16,10|0.50,0.18|0.50,0.62|0.72,0.58|0.38,0.42|0.04,0.00|88|28|30|Talk|0.08|0.02|Nasal focused lead with wire edge
Sync Razor|Lead|Stack|7,8,6|0.42,0.24,0.10|0.00,12.00,0.00|0.68,0.62,0.54|0.44,0.38,0.58|0.48,0.52,0.46|108|24|26|Pulse|0.05|0.02|Hard sync-like razor lead
Honey Saw|Lead|Deluge|5|0.68|0.00|0.54|0.00|3.00|92|14|28|Express|0.10|0.04|Warm analog saw lead
Acid Voice|Lead|Prism|8,17|0.46,0.14|0.50,0.50|0.74,0.62|0.52,0.48|0.08,0.02|72|62|72|Acid|0.06|0.04|Resonant vocal sync lead
Tri Burner|Lead|Stack|10,4,0|0.46,0.22,0.00|0.00,12.00,0.00|0.70,0.54,0.50|0.56,0.48,0.50|0.62,0.44,0.50|102|18|34|Express|0.04|0.02|Folded triangle lead with FM heat
Digi Fang|Lead|Prism|37,22|0.44,0.16|0.50,0.57|0.68,0.60|0.74,0.52|0.18,0.08|112|12|20|Growl|0.04|0.02|Aggressive digital modulation lead
Pulse Talk|Lead|Deluge|3|0.70|0.00|0.30|0.00|-5.00|84|26|34|Talk|0.07|0.03|Expressive analog pulse talker
Morph Scream|Lead|Stack|11,12,5|0.40,0.28,0.10|0.00,0.03,12.00|0.72,0.60,0.52|0.62,0.46,0.58|0.70,0.64,0.48|110|22|28|Growl|0.03|0.02|Morphing aggressive solo voice
Deep Sub|Bass|Deluge|0|0.78|-12.00|0.50|0.00|0.00|54|8|18|Bass|0.01|0.00|Pure deep sine sub
Rubber Sub|Bass|Stack|3,0,0|0.62,0.20,0.00|-12.00,0.00,0.00|0.74,0.42,0.50|0.34,0.46,0.50|0.52,0.50,0.50|60|18|46|Bass|0.02|0.00|Rubbery sub with a folded upper edge
Night Reese|Bass|Stack|7,7,0|0.46,0.42,0.00|-12.00,-11.88,0.00|0.48,0.54,0.50|0.52,0.46,0.50|0.50,0.50,0.50|68|24|34|Growl|0.03|0.01|Slowly beating dark Reese bass
Acid Core|Bass|Prism|8,6|0.50,0.18|0.38,0.38|0.70,0.42|0.50,0.34|0.06,0.00|58|78|86|Acid|0.02|0.03|Tight resonant acid core
FM Thud|Bass|Prism|20,3|0.54,0.12|0.38,0.50|0.62,0.40|0.44,0.32|0.24,0.02|66|16|54|Bass|0.01|0.00|Punchy FM bass thud
Saw Floor|Bass|Deluge|5|0.72|-12.00|0.58|0.00|2.00|64|16|38|Bass|0.02|0.00|Solid analog saw floor
Growl Root|Bass|Stack|5,3,6|0.40,0.30,0.12|-12.00,-24.00,0.00|0.64,0.72,0.46|0.52,0.36,0.58|0.62,0.50,0.44|70|30|50|Growl|0.02|0.01|Feedback growl anchored by sub
Hollow Bass|Bass|Prism|16,12|0.48,0.20|0.38,0.38|0.54,0.34|0.28,0.40|0.04,0.00|62|28|42|Talk|0.02|0.01|Hollow formant bass
Square Weight|Bass|Deluge|3|0.76|-12.00|0.44|0.00|-2.00|56|20|32|Pulse|0.01|0.00|Heavy analog square bass
Folded Low|Bass|Stack|0,10,3|0.48,0.24,0.20|-12.00,-12.00,-24.00|0.66,0.58,0.70|0.38,0.44,0.32|0.72,0.60,0.50|64|22|44|Bass|0.02|0.00|Folded low bass with triangle bite
Dream Keys|Keys|Prism|3,23|0.48,0.15|0.50,0.69|0.42,0.62|0.38,0.68|0.02,0.08|92|10|28|Keys|0.18|0.08|Dreamy sine keys with bell dust
Organic Tines|Keys|Stack|12,0,3|0.44,0.20,0.12|0.00,12.00,-12.00|0.48,0.40,0.62|0.34,0.48,0.38|0.36,0.52,0.50|84|14|34|Keys|0.12|0.04|Organic trimorph electric tines
Digital EP|Keys|Prism|20,12|0.46,0.18|0.50,0.62|0.58,0.36|0.52,0.34|0.14,0.00|98|8|26|Keys|0.14|0.05|Controlled FM electric piano
Soft Organ|Keys|Deluge|1|0.70|0.00|0.50|0.00|0.00|108|4|8|Organ|0.10|0.03|Simple mellow triangle organ
Reed Keys|Keys|Prism|16,3|0.50,0.14|0.50,0.62|0.48,0.36|0.30,0.42|0.02,0.00|78|18|30|Keys|0.08|0.03|Breathy reed keyboard
Fold Clav|Keys|Stack|10,1,0|0.48,0.20,0.00|0.00,12.00,0.00|0.64,0.38,0.50|0.42,0.50,0.50|0.66,0.44,0.50|90|12|46|Pluck|0.06|0.02|Folded clavinet snap
Toy Keys|Keys|Prism|15,3|0.46,0.12|0.50,0.62|0.56,0.38|0.64,0.40|0.04,0.00|102|6|24|Keys|0.12|0.04|Playful toy keyboard
Warm Combo|Keys|Stack|7,8,3|0.36,0.24,0.18|0.00,12.00,-12.00|0.42,0.36,0.66|0.48,0.40,0.34|0.50,0.50,0.50|86|16|32|Organ|0.10|0.03|Warm compact combo keyboard
Silver Bell|Bell|Prism|23,12|0.44,0.14|0.50,0.74|0.68,0.42|0.74,0.46|0.12,0.02|116|8|18|Bell|0.28|0.10|Clear silver bell
Wood Mallet|Bell|Stack|1,3,0|0.46,0.22,0.00|0.00,-12.00,0.00|0.24,0.66,0.50|0.26,0.36,0.50|0.34,0.50,0.50|76|12|38|Mallet|0.10|0.03|Rounded wooden mallet
FM Chime|Bell|Prism|20,20|0.38,0.18|0.50,0.69|0.72,0.58|0.64,0.52|0.28,0.16|118|6|16|Bell|0.34|0.12|Bright two-ratio FM chime
Glass Marimba|Bell|Stack|0,6,3|0.42,0.18,0.12|0.00,12.00,-12.00|0.62,0.46,0.70|0.44,0.58,0.32|0.66,0.42,0.50|100|10|30|Mallet|0.16|0.05|Glassy marimba strike
Temple Bowl|Bell|Prism|13,23|0.42,0.20|0.50,0.62|0.48,0.64|0.58,0.70|0.04,0.10|90|26|22|Bell|0.30|0.12|Dark resonant temple bowl
Soft Glock|Bell|Prism|12,23|0.44,0.12|0.50,0.74|0.40,0.68|0.52,0.72|0.02,0.08|112|8|14|Bell|0.26|0.09|Soft compact glockenspiel
Polar Drone|Texture|Stack|9,11,10|0.32,0.28,0.20|-12.00,0.00,12.00|0.42,0.66,0.58|0.72,0.44,0.62|0.50,0.72,0.64|74|32|24|Evolve|0.34|0.12|Slow polar swarm drone
Dust Field|Texture|Prism|35,32|0.38,0.18|0.50,0.50|0.58,0.46|0.52,0.62|0.06,0.00|82|18|26|Dust|0.30|0.10|Cloud and filtered-noise dust field
Machine Fog|Texture|Deluge|3|0.60|-12.00|0.22|270.00|-7.00|66|34|36|Pulse|0.24|0.08|Slow industrial pulse fog
Deep Current|Texture|Stack|3,12,0|0.42,0.30,0.14|-24.00,-12.00,0.00|0.72,0.54,0.44|0.34,0.48,0.56|0.50,0.62,0.54|58|20|32|Evolve|0.28|0.08|Low evolving current drone
Broken Halo|Experimental|Prism|36,37|0.34,0.22|0.50,0.57|0.62,0.74|0.72,0.56|0.08,0.14|106|18|20|Dust|0.24|0.10|Granular halo with digital fractures
Elastic Metal|Experimental|Stack|6,4,10|0.34,0.26,0.18|0.00,7.00,-12.00|0.68,0.58,0.72|0.62,0.48,0.54|0.56,0.66,0.60|94|26|34|Growl|0.12|0.05|Playable elastic metallic resonance
Phase Garden|Experimental|Deluge|2|0.62|0.00|0.36|135.00|6.00|88|20|28|Pulse|0.18|0.07|Musical phase-reset square garden
Morphic Reed|Experimental|Stack|11,12,6|0.38,0.28,0.14|0.00,12.00,-12.00|0.58,0.46,0.54|0.42,0.60,0.66|0.72,0.68,0.44|82|24|38|Talk|0.14|0.06|Unusual but melodic morphing reed
'@

$specs = New-Object System.Collections.Generic.List[object]
$slot = 64
foreach ($line in ($rows -split "`r?`n")) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $p = $line.Split("|")
    if ($p.Count -ne 16) { throw "Malformed patch row: $line" }
    $category = $p[1]
    $specs.Add([pscustomobject]@{
        Slot = $slot; Name = $p[0]; Category = $category; Engine = $p[2]
        Models = $p[3]; Levels = $p[4]; Tunes = $p[5]; Timbres = $p[6]
        Colors = $p[7]; P3 = $p[8]
        Cutoff = [single]::Parse($p[9], [Globalization.CultureInfo]::InvariantCulture)
        Resonance = [single]::Parse($p[10], [Globalization.CultureInfo]::InvariantCulture)
        Eg = [single]::Parse($p[11], [Globalization.CultureInfo]::InvariantCulture)
        Mod = $p[12]
        Send1 = [single]::Parse($p[13], [Globalization.CultureInfo]::InvariantCulture)
        Send2 = [single]::Parse($p[14], [Globalization.CultureInfo]::InvariantCulture)
        Intention = $p[15]
    })
    $slot++
}
if (($specs.Count -ne 64) -or ($slot -ne 128)) {
    throw "The curated bank must contain exactly 64 patches in slots 64..127."
}

function Get-Envelope([string]$Category) {
    switch ($Category) {
        "Pad"          { return @(22,72,82,82, 34,58,104,84, 18,74,80,86) }
        "Pluck"        { return @(0,30,0,20,   0,28,0,18,    0,34,0,24) }
        "Lead"         { return @(0,38,82,34,  0,20,112,30,  2,44,72,34) }
        "Bass"         { return @(0,26,48,18,  0,18,108,16,  0,30,32,18) }
        "Keys"         { return @(3,46,44,36,  2,42,58,36,   2,48,40,38) }
        "Bell"         { return @(0,58,0,54,   0,66,0,58,    0,62,0,58) }
        "Texture"      { return @(54,84,88,112, 62,88,94,112, 42,90,78,116) }
        "Experimental" { return @(8,64,54,68,  6,62,72,70,  8,72,52,76) }
        default        { throw "Unknown category $Category" }
    }
}

function Set-ToneDefaults([byte[]]$Tone) {
    Set-F32 $Tone 0 0; Set-F32 $Tone 4 1; Set-F32 $Tone 8 0; Set-F32 $Tone 12 1
    Set-F32 $Tone 16 0; Set-F32 $Tone 20 0; Set-F32 $Tone 24 0; Set-F32 $Tone 28 0
    $clip = @(120,0,0,0,1,1,4,3,4)
    for ($i = 0; $i -lt $clip.Count; $i++) { Set-F32 $Tone (32 + 4*$i) $clip[$i] }
    Set-F32 $Tone 68 0
    $looper = @(0,0,0,0,0,0,4)
    for ($i = 0; $i -lt $looper.Count; $i++) { Set-F32 $Tone (72 + 4*$i) $looper[$i] }
    for ($i = 0; $i -lt 16; $i++) { Set-F32 $Tone (100 + 4*$i) 0 }
    for ($osc = 0; $osc -lt 2; $osc++) {
        Set-F32 $Tone (164 + 4*$osc) 0
        Set-F32 $Tone (172 + 4*$osc) 0.5
        Set-F32 $Tone (180 + 4*$osc) 0.5
        Set-F32 $Tone (188 + 4*$osc) 0
        Set-F32 $Tone (196 + 4*$osc) 0.5
        Set-F32 $Tone (204 + 4*$osc) 0.5
        Set-F32 $Tone (212 + 4*$osc) 0.5
        Set-F32 $Tone (220 + 4*$osc) 0
        Set-F32 $Tone (228 + 4*$osc) $(if ($osc -eq 0) { 1 } else { 0 })
    }
    for ($osc = 0; $osc -lt 3; $osc++) {
        Set-F32 $Tone (236 + 4*$osc) $(if ($osc -eq 0) { 1 } else { 0 })
        Set-F32 $Tone (248 + 4*$osc) 1
        Set-F32 $Tone (260 + 4*$osc) 0
        Set-F32 $Tone (272 + 4*$osc) 0.5
        Set-F32 $Tone (284 + 4*$osc) 0.5
        Set-F32 $Tone (296 + 4*$osc) 0.5
    }
    Set-F32 $Tone 308 0; Set-F32 $Tone 312 0; Set-F32 $Tone 316 0
    for ($osc = 0; $osc -lt 2; $osc++) {
        Set-F32 $Tone (320 + 4*$osc) 0; Set-F32 $Tone (328 + 4*$osc) 0
        Set-F32 $Tone (336 + 4*$osc) 0; Set-F32 $Tone (344 + 4*$osc) 1
        Set-F32 $Tone (352 + 4*$osc) $(if ($osc -eq 0) { 1 } else { 0 })
        Set-F32 $Tone (360 + 4*$osc) 0; Set-F32 $Tone (368 + 4*$osc) 0
        Set-F32 $Tone (376 + 4*$osc) 0
    }
    Set-F32 $Tone 384 0; Set-F32 $Tone 388 0; Set-F32 $Tone 392 2; Set-F32 $Tone 396 1
    Set-F32 $Tone 400 2; Set-F32 $Tone 404 1; Set-F32 $Tone 408 0
    Set-F32 $Tone 412 0; Set-F32 $Tone 416 0.5; Set-F32 $Tone 420 0; Set-F32 $Tone 424 0
    Set-F32 $Tone 428 0
    for ($i = 0; $i -lt 12; $i++) { Set-F32 $Tone (432 + 4*$i) 0 }
    $bd = @(0,0.4,0.3,0.1,1,0,0,0)
    for ($i = 0; $i -lt $bd.Count; $i++) { Set-F32 $Tone (480 + 4*$i) $bd[$i] }
}

function Get-EngineType([string]$Engine) {
    switch ($Engine) {
        "Prism" { return $TypePrism }
        "Stack" { return $TypeStack }
        "Deluge" { return $TypeDeluge }
        default { throw "Unsupported engine $Engine" }
    }
}
function Get-EngineDestinations([string]$Engine) {
    switch ($Engine) {
        "Prism" {
            return @($Param.PARAM_PRISM_COARSE, $Param.PARAM_PRISM_FM, $Param.PARAM_PRISM_TIMBRE,
                $Param.PARAM_PRISM_MODULATION, $Param.PARAM_PRISM_COLOR, $Param.PARAM_PRISM_LEVEL,
                $Param.PARAM_PRISM_OSC2_COARSE, $Param.PARAM_PRISM_OSC2_FM,
                $Param.PARAM_PRISM_OSC2_TIMBRE, $Param.PARAM_PRISM_OSC2_MODULATION,
                $Param.PARAM_PRISM_OSC2_COLOR, $Param.PARAM_PRISM_OSC2_LEVEL)
        }
        "Stack" {
            return @($Param.PARAM_STACK_OSC1_LEVEL, $Param.PARAM_STACK_OSC2_LEVEL,
                $Param.PARAM_STACK_OSC3_LEVEL, $Param.PARAM_STACK_NOISE_LEVEL,
                $Param.PARAM_STACK_OSC1_TUNE, $Param.PARAM_STACK_OSC1_TIMBRE,
                $Param.PARAM_STACK_OSC1_COLOR, $Param.PARAM_STACK_OSC1_PARAM3,
                $Param.PARAM_STACK_OSC2_TUNE, $Param.PARAM_STACK_OSC2_TIMBRE,
                $Param.PARAM_STACK_OSC2_COLOR, $Param.PARAM_STACK_OSC2_PARAM3,
                $Param.PARAM_STACK_OSC3_TUNE, $Param.PARAM_STACK_OSC3_TIMBRE,
                $Param.PARAM_STACK_OSC3_COLOR, $Param.PARAM_STACK_OSC3_PARAM3)
        }
        "Deluge" {
            return @($Param.PARAM_DELUGE_LEVEL, $Param.PARAM_DELUGE_TUNE,
                $Param.PARAM_DELUGE_FINE, $Param.PARAM_DELUGE_WIDTH)
        }
    }
}
function Get-PrimaryToneDests([string]$Engine) {
    switch ($Engine) {
        "Prism" { return @($Param.PARAM_PRISM_TIMBRE, $Param.PARAM_PRISM_COLOR, $Param.PARAM_PRISM_FM) }
        "Stack" { return @($Param.PARAM_STACK_OSC1_TIMBRE, $Param.PARAM_STACK_OSC1_COLOR, $Param.PARAM_STACK_OSC1_PARAM3) }
        "Deluge" { return @($Param.PARAM_DELUGE_WIDTH, $Param.PARAM_DELUGE_FINE, $Param.PARAM_DELUGE_TUNE) }
    }
}
function Get-Modulation([object]$Spec) {
    $toneDest = Get-PrimaryToneDests $Spec.Engine
    $routes = New-Object System.Collections.Generic.List[object]
    $lfo = @(@(0.08,0,0,0), @(0.05,1,0,90), @(0.12,0,0,180))
    $env3 = @(8,64,64,70)
    switch ($Spec.Mod) {
        "Drift"   { $lfo=@(@(0.07,0,0,0),@(0.031,1,0,90),@(0.11,5,0,180)); $env3=@(18,74,78,86); $routes.Add(@(1,$Param.PARAM_FILTER_CUTOFF,7)); $routes.Add(@(2,$toneDest[0],8)); $routes.Add(@(3,$Param.PARAM_MIX_PAN,6)); $routes.Add(@(6,$toneDest[1],9)) }
        "Evolve"  { $lfo=@(@(0.045,0,0,0),@(0.073,1,0,120),@(0.11,8,0,240)); $env3=@(28,88,72,104); $routes.Add(@(1,$toneDest[0],14)); $routes.Add(@(2,$Param.PARAM_FILTER_CUTOFF,10)); $routes.Add(@(3,$toneDest[1],11)); $routes.Add(@(6,$toneDest[2],8)); $routes.Add(@(1,$Param.PARAM_MIX_PAN,5)) }
        "Shimmer" { $lfo=@(@(0.14,0,0,0),@(0.06,1,0,90),@(0.21,5,0,180)); $env3=@(6,78,20,88); $routes.Add(@(1,$toneDest[0],12)); $routes.Add(@(2,$Param.PARAM_MIX_PAN,8)); $routes.Add(@(3,$toneDest[1],9)); $routes.Add(@(6,$Param.PARAM_FILTER_CUTOFF,15)) }
        "Pulse"   { $lfo=@(@(0.19,1,1,0),@(0.08,0,0,90),@(0.31,5,0,180)); $env3=@(4,52,36,54); $routes.Add(@(1,$toneDest[0],11)); $routes.Add(@(2,$Param.PARAM_FILTER_CUTOFF,8)); $routes.Add(@(3,$Param.PARAM_MIX_PAN,5)); $routes.Add(@(6,$toneDest[1],7)) }
        "Pluck"   { $lfo=@(@(0.18,0,1,0),@(0.09,1,0,90),@(0.33,5,1,180)); $env3=@(0,32,0,24); $routes.Add(@(6,$toneDest[0],24)); $routes.Add(@(4,$Param.PARAM_FILTER_CUTOFF,12)); $routes.Add(@(2,$Param.PARAM_MIX_PAN,4)); $routes.Add(@(3,$toneDest[1],6)) }
        "Talk"    { $lfo=@(@(0.24,1,1,0),@(0.07,0,0,90),@(0.16,5,0,180)); $env3=@(1,42,28,34); $routes.Add(@(6,$toneDest[0],18)); $routes.Add(@(1,$toneDest[1],10)); $routes.Add(@(2,$Param.PARAM_FILTER_CUTOFF,7)); $routes.Add(@(3,$Param.PARAM_MIX_PAN,5)) }
        "Express" { $lfo=@(@(5.2,0,1,0),@(0.09,1,0,90),@(0.17,5,0,180)); $env3=@(3,46,62,42); $routes.Add(@(1,$toneDest[2],5)); $routes.Add(@(2,$toneDest[0],8)); $routes.Add(@(3,$Param.PARAM_MIX_PAN,4)); $routes.Add(@(6,$Param.PARAM_FILTER_CUTOFF,10)) }
        "Acid"    { $lfo=@(@(0.27,1,1,0),@(0.13,0,0,90),@(0.41,5,1,180)); $env3=@(0,36,0,22); $routes.Add(@(4,$Param.PARAM_FILTER_CUTOFF,26)); $routes.Add(@(6,$toneDest[0],16)); $routes.Add(@(1,$Param.PARAM_FILTER_RESONANCE,8)); $routes.Add(@(2,$toneDest[1],7)) }
        "Growl"   { $lfo=@(@(0.16,1,1,0),@(0.053,0,0,90),@(0.29,8,0,180)); $env3=@(2,48,34,38); $routes.Add(@(6,$toneDest[0],22)); $routes.Add(@(1,$toneDest[1],14)); $routes.Add(@(2,$Param.PARAM_FILTER_CUTOFF,9)); $routes.Add(@(3,$toneDest[2],10)) }
        "Bass"    { $lfo=@(@(0.12,0,1,0),@(0.047,1,0,90),@(0.22,5,1,180)); $env3=@(0,30,26,20); $routes.Add(@(6,$toneDest[0],12)); $routes.Add(@(1,$Param.PARAM_FILTER_CUTOFF,6)); $routes.Add(@(2,$toneDest[1],5)) }
        "Keys"    { $lfo=@(@(4.8,0,1,0),@(0.08,1,0,90),@(0.19,5,1,180)); $env3=@(2,48,36,38); $routes.Add(@(1,$toneDest[2],3)); $routes.Add(@(2,$Param.PARAM_MIX_PAN,5)); $routes.Add(@(6,$toneDest[0],10)); $routes.Add(@(3,$Param.PARAM_FILTER_CUTOFF,5)) }
        "Organ"   { $lfo=@(@(5.6,0,0,0),@(0.12,1,0,90),@(0.07,5,0,180)); $env3=@(8,60,80,58); $routes.Add(@(1,$toneDest[2],4)); $routes.Add(@(2,$Param.PARAM_MIX_PAN,6)); $routes.Add(@(3,$toneDest[0],5)) }
        "Bell"    { $lfo=@(@(0.21,0,1,0),@(0.063,1,0,90),@(0.28,5,1,180)); $env3=@(0,68,0,64); $routes.Add(@(6,$toneDest[1],18)); $routes.Add(@(1,$toneDest[0],7)); $routes.Add(@(2,$Param.PARAM_MIX_PAN,5)); $routes.Add(@(3,$Param.PARAM_FILTER_CUTOFF,6)) }
        "Mallet"  { $lfo=@(@(0.17,0,1,0),@(0.071,1,0,90),@(0.24,5,1,180)); $env3=@(0,44,0,34); $routes.Add(@(6,$toneDest[0],16)); $routes.Add(@(4,$Param.PARAM_FILTER_CUTOFF,10)); $routes.Add(@(2,$toneDest[1],5)) }
        "Dust"    { $lfo=@(@(0.09,4,0,0),@(0.037,0,0,90),@(0.14,8,0,180)); $env3=@(12,76,50,88); $routes.Add(@(1,$toneDest[0],13)); $routes.Add(@(2,$Param.PARAM_FILTER_CUTOFF,11)); $routes.Add(@(3,$toneDest[1],12)); $routes.Add(@(6,$Param.PARAM_MIX_PAN,8)) }
        default   { throw "Unknown modulation style $($Spec.Mod)" }
    }
    return @{ Lfo=$lfo; Env3=$env3; Routes=$routes }
}

function New-Payload([object]$Spec) {
    $payload = New-Object byte[] $PayloadSize
    $type = Get-EngineType $Spec.Engine
    Set-Ascii $payload 0 32 $Spec.Name
    Set-U8 $payload 32 $FamilySynth; Set-U8 $payload 33 $type; Set-U8 $payload 34 0
    Set-U8 $payload 35 $Width; Set-U8 $payload 36 $FamilySynth; Set-U8 $payload 37 $type
    Set-U8 $payload 38 $Width; Set-U8 $payload 39 0

    $member = $MetadataSize
    Set-U8 $payload ($member + 0) 0; Set-U8 $payload ($member + 1) 0
    Set-U8 $payload ($member + 2) $FamilySynth; Set-U8 $payload ($member + 3) $type
    Set-F32 $payload ($member + 4) 0
    Set-U8 $payload ($member + 8) 0; Set-U8 $payload ($member + 9) 0
    $sound = $member + 12
    $tone = $member + 272

    $env = Get-Envelope $Spec.Category
    $soundDefaults = @(0.76,0,$Spec.Send1,$Spec.Send2,0,0,2,$Spec.Cutoff,$Spec.Resonance,$Spec.Eg,
        $env[0],$env[1],$env[2],$env[3],64,1,0,64,64,64,
        $env[4],$env[5],$env[6],$env[7],1,1,1)
    for ($i = 0; $i -lt $soundDefaults.Count; $i++) { Set-F32 $payload ($sound + 4*$i) $soundDefaults[$i] }
    $mod = Get-Modulation $Spec
    for ($l = 0; $l -lt 3; $l++) {
        for ($k = 0; $k -lt 4; $k++) { Set-F32 $payload ($sound + 108 + 16*$l + 4*$k) $mod.Lfo[$l][$k] }
    }
    Set-U8 $payload ($sound + 156) 1; Set-U8 $payload ($sound + 157) 2
    Set-U8 $payload ($sound + 158) 1; Set-U8 $payload ($sound + 159) 6
    Set-U8 $payload ($sound + 160) 1; Set-F32 $payload ($sound + 164) 0.08
    Set-U8 $payload ($sound + 168) 2; Set-F32 $payload ($sound + 172) 0.12
    for ($i = 0; $i -lt 4; $i++) { Set-F32 $payload ($sound + 176 + 4*$i) $mod.Env3[$i] }
    for ($i = 0; $i -lt 8; $i++) {
        Set-U8 $payload ($sound + 192 + 8*$i) 0
        Set-U8 $payload ($sound + 193 + 8*$i) $(if ($i -lt 3) { $i + 1 } elseif ($i -eq 3) { 6 } else { 0 })
        Set-U16 $payload ($sound + 194 + 8*$i) $Param.PARAM_COUNT
        Set-F32 $payload ($sound + 196 + 8*$i) 0
    }
    for ($i = 0; $i -lt $mod.Routes.Count; $i++) {
        $route = $mod.Routes[$i]
        Set-U8 $payload ($sound + 192 + 8*$i) 1
        Set-U8 $payload ($sound + 193 + 8*$i) $route[0]
        Set-U16 $payload ($sound + 194 + 8*$i) $route[1]
        Set-F32 $payload ($sound + 196 + 8*$i) $route[2]
    }
    Set-U8 $payload ($sound + 256) 0

    $toneBytes = New-Object byte[] $ToneSize
    Set-ToneDefaults $toneBytes
    $models = @(Parse-Floats $Spec.Models $(if ($Spec.Engine -eq "Stack") {3} elseif ($Spec.Engine -eq "Prism") {2} else {1}))
    $levels = @(Parse-Floats $Spec.Levels $models.Count)
    $tunes = @(Parse-Floats $Spec.Tunes $models.Count)
    $timbres = @(Parse-Floats $Spec.Timbres $models.Count)
    $colors = @(Parse-Floats $Spec.Colors $models.Count)
    $p3 = @(Parse-Floats $Spec.P3 $models.Count)
    if ($Spec.Engine -eq "Prism") {
        for ($i = 0; $i -lt 2; $i++) {
            Set-F32 $toneBytes (164 + 4*$i) $models[$i]
            Set-F32 $toneBytes (180 + 4*$i) $tunes[$i]
            Set-F32 $toneBytes (188 + 4*$i) $p3[$i]
            Set-F32 $toneBytes (196 + 4*$i) $timbres[$i]
            Set-F32 $toneBytes (212 + 4*$i) $colors[$i]
            Set-F32 $toneBytes (228 + 4*$i) $levels[$i]
        }
    } elseif ($Spec.Engine -eq "Stack") {
        for ($i = 0; $i -lt 3; $i++) {
            Set-F32 $toneBytes (236 + 4*$i) $levels[$i]
            Set-F32 $toneBytes (248 + 4*$i) $models[$i]
            Set-F32 $toneBytes (260 + 4*$i) $tunes[$i]
            Set-F32 $toneBytes (272 + 4*$i) $timbres[$i]
            Set-F32 $toneBytes (284 + 4*$i) $colors[$i]
            Set-F32 $toneBytes (296 + 4*$i) $p3[$i]
        }
        Set-F32 $toneBytes 312 0.06
        Set-F32 $toneBytes 316 1
    } else {
        Set-F32 $toneBytes 400 $models[0]; Set-F32 $toneBytes 404 $levels[0]
        Set-F32 $toneBytes 408 $tunes[0]; Set-F32 $toneBytes 412 $p3[0]
        Set-F32 $toneBytes 416 $timbres[0]; Set-F32 $toneBytes 420 $colors[0]
        Set-F32 $toneBytes 424 1
    }
    [Array]::Copy($toneBytes, 0, $payload, $tone, $ToneSize)
    return $payload
}

function New-Header([object]$Spec, [byte[]]$Payload) {
    $header = New-Object byte[] $HeaderSize
    $type = Get-EngineType $Spec.Engine
    Set-U32 $header 0 $PatchMagic; Set-U16 $header 4 $PatchVersion; Set-U16 $header 6 $HeaderSize
    Set-U32 $header 8 $PayloadSize
    Set-U8 $header 12 $FamilySynth; Set-U8 $header 13 $type; Set-U8 $header 14 0
    Set-U8 $header 15 $Width; Set-U8 $header 16 $FamilySynth; Set-U8 $header 17 $type
    Set-U8 $header 18 $Width; Set-U8 $header 19 0
    Set-Ascii $header 20 32 $Spec.Name
    Set-U32 $header 52 (Get-Checksum $Payload)
    return $header
}

function Validate-Patch([string]$Path, [object]$Spec) {
    $file = [IO.File]::ReadAllBytes($Path)
    if ($file.Length -ne ($HeaderSize + $PayloadSize)) { throw "$Path has invalid total size." }
    if ((Get-U32 $file 0) -ne $PatchMagic) { throw "$Path has invalid magic." }
    if ((Get-U16 $file 4) -ne $PatchVersion) { throw "$Path has invalid version." }
    if ((Get-U16 $file 6) -ne $HeaderSize) { throw "$Path has invalid header_size." }
    if ((Get-U32 $file 8) -ne $PayloadSize) { throw "$Path has invalid payload_size." }
    if (($file[15] -ne 1) -or ($file[18] -ne 1)) { throw "$Path is not P1." }
    if (($file[12] -ne $FamilySynth) -or ($file[13] -ne (Get-EngineType $Spec.Engine)) -or
        ($file[16] -ne $FamilySynth) -or ($file[17] -ne (Get-EngineType $Spec.Engine))) { throw "$Path header family/type invalid." }
    $payload = New-Object byte[] $PayloadSize
    [Array]::Copy($file, $HeaderSize, $payload, 0, $PayloadSize)
    if ((Get-Checksum $payload) -ne (Get-U32 $file 52)) { throw "$Path checksum mismatch." }
    if ((Get-Ascii $file 20 32) -ne $Spec.Name -or (Get-Ascii $payload 0 32) -ne $Spec.Name) { throw "$Path name mismatch." }
    $member = 40; $sound = $member + 12; $tone = $member + 272
    if (($payload[32] -ne $FamilySynth) -or ($payload[33] -ne (Get-EngineType $Spec.Engine)) -or
        ($payload[35] -ne 1) -or ($payload[36] -ne $FamilySynth) -or
        ($payload[37] -ne (Get-EngineType $Spec.Engine)) -or ($payload[38] -ne 1) -or ($payload[$member] -ne 0) -or
        ($payload[$member+1] -ne 0) -or ($payload[$member+2] -ne $FamilySynth) -or
        ($payload[$member+3] -ne (Get-EngineType $Spec.Engine))) { throw "$Path payload P1/family/type invalid." }
    $soundRanges = @(
        @(0,0,2),@(4,-1,1),@(8,0,1),@(12,0,1),@(16,0,1),@(20,0,1),
        @(24,0,4),@(28,0,127),@(32,0,127),@(36,0,127),
        @(40,0,127),@(44,0,127),@(48,0,127),@(52,0,127),@(56,0,127),
        @(60,0,1),@(64,0,127),@(68,0,127),@(72,0,127),@(76,0,127),
        @(80,0,127),@(84,0,127),@(88,0,127),@(92,0,127),
        @(96,0,1),@(100,0,1),@(104,0,1)
    )
    foreach ($r in $soundRanges) { Assert-Range (Get-F32 $payload ($sound+$r[0])) $r[1] $r[2] "$Path sound+$($r[0])" }
    for ($l = 0; $l -lt 3; $l++) {
        Assert-Range (Get-F32 $payload ($sound+108+16*$l)) -80 16 "$Path LFO rate"
        Assert-Range (Get-F32 $payload ($sound+112+16*$l)) 0 8 "$Path LFO shape"
        Assert-Range (Get-F32 $payload ($sound+116+16*$l)) 0 3 "$Path LFO trig"
        Assert-Range (Get-F32 $payload ($sound+120+16*$l)) 0 360 "$Path LFO phase"
    }
    foreach ($offset in @(156,157,158,159,160,168)) {
        if ($payload[$sound+$offset] -gt 10) { throw "$Path modulation operator source invalid." }
    }
    Assert-Range (Get-F32 $payload ($sound+164)) 0 1 "$Path SLEW1 amount"
    Assert-Range (Get-F32 $payload ($sound+172)) 0 1 "$Path SLEW2 amount"
    for ($i = 0; $i -lt 4; $i++) { Assert-Range (Get-F32 $payload ($sound+176+4*$i)) 0 127 "$Path ENV3" }
    if ($payload[$sound+256] -gt 7) { throw "$Path selected Matrix slot invalid." }
    $commonDest = @($Param.PARAM_MIX_LEVEL,$Param.PARAM_MIX_PAN,$Param.PARAM_MIX_SEND1,$Param.PARAM_MIX_SEND2,
        $Param.PARAM_FILTER_CUTOFF,$Param.PARAM_FILTER_RESONANCE,$Param.PARAM_FILTER_EG_AMT,
        $Param.PARAM_FILTER_ATTACK,$Param.PARAM_FILTER_DECAY,$Param.PARAM_FILTER_SUSTAIN,$Param.PARAM_FILTER_RELEASE,
        $Param.PARAM_FILTER_EQ_LOW,$Param.PARAM_FILTER_EQ_MID,$Param.PARAM_FILTER_EQ_HIGH,
        $Param.PARAM_VCA_ATTACK,$Param.PARAM_VCA_DECAY,$Param.PARAM_VCA_SUSTAIN,$Param.PARAM_VCA_RELEASE,
        $Param.PARAM_LFO1_RATE,$Param.PARAM_LFO2_RATE,$Param.PARAM_LFO3_RATE)
    $engineDest = Get-EngineDestinations $Spec.Engine
    for ($i = 0; $i -lt 8; $i++) {
        $enabled = $payload[$sound+192+8*$i]
        $source = $payload[$sound+193+8*$i]
        $dest = Get-U16 $payload ($sound+194+8*$i)
        $depth = Get-F32 $payload ($sound+196+8*$i)
        Assert-Range $depth -127 127 "$Path matrix depth"
        if ($enabled -gt 1) { throw "$Path matrix enabled flag invalid." }
        if ($enabled -ne 0) {
            if (($source -lt 1) -or ($source -gt 10)) { throw "$Path matrix source invalid." }
            if (($commonDest -notcontains $dest) -and ($engineDest -notcontains $dest)) { throw "$Path matrix destination $dest incompatible with $($Spec.Engine)." }
        } elseif (($source -gt 10) -or ($dest -ne $Param.PARAM_COUNT) -or ($depth -ne 0)) {
            throw "$Path disabled matrix slot is not canonical."
        }
    }
    $toneRanges = @(
        @(0,0,63),@(4,0,2),@(8,0,1),@(12,0,1),@(16,0,3),@(20,-24,24),@(24,0,6),@(28,0,1),
        @(32,40,300),@(36,0,4),@(40,-12,12),@(44,0,1),@(48,0,1),@(52,0,2),@(56,0,5),@(60,0,5),@(64,0,4),
        @(68,0,1),@(72,0,2),@(76,0,5),@(80,0,1),@(84,0,1),@(88,0,2),@(92,-12,12),@(96,0,5)
    )
    foreach ($r in $toneRanges) { Assert-Range (Get-F32 $payload ($tone+$r[0])) $r[1] $r[2] "$Path tone+$($r[0])" }
    for ($i=0;$i-lt4;$i++) {
        Assert-Range (Get-F32 $payload ($tone+100+16*$i)) 0 10 "$Path master-fx type"
        foreach ($delta in @(4,8,12)) { Assert-Range (Get-F32 $payload ($tone+100+16*$i+$delta)) 0 127 "$Path master-fx field" }
    }
    for ($i=0;$i-lt2;$i++) {
        Assert-Range (Get-F32 $payload ($tone+164+4*$i)) 0 38 "$Path Prism model"
        foreach ($base in @(172,180,188,196,204,212,220,228)) { Assert-Range (Get-F32 $payload ($tone+$base+4*$i)) 0 1 "$Path Prism field" }
    }
    for ($i=0;$i-lt3;$i++) {
        Assert-Range (Get-F32 $payload ($tone+236+4*$i)) 0 1 "$Path Stack level"
        Assert-Range (Get-F32 $payload ($tone+248+4*$i)) 0 12 "$Path Stack model"
        Assert-Range (Get-F32 $payload ($tone+260+4*$i)) -24 24 "$Path Stack tune"
        foreach ($base in @(272,284,296)) { Assert-Range (Get-F32 $payload ($tone+$base+4*$i)) 0 1 "$Path Stack field" }
    }
    foreach ($offset in @(308,312,316)) { Assert-Range (Get-F32 $payload ($tone+$offset)) 0 1 "$Path Stack global" }
    for ($i=0;$i-lt2;$i++) {
        Assert-Range (Get-F32 $payload ($tone+320+4*$i)) 0 63 "$Path Wave table"
        foreach ($base in @(328,336,344,352)) { Assert-Range (Get-F32 $payload ($tone+$base+4*$i)) 0 1 "$Path Wave inactive field" }
        Assert-Range (Get-F32 $payload ($tone+360+4*$i)) -60 60 "$Path Wave tune"
        foreach ($base in @(368,376)) { Assert-Range (Get-F32 $payload ($tone+$base+4*$i)) 0 3 "$Path Wave enum" }
    }
    Assert-Range (Get-F32 $payload ($tone+384)) 0 1 "$Path Wave frame interpolation"
    Assert-Range (Get-F32 $payload ($tone+388)) 0 1 "$Path Wave sample interpolation"
    Assert-Range (Get-F32 $payload ($tone+392)) 0 3 "$Path Wave position update"
    Assert-Range (Get-F32 $payload ($tone+396)) 0 1 "$Path Wave smoothing"
    Assert-Range (Get-F32 $payload ($tone+400)) 0 5 "$Path Deluge model"
    Assert-Range (Get-F32 $payload ($tone+404)) 0 1 "$Path Deluge level"
    Assert-Range (Get-F32 $payload ($tone+408)) -48 48 "$Path Deluge tune"
    Assert-Range (Get-F32 $payload ($tone+412)) -100 100 "$Path Deluge fine"
    Assert-Range (Get-F32 $payload ($tone+416)) 0 1 "$Path Deluge width"
    Assert-Range (Get-F32 $payload ($tone+420)) 0 360 "$Path Deluge phase"
    Assert-Range (Get-F32 $payload ($tone+424)) 0 1 "$Path Deluge retrig"
    Assert-Range (Get-F32 $payload ($tone+428)) 0 128 "$Path MIDI program"
    for ($i=0;$i-lt12;$i++) { Assert-Range (Get-F32 $payload ($tone+432+4*$i)) 0 127 "$Path MIDI CC" }
    $bdRanges=@(@(-48,24),@(0.01,2),@(0,1),@(0.01,1),@(0,2),@(0,1),@(0,1),@(0,1))
    for ($i=0;$i-lt8;$i++) { Assert-Range (Get-F32 $payload ($tone+480+4*$i)) $bdRanges[$i][0] $bdRanges[$i][1] "$Path inactive drum field" }
    for ($i=0;$i-lt166;$i++) {
        if ($payload[$member+784+$i] -ne 0) { throw "$Path carries a non-portable asset reference." }
    }
    for ($m=1;$m-lt4;$m++) {
        $start = $MetadataSize + $MemberSize*$m
        for ($i=0;$i-lt$MemberSize;$i++) {
            if ($payload[$start+$i] -ne 0) { throw "$Path contains non-zero inactive Poly member $m." }
        }
    }
}

$abiObject = Join-Path $env:TEMP "brick6_patch_abi_probe.o"
$armGcc = (Get-Command arm-none-eabi-gcc -ErrorAction Stop).Source
$includeArgs = @("-I$RepoRoot\Inc", "-I$RepoRoot\App\Middlewares\Third_Party\FatFs\src",
    "-I$RepoRoot\Board\LowCost\Generated\Inc", "-I$RepoRoot\Drivers\STM32H7xx_HAL_Driver\Inc",
    "-I$RepoRoot\Drivers\CMSIS\Device\ST\STM32H7xx\Include", "-I$RepoRoot\Drivers\CMSIS\Include")
foreach ($dir in Get-ChildItem (Join-Path $RepoRoot "Inc") -Directory) { $includeArgs += "-I$($dir.FullName)" }
& $armGcc -std=c11 -mcpu=cortex-m7 -mthumb -DSTM32H743xx -DBRICK6_VARIANT_LOWCOST @includeArgs `
    -c (Join-Path $RepoRoot "tools\patch_bank\abi_probe.c") -o $abiObject
if ($LASTEXITCODE -ne 0) { throw "ARM ABI probe failed." }
Remove-Item -LiteralPath $abiObject -Force

if (Test-Path $OutputRoot) { Remove-Item -LiteralPath $OutputRoot -Recurse -Force }
New-Item -ItemType Directory -Path $PatchDir -Force | Out-Null
$manifest = New-Object System.Collections.Generic.List[object]
foreach ($spec in $specs) {
    $payload = New-Payload $spec
    $header = New-Header $spec $payload
    $path = Join-Path $PatchDir ("P{0:D4}.B6P" -f $spec.Slot)
    $stream = [IO.File]::Open($path, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $stream.Write($header, 0, $header.Length)
        $stream.Write($payload, 0, $payload.Length)
    } finally {
        $stream.Dispose()
    }
    Validate-Patch $path $spec
    $manifest.Add($spec)
}

$categoryOrder = @("Pad","Pluck","Lead","Bass","Keys","Bell","Texture","Experimental")
$readme = New-Object Text.StringBuilder
[void]$readme.AppendLine("# BRICK6 Musical Patch Bank")
[void]$readme.AppendLine()
[void]$readme.AppendLine("64 mono-track P1 patches for slots 064-127. Copy the `BRICK` directory to the SD-card root.")
[void]$readme.AppendLine()
[void]$readme.AppendLine("The filename carries the slot only; B6P payloads contain no slot number, so files may be renamed to another valid `Pxxxx.B6P` slot.")
[void]$readme.AppendLine()
[void]$readme.AppendLine("Wave is intentionally excluded: the current Patch payload stores Wave table indices but `patch_v1_capture_sampler_asset()` captures portable asset paths only for Sampler RAM/STREAM/MULTI. A Wave patch would therefore depend on an arbitrary preloaded wavetable slot.")
[void]$readme.AppendLine()
[void]$readme.AppendLine("## Patch list")
[void]$readme.AppendLine()
[void]$readme.AppendLine("| Slot | Patch | Engine | Category | Intention |")
[void]$readme.AppendLine("|---:|---|---|---|---|")
foreach ($spec in $manifest) {
    [void]$readme.AppendLine("| $($spec.Slot) | $($spec.Name) | $($spec.Engine) | $($spec.Category) | $($spec.Intention) |")
}
[void]$readme.AppendLine()
[void]$readme.AppendLine("## Validation")
[void]$readme.AppendLine()
[void]$readme.AppendLine("Generated and validated against the current branch headers: B6PT magic, version 1, 56-byte packed header, 3848-byte PatchSaveV1 payload, DJB2-XOR checksum, P1 width, Synth family and engine type, finite/ranged persisted floats, and engine-compatible Matrix destinations. The ARM Cortex-M7 compiler also checks every persistent size and offset used by the serializer.")
[void]$readme.AppendLine()
[void]$readme.AppendLine("Regenerate from the repository root with:")
[void]$readme.AppendLine()
[void]$readme.AppendLine('```powershell')
[void]$readme.AppendLine('powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\patch_bank\generate_musical_patch_bank.ps1')
[void]$readme.AppendLine('```')
[IO.File]::WriteAllText((Join-Path $OutputRoot "README.md"), $readme.ToString(), [Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $RepoRoot "tools\patch_bank\generate_musical_patch_bank.ps1") -Destination (Join-Path $OutputRoot "generate_musical_patch_bank.ps1")
Copy-Item -LiteralPath (Join-Path $RepoRoot "tools\patch_bank\abi_probe.c") -Destination (Join-Path $OutputRoot "abi_probe.c")

if (Test-Path $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
Compress-Archive -Path (Join-Path $OutputRoot "*") -DestinationPath $ZipPath -CompressionLevel Optimal

$engineCounts = $manifest | Group-Object Engine | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Count)" }
$categoryCounts = foreach ($category in $categoryOrder) {
    $count = @($manifest | Where-Object Category -eq $category).Count
    "$category=$count"
}
Write-Output "PATCHES=64"
Write-Output ("ENGINES=" + ($engineCounts -join ","))
Write-Output ("CATEGORIES=" + ($categoryCounts -join ","))
Write-Output "SLOTS=64-127"
Write-Output "VALIDATION=64/64 + ARM_ABI_OK"
Write-Output "ZIP=$ZipPath"
