/* Safetensors reader (the model-weights format vox_load() and inspect_weights open):
 * safetensors_open() parses the u64 header length + JSON header and bounds-checks
 * every tensor; then each tensor is read back the way the loader does. */
#include <stdio.h>
#include "tmpfile.h"
#include "voxtral_safetensors.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const char *path = write_input(data, size);
    if (!path) return 0;
    safetensors_file_t *sf = safetensors_open(path);
    if (!sf) return 0;
    for (int i = 0; i < sf->num_tensors; i++) {
        const safetensor_t *t = &sf->tensors[i];
        (void)safetensor_numel(t);
        (void)safetensors_data(sf, t);
        float *f = safetensors_get_f32(sf, t);
        free(f);
        if (safetensor_is_bf16(t)) {
            uint16_t *b = safetensors_get_bf16(sf, t);
            free(b);
            (void)safetensors_get_bf16_direct(sf, t);
        }
        (void)safetensors_find(sf, t->name);
    }
    (void)safetensors_find(sf, "mm_streams_embeddings.embedding_module.tok_embeddings.weight");
    /* print_all writes to stdout: keep it, it formats every parsed field */
    safetensors_print_all(sf);
    safetensors_close(sf);
    return 0;
}
