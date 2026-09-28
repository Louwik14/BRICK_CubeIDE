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
