/*
 * mesa_stubs.c — Stub implementations for Mesa utility functions that
 * libpsbc.orbis.a references but doesn't include.
 *
 * These functions (BLAKE3 SIMD variants, DXT format pack/unpack/fetch,
 * Mesa logging) are transitively referenced by Mesa's NIR/ACO compiler
 * but are never called during SPIR-V → GCN shader compilation.  We
 * provide no-op stubs so the static linker resolves them without pulling
 * in the full Mesa util library.
 */

#include <stddef.h>
#include <stdint.h>

/* === Mesa logging === */
void _mesa_log_multiline(const char *tag, const char *line) {
    (void)tag;
    (void)line;
}

/* === BLAKE3 SIMD variants (never called on PS4 — GCN, not x86) === */
void blake3_compress_in_place_avx512(uint32_t *out, const uint32_t *input,
    uint32_t block_len, const uint32_t key[8], uint8_t flags) {
    (void)out; (void)input; (void)block_len; (void)key; (void)flags;
}
void blake3_compress_in_place_sse41(uint32_t *out, const uint32_t *input,
    uint32_t block_len, const uint32_t key[8], uint8_t flags) {
    (void)out; (void)input; (void)block_len; (void)key; (void)flags;
}
void blake3_compress_in_place_sse2(uint32_t *out, const uint32_t *input,
    uint32_t block_len, const uint32_t key[8], uint8_t flags) {
    (void)out; (void)input; (void)block_len; (void)key; (void)flags;
}
void blake3_compress_xof_avx512(uint32_t *out, const uint32_t *input,
    uint32_t block_len, const uint32_t key[8], uint8_t flags,
    uint8_t block_length, uint64_t chunk_counter, uint8_t chunk_start) {
    (void)out; (void)input; (void)block_len; (void)key; (void)flags;
    (void)block_length; (void)chunk_counter; (void)chunk_start;
}
void blake3_compress_xof_sse41(uint32_t *out, const uint32_t *input,
    uint32_t block_len, const uint32_t key[8], uint8_t flags,
    uint8_t block_length, uint64_t chunk_counter, uint8_t chunk_start) {
    (void)out; (void)input; (void)block_len; (void)key; (void)flags;
    (void)block_length; (void)chunk_counter; (void)chunk_start;
}
void blake3_compress_xof_sse2(uint32_t *out, const uint32_t *input,
    uint32_t block_len, const uint32_t key[8], uint8_t flags,
    uint8_t block_length, uint64_t chunk_counter, uint8_t chunk_start) {
    (void)out; (void)input; (void)block_len; (void)key; (void)flags;
    (void)block_length; (void)chunk_counter; (void)chunk_start;
}
size_t blake3_hash_many_avx2(const uint8_t *const *inputs, size_t num_inputs,
    size_t block_len, const uint32_t key[8], uint64_t chunk_counter,
    int curr_chunk_flags, uint8_t *out, size_t out_len) {
    (void)inputs; (void)num_inputs; (void)block_len; (void)key;
    (void)chunk_counter; (void)curr_chunk_flags; (void)out; (void)out_len;
    return 0;
}
size_t blake3_hash_many_avx512(const uint8_t *const *inputs, size_t num_inputs,
    size_t block_len, const uint32_t key[8], uint64_t chunk_counter,
    int curr_chunk_flags, uint8_t *out, size_t out_len) {
    (void)inputs; (void)num_inputs; (void)block_len; (void)key;
    (void)chunk_counter; (void)curr_chunk_flags; (void)out; (void)out_len;
    return 0;
}
size_t blake3_hash_many_sse2(const uint8_t *const *inputs, size_t num_inputs,
    size_t block_len, const uint32_t key[8], uint64_t chunk_counter,
    int curr_chunk_flags, uint8_t *out, size_t out_len) {
    (void)inputs; (void)num_inputs; (void)block_len; (void)key;
    (void)chunk_counter; (void)curr_chunk_flags; (void)out; (void)out_len;
    return 0;
}
size_t blake3_hash_many_sse41(const uint8_t *const *inputs, size_t num_inputs,
    size_t block_len, const uint32_t key[8], uint64_t chunk_counter,
    int curr_chunk_flags, uint8_t *out, size_t out_len) {
    (void)inputs; (void)num_inputs; (void)block_len; (void)key;
    (void)chunk_counter; (void)curr_chunk_flags; (void)out; (void)out_len;
    return 0;
}
size_t blake3_xof_many_avx512(const uint8_t *const *inputs, size_t num_inputs,
    size_t block_len, const uint32_t key[8], uint64_t chunk_counter,
    int curr_chunk_flags, uint8_t *out, size_t out_len) {
    (void)inputs; (void)num_inputs; (void)block_len; (void)key;
    (void)chunk_counter; (void)curr_chunk_flags; (void)out; (void)out_len;
    return 0;
}

/* === DXT format pack/unpack/fetch stubs ===
 * Referenced by Mesa's u_format_table but never used for GCN shader
 * compilation.  PS4 uses its own texture compression format (GNM),
 * not S3TC/DXT.  All stubs are no-ops.
 *
 * Signatures (from Mesa util/format/u_format_s3tc.h):
 *   pack:   (const void *src, void *dst, size_t dst_stride)
 *   unpack: (void *dst, const void *src, size_t src_stride)
 *   fetch:  (void *dst, const void *src)
 */

