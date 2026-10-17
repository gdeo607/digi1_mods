| tests/digi101_vcf.py: set the EMAC up as sh101.c's emac_enter does, call the loop, stop.
        .text
        .globl  _start
_start:
        move.l  #0x20, %d0
        move.l  %d0, %macsr
        moveq   #0, %d0
        move.l  %d0, %acc0
        move.l  %d0, %accext01
        move.l  (0x8000c), -(%sp)               | s
        move.l  (0x80008), -(%sp)               | c
        move.l  (0x80004), -(%sp)               | n
        move.l  (0x80000), -(%sp)               | io
        jsr     sh101_vcf_loop
        lea     (16, %sp), %sp
        .globl  _stop
_stop:
        nop
        nop
