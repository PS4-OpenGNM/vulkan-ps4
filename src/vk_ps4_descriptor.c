/*
 * vk_ps4_descriptor.c — Vulkan descriptor sets over GNM user-data registers.
 *
 * Vulkan descriptor sets map to GNM user-data register slots:
 *   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER   → GnmBuffer (Vsharp, 4 regs)
 *   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER   → GnmBuffer (Vsharp, 4 regs)
 *   VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE    → GnmTexture (Tsharp, 8 regs)
 *   VK_DESCRIPTOR_TYPE_STORAGE_IMAGE    → GnmTexture (Tsharp, 8 regs)
 *   VK_DESCRIPTOR_TYPE_SAMPLER          → GnmSampler (Ssharp, 4 regs)
 *   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER → Tsharp + Ssharp
 *   VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER   → GnmBuffer (Vsharp)
 *   VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER   → GnmBuffer (Vsharp)
 *
 * The shader binary's GnmInputUsageSlot table (embedded by psbc) maps
 * Vulkan binding numbers (apislot) to GNM user-data registers
 * (startregister). At CmdBindDescriptorSets, we iterate the pipeline's
 * slot tables and emit Set*UserData PM4 commands.
 */

#include "vk_ps4_internal.h"
#include <string.h>
#include <stdio.h>

/* === Descriptor set layout === */
/* (Already implemented in vk_ps4_pipeline.c) */

/* === Descriptor pool === */
/* (Already implemented in vk_ps4_pipeline.c) */

/* === Descriptor set allocation === */
/* (Already implemented in vk_ps4_pipeline.c — but we need to initialize
 * the binding storage here. See vk_ps4_AllocateDescriptorSets below. */

/* === Helper: get GnmShaderStage from Vulkan bind point === */
static GnmShaderStage vk_bind_point_to_gnm_stage(VkPipelineBindPoint bp) {
    switch (bp) {
    case VK_PIPELINE_BIND_POINT_GRAPHICS: return GNM_STAGE_VS;
    case VK_PIPELINE_BIND_POINT_COMPUTE:  return GNM_STAGE_CS;
    default: return GNM_STAGE_VS;
    }
}

/* === Helper: find a binding in a descriptor set by binding number === */
static VkPs4DescriptorBinding *find_binding(VkPs4DescriptorSet *set, uint32_t binding) {
    if (!set || binding >= set->binding_count) return NULL;
    return &set->bindings[binding];
}

