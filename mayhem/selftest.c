/* mayhem/selftest.c — known-answer tests for the model-free voxtral.c APIs
 * (WAV parsing, mel spectrogram, streaming mel, safetensors reader, tokenizer).
 * Upstream's own runtest.sh needs the 8.9 GB model and ffmpeg, so this is the
 * functional oracle. Built with normal flags by build.sh, run by test.sh.
 * Prints "PASS <name>" / "FAIL <name>" per test. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "voxtral_audio.h"
#include "voxtral_safetensors.h"
#include "voxtral_tokenizer.h"

/* Golden values for samples/test_speech.wav (computed once from this revision). */
#define WAV_ABS_SUM   4740.429199
#define WAV_S1000     (1481.0 / 32768.0)
#define MEL_SUM       2658.591697
#define STREAM_FRAMES 364
#define STREAM_SUM    2655.052887

static int failures = 0;
static void report(const char *name, int ok) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}
static int near(double a, double b, double tol) { return fabs(a - b) <= tol * (1.0 + fabs(b)); }

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(sz);
    if (fread(b, 1, sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f); *n = sz; return b;
}
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static size_t mkwav(uint8_t *b, int ch, int rate, const int16_t *pcm, int n, uint32_t dsize) {
    memcpy(b, "RIFF", 4); put32(b + 4, 36 + n * 2); memcpy(b + 8, "WAVEfmt ", 8);
    put32(b + 16, 16); put16(b + 20, 1); put16(b + 22, ch); put32(b + 24, rate);
    put32(b + 28, rate * ch * 2); put16(b + 32, ch * 2); put16(b + 34, 16);
    memcpy(b + 36, "data", 4); put32(b + 40, dsize);
    for (int i = 0; i < n; i++) put16(b + 44 + 2 * i, (uint16_t)pcm[i]);
    return 44 + n * 2;
}

static void test_wav_and_mel(const char *wavpath) {
    size_t sz = 0; uint8_t *b = slurp(wavpath, &sz);
    int n = 0; float *s = b ? vox_parse_wav_buffer(b, sz, &n) : NULL;
    report("wav_sample_count", s && n == 58268);
    double sum = 0; for (int i = 0; s && i < n; i++) sum += fabs(s[i]);
    printf("# wav abs sum=%.6f s[1000]=%.8f\n", sum, s ? s[1000] : 0.0f);
    report("wav_sample_values", s && near(sum, WAV_ABS_SUM, 1e-6) && near(s[1000], WAV_S1000, 1e-6));
    int frames = 0; float *m = s ? vox_mel_spectrogram(s, n, &frames) : NULL;
    report("mel_frame_count", m && frames == 364);
    double ms = 0; float mx = -1e9f, mn = 1e9f;
    for (int i = 0; m && i < frames * 128; i++) { ms += m[i]; if (m[i] > mx) mx = m[i]; if (m[i] < mn) mn = m[i]; }
    report("mel_value_range", m && mx <= 1.5f + 1e-4f && mn >= 1.5f - 8.0f - 1e-3f && mx > mn);
    report("mel_checksum", m && near(ms, MEL_SUM, 1e-3));
    /* streaming mel over the same samples must match the batch mel frame-for-frame */
    vox_mel_ctx_t *ctx = vox_mel_ctx_init(0);
    for (int off = 0; s && ctx && off < n; off += 1234) vox_mel_feed(ctx, s + off, (n - off < 1234) ? n - off : 1234);
    int sf = ctx ? vox_mel_finish(ctx, 0) : 0;
    int av = 0; float *sm = ctx ? vox_mel_data(ctx, &av) : NULL;
    double ss = 0; for (int i = 0; sm && i < av * 128; i++) ss += sm[i];
    printf("# stream frames=%d batch=%d sum=%.6f batchsum=%.6f\n", sf, frames, ss, ms);
    report("mel_stream_checksum", sm && sf == STREAM_FRAMES && near(ss, STREAM_SUM, 1e-3));
    vox_mel_discard_before(ctx, 100);
    int av2 = 0; vox_mel_data(ctx, &av2);
    report("mel_stream_discard", ctx && vox_mel_frame_offset(ctx) == 100 && av2 == av - 100);
    vox_mel_free(ctx);
    printf("# mel sum=%.6f max=%f min=%f\n", ms, mx, mn);
    free(m); free(s); free(b);
}

