/*
 * vk_ps4_pipeline.c — VkPipeline implementation.
 *
 * vkCreateGraphicsPipelines compiles shader stages via libpsbc,
 * extracts GNM stage registers from the shader binary, and stores
 * pipeline state (blend, rasterizer, depth/stencil) for later
 * emission in vkCmdBindPipeline.
 */

#include "vk_ps4_internal.h"

#include <string.h>

/* Forward declaration from vk_ps4_shader.c */
VkResult vk_ps4_compile_shader_module(VkPs4ShaderModule *mod, VkShaderStageFlagBits stage,
                                      const VkAllocationCallbacks *alloc,
                                      void **out_binary, size_t *out_binary_size,
                                      GnmShaderMetadata *out_metadata);

GnmPrimitiveType vk_topology_to_gnm(VkPrimitiveTopology topology) {
    switch (topology) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return GNM_PT_POINTLIST;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: return GNM_PT_LINELIST;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: return GNM_PT_LINESTRIP;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return GNM_PT_TRILIST;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return GNM_PT_TRISTRIP;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: return GNM_PT_TRIFAN;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY: return GNM_PT_LINELIST_ADJ;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY: return GNM_PT_LINESTRIP_ADJ;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY: return GNM_PT_TRILIST_ADJ;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY: return GNM_PT_TRIPSTRIP_ADJ;
    case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST: return GNM_PT_TRILIST; /* tess uses different path */
    default: return GNM_PT_TRILIST;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
                                uint32_t createInfoCount,
                                const VkGraphicsPipelineCreateInfo *pCreateInfos,
                                const VkAllocationCallbacks *pAllocator,
                                VkPipeline *pPipelines) {
    (void)pipelineCache;

    if (!device || !pCreateInfos || !pPipelines) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkResult overall_result = VK_SUCCESS;

    for (uint32_t i = 0; i < createInfoCount; i++) {
        const VkGraphicsPipelineCreateInfo *ci = &pCreateInfos[i];
        VkPs4Pipeline *pipe = vk_ps4_alloc_zero(alloc, sizeof(*pipe), 16);
        if (!pipe) {
            pPipelines[i] = VK_NULL_HANDLE;
            overall_result = VK_ERROR_OUT_OF_HOST_MEMORY;
            continue;
        }
        pipe->type = VK_PS4_OBJ_PIPELINE;
        pipe->device = dev;
        pipe->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;

        /* Zero out stage registers */
        memset(&pipe->vs_regs, 0, sizeof(pipe->vs_regs));
        memset(&pipe->ps_regs, 0, sizeof(pipe->ps_regs));
        memset(&pipe->cs_regs, 0, sizeof(pipe->cs_regs));

        /* Store pipeline state (shallow copy of structs — internal pointers
         * like pVertexBindingDescriptions are NOT copied. For MVP we only
         * use top-level fields like topology, polygonMode, etc. during
         * CmdBindPipeline. Full deep copy is Phase 2.) */
        if (ci->pVertexInputState)
            pipe->vertex_input_state = *ci->pVertexInputState;
        if (ci->pInputAssemblyState)
            pipe->input_assembly_state = *ci->pInputAssemblyState;
        if (ci->pRasterizationState)
            pipe->rasterization_state = *ci->pRasterizationState;
        if (ci->pColorBlendState)
            pipe->color_blend_state = *ci->pColorBlendState;
        if (ci->pDepthStencilState)
            pipe->depth_stencil_state = *ci->pDepthStencilState;
        if (ci->pViewportState)
            pipe->viewport_state = *ci->pViewportState;
        if (ci->pMultisampleState)
            pipe->multisample_state = *ci->pMultisampleState;

        /* Process shader stages */
        bool compile_ok = true;
        bool vs_found = false, ps_found = false;
        for (uint32_t s = 0; s < ci->stageCount; s++) {
            const VkPipelineShaderStageCreateInfo *stage = &ci->pStages[s];
            VkPs4ShaderModule *mod = (VkPs4ShaderModule *)stage->module;

            void *binary = NULL;
            size_t binary_size = 0;
            GnmShaderMetadata metadata = {0};

            VkResult vr = vk_ps4_compile_shader_module(
                mod, stage->stage, alloc, &binary, &binary_size, &metadata
            );
            if (vr != VK_SUCCESS) {
                compile_ok = false;
                break;
            }

            /* Extract stage registers and input usage slots from the compiled shader binary */
            if (metadata.fileheader && metadata.stage) {
                /* Extract input usage slots (shared across all stages in the binary) */
                uint32_t nslots = metadata.numinputusageslots;
                if (nslots > VK_PS4_MAX_INPUT_USAGE_SLOTS) nslots = VK_PS4_MAX_INPUT_USAGE_SLOTS;
                const GnmInputUsageSlot *slots = metadata.inputusageslots;

                switch (stage->stage) {
                case VK_SHADER_STAGE_VERTEX_BIT: {
                    const GnmVsShader *vs = (const GnmVsShader *)metadata.stage;
                    pipe->vs_regs = vs->registers;
                    pipe->vs_module = mod;
                    vs_found = true;
                    if (slots && nslots > 0) {
                        memcpy(pipe->vs_input_usage_slots, slots, nslots * sizeof(GnmInputUsageSlot));
                        pipe->vs_input_usage_slot_count = nslots;
                    }
                    /* Extract vertex input semantics */
                    uint32_t nsemantics = vs->numinputsemantics;
                    if (nsemantics > VK_PS4_MAX_INPUT_USAGE_SLOTS) nsemantics = VK_PS4_MAX_INPUT_USAGE_SLOTS;
                    const GnmVertexInputSemantic *semantics = sceGnmVsShaderInputSemanticTable(vs);
                    if (semantics && nsemantics > 0) {
                        memcpy(pipe->vs_input_semantics, semantics, nsemantics * sizeof(GnmVertexInputSemantic));
                        pipe->vs_input_semantic_count = nsemantics;
                    }
                    break;
                }
                case VK_SHADER_STAGE_FRAGMENT_BIT: {
                    const GnmPsShader *ps = (const GnmPsShader *)metadata.stage;
                    pipe->ps_regs = ps->registers;
                    pipe->fs_module = mod;
                    ps_found = true;
                    if (slots && nslots > 0) {
                        memcpy(pipe->ps_input_usage_slots, slots, nslots * sizeof(GnmInputUsageSlot));
                        pipe->ps_input_usage_slot_count = nslots;
                    }
                    break;
                }
                case VK_SHADER_STAGE_GEOMETRY_BIT:
                    pipe->gs_module = mod;
                    break;
                case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
                    pipe->tcs_module = mod;
                    break;
                case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
                    pipe->tes_module = mod;
                    break;
                default:
                    break;
                }
            }

            /* Free the compiled binary — stage registers are already extracted */
            if (binary && binary != mod->binary) {
                vk_ps4_free(alloc, binary);
            }
        }

        /* A graphics pipeline must have at least VS with valid registers */
        if (!compile_ok || !vs_found) {
            vk_ps4_free(alloc, pipe);
            pPipelines[i] = VK_NULL_HANDLE;
            overall_result = VK_ERROR_FEATURE_NOT_PRESENT;
            continue;
        }
        /* PS is optional but if present must have valid registers */
        if (!ps_found) {
            /* Pipeline without fragment shader — may be used for depth-only.
             * This is valid in Vulkan. PS regs stay zeroed. */
        }

        /* Generate fetch shader from VS input semantics + input usage slots.
         * The fetch shader tells the GPU how to load vertex attributes from
         * bound vertex buffers into VGPRs. */
        if (pipe->vs_input_semantic_count > 0 && pipe->vs_input_usage_slot_count > 0) {
            /* Find the fetch shader slot and vertex buffer table slot
             * from the input usage slot table */
            for (uint32_t s = 0; s < pipe->vs_input_usage_slot_count; s++) {
                if (pipe->vs_input_usage_slots[s].usagetype == GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER) {
                    pipe->fetch_shader_slot = pipe->vs_input_usage_slots[s].startregister;
                } else if (pipe->vs_input_usage_slots[s].usagetype == GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE) {
                    pipe->vertex_buffer_table_slot = pipe->vs_input_usage_slots[s].startregister;
                }
            }

            /* Build instancing modes from Vulkan vertex input binding descriptions */
            GnmFetchShaderInstancingMode inst_modes[VK_PS4_MAX_VERTEX_BINDINGS];
            uint32_t num_inst_modes = 0;
            if (ci->pVertexInputState) {
                const VkPipelineVertexInputStateCreateInfo *vi = ci->pVertexInputState;
                num_inst_modes = vi->vertexBindingDescriptionCount;
                if (num_inst_modes > VK_PS4_MAX_VERTEX_BINDINGS) num_inst_modes = VK_PS4_MAX_VERTEX_BINDINGS;
                for (uint32_t b = 0; b < num_inst_modes; b++) {
                    VkVertexInputRate rate = vi->pVertexBindingDescriptions[b].inputRate;
                    inst_modes[b] = (rate == VK_VERTEX_INPUT_RATE_INSTANCE) ?
                        GNM_FETCH_MODE_INSTANCEID : GNM_FETCH_MODE_VERTEXINDEX;
                }
            }

            GnmFetchShaderCreateInfo fetch_ci = {0};
            fetch_ci.regs = &pipe->vs_regs;
            fetch_ci.vtxinputs = pipe->vs_input_semantics;
            fetch_ci.numvtxinputs = pipe->vs_input_semantic_count;
            fetch_ci.inputusages = pipe->vs_input_usage_slots;
            fetch_ci.numinputusages = pipe->vs_input_usage_slot_count;
            if (num_inst_modes > 0) {
                fetch_ci.instancedata = inst_modes;
                fetch_ci.numinstancedata = num_inst_modes;
            }

            uint32_t fetch_size = 0;
            GnmError gerr = sceGnmFetchShaderCalcSize(&fetch_size, &fetch_ci);
            if (gerr == GNM_ERROR_OK && fetch_size > 0) {
                /* Allocate fetch shader (must be 256-byte aligned) */
                pipe->fetch_shader = vk_ps4_alloc_zero(alloc, fetch_size, 256);
                if (pipe->fetch_shader) {
                    pipe->fetch_shader_size = fetch_size;
                    GnmFetchShaderResults fetch_res = {0};
                    gerr = sceGnmCreateFetchShader(
                        pipe->fetch_shader, fetch_size, &fetch_ci, &fetch_res
                    );
                    if (gerr == GNM_ERROR_OK) {
                        sceGnmVsRegsSetFetchShaderModifier(&pipe->vs_regs, &fetch_res);
                        pipe->has_fetch_shader = true;
                    } else {
                        vk_ps4_free(alloc, pipe->fetch_shader);
                        pipe->fetch_shader = NULL;
                        pipe->fetch_shader_size = 0;
                    }
                }
            }
        }

        pPipelines[i] = (VkPipeline)pipe;
    }

    return overall_result;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache,
                               uint32_t createInfoCount,
                               const VkComputePipelineCreateInfo *pCreateInfos,
                               const VkAllocationCallbacks *pAllocator,
                               VkPipeline *pPipelines) {
    (void)pipelineCache;

    if (!device || !pCreateInfos || !pPipelines) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    VkResult overall_result = VK_SUCCESS;

    for (uint32_t i = 0; i < createInfoCount; i++) {
        const VkComputePipelineCreateInfo *ci = &pCreateInfos[i];
        VkPs4Pipeline *pipe = vk_ps4_alloc_zero(alloc, sizeof(*pipe), 16);
        if (!pipe) {
            pPipelines[i] = VK_NULL_HANDLE;
            overall_result = VK_ERROR_OUT_OF_HOST_MEMORY;
            continue;
        }
        pipe->type = VK_PS4_OBJ_PIPELINE;
        pipe->device = dev;
        pipe->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
        memset(&pipe->cs_regs, 0, sizeof(pipe->cs_regs));

        VkPs4ShaderModule *mod = (VkPs4ShaderModule *)ci->stage.module;
        void *binary = NULL;
        size_t binary_size = 0;
        GnmShaderMetadata metadata = {0};

        VkResult vr = vk_ps4_compile_shader_module(
            mod, ci->stage.stage, alloc, &binary, &binary_size, &metadata
        );
        if (vr != VK_SUCCESS) {
            vk_ps4_free(alloc, pipe);
            pPipelines[i] = VK_NULL_HANDLE;
            overall_result = VK_ERROR_FEATURE_NOT_PRESENT;
            continue;
        }

        bool cs_ok = false;
        if (metadata.fileheader && metadata.common) {
            pipe->cs_module = mod;

            /* Set up CS stage registers — the CS shader binary doesn't
             * contain GnmCsStageRegisters; the driver fills them in.
             * The shader code address is set via sceGnmCsRegsSetAddress. */
            if (metadata.shadercode) {
                sceGnmCsRegsSetAddress(&pipe->cs_regs, metadata.shadercode);

                /* Set thread group counts from the shader's resource regs.
                 * For now, leave them at 0 — they'll be set per-dispatch
                 * by the hardware based on the dispatch dimensions. */

                /* Extract CS input usage slots — for CS, the input usage
                 * slot table follows GnmShaderCommonData in the binary.
                 * The metadata extraction doesn't set inputusageslots for
                 * CS (falls through to default), so we extract them here. */
                if (metadata.numinputusageslots > 0 && metadata.common) {
                    const uint8_t *base = (const uint8_t *)metadata.common;
                    const GnmInputUsageSlot *cs_slots =
                        (const GnmInputUsageSlot *)(base + sizeof(GnmShaderCommonData));
                    uint32_t nslots = metadata.numinputusageslots;
                    if (nslots > VK_PS4_MAX_INPUT_USAGE_SLOTS) nslots = VK_PS4_MAX_INPUT_USAGE_SLOTS;
                    memcpy(pipe->vs_input_usage_slots, cs_slots, nslots * sizeof(GnmInputUsageSlot));
                    /* Store CS slots in vs_input_usage_slots for CmdBindDescriptorSets */
                    pipe->vs_input_usage_slot_count = nslots;
                }

                cs_ok = true;
            }
        }

        if (binary && binary != mod->binary) {
            vk_ps4_free(alloc, binary);
        }

        if (!cs_ok) {
            vk_ps4_free(alloc, pipe);
            pPipelines[i] = VK_NULL_HANDLE;
            overall_result = VK_ERROR_FEATURE_NOT_PRESENT;
            continue;
        }

        pPipelines[i] = (VkPipeline)pipe;
    }

    return overall_result;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator) {
    if (!device || !pipeline) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Pipeline *pipe = (VkPs4Pipeline *)pipeline;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    if (pipe->fetch_shader) {
        vk_ps4_free(alloc, pipe->fetch_shader);
    }
    vk_ps4_free(alloc, pipe);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout) {
    if (!device || !pCreateInfo || !pPipelineLayout) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4PipelineLayout *layout = vk_ps4_alloc_zero(alloc, sizeof(*layout), 16);
    if (!layout) return VK_ERROR_OUT_OF_HOST_MEMORY;
    layout->type = VK_PS4_OBJ_PIPELINE_LAYOUT;
    layout->device = dev;
    layout->create_info = *pCreateInfo;
    layout->set_layout_count = pCreateInfo->setLayoutCount;
    layout->push_constant_range_count = pCreateInfo->pushConstantRangeCount;

    /* Copy set layout pointers */
    if (layout->set_layout_count > 0) {
        layout->set_layouts = vk_ps4_alloc_zero(alloc,
            layout->set_layout_count * sizeof(VkPs4DescriptorSetLayout *), 16);
        if (!layout->set_layouts) {
            vk_ps4_free(alloc, layout);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        for (uint32_t i = 0; i < layout->set_layout_count; i++) {
            layout->set_layouts[i] = (VkPs4DescriptorSetLayout *)pCreateInfo->pSetLayouts[i];
        }
    }

    /* Copy push constant ranges */
    if (layout->push_constant_range_count > 0) {
        layout->push_constant_ranges = vk_ps4_alloc_zero(alloc,
            layout->push_constant_range_count * sizeof(VkPushConstantRange), 16);
        if (!layout->push_constant_ranges) {
            vk_ps4_free(alloc, layout->set_layouts);
            vk_ps4_free(alloc, layout);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memcpy(layout->push_constant_ranges, pCreateInfo->pPushConstantRanges,
               layout->push_constant_range_count * sizeof(VkPushConstantRange));
    }

    *pPipelineLayout = (VkPipelineLayout)layout;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator) {
    if (!device || !pipelineLayout) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4PipelineLayout *layout = (VkPs4PipelineLayout *)pipelineLayout;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, layout->set_layouts);
    vk_ps4_free(alloc, layout->push_constant_ranges);
    vk_ps4_free(alloc, layout);
}

/* === Descriptor set layout === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout) {
    if (!device || !pCreateInfo || !pSetLayout) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4DescriptorSetLayout *layout = vk_ps4_alloc_zero(alloc, sizeof(*layout), 16);
    if (!layout) return VK_ERROR_OUT_OF_HOST_MEMORY;
    layout->type = VK_PS4_OBJ_DESCRIPTOR_SET_LAYOUT;
    layout->device = dev;
    layout->create_info = *pCreateInfo;
    layout->binding_count = pCreateInfo->bindingCount;

    if (layout->binding_count > 0) {
        layout->bindings = vk_ps4_alloc_zero(alloc,
            layout->binding_count * sizeof(VkDescriptorSetLayoutBinding), 16);
        if (!layout->bindings) {
            vk_ps4_free(alloc, layout);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memcpy(layout->bindings, pCreateInfo->pBindings,
               layout->binding_count * sizeof(VkDescriptorSetLayoutBinding));
        /* Null out pImmutableSamplers pointers in the copy — they dangle.
         * Immutable samplers are Phase 2. */
        for (uint32_t i = 0; i < layout->binding_count; i++) {
            layout->bindings[i].pImmutableSamplers = NULL;
        }
        /* Wire deep-copied bindings into create_info */
        layout->create_info.pBindings = layout->bindings;
    }

    *pSetLayout = (VkDescriptorSetLayout)layout;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout setLayout, const VkAllocationCallbacks *pAllocator) {
    if (!device || !setLayout) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4DescriptorSetLayout *layout = (VkPs4DescriptorSetLayout *)setLayout;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, layout->bindings);
    vk_ps4_free(alloc, layout);
}

