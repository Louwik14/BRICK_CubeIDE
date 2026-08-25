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

/* HOT code: explicit opt-in for code copied from Flash to ITCM at reset. */
#define ITCM_TEXT __attribute__((section(".itcm_text"), noinline))
#define ITCM_TEXT_NAMED(name) __attribute__((section(".itcm_text." name), noinline))
#define AUDIO_CODE_HOT ITCM_TEXT

/* HOT: IRQ critical data/state */
#define AUDIO_HOT SEC_ATTR(".dtcm_audio")

/* CPU-only hot UI/control state. Never use for FatFs/SD/DMA payload buffers. */
#define UI_HOT_DTCM SEC_ATTR(".dtcm_audio")

/* WARM: block DSP state not directly DMA-owned */
#define AUDIO_WARM SEC_ATTR(".ram_d1_audio")

/* DMA/shared buffers: hardware-proven non-cacheable D2 contract. */
#define DMA_BUFFER SEC_ATTR(".ram_d2_dma") ALIGN32

/* SAI RX/TX stays in the non-cacheable D2 window used by the GOOD image. */
#define AUDIO_DMA_BUFFER_NONCACHEABLE SEC_ATTR(".ram_d2_dma_audio") ALIGN32
#define AUDIO_DMA_BUFFER_IS_CACHEABLE 0U

/* Read-mostly audio LUTs moved out of D1 without using SDRAM. */
#define AUDIO_LUT_D2 SEC_ATTR(".ram_d2_local_cacheable")

/* Sequencer runtime/model state placed in internal D2 (non-SDRAM). */
#define SEQ_STATE_D2 SEC_ATTR(".ram_d2_m4")

/* CONTROL state local to SRAM2/D2, reserved for the future M4 owner. */
#define CONTROL_M4_SRAM2 SEC_ATTR(".ram_control_m4_sram2")

/* Explicit CONTROL/AUDIO IPC contract.  The linkers place this section in the
 * upper 32 KiB of SRAM4 and both boards map that window shareable,
 * non-cacheable.  Objects here must remain pointer-free and single-owner. */
#define D3_IPC SEC_ATTR(".ram_d3_ipc") ALIGN32

/* Bulk restore singleton: cacheable SDRAM payload plus pointer-free D3 doorbell.
 * Publication/consumption must perform explicit D-cache maintenance. */
#define RESTORE_PLAN_SDRAM SEC_ATTR(".restore_plan_sdram") ALIGN32
#define RESTORE_IPC_D3 SEC_ATTR(".ram_d3_restore_ipc") ALIGN32

/* Low-rate control/flags */
#define CTRL_STATE SEC_ATTR(".ram_d3_ctrl")
/* Bounded polyphonic DSP state; CPU-owned and directly accessed by audio IRQ. */
#define AUDIO_STATE_D3 SEC_ATTR(".ram_d3_ctrl")

/* UI / non real-time bulk buffers */
#define UI_SDRAM SEC_ATTR(".sdram_ui") ALIGN32

/* Small read-mostly UI registry kept internal to avoid SDRAM pressure. */
#define UI_D1 SEC_ATTR(".ram_d1_ui") ALIGN32

/* UI-owned state that is cold enough to keep out of D1. */
#define UI_STATE_SDRAM SEC_ATTR(".ui_state_sdram") ALIGN32

/* Low-rate control state that is not audio IRQ-owned. */
#define CONTROL_STATE_SDRAM SEC_ATTR(".control_state_sdram") ALIGN32

/* Storage metadata/state, not DMA-owned and not audio IRQ-owned. */
#define STORAGE_STATE_SDRAM SEC_ATTR(".storage_state_sdram") ALIGN32

/* Cooperative Sampler/Multi load queues and state, never audio IRQ-owned. */
#define MULTI_LOAD_SDRAM SEC_ATTR(".multi_load_sdram") ALIGN32

/* Storage conversion/scratch state used only by SD/FatFs services. */
#define STORAGE_SCRATCH_SDRAM SEC_ATTR(".storage_scratch_sdram") ALIGN32

/* Recorder/export scratch used by SD writer/export services, not IRQ producers. */
#define RECORDER_SCRATCH_SDRAM SEC_ATTR(".recorder_scratch_sdram") ALIGN32

/* Volatile editor-owned audio focus cache, not sample-owned and never IRQ-owned. */
#define EDITOR_AUDIO_CACHE_SDRAM SEC_ATTR(".editor_audio_cache_sdram") ALIGN32

/* Dedicated SDRAM arena for resident samples. */
#define SDRAM_SAMPLES SEC_ATTR(".sdram_samples") ALIGN32

/* Dedicated SDRAM arena for page-cache descriptors. */
#define SDRAM_SAMPLE_PAGE_DESC SEC_ATTR(".page_desc_sdram") ALIGN32

/* Dedicated SDRAM arena for Sampler dynamic sample audio pages only. */
#define SDRAM_PAGE_POOL SEC_ATTR(".sdram_sample_page_pool") ALIGN32
#define SDRAM_PAGE_META SEC_ATTR(".sdram_page_meta") ALIGN32
#define SDRAM_PAGE_INDEX SEC_ATTR(".sdram_page_index") ALIGN32
#define SDRAM_STREAM_SERVICE SEC_ATTR(".sdram_stream_service") ALIGN32
#define SDRAM_STREAM_SCRATCH SEC_ATTR(".sdram_stream_scratch") ALIGN32
#define SDRAM_MULTI_POOL SEC_ATTR(".sdram_multi_pool") ALIGN32
#define SDRAM_MULTI_LOAD SEC_ATTR(".sdram_multi_load") ALIGN32
#define SDRAM_MULTI_IMPORT SEC_ATTR(".sdram_multi_import") ALIGN32
#define SDRAM_CLASSIC_POOL SEC_ATTR(".sdram_classic_pool") ALIGN32

/* Dedicated SDRAM arena for recorder/master-buffer history. */
#define SDRAM_RECORDER SEC_ATTR(".sdram_recorder") ALIGN32
/* M7/M4 CPU bulk data in the same shareable, non-cacheable 256 KiB MPU region. */
#define AUDIO_STORAGE_SHARED_SDRAM SEC_ATTR(".sdram_recorder") ALIGN32

/* Large cold audio history (delay/grain/reverb tails) */
#define AUDIO_COLD_SDRAM SEC_ATTR(".sdram_audio_cold") ALIGN32

/* Shared global send-delay pool. */
#define AUDIO_DELAY_SDRAM SEC_ATTR(".audio_delay_sdram") ALIGN32

/* Dedicated SDRAM arena for audio history buffers with direct IRQ access. */
#define AUDIO_HISTORY_SDRAM SEC_ATTR(".audio_history_sdram") ALIGN32

#endif /* MEMORY_LAYOUT_H */
