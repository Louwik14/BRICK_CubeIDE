#include "App/hall_midi_test.h"

#include "App/hall_kbd.h"
#include "midi.h"

#define HALL_MIDI_TEST_KEY_COUNT 16U
#define HALL_MIDI_TEST_BASE_NOTE 48U
#define HALL_MIDI_TEST_CHANNEL   0U
#define HALL_MIDI_TEST_VELOCITY_FALLBACK 100U

static uint8_t prev_state[HALL_MIDI_TEST_KEY_COUNT];

void hall_midi_test_init(void)
{
  for (uint8_t key = 0U; key < HALL_MIDI_TEST_KEY_COUNT; key++)
  {
    prev_state[key] = hall_kbd_is_pressed(key);
  }
}

void hall_midi_test_process(void)
{
  for (uint8_t key = 0U; key < HALL_MIDI_TEST_KEY_COUNT; key++)
  {
    const uint8_t is_pressed = hall_kbd_is_pressed(key);

    if (is_pressed == prev_state[key])
    {
      continue;
    }

    const uint8_t note = (uint8_t)(HALL_MIDI_TEST_BASE_NOTE + key);

    if (is_pressed != 0U)
    {
      uint8_t velocity = hall_kbd_get_velocity(key);
      if (velocity == 0U)
      {
        velocity = HALL_MIDI_TEST_VELOCITY_FALLBACK;
      }

      const uint8_t note_on[3] = {
          (uint8_t)(0x90U | HALL_MIDI_TEST_CHANNEL),
          note,
          velocity};
      midi_send_raw(MIDI_DEST_USB, note_on, 3U);
    }
    else
    {
      const uint8_t note_off[3] = {
          (uint8_t)(0x80U | HALL_MIDI_TEST_CHANNEL),
          note,
          0U};
      midi_send_raw(MIDI_DEST_USB, note_off, 3U);
    }

    prev_state[key] = is_pressed;
  }
}
