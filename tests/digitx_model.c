/* Digi TX's drums on a PC (mods/digitx/tx.c as it is): each drum's pitch over time (zero crossings), its
 * level to -30 dB, against hugz202's measurements of the TX-8 / TX-9 machines; writes a WAV of the kit.
 *   cc -O2 -I mods/digitx -o /tmp/digitx_model tests/digitx_model.c mods/digitx/tx.c && /tmp/digitx_model out.wav */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tx.h"

#define INC_C4 23409859u
static int16_t buf[48000 * 3];
static int fails;

static int render(int drum, const uint8_t *p0, int samples)
{
    struct tx_voice v;
    uint8_t p[TX_PARAMS];
    int i;
    memcpy(p, p0, TX_PARAMS);
    p[TX_P_TYPE] = (uint8_t)(drum * 8 + 4);
    tx_init(&v);
    tx_trig(&v, p);
    for (i = 0; i < samples; i += 32)
        tx_render(&v, p, INC_C4, buf + i, 32);
    return samples;
}

/* the frequency around sample s, from rising zero crossings in a 20 ms window */
static double freq_at(int s, int w)
{
    int i, first = -1, last = -1, n = 0;
    for (i = s; i < s + w; i++)
        if (buf[i - 1] < 0 && buf[i] >= 0) {
            if (first < 0) first = i;
            last = i;
            n++;
        }
    return n > 1 ? 48000.0 * (n - 1) / (last - first) : 0;
}

static double rms(int s, int w)
{
    double a = 0;
    int i;
    for (i = s; i < s + w; i++)
        a += (double)buf[i] * buf[i];
    return sqrt(a / w);
}

/* ms to -30 dB below the peak RMS (5 ms windows) */
static double t30(int len)
{
    double pk = 0, r;
    int s, at = 0;
    for (s = 0; s + 240 < len; s += 240)
        if ((r = rms(s, 240)) > pk) pk = r, at = s;
    for (s = at; s + 240 < len; s += 240)
        if (rms(s, 240) < pk * 0.0316)
            return s / 48.0;
    return len / 48.0;
}

static void check(int ok, const char *what, double got, double want)
{
    printf("  %s %-40s got %8.1f  want %8.1f\n", ok ? "ok  " : "FAIL", what, got, want);
    fails += !ok;
}

static int near(double a, double b, double tol) { return fabs(a - b) <= tol * b; }

