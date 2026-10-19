| Digi EQ: its row in the GLOBAL FX/MIX list. The render (ev_render_master: eq_dsp.s's digieq_run) and the
| master page (core_mpages: eq.c's digieq_page) are core 4.1's. Firmware addresses are OS 1.53's.

| ---- firmware addresses that moved in OS 1.54 (tools/port_os.py, tools/os154.json) ----
        .ifdef  OS154
        .equ    .LF_4017ac20, 0x4017af20
        .else
        .equ    .LF_4017ac20, 0x4017ac20
        .endif
| ---- end of the moved addresses ----


        .text

| ---------------- SETTINGS > GLOBAL FX/MIX: the MASTER EQ row ---------------------------------------
| core 4.1's ev_fxmix: f(menu), after that menu's last row. The row is core_additem's (callbacks in eq.c).
        .balign 2
        .globl  digieq_fxmix
digieq_fxmix:
        pea     row_eq
        move.l  8(%sp), -(%sp)                  | the menu
        jsr     core_additem
        addq.l  #8, %sp
        rts

| label: builds the std::string in a0
row_label:
        link    %a6, #-4
        move.l  %d2, -(%sp)
        pea     -1(%a6)
        pea     str_row
        move.l  %a0, %d2
        move.l  %a0, -(%sp)
        jsr     .LF_4017ac20                      | std::string(this, const char*, alloc&)
        lea     12(%sp), %sp
        move.l  %d2, %d0
        move.l  -8(%a6), %d2
        unlk    %a6
        rts

        .balign 4
row_eq:
        .long   row_label, digieq_gselect, digieq_gdraw, digieq_gchange
str_row:
        .asciz  "MASTER EQ"
        .balign 2
