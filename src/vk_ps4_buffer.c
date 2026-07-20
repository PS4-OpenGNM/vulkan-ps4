/*
 * vk_ps4_buffer.c — VkBuffer implementation via GnmBuffer.
 */

#include "vk_ps4_internal.h"

#include <string.h>

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer) {
    if (!device || !pCreateInfo || !pBuffer) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pCreateInfo->sType != VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Buffer *buf = vk_ps4_alloc_zero(alloc, sizeof(*buf), 16);
    if (!buf) return VK_ERROR_OUT_OF_HOST_MEMORY;
    buf->type = VK_PS4_OBJ_BUFFER;
    buf->device = dev;
    buf->create_info = *pCreateInfo;
    buf->memory = NULL;
    buf->memory_offset = 0;

    /* Initialize GnmBuffer descriptor to zero — will be filled on BindBufferMemory */
    memset(&buf->gnm_buffer, 0, sizeof(buf->gnm_buffer));

    *pBuffer = (VkBuffer)buf;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator) {
    if (!device || !buffer) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, buf);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements) {
    (void)device;
    if (!buffer || !pMemoryRequirements) return;
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;
    /* GNM buffers need 4-byte alignment minimum, 256 for optimal GPU access */
    pMemoryRequirements->size = buf->create_info.size;
    pMemoryRequirements->alignment = 256;
    /* Buffers can live in either Onion or Garlic memory */
    pMemoryRequirements->memoryTypeBits = (1u << VK_PS4_MEMORY_TYPE_ONION) | (1u << VK_PS4_MEMORY_TYPE_GARLIC);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
    if (!device || !buffer || !memory) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;
    VkPs4DeviceMemory *mem = (VkPs4DeviceMemory *)memory;

    /* Overflow-safe bounds check */
    if (offset > mem->size || buf->create_info.size > mem->size - offset) {
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    buf->memory = mem;
    buf->memory_offset = offset;

    /* Set up the GnmBuffer descriptor with the GPU address */
    if (!mem->gnm_mem.mapped) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    void *gpu_addr = (char *)mem->gnm_mem.mapped + offset;
    sceGnmBufSetBaseAddress(&buf->gnm_buffer, gpu_addr);

    return VK_SUCCESS;
}
