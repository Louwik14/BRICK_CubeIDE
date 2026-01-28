#include "sdram.h"
#include "w9825g6kh.h"
#include "fmc.h"

void SDRAM_Init(void)
{
    W9825G6KH_Context_t sdramCtx;

    sdramCtx.TargetBank      = FMC_SDRAM_CMD_TARGET_BANK1;
    sdramCtx.RefreshMode     = W9825G6KH_AUTOREFRESH_MODE_CMD;
    sdramCtx.RefreshRate     = REFRESH_COUNT;
    sdramCtx.BurstLength     = W9825G6KH_BURST_LENGTH_8;
    sdramCtx.BurstType       = W9825G6KH_BURST_TYPE_SEQUENTIAL;
    sdramCtx.CASLatency      = W9825G6KH_CAS_LATENCY_3;
    sdramCtx.OperationMode   = W9825G6KH_OPERATING_MODE_STANDARD;
    sdramCtx.WriteBurstMode  = W9825G6KH_WRITEBURST_MODE_PROGRAMMED;

    W9825G6KH_Init(&hsdram1, &sdramCtx);
}
