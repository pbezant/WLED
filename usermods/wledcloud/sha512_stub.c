/*
 * sha512_stub.c — linker stubs for mbedtls_sha512_* functions
 *
 * The Tasmota IDF V4 ESP32 platform strips SHA-512 from its pre-built mbedTLS
 * to save flash, but esp_sha.c.obj (the hardware SHA driver) still references
 * those symbols.  Our code never performs SHA-512 at runtime (WebSocket
 * handshake uses SHA-1), so empty stubs satisfy the linker without risk.
 */

#include <stddef.h>
#include <string.h>

/* Minimal context struct — mirrors the real mbedtls_sha512_context layout
   just enough so the pointer-based calls don't crash if ever hit.  */
typedef struct { unsigned char buf[128]; size_t total[2]; unsigned long state[8]; int is384; } mbedtls_sha512_context;

void mbedtls_sha512_init(mbedtls_sha512_context *ctx)            { (void)ctx; }
void mbedtls_sha512_free(mbedtls_sha512_context *ctx)            { (void)ctx; }
void mbedtls_sha512_clone(mbedtls_sha512_context *dst,
                           const mbedtls_sha512_context *src)    { (void)dst; (void)src; }

int  mbedtls_sha512_starts_ret(mbedtls_sha512_context *ctx, int is384) { (void)ctx; (void)is384; return 0; }
int  mbedtls_sha512_update_ret(mbedtls_sha512_context *ctx,
                                const unsigned char *input, size_t ilen) { (void)ctx; (void)input; (void)ilen; return 0; }
int  mbedtls_sha512_finish_ret(mbedtls_sha512_context *ctx,
                                unsigned char output[64])        { (void)ctx; memset(output, 0, 64); return 0; }
int  mbedtls_internal_sha512_process(mbedtls_sha512_context *ctx,
                                      const unsigned char data[128]) { (void)ctx; (void)data; return 0; }

/* Some IDF versions call the non-_ret variants */
void mbedtls_sha512_starts(mbedtls_sha512_context *ctx, int is384) { (void)ctx; (void)is384; }
void mbedtls_sha512_update(mbedtls_sha512_context *ctx,
                            const unsigned char *input, size_t ilen) { (void)ctx; (void)input; (void)ilen; }
void mbedtls_sha512_finish(mbedtls_sha512_context *ctx,
                            unsigned char output[64])             { (void)ctx; memset(output, 0, 64); }
