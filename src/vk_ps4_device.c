/*
 * vk_ps4_device.c — VkDevice / VkQueue implementation.
 *
 * On CreateDevice the device submits a one-shot GNM command buffer carrying
 * sceGnmDrawInitDefaultHardwareState so the GPU starts from a known context
 * before any Vulkan command buffer is submitted.  The backing store for that
 * preamble is held in VkPs4Device::gnm_init_mem and released on DestroyDevice
 * after the device is quiesced.
 */

#include "vk_ps4_internal.h"

#include <stdlib.h>
#include <string.h>

#ifdef VK_PS4_HAVE_PSBC
#include "psbc_compile.h"
#endif

/* Size of the device-wide GNM init command buffer, in dwords.
 * The default-hardware-state packet is 256 dwords (HW_INIT_PACKET_SIZE in
 * opengnm); 1024 dwords gives headroom for any future preamble additions. */
#define VK_PS4_GNM_INIT_CMD_DWORDS 1024u

/* Size of the epilogue command buffer used for EOP fence/semaphore writes.
 * An EVENT_WRITE_EOP packet is 6 dwords on GFX6-8; 64 dwords is plenty for
 * one EOP write plus NOP padding. */
#define VK_PS4_GNM_EPILOGUE_CMD_DWORDS 64u

/* Submit the GNM default-hardware-state preamble and mark the device as
 * GNM-initialized.  Returns VK_SUCCESS on success, an error otherwise.
 * On failure the caller is responsible for tearing down partial state. */
