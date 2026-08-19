/*
******************************************************************************
**
**  File        : LinkerScript.ld
**
**  Author      : STM32CubeIDE
**
**  Abstract    : Linker script for STM32H7 series
**                2048Kbytes FLASH and 1056Kbytes RAM
**
**                Set heap size, stack size and stack location according
**                to application requirements.
**
**                Set memory bank area and size if external memory is used.
**
**  Target      : STMicroelectronics STM32
**
**  Distribution: The file is distributed as is, without any warranty
**                of any kind.
**
*****************************************************************************
** @attention
**
** Copyright (c) 2025 STMicroelectronics.
** All rights reserved.
**
** This software is licensed under terms that can be found in the LICENSE file
** in the root directory of this software component.
** If no LICENSE file comes with this software, it is provided AS-IS.
**
****************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM_D1) + LENGTH(RAM_D1);    /* end of RAM */
/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size = 0x200;      /* required amount of heap  */
_Min_Stack_Size = 0x400; /* required amount of stack */

/* Specify the memory areas */
MEMORY
{
  /* Application firmware area (bank1 + bank2 sectors 0..5) */
  FLASH (rx)          : ORIGIN = 0x08000000, LENGTH = 1792K
  /* Dedicated bank2 sector6 for minimal boot context persistence */
  BOOT_CTX_FLASH (rx) : ORIGIN = 0x081C0000, LENGTH = 128K
  DTCMRAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 128K
  RAM_D1 (xrw)   : ORIGIN = 0x24000000, LENGTH = 512K
  SRAM1_D2_DMA_NC (xrw) : ORIGIN = 0x30000000, LENGTH = 12K
  SRAM1_D2_CACHEABLE (xrw) : ORIGIN = 0x30003000, LENGTH = 116K
  SRAM2_D2 (xrw) : ORIGIN = 0x30020000, LENGTH = 128K
  SRAM3_D2 (xrw) : ORIGIN = 0x30040000, LENGTH = 32K
  RAM_D3 (xrw)   : ORIGIN = 0x38000000, LENGTH = 64K
  BKPSRAM (xrw)  : ORIGIN = 0x38800000, LENGTH = 4K
  SDRAM (xrw)          : ORIGIN = 0xC0000000, LENGTH = 31M
  SDRAM_RECORDER (xrw) : ORIGIN = 0xC1F00000, LENGTH = 1M
  ITCMRAM (xrw)  : ORIGIN = 0x00000000, LENGTH = 64K
}

