/*
 * vk_ps4_command.c — VkCommandBuffer implementation via GnmCommandBuffer.
 *
 * vkBeginCommandBuffer allocates PM4 buffer and inits default hardware state.
 * vkCmdBindPipeline emits SetVsShader/SetPsShader + state registers.
 * vkCmdSetViewport / SetScissor emit corresponding PM4 packets.
 * vkCmdDraw emits DrawIndexAuto.
 * vkCmdBeginRenderPass emits SetRenderTarget + clear.
 * vkCmdEndRenderPass emits EOP event.
 * vkEndCommandBuffer finalizes the PM4 stream.
 */

#include "vk_ps4_internal.h"

#include <string.h>

#define VK_PS4_CMD_BUFFER_SIZE (256 * 1024)  /* 256KB default PM4 buffer */

/* Forward declaration from vk_ps4_pipeline.c */
extern GnmPrimitiveType vk_topology_to_gnm(VkPrimitiveTopology topology);

/* === Command pool === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool) {
    if (!device || !pCreateInfo || !pCommandPool) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4CommandPool *pool = vk_ps4_alloc_zero(alloc, sizeof(*pool), 16);
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->type = VK_PS4_OBJ_COMMAND_POOL;
    pool->device = dev;
    pool->queue_family_index = pCreateInfo->queueFamilyIndex;
    pool->flags = pCreateInfo->flags;
    *pCommandPool = (VkCommandPool)pool;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator) {
    if (!device || !commandPool) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4CommandPool *pool = (VkPs4CommandPool *)commandPool;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    /* Free all command buffers allocated from this pool */
    for (uint32_t i = 0; i < pool->command_buffer_count; i++) {
        VkPs4CommandBuffer *cmd = pool->command_buffers[i];
        if (cmd) {
            if (cmd->pm4_buffer) vk_ps4_free(alloc, cmd->pm4_buffer);
            vk_ps4_free(alloc, cmd);
            pool->command_buffers[i] = NULL;
        }
    }
    pool->command_buffer_count = 0;
    vk_ps4_free(alloc, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
                              VkCommandBuffer *pCommandBuffers) {
    if (!device || !pAllocateInfo || !pCommandBuffers) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4CommandPool *pool = (VkPs4CommandPool *)pAllocateInfo->commandPool;

    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
        VkPs4CommandBuffer *cmd = vk_ps4_alloc_zero(alloc, sizeof(*cmd), 16);
        if (!cmd) {
            for (uint32_t j = 0; j < i; j++) {
                VkPs4CommandBuffer *c = (VkPs4CommandBuffer *)pCommandBuffers[j];
                if (c->pm4_buffer) vk_ps4_free(alloc, c->pm4_buffer);
                vk_ps4_free(alloc, c);
            }
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        cmd->type = VK_PS4_OBJ_COMMAND_BUFFER;
        cmd->device = dev;
        cmd->pool = pool;
        cmd->level = pAllocateInfo->level;
        cmd->is_recording = false;
        cmd->is_begin = false;
        cmd->current_pipeline = NULL;
        cmd->vertex_binding_count = 0;

        /* Allocate PM4 buffer */
        cmd->pm4_buffer_size = VK_PS4_CMD_BUFFER_SIZE / sizeof(uint32_t);
        cmd->pm4_buffer = vk_ps4_alloc_zero(alloc, cmd->pm4_buffer_size * sizeof(uint32_t), 256);
        if (!cmd->pm4_buffer) {
            vk_ps4_free(alloc, cmd);
            for (uint32_t j = 0; j < i; j++) {
                VkPs4CommandBuffer *c = (VkPs4CommandBuffer *)pCommandBuffers[j];
                if (c->pm4_buffer) vk_ps4_free(alloc, c->pm4_buffer);
                vk_ps4_free(alloc, c);
            }
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        cmd->pm4_used = 0;

        /* Register in pool for cleanup */
        if (pool && pool->command_buffer_count < VK_PS4_MAX_COMMAND_BUFFERS_PER_POOL) {
            pool->command_buffers[pool->command_buffer_count++] = cmd;
        }

        pCommandBuffers[i] = (VkCommandBuffer)cmd;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                          uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers) {
    if (!device || !pCommandBuffers) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4CommandPool *pool = (VkPs4CommandPool *)commandPool;

    for (uint32_t i = 0; i < commandBufferCount; i++) {
        if (!pCommandBuffers[i]) continue;
        VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)pCommandBuffers[i];

        /* Remove from pool's tracking array to avoid double-free in DestroyCommandPool */
        if (pool) {
            for (uint32_t j = 0; j < pool->command_buffer_count; j++) {
                if (pool->command_buffers[j] == cmd) {
                    pool->command_buffers[j] = NULL;
                    /* Compact: move last element into the gap */
                    if (j < pool->command_buffer_count - 1) {
                        pool->command_buffers[j] = pool->command_buffers[pool->command_buffer_count - 1];
                        pool->command_buffers[pool->command_buffer_count - 1] = NULL;
                    }
                    pool->command_buffer_count--;
                    break;
                }
            }
        }

        if (cmd->pm4_buffer) vk_ps4_free(alloc, cmd->pm4_buffer);
        vk_ps4_free(alloc, cmd);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo) {
    (void)pBeginInfo;
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (!cmd->pm4_buffer) return VK_ERROR_INITIALIZATION_FAILED;

    /* Initialize GnmCommandBuffer with the PM4 buffer */
    cmd->gnm_cmd = sceGnmCmdInit(
        cmd->pm4_buffer, cmd->pm4_buffer_size * sizeof(uint32_t), NULL, NULL
    );

    /* Emit default hardware state */
    sceGnmDrawCmdInitDefaultHardwareState(&cmd->gnm_cmd);

    cmd->is_recording = true;
    cmd->is_begin = true;
    cmd->pm4_used = (uint32_t)(cmd->gnm_cmd.cmdptr - cmd->gnm_cmd.beginptr);
    /* Reset all tracking state */
    cmd->current_pipeline = NULL;
    cmd->vertex_binding_count = 0;
    memset(&cmd->index_buffer, 0, sizeof(cmd->index_buffer));
    memset(cmd->vertex_buffers, 0, sizeof(cmd->vertex_buffers));
    memset(&cmd->current_render_pass, 0, sizeof(cmd->current_render_pass));

    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_EndCommandBuffer(VkCommandBuffer commandBuffer) {
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    cmd->is_recording = false;
    cmd->pm4_used = (uint32_t)(cmd->gnm_cmd.cmdptr - cmd->gnm_cmd.beginptr);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) {
    (void)flags;
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (cmd->pm4_buffer) {
        sceGnmCmdReset(&cmd->gnm_cmd);
    }
    cmd->pm4_used = 0;
    cmd->is_recording = false;
    /* Reset all tracking state */
    cmd->current_pipeline = NULL;
    cmd->vertex_binding_count = 0;
    memset(&cmd->index_buffer, 0, sizeof(cmd->index_buffer));
    memset(cmd->vertex_buffers, 0, sizeof(cmd->vertex_buffers));
    memset(&cmd->current_render_pass, 0, sizeof(cmd->current_render_pass));
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) {
    (void)device;
    (void)commandPool;
    (void)flags;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_TrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) {
    (void)device;
    (void)commandPool;
    (void)flags;
}

/* === Command buffer recording === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
    if (!commandBuffer || !pipeline) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Pipeline *pipe = (VkPs4Pipeline *)pipeline;
    cmd->current_pipeline = pipe;

    if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        /* Set primitive type */
        sceGnmDrawCmdSetPrimitiveType(&cmd->gnm_cmd,
            vk_topology_to_gnm(pipe->input_assembly_state.topology));

        /* Set vertex shader */
        sceGnmDrawCmdSetVsShader(&cmd->gnm_cmd, &pipe->vs_regs, 0);

        /* Set pixel shader */
        sceGnmDrawCmdSetPsShader(&cmd->gnm_cmd, &pipe->ps_regs);
    } else if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
        sceGnmDrawCmdSetCsShader(&cmd->gnm_cmd, &pipe->cs_regs);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports) {
    if (!commandBuffer || !pViewports) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* GNM viewport: scale/offset maps Vulkan viewport to GNM */
    for (uint32_t i = 0; i < viewportCount; i++) {
        const VkViewport *vp = &pViewports[i];
        GnmSetViewportInfo vp_info;
        vp_info.dmin = vp->minDepth;
        vp_info.dmax = vp->maxDepth;
        /* Vulkan viewport: x, y is top-left, width/height extend right/down
         * GNM viewport: scale = half-dimension, offset = center */
        vp_info.scale[0] = vp->width * 0.5f;
        vp_info.scale[1] = vp->height * 0.5f;
        vp_info.scale[2] = vp->maxDepth - vp->minDepth;
        vp_info.offset[0] = vp->x + vp->width * 0.5f;
        vp_info.offset[1] = vp->y + vp->height * 0.5f;
        vp_info.offset[2] = vp->minDepth;
        sceGnmDrawCmdSetViewport(&cmd->gnm_cmd, firstViewport + i, &vp_info);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors) {
    if (!commandBuffer || !pScissors) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    for (uint32_t i = 0; i < scissorCount; i++) {
        const VkRect2D *sc = &pScissors[i];
        sceGnmDrawCmdSetScreenScissor(&cmd->gnm_cmd,
            sc->offset.x, sc->offset.y,
            sc->offset.x + sc->extent.width,
            sc->offset.y + sc->extent.height);
    }
}

/* CmdBindDescriptorSets moved to vk_ps4_descriptor.c */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                             uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets) {
    if (!commandBuffer || !pBuffers) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    for (uint32_t i = 0; i < bindingCount; i++) {
        uint32_t idx = firstBinding + i;
        if (idx < VK_PS4_MAX_VERTEX_BINDINGS) {
            cmd->vertex_buffers[idx].buffer = pBuffers[i];
            cmd->vertex_buffers[idx].offset = pOffsets ? pOffsets[i] : 0;
        }
    }
    if (firstBinding + bindingCount > cmd->vertex_binding_count) {
        cmd->vertex_binding_count = firstBinding + bindingCount;
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    cmd->index_buffer.buffer = buffer;
    cmd->index_buffer.offset = offset;
    cmd->index_buffer.type = indexType;

    /* Set index buffer in GNM */
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;
    if (buf && buf->memory && buf->memory->gnm_mem.mapped) {
        void *gpu_addr = (char *)buf->memory->gnm_mem.mapped + buf->memory_offset + offset;
        sceGnmDrawCmdSetIndexBuffer(&cmd->gnm_cmd, gpu_addr);

        GnmIndexSize idx_size;
        switch (indexType) {
        case VK_INDEX_TYPE_UINT16: idx_size = GNM_INDEX_16; break;
        case VK_INDEX_TYPE_UINT32: idx_size = GNM_INDEX_32; break;
        default: idx_size = GNM_INDEX_16; break;
        }
        sceGnmDrawCmdSetIndexSize(&cmd->gnm_cmd, idx_size, GNM_POLICY_BYPASS);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount,
               uint32_t firstVertex, uint32_t firstInstance) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* Always set instance count to avoid state leak from previous draw */
    sceGnmDrawCmdSetNumInstances(&cmd->gnm_cmd, instanceCount);

    /* DrawIndexAuto draws vertexCount vertices using auto-generated indices
     * starting from firstVertex. For firstVertex > 0, we need DrawIndexOffset
     * or an actual index buffer. For the MVP, use DrawIndexAuto. */
    (void)firstVertex;
    (void)firstInstance;
    sceGnmDrawCmdDrawIndexAuto(&cmd->gnm_cmd, vertexCount);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
                      uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* Always set instance count to avoid state leak from previous draw */
    sceGnmDrawCmdSetNumInstances(&cmd->gnm_cmd, instanceCount);

    /* If we have an index buffer bound, use DrawIndex */
    VkPs4Buffer *buf = (VkPs4Buffer *)cmd->index_buffer.buffer;
    if (buf && buf->memory && buf->memory->gnm_mem.mapped) {
        void *gpu_addr = (char *)buf->memory->gnm_mem.mapped + buf->memory_offset +
                         cmd->index_buffer.offset;
        /* Adjust for firstIndex based on index size */
        uint32_t index_stride = (cmd->index_buffer.type == VK_INDEX_TYPE_UINT32) ? 4 : 2;
        void *index_addr = (char *)gpu_addr + (uint64_t)firstIndex * index_stride;
        sceGnmDrawCmdDrawIndex(&cmd->gnm_cmd, indexCount, index_addr);
    } else {
        /* Fallback: auto-draw */
        sceGnmDrawCmdDrawIndexAuto(&cmd->gnm_cmd, indexCount);
    }
    (void)vertexOffset;
    (void)firstInstance;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                       uint32_t drawCount, uint32_t stride) {
    (void)commandBuffer;
    (void)buffer;
    (void)offset;
    (void)drawCount;
    (void)stride;
    /* Phase 3 */
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                              uint32_t drawCount, uint32_t stride) {
    (void)commandBuffer;
    (void)buffer;
    (void)offset;
    (void)drawCount;
    (void)stride;
    /* Phase 3 */
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    sceGnmDrawCmdDispatchDirect(&cmd->gnm_cmd, x, y, z, 0);
}

/* === Copy/blit commands (Phase 2) === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer,
                     uint32_t regionCount, const VkBufferCopy *pRegions) {
    if (!commandBuffer || !pRegions) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Buffer *src = (VkPs4Buffer *)srcBuffer;
    VkPs4Buffer *dst = (VkPs4Buffer *)dstBuffer;

    if (!src || !dst || !src->memory || !dst->memory) return;
    if (!src->memory->gnm_mem.mapped || !dst->memory->gnm_mem.mapped) return;

    for (uint32_t i = 0; i < regionCount; i++) {
        uint64_t src_addr = (uint64_t)((char *)src->memory->gnm_mem.mapped +
                                        src->memory_offset + pRegions[i].srcOffset);
        uint64_t dst_addr = (uint64_t)((char *)dst->memory->gnm_mem.mapped +
                                        dst->memory_offset + pRegions[i].dstOffset);
        /* sceGnmDrawCmdCopyMemory takes uint32_t size — split large copies */
        uint64_t remaining = pRegions[i].size;
        uint64_t cur_src = src_addr;
        uint64_t cur_dst = dst_addr;
        while (remaining > 0) {
            uint32_t chunk = (remaining > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)remaining;
            sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd, cur_dst, cur_src, chunk);
            cur_src += chunk;
            cur_dst += chunk;
            remaining -= chunk;
        }
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                    VkImage dstImage, VkImageLayout dstImageLayout,
                    uint32_t regionCount, const VkImageCopy *pRegions) {
    /* Phase 2 */
    (void)commandBuffer;
    (void)srcImage;
    (void)srcImageLayout;
    (void)dstImage;
    (void)dstImageLayout;
    (void)regionCount;
    (void)pRegions;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                    VkImage dstImage, VkImageLayout dstImageLayout,
                    uint32_t regionCount, const VkImageBlit *pRegions, VkFilter filter) {
    /* Phase 2 */
    (void)commandBuffer;
    (void)srcImage;
    (void)srcImageLayout;
    (void)dstImage;
    (void)dstImageLayout;
    (void)regionCount;
    (void)pRegions;
    (void)filter;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                            VkImageLayout dstImageLayout, uint32_t regionCount,
                            const VkBufferImageCopy *pRegions) {
    /* Phase 2 */
    (void)commandBuffer;
    (void)srcBuffer;
    (void)dstImage;
    (void)dstImageLayout;
    (void)regionCount;
    (void)pRegions;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                            VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy *pRegions) {
    /* Phase 2 */
    (void)commandBuffer;
    (void)srcImage;
    (void)srcImageLayout;
    (void)dstBuffer;
    (void)regionCount;
    (void)pRegions;
}

