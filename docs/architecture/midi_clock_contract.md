# MIDI Clock output contract

The sequencer's Q16 sample period remains the musical tempo authority. One
sixteenth-note step contains six MIDI clocks. TIM3 is a 1 MHz execution timer,
not a second tempo source: START and CONTINUE convert the next absolute sample
deadline to TIM3 time, and each compare advances that deadline by the Q16
period. Conversion retains the remainder of the exact 125/6 timer ticks per
48 kHz sample. At 120 BPM, successive ideal intervals are 20,833, 20,833 and
20,834 timer ticks. TIM3 priority 0 may briefly preempt priority-1 audio.

TIM3's IRQ only acknowledges compare, queues at most one timestamped clock
event with a target USB SOF, advances the absolute deadline, and programs the next compare. Its
16-bit counter uses intermediate wakeups when a deadline is more than 60 ms
away. If multiple periods have passed, they are counted and skipped; no burst
is generated. A pending tempo change takes effect after the next emitted clock,
without resetting phase. The IRQ does not call TinyUSB or inspect DSP state.

START queues `FA` before arming the timer; CONTINUE queues `FB` first. STOP
disarms the timer and discards unpublished clock events before emitting `FC`.
The former audio-boundary clock producer is removed. The USB SOF IRQ consumes
at most the newest pending timer event; it discards older events rather than
emitting a catch-up burst. Transport messages may not
bypass already queued MIDI packets. External MIDI clock mode does not echo
received clocks.

The USB IRQ releases at most one queued F8 on its selected SOF and immediately
services MIDI TX. The target is the first SOF at or after the absolute TIM3
deadline; the fractional musical period is never reset to the USB grid. At
120 BPM, 24 target intervals span 500 USB frames when each target is met.
USB MIDI remains bulk, so the host controls the actual IN transaction and can
delay it beyond the selected frame. START/CONTINUE and STOP keep their causal
order through the existing MIDI TX queue.

The timer removes the 64-frame generation quantization. USB MIDI still uses the
128-packet application TX queue, TinyUSB's FIFO, and a 64-byte Full Speed endpoint.
`g_midi_clock_prof` exposes IRQ latency, IRQ cycles, missed ticks, queue drops,
and consumer delay in the Release/LTO ELF. Run
`tools/test_midi_clock_contract.ps1` for the deterministic source contract.
