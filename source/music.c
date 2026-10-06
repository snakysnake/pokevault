#include "music.h"

#include <maxmod9.h>
#include <nds.h>
#include <stdio.h>
#include <string.h>

/* 16-bit mono 22050 Hz PCM. Maxmod plays the stream; this file only feeds it.
   Manual updates stay on the main thread so a NitroFS read never overlaps a save read. */

enum {
    TRACK_COUNT = 2,
    RATE = 22050,
    STREAM_SAMPLES = 32768,
    /* About 30 ms. Long enough to read as a tick on the DS speaker,
       short enough that repeats while scrolling stay separate. */
    CLICK_LEN = 640
};

static const char *const tracks[TRACK_COUNT] = {
    "nitro:/music/lake.wav",
    "nitro:/music/eterna_forest.wav"
};

static FILE *audio;
static uint32_t data_left;
static int track = TRACK_COUNT - 1;
static int started;
static int16_t click_pcm[CLICK_LEN];
static int click_pos = CLICK_LEN;

/* Quarter-wave sine, 0..32767. Index 64 is sin(pi/2). */
static const int16_t sin_q[65] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602,
    6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767
};

static int32_t quarter(uint32_t frac)
{
    uint32_t idx = frac >> 8;
    uint32_t sub = frac & 255;
    int32_t a = sin_q[idx];
    int32_t b = sin_q[idx + 1];
    return a + (((b - a) * (int32_t)sub) >> 8);
}

/* phase 0..65535 covers one cycle. */
static int32_t sine(uint32_t phase)
{
    uint32_t p = phase & 65535u;
    uint32_t quad = p >> 14;
    uint32_t frac = p & 16383u;
    int32_t y = (quad & 1u) ? quarter(16383u - frac) : quarter(frac);
    return (quad & 2u) ? -y : y;
}

/* A dry tick: a short 1.5 kHz body plus a low-passed noise attack,
   then a steep decay. Close to the PC box cursor. */
static void build_click(void)
{
    uint32_t noise = 0xC0FFEEu;
    uint32_t phase = 0;
    uint32_t phase2 = 0;
    int32_t low = 0;
    int32_t env = 22000;
    const uint32_t step = (1500u * 65536u) / (uint32_t)RATE;
    int i;

    for (i = 0; i < CLICK_LEN; i++) {
        int32_t n;
        int32_t s;
        noise = noise * 1664525u + 1013904223u;
        n = (int32_t)((noise >> 16) & 0xFFFFu) - 32768;
        low += (n - low) >> 2;
        s = (sine(phase) * 5 + sine(phase2) * 2 + low) / 8;
        phase += step;
        phase2 += step * 2u;
        if (i < 16)
            s = (s * i) / 16;
        s = (s * env) >> 15;
        click_pcm[i] = (int16_t)s;
        /* ~0.989 per sample: a short tok, still gone before the next repeat. */
        env = (env * 32407) >> 15;
    }
    click_pos = CLICK_LEN;
}

static void mix_click(int16_t *out, mm_word length)
{
    mm_word i;
    for (i = 0; i < length && click_pos < CLICK_LEN; i++, click_pos++) {
        int32_t s = (int32_t)out[i] + click_pcm[click_pos];
        if (s > 32767)
            s = 32767;
        if (s < -32768)
            s = -32768;
        out[i] = (int16_t)s;
    }
}

static uint32_t ru32(const unsigned char *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static void close_track(void)
{
    if (audio) {
        fclose(audio);
        audio = NULL;
    }
    data_left = 0;
}

static int open_track(int which)
{
    unsigned char hdr[12];
    unsigned char fmt[16];
    int saw_fmt = 0;

    close_track();
    audio = fopen(tracks[which], "rb");
    if (!audio)
        return 0;
    if (fread(hdr, 1, 12, audio) != 12 || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        close_track();
        return 0;
    }

    for (;;) {
        unsigned char chunk[8];
        uint32_t size;
        uint32_t skip;

        if (fread(chunk, 1, 8, audio) != 8)
            break;
        size = ru32(chunk + 4);
        skip = (size + 1u) & ~1u;
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint16_t tag;
            uint16_t channels;
            uint16_t bits;
            uint32_t rate;
            size_t take = size < sizeof fmt ? size : sizeof fmt;

            if (fread(fmt, 1, take, audio) != take)
                break;
            if (size > take && fseek(audio, (long)(skip - take), SEEK_CUR) != 0)
                break;
            if (take < 16)
                continue;
            tag = (uint16_t)fmt[0] | ((uint16_t)fmt[1] << 8);
            channels = (uint16_t)fmt[2] | ((uint16_t)fmt[3] << 8);
            rate = ru32(fmt + 4);
            bits = (uint16_t)fmt[14] | ((uint16_t)fmt[15] << 8);
            saw_fmt = tag == 1 && channels == 1 && bits == 16 && rate == RATE;
        } else if (memcmp(chunk, "data", 4) == 0) {
            if (!saw_fmt || size < 2) {
                close_track();
                return 0;
            }
            data_left = size & ~1u;
            return 1;
        } else if (fseek(audio, (long)skip, SEEK_CUR) != 0) {
            break;
        }
    }

    close_track();
    return 0;
}

static int next_track(void)
{
    int tries;

    for (tries = 0; tries < TRACK_COUNT; tries++) {
        track = (track + 1) % TRACK_COUNT;
        if (open_track(track))
            return 1;
    }
    return 0;
}

static mm_word fill(mm_word length, mm_addr dest, mm_stream_formats format)
{
    int16_t *out = dest;
    mm_word filled = 0;

    (void)format;
    while (filled < length) {
        size_t bytes;
        size_t got;

        if (data_left < 2 && !next_track()) {
            memset(out + filled, 0, (length - filled) * sizeof(int16_t));
            break;
        }
        bytes = (size_t)(length - filled) * 2;
        if (bytes > data_left)
            bytes = data_left;
        got = fread(out + filled, 1, bytes, audio);
        got &= ~1u;
        if (got == 0) {
            data_left = 0;
            continue;
        }
        data_left -= (uint32_t)got;
        filled += got / 2;
        if (got < bytes)
            data_left = 0;
    }
    mix_click(out, length);
    return length;
}

void music_init(void)
{
    mm_ds_system sys;
    mm_stream stream;

    build_click();
    memset(&sys, 0, sizeof sys);
    mmInit(&sys);

    memset(&stream, 0, sizeof stream);
    stream.sampling_rate = RATE;
    stream.buffer_length = STREAM_SAMPLES;
    stream.callback = fill;
    stream.format = MM_STREAM_16BIT_MONO;
    stream.manual = true;
    mmStreamOpen(&stream);
    started = 1;
}

void music_pump(void)
{
    if (started)
        mmStreamUpdate();
}

void music_click(void)
{
    if (started)
        click_pos = 0;
}
