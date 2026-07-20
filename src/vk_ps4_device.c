/*
 * vk_ps4_device.c — VkDevice / VkQueue implementation (stub).
 */

#include "vk_ps4_internal.h"

#include <string.h>

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
    }
    dev->gnm_initialized = false; /* TODO: init GNM on PS4 */

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
    if (queueFamilyIndex != 0 || queueIndex != 0) {
        *pQueue = VK_NULL_HANDLE;
        return;
    }
    VkPs4Queue *queue = vk_ps4_alloc_zero(&dev->allocator, sizeof(*queue), 16);
    if (!queue) {
        *pQueue = VK_NULL_HANDLE;
        return;
    }
    queue->type = VK_PS4_OBJ_QUEUE;
    queue->device = dev;
    queue->family_index = 0;
    *pQueue = (VkQueue)queue;
}
