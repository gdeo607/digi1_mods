| Digi Poly: the POLY machine itself, for core 2.1's machine table (elekloader docs/ADAPTING.md, "SRC
| machines"). Core lists it in FUNC+SRC with its name and icon, keeps it in a loaded kit, and plays it as
| ONESHOT with ONESHOT's eight parameters (defaults, MIDI CC, Randomize, the sample browser); its SRC page
| is ONESHOT's, drawn by machine-pages (poly_page). Firmware addresses are OS 1.53's.

| ---- firmware addresses that moved in OS 1.54 (tools/port_os.py, tools/os154.json) ----
        .ifdef  OS154
        .equ    .LF_401b73b4, 0x401b7734
        .equ    .LF_4197ced8, 0x4197ded8
        .else
        .equ    .LF_401b73b4, 0x401b73b4
        .equ    .LF_4197ced8, 0x4197ced8
        .endif
| ---- end of the moved addresses ----


        .equ    POLY_ID, 6              | kits store it: fixed for good (resource machine:6)
        .equ    BMP_VT, .LF_401b73b4      | the firmware's Bitmap vtable
        .equ    LAY_ONESHOT, .LF_4197ced8 | the SRC page layout of machine 0 (0x400657cc: 0x4197ced8 + 44 x m)

        .text
        .balign 4
        .globl  digipoly_machine
digipoly_machine:                       | id, name, short name, icon, params, render
        .long   POLY_ID, poly_str, poly_str, poly_icon, 0, 0
        .long   0x55493330, poly_page       | core 3.0's tail: its page (machine-pages)
poly_str:
        .asciz  "POLY"
        .balign 4

| 11 x 7, one word a column, rows in bits 25 (top) .. 31: a chord on a piano roll, three notes stacked
poly_icon:
        .long   BMP_VT, 11, 7, 1, poly_px, poly_mask, 0
poly_px:
        .long   0x00000000, 0x40000000, 0x40000000, 0x50000000, 0x50000000, 0x54000000
        .long   0x14000000, 0x14000000, 0x04000000, 0x04000000, 0x00000000
poly_mask:
        .rept   11
        .long   0xfe000000
        .endr

| POLY's SRC and LFO pages, drawn by machine-pages (core 3.0; elekloader's digitakt-mk1/core3.h struct cm_ui):
| ONESHOT's page as it is (page_from 0, every knob the stock one).
        .balign 4
poly_page:
        .long   cm_ui_v3                    | +0 abi
        .byte   0, 0                        | +4 page_from: ONESHOT; flags
        .short  0
        .long   0, 0                        | +8 group, +12 on_switch: the stock ones, none
        .rept   8                           | +16 knobs A..H: the stock ones (40 bytes each)
        .long   0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        .endr
