/* Incremental (streaming) mel spectrogram: the path raw s16le PCM from --stdin and
 * vox_stream_feed() take. Input layout:
 *   byte 0      flags: bit0 -> left pad 40960 samples (as voxtral.c does), else
 *                      (byte 0 >> 2) * 16 samples (0..1008: odd, small pads),
 *                      bit1 -> right pad 0 or (byte 1 * 64) samples at finish
 *   byte 1      right-pad amount
 *   then records: [len byte][len * 2 bytes s16le PCM][discard byte]
 *     the PCM is fed with vox_mel_feed(); the discard byte k drops frames before
 *     vox_mel_frame_offset() + k (as the streaming decoder does with its cursor). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "voxtral_audio.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) return 0;
    int left_pad = (data[0] & 1) ? 40960 : (data[0] >> 2) * 16;
    int right_pad = (data[0] & 2) ? data[1] * 64 : 0;
    vox_mel_ctx_t *ctx = vox_mel_ctx_init(left_pad);
    if (!ctx) return 0;
    size_t pos = 2;
    float pcm[255];
    while (pos < size) {
        int len = data[pos++];
        int n = 0;
        while (n < len && pos + 1 < size) {
            int16_t v = (int16_t)(uint16_t)(data[pos] | (data[pos + 1] << 8));
            pcm[n++] = v / 32768.0f;
            pos += 2;
        }
        vox_mel_feed(ctx, pcm, n);
        int avail = 0;
        float *mel = vox_mel_data(ctx, &avail);
        if (mel && avail > 0) {
            volatile float sink = mel[0] + mel[(size_t)avail * 128 - 1];
            (void)sink;
        }
        if (pos < size) {
            int k = data[pos++];
            vox_mel_discard_before(ctx, vox_mel_frame_offset(ctx) + k);
        }
    }
    int total = vox_mel_finish(ctx, right_pad);
    int avail = 0;
    float *mel = vox_mel_data(ctx, &avail);
    if (mel && avail > 0 && total > 0) {
        volatile float sink = mel[(size_t)avail * 128 - 1];
        (void)sink;
    }
    vox_mel_free(ctx);
    return 0;
}
