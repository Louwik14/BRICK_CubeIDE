# MIDI Clock output contract

The sequencer's Q16 sample period remains the musical tempo authority. One
sixteenth-note step contains six MIDI clocks. TIM3 is a 1 MHz execution timer,
not a second tempo source: START and CONTINUE convert the next absolute sample
deadline to TIM3 time, and each compare advances that deadline by the Q16
period. Conversion retains the remainder of the exact 125/6 timer ticks per
48 kHz sample. At 120 BPM, successive ideal intervals are 20,833, 20,833 and
20,834 timer ticks. TIM3 priority 0 may briefly preempt priority-1 audio.

TIM3's IRQ only acknowledges compare, publishes at most one timestamped clock
event, advances the absolute deadline, and programs the next compare. Its
16-bit counter uses intermediate wakeups when a deadline is more than 60 ms
away. If multiple periods have passed, they are counted and skipped; no burst
is generated. A pending tempo change takes effect after the next emitted clock,
without resetting phase. The IRQ does not call TinyUSB or inspect DSP state.

START queues `FA` before arming the timer; CONTINUE queues `FB` first. STOP
disarms the timer and discards unpublished clock events before emitting `FC`.
The former audio-boundary clock producer is removed. The main-loop MIDI poll
consumes at most the newest pending timer event; it discards older or excessively
late events rather than emitting a catch-up burst. Transport messages may not
bypass already queued MIDI packets. External MIDI clock mode does not echo
received clocks.

The timer removes the 64-frame generation quantization. USB MIDI still uses the
128-packet application TX queue, TinyUSB's FIFO, a 64-byte Full Speed endpoint,
and cooperative service; host arrival timing remains subject to those stages.
`g_midi_clock_prof` exposes IRQ latency, IRQ cycles, missed ticks, queue drops,
and consumer delay in the Release/LTO ELF. Run
`tools/test_midi_clock_contract.ps1` for the deterministic source contract.