int main(int argc, char **argv)
{
    static const uint8_t def[TX_PARAMS] = {0, 64, 64, 64, 100, 40, 64};
    uint8_t p[TX_PARAMS];
    int d, len = 48000 * 2;
    FILE *f;
    printf("BD9\n");
    memcpy(p, def, sizeof p); p[TX_P_SNAP] = 0;
    render(TX_BD9, p, len);
    {   /* the sweep, cycle by cycle (the originals: 175 > 122 > 83 > 64 > 58 > 56 Hz) */
        int i, last = -1, k = 0;
        double fq[8] = {0};
        for (i = 1; i < 4800 && k < 8; i++)
            if (buf[i - 1] < 0 && buf[i] >= 0) {
                if (last >= 0)
                    fq[k++] = 48000.0 / (i - last);
                last = i;
            }
        printf("  cycles: %.0f %.0f %.0f %.0f %.0f %.0f Hz\n", fq[0], fq[1], fq[2], fq[3], fq[4], fq[5]);
        check(near(fq[0], 150, 0.3) && fq[0] > fq[1] && fq[1] > fq[2], "sweep falling from ~150..175", fq[0], 160);
    }
    check(near(freq_at(4800, 2400), 56, 0.04), "settled 56 Hz", freq_at(4800, 2400), 56);
    check(near(t30(len), 165, 0.15), "t30 at DEC 64 (~165 ms)", t30(len), 165);
    p[TX_P_DEC] = 0; render(TX_BD9, p, len);
    check(near(t30(len), 75, 0.25), "t30 at DEC 0 (75)", t30(len), 75);
    p[TX_P_DEC] = 127; render(TX_BD9, p, len);
    check(near(t30(len), 344, 0.15), "t30 at DEC 127 (344)", t30(len), 344);
    printf("BD8\n");
    memcpy(p, def, sizeof p); p[TX_P_SNAP] = 0; p[TX_P_CHAR] = 0; p[TX_P_TONE] = 0;
    for (d = 0; d <= 127; d += 127) {
        p[TX_P_PTC] = (uint8_t)d; render(TX_BD8, p, len);
        check(near(freq_at(2400, 4800), d ? 98 : 25, 0.06), d ? "PTC 127: 98 Hz" : "PTC 0: 25 Hz", freq_at(2400, 4800), d ? 98 : 25);
    }
    p[TX_P_PTC] = 64; render(TX_BD8, p, len);
    check(near(freq_at(2400, 4800), 49.7, 0.04), "PTC 64: 49.7 Hz", freq_at(2400, 4800), 49.7);
    p[TX_P_DEC] = 0; render(TX_BD8, p, len);
    check(near(t30(len), 80, 0.3), "t30 DEC 0 (80)", t30(len), 80);
    p[TX_P_DEC] = 127; render(TX_BD8, p, len);
    check(near(t30(len), 1170, 0.15), "t30 DEC 127 (1170)", t30(len), 1170);
    printf("toms 9 (PTC 64)\n");
    memcpy(p, def, sizeof p); p[TX_P_SNAP] = 0; p[TX_P_CHAR] = 0; p[TX_P_TONE] = 0;
    {
        static const double want[3] = {91.5, 112, 134.5};
        for (d = 0; d < 3; d++) {
            render(TX_LT9 + d, p, len);
            check(near(freq_at(2400, 4800), want[d], 0.04), "tom 9 pitch", freq_at(2400, 4800), want[d]);
        }
    }
    printf("toms 8 (PTC 64)\n");
    {
        static const double want[3] = {91, 141, 192};
        for (d = 0; d < 3; d++) {
            render(TX_LT8 + d, p, len);
            check(near(freq_at(2400, 4800), want[d], 0.04), "tom 8 pitch", freq_at(2400, 4800), want[d]);
        }
    }
    printf("SD9 body (no noise)\n");
    memcpy(p, def, sizeof p); p[TX_P_SNAP] = 0; p[TX_P_CHAR] = 0;
    render(TX_SD9, p, len);
    check(near(freq_at(2400, 4800), 187.5, 0.04), "SD9 f1 at PTC 64", freq_at(2400, 4800), 187.5);
    /* the kit, one hit each, every 0.5 s, for listening */
    if (argc > 1 && (f = fopen(argv[1], "wb"))) {
        static int16_t all[48000 * 6];
        int n = 0, i;
        uint32_t sz;
        for (d = 0; d < TX_DRUMS && n + 24000 <= 48000 * 6; d++) {
            render(d, def, 24000);
            memcpy(all + n, buf, 24000 * 2);
            n += 24000;
        }
        fwrite("RIFF", 1, 4, f); sz = 36 + n * 2; fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
        sz = 16; fwrite(&sz, 4, 1, f);
        { uint16_t a[2] = {1, 1}; fwrite(a, 2, 2, f); }
        sz = 48000; fwrite(&sz, 4, 1, f); sz = 96000; fwrite(&sz, 4, 1, f);
        { uint16_t a[2] = {2, 16}; fwrite(a, 2, 2, f); }
        fwrite("data", 1, 4, f); sz = n * 2; fwrite(&sz, 4, 1, f); fwrite(all, 2, n, f); fclose(f);
        for (i = 0, sz = 0; i < n; i++) if (abs(all[i]) > (int)sz) sz = abs(all[i]);
        printf("wrote %s (%d drums, peak %u)\n", argv[1], d, sz);
    }
    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails != 0;
}
