#ifndef MEMORY_LAYOUT_H
#define MEMORY_LAYOUT_H

/*
 * Memory placement macros for STM32H743 BRICK6 project.
 *
 * Usage:
 *   AUDIO_HOT static float hp_state[2];
 *   DMA_BUFFER static int32_t rx_pingpong[1024];
 *
 * All DMA buffers must be 32-byte aligned (cache line size) to be
 * safe when D-Cache gets enabled.
 */

#define SEC_ATTR(name) __attribute__((section(name)))
#define ALIGN32 __attribute__((aligned(32)))
#define ALIGN64 __attribute__((aligned(64)))

/* HOT: IRQ critical data/state */
#define AUDIO_HOT SEC_ATTR(".dtcm_audio")

/* WARM: block DSP state not directly DMA-owned */
#define AUDIO_WARM SEC_ATTR(".ram_d1_audio")

/* DMA-owned buffers (SAI/SDMMC/other DMA) */
#define DMA_BUFFER SEC_ATTR(".ram_d2_dma") ALIGN32

/* Read-mostly audio LUTs moved out of D1 without using SDRAM. */
#define AUDIO_LUT_D2 SEC_ATTR(".ram_d2_lut")

/* Sequencer runtime/model state placed in internal D2 (non-SDRAM). */
#define SEQ_STATE_D2 SEC_ATTR(".ram_d2_lut")

/* Low-rate control/flags */
#define CTRL_STATE SEC_ATTR(".ram_d3_ctrl")

/* UI / non real-time bulk buffers */
#define UI_SDRAM SEC_ATTR(".sdram_ui") ALIGN32

/* Large cold audio history (delay/grain/reverb tails) */
#define AUDIO_COLD_SDRAM SEC_ATTR(".sdram_audio_cold") ALIGN32

#endif /* MEMORY_LAYOUT_H */
