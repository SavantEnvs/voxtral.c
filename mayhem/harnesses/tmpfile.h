/* The safetensors and tokenizer loaders take a path only (mmap / fopen), so the
 * input is written to one scratch file under $TMPDIR, rewritten for every input. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char g_path[4096];

static const char *write_input(const uint8_t *data, size_t size) {
    if (!g_path[0]) {
        const char *d = getenv("TMPDIR");
        if (!d || !*d) d = "/tmp";
        snprintf(g_path, sizeof g_path, "%s/voxfuzz-XXXXXX", d);
        int fd = mkstemp(g_path);
        if (fd < 0) { g_path[0] = 0; return NULL; }
        close(fd);
    }
    FILE *f = fopen(g_path, "wb");
    if (!f) return NULL;
    if (size && fwrite(data, 1, size, f) != size) { fclose(f); return NULL; }
    fclose(f);
    return g_path;
}
