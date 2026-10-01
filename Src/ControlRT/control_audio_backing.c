#include "ControlRT/control_audio_fifo_layout.h"
#include "ControlRT/audio_state_transaction.h"
#include "ControlRT/prepared_audio_state.h"
#include "Platform/memory_layout.h"

CTRL_STATE control_audio_fifo_layout_t g_control_audio_fifo_layout;
CONTROL_STATE_SDRAM control_audio_command_t
    g_control_audio_fifo_commands[CONTROL_AUDIO_FIFO_CAPACITY];
CONTROL_STATE_SDRAM audio_state_transaction_t g_audio_state_transaction;
CONTROL_STATE_SDRAM prepared_audio_slot_t
    g_prepared_audio_slots[PREPARED_AUDIO_SLOT_COUNT];
