| SPDX-License-Identifier: GPL-2.0-or-later
| Tone+FX, DT1 port: a page change done the stock way (the titles are core 4.1's, core_mpages).
| Addresses are OS 1.53's (tools/port_os.py names the 1.54 ones).

| ---- firmware addresses that moved in OS 1.54 (tools/port_os.py, tools/os154.json) ----
        .ifdef  OS154
        .equ    .LF_40093a12, 0x40093a56
        .equ    .LF_400c02e4, 0x400c050c
        .equ    .LF_400c9812, 0x400c9a3a
        .equ    .LF_40179170, 0x40179470
        .else
        .equ    .LF_40093a12, 0x40093a12
        .equ    .LF_400c02e4, 0x400c02e4
        .equ    .LF_400c9812, 0x400c9812
        .equ    .LF_40179170, 0x40179170
        .endif
| ---- end of the moved addresses ----


        .section .run, "ax"

| ---- dt_page_show(view): what the stock page key does after it moves the page index ------------------
| (0x4003376c..0x400337b8): invalidate the view, reset its knob state (+148), show the title popup
| (vtable slot 19 -> the screen's popup 0x40093a12 with the view's +108), and tell the view (slot 20).
        .balign 2
        .globl  dt_page_show
dt_page_show:
        link    %a6, #-16
        move.l  %a2, -(%sp)
        move.l  8(%a6), %a2
        move.l  %a2, -(%sp)
        jsr     .LF_400c9812                      | invalidate
        pea     148(%a2)
        jsr     .LF_400c02e4
        addq.l  #8, %sp
        move.l  (%a2), %a1
        move.l  %a2, -(%sp)
        move.l  76(%a1), %a1                    | slot 19: the title, into -16(a6)
        lea     -16(%a6), %a0
        jsr     (%a1)
        pea     -16(%a6)
        move.l  108(%a2), -(%sp)
        jsr     .LF_40093a12                      | the title popup
        pea     -16(%a6)
        jsr     .LF_40179170                      | ~string
        lea     16(%sp), %sp
        move.l  (%a2), %a0
        move.l  %a2, -(%sp)
        move.l  80(%a0), %a0                    | slot 20: the page changed
        jsr     (%a0)
        addq.l  #4, %sp
        move.l  -20(%a6), %a2
        unlk    %a6
        rts
