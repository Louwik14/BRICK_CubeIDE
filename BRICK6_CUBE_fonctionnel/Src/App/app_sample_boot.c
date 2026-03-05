#include <stdio.h>

#include "App/app_sample_boot.h"
#include "Storage/wav_loader.h"
#include "Storage/audio_streamer.h"

#define DBG(...) printf(__VA_ARGS__)

void app_sample_boot_init(void)
{
    char wav_path[64];

    if(wav_loader_find_first_wav(wav_path, sizeof(wav_path)))
    {
        DBG("[WAV] found: %s\r\n", wav_path);

        if(!audio_streamer_start(wav_path))
            DBG("[STREAM] start failed\r\n");
    }
    else
    {
        DBG("[WAV] no WAV found\r\n");
    }
}
