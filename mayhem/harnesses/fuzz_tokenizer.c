/* Tekken tokenizer (tekken.json): vox_tokenizer_load() parses the hand-written JSON
 * (vocab with base64 token_bytes, special_tokens), then decode / decode_seq look the
 * ids up as the transcription loop does. */
#include "tmpfile.h"
#include "voxtral_tokenizer.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const char *path = write_input(data, size);
    if (!path) return 0;
    vox_tokenizer_t *tok = vox_tokenizer_load(path);
    if (!tok) return 0;
    int vs = vox_tokenizer_vocab_size(tok);
    int ids[16];
    int n = 0;
    ids[n++] = 0; ids[n++] = 1; ids[n++] = 2; ids[n++] = 999; ids[n++] = 1000;
    ids[n++] = 1001; ids[n++] = vs - 1; ids[n++] = vs; ids[n++] = vs + 999;
    ids[n++] = vox_tokenizer_bos(tok); ids[n++] = vox_tokenizer_eos(tok);
    /* ids from the input tail, as a model would emit arbitrary ids */
    for (size_t i = 0; i + 1 < size && n < 16 && i < 10; i += 2)
        ids[n++] = (data[size - 1 - i] | (data[size - 2 - i] << 8)) + 1000;
    for (int i = 0; i < n; i++) {
        const char *s = vox_tokenizer_decode(tok, ids[i]);
        if (s) { volatile size_t l = strlen(s); (void)l; }
    }
    char *seq = vox_tokenizer_decode_seq(tok, ids, n);
    free(seq);
    vox_tokenizer_free(tok);
    return 0;
}