/* === Render pass commands === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pBeginInfo,
                          VkSubpassContents contents) {
    (void)contents;
    if (!commandBuffer || !pBeginInfo) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4RenderPass *rp = (VkPs4RenderPass *)pBeginInfo->renderPass;
    VkPs4Framebuffer *fb = (VkPs4Framebuffer *)pBeginInfo->framebuffer;

    if (!rp || !fb) return;

    cmd->current_render_pass.pass = rp;
    cmd->current_render_pass.framebuffer = fb;
    cmd->current_render_pass.render_area = pBeginInfo->renderArea;

    /* Set render targets from framebuffer attachments */
    for (uint32_t i = 0; i < fb->attachment_count && i < 8; i++) {
        VkPs4ImageView *view = fb->attachments[i];
        if (!view || !view->image) continue;

        if (view->image->is_render_target) {
            sceGnmDrawCmdSetRenderTarget(&cmd->gnm_cmd, i, &view->image->gnm_rt);
        }
    }

    /* Wait until safe for rendering on the video out handle */
    /* TODO: if this is a swapchain render, call sceGnmDrawCmdWaitUntilSafeForRendering */

    /* Clear attachments if specified */
    if (pBeginInfo->clearValueCount > 0 && pBeginInfo->pClearValues) {
        for (uint32_t i = 0; i < pBeginInfo->clearValueCount && i < fb->attachment_count; i++) {
            VkPs4ImageView *view = fb->attachments[i];
            if (!view || !view->image) continue;

            if (i < rp->attachment_count) {
                VkAttachmentLoadOp load_op = rp->attachments[i].loadOp;
                if (load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
                    /* For color attachments, use fill memory or clear RT.
                     * For MVP, we use a simple clear via PM4. */
                    if (view->image->is_render_target) {
                        const VkClearColorValue *clear_color = &pBeginInfo->pClearValues[i].color;
                        /* TODO: emit proper clear packet */
                        (void)clear_color;
                    }
                }
            }
        }
    }

    /* Set scissor to render area */
    sceGnmDrawCmdSetScreenScissor(&cmd->gnm_cmd,
        pBeginInfo->renderArea.offset.x,
        pBeginInfo->renderArea.offset.y,
        pBeginInfo->renderArea.offset.x + pBeginInfo->renderArea.extent.width,
        pBeginInfo->renderArea.offset.y + pBeginInfo->renderArea.extent.height);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdEndRenderPass(VkCommandBuffer commandBuffer) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* Emit EOP event to signal completion */
    sceGnmDrawCmdEventWriteEop(&cmd->gnm_cmd,
        GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0,
        GNM_DATA_SEL_DISCARD, 0);

    cmd->current_render_pass.pass = NULL;
    cmd->current_render_pass.framebuffer = NULL;
}