/* === Descriptor pool / set (Phase 2 — minimal stubs for Phase 1) === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool) {
    if (!device || !pCreateInfo || !pDescriptorPool) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4DescriptorPool *pool = vk_ps4_alloc_zero(alloc, sizeof(*pool), 16);
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->type = VK_PS4_OBJ_DESCRIPTOR_POOL;
    pool->device = dev;
    pool->create_info = *pCreateInfo;
    *pDescriptorPool = (VkDescriptorPool)pool;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator) {
    if (!device || !descriptorPool) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4DescriptorPool *pool = (VkPs4DescriptorPool *)descriptorPool;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
                              VkDescriptorSet *pDescriptorSets) {
    if (!device || !pAllocateInfo || !pDescriptorSets) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;

    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
        VkPs4DescriptorSet *set = vk_ps4_alloc_zero(alloc, sizeof(*set), 16);
        if (!set) {
            for (uint32_t j = 0; j < i; j++) {
                vk_ps4_free(alloc, (void *)pDescriptorSets[j]);
                pDescriptorSets[j] = VK_NULL_HANDLE;
            }
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        set->type = VK_PS4_OBJ_DESCRIPTOR_SET;
        set->device = dev;
        set->pool = (VkPs4DescriptorPool *)pAllocateInfo->descriptorPool;
        set->layout = (VkPs4DescriptorSetLayout *)pAllocateInfo->pSetLayouts[i];

        /* Initialize bindings from the layout */
        if (set->layout && set->layout->binding_count <= VK_PS4_MAX_DESCRIPTOR_BINDINGS) {
            set->binding_count = set->layout->binding_count;
            for (uint32_t b = 0; b < set->binding_count; b++) {
                set->bindings[b].type = set->layout->bindings[b].descriptorType;
                set->bindings[b].count = set->layout->bindings[b].descriptorCount;
                set->bindings[b].buffers = NULL;
                set->bindings[b].textures = NULL;
                set->bindings[b].samplers = NULL;
                /* Resource arrays are allocated lazily in UpdateDescriptorSets */
            }
        }

        pDescriptorSets[i] = (VkDescriptorSet)set;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool,
                          uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets) {
    (void)descriptorPool;
    if (!device || !pDescriptorSets) return VK_SUCCESS;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    for (uint32_t i = 0; i < descriptorSetCount; i++) {
        if (pDescriptorSets[i]) {
            VkPs4DescriptorSet *set = (VkPs4DescriptorSet *)pDescriptorSets[i];
            /* Free binding resource arrays */
            for (uint32_t b = 0; b < set->binding_count; b++) {
                if (set->bindings[b].buffers) vk_ps4_free(alloc, set->bindings[b].buffers);
                if (set->bindings[b].textures) vk_ps4_free(alloc, set->bindings[b].textures);
                if (set->bindings[b].samplers) vk_ps4_free(alloc, set->bindings[b].samplers);
            }
            vk_ps4_free(alloc, set);
        }
    }
    return VK_SUCCESS;
}

/* UpdateDescriptorSets moved to vk_ps4_descriptor.c */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) {
    (void)device;
    (void)descriptorPool;
    (void)flags;
    return VK_SUCCESS;
}

/* === Sampler === */
/* (Moved to vk_ps4_descriptor.c for proper GnmSampler initialization) */
