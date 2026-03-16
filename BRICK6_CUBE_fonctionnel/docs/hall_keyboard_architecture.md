# Hall Effect Keyboard Architecture (STM32H7)

## Goals

Design a deterministic and low‑latency Hall sensor keyboard driver
compatible with an audio DSP environment where audio interrupts have the
highest priority.

Key constraints: - No jitter affecting audio ISR - Deterministic scan
timing - Low CPU usage - Clean architecture separating hardware
acquisition from keyboard logic

------------------------------------------------------------------------

# Hardware Pipeline

The acquisition pipeline is fully hardware‑driven using timers and DMA.

TIM_SCAN (1 kHz) → change MUX → TIM_ADC (8 kHz) → ADC conversion → DMA
circular → RAM buffer

This architecture ensures the CPU never waits for ADC conversions.

------------------------------------------------------------------------

# Timers

## Timer 1 --- Scan Timer

Purpose: - Advance the multiplexers.

Frequency: 1 kHz

Responsibilities: - increment mux index - select next channel

Pseudo behaviour:

TIM_SCAN IRQ mux_index++ hall_mux_select(mux_index)

No ADC operations occur here.

------------------------------------------------------------------------

## Timer 2 --- ADC Timer

Purpose: - Trigger ADC conversions at a constant rate.

Frequency:

scan_rate × mux_channels

Example:

1 kHz × 8 mux = 8 kHz

Configuration:

ADC external trigger = TIM_ADC TRGO

This guarantees deterministic sampling.

------------------------------------------------------------------------

# DMA

DMA runs in circular mode.

Example buffer:

uint16_t adc_dma_buffer\[16\];

Memory layout example:

mux0 ADC1 → key0 mux0 ADC2 → key8 mux1 ADC1 → key1 mux1 ADC2 → key9 ...

DMA writes samples continuously with zero CPU overhead.

------------------------------------------------------------------------

# Software Architecture

The firmware is divided into three layers.

ADC + DMA ↓ hall_adc ↓ hall_engine ↓ hall_kbd

------------------------------------------------------------------------

# Layer 1 --- hall_adc (hardware driver)

Responsibilities: - control mux - configure timers - configure ADC -
manage DMA circular buffer

Outputs raw values only.

Example API:

uint16_t hall_adc_get_raw(uint8_t key);

No filtering or keyboard logic.

------------------------------------------------------------------------

# Layer 2 --- hall_engine (signal processing)

Responsibilities: - filtering - min/max tracking - normalization -
velocity detection - pressed state

Input: raw ADC values

Output: value (0‑127) velocity (0‑127) pressed state

------------------------------------------------------------------------

# Layer 3 --- hall_kbd (keyboard interface)

Responsibilities: - event generation - MIDI interaction - UI
interaction - user calibration control

Example events:

PRESS RELEASE

------------------------------------------------------------------------

# Calibration Strategy

Calibration occurs explicitly on user request.

Process: 1. User enters calibration mode. 2. User presses every key
fully. 3. Firmware records min/max. 4. Values stored in flash.

After reboot:

load calibration from flash

This avoids continuous recalibration during normal usage.

------------------------------------------------------------------------

# Development Phases

Recommended development order.

Phase 1 --- acquisition - mux - timers - ADC - DMA

Phase 2 --- filtering - reduce sensor noise

Phase 3 --- normalization normalized = (value - min) / (max - min)

Phase 4 --- velocity measure time between thresholds

Phase 5 --- events NOTE ON / NOTE OFF

------------------------------------------------------------------------

# Interrupt Priority Strategy

Audio interrupt must always remain highest priority.

Example priorities:

Audio ISR → priority 0 DMA ADC → priority 4 Scan Timer → priority 5

This prevents audio glitches.

------------------------------------------------------------------------

# Summary

Final pipeline:

TIM_SCAN → change mux TIM_ADC → trigger ADC ADC → DMA circular CPU →
process samples

The CPU never blocks on ADC operations, ensuring compatibility with
real‑time audio processing.
