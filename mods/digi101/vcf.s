| DH-101: the 101 filter's per-sample loop on the ColdFire (sh101.c's sh101_vcf_loop is the reference;
| tests/check_vcf.py runs both and compares). Every coefficient and the four stage states stay in
| registers, and the zero-delay solve is one EMAC accumulator chain.
|
| void sh101_vcf_loop(int16_t *io, int n, const int32_t *c, int32_t *s)
|   c = {e0, e1, e2, e3, e4, G, comp}: e Q28, G Q31, comp Q28;  s = the four stage states, Q26
|   per sample:  x = io << 11;  F = limit((e0 x + e1 s1 + e2 s2 + e3 s3 + e4 s4) << 3);  u = x - F
|                four stages: w = G (in - s), y = w + s, s = y + w;  out = sat16((comp y4) >> 8)
| The EMAC is set up by the caller (fractional, acc0 clear: emac_enter in sh101.c).
|
| Registers: d0 x / the signal, d1 d2 temporaries, d3..d6 s1..s4, d7 e0, a0 io, a1..a4 e1..e4, a5 G,
| a6 comp; the end of io on the stack.

        .equ    VCF_T, 3 << 23                  | the soft limit's knee (sh101.c VCF_T)
        .equ    VCF_Q, 1365                     | its bend (sh101.c VCF_Q)

        .text
        .globl  sh101_vcf_loop
sh101_vcf_loop:
        lea     (-44, %sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        move.l  (56, %sp), %a0                  | c
        movem.l (%a0), %d7/%a1-%a6
        move.l  (60, %sp), %a0                  | s
        movem.l (%a0), %d3-%d6
        move.l  (52, %sp), %d0                  | n
        ble.w   .Lout
        move.l  (48, %sp), %a0                  | io
        add.l   %d0, %d0
        add.l   %a0, %d0
        move.l  %d0, -(%sp)                     | the end of io

.Lloop:
        mvs.w   (%a0), %d0
        asl.l   #8, %d0
        asl.l   #3, %d0                         | x, Q26
        mac.l   %d7, %d0, %acc0
        mac.l   %a1, %d3, %acc0
        mac.l   %a2, %d4, %acc0
        mac.l   %a3, %d5, %acc0
        mac.l   %a4, %d6, %acc0
        movclr.l %acc0, %d1
        asl.l   #3, %d1                         | F = k y4, Q26
        move.l  %d1, %d2
        bpl.s   1f
        neg.l   %d2
1:      cmpi.l  #VCF_T, %d2
        bgt.w   .Llimit
.Lback:
        sub.l   %d1, %d0                        | u = x - F

        move.l  %d0, %d1                        | stage 1
        sub.l   %d3, %d1
        mac.l   %a5, %d1, %acc0
        movclr.l %acc0, %d2
        move.l  %d3, %d0
        add.l   %d2, %d0
        move.l  %d0, %d3
        add.l   %d2, %d3

        move.l  %d0, %d1                        | stage 2
        sub.l   %d4, %d1
        mac.l   %a5, %d1, %acc0
        movclr.l %acc0, %d2
        move.l  %d4, %d0
        add.l   %d2, %d0
        move.l  %d0, %d4
        add.l   %d2, %d4

        move.l  %d0, %d1                        | stage 3
        sub.l   %d5, %d1
        mac.l   %a5, %d1, %acc0
        movclr.l %acc0, %d2
        move.l  %d5, %d0
        add.l   %d2, %d0
        move.l  %d0, %d5
        add.l   %d2, %d5

        move.l  %d0, %d1                        | stage 4
        sub.l   %d6, %d1
        mac.l   %a5, %d1, %acc0
        movclr.l %acc0, %d2
        move.l  %d6, %d0
        add.l   %d2, %d0
        move.l  %d0, %d6
        add.l   %d2, %d6

        mac.l   %a6, %d0, %acc0                 | the output gain
        movclr.l %acc0, %d0
        asr.l   #8, %d0
        cmpi.l  #32767, %d0
        ble.s   2f
        move.l  #32767, %d0
        bra.s   3f
2:      cmpi.l  #-32768, %d0
        bge.s   3f
        move.l  #-32768, %d0
3:      move.w  %d0, (%a0)+
        cmpa.l  (%sp), %a0
        bcs.w   .Lloop

        addq.l  #4, %sp
        move.l  (60, %sp), %a0                  | the states back
        movem.l %d3-%d6, (%a0)
.Lout:
        movem.l (%sp), %d2-%d7/%a2-%a6
        lea     (44, %sp), %sp
        rts

| the soft limit past the knee (sh101.c vcf_limit): |F| = T + e - VCF_Q ((e >> 14)^2 >> 9), e = |F| - T,
| held at 2 T from e = 2 T. d1 = F, d2 = |F|; the sign picks the copy, so d1 is free for the arithmetic.
        .macro  BEND
        subi.l  #VCF_T, %d2                     | e
        cmpi.l  #2 * VCF_T, %d2
        blt.s   1f
        move.l  #2 * VCF_T, %d1
        bra.s   2f
1:      move.l  %d2, %d1
        asr.l   #8, %d1
        asr.l   #6, %d1                         | q = e >> 14 (<= 3072)
        muls.w  %d1, %d1
        asr.l   #8, %d1
        asr.l   #1, %d1                         | q^2 >> 9 (<= 18432)
        muls.w  #VCF_Q, %d1
        addi.l  #VCF_T, %d2
        sub.l   %d1, %d2
        move.l  %d2, %d1
2:
        .endm
.Llimit:
        tst.l   %d1
        bmi.s   .Lneg
        BEND
        bra.w   .Lback
.Lneg:
        BEND
        neg.l   %d1
        bra.w   .Lback