static void test_wav_variants(void) {
    uint8_t b[256]; int16_t pcm[16] = {1000, -1000, 32767, -32768};
    /* ffmpeg piping writes 0xFFFFFFFF as data size: use the rest of the file */
    size_t sz = mkwav(b, 1, 16000, pcm, 4, 0xFFFFFFFFu);
    int n = 0; float *s = vox_parse_wav_buffer(b, sz, &n);
    report("wav_ffmpeg_pipe_size", s && n == 4 && s[0] == 1000 / 32768.0f && s[3] == -1.0f);
    free(s);
    /* stereo 8 kHz -> mono 16 kHz */
    for (int i = 0; i < 16; i++) pcm[i] = (int16_t)(i * 1000 - 8000);
    sz = mkwav(b, 2, 8000, pcm, 16, 32);
    s = vox_parse_wav_buffer(b, sz, &n);
    report("wav_stereo_resample", s && n == 16 && near(s[0], (-8000 + -7000) / 2.0 / 32768.0, 1e-6)
           && near(s[1], ((-7500.0 + -5500.0) / 2.0) / 32768.0, 1e-5));
    free(s);
    /* 8-bit PCM is rejected */
    sz = mkwav(b, 1, 16000, pcm, 4, 8); put16(b + 34, 8);
    s = vox_parse_wav_buffer(b, sz, &n);
    report("wav_reject_8bit", s == NULL);
    free(s);
    memcpy(b, "RIFX", 4);
    report("wav_reject_magic", vox_parse_wav_buffer(b, sz, &n) == NULL);
}

static const char *tmp_write(const char *tag, const void *d, size_t n) {
    static char path[512];
    const char *t = getenv("TMPDIR"); if (!t || !*t) t = "/tmp";
    snprintf(path, sizeof path, "%s/voxselftest-%s-%d", t, tag, (int)getpid());
    FILE *f = fopen(path, "wb"); fwrite(d, 1, n, f); fclose(f);
    return path;
}

static void test_safetensors(void) {
    const char *hdr = "{\"__metadata__\":{\"format\":\"pt\"},"
        "\"a.weight\":{\"dtype\":\"F32\",\"shape\":[2,2],\"data_offsets\":[0,16]},"
        "\"b.weight\":{\"dtype\":\"BF16\",\"shape\":[4],\"data_offsets\":[16,24]},"
        "\"c.bias\":{\"dtype\":\"F16\",\"shape\":[1,4],\"data_offsets\":[24,32]}}";
    uint8_t buf[1024]; uint64_t hl = strlen(hdr);
    memcpy(buf, &hl, 8); memcpy(buf + 8, hdr, hl);
    float f32[4] = {1.0f, -2.5f, 0.0f, 3.25f}; memcpy(buf + 8 + hl, f32, 16);
    uint16_t bf[4] = {0x3f80, 0xc020, 0x0000, 0x4050}; memcpy(buf + 8 + hl + 16, bf, 8);
    uint16_t hf[4] = {0x3c00, 0xc100, 0x0000, 0x4280}; memcpy(buf + 8 + hl + 24, hf, 8);
    const char *p = tmp_write("st", buf, 8 + hl + 32);
    safetensors_file_t *sf = safetensors_open(p);
    report("st_open", sf && sf->num_tensors == 3);
    const safetensor_t *a = sf ? safetensors_find(sf, "a.weight") : NULL;
    const safetensor_t *bt = sf ? safetensors_find(sf, "b.weight") : NULL;
    const safetensor_t *c = sf ? safetensors_find(sf, "c.bias") : NULL;
    report("st_shapes", a && bt && c && a->ndim == 2 && a->shape[0] == 2 && safetensor_numel(a) == 4
           && c->ndim == 2 && c->shape[1] == 4 && bt->dtype == DTYPE_BF16 && c->dtype == DTYPE_F16
           && !safetensors_find(sf, "missing"));
    float *fa = a ? safetensors_get_f32(sf, a) : NULL;
    report("st_f32", fa && fa[1] == -2.5f && fa[3] == 3.25f);
    float *fb = bt ? safetensors_get_f32(sf, bt) : NULL;
    report("st_bf16_to_f32", fb && fb[0] == 1.0f && fb[1] == -2.5f && fb[3] == 3.25f);
    float *fc = c ? safetensors_get_f32(sf, c) : NULL;
    report("st_f16_to_f32", fc && fc[0] == 1.0f && fc[1] == -2.5f && fc[3] == 3.25f);
    uint16_t *rb = bt ? safetensors_get_bf16(sf, bt) : NULL;
    report("st_bf16_raw", rb && rb[1] == 0xc020 && safetensors_get_bf16(sf, a) == NULL);
    free(fa); free(fb); free(fc); free(rb);
    safetensors_close(sf); unlink(p);
    /* tensor data past the end of the file is rejected */
    const char *bad = "{\"x\":{\"dtype\":\"F32\",\"shape\":[4],\"data_offsets\":[0,64]}}";
    hl = strlen(bad); memcpy(buf, &hl, 8); memcpy(buf + 8, bad, hl);
    p = tmp_write("stbad", buf, 8 + hl + 16);
    sf = safetensors_open(p);
    report("st_reject_oob", sf == NULL);
    safetensors_close(sf); unlink(p);
}

