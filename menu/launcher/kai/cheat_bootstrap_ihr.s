!
! Cheat Codes for Pseudo Saturn Kai
! by cafe-alpha and Yabause Team.
! WWW: http://ppcenter.free.fr/pskai/
!
! See LICENSE file for details.
!

/*
 * Copyright (c) 2006-2012 Israel Jacquez <mrkotfw@gmail.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


.text
.align 2

!; Cheat codes interrupt handler
!; This code is executed when interrupt for cheat codes
!; is triggered, typically with interrupt #5.
!;
!; What this interrupt handler does :
!;  1. Retrieve location of cheat codes configuration memory
!;  2. Call cheat codes handler, whose may be either in-
!;      - in-RAM cheat codes handler, with variable execution location
!;      - ROM-based cheat codes handler, at fixed address in ROM
!;
!; This program runs from start of configuration memory, hence
;! execution location varies from a game to another.
!; -> This program is not directly executed, but copied in RAM, patched
!;    with opcode and cheat codes handler location, and executed from RAM.

.global _asm_cheat_bootstrap_ihr
.type _asm_cheat_bootstrap_ihr, @function
_asm_cheat_bootstrap_ihr:

    !; Save R4 to stack, which will be used in order to
    !; provide configuration memory location to cheat codes handler.
    !; Save also R0 to stack, which will be used in order
    !; to retrieve cheat codes handler location.
    sts.l  pr,@-r15
    mov.l  r0,@-r15
    mov.l  r1,@-r15
    mov.l  r2,@-r15
    mov.l  r3,@-r15
    mov.l  r4,@-r15

    !; Retrieve configuration memory location, whose address
    !; is stored after this code, plus offset 0x04.
    !; -> unsigned long cfg_location in ct_config_t structure.
    !;
    !; Typical locations :
    !;  - 0x060FF000 : default location.
    !;  - 0x06002800 : default alternate location.
    mov.l @(0x020, pc), r4

    !; Retrieve cheats handler location, whose address
    !; is stored after this code, plus offset 0x00.
    !; -> unsigned long ct_hdlr_location in ct_config_t structure.
    !;
    !; This can be located either in ROM (when ROM-based handler is
    !; used), or in RAM, and in that case it is located after cheat
    !; codes cache memory :
    !; -> ct_config_t (64 bytes, including this 1st stage handler)
    !;   -> cache (8 bytes * num_codes)
    !;     -> in-RAM handler
    mov.l @(0x01A, pc), r0

    !; Call cheat codes handler.
    jsr @r0
    nop

    !; Restore registers modified above.
    mov.l  @r15+,r4
    mov.l  @r15+,r3
    mov.l  @r15+,r2
    mov.l  @r15+,r1
    mov.l  @r15+,r0
    lds.l  @r15+,pr

    !; Registers are now in same state just before being
    !; interrupted, so it's the good moment to execute
    !; opcode patched by master code.
    !; (Opcode below is set to nop, and needs to be modified
    !; with appropriate one when setting up master code)
    nop
    nop

    !; Return back from interruption.
    rte
    nop
