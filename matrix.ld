ENTRY(_start)

SECTIONS
{
    /* Setzt die Basisadresse deiner Matrix-Anwendung im Speicher */
    . = 0x400000;

    .text :
    {
        *(.text)
    }

    .data :   { *(.data) }
    .rodata : { *(.rodata) }
    .bss :    { *(.bss) }

    /* Entfernt den GNU-Metadaten-Müll, um Überlappungen zu verhindern */
    /DISCARD/ :
    {
        *(.note.gnu.property)
        *(.comment)
        *(.eh_frame)
    }
}
