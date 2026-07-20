/*
 * vk_ps4_stubs.c — Stub implementations for not-yet-implemented Vulkan functions.
 *
 * Phase 1 implements: memory, buffer, image, render pass, framebuffer, shader,
 * pipeline, command buffer, swapchain, queue, sync.
 *
 * Remaining stubs: query pool (Phase 3), some copy/blit commands (Phase 2),
 * and the stub shader compile path (when libpsbc is unavailable).
 */

#include "vk_ps4_internal.h"

#include <string.h>

/* === Stub shader (when libpsbc is not available) === */
VkResult vk_ps4_shader_compile_stub(
    const uint32_t *spirv, size_t spirv_size,
    VkShaderStageFlagBits stage,
    void **out_binary, size_t *out_binary_size
) {
    (void)spirv; (void)spirv_size; (void)stage;
    if (!out_binary || !out_binary_size) return VK_ERROR_INITIALIZATION_FAILED;
    *out_binary = NULL;
    *out_binary_size = 0;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}