static VkResult vk_ps4_device_init_gnm(VkPs4Device *dev) {
    const uint64_t cmd_bytes = (uint64_t)VK_PS4_GNM_INIT_CMD_DWORDS * sizeof(uint32_t);
    const uint64_t alignment = 64 * 1024; /* 64KB Garlic alignment */

    GnmError err = sceGnmDirectMemoryAllocate(
        &dev->gnm_init_mem, cmd_bytes, alignment,
        GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW
    );
    if (err != GNM_ERROR_OK) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    dev->gnm_init_cmd = (uint32_t *)dev->gnm_init_mem.mapped;
    dev->gnm_init_cmd_dwords = 0;

    /* Build the init command buffer in the mapped direct memory. */
    GnmCommandBuffer cmd = sceGnmCmdInit(
        dev->gnm_init_cmd, (uint32_t)cmd_bytes, NULL, NULL
    );
    sceGnmDrawCmdInitDefaultHardwareState(&cmd);

    uint32_t used_dwords = (uint32_t)(cmd.cmdptr - cmd.beginptr);
    uint32_t used_bytes = used_dwords * sizeof(uint32_t);
    dev->gnm_init_cmd_dwords = used_dwords;

    /* Submit the one-shot init packet.  On Orbis this programs the GPU's
     * default context state; on the host generic build it is a no-op. */
    void *dcb_addr = dev->gnm_init_cmd;
    int32_t result = sceGnmSubmitCommandBuffers(
        1, &dcb_addr, &used_bytes, NULL, NULL
    );
    if (result != 0) {
        sceGnmDirectMemoryRelease(&dev->gnm_init_mem);
        dev->gnm_init_cmd = NULL;
        dev->gnm_init_cmd_dwords = 0;
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    result = sceGnmSubmitDone();
    if (result != 0) {
        /* SubmitDone failure means the init packet was rejected; the GPU
         * may be in an indeterminate state, so treat this as fatal. */
        sceGnmDirectMemoryRelease(&dev->gnm_init_mem);
        dev->gnm_init_cmd = NULL;
        dev->gnm_init_cmd_dwords = 0;
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Allocate the epilogue command buffer used by QueueSubmit for EOP
         * fence/semaphore signal writes.  Small and reused across submits. */
    const uint64_t epilogue_bytes =
        (uint64_t)VK_PS4_GNM_EPILOGUE_CMD_DWORDS * sizeof(uint32_t);
    err = sceGnmDirectMemoryAllocate(
        &dev->gnm_epilogue_mem, epilogue_bytes, alignment,
        GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW
    );
    if (err != GNM_ERROR_OK) {
        sceGnmDirectMemoryRelease(&dev->gnm_init_mem);
        dev->gnm_init_cmd = NULL;
        dev->gnm_init_cmd_dwords = 0;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    dev->gnm_epilogue_cmd = (uint32_t *)dev->gnm_epilogue_mem.mapped;
    dev->gnm_epilogue_cmd_dwords = VK_PS4_GNM_EPILOGUE_CMD_DWORDS;

    /* Load the embedded clear pixel shader for tiled RT clears.
     * Copy the static binary to a malloc'd buffer (GPU-accessible on PS4)
     * and parse it with sceGnmShaderBinaryGetMetadata to extract the PS
     * stage registers and shader code address. */
    {
#include "../shaders/clear_ps_binary.h"
        dev->clear_ps_binary = malloc(g_clear_ps_binary_size);
        if (dev->clear_ps_binary) {
            memcpy(dev->clear_ps_binary, g_clear_ps_binary, g_clear_ps_binary_size);
            GnmShaderMetadata meta;
            memset(&meta, 0, sizeof(meta));
            GnmError gnm_err = sceGnmShaderBinaryGetMetadata(
                dev->clear_ps_binary, g_clear_ps_binary_size, &meta
            );
            if (gnm_err == GNM_ERROR_OK && meta.stage) {
                const GnmPsShader *ps = (const GnmPsShader *)meta.stage;
                dev->clear_ps_regs = ps->registers;
                /* The binary's spishaderpgmlops is an offset from the
                 * start of the GnmPsShader, not a GPU address.  We must
                 * call sceGnmPsRegsSetAddress to patch it to the actual
                 * address of the shader code in the malloc'd buffer.
                 * meta.shadercode points into dev->clear_ps_binary. */
                if (meta.shadercode) {
                    sceGnmPsRegsSetAddress(
                        &dev->clear_ps_regs, (void *)meta.shadercode
                    );
                    dev->clear_ps_ready = true;
                }
            }
        }
    }

    dev->gnm_initialized = true;
    return VK_SUCCESS;
}

/* Release the GNM init and epilogue command buffer backing stores.
 * Safe to call when gnm_initialized is false (no-op). */
static void vk_ps4_device_finish_gnm(VkPs4Device *dev) {
    if (dev->clear_ps_binary) {
        free(dev->clear_ps_binary);
        dev->clear_ps_binary = NULL;
        dev->clear_ps_ready = false;
    }
    if (dev->gnm_epilogue_mem.allocated) {
        sceGnmDirectMemoryRelease(&dev->gnm_epilogue_mem);
    }
    dev->gnm_epilogue_cmd = NULL;
    dev->gnm_epilogue_cmd_dwords = 0;
    if (dev->gnm_init_mem.allocated) {
        sceGnmDirectMemoryRelease(&dev->gnm_init_mem);
    }
    dev->gnm_init_cmd = NULL;
    dev->gnm_init_cmd_dwords = 0;
    dev->gnm_initialized = false;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDevice *pDevice
) {
    if (!physicalDevice || !pCreateInfo || !pDevice) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4PhysicalDevice *phys = (VkPs4PhysicalDevice *)physicalDevice;

    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &phys->instance->allocator;

    VkPs4Device *dev = vk_ps4_alloc_zero(alloc, sizeof(*dev), 16);
    if (!dev) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    dev->type = VK_PS4_OBJ_DEVICE;
    dev->physical_device = phys;
    if (pAllocator) {
        dev->allocator = *pAllocator;
    } else {
        /* Inherit instance allocator so destroy uses the same one */
        dev->allocator = phys->instance->allocator;
    }
    dev->gnm_initialized = false;

#ifdef VK_PS4_HAVE_PSBC
    /* Initialize libpsbc once per device — refcounted internally.
     * This avoids calling psbc_init/psbc_shutdown on every shader compile. */
    psbc_init();
#endif

    /* Submit the GNM default-hardware-state preamble so the GPU is in a
     * known state before any Vulkan command buffer is submitted.  This
     * replaces the previous "TODO: init GNM on PS4" stub. */
    VkResult gnm_result = vk_ps4_device_init_gnm(dev);
    if (gnm_result != VK_SUCCESS) {
#ifdef VK_PS4_HAVE_PSBC
        psbc_shutdown();
#endif
        vk_ps4_free(alloc, dev);
        return gnm_result;
    }

    /* Pre-allocate queues from pCreateInfo so handles are stable */
    dev->queue_count = 0;
    for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
        const VkDeviceQueueCreateInfo *qci = &pCreateInfo->pQueueCreateInfos[i];
        for (uint32_t j = 0; j < qci->queueCount && dev->queue_count < VK_PS4_MAX_QUEUES; j++) {
            VkPs4Queue *queue = vk_ps4_alloc_zero(alloc, sizeof(*queue), 16);
            if (!queue) {
                /* Free already-allocated queues */
                for (uint32_t k = 0; k < dev->queue_count; k++) {
                    vk_ps4_free(alloc, dev->queues[k]);
                }
                vk_ps4_device_finish_gnm(dev);
#ifdef VK_PS4_HAVE_PSBC
                psbc_shutdown();
#endif
                vk_ps4_free(alloc, dev);
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            queue->type = VK_PS4_OBJ_QUEUE;
            queue->device = dev;
            queue->family_index = qci->queueFamilyIndex;
            /* Set queue flags based on family index */
            if (qci->queueFamilyIndex == VK_PS4_QUEUE_FAMILY_GRAPHICS) {
                queue->flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
                               VK_QUEUE_TRANSFER_BIT;
            } else if (qci->queueFamilyIndex == VK_PS4_QUEUE_FAMILY_COMPUTE) {
                queue->flags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
            } else {
                queue->flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
                               VK_QUEUE_TRANSFER_BIT;
            }
            dev->queues[dev->queue_count++] = queue;
        }
    }

    *pDevice = (VkDevice)dev;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) {
    if (!device) {
        return;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    /* Tear down GNM state — release the init command buffer backing store
     * after the device is quiesced.  The GPU has no outstanding work at this
     * point because all queues were idle when the app called DestroyDevice. */
    vk_ps4_device_finish_gnm(dev);
    /* Free cached queues */
    for (uint32_t i = 0; i < dev->queue_count; i++) {
        if (dev->queues[i]) {
            vk_ps4_free(alloc, dev->queues[i]);
            dev->queues[i] = NULL;
        }
    }
    dev->queue_count = 0;
#ifdef VK_PS4_HAVE_PSBC
    psbc_shutdown();
#endif
    vk_ps4_free(alloc, dev);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetDeviceQueue(
    VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue
) {
    if (!device || !pQueue) {
        return;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    /* Return the cached queue handle (stable across calls) */
    uint32_t idx = 0;
    for (uint32_t i = 0; i < dev->queue_count; i++) {
        if (dev->queues[i] && dev->queues[i]->family_index == queueFamilyIndex) {
            if (idx == queueIndex) {
                *pQueue = (VkQueue)dev->queues[i];
                return;
            }
            idx++;
        }
    }
    *pQueue = VK_NULL_HANDLE;
}
