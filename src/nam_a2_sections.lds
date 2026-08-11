SECTIONS
{
    .sram_d2_bss (NOLOAD) :
    {
        . = ALIGN(32);
        *(.sram_d2_bss)
        *(.sram_d2_bss*)
        . = ALIGN(32);
    } > RAM_D2
} INSERT BEFORE .heap;
