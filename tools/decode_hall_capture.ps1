param(
    [Parameter(Mandatory=$true)][string]$Capture,
    [Parameter(Mandatory=$true)][string]$Calibration
)

$mapA = @(1,3,5,0,2,7,4,6)
$mapB = @(13,10,8,15,9,14,11,12)
$mapC = @(23,20,17,22,16,21,18,19)
$cal = [System.IO.BinaryReader]::new([System.IO.File]::OpenRead($Calibration))
try {
    if ($cal.BaseStream.Length -ne 192) { throw 'Calibration dump must be 192 bytes' }
    $thresholds = @()
    for ($i = 0; $i -lt 24; $i++) {
        $thresholds += [pscustomobject]@{
            Minimum = $cal.ReadUInt16(); Maximum = $cal.ReadUInt16()
            Press = $cal.ReadUInt16(); Release = $cal.ReadUInt16()
        }
    }
} finally { $cal.Dispose() }

$reader = [System.IO.BinaryReader]::new([System.IO.File]::OpenRead($Capture))
try {
    if ($reader.BaseStream.Length -ne 229376) { throw 'Capture dump must be 229376 bytes' }
    $rows = for ($i = 0; $i -lt 4096; $i++) {
        $seq = $reader.ReadUInt32()
        $tick = $reader.ReadUInt32()
        $tim5 = $reader.ReadUInt32()
        $heldBefore = $reader.ReadUInt32()
        $heldAfter = $reader.ReadUInt32()
        $generation = $reader.ReadUInt32()
        $rawA = $reader.ReadUInt16()
        $rawB = $reader.ReadUInt16()
        $rawC = $reader.ReadUInt16()
        $volume = $reader.ReadUInt16()
        $countA = $reader.ReadUInt16()
        $countB = $reader.ReadUInt16()
        $mux = $reader.ReadByte()
        $odr = $reader.ReadByte()
        $idr = $reader.ReadByte()
        $completed = $reader.ReadByte()
        $callbackA = $reader.ReadUInt32()
        $callbackB = $reader.ReadUInt32()
        $errorA = $reader.ReadUInt16()
        $errorB = $reader.ReadUInt16()
        $ndtrBefore = $reader.ReadUInt16()
        $ndtrAfter = $reader.ReadUInt16()
        if ($seq -eq 0 -or $mux -ge 8) { continue }
        [pscustomobject]@{
            sequence = $seq; tick_ms = $tick; tim5_tick = $tim5
            held_before = ('0x{0:x6}' -f $heldBefore)
            held_after = ('0x{0:x6}' -f $heldAfter)
            calibration_generation = $generation
            mux = $mux; mux_odr = $odr; mux_idr = $idr
            key_a = $mapA[$mux]; raw_a = $rawA
            press_a = $thresholds[$mapA[$mux]].Press
            release_a = $thresholds[$mapA[$mux]].Release
            key_b = $mapB[$mux]; raw_b = $rawB
            press_b = $thresholds[$mapB[$mux]].Press
            release_b = $thresholds[$mapB[$mux]].Release
            key_c = $mapC[$mux]; raw_c = $rawC
            press_c = $thresholds[$mapC[$mux]].Press
            release_c = $thresholds[$mapC[$mux]].Release
            volume = $volume; adc1_callbacks = $countA
            adc2_callbacks = $countB; completing_adc = $completed
            adc1_callback_tick = $callbackA; adc2_callback_tick = $callbackB
            adc1_error = $errorA; adc2_error = $errorB
            adc1_dma_ndtr_before = $ndtrBefore
            adc1_dma_ndtr_after = $ndtrAfter
        }
    }
    $rows | Sort-Object sequence | ConvertTo-Csv -NoTypeInformation
} finally { $reader.Dispose() }
