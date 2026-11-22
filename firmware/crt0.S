/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GerbilHamster Records (after the entry of Felucca by Leo Kuroshita, Hügelton Instruments)
 * TANK-G FIRST LIGHT entry. The bootloader maps app.bin at XIP 0x02000120 and jumps to its first byte; it
 * expects "goto +4; rts" there (as the factory app and MOTES/Felucca have). Interrupts off, our stacks, C. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	rts
1:	cli
	sp = _ustack_top
	ssp = _sstack_top
	call tg_cstart
2:	goto 2b
