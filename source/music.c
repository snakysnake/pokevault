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
    STREAM_SAMPLES = 32768
};

static const char *const tracks[TRACK_COUNT] = {
    "nitro:/music/lake.wav",
    "nitro:/music/eterna_forest.wav"
};

static FILE *audio;
static uint32_t data_left;
static int track = TRACK_COUNT - 1;
static int started;

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
            return length;
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
    return length;
}

void music_init(void)
{
    mm_ds_system sys;
    mm_stream stream;

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
