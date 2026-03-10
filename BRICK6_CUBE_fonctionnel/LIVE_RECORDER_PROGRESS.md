# Live Recorder Progress

## Step update: step-limited recording transport (pre-sequencer)

Implemented a temporary `recorder_transport` layer to control recording length in **steps** without introducing a full sequencer yet.

### What was added

- New module:
  - `Inc/Audio/recorder_transport.h`
  - `Src/Audio/recorder_transport.c`
- Integration in `brick6_app_process()`:
  - `engine_tasklet_poll()`
  - `recorder_transport_process()`
  - main-loop edge handling that calls `live_recorder_start_record()` / `live_recorder_stop_record()`.

### Current behavior

- Manual trigger via `recorder_transport_start_record(steps)`.
- Supported lengths: `16`, `32`, `48`, `64` steps.
- Uses `engine_tick_count` as master clock (audio-derived, deterministic).
- Recording auto-stops exactly when the configured step count is reached.
- Stop happens from main loop only (never inside audio IRQ).

### Sequencer compatibility notes

When sequencer transport is introduced, it should replace:

- `ticks_per_step` mapping
- local step counting logic
- manual record trigger source

The following recorder backend API remains valid:

- `live_recorder_write()`
- `live_recorder_read()`
- `live_recorder_start_record()`
- `live_recorder_stop_record()`
