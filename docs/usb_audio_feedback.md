# UAC2 feedback (48 kHz, full speed)

The OUT endpoint's explicit feedback is a 16.16 number of stereo frames per
USB millisecond. TinyUSB sends its current value on feedback endpoint transfer
completion; `tud_audio_n_fb_set` only replaces that value. At interface
activation, the first queued feedback is nominal `48 << 16`. No application
code arms the endpoint a second time.

SAI1 runs from PLL3P: 12 MHz HSE / 5 * 256 / 25 = 24.576 MHz. SAI1 block A
divides this to the 48 kHz frame clock; synchronized block B drives circular
RX DMA. TIM5 runs from the PLL1/APB timer domain and is not an audio-clock
counter. The USB SOF IRQ samples the RX DMA NDTR position (256 32-bit words
per circular buffer, two words per stereo frame) and the USB frame number.
Consecutive SOFs yield a modulo-256-word DMA advance; a skipped SOF discards
the measurement window to avoid wrap ambiguity. After 256 consecutive SOF
intervals, `measured = sum_dma_words / (2 * 256)` frames/ms. The rate estimate
uses `rate += (measured - rate) / 16`. The feedback is
`rate + (144 - average_fill) / 4096` frames/ms, clamped to 48 ± 0.25.
The average fill is the mean of the 256 SOF samples. The estimate starts at
48 frames/ms and first changes after 256 ms. The rate filter has an approximate
4.1 s time constant; the fill correction can move at most 0.0352 frames/ms
for a ring fill from 0 to 288 frames. A slightly empty ring raises the rate,
and a slightly full ring lowers it. The fill correction is deliberately slow
relative to one USB packet or one DMA block.

The former 64-frame callback count resolved only 64/256 = 0.25 frame/ms
(5210 ppm) before filtering. NDTR resolves one DMA word, nominally half a
stereo frame: 0.5/256 = 0.001953125 frame/ms (40.7 ppm). DMA FIFO bursts may
make the observed position coarser, but this remains far finer than a 64-frame
callback. The 256-SOF fill average removes dependence on a single DMA read.
Long-term physical rate comes from SAI RX DMA progress; fill compensates slow
clock drift only.

## IRQ context audit

Calculation and `tud_audio_n_fb_set()` publication already run in the USB OTG
FS IRQ. `usb_device_irq()` detects an enabled SOF, calls TinyUSB's interrupt
handler, then calls `usb_audio_feedback_sof()` with the USB frame number.
The superloop does not calculate or publish OUT feedback. Interface activation installs the nominal
first value from TinyUSB's SET_INTERFACE callback in USB context. TinyUSB
queues that first packet after the callback and later packets on feedback
endpoint transfer completion. The descriptor has bInterval=1 (1 ms), though
the host controls actual IN transactions. The estimate changes every 256 SOFs.

USB has NVIC priority 1 and SAI RX DMA priority 2. NDTR is hardware state,
so USB preemption of the DMA callback cannot shift its measured progress.
The 32-bit NDTR and ring cursor loads are atomic on Cortex-M7. The aligned 32-bit TinyUSB
feedback value is published and read in the same USB IRQ. No endpoint arm,
allocation, or lock is added. Each SOF in RUN reads ring fill and adds
integers; division and publication run once per 256 SOFs. A feedback
completion callback would tie estimation to host IN scheduling; a SAI DMA
callback lacks the USB timebase.

## OUT prefill and audio boundary

An opened OUT interface starts in PREFILL. USB writes packets to the
288-frame ring while AUDIO renders silence for the USB input. At the start
of an accepted SAI RX DMA half (64 frames), AUDIO checks whether the ring
contains at least 144 frames. Only that boundary changes PREFILL to RUN.
Later segments and halves consume normally, independent of music transport
STOP/PLAY. Underflow returns to PREFILL; closing the USB interface resets
the ring as before. A 144-frame target leaves equal headroom on either side.
The feedback fill correction is disabled in PREFILL and averages only SOF
samples observed in RUN, including a partial first 256-SOF window. The
physical-rate estimator continues across both states.
