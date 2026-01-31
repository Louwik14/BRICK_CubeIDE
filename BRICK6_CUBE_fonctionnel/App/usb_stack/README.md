\
# TinyUSB - STM32H7 FS - UAC2 (stereo 48kHz in/out) + USB MIDI 1.0

This folder contains the **project-side** TinyUSB files you typically own:
- `tusb_config.h`
- `usb_descriptors.h`
- `usb_descriptors.c`

They implement the descriptor topology you posted (ChibiOS ref), but **without** the vendor/bulk interface:
- Device class = 0xEF/0x02/0x01 (IAD composite)
- One IAD groups:
  - AudioControl (UAC2)
  - AudioStreaming Speaker (alt0/alt1)
  - AudioStreaming Microphone (alt0/alt1)
  - MIDIStreaming (USB-MIDI 1.0)

## STM32H7 notes (vs F4)
- **D-Cache**: If enabled, USB buffers must be cache-coherent.
  - Option A: Put USB buffers in DTCM / non-cacheable SRAM (MPU).
  - Option B: Enable TinyUSB cache sync via `CFG_TUD_MEM_DCACHE_ENABLE=1` and ensure buffers are cache-line aligned.
- **UID / Serial**: We use `HAL_GetUIDw0/1/2()` instead of hardcoding F4 UID addresses.

## Endpoint map
- MIDI:  EP1 OUT (0x01), EP1 IN (0x81)  (bulk, 64B)
- Audio: EP3 OUT (0x03), EP3 IN (0x83)  (iso, 192B @ 48k stereo 16b)

If you need different endpoint numbers, update `usb_descriptors.h` and the descriptor bytes in `usb_descriptors.c`.

## What you still need to provide
This gives you enumeration + class drivers. You still have to connect audio samples to your codec/DMA:
- In your main loop / RTOS task: call `tud_task()` frequently (<= 1ms jitter).
- Implement/override the TinyUSB audio callbacks you need (mute/volume, stream read/write),
  according to the TinyUSB version you vendored in your project.

Tip: start from your existing TinyUSB audio project, then drop these files in and enable `CFG_TUD_MIDI=1`.
