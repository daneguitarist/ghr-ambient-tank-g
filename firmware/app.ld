/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GerbilHamster Records (memory map after Felucca by Leo Kuroshita, Hügelton Instruments)
 * TANK-G FIRST LIGHT memory map. Code + constants run in place from flash (XIP, app.bin offset 0 = 0x02000120).
 * .data/.bss at 0x01C40000 (64 KiB); stacks 0x01C74100..0x01C7C000 (as Felucca/MOTES on the same chip);
 * 0x01C7FD50.. is the bootloader's (boot info, mailbox, vectors). Big buffers sit at fixed addresses (main.c). */
ENTRY(_start)
MEMORY
{
    XIP (rx)  : ORIGIN = 0x02000120, LENGTH = 0x70000
    RAM (rwx) : ORIGIN = 0x01C40000, LENGTH = 0x10000
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    _ustack_top = 0x01C7A000;
    _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
