ENTRY(_start)  /* Geändert von stage2_start auf _start */

SECTIONS
{
    . = 0x7E00;

    .text :
    {
        stage2.o(.text)
        *(.text)
    }

    .data : { *(.data) }
    .rodata : { *(.rodata) }
    .bss : { *(.bss) }

    /DISCARD/ :
    {
        *(.note.gnu.property)
        *(.comment)
        *(.eh_frame)
    }

    . = ALIGN(512);
}
