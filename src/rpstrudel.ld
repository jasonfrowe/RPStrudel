/* RPStrudel's linker script: the rp6502 one (mos-platform/rp6502/lib/
 * link.ld) with an overlay region. The parser (.ovl_parse) and the queries
 * (.ovl_query) are linked to run at the same address, after the code that
 * always stays in RAM. Their images are placed outside the 64K, so they are
 * not in the program file; the build copies each out of the ELF into a ROM
 * file (parse.ovl, query.ovl), and overlay.c reads them in. */

/* Provide imaginary (zero page) registers. */
__rc0 = 0x00;
INCLUDE imag-regs.ld
ASSERT(__rc31 == 0x001f, "Inconsistent zero page map.")

MEMORY {
    zp : ORIGIN = __rc31 + 1, LENGTH = 0x100 - (__rc31 + 1)
    ram (rw) : ORIGIN = 0x0200, LENGTH = 0xfd00
    ovl (r) : ORIGIN = 0x10000, LENGTH = 0x10000
}

REGION_ALIAS("c_readonly", ram)
REGION_ALIAS("c_writeable", ram)

SECTIONS {
    INCLUDE zp.ld
    INCLUDE text.ld

    OVERLAY : NOCROSSREFS AT (ORIGIN(ovl)) {
        .ovl_parse { *(.ovl_parse .ovl_parse.*) }
        .ovl_query { *(.ovl_query .ovl_query.*) }
    } >ram
    __overlay_start = ADDR(.ovl_parse);
    __overlay_end = MAX(ADDR(.ovl_parse) + SIZEOF(.ovl_parse), ADDR(.ovl_query) + SIZEOF(.ovl_query));

    /* As rodata.ld, but loaded where it runs (after the overlays, whose
     * images are elsewhere, the linker would carry their offset on). */
    .rodata : { INCLUDE rodata-sections.ld } >c_readonly AT>c_readonly
    INCLUDE data.ld
    INCLUDE bss.ld
    INCLUDE noinit.ld
}

/* Set initial soft stack address to just above last memory address. (It grows down.) */
__stack = 0xFF00;

OUTPUT_FORMAT {
    SHORT(ORIGIN(ram))
    SHORT(_start)
    TRIM(ram)
}
