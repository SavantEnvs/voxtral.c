/* WAV parser + mel spectrogram: vox_parse_wav_buffer() then vox_mel_spectrogram(),
 * the path every WAV file / piped WAV on stdin takes before the model runs. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "voxtral_audio.h"

/* 30 s of 16 kHz audio. The CLI feeds the encoder in chunks of this order; longer
 * buffers only make the (linear) mel pass slower, they reach no new code. */
#define MAX_MEL_SAMPLES (16000 * 30)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* exact-size heap copy so ASan sees any read past the end of the file */
    uint8_t *buf = (uint8_t *)malloc(size ? size : 1);
    if (!buf) return 0;
    if (size) memcpy(buf, data, size);
    int n = 0;
    float *s = vox_parse_wav_buffer(buf, size, &n);
    if (s && n > 0 && n <= MAX_MEL_SAMPLES) {
        int frames = 0;
        float *m = vox_mel_spectrogram(s, n, &frames);
        free(m);
    }
    free(s);
    free(buf);
    return 0;
}
