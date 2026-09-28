# USB Audio adaptive duplex

The full-speed UAC2 function exposes stereo OUT and IN interfaces at 48 kHz,
with signed little-endian PCM32 (four-byte subslots, 32 valid bits). Its OUT isochronous data endpoint
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

The IN endpoint is isochronous Asynchronous/Data, interval 1, with no
separate feedforward endpoint. Both streams use clock source 0x10, which
reports CUR 48000 Hz, a single fixed RANGE, and VALID true. The OUT and IN
alternate settings can be controlled independently. There is no OUT feedback.

The IN tap is the stereo main monitor after the master output gain and
metronome mix, immediately before the physical codec pack. The SAI-paced
audio processing writes it to a separate 288-frame float ring; USB cannot
stall audio. USB starts reading at 144 frames. The target is 144 frames,
with low/high thresholds 72/216. Each 48-frame IN packet normally consumes
48 source frames. At low fill it consumes 47 and repeats the final stereo
frame; at high fill it consumes 49 and discards the last source frame. Only
one adjustment is allowed per at least 5000 output frames (about 200 ppm).
If the ring lacks a whole source block, IN sends 48 stereo silence frames
and returns to PREFILL. If USB stops consuming, the ring rejects new frames
once full; the audio engine continues unchanged. Alt-setting close and USB
reset clear only the affected stream state.

TinyUSB schedules an initial zero-length transfer when IN alt 1 opens. Its
TX completion path takes up to the 392-byte descriptor maximum from the
software FIFO and arms the endpoint, then calls `tud_audio_tx_done_isr`.
That callback queues the next nominal 384-byte packet. The FIFO is 768
bytes; packet length is fixed at 384 bytes while 392 is the endpoint maximum.
Each stereo frame is eight bytes. Nominal audio payload is 768 bytes/ms for
both directions; even two maximum audio packets plus two 64-byte MIDI
packets total 912 payload bytes/ms, below the Full-Speed 90% periodic
allocation ceiling after normal packet overhead. The DWC2 FIFO reservation
is 346 words out of 1024.
