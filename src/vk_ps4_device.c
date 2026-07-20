/*
 * vk_ps4_device.c — VkDevice / VkQueue implementation (stub).
 */

#include "vk_ps4_internal.h"

#include <string.h>

#ifdef VK_PS4_HAVE_PSBC
#include "psbc_compile.h"
#endif

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
    dev->gnm_initialized = false; /* TODO: init GNM on PS4 */

#ifdef VK_PS4_HAVE_PSBC
    /* Initialize libpsbc once per device — refcounted internally.
     * This avoids calling psbc_init/psbc_shutdown on every shader compile. */
    psbc_init();
#endif

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
#ifdef VK_PS4_HAVE_PSBC
                psbc_shutdown();
#endif
                vk_ps4_free(alloc, dev);
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            queue->type = VK_PS4_OBJ_QUEUE;
            queue->device = dev;
            queue->family_index = qci->queueFamilyIndex;
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
    /* TODO: tear down GNM state */
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
