/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2017 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "audio.h"

#include <3ds.h>
#include <math.h>
#include <opus/opus_multistream.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WAVEBUF_SIZE 16
bool n3ds_audio_disabled = false;

static OpusMSDecoder *decoder;
static u8 *audioBuffer;
static int samplesPerFrame;
static int sampleRate;
static int channelCount;
static ndspWaveBuf audio_wave_buf[WAVEBUF_SIZE];
static int wave_buf_idx = 0;
static int gain_percent = 100;

void n3ds_audio_set_gain_percent(int percent) {
    gain_percent = percent < 100 ? 100 : percent > 400 ? 400 : percent;
}

// Scales samples in place with saturation. Done on the CPU after decoding
// rather than through the DSP mix, which cannot go above 1.0.
static void apply_gain(opus_int16 *samples, int count) {
    if (gain_percent == 100) {
        return;
    }
    const int gain = gain_percent;
    for (int i = 0; i < count; i++) {
        int v = ((int)samples[i] * gain) / 100;
        if (v > 32767) {
            v = 32767;
        } else if (v < -32768) {
            v = -32768;
        }
        samples[i] = (opus_int16)v;
    }
}

static int n3ds_renderer_init(int audioConfiguration,
                              POPUS_MULTISTREAM_CONFIGURATION opusConfig,
                              void *context, int arFlags) {
    int rc;
    decoder = opus_multistream_decoder_create(
        opusConfig->sampleRate, opusConfig->channelCount, opusConfig->streams,
        opusConfig->coupledStreams, opusConfig->mapping, &rc);
    if (rc != OPUS_OK || decoder == NULL) {
        fprintf(stderr, "opus_multistream_decoder_create failed: %d\n", rc);
        return -1;
    }

    sampleRate = opusConfig->sampleRate;
    channelCount = opusConfig->channelCount;
    samplesPerFrame = opusConfig->samplesPerFrame;
    int bytes_per_frame = sizeof(short) * channelCount * samplesPerFrame;

    if (ndspInit() != 0) {
        fprintf(stderr, "ndspInit() failed\n");
        return -1;
    }

    // Assign the file-scope buffer (a local here would shadow it and leak
    // the linear allocation on every stream)
    audioBuffer = (u8 *)linearAlloc(bytes_per_frame * WAVEBUF_SIZE);
    if (audioBuffer == NULL)
        return -1;
    memset(audioBuffer, 0, bytes_per_frame * WAVEBUF_SIZE);

    ndspChnWaveBufClear(0);
    ndspChnReset(0);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, sampleRate);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(0, mix);

    memset(audio_wave_buf, 0, sizeof(audio_wave_buf));
    for (int i = 0; i < WAVEBUF_SIZE; i++) {
        audio_wave_buf[i].data_vaddr = &audioBuffer[i * bytes_per_frame];
        audio_wave_buf[i].status = NDSP_WBUF_DONE;
    }

    ndspChnSetPaused(0, false);

    return 0;
}

static void n3ds_renderer_cleanup() {
    if (decoder != NULL) {
        opus_multistream_decoder_destroy(decoder);
        decoder = NULL;
    }

    ndspChnWaveBufClear(0);
    ndspExit();
    if (audioBuffer != NULL) {
        linearFree(audioBuffer);
        audioBuffer = NULL;
    }
    printf("Audio renderer shutdown successfully\n");
}

static void n3ds_renderer_decode_and_play_sample(char *data, int length) {
    if (n3ds_audio_disabled ||
        (audio_wave_buf[wave_buf_idx].status != NDSP_WBUF_DONE)) {
        return;
    }

    int decodeLen = opus_multistream_decode(
        decoder, (const unsigned char *)data, length,
        (opus_int16 *)audio_wave_buf[wave_buf_idx].data_vaddr, samplesPerFrame,
        0);
    if (decodeLen < 0) {
        fprintf(stderr, "Opus error from decode: %d\n", decodeLen);
        return;
    }
    apply_gain((opus_int16 *)audio_wave_buf[wave_buf_idx].data_vaddr,
               decodeLen * channelCount);
    DSP_FlushDataCache(audio_wave_buf[wave_buf_idx].data_vaddr,
                       decodeLen * channelCount * sizeof(short));
    audio_wave_buf[wave_buf_idx].nsamples = decodeLen;
    ndspChnWaveBufAdd(0, &audio_wave_buf[wave_buf_idx]);

    wave_buf_idx = (wave_buf_idx + 1) % WAVEBUF_SIZE;
}

extern AUDIO_RENDERER_CALLBACKS audio_callbacks_mock = {
    .init = NULL,
    .cleanup = NULL,
    .decodeAndPlaySample = NULL,
    .capabilities = 0,
};

AUDIO_RENDERER_CALLBACKS audio_callbacks_n3ds = {
    .init = n3ds_renderer_init,
    .cleanup = n3ds_renderer_cleanup,
    .decodeAndPlaySample = n3ds_renderer_decode_and_play_sample,
    .capabilities = CAPABILITY_DIRECT_SUBMIT | CAPABILITY_SLOW_OPUS_DECODER,
};
