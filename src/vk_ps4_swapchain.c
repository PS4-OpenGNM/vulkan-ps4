/*
 * vk_ps4_swapchain.c — VkSwapchainKHR implementation via GnmVideoOut.
 *
 * vkCreateSwapchainKHR opens a GnmVideoOut, allocates display buffers,
 * and creates VkImage wrappers for each buffer.
 * vkAcquireNextImageKHR returns the next available buffer.
 * vkQueuePresentKHR submits the command buffer with a flip command.
 */

#include "vk_ps4_internal.h"

#include <string.h>

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain) {
    if (!device || !pCreateInfo || !pSwapchain) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Swapchain *sc = vk_ps4_alloc_zero(alloc, sizeof(*sc), 16);
    if (!sc) return VK_ERROR_OUT_OF_HOST_MEMORY;
    sc->type = VK_PS4_OBJ_SWAPCHAIN_KHR;
    sc->device = dev;
    sc->create_info = *pCreateInfo;  /* shallow copy — see below for pointer fixup */
    sc->current_image = 0;

    /* Validate minImageCount */
    if (pCreateInfo->minImageCount == 0) {
        vk_ps4_free(alloc, sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Null out dangling pointers in create_info — we don't use them after creation */
    sc->create_info.pNext = NULL;
    sc->create_info.pQueueFamilyIndices = NULL;
    sc->create_info.oldSwapchain = VK_NULL_HANDLE;

    /* Initialize VideoOut */
    GnmVideoOutCreateInfo vo_info;
    uint32_t width = pCreateInfo->imageExtent.width;
    uint32_t height = pCreateInfo->imageExtent.height;
    if (width == 0 || height == 0) {
        width = GNM_VIDEO_OUT_DEFAULT_WIDTH;
        height = GNM_VIDEO_OUT_DEFAULT_HEIGHT;
    }
    sceGnmVideoOutInitDefaultCreateInfo(&vo_info, width, height);

    /* Set number of buffers from swapchain create info */
    vo_info.numbuffers = pCreateInfo->minImageCount;
    if (vo_info.numbuffers > GNM_VIDEO_OUT_MAX_BUFFERS) {
        vo_info.numbuffers = GNM_VIDEO_OUT_MAX_BUFFERS;
    }
    if (vo_info.numbuffers == 0) {
        vk_ps4_free(alloc, sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Open VideoOut */
    GnmError err = sceGnmVideoOutOpen(&sc->video_out, &vo_info);
    if (err != GNM_ERROR_OK) {
        vk_ps4_free(alloc, sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Create VkImage wrappers for each VideoOut buffer */
    sc->image_count = vo_info.numbuffers;
    sc->images = vk_ps4_alloc_zero(alloc, sc->image_count * sizeof(VkPs4Image), 16);
    if (!sc->images) {
        sceGnmVideoOutClose(&sc->video_out);
        vk_ps4_free(alloc, sc);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    bool rt_ok = true;
    for (uint32_t i = 0; i < sc->image_count; i++) {
        VkPs4Image *img = &sc->images[i];
        img->type = VK_PS4_OBJ_IMAGE;
        img->device = dev;
        memset(&img->create_info, 0, sizeof(img->create_info));
        img->create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img->create_info.imageType = VK_IMAGE_TYPE_2D;
        img->create_info.format = pCreateInfo->imageFormat;
        img->create_info.extent.width = width;
        img->create_info.extent.height = height;
        img->create_info.extent.depth = 1;
        img->create_info.mipLevels = 1;
        img->create_info.arrayLayers = 1;
        img->create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        img->create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        img->create_info.usage = pCreateInfo->imageUsage | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        img->create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img->is_render_target = true;
        img->layout = VK_IMAGE_LAYOUT_UNDEFINED;
        img->memory = NULL;
        /* Mark as swapchain image for WaitUntilSafeForRendering. */
        img->is_swapchain_image = true;
        img->video_out_handle = sc->video_out.handle;
        img->swapchain_buffer_index = i;

        /* Set up render target descriptor pointing to the VideoOut buffer */
        void *buffer = sceGnmVideoOutGetBuffer(&sc->video_out, i);
        if (!buffer) {
            rt_ok = false;
            break;
        }
        GnmDataFormat gnm_fmt = vk_ps4_vk_format_to_gnm(pCreateInfo->imageFormat);
        uint64_t rt_size = 0;
        uint32_t rt_align = 0;
        GnmError rt_err = sceGnmRtCreateColorTarget(
            &img->gnm_rt, buffer, gnm_fmt,
            width, height, 1, 1, 1,
            GNM_TM_DISPLAY_LINEAR_GENERAL, GNM_GPU_BASE,
            &rt_size, &rt_align
        );
        if (rt_err != GNM_ERROR_OK) {
            rt_ok = false;
            break;
        }
    }

    if (!rt_ok) {
        vk_ps4_free(alloc, sc->images);
        sceGnmVideoOutClose(&sc->video_out);
        vk_ps4_free(alloc, sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    *pSwapchain = (VkSwapchainKHR)sc;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator) {
    if (!device || !swapchain) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Swapchain *sc = (VkPs4Swapchain *)swapchain;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    /* Wait for GPU to finish any in-flight work on swapchain images
     * before freeing them.  The Vulkan spec says the app must not
     * destroy a swapchain while images are in use, but be defensive
     * to avoid GPU faults or memory corruption. */
    vk_ps4_DeviceWaitIdle(device);

    if (sc->images) {
        vk_ps4_free(alloc, sc->images);
    }
    sceGnmVideoOutClose(&sc->video_out);
    vk_ps4_free(alloc, sc);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain,
                             uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages) {
    if (!device || !swapchain || !pSwapchainImageCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Swapchain *sc = (VkPs4Swapchain *)swapchain;

    if (!pSwapchainImages) {
        *pSwapchainImageCount = sc->image_count;
        return VK_SUCCESS;
    }

    if (*pSwapchainImageCount < sc->image_count) {
        *pSwapchainImageCount = sc->image_count;
        return VK_INCOMPLETE;
    }

    for (uint32_t i = 0; i < sc->image_count; i++) {
        pSwapchainImages[i] = (VkImage)&sc->images[i];
    }
    *pSwapchainImageCount = sc->image_count;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                           VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex) {
    if (!swapchain || !pImageIndex) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Swapchain *sc = (VkPs4Swapchain *)swapchain;
    if (sc->image_count == 0) {
        return VK_ERROR_SURFACE_LOST_KHR;
    }

    /* Find an available image.  An image is available if it is not
     * in-flight (image_in_flight[idx] == false).  If the caller
     * previously passed a fence, we check it as an optimization to
     * reclaim images whose GPU work has completed even before the
     * present call.  We search starting from current_image to
     * distribute load across buffers. */
    uint32_t acquired = sc->image_count;  /* invalid sentinel */
    for (uint32_t attempt = 0; attempt < sc->image_count; attempt++) {
        uint32_t idx = (sc->current_image + attempt) % sc->image_count;
        if (!sc->image_in_flight[idx]) {
            /* Image is free */
            acquired = idx;
            break;
        }
        /* Image is in-flight — check if its fence has been signaled.
         * If so, the GPU work is done and we can reclaim it early
         * (even before QueuePresentKHR clears the in-flight flag). */
        if (sc->image_fences[idx]) {
            VkPs4Fence *f = (VkPs4Fence *)sc->image_fences[idx];
            bool done = f->signaled;
            if (!done && f->label) {
                done = (*f->label == f->signal_value);
            }
            if (done) {
                sc->image_in_flight[idx] = false;
                sc->image_fences[idx] = NULL;
                acquired = idx;
                break;
            }
        }
    }

    if (acquired >= sc->image_count) {
        /* All images are in flight.  Wait for any one to become available.
         * Collect all in-flight fences and wait with waitAll=false so
         * we wait up to `timeout` for ANY fence, not the full timeout
         * per fence. */
        VkFence wait_fences[GNM_VIDEO_OUT_MAX_BUFFERS];
        uint32_t wait_count = 0;
        for (uint32_t i = 0; i < sc->image_count; i++) {
            if (sc->image_fences[i]) {
                wait_fences[wait_count++] = sc->image_fences[i];
            }
        }
        if (wait_count > 0) {
            VkResult wr = vk_ps4_WaitForFences(
                device, wait_count, wait_fences, VK_FALSE, timeout
            );
            if (wr != VK_SUCCESS) {
                return wr;  /* VK_TIMEOUT or error */
            }
            /* Find which fence signaled and reclaim its image. */
            for (uint32_t i = 0; i < sc->image_count; i++) {
                if (sc->image_fences[i]) {
                    VkPs4Fence *f = (VkPs4Fence *)sc->image_fences[i];
                    bool done = f->signaled;
                    if (!done && f->label) {
                        done = (*f->label == f->signal_value);
                    }
                    if (done) {
                        sc->image_in_flight[i] = false;
                        sc->image_fences[i] = NULL;
                        acquired = i;
                        break;
                    }
                }
            }
            if (acquired >= sc->image_count) {
                /* WaitForFences returned success but we didn't find
                 * a signaled fence — shouldn't happen, but handle it. */
                acquired = sc->current_image;
            }
        } else {
            /* No fences to wait on (all semaphore-only sync).
             * Wait for all GPU work to complete before reusing the
             * image, since we have no per-image fence to wait on.
             * This is conservative but correct — without it, the GPU
             * may still be rendering to the buffer we're about to
             * hand back to the caller. */
            vk_ps4_DeviceWaitIdle(device);
            /* Clear all in-flight flags since the GPU is now idle. */
            for (uint32_t i = 0; i < sc->image_count; i++) {
                sc->image_in_flight[i] = false;
            }
            acquired = sc->current_image;
        }
    }

    /* Mark this image as in-flight */
    sc->image_in_flight[acquired] = true;
    sc->image_fences[acquired] = fence;  /* may be NULL */
    sc->current_image = (acquired + 1) % sc->image_count;
    *pImageIndex = acquired;

    /* Signal semaphore and fence.  Per Vulkan spec, the semaphore/fence
     * passed to AcquireNextImageKHR is signaled when the image is
     * available, which is now.  We only set the CPU-side signaled flag
     * — we do NOT increment signal_value or write the GPU label, because
     * QueueSubmit owns GPU-side signaling (EOP writes).  Touching
     * signal_value here would cause a double-increment if the caller
     * reuses the same fence/semaphore with QueueSubmit. */
    if (semaphore) {
        VkPs4Semaphore *sem = (VkPs4Semaphore *)semaphore;
        sem->signaled = true;
    }
    if (fence) {
        VkPs4Fence *f = (VkPs4Fence *)fence;
        f->signaled = true;
    }

    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) {
    if (!queue || !pPresentInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pPresentInfo->pSwapchains || !pPresentInfo->pImageIndices) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Queue *q = (VkPs4Queue *)queue;

    /* Wait on semaphores.  On host, semaphores are CPU-tracked and
     * signaled synchronously by QueueSubmit, so we just reset them.
     * On Orbis, the GPU-side WaitMem in QueueSubmit ensures the GPU
     * has completed the semaphore's signaling work before we present. */
    for (uint32_t s = 0; s < pPresentInfo->waitSemaphoreCount; s++) {
        VkPs4Semaphore *sem = (VkPs4Semaphore *)pPresentInfo->pWaitSemaphores[s];
        if (sem) {
            sem->signaled = false;
        }
    }

    /* Submit flip for each swapchain.  After the flip completes
     * (sceGnmVideoOutSubmitFlipAndWait is blocking), the image is
     * no longer in flight — clear its fence so AcquireNextImageKHR
     * can reclaim it. */
    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
        VkPs4Swapchain *sc = (VkPs4Swapchain *)pPresentInfo->pSwapchains[i];
        if (!sc) continue;
        uint32_t image_index = pPresentInfo->pImageIndices[i];

        /* Submit flip for this buffer */
        if (image_index < sc->image_count) {
            sceGnmVideoOutSubmitFlipAndWait(
                &sc->video_out, image_index, (int64_t)sc->video_out.frame,
                GNM_VIDEO_OUT_FLIP_VSYNC
            );
            sc->video_out.frame++;
            /* The flip is complete — the display engine is done
             * with the buffer.  Clear the in-flight tracking so
             * AcquireNextImageKHR can reclaim this image. */
            sc->image_in_flight[image_index] = false;
            sc->image_fences[image_index] = NULL;
        }
    }

    /* Signal semaphores (Vulkan 1.1+ via VkPresentTimesInfoGOOGLE etc.
     * Vulkan 1.0 VkPresentInfoKHR has no signal semaphores) */
    (void)q;
    return VK_SUCCESS;
}
