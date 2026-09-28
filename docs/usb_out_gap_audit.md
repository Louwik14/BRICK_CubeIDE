# USB Audio OUT gap audit

USB FS IRQ has NVIC priority 1. SAI DMA is priority 2; SDMMC and SPI are
priority 5. The USB IRQ dispatches TinyUSB, then bounded USB MIDI service
(at most 16 queued MIDI packets). OUT transfer completion calls the TinyUSB
Audio ISR, which rearms endpoint 0x02 before the BRICK callback drains its
software FIFO. The DWC2 slave RX FIFO allocation is 216 words; it is not a
multi-millisecond queue at 48 kHz stereo with 32-bit subslots.

Most PRIMASK critical sections only copy flags, pointers, or bounded small
structures. Their code contains no wait for milliseconds. The exception is
`groove_flash_backend_erase_all/program`: IRQs remain disabled throughout a
FLASH bank-2 busy wait. Its duration depends on hardware and is not bounded
to less than one USB frame by the code. These calls run only while
`groove_bank_service()` processes the boot import; after
`groove_bank_boot_complete()`, the normal app loop does not call it. Flash
can explain a USB outage during import, but the code alone does not show that
it occurred at either captured glitch. SD/FatFs work outside PRIMASK can be
preempted by USB.

For a hardware run, `g_usb_audio_bus_gap_diag` is reset when OUT alt 1 opens.
It records the largest spacing and count of gaps >= 3 USB frames, separately
for observed SOFs and completed OUT packets. Compare its values immediately
before and after a glitch. A SOF gap supports an IRQ blackout; an OUT-only gap
supports missing bus packets or endpoint service; neither gap means the
USBPcap URB completion delay is not a device IRQ gap. This diagnostic only
reads the DWC2 frame register and updates counters in the existing USB IRQ.

The 14-bit DWC2 frame counter continues while GDB halts the CPU. On resume,
the first observed SOF/OUT can therefore be separated from the previous one
by thousands of frames even though the firmware was stopped for inspection.
Gaps above 32 frames are treated as an interrupted measurement: each previous
frame reference is resynchronized, without changing its count or maximum.
Gaps of 3 through 32 frames remain visible, including the suspected 4 ms
event. DHCSR does not expose a reliable post-resume halt indication to this
running Cortex-M7 code. Because the hardware frame number wraps every 16384
frames, a debugger pause whose duration aliases to 3–32 frames modulo 16384
cannot be distinguished by this frame-only diagnostic.

## Rolling IRQ trace

`g_usb_audio_trace` contains 1024 circular entries of 28 bytes (28,672 bytes
in RAM_D1). `g_usb_audio_trace_head` is the monotonically increasing next-write
index; the latest complete entry is `(head - 1) & 1023`. Entries have, in
order, `cycles` (DWT CYCCNT), `out_sequence`, `pcm[3]` (first three raw PCM32
words, zero for SOF), `frame` (DWC2 FNSOF), `bytes` (received OUT packet size,
zero for SOF), and `event` (1=SOF, 2=OUT). `g_usb_audio_out_sequence` advances
for each active OUT callback. The trace writes in the existing USB IRQ only;
there is no main-loop reader. SOF is recorded after TinyUSB IRQ dispatch and
OUT is timestamped at entry to `tud_audio_rx_done_isr`, before the PCM FIFO
read. Both use the existing DWT cycle counter enabled by `cpu_load_init()`;
TIM3 initialization also enables it. Its 32-bit count wraps, so compare
adjacent timestamps with unsigned subtraction. To inspect after a glitch,
halt once and dump `g_usb_audio_trace_head`, `g_usb_audio_out_sequence`, and
`g_usb_audio_trace`; a GDB halt itself creates an invalid last timing gap.

Static NVIC audit of the current working tree: OTG_FS priority 1; SAI RX/TX
DMA1 streams 3/4 priority 2; SAI1 peripheral priority 2. TIM3 is priority 3
in `midi_clock_timer_init()`; no later code changes its priority. There is no
normally enabled external IRQ at priority 0; OTG_FS is the only one at priority
1. TIM3 schedules MIDI clock comparisons and cannot preempt USB while armed;
it has no blocking wait in its handler.
SysTick is priority 15. Other configured IRQs are priority 2 or lower urgency.

Global PRIMASK masking can still block USB. The significant unbounded case is
bank-2 FLASH erase/program in `groove_flash_backend.c`, which polls FLASH busy
with interrupts disabled during boot import. `seq_engine_pattern_cycle_boundary`
scans bounded sequencer lanes under PRIMASK. Wavetable and sampler publication
copy bounded descriptors under PRIMASK. Other inspected critical sections copy
small state or perform bounded bookkeeping; fatal paths can leave interrupts
disabled permanently but do not represent a recoverable 4 ms stall.
