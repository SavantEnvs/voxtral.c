/* voxtral_tokenizer.c reads `extern int vox_verbose`, which upstream defines in voxtral.c —
 * the file that pulls in the whole model (encoder/decoder/kernels/BLAS). The harnesses and
 * the selftest link only the model-free parsers, so the global is defined here. */
int vox_verbose = 0;