#define DXT_PACK(name) \
    void name(const void *src, void *dst, size_t dst_stride) { \
        (void)src; (void)dst; (void)dst_stride; \
    }
#define DXT_UNPACK(name) \
    void name(void *dst, const void *src, size_t src_stride) { \
        (void)dst; (void)src; (void)src_stride; \
    }
#define DXT_FETCH(name) \
    void name(void *dst, const void *src) { \
        (void)dst; (void)src; \
    }

/* DXT1 RGB */
DXT_PACK(util_format_dxt1_rgb_pack_rgba_8unorm)
DXT_PACK(util_format_dxt1_rgb_pack_rgba_float)
DXT_UNPACK(util_format_dxt1_rgb_unpack_rgba_8unorm)
DXT_UNPACK(util_format_dxt1_rgb_unpack_rgba_float)
DXT_FETCH(util_format_dxt1_rgb_fetch_rgba_8unorm)

/* DXT1 RGBA */
DXT_PACK(util_format_dxt1_rgba_pack_rgba_8unorm)
DXT_PACK(util_format_dxt1_rgba_pack_rgba_float)
DXT_UNPACK(util_format_dxt1_rgba_unpack_rgba_8unorm)
DXT_UNPACK(util_format_dxt1_rgba_unpack_rgba_float)
DXT_FETCH(util_format_dxt1_rgba_fetch_rgba_8unorm)

/* DXT1 SRGB */
DXT_PACK(util_format_dxt1_srgb_pack_rgba_8unorm)
DXT_PACK(util_format_dxt1_srgb_pack_rgba_float)
DXT_UNPACK(util_format_dxt1_srgb_unpack_rgba_8unorm)
DXT_UNPACK(util_format_dxt1_srgb_unpack_rgba_float)
DXT_FETCH(util_format_dxt1_srgb_fetch_rgba_8unorm)

/* DXT1 SRGBA */
DXT_PACK(util_format_dxt1_srgba_pack_rgba_8unorm)
DXT_PACK(util_format_dxt1_srgba_pack_rgba_float)
DXT_UNPACK(util_format_dxt1_srgba_unpack_rgba_8unorm)
DXT_UNPACK(util_format_dxt1_srgba_unpack_rgba_float)
DXT_FETCH(util_format_dxt1_srgba_fetch_rgba_8unorm)

/* DXT3 RGBA */
DXT_PACK(util_format_dxt3_rgba_pack_rgba_8unorm)
DXT_PACK(util_format_dxt3_rgba_pack_rgba_float)
DXT_UNPACK(util_format_dxt3_rgba_unpack_rgba_8unorm)
DXT_UNPACK(util_format_dxt3_rgba_unpack_rgba_float)
DXT_FETCH(util_format_dxt3_rgba_fetch_rgba_8unorm)

/* DXT3 SRGBA */
DXT_PACK(util_format_dxt3_srgba_pack_rgba_8unorm)
DXT_PACK(util_format_dxt3_srgba_pack_rgba_float)
DXT_UNPACK(util_format_dxt3_srgba_unpack_rgba_8unorm)
DXT_UNPACK(util_format_dxt3_srgba_unpack_rgba_float)
DXT_FETCH(util_format_dxt3_srgba_fetch_rgba_8unorm)

/* DXT5 RGBA */
DXT_PACK(util_format_dxt5_rgba_pack_rgba_8unorm)
DXT_PACK(util_format_dxt5_rgba_pack_rgba_float)
DXT_UNPACK(util_format_dxt5_rgba_unpack_rgba_8unorm)
DXT_UNPACK(util_format_dxt5_rgba_unpack_rgba_float)
DXT_FETCH(util_format_dxt5_rgba_fetch_rgba_8unorm)

/* DXT5 SRGBA */
DXT_PACK(util_format_dxt5_srgba_pack_rgba_8unorm)
DXT_PACK(util_format_dxt5_srgba_pack_rgba_float)
DXT_UNPACK(util_format_dxt5_srgba_unpack_rgba_8unorm)
DXT_UNPACK(util_format_dxt5_srgba_unpack_rgba_float)
DXT_FETCH(util_format_dxt5_srgba_fetch_rgba_8unorm)

/* DXT fetch_rgba (float variant — no _8unorm suffix) */
DXT_FETCH(util_format_dxt1_rgb_fetch_rgba)
DXT_FETCH(util_format_dxt1_rgba_fetch_rgba)
DXT_FETCH(util_format_dxt1_srgb_fetch_rgba)
DXT_FETCH(util_format_dxt1_srgba_fetch_rgba)
DXT_FETCH(util_format_dxt3_rgba_fetch_rgba)
DXT_FETCH(util_format_dxt3_srgba_fetch_rgba)
DXT_FETCH(util_format_dxt5_rgba_fetch_rgba)
DXT_FETCH(util_format_dxt5_srgba_fetch_rgba)
