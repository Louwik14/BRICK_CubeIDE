/*
******************************************************************************
**
**  File        : LinkerScript.ld (debug in RAM dedicated)
**
**  Author      : STM32CubeIDE
**
**  Abstract    : Linker script for STM32H7 series
**                512Kbytes RAM_EXEC and 544Kbytes RAM
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
_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);    /* end of RAM */
/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size = 0x200;      /* required amount of heap  */
_Min_Stack_Size = 0x400; /* required amount of stack */

/* Specify the memory areas */
MEMORY
{
  RAM_EXEC (xrw)  : ORIGIN = 0x24000000, LENGTH = 512K
  DTCMRAM (xrw)   : ORIGIN = 0x20000000, LENGTH = 128K
  SRAM1_D2_DMA_NC (xrw) : ORIGIN = 0x30000000, LENGTH = 12K
  SRAM1_D2_CACHEABLE (xrw) : ORIGIN = 0x30003000, LENGTH = 116K
  SRAM2_D2 (xrw) : ORIGIN = 0x30020000, LENGTH = 128K
  SRAM3_D2 (xrw) : ORIGIN = 0x30040000, LENGTH = 32K
  RAM_D3 (xrw)    : ORIGIN = 0x38000000, LENGTH = 64K
  SDRAM (xrw)     : ORIGIN = 0xC0000000, LENGTH = 32M
  ITCMRAM (xrw)   : ORIGIN = 0x00000000, LENGTH = 64K
}

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into RAM_EXEC */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >RAM_EXEC

  /* The program code and other data goes into RAM_EXEC */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >RAM_EXEC

  .itcm_text :
  {
    . = ALIGN(4);
    __itcm_text_start__ = .;
    *(.itcm_text)
    *(.itcm_text*)
    . = ALIGN(4);
    __itcm_text_end__ = .;
  } >ITCMRAM AT> RAM_EXEC
  __itcm_text_load__ = LOADADDR(.itcm_text);

  /* Constant data goes into RAM_EXEC */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >RAM_EXEC

  .ARM.extab (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >RAM_EXEC
  .ARM (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >RAM_EXEC

  .preinit_array (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >RAM_EXEC

  .init_array (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >RAM_EXEC

  .fini_array (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >RAM_EXEC

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into RAM, load LMA copy after code */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
  } >DTCMRAM AT> RAM_EXEC

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
  } >DTCMRAM

  .ram_d1 (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d1)
    . = ALIGN(32);
  } >RAM_EXEC

  .ram_d1_ui (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d1_ui)
    *(.ram_d1_ui*)
    . = ALIGN(32);
  } >RAM_EXEC

  /* USER MEMORY SECTIONS BEGIN */
  .dtcm_audio (NOLOAD) :
  {
    . = ALIGN(32);
    *(.dtcm_audio)
    *(.dtcm_audio*)
    . = ALIGN(32);
  } >DTCMRAM

  .ram_d1_audio (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d1_audio)
    *(.ram_d1_audio*)
    . = ALIGN(32);
  } >RAM_EXEC

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
  ASSERT(SIZEOF(.ram_d3_ctrl) <= 32K,
         "SRAM4 cacheable CONTROL half overflow")

  .ram_d3_restore_ipc ORIGIN(RAM_D3) + 32K (NOLOAD) :
  {
    . = ALIGN(32);
    __restore_ipc_start__ = .;
    KEEP(*(.ram_d3_restore_ipc))
    KEEP(*(.ram_d3_restore_ipc.*))
    . = ALIGN(32);
    __restore_ipc_end__ = .;
  } >RAM_D3
  ASSERT(SIZEOF(.ram_d3_restore_ipc) <= 64,
         "Restore IPC exceeds its 64-byte contract")

  .ram_d3_ipc (NOLOAD) :
  {
    . = ALIGN(32);
    *(.ram_d3_ipc)
    *(.ram_d3_ipc*)
    . = ALIGN(32);
  } >RAM_D3
  ASSERT(SIZEOF(.ram_d3_restore_ipc) + SIZEOF(.ram_d3_ipc) <= 32K,
         "SRAM4 non-cacheable IPC half overflow")

  .restore_plan_sdram (NOLOAD) :
  {
    . = ALIGN(32);
    __restore_plan_start__ = .;
    KEEP(*(.restore_plan_sdram))
    KEEP(*(.restore_plan_sdram.*))
    . = ALIGN(32);
    __restore_plan_end__ = .;
  } >SDRAM
  ASSERT(SIZEOF(.restore_plan_sdram) <= 48K,
         "Restore plan exceeds its 48 KiB SDRAM contract")
  ASSERT((__restore_plan_start__ & 31) == 0,
         "Restore plan is not cache-line aligned")

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
    *(.sdram*)
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
    *(.sdram_recorder)
    *(.sdram_recorder*)
    . = ALIGN(32);
  } >SDRAM

  .sdram_audio_cold (NOLOAD) :
  {
    . = ALIGN(32);
    *(.sdram_audio_cold)
    *(.sdram_audio_cold*)
    . = ALIGN(32);
  } >SDRAM
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
  } >DTCMRAM

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
