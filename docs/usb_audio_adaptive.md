# USB Audio adaptive playback

The full-speed UAC2 function exposes one stereo OUT interface at 48 kHz,
with 32-bit subslots and 24 valid PCM bits. Its isochronous data endpoint
has Adaptive/Data attributes, bInterval 1 (one USB frame), and a maximum
packet of 392 bytes (49 stereo frames). It has no feedback endpoint or
feedback calculation. The clock source advertises fixed 48 kHz.
The UAC2 standard isochronous endpoint descriptor is seven bytes and has no
`bSynchAddress` field; its alternate setting declares exactly one endpoint.

The USB IRQ moves each OUT packet from TinyUSB's FIFO to the 288-frame
PC-to-BRICK ring. AUDIO starts consumption only at an accepted 64-frame
SAI RX DMA boundary once at least 144 frames have accumulated. This is
PREFILL to RUN; music transport does not affect it.

In RUN, AUDIO normally consumes one source stereo frame per output frame.
At or below 72 queued frames, it may consume one source frame less and
repeat the last frame of that segment. At or above 216 frames, it may
consume one source frame extra and discard the last frame of that segment.
Both channels are adjusted together. A correction is allowed only after
at least 5000 output frames since the previous correction (about 104 ms
at 48 kHz), and only on a segment of at least two frames. The maximum
adaptation is 200 ppm. Normal operation between the thresholds remains
strictly 1:1. On insufficient data, AUDIO returns to PREFILL.
