MEMORY
{
  FLASH (rx): ORIGIN = 0x00000000, LENGTH = 32K
  SRAM (rwx): ORIGIN = 0x10000000, LENGTH = 8K
}

SECTIONS
{
  .text : {
    KEEP(*(.isr_vector))
    *(.text .text.*)
  } > FLASH

  .rodata : ALIGN(4) {
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } >FLASH
}
