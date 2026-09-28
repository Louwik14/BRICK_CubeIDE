# UAC2 feedback (48 kHz, full speed)

The OUT endpoint's explicit feedback is a 16.16 number of stereo frames per
USB millisecond. TinyUSB sends its current value on feedback endpoint transfer
completion; `tud_audio_n_fb_set` only replaces that value. At interface
activation, the first queued feedback is nominal `48 << 16`. No application
code arms the endpoint a second time.

The SAI RX DMA half/full callbacks count 64 physical frames each. The USB SOF
handler samples that count and the PC-to-BRICK ring fill once per millisecond.
Every 256 SOFs, the physical rate estimate is `delta_dma_frames / 256` in
frames/ms, filtered with `rate += (measured - rate) / 16`. The feedback is
`rate + (144 - average_fill) / 4096` frames/ms, clamped to 48 ± 0.25.
The average fill is the mean of the 256 SOF samples. The estimate starts at
48 frames/ms and first changes after 256 ms. The rate filter has an approximate
4.1 s time constant; the fill correction can move at most 0.0352 frames/ms
for a ring fill from 0 to 288 frames. A slightly empty ring raises the rate,
and a slightly full ring lowers it. The fill correction is deliberately slow
relative to one USB packet or one DMA block.

Before this change, each superloop pass read the instantaneous fill. A 64-frame
DMA read alone stepped feedback by 64/256 = 0.25 frames/ms (about 5210 ppm),
even with exactly stable SAI. The new 256-ms DMA measurement can have a
one-block boundary error of at most 64/256 = 0.25 frames/ms; its 1/16 filter
limits a single such update to 0.015625 frames/ms. The 256-SOF fill average
removes the dependence on the instant of a single DMA read. Long-term physical
rate comes from SAI DMA, while fill only compensates slow clock drift.