/* Exported symbols for boot context backend */
__boot_context_flash_start__ = ORIGIN(BOOT_CTX_FLASH);
__boot_context_flash_end__ = ORIGIN(BOOT_CTX_FLASH) + LENGTH(BOOT_CTX_FLASH);
__sdram_global_start__ = ORIGIN(SDRAM);
__sdram_global_end__ = ORIGIN(SDRAM_RECORDER) + LENGTH(SDRAM_RECORDER);
__sdram_cacheable_start__ = ORIGIN(SDRAM);
__sdram_cacheable_end__ = ORIGIN(SDRAM) + LENGTH(SDRAM);
__sdram_recorder_region_start__ = ORIGIN(SDRAM_RECORDER);
__sdram_recorder_region_end__ = ORIGIN(SDRAM_RECORDER) + LENGTH(SDRAM_RECORDER);
__sram1_d2_start__ = ORIGIN(SRAM1_D2_DMA_NC);
__sram1_d2_end__ = ORIGIN(SRAM1_D2_CACHEABLE) + LENGTH(SRAM1_D2_CACHEABLE);
__sram2_d2_start__ = ORIGIN(SRAM2_D2);
__sram2_d2_end__ = ORIGIN(SRAM2_D2) + LENGTH(SRAM2_D2);
__sram3_d2_start__ = ORIGIN(SRAM3_D2);
__sram3_d2_end__ = ORIGIN(SRAM3_D2) + LENGTH(SRAM3_D2);

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into FLASH */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >FLASH

  /* The program code and other data goes into FLASH */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >FLASH

  .itcm_text :
  {
    . = ALIGN(4);
    __itcm_text_start__ = .;
    *(.itcm_text)
    *(.itcm_text*)
    . = ALIGN(4);
    __itcm_text_end__ = .;
  } >ITCMRAM AT> FLASH
  __itcm_text_load__ = LOADADDR(.itcm_text);

  /* Constant data goes into FLASH */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >FLASH

  .ARM.extab (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH
  .ARM (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH

  .init_array (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH

  .fini_array (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  /* Reserved persistent flash area for minimal boot context only (no C data linked here) */
  .boot_context_flash (NOLOAD) :
  {
    . = ALIGN(32);
    . = ALIGN(32);
  } >BOOT_CTX_FLASH

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into RAM, load LMA copy after code */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
  } >RAM_D1 AT> FLASH

  /* Uninitialized data section */
  . = ALIGN(4);
  .bss :
  {
    /* This is used by the startup in order to initialize the .bss section */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >RAM_D1

  .ram_d1 (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d1)
    . = ALIGN(32);
  } >RAM_D1

  .ram_d1_ui (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d1_ui)
    *(.ram_d1_ui*)
    . = ALIGN(32);
  } >RAM_D1

  /* USER MEMORY SECTIONS BEGIN */
  .dtcm_audio (NOLOAD) :
  {
    . = ALIGN(32);
    *(.dtcm_audio)
    *(.dtcm_audio*)
    . = ALIGN(32);
  } >DTCMRAM

  .dtcm_fault (NOLOAD) :
  {
    . = ALIGN(8);
    *(.dtcm_fault)
    *(.dtcm_fault*)
    . = ALIGN(8);
  } >DTCMRAM

  .ram_d1_audio (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d1_audio)
    *(.ram_d1_audio*)
    . = ALIGN(32);
  } >RAM_D1

  .ram_d2_dma (NOLOAD) :
  {
    . = ALIGN(32);
    __ram_d2_dma_start__ = .;
    *(.ram_d2_dma)
    . = ALIGN(32);
    __ram_d2_dma_end__ = .;
  } >SRAM1_D2_DMA_NC
  ASSERT((__ram_d2_dma_end__ - __ram_d2_dma_start__) <= LENGTH(SRAM1_D2_DMA_NC),
         "SRAM1 D2 DMA contract overflow")

  .ram_d2_dma_audio (NOLOAD) :
  {
    . = ALIGN(32);
    __ram_d2_dma_audio_start__ = .;
    *(.ram_d2_dma_audio)
    *(.ram_d2_dma_audio*)
    . = ALIGN(32);
    __ram_d2_dma_audio_end__ = .;
  } >SRAM1_D2_DMA_NC
  ASSERT((__ram_d2_dma_audio_end__ - __ram_d2_dma_audio_start__) <= LENGTH(SRAM1_D2_DMA_NC),
         "SRAM1 D2 audio DMA contract overflow")

  .ram_d2_dma_cacheable (NOLOAD) :
  {
    . = ALIGN(32);
    __ram_d2_dma_cacheable_start__ = .;
    *(.ram_d2_dma_cacheable)
    *(.ram_d2_dma_cacheable*)
    . = ALIGN(32);
    __ram_d2_dma_cacheable_end__ = .;
  } >SRAM1_D2_CACHEABLE
  ASSERT((__ram_d2_dma_cacheable_end__ - __ram_d2_dma_cacheable_start__) <= LENGTH(SRAM1_D2_CACHEABLE),
         "SRAM1 D2 cacheable DMA contract overflow")

  .ram_d2_local_cacheable (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d2_local_cacheable)
    *(.ram_d2_local_cacheable*)
    . = ALIGN(32);
  } >SRAM1_D2_CACHEABLE

  .ram_d2_m4_sram1 (NOLOAD) :
  {
    . = ALIGN(32);
    * (EXCLUDE_FILE (*seq_model.c.obj *note_fx_pipeline.c.obj) .ram_d2_m4)
    * (EXCLUDE_FILE (*seq_model.c.obj *note_fx_pipeline.c.obj) .ram_d2_m4.*)
    . = ALIGN(32);
  } >SRAM1_D2_CACHEABLE

  .ram_d2_m4_sram2 (NOLOAD) :
  {
    . = ALIGN(32);
    *seq_model.c.obj(.ram_d2_m4)
    *seq_model.c.obj(.ram_d2_m4.*)
    . = ALIGN(32);
  } >SRAM2_D2
  ASSERT(SIZEOF(.ram_d2_m4_sram2) <= LENGTH(SRAM2_D2),
         "SRAM2 D2 M4 contract overflow")

  .ram_control_m4_sram2 (NOLOAD) :
  {
    . = ALIGN(32);
    __ram_control_m4_sram2_start__ = .;
    *(.ram_control_m4_sram2)
    *(.ram_control_m4_sram2.*)
    . = ALIGN(32);
    __ram_control_m4_sram2_end__ = .;
  } >SRAM2_D2
  ASSERT(SIZEOF(.ram_control_m4_sram2) <= LENGTH(SRAM2_D2),
         "SRAM2 D2 CONTROL M4 contract overflow")

  .ram_d2_m7 (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d2_m7)
    *(.ram_d2_m7*)
    . = ALIGN(32);
  } >SRAM2_D2

  .ram_d2_m4_sram3 (NOLOAD) :
  {
    . = ALIGN(32);
    *note_fx_pipeline.c.obj(.ram_d2_m4)
    *note_fx_pipeline.c.obj(.ram_d2_m4.*)
    . = ALIGN(32);
  } >SRAM3_D2
  ASSERT(SIZEOF(.ram_d2_m4_sram3) <= LENGTH(SRAM3_D2),
         "SRAM3 D2 M4 contract overflow")

  .ram_d2_ipc (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d2_ipc)
    *(.ram_d2_ipc*)
    . = ALIGN(32);
  } >SRAM3_D2

  .ram_d3_ctrl (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d3_ctrl)
    *(.ram_d3_ctrl*)
    . = ALIGN(32);
  } >RAM_D3

  .ram_d3_ipc (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d3_ipc)
    *(.ram_d3_ipc*)
    . = ALIGN(32);
  } >RAM_D3
  ASSERT(SIZEOF(.ram_d3_ctrl) + SIZEOF(.ram_d3_ipc) <= LENGTH(RAM_D3),
         "SRAM4 D3 contract overflow")

  .backup_sram (NOLOAD) :
  {
    . = ALIGN(32);
    __backup_sram_start__ = .;
    KEEP(*(.backup_sram))
    KEEP(*(.backup_sram*))
    . = ALIGN(32);
    __backup_sram_end__ = .;
  } >BKPSRAM
  ASSERT((__backup_sram_end__ - __backup_sram_start__) <= LENGTH(BKPSRAM),
         "Backup SRAM overflow")

  .multi_load_sdram (NOLOAD) :
  {
    . = ALIGN(32);
    *(.multi_load_sdram)
    *(.multi_load_sdram*)
    . = ALIGN(32);
  } >SDRAM

  .storage_scratch_sdram (NOLOAD) :
  {
    . = ALIGN(32);
    *(.storage_scratch_sdram)
    *(.storage_scratch_sdram*)
    . = ALIGN(32);
  } >SDRAM

  .recorder_scratch_sdram (NOLOAD) :
  {
    . = ALIGN(32);
    *(.recorder_scratch_sdram)
    *(.recorder_scratch_sdram*)
    . = ALIGN(32);
  } >SDRAM

  .editor_audio_cache_sdram (NOLOAD) :
  {
    . = ALIGN(32);
    *(.editor_audio_cache_sdram)
    *(.editor_audio_cache_sdram*)
    . = ALIGN(32);
  } >SDRAM

  .ui_state_sdram (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ui_state_sdram)
    *(.ui_state_sdram*)
    . = ALIGN(32);
  } >SDRAM

  .control_state_sdram (NOLOAD) :
  {
    . = ALIGN(32);
    *(.control_state_sdram)
    *(.control_state_sdram*)
    . = ALIGN(32);
  } >SDRAM

  .storage_state_sdram (NOLOAD) :
  {
    . = ALIGN(32);
    *(.storage_state_sdram)
    *(.storage_state_sdram*)
    . = ALIGN(32);
  } >SDRAM

  .sdram_sample_page_pool (NOLOAD) :
  {
    . = ALIGN(32);
    __sdram_page_pool_start__ = .;
    *(.sdram_sample_page_pool)
    *(.sdram_sample_page_pool*)
    . = ALIGN(32);
    __sdram_page_pool_end__ = .;
  } >SDRAM

  .sdram_page_meta (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram_page_meta)
    *(.sdram_page_meta*)
    . = ALIGN(32);
  } >SDRAM

  .sdram_page_index (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram_page_index)
    *(.sdram_page_index*)
    . = ALIGN(32);
  } >SDRAM

  .sdram_stream_service (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram_stream_service)
    *(.sdram_stream_service*)
    . = ALIGN(32);
  } >SDRAM

  .sdram_stream_scratch (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram_stream_scratch)
    *(.sdram_stream_scratch*)
    . = ALIGN(32);
  } >SDRAM

  .sdram_multi_pool (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram_multi_pool)
    *(.sdram_multi_pool*)
    . = ALIGN(32);
  } >SDRAM

  .sdram_multi_load (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram_multi_load)
    *(.sdram_multi_load*)
    . = ALIGN(32);
  } >SDRAM

  .sdram_multi_import (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram_multi_import)
    *(.sdram_multi_import*)
    . = ALIGN(32);
  } >SDRAM

  .sdram_classic_pool (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram_classic_pool)
    *(.sdram_classic_pool*)
    . = ALIGN(32);
  } >SDRAM

  .audio_delay_sdram (NOLOAD) :
  {
    . = ALIGN(32);
    *(.audio_delay_sdram)
    *(.audio_delay_sdram*)
    . = ALIGN(32);
  } >SDRAM

  .audio_history_sdram (NOLOAD) :
  {
    . = ALIGN(32);
    *(.audio_history_sdram)
    *(.audio_history_sdram*)
    . = ALIGN(32);
  } >SDRAM

  .sdram (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram)
    . = ALIGN(32);
  } >SDRAM

  .sdram_ui (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram_ui)
    *(.sdram_ui*)
    . = ALIGN(32);
  } >SDRAM

  .sdram_samples (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram_samples)
    *(.sdram_samples*)
    . = ALIGN(32);
  } >SDRAM

  .page_desc_sdram (NOLOAD) :
  {
    . = ALIGN(32);
    *(.page_desc_sdram)
    *(.page_desc_sdram*)
    . = ALIGN(32);
  } >SDRAM

  .sdram_recorder (NOLOAD) :
  {
    . = ALIGN(32);
    __sdram_recorder_start__ = .;
    *(.sdram_recorder)
    *(.sdram_recorder*)
    . = ALIGN(32);
    __sdram_recorder_end__ = .;
  } >SDRAM_RECORDER

  .sdram_audio_cold (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram_audio_cold)
    *(.sdram_audio_cold*)
    . = ALIGN(32);
  } >RAM_D1
  ASSERT(__sdram_page_pool_start__ >= __sdram_cacheable_start__,
         "Sample page pool starts outside cacheable SDRAM")
  ASSERT(__sdram_page_pool_end__ <= __sdram_cacheable_end__,
         "Sample page pool exceeds cacheable SDRAM")
  ASSERT(__sdram_recorder_start__ >= __sdram_recorder_region_start__,
         "Recorder starts outside dedicated SDRAM region")
  ASSERT(__sdram_recorder_end__ <= __sdram_recorder_region_end__,
         "Recorder exceeds dedicated SDRAM region")
  ASSERT((ORIGIN(SDRAM_RECORDER) & (LENGTH(SDRAM_RECORDER) - 1)) == 0,
         "Recorder SDRAM region is not MPU aligned")
  ASSERT((LENGTH(SDRAM_RECORDER) & (LENGTH(SDRAM_RECORDER) - 1)) == 0,
         "Recorder SDRAM region size is not a power of two")
  /* USER MEMORY SECTIONS END */

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM_D1

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
