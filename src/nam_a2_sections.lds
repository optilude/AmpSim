SECTIONS
{
    .sram_d2_bss (NOLOAD) :
    {
        . = ALIGN(32);
        PROVIDE(__sram_d2_bss_start__ = .);
        *(.sram_d2_bss)
        *(.sram_d2_bss*)
        . = ALIGN(32);
        PROVIDE(__sram_d2_bss_end__ = .);
    } > RAM_D2
} INSERT BEFORE .heap;