/* === Barriers === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                          VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                          uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
                          uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                          uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* For MVP, emit a cache flush EOP event as a full barrier */
    sceGnmDrawCmdEventWriteEop(&cmd->gnm_cmd,
        GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0,
        GNM_DATA_SEL_DISCARD, 0);

    (void)srcStageMask;
    (void)dstStageMask;
    (void)dependencyFlags;
    (void)memoryBarrierCount;
    (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount;
    (void)pBufferMemoryBarriers;
    (void)imageMemoryBarrierCount;
    (void)pImageMemoryBarriers;
}

/* === Clear commands === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                          const VkClearColorValue *pColor, uint32_t rangeCount,
                          const VkImageSubresourceRange *pRanges) {
    /* Phase 3: proper clear via PM4 */
    (void)commandBuffer;
    (void)image;
    (void)imageLayout;
    (void)pColor;
    (void)rangeCount;
    (void)pRanges;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                                 const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount,
                                 const VkImageSubresourceRange *pRanges) {
    /* Phase 3 */
    (void)commandBuffer;
    (void)image;
    (void)imageLayout;
    (void)pDepthStencil;
    (void)rangeCount;
    (void)pRanges;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                           const VkClearAttachment *pAttachments, uint32_t rectCount,
                           const VkClearRect *pRects) {
    /* Phase 3 */
    (void)commandBuffer;
    (void)attachmentCount;
    (void)pAttachments;
    (void)rectCount;
    (void)pRects;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                        VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size,
                        const void *pValues) {
    /* Phase 2: push constants via inline user data */
    (void)commandBuffer;
    (void)layout;
    (void)stageFlags;
    (void)offset;
    (void)size;
    (void)pValues;
}

/* === Query commands (Phase 3) === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool) {
    (void)device; (void)pCreateInfo; (void)pAllocator; (void)pQueryPool;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks *pAllocator) {
    (void)device; (void)queryPool; (void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery,
                           uint32_t queryCount, size_t dataSize, void *pData,
                           VkDeviceSize stride, VkQueryResultFlags flags) {
    (void)device; (void)queryPool; (void)firstQuery; (void)queryCount;
    (void)dataSize; (void)pData; (void)stride; (void)flags;
    return VK_NOT_READY;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                         uint32_t firstQuery, uint32_t queryCount) {
    (void)commandBuffer; (void)queryPool; (void)firstQuery; (void)queryCount;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags) {
    (void)commandBuffer; (void)queryPool; (void)query; (void)flags;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) {
    (void)commandBuffer; (void)queryPool; (void)query;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage,
                         VkQueryPool queryPool, uint32_t query) {
    (void)commandBuffer; (void)pipelineStage; (void)queryPool; (void)query;
}