static void test_tokenizer(void) {
    const char *j = "{\"config\":{\"pattern\":\"[a-z]+\",\"n\":[1,2]},\"vocab\":["
        "{\"rank\":0,\"token_bytes\":\"aGVsbG8=\",\"token_str\":\"hello\"},"
        "{\"rank\":1,\"token_bytes\":\"IHdvcmxk\",\"token_str\":\" world\"},"
        "{\"rank\":2,\"token_bytes\":\"w6l0w6k=\",\"token_str\":null}],"
        "\"special_tokens\":[{\"rank\":0,\"token_str\":\"<unk>\"},{\"rank\":1,\"token_str\":\"<s>\"},"
        "{\"rank\":2,\"token_str\":\"</s>\"},{\"rank\":3,\"token_str\":\"\\u00e9\\t\"}]}";
    const char *p = tmp_write("tok", j, strlen(j));
    vox_tokenizer_t *t = vox_tokenizer_load(p);
    report("tok_load", t != NULL);
    const char *d0 = t ? vox_tokenizer_decode(t, 1000) : NULL;
    const char *d2 = t ? vox_tokenizer_decode(t, 1002) : NULL;
    report("tok_decode_b64", d0 && strcmp(d0, "hello") == 0 && d2 && strcmp(d2, "\xc3\xa9t\xc3\xa9") == 0);
    const char *sp = t ? vox_tokenizer_decode(t, 3) : NULL;
    report("tok_special_unicode", sp && strcmp(sp, "\xc3\xa9\t") == 0 && t && strcmp(vox_tokenizer_decode(t, 1), "<s>") == 0
           && vox_tokenizer_decode(t, 1003) == NULL);
    int ids[] = {1, 1000, 1001, 2, 1002, 5000};
    char *seq = t ? vox_tokenizer_decode_seq(t, ids, 6) : NULL;
    report("tok_decode_seq", seq && strcmp(seq, "hello world\xc3\xa9t\xc3\xa9") == 0);
    free(seq);
    vox_tokenizer_free(t); unlink(p);
}

int main(int argc, char **argv) {
    const char *wav = argc > 1 ? argv[1] : "samples/test_speech.wav";
    test_wav_and_mel(wav);
    test_wav_variants();
    test_safetensors();
    test_tokenizer();
    printf("# %d failures\n", failures);
    return failures ? 1 : 0;
}
