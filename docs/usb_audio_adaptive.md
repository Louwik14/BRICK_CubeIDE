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

For the 1:1 test, RUN always consumes exactly one source stereo frame per
output frame. No frame is duplicated or discarded, regardless of ring fill.
The ring may therefore drift with the difference between USB and SAI clocks.
On insufficient data, AUDIO returns to PREFILL.
