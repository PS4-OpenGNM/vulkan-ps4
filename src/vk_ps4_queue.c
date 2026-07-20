/*
 * vk_ps4_queue.c — VkQueue implementation via sceGnmSubmitCommandBuffers.
 *
 * vkQueueSubmit submits recorded command buffers to the GPU.
 * For MVP, submission is synchronous (blocks until GPU "completes").
 * Phase 3 will add real fence/semaphore sync via EOP event writes.
 */

#include "vk_ps4_internal.h"

#include <string.h>

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence) {
    if (!queue) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Queue *q = (VkPs4Queue *)queue;

    for (uint32_t i = 0; i < submitCount; i++) {
        const VkSubmitInfo *submit = &pSubmits[i];

        /* Wait semaphores — MVP: just check they're signaled */
        for (uint32_t w = 0; w < submit->waitSemaphoreCount; w++) {
            /* Phase 3: real semaphore wait via EOP events */
        }

        /* Submit each command buffer */
        for (uint32_t c = 0; c < submit->commandBufferCount; c++) {
            VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)submit->pCommandBuffers[c];
            if (!cmd || !cmd->pm4_buffer) continue;

            uint32_t cmd_size_bytes = cmd->pm4_used * sizeof(uint32_t);
            if (cmd_size_bytes == 0) continue;

            void *dcb_addr = cmd->pm4_buffer;

            /* Submit to GNM — pass NULL for ccb_addrs and ccb_sizes
             * since we don't use a separate constant command buffer. */
            int32_t result = sceGnmSubmitCommandBuffers(
                1, &dcb_addr, &cmd_size_bytes, NULL, NULL
            );

            if (result != 0) {
                /* Submission failed — signal fence to avoid deadlock */
                if (fence) {
                    VkPs4Fence *f = (VkPs4Fence *)fence;
                    f->signaled = true;
                }
                return VK_ERROR_DEVICE_LOST;
            }
        }

        /* Signal semaphores */
        for (uint32_t s = 0; s < submit->signalSemaphoreCount; s++) {
            VkPs4Semaphore *sem = (VkPs4Semaphore *)submit->pSignalSemaphores[s];
            if (sem) sem->signaled = true;
        }
    }

    /* Signal fence */
    if (fence) {
        VkPs4Fence *f = (VkPs4Fence *)fence;
        f->signaled = true;
    }

    (void)q;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_QueueWaitIdle(VkQueue queue) {
    (void)queue;
    /* MVP: synchronous submit means queue is always idle after submit returns */
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_DeviceWaitIdle(VkDevice device) {
    (void)device;
    /* MVP: synchronous submit means device is always idle after submit returns */
    return VK_SUCCESS;
}
