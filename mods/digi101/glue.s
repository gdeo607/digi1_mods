| DH-101: two more saved words a sound (parameter slots 46 and 47).
|
| The firmware's sound record (Sound::save 0x4007a5a0 / load 0x4007a236, the same on OS 1.53 and 1.54;
| digi1_mods docs/TECHNICAL_NOTES.md) has room for 48 parameter words at +0x1c, and the save clears all 48
| first, but its loops copy only 46 of them, through a table (runtime slot i <-> storage word tbl[i]):
| storage words 46 and 47 are always zero, and runtime slots 46..52 (+0x14 + 2 slot) are RAM-only. These
| two hooks, right after each loop, copy slot 46 <-> word 46 and slot 47 <-> word 47 as well, so those two
| slots are saved and loaded like any other parameter: in the kits, the project, the sound pool and the
| unit's power-off mirror (Sound::updateMirror uses the same save). An old record loads them as 0.
| Each hook then does the stock instructions it replaced, and returns after them.

        .text
| ---- the save: at 0x4007a61a, after the loop. a2 = the storage record, a3 = the runtime sound, a1 = the
| save table. Replaced: mvsb (42,a2),d0; move.l (0,a1,d0.l*4),d0  (LFO1 DEST to its storage form)
        .globl  digi101_save_hook
digi101_save_hook:
        move.w  (0x14 + 2 * 46, %a3), (0x1c + 2 * 46, %a2)
        move.w  (0x14 + 2 * 47, %a3), (0x1c + 2 * 47, %a2)
        mvs.b   (42, %a2), %d0
        move.l  (0, %a1, %d0.l * 4), %d0
        rts

| ---- the load: at 0x4007a2cc, after the loop. a2 = the runtime sound (slots cleared by the memset before
| the loop), a3 = the storage record, a1 = the load table. Replaced: mvsb (28,a2),d0; moveq #5,d2;
| move.l (0,a1,d0.l*4),d0  (LFO1 DEST back to a slot)
        .globl  digi101_load_hook
digi101_load_hook:
        move.w  (0x1c + 2 * 46, %a3), (0x14 + 2 * 46, %a2)
        move.w  (0x1c + 2 * 47, %a3), (0x14 + 2 * 47, %a2)
        mvs.b   (28, %a2), %d0
        moveq   #5, %d2
        bra.s   dest_from_store

| OS 1.53 (whose LFO DEST hooks are not made): the slots only, then the 6 bytes replaced there
        .globl  digi101_load_hook153
digi101_load_hook153:
        move.w  (0x1c + 2 * 46, %a3), (0x14 + 2 * 46, %a2)
        move.w  (0x1c + 2 * 47, %a3), (0x14 + 2 * 47, %a2)
        mvs.b   (28, %a2), %d0
        moveq   #5, %d2
        rts

| ---- the load of LFO2's DEST: at 0x4007a2dc. Replaced: mvsb (44,a2),d0; move.l (0,a1,d0.l*4),d0
        .globl  digi101_load2_hook
digi101_load2_hook:
        mvs.b   (44, %a2), %d0

| An LFO's DEST from its storage form, through the firmware's table (a1). DH-101 0.20 saved its own
| destinations (page 2 as slots 46..52) as 0x60..0x66, which no stock form takes (0..45, 51 for none):
| from 0.21 FREQ (0x60) is slot 18 and RESO (0x61) slot 23, as the LFO page now lists them; the other five
| are gone and load as none.
dest_from_store:
        cmpi.l  #0x60, %d0
        blt.s   2f
        cmpi.l  #0x66, %d0
        bgt.s   2f
        cmpi.l  #0x61, %d0
        bgt.s   1f
        beq.s   3f
        moveq   #18, %d0
        rts
3:      moveq   #23, %d0
        rts
1:      moveq   #51, %d0
2:      move.l  (0, %a1, %d0.l * 4), %d0
        rts

| ---- the LFO page's destination list (OS 1.54). The firmware lists a track's LFO destinations slot by
| slot 0..52 (0x40011348), asking each slot's parameter id (0x40078f44(slot, machine)). On a DH-101 track,
| asked from that list alone (the call's return address 0x400113be), slots 17..24 answer their ids in the
| order the list shows them (d101_slot2id in digi101.c): FREQ and RESO together, last.

| 0x40078f44 at its entry. Replaced: move.l d2,-(sp); moveq #52,d1; movea.l (8,sp),a0
        .globl  digi101_slot2id
digi101_slot2id:
        move.l  (12, %sp), -(%sp)               | the machine
        move.l  (12, %sp), -(%sp)               | the slot
        move.l  (12, %sp), -(%sp)               | who asks (the caller's return address)
        jsr     d101_slot2id
        lea     (12, %sp), %sp
        tst.l   %d0
        bmi.s   1f
        addq.l  #4, %sp                         | ours: straight back to the caller with the id
        rts
1:      movea.l (%sp)+, %a1                     | not ours: the stock instructions, and on
        move.l  %d2, -(%sp)
        moveq   #52, %d1
        movea.l (8, %sp), %a0
        jmp     (%a1)

| ---- the track's name in the messages (TRK + a trig, and the two others that print the same way): the
| firmware prints "%s: %.16s", the machine's short name and the sample's name (or OFF). OS 1.54 only.
| The three call sites of the short-name lookup (0x4007912c, core's for added machines) come here; the
| sample's text is in d5 (the first two sites) or d3 (the third), pushed right after the call. C
| (digi101_popup_name) gives the name to print and may point the sample's text elsewhere; the format is
| our "%s%.16s" (digi101_popup_fmt), the ": " now part of the name.
        .globl  digi101_name_d5, digi101_name_d3
digi101_name_d5:
        move.l  %d5, -(%sp)                     | the sample's text, which the C may replace
        pea     (%sp)
        move.l  (12, %sp), -(%sp)               | the machine
        jsr     digi101_popup_name
        addq.l  #8, %sp
        move.l  (%sp)+, %d5
        rts
digi101_name_d3:
        move.l  %d3, -(%sp)
        pea     (%sp)
        move.l  (12, %sp), -(%sp)
        jsr     digi101_popup_name
        addq.l  #8, %sp
        move.l  (%sp)+, %d3
        rts
