/*
 * vk_ps4_sync.c — VkFence / VkSemaphore / VkEvent implementation.
 *
 * For Phase 1 (MVP), these are simple CPU-tracked booleans.
 * Phase 3 will replace fences with EOP event writes for real GPU sync.
 */

#include "vk_ps4_internal.h"

#include <string.h>

/* === Fence === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkFence *pFence) {
    if (!device || !pCreateInfo || !pFence) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Fence *fence = vk_ps4_alloc_zero(alloc, sizeof(*fence), 16);
    if (!fence) return VK_ERROR_OUT_OF_HOST_MEMORY;
    fence->type = VK_PS4_OBJ_FENCE;
    fence->device = dev;
    fence->signaled = (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0;
    *pFence = (VkFence)fence;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator) {
    if (!device || !fence) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Fence *f = (VkPs4Fence *)fence;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, f);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences,
                     VkBool32 waitAll, uint64_t timeout) {
    (void)device;
    (void)timeout;
    /* MVP: all fences are signaled immediately after QueueSubmit (synchronous submit).
     * Just check the signaled flag. */
    uint32_t signaled_count = 0;
    for (uint32_t i = 0; i < fenceCount; i++) {
        if (!pFences[i]) continue;
        VkPs4Fence *f = (VkPs4Fence *)pFences[i];
        if (f->signaled) {
            signaled_count++;
        }
    }

    if (waitAll) {
        return (signaled_count == fenceCount) ? VK_SUCCESS : VK_TIMEOUT;
    } else {
        /* wait-any: succeed if at least one is signaled */
        return (signaled_count > 0) ? VK_SUCCESS : VK_TIMEOUT;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences) {
    (void)device;
    for (uint32_t i = 0; i < fenceCount; i++) {
        if (!pFences[i]) continue;
        VkPs4Fence *f = (VkPs4Fence *)pFences[i];
        f->signaled = false;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_GetFenceStatus(VkDevice device, VkFence fence) {
    (void)device;
    if (!fence) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Fence *f = (VkPs4Fence *)fence;
    return f->signaled ? VK_SUCCESS : VK_NOT_READY;
}

/* === Semaphore === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore) {
    if (!device || !pCreateInfo || !pSemaphore) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Semaphore *sem = vk_ps4_alloc_zero(alloc, sizeof(*sem), 16);
    if (!sem) return VK_ERROR_OUT_OF_HOST_MEMORY;
    sem->type = VK_PS4_OBJ_SEMAPHORE;
    sem->device = dev;
    sem->signaled = false;
    *pSemaphore = (VkSemaphore)sem;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator) {
    if (!device || !semaphore) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Semaphore *sem = (VkPs4Semaphore *)semaphore;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, sem);
}

/* === Event === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateEvent(VkDevice device, const VkEventCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkEvent *pEvent) {
    if (!device || !pCreateInfo || !pEvent) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Event *ev = vk_ps4_alloc_zero(alloc, sizeof(*ev), 16);
    if (!ev) return VK_ERROR_OUT_OF_HOST_MEMORY;
    ev->type = VK_PS4_OBJ_EVENT;
    ev->device = dev;
    ev->signaled = false;
    *pEvent = (VkEvent)ev;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks *pAllocator) {
    if (!device || !event) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Event *ev = (VkPs4Event *)event;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, ev);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_GetEventStatus(VkDevice device, VkEvent event) {
    (void)device;
    if (!event) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Event *ev = (VkPs4Event *)event;
    return ev->signaled ? VK_EVENT_SET : VK_EVENT_RESET;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_SetEvent(VkDevice device, VkEvent event) {
    (void)device;
    if (!event) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Event *ev = (VkPs4Event *)event;
    ev->signaled = true;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_ResetEvent(VkDevice device, VkEvent event) {
    (void)device;
    if (!event) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Event *ev = (VkPs4Event *)event;
    ev->signaled = false;
    return VK_SUCCESS;
}
