#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

typedef struct __SAI_HandleTypeDef SAI_HandleTypeDef;

void Audio_Init(SAI_HandleTypeDef *hsai);
void Audio_Start(void);
void Audio_Process_Half(void);
void Audio_Process_Full(void);
void Audio_DebugDump(void);
uint32_t Audio_GetHalfEvents(void);
uint32_t Audio_GetFullEvents(void);

#endif /* AUDIO_H */
