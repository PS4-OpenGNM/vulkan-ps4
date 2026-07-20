/*
 * vk_ps4_image.c — VkImage / VkImageView implementation.
 *
 * VkImage with color attachment usage → GnmRenderTarget
 * VkImage with depth/stencil usage → GnmDepthRenderTarget
 * VkImage with sampled/storage usage → GnmTexture
 * VkImageView → copy/modify the GnmTexture or GnmRenderTarget descriptor
 */

#include "vk_ps4_internal.h"

#include <string.h>

static GnmTextureType vk_image_type_to_gnm(VkImageType type) {
    switch (type) {
    case VK_IMAGE_TYPE_1D: return GNM_TEXTURE_1D;
    case VK_IMAGE_TYPE_2D: return GNM_TEXTURE_2D;
    case VK_IMAGE_TYPE_3D: return GNM_TEXTURE_3D;
    default: return GNM_TEXTURE_2D;
    }
}

static bool vk_image_is_color_target(const VkImageCreateInfo *ci) {
    return (ci->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0;
}

static bool vk_image_is_depth(const VkImageCreateInfo *ci) {
    return (ci->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkImage *pImage) {
    if (!device || !pCreateInfo || !pImage) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Image *img = vk_ps4_alloc_zero(alloc, sizeof(*img), 16);
    if (!img) return VK_ERROR_OUT_OF_HOST_MEMORY;
    img->type = VK_PS4_OBJ_IMAGE;
    img->device = dev;
    img->create_info = *pCreateInfo;
    img->memory = NULL;
    img->memory_offset = 0;
    img->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    img->is_render_target = false;

    GnmDataFormat gnm_fmt = vk_ps4_vk_format_to_gnm(pCreateInfo->format);
    GnmGpuMode gpu_mode = GNM_GPU_BASE;

    if (vk_image_is_color_target(pCreateInfo)) {
        /* Create as render target */
        img->is_render_target = true;
        GnmRenderTargetCreateInfo rt_ci;
        sceGnmRtInitColorTargetCreateInfo(
            &rt_ci, gnm_fmt,
            pCreateInfo->extent.width, pCreateInfo->extent.height,
            pCreateInfo->arrayLayers,  /* numslices */
            pCreateInfo->samples,      /* numsamples */
            pCreateInfo->samples,      /* numfragments */
            GNM_TM_DISPLAY_2D_THIN,    /* tilemode */
            gpu_mode
        );

        GnmError err = sceGnmCreateRenderTarget(&img->gnm_rt, &rt_ci);
        if (err != GNM_ERROR_OK) {
            vk_ps4_free(alloc, img);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
    } else if (vk_image_is_depth(pCreateInfo)) {
        /* Depth/stencil target — for Phase 1, store minimal info.
         * Full depth target support is Phase 3. */
        img->is_render_target = true;
        memset(&img->gnm_rt, 0, sizeof(img->gnm_rt));
    } else {
        /* Create as texture */
        GnmTextureCreateInfo tex_ci;
        memset(&tex_ci, 0, sizeof(tex_ci));
        tex_ci.format = gnm_fmt;
        tex_ci.texturetype = vk_image_type_to_gnm(pCreateInfo->imageType);
        tex_ci.width = pCreateInfo->extent.width;
        tex_ci.height = pCreateInfo->extent.height;
        tex_ci.depth = pCreateInfo->extent.depth;
        tex_ci.nummiplevels = pCreateInfo->mipLevels;
        tex_ci.numslices = pCreateInfo->arrayLayers;
        tex_ci.numfragments = 1;
        tex_ci.tilemodehint = GNM_TM_DISPLAY_2D_THIN;
        tex_ci.mingpumode = gpu_mode;

        GnmError err = sceGnmCreateTexture(&img->gnm_texture, &tex_ci);
        if (err != GNM_ERROR_OK) {
            vk_ps4_free(alloc, img);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
    }

    *pImage = (VkImage)img;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator) {
    if (!device || !image) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Image *img = (VkPs4Image *)image;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, img);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements) {
    (void)device;
    if (!image || !pMemoryRequirements) return;
    VkPs4Image *img = (VkPs4Image *)image;

    if (img->is_render_target) {
        uint64_t size = 0;
        uint32_t align = 0;
        sceGnmRtCalcByteSize(&size, &align, &img->gnm_rt);
        if (size == 0) {
            /* Fallback for depth targets or unconfigured RTs */
            GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(img->create_info.format);
            uint32_t bpp = 4; /* default 4 bytes per pixel */
            (void)fmt;
            size = (uint64_t)img->create_info.extent.width *
                   img->create_info.extent.height * bpp;
            align = 64;
        }
        pMemoryRequirements->size = size;
        pMemoryRequirements->alignment = align ? align : 64;
    } else {
        uint64_t size = 0;
        uint32_t align = 0;
        sceGnmTexCalcByteSize(&size, &align, &img->gnm_texture);
        if (size == 0) {
            uint32_t bpp = 4;
            size = (uint64_t)img->create_info.extent.width *
                   img->create_info.extent.height * bpp;
            align = 64;
        }
        pMemoryRequirements->size = size;
        pMemoryRequirements->alignment = align ? align : 64;
    }
    /* Images prefer Garlic (GPU-local) memory */
    pMemoryRequirements->memoryTypeBits = (1u << VK_PS4_MEMORY_TYPE_GARLIC) |
                                          (1u << VK_PS4_MEMORY_TYPE_ONION);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
    if (!device || !image || !memory) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Image *img = (VkPs4Image *)image;
    VkPs4DeviceMemory *mem = (VkPs4DeviceMemory *)memory;

    img->memory = mem;
    img->memory_offset = offset;

    void *gpu_addr = (char *)mem->gnm_mem.mapped + offset;

    if (img->is_render_target) {
        sceGnmRtSetBaseAddr(&img->gnm_rt, gpu_addr);
    } else {
        sceGnmTexSetBaseAddress(&img->gnm_texture, gpu_addr);
    }

    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator, VkImageView *pImageView) {
    if (!device || !pCreateInfo || !pImageView) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4ImageView *view = vk_ps4_alloc_zero(alloc, sizeof(*view), 16);
    if (!view) return VK_ERROR_OUT_OF_HOST_MEMORY;
    view->type = VK_PS4_OBJ_IMAGE_VIEW;
    view->device = dev;
    view->image = (VkPs4Image *)pCreateInfo->image;
    view->create_info = *pCreateInfo;

    /* For render target images, the view is the same RT descriptor.
     * For texture images, copy the texture descriptor (view = same for now). */
    if (view->image->is_render_target) {
        /* No separate view descriptor needed for render targets */
    } else {
        view->gnm_view = view->image->gnm_texture;
    }

    *pImageView = (VkImageView)view;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator) {
    if (!device || !imageView) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4ImageView *view = (VkPs4ImageView *)imageView;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, view);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetImageSubresourceLayout(VkDevice device, VkImage image,
                                 const VkImageSubresource *pSubresource, VkSubresourceLayout *pLayout) {
    (void)device;
    (void)pSubresource;
    if (!image || !pLayout) return;
    VkPs4Image *img = (VkPs4Image *)image;
    memset(pLayout, 0, sizeof(*pLayout));
    /* For linear textures, row pitch = width * bytes_per_pixel */
    uint32_t bpp = 4;
    pLayout->offset = 0;
    pLayout->rowPitch = img->create_info.extent.width * bpp;
    pLayout->depthPitch = pLayout->rowPitch * img->create_info.extent.height;
    pLayout->size = pLayout->depthPitch * img->create_info.extent.depth;
}
