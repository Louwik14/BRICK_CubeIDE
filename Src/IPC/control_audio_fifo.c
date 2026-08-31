#include "IPC/control_audio_fifo_layout.h"
#include "Platform/memory_layout.h"
D3_IPC control_audio_fifo_layout_t g_control_audio_fifo_layout;
AUDIO_STORAGE_SHARED_SDRAM control_audio_command_t
    g_control_audio_fifo_commands[CONTROL_AUDIO_FIFO_CAPACITY];
