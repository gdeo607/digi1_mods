/* Desktop harness: render the DH-101 engine to a WAV, the way the unit calls it (48 kHz, blocks
 * of 32 samples, the knobs constant over a block).
 *
 * Build: gcc -O2 -o tools/digi101_render tools/digi101_render.c mods/digi101/sh101.c -lm
 *
 *   digi101_render out.wav seconds "B C E F G H D" seq
 *     seq: notes as midi:seconds[,midi:seconds...] (each note retriggers, as a trig does)
 *   optional env SWEEP="k from to": knob k (0..6) moves linearly over the whole render
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../mods/digi101/sh101.h"

static void wr32(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void wr16(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); }

int main(int argc, char **argv)
{
    int i, k, total, pos = 0, sweep_k = -1, delay = getenv("NOTE_DELAY") ? atoi(getenv("NOTE_DELAY")) : 0;
    uint32_t prev_inc = 0;
    double secs, sweep_a = 0, sweep_b = 0;
    uint8_t p[7];
    int16_t *buf;
    struct sh101_voice v;
    char *seq, *tok;
    (void)k;
    FILE *f;
    if (argc < 5) {
        fprintf(stderr, "usage: render out.wav seconds \"B C E F G H D\" midi:sec[,midi:sec]\n");
        return 1;
    }
    secs = atof(argv[2]);
    if (sscanf(argv[3], "%hhu %hhu %hhu %hhu %hhu %hhu %hhu", &p[0], &p[1], &p[2], &p[3], &p[4], &p[5], &p[6]) != 7)
        return 1;
    if (getenv("SWEEP"))
        sscanf(getenv("SWEEP"), "%d %lf %lf", &sweep_k, &sweep_a, &sweep_b);
    total = (int)(secs * 48000);
    buf = calloc(total, 2);
    sh101_init(&v);
    v.port = getenv("PORT") ? (uint8_t)atoi(getenv("PORT")) : 0;     /* the track's PORT (SRC page 2) */
#define ENVSET(f, name) if (getenv(name)) v.f = (uint8_t)atoi(getenv(name))
    ENVSET(f_cut, "CUT"); ENVSET(f_res, "RES"); ENVSET(f_env, "FENV"); ENVSET(f_mod, "MOD");
    ENVSET(f_kyb, "KYB"); ENVSET(f_vib, "VIB"); ENVSET(f_rate, "RATE");
    {
        extern int32_t sh101_k_override;
        if (getenv("KRAW"))
            sh101_k_override = (int32_t)(atof(getenv("KRAW")) * 268435456.0);
    }
    if (getenv("ENVQ"))
        v.envq = (uint16_t)atoi(getenv("ENVQ"));                     /* a constant AMP envelope */
    /* the notes first, so that NOTE_DELAY < 0 (the new pitch |d| blocks before its trig) can look ahead */
    {
        int nn = 0, ni, nnote[256];
        double nlen[256];
        uint32_t ninc[256];
        seq = strdup(argv[4]);
        for (tok = strtok(seq, ","); tok && nn < 256; tok = strtok(NULL, ",")) {
            if (sscanf(tok, "%d:%lf", &nnote[nn], &nlen[nn]) != 2)
                return 1;
            ninc[nn] = (uint32_t)(440.0 * pow(2.0, (nnote[nn] - 69) / 12.0) / 48000.0 * 4294967296.0);
            nn++;
        }
        for (ni = 0; ni < nn && pos < total; ni++) {
            uint32_t inc = ninc[ni];
            int len = (int)(nlen[ni] * 48000), nb = (len + 31) / 32;
            sh101_trig(&v);
            for (i = 0; i < len && pos < total; i += 32) {
                int n = len - i < 32 ? len - i : 32, blk = i / 32;
                uint32_t use = inc;
                if (pos + n > total)
                    n = total - pos;
                if (sweep_k >= 0) {
                    double x = sweep_a + (sweep_b - sweep_a) * pos / total;
                    p[sweep_k] = (uint8_t)(x + 0.5);
                }
                if (delay > 0 && blk < delay && prev_inc)
                    use = prev_inc;                 /* the pitch comes late */
                if (delay < 0 && ni + 1 < nn && blk >= nb + delay)
                    use = ninc[ni + 1];             /* the next note's pitch comes early */
                sh101_render(&v, p, use, buf + pos, n);
                pos += n;
            }
            prev_inc = inc;
        }
    }
    f = fopen(argv[1], "wb");
    fwrite("RIFF", 1, 4, f); wr32(f, 36 + total * 2); fwrite("WAVEfmt ", 1, 8, f);
    wr32(f, 16); wr16(f, 1); wr16(f, 1); wr32(f, 48000); wr32(f, 96000); wr16(f, 2); wr16(f, 16);
    fwrite("data", 1, 4, f); wr32(f, total * 2);
    for (k = 0; k < total; k++)
        wr16(f, (uint16_t)buf[k]);
    fclose(f);
    return 0;
}
