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

## IRQ context audit

Calculation and `tud_audio_n_fb_set()` publication already run in the USB OTG
FS IRQ. `usb_device_irq()` detects an enabled SOF, calls TinyUSB's interrupt
handler, then calls `usb_audio_feedback_sof()`. The superloop's
`usb_audio_transport_process()` services audio IN data only; it does not
calculate or publish OUT feedback. Interface activation installs the nominal
first value from TinyUSB's SET_INTERFACE callback in USB context. TinyUSB
queues that first packet after the callback and later packets on feedback
endpoint transfer completion. The descriptor has bInterval=1 (1 ms), though
the host controls actual IN transactions. The estimate changes every 256 SOFs.

USB has NVIC priority 1 and SAI RX DMA priority 2. USB may preempt a DMA
callback, so the SOF count may see a completed 64-frame block one SOF late;
the window and filter suppress this boundary effect. The 32-bit DMA count
and ring cursor loads are atomic on Cortex-M7. The aligned 32-bit TinyUSB
feedback value is published and read in the same USB IRQ. No endpoint arm,
allocation, or lock is added. Each ordinary SOF reads ring fill and adds
integers; division and publication run once per 256 SOFs. A feedback
completion callback would tie estimation to host IN scheduling; a SAI DMA
callback lacks the USB timebase.