/* === Helper: allocate resource arrays for a binding === */
static VkResult alloc_binding_resources(VkPs4DescriptorBinding *b,
                                        const VkAllocationCallbacks *alloc) {
    if (b->count == 0) return VK_SUCCESS;
    switch (b->type) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        b->buffers = vk_ps4_alloc_zero(alloc, b->count * sizeof(GnmBuffer), 16);
        b->textures = NULL;
        b->samplers = NULL;
        if (!b->buffers) return VK_ERROR_OUT_OF_HOST_MEMORY;
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        b->samplers = vk_ps4_alloc_zero(alloc, b->count * sizeof(GnmSampler), 16);
        b->buffers = NULL;
        b->textures = NULL;
        if (!b->samplers) return VK_ERROR_OUT_OF_HOST_MEMORY;
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        b->textures = vk_ps4_alloc_zero(alloc, b->count * sizeof(GnmTexture), 16);
        b->buffers = NULL;
        b->samplers = NULL;
        if (!b->textures) return VK_ERROR_OUT_OF_HOST_MEMORY;
        break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        b->textures = vk_ps4_alloc_zero(alloc, b->count * sizeof(GnmTexture), 16);
        if (!b->textures) return VK_ERROR_OUT_OF_HOST_MEMORY;
        b->samplers = vk_ps4_alloc_zero(alloc, b->count * sizeof(GnmSampler), 16);
        if (!b->samplers) {
            vk_ps4_free(alloc, b->textures);
            b->textures = NULL;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        b->buffers = NULL;
        break;
    default:
        /* Unsupported descriptor type */
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    return VK_SUCCESS;
}

/* === Helper: free resource arrays for a binding === */
static void free_binding_resources(VkPs4DescriptorBinding *b,
                                   const VkAllocationCallbacks *alloc) {
    if (b->buffers) { vk_ps4_free(alloc, b->buffers); b->buffers = NULL; }
    if (b->textures) { vk_ps4_free(alloc, b->textures); b->textures = NULL; }
    if (b->samplers) { vk_ps4_free(alloc, b->samplers); b->samplers = NULL; }
}

/* === Helper: build a GnmBuffer from a VkDescriptorBufferInfo === */
static void build_buffer_descriptor(GnmBuffer *gnm_buf, VkPs4Buffer *vk_buf,
                                     VkDeviceSize offset, VkDeviceSize range,
                                     VkDescriptorType desc_type) {
    if (!vk_buf || !vk_buf->memory || !vk_buf->memory->gnm_mem.mapped) {
        memset(gnm_buf, 0, sizeof(*gnm_buf));
        return;
    }

    void *gpu_addr = (char *)vk_buf->memory->gnm_mem.mapped +
                     vk_buf->memory_offset + offset;

    /* Use sceGnmCreateConstBuffer as a base for UBOs, or create a raw
     * buffer for SSBOs. For MVP, both use the same const buffer path. */
    uint32_t size = (range == VK_WHOLE_SIZE) ?
        (uint32_t)(vk_buf->create_info.size - offset) : (uint32_t)range;

    *gnm_buf = sceGnmCreateConstBuffer(gpu_addr, size);

    /* For storage buffers, set stride to 0 (raw buffer) */
    if (desc_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
        desc_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
        gnm_buf->stride = 0;
    }
}

/* === Helper: build a GnmSampler from VkSamplerCreateInfo fields === */
static void build_sampler_descriptor(GnmSampler *s, const VkSamplerCreateInfo *ci) {
    memset(s, 0, sizeof(*s));

    /* Clamp modes */
    static const GnmTexClamp vk_to_gnm_clamp[] = {
        [VK_SAMPLER_ADDRESS_MODE_REPEAT] = 0,          /* WRAP */
        [VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT] = 1,  /* MIRROR */
        [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE] = 2,    /* CLAMP_LAST */
        [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER] = 3,  /* CLAMP_BORDER */
        [VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE] = 4, /* MIRROR_ONCE_LAST */
    };
    if (ci->addressModeU < sizeof(vk_to_gnm_clamp)/sizeof(vk_to_gnm_clamp[0]))
        s->clampx = vk_to_gnm_clamp[ci->addressModeU];
    if (ci->addressModeV < sizeof(vk_to_gnm_clamp)/sizeof(vk_to_gnm_clamp[0]))
        s->clampy = vk_to_gnm_clamp[ci->addressModeV];
    if (ci->addressModeW < sizeof(vk_to_gnm_clamp)/sizeof(vk_to_gnm_clamp[0]))
        s->clampz = vk_to_gnm_clamp[ci->addressModeW];

    /* Filter modes */
    s->filtermode = 0; /* 0 = sync, 1 = async */
    if (ci->magFilter == VK_FILTER_LINEAR) {
        s->xymagfilter = 1; /* LINEAR */
    } else {
        s->xymagfilter = 0; /* POINT */
    }
    if (ci->minFilter == VK_FILTER_LINEAR) {
        s->xyminfilter = 1;
    } else {
        s->xyminfilter = 0;
    }
    if (ci->mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR) {
        s->mipfilter = 1; /* LINEAR */
    } else {
        s->mipfilter = 0; /* NONE/POINT */
    }

    /* LOD */
    s->minlod = (uint32_t)(ci->minLod * 256.0f) & 0xFFF;
    s->maxlod = (uint32_t)(ci->maxLod * 256.0f) & 0xFFF;
    s->lodbias = (uint32_t)(int32_t)(ci->mipLodBias * 256.0f) & 0x3FFF;

    /* Anisotropic filtering */
    if (ci->anisotropyEnable) {
        s->maxanisoratio = ci->maxAnisotropy > 0 ? (uint32_t)ci->maxAnisotropy : 1;
        if (s->maxanisoratio > 7) s->maxanisoratio = 7;  /* 3-bit field, max 7 */
    }

    /* Compare */
    if (ci->compareEnable) {
        s->depthcomparefunc = 0; /* NEVER — simplified; full mapping is Phase 3 */
    } else {
        s->depthcomparefunc = 0;
    }

    /* Border color */
    if (ci->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
        ci->addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
        ci->addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER) {
        s->bordercolortype = 0; /* OPAQUE_BLACK — simplified */
    }

    /* Force normalized coordinates (Vulkan always uses normalized) */
    s->forceunormalized = 0;
}

/* === vkUpdateDescriptorSets === */
VKAPI_ATTR void VKAPI_CALL
vk_ps4_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                            const VkWriteDescriptorSet *pDescriptorWrites,
                            uint32_t descriptorCopyCount,
                            const VkCopyDescriptorSet *pDescriptorCopies) {
    if (!device) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;

    /* Process writes */
    for (uint32_t i = 0; i < descriptorWriteCount; i++) {
        const VkWriteDescriptorSet *w = &pDescriptorWrites[i];
        VkPs4DescriptorSet *set = (VkPs4DescriptorSet *)w->dstSet;
        if (!set) continue;

        uint32_t binding_idx = w->dstBinding;
        if (binding_idx >= set->binding_count) continue;

        VkPs4DescriptorBinding *b = &set->bindings[binding_idx];

        /* Ensure the binding has resource arrays allocated.
         * AllocateDescriptorSets sets type/count but doesn't allocate
         * the resource arrays — that happens lazily here. */
        bool needs_alloc = false;
        if (b->count == 0 || b->type != w->descriptorType) {
            needs_alloc = true;
        } else {
            switch (w->descriptorType) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                if (!b->buffers) needs_alloc = true;
                break;
            case VK_DESCRIPTOR_TYPE_SAMPLER:
                if (!b->samplers) needs_alloc = true;
                break;
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                if (!b->textures) needs_alloc = true;
                break;
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                if (!b->textures || !b->samplers) needs_alloc = true;
                break;
            default: break;
            }
        }
        if (needs_alloc) {
            free_binding_resources(b, alloc);
            b->type = w->descriptorType;
            b->count = w->descriptorCount;
            if (alloc_binding_resources(b, alloc) != VK_SUCCESS) {
                b->count = 0;
                continue;
            }
        }

        uint32_t dst_start = w->dstArrayElement;
        uint32_t count = w->descriptorCount;
        if (dst_start + count > b->count) {
            count = b->count - dst_start;
        }

        switch (w->descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
            for (uint32_t j = 0; j < count; j++) {
                const VkDescriptorBufferInfo *bi = &w->pBufferInfo[j];
                VkPs4Buffer *vk_buf = (VkPs4Buffer *)bi->buffer;
                if (!vk_buf) {
                    memset(&b->buffers[dst_start + j], 0, sizeof(GnmBuffer));
                    continue;
                }
                build_buffer_descriptor(&b->buffers[dst_start + j], vk_buf,
                                        bi->offset, bi->range, w->descriptorType);
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
            for (uint32_t j = 0; j < count; j++) {
                const VkDescriptorImageInfo *ii = &w->pImageInfo[j];
                VkPs4ImageView *view = (VkPs4ImageView *)ii->imageView;
                if (view && view->image) {
                    if (!view->image->is_render_target) {
                        b->textures[dst_start + j] = view->gnm_view;
                    } else {
                        /* Render target used as texture — copy RT to texture
                         * descriptor. Full RT-as-texture is Phase 3. */
                        memset(&b->textures[dst_start + j], 0, sizeof(GnmTexture));
                    }
                } else {
                    memset(&b->textures[dst_start + j], 0, sizeof(GnmTexture));
                }
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_SAMPLER: {
            for (uint32_t j = 0; j < count; j++) {
                const GnmSampler *s = (const GnmSampler *)w->pImageInfo[j].sampler;
                if (s) {
                    b->samplers[dst_start + j] = *s;
                } else {
                    memset(&b->samplers[dst_start + j], 0, sizeof(GnmSampler));
                }
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
            for (uint32_t j = 0; j < count; j++) {
                const VkDescriptorImageInfo *ii = &w->pImageInfo[j];
                VkPs4ImageView *view = (VkPs4ImageView *)ii->imageView;
                const GnmSampler *s = (const GnmSampler *)ii->sampler;
                if (view && view->image && !view->image->is_render_target) {
                    b->textures[dst_start + j] = view->gnm_view;
                } else {
                    memset(&b->textures[dst_start + j], 0, sizeof(GnmTexture));
                }
                if (s) {
                    b->samplers[dst_start + j] = *s;
                } else {
                    memset(&b->samplers[dst_start + j], 0, sizeof(GnmSampler));
                }
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            /* Texel buffer views not implemented in MVP — would need
             * VkCreateBufferView and GnmBuffer descriptor from the view. */
            break;
        default:
            break;
        }
    }

    /* Process copies (simplified — just copy raw resource data) */
    for (uint32_t i = 0; i < descriptorCopyCount; i++) {
        const VkCopyDescriptorSet *c = &pDescriptorCopies[i];
        VkPs4DescriptorSet *dst = (VkPs4DescriptorSet *)c->dstSet;
        VkPs4DescriptorSet *src = (VkPs4DescriptorSet *)c->srcSet;
        if (!dst || !src) continue;
        if (c->dstBinding >= dst->binding_count || c->srcBinding >= src->binding_count)
            continue;

        VkPs4DescriptorBinding *db = &dst->bindings[c->dstBinding];
        VkPs4DescriptorBinding *sb = &src->bindings[c->srcBinding];
        if (db->type != sb->type) continue;

        uint32_t count = c->descriptorCount;
        if (c->dstArrayElement + count > db->count) {
            count = db->count - c->dstArrayElement;
        }
        if (c->srcArrayElement + count > sb->count) {
            count = sb->count - c->srcArrayElement;
        }

        if (db->buffers && sb->buffers) {
            memcpy(&db->buffers[c->dstArrayElement],
                   &sb->buffers[c->srcArrayElement],
                   count * sizeof(GnmBuffer));
        }
        if (db->textures && sb->textures) {
            memcpy(&db->textures[c->dstArrayElement],
                   &sb->textures[c->srcArrayElement],
                   count * sizeof(GnmTexture));
        }
        if (db->samplers && sb->samplers) {
            memcpy(&db->samplers[c->dstArrayElement],
                   &sb->samplers[c->srcArrayElement],
                   count * sizeof(GnmSampler));
        }
    }
}

/* === vkCmdBindDescriptorSets === */
VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                              VkPipelineLayout layout, uint32_t firstSet, uint32_t setCount,
                              const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
                              const uint32_t *pDynamicOffsets) {
    if (!commandBuffer || !pDescriptorSets) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4PipelineLayout *pl = (VkPs4PipelineLayout *)layout;
    if (!pl) return;

    GnmShaderStage stage = vk_bind_point_to_gnm_stage(pipelineBindPoint);

    /* Get the current pipeline to access input usage slot tables */
    VkPs4Pipeline *pipe = cmd->current_pipeline;
    if (!pipe) return;

    /* Get the appropriate input usage slot table */
    const GnmInputUsageSlot *slots = NULL;
    uint32_t slot_count = 0;

    if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        /* Emit for both VS and PS stages */
        for (int s = 0; s < 2; s++) {
            if (s == 0) {
                slots = pipe->vs_input_usage_slots;
                slot_count = pipe->vs_input_usage_slot_count;
                stage = GNM_STAGE_VS;
            } else {
                slots = pipe->ps_input_usage_slots;
                slot_count = pipe->ps_input_usage_slot_count;
                stage = GNM_STAGE_PS;
            }

            for (uint32_t i = 0; i < slot_count; i++) {
                const GnmInputUsageSlot *slot = &slots[i];
                uint32_t apislot = slot->apislot;

                /* Determine which set this binding belongs to.
                 * For MVP, we assume all bindings are in set 0.
                 * Multi-set support requires tracking set layout offsets. */
                VkPs4DescriptorSet *set = NULL;
                for (uint32_t si = 0; si < setCount; si++) {
                    set = (VkPs4DescriptorSet *)pDescriptorSets[si];
                    if (set && apislot < set->binding_count) {
                        break;
                    }
                    set = NULL;
                }
                if (!set) continue;

                VkPs4DescriptorBinding *b = find_binding(set, apislot);
                if (!b || b->count == 0) continue;

                /* Emit the appropriate Set*UserData command based on usage type */
                switch (slot->usagetype) {
                case GNM_SHINPUTUSAGE_IMM_CONSTBUFFER:
                case GNM_SHINPUTUSAGE_IMM_VERTEXBUFFER:
                    /* Buffer resource → Vsharp */
                    if (b->buffers) {
                        sceGnmDrawCmdSetVsharpUserData(
                            &cmd->gnm_cmd, stage, slot->startregister,
                            &b->buffers[0]
                        );
                    }
                    break;
                case GNM_SHINPUTUSAGE_IMM_RESOURCE:
                case GNM_SHINPUTUSAGE_IMM_RWRESOURCE:
                    /* Texture resource → Tsharp */
                    if (b->textures) {
                        sceGnmDrawCmdSetTsharpUserData(
                            &cmd->gnm_cmd, stage, slot->startregister,
                            &b->textures[0]
                        );
                    }
                    break;
                case GNM_SHINPUTUSAGE_IMM_SAMPLER:
                    /* Sampler resource → Ssharp */
                    if (b->samplers) {
                        sceGnmDrawCmdSetSsharpUserData(
                            &cmd->gnm_cmd, stage, slot->startregister,
                            &b->samplers[0]
                        );
                    }
                    break;
                case GNM_SHINPUTUSAGE_PTR_CONSTBUFFERTABLE:
                case GNM_SHINPUTUSAGE_PTR_RESOURCETABLE:
                case GNM_SHINPUTUSAGE_PTR_SAMPLERTABLE:
                case GNM_SHINPUTUSAGE_PTR_RWRESOURCETABLE:
                case GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE:
                    /* Pointer-based resource table — set a pointer to
                     * the descriptor array in GPU memory.
                     * For MVP, we set the pointer to the first resource
                     * in the binding's array. */
                    if (b->buffers) {
                        sceGnmDrawCmdSetPointerUserData(
                            &cmd->gnm_cmd, stage, slot->startregister,
                            b->buffers
                        );
                    } else if (b->textures) {
                        sceGnmDrawCmdSetPointerUserData(
                            &cmd->gnm_cmd, stage, slot->startregister,
                            b->textures
                        );
                    } else if (b->samplers) {
                        sceGnmDrawCmdSetPointerUserData(
                            &cmd->gnm_cmd, stage, slot->startregister,
                            b->samplers
                        );
                    }
                    break;
                default:
                    /* Other usage types not handled in MVP */
                    break;
                }
            }
        }
    } else if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
        /* Compute pipeline — CS input usage slots stored in
         * pipe->vs_input_usage_slots (reused for CS). */
        slots = pipe->vs_input_usage_slots;
        slot_count = pipe->vs_input_usage_slot_count;
        stage = GNM_STAGE_CS;

        for (uint32_t i = 0; i < slot_count; i++) {
            const GnmInputUsageSlot *slot = &slots[i];
            uint32_t apislot = slot->apislot;

            VkPs4DescriptorSet *set = NULL;
            for (uint32_t si = 0; si < setCount; si++) {
                set = (VkPs4DescriptorSet *)pDescriptorSets[si];
                if (set && apislot < set->binding_count) {
                    break;
                }
                set = NULL;
            }
            if (!set) continue;

            VkPs4DescriptorBinding *b = find_binding(set, apislot);
            if (!b || b->count == 0) continue;

            switch (slot->usagetype) {
            case GNM_SHINPUTUSAGE_IMM_CONSTBUFFER:
            case GNM_SHINPUTUSAGE_IMM_VERTEXBUFFER:
                if (b->buffers) {
                    sceGnmDrawCmdSetVsharpUserData(
                        &cmd->gnm_cmd, stage, slot->startregister,
                        &b->buffers[0]
                    );
                }
                break;
            case GNM_SHINPUTUSAGE_IMM_RESOURCE:
            case GNM_SHINPUTUSAGE_IMM_RWRESOURCE:
                if (b->textures) {
                    sceGnmDrawCmdSetTsharpUserData(
                        &cmd->gnm_cmd, stage, slot->startregister,
                        &b->textures[0]
                    );
                }
                break;
            case GNM_SHINPUTUSAGE_IMM_SAMPLER:
                if (b->samplers) {
                    sceGnmDrawCmdSetSsharpUserData(
                        &cmd->gnm_cmd, stage, slot->startregister,
                        &b->samplers[0]
                    );
                }
                break;
            case GNM_SHINPUTUSAGE_PTR_CONSTBUFFERTABLE:
            case GNM_SHINPUTUSAGE_PTR_RESOURCETABLE:
            case GNM_SHINPUTUSAGE_PTR_SAMPLERTABLE:
            case GNM_SHINPUTUSAGE_PTR_RWRESOURCETABLE:
                if (b->buffers) {
                    sceGnmDrawCmdSetPointerUserData(
                        &cmd->gnm_cmd, stage, slot->startregister,
                        b->buffers
                    );
                } else if (b->textures) {
                    sceGnmDrawCmdSetPointerUserData(
                        &cmd->gnm_cmd, stage, slot->startregister,
                        b->textures
                    );
                } else if (b->samplers) {
                    sceGnmDrawCmdSetPointerUserData(
                        &cmd->gnm_cmd, stage, slot->startregister,
                        b->samplers
                    );
                }
                break;
            default:
                break;
            }
        }
    }

    (void)firstSet;
    (void)dynamicOffsetCount;
    (void)pDynamicOffsets;
    (void)pl;
}

/* === Sampler === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator, VkSampler *pSampler) {
    if (!device || !pCreateInfo || !pSampler) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    GnmSampler *sampler = vk_ps4_alloc_zero(alloc, sizeof(*sampler), 16);
    if (!sampler) return VK_ERROR_OUT_OF_HOST_MEMORY;

    build_sampler_descriptor(sampler, pCreateInfo);

    *pSampler = (VkSampler)sampler;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator) {
    if (!device || !sampler) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, (void *)sampler);
}
