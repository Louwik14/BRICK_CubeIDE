#ifndef USBD_BRICK6_COMPOSITE_H
#define USBD_BRICK6_COMPOSITE_H

#include "usbd_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_AUDIO_EP_OUT_ADDR          0x02U
#define BRICK6_AUDIO_EP_IN_ADDR           0x82U
#define BRICK6_AUDIO_EP_OUT_SIZE          384U
#define BRICK6_AUDIO_EP_IN_SIZE           384U
#define BRICK6_AUDIO_FRAMES_PER_PACKET    48U

#define BRICK6_AUDIO_CONTROL_IF           0x00U
#define BRICK6_AUDIO_STREAMING_OUT_IF     0x01U
#define BRICK6_AUDIO_STREAMING_IN_IF      0x02U
#define BRICK6_MIDI_STREAMING_IF          0x03U

extern USBD_ClassTypeDef USBD_BRICK6_COMPOSITE;

#ifdef __cplusplus
}
#endif

#endif /* USBD_BRICK6_COMPOSITE_H */
