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
