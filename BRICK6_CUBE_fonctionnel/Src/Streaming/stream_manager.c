#include "Streaming/stream_manager.h"
#include "Storage/audio_streamer.h"

bool stream_manager_start(const char *path)
{
    return audio_streamer_start(path);
}

void stream_manager_process(void)
{
    audio_streamer_process();
}

void stream_manager_get_frame(float *L, float *R)
{
    audio_streamer_get_frame(L, R);
}
