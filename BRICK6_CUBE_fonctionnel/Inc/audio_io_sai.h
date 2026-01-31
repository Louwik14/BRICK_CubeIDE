#ifndef AUDIO_IO_SAI_H
#define AUDIO_IO_SAI_H

#include <stdint.h>

const int32_t *audio_io_sai_get_rx_block(void);
int32_t *audio_io_sai_get_tx_block(void);
uint32_t audio_io_sai_get_frames(void);

#endif /* AUDIO_IO_SAI_H */
