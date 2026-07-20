/*
 * vk_ps4_render_pass.c — VkRenderPass / VkFramebuffer implementation.
 *
 * VkRenderPass stores attachment descriptions and subpass info.
 * VkFramebuffer links attachment image views.
 * vkCmdBeginRenderPass emits render target set + clear operations.
 * vkCmdEndRenderPass emits EOP event.
 */

#include "vk_ps4_internal.h"

#include <string.h>

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
    if (!device || !pCreateInfo || !pRenderPass) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4RenderPass *rp = vk_ps4_alloc_zero(alloc, sizeof(*rp), 16);
    if (!rp) return VK_ERROR_OUT_OF_HOST_MEMORY;
    rp->type = VK_PS4_OBJ_RENDER_PASS;
    rp->device = dev;
    rp->create_info = *pCreateInfo;
    rp->attachment_count = pCreateInfo->attachmentCount;
    rp->subpass_count = pCreateInfo->subpassCount;

    /* Deep copy attachments */
    if (rp->attachment_count > 0) {
        rp->attachments = vk_ps4_alloc_zero(alloc,
            rp->attachment_count * sizeof(VkAttachmentDescription), 16);
        if (!rp->attachments) {
            vk_ps4_free(alloc, rp);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memcpy(rp->attachments, pCreateInfo->pAttachments,
               rp->attachment_count * sizeof(VkAttachmentDescription));
    }

    /* Deep copy subpasses (simplified — just copy the top-level structs) */
    if (rp->subpass_count > 0) {
        rp->subpasses = vk_ps4_alloc_zero(alloc,
            rp->subpass_count * sizeof(VkSubpassDescription), 16);
        if (!rp->subpasses) {
            vk_ps4_free(alloc, rp->attachments);
            vk_ps4_free(alloc, rp);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memcpy(rp->subpasses, pCreateInfo->pSubpasses,
               rp->subpass_count * sizeof(VkSubpassDescription));
    }

    *pRenderPass = (VkRenderPass)rp;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator) {
    if (!device || !renderPass) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4RenderPass *rp = (VkPs4RenderPass *)renderPass;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, rp->attachments);
    vk_ps4_free(alloc, rp->subpasses);
    vk_ps4_free(alloc, rp);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer) {
    if (!device || !pCreateInfo || !pFramebuffer) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Framebuffer *fb = vk_ps4_alloc_zero(alloc, sizeof(*fb), 16);
    if (!fb) return VK_ERROR_OUT_OF_HOST_MEMORY;
    fb->type = VK_PS4_OBJ_FRAMEBUFFER;
    fb->device = dev;
    fb->render_pass = (VkPs4RenderPass *)pCreateInfo->renderPass;
    fb->create_info = *pCreateInfo;
    fb->attachment_count = pCreateInfo->attachmentCount;
    fb->width = pCreateInfo->width;
    fb->height = pCreateInfo->height;
    fb->layers = pCreateInfo->layers;

    /* Store attachment image view pointers */
    if (fb->attachment_count > 0) {
        fb->attachments = vk_ps4_alloc_zero(alloc,
            fb->attachment_count * sizeof(VkPs4ImageView *), 16);
        if (!fb->attachments) {
            vk_ps4_free(alloc, fb);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        for (uint32_t i = 0; i < fb->attachment_count; i++) {
            fb->attachments[i] = (VkPs4ImageView *)pCreateInfo->pAttachments[i];
        }
    }

    *pFramebuffer = (VkFramebuffer)fb;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator) {
    if (!device || !framebuffer) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Framebuffer *fb = (VkPs4Framebuffer *)framebuffer;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, fb->attachments);
    vk_ps4_free(alloc, fb);
}
