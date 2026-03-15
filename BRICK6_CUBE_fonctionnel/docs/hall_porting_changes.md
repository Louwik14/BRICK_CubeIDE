# Hall driver HAL porting notes

## Scope

This port moves the Hall keyboard logic from the legacy ChibiOS implementation
(`HallEffect_chibios_old/drv_hall.c`) into the current STM32H743 HAL firmware
architecture without modifying CubeMX-generated files.

## Step 1: minimal polling module (`hall_mux_test`)

A new bring-up module was added:

- `Inc/App/hall_mux_test.h`
- `Src/App/hall_mux_test.c`

Behavior:

- Scans mux positions 0..7 sequentially.
- Selects mux via `HAL_GPIO_WritePin`.
- Waits fixed settle delay using `TIM5->CNT` microsecond timer.
- Reads ADC1 + ADC2 pair using:
  - `HAL_ADC_Start`
  - `HAL_ADC_PollForConversion`
  - `HAL_ADC_GetValue`
  - `HAL_ADC_Stop`
- Stores raw values in a 16-entry array (`[0..7]=ADC1`, `[8..15]=ADC2`).
- Exposes readings through `hall_mux_test_get_raw(key)`.

## Step 2: driver logic ported to HAL

`Src/App/hall_kbd.c` now ports the ChibiOS hall logic concepts to HAL-compatible code:

- Mux select: `palWriteLine` -> `HAL_GPIO_WritePin`.
- ADC conversion: `adcConvert` -> HAL ADC start/poll/get/stop sequence.
- Microsecond settle wait: `chThdSleepMicroseconds` -> TIM5 counter wait loop.
- Timestamping for velocity: `chVTGetSystemTimeX` -> TIM5 1 MHz free-running counter.

Reintroduced logic:

- IIR-style filtering (`HALL_FILTER_FACTOR`).
- Dynamic min/max tracking per key.
- Press/release thresholds with hysteresis.
- Deadzone-based normalized value output (0..127).
- Velocity arming window and velocity computation from threshold crossing time.

## Step 3: timer-driven ISR architecture

The final architecture remains timer-driven and deterministic:

- TIM6 update IRQ drives the keyboard scan loop.
- ISR performs full 8-step mux scan (2 keys per step = 16 keys/frame).
- ISR updates per-key state and pushes press/release events to SPSC ring buffer.
- Main loop (`hall_kbd_poll` via `engine_tick`) consumes events and publishes stable key state.

Real-time safeguards:

- No dynamic allocation.
- No blocking delays in ISR (`HAL_Delay` not used).
- Bounded ADC polling loop (`HALL_ADC_POLL_MAX_ITER`).
- Overrun/overflow counters kept for instrumentation.
