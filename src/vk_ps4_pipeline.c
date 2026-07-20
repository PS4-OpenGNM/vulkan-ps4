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

/* PM4 register definitions for stencil op/ref/mask programming. */
#include <pm4/sid.h>
#include <pm4/amdgfxregs.h>

/* Forward declaration from vk_ps4_shader.c */
VkResult vk_ps4_compile_shader_module(VkPs4ShaderModule *mod, VkShaderStageFlagBits stage,
                                      const VkAllocationCallbacks *alloc,
                                      void **out_binary, size_t *out_binary_size,
                                      GnmShaderMetadata *out_metadata);

/* Forward declaration from vk_ps4_command.c */
extern uint32_t vk_stencil_op_to_pm4(VkStencilOp op);

/* === Blend state conversion === */

static GnmBlendOp vk_blend_factor_to_gnm(VkBlendFactor f) {
    switch (f) {
    case VK_BLEND_FACTOR_ZERO:                       return GNM_BLEND_ZERO;
    case VK_BLEND_FACTOR_ONE:                        return GNM_BLEND_ONE;
    case VK_BLEND_FACTOR_SRC_COLOR:                  return GNM_BLEND_SRC_COLOR;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:        return GNM_BLEND_ONE_MINUS_SRC_COLOR;
    case VK_BLEND_FACTOR_DST_COLOR:                  return GNM_BLEND_DEST_COLOR;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:        return GNM_BLEND_ONE_MINUS_DEST_COLOR;
    case VK_BLEND_FACTOR_SRC_ALPHA:                  return GNM_BLEND_SRC_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:        return GNM_BLEND_ONE_MINUS_SRC_ALPHA;
    case VK_BLEND_FACTOR_DST_ALPHA:                  return GNM_BLEND_DEST_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:        return GNM_BLEND_ONE_MINUS_DEST_ALPHA;
    case VK_BLEND_FACTOR_CONSTANT_COLOR:             return GNM_BLEND_CONSTANT_COLOR;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:   return GNM_BLEND_ONE_MINUS_CONSTANT_COLOR;
    case VK_BLEND_FACTOR_CONSTANT_ALPHA:             return GNM_BLEND_CONSTANT_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:   return GNM_BLEND_ONE_MINUS_CONSTANT_ALPHA;
    case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:         return GNM_BLEND_SRC_ALPHA_SATURATE;
    case VK_BLEND_FACTOR_SRC1_COLOR:                 return GNM_BLEND_SRC1_COLOR;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:       return GNM_BLEND_INVERSE_SRC1_COLOR;
    case VK_BLEND_FACTOR_SRC1_ALPHA:                 return GNM_BLEND_SRC1_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:       return GNM_BLEND_INVERSE_SRC1_ALPHA;
    default:                                         return GNM_BLEND_ZERO;
    }
}

static GnmCombFunc vk_blend_op_to_gnm(VkBlendOp op) {
    switch (op) {
    case VK_BLEND_OP_ADD:                 return GNM_COMB_DST_PLUS_SRC;
    case VK_BLEND_OP_SUBTRACT:            return GNM_COMB_SRC_MINUS_DST;
    case VK_BLEND_OP_REVERSE_SUBTRACT:    return GNM_COMB_DST_MINUS_SRC;
    case VK_BLEND_OP_MIN:                 return GNM_COMB_MIN_DST_SRC;
    case VK_BLEND_OP_MAX:                 return GNM_COMB_MAX_DST_SRC;
    default:                              return GNM_COMB_DST_PLUS_SRC;
    }
}

/* Convert a VkPipelineColorBlendAttachmentState to GnmBlendControl. */
static void vk_blend_attachment_to_gnm(const VkPipelineColorBlendAttachmentState *att,
                                        GnmBlendControl *out) {
    memset(out, 0, sizeof(*out));
    out->blendenabled = att->blendEnable ? true : false;
    out->colorfunc = vk_blend_op_to_gnm(att->colorBlendOp);
    out->colorsrcmult = vk_blend_factor_to_gnm(att->srcColorBlendFactor);
    out->colordstmult = vk_blend_factor_to_gnm(att->dstColorBlendFactor);
    out->alphafunc = vk_blend_op_to_gnm(att->alphaBlendOp);
    out->alphasrcmult = vk_blend_factor_to_gnm(att->srcAlphaBlendFactor);
    out->alphadstmult = vk_blend_factor_to_gnm(att->dstAlphaBlendFactor);
    out->separatealphaenable = (att->alphaBlendOp != att->colorBlendOp ||
        att->srcAlphaBlendFactor != att->srcColorBlendFactor ||
        att->dstAlphaBlendFactor != att->dstColorBlendFactor);
}

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

        /* Store pipeline state — deep copy vertex input state to avoid
         * dangling pointers (app can free pVertexInputState after creation). */
        if (ci->pVertexInputState) {
            pipe->vertex_input_state = *ci->pVertexInputState;
            const VkPipelineVertexInputStateCreateInfo *vi = ci->pVertexInputState;
            /* Deep copy vertex binding descriptions */
            if (vi->vertexBindingDescriptionCount > 0 && vi->pVertexBindingDescriptions) {
                uint32_t n = vi->vertexBindingDescriptionCount;
                pipe->vertex_bindings = vk_ps4_alloc_zero(alloc,
                    n * sizeof(VkVertexInputBindingDescription), 16);
                if (pipe->vertex_bindings) {
                    memcpy(pipe->vertex_bindings, vi->pVertexBindingDescriptions,
                           n * sizeof(VkVertexInputBindingDescription));
                    pipe->vertex_input_state.pVertexBindingDescriptions = pipe->vertex_bindings;
                } else {
                    /* Alloc failure — zero out to avoid dangling pointer */
                    pipe->vertex_input_state.vertexBindingDescriptionCount = 0;
                    pipe->vertex_input_state.pVertexBindingDescriptions = NULL;
                }
            } else {
                pipe->vertex_input_state.vertexBindingDescriptionCount = 0;
                pipe->vertex_input_state.pVertexBindingDescriptions = NULL;
            }
            /* Deep copy vertex attribute descriptions */
            if (vi->vertexAttributeDescriptionCount > 0 && vi->pVertexAttributeDescriptions) {
                uint32_t n = vi->vertexAttributeDescriptionCount;
                pipe->vertex_attributes = vk_ps4_alloc_zero(alloc,
                    n * sizeof(VkVertexInputAttributeDescription), 16);
                if (pipe->vertex_attributes) {
                    memcpy(pipe->vertex_attributes, vi->pVertexAttributeDescriptions,
                           n * sizeof(VkVertexInputAttributeDescription));
                    pipe->vertex_input_state.pVertexAttributeDescriptions = pipe->vertex_attributes;
                } else {
                    pipe->vertex_input_state.vertexAttributeDescriptionCount = 0;
                    pipe->vertex_input_state.pVertexAttributeDescriptions = NULL;
                }
            } else {
                pipe->vertex_input_state.vertexAttributeDescriptionCount = 0;
                pipe->vertex_input_state.pVertexAttributeDescriptions = NULL;
            }
        }
        if (ci->pInputAssemblyState)
            pipe->input_assembly_state = *ci->pInputAssemblyState;
        /* Tessellation state: patch control points */
        if (ci->pTessellationState && ci->pTessellationState->patchControlPoints > 0) {
            pipe->tess_patch_control_points = ci->pTessellationState->patchControlPoints;
        }
        if (ci->pRasterizationState)
            pipe->rasterization_state = *ci->pRasterizationState;
        if (ci->pColorBlendState) {
            pipe->color_blend_state = *ci->pColorBlendState;
            pipe->has_blend_state = true;

            /* Deep copy blend attachment states (pAttachments is a pointer
             * to caller-owned memory that may be freed after pipeline creation). */
            const VkPipelineColorBlendStateCreateInfo *cb = ci->pColorBlendState;
            if (cb->attachmentCount > 0 && cb->pAttachments) {
                uint32_t n = cb->attachmentCount;
                if (n > 8) n = 8;  /* max 8 RT slots */
                pipe->blend_attachments = vk_ps4_alloc_zero(alloc,
                    n * sizeof(VkPipelineColorBlendAttachmentState), 16);
                if (pipe->blend_attachments) {
                    memcpy(pipe->blend_attachments, cb->pAttachments,
                           n * sizeof(VkPipelineColorBlendAttachmentState));
                    pipe->color_blend_state.pAttachments = pipe->blend_attachments;
                    pipe->color_blend_state.attachmentCount = n;

                    /* Pre-compute GnmBlendControl for each RT slot */
                    pipe->blend_control_count = n;
                    pipe->color_write_mask = 0;
                    for (uint32_t j = 0; j < n; j++) {
                        vk_blend_attachment_to_gnm(&cb->pAttachments[j],
                            &pipe->blend_controls[j]);
                        /* Pack colorWriteMask into 4-bit-per-RT mask */
                        uint32_t wm = cb->pAttachments[j].colorWriteMask & 0xF;
                        pipe->color_write_mask |= wm << (j * 4);
                    }
                } else {
                    pipe->color_blend_state.pAttachments = NULL;
                    pipe->color_blend_state.attachmentCount = 0;
                    pipe->blend_control_count = 0;
                    pipe->color_write_mask = 0xFFFFFFFF;  /* write all by default */
                }
            } else {
                pipe->color_blend_state.pAttachments = NULL;
                pipe->color_blend_state.attachmentCount = 0;
                pipe->blend_control_count = 0;
                pipe->color_write_mask = 0xFFFFFFFF;  /* write all by default */
            }

            /* Copy blend constants */
            memcpy(pipe->blend_constants, cb->blendConstants, sizeof(pipe->blend_constants));
        } else {
            pipe->has_blend_state = false;
            pipe->color_write_mask = 0xFFFFFFFF;  /* write all by default */
        }
        if (ci->pDepthStencilState) {
            pipe->depth_stencil_state = *ci->pDepthStencilState;
            pipe->has_depth_stencil_state = true;

            /* Convert VkPipelineDepthStencilStateCreateInfo to GNM state */
            const VkPipelineDepthStencilStateCreateInfo *ds = ci->pDepthStencilState;
            GnmDepthStencilControl *dsc = &pipe->depth_stencil_control;
            memset(dsc, 0, sizeof(*dsc));

            dsc->depthenable = ds->depthTestEnable;
            dsc->zwrite = ds->depthWriteEnable;
            dsc->zfunc = (GnmDepthCompare)ds->depthCompareOp;
            dsc->depthboundsenable = ds->depthBoundsTestEnable;
            dsc->stencilenable = ds->stencilTestEnable;
            dsc->separatestencilenable = false;

            if (ds->stencilTestEnable) {
                /* Front face stencil */
                dsc->stencilfunc = (GnmDepthCompare)ds->front.compareOp;
                /* Check if back face is different from front */
                if (ds->back.compareOp != ds->front.compareOp ||
                    ds->back.failOp != ds->front.failOp ||
                    ds->back.passOp != ds->front.passOp ||
                    ds->back.depthFailOp != ds->front.depthFailOp ||
                    ds->back.compareMask != ds->front.compareMask ||
                    ds->back.writeMask != ds->front.writeMask ||
                    ds->back.reference != ds->front.reference) {
                    dsc->separatestencilenable = true;
                    dsc->stencilbackfunc = (GnmDepthCompare)ds->back.compareOp;
                }

                /* Pre-compute DB_STENCIL_CONTROL (ops for front and back) */
                pipe->stencil_control =
                    S_02842C_STENCILFAIL(vk_stencil_op_to_pm4(ds->front.failOp)) |
                    S_02842C_STENCILZPASS(vk_stencil_op_to_pm4(ds->front.passOp)) |
                    S_02842C_STENCILZFAIL(vk_stencil_op_to_pm4(ds->front.depthFailOp));
                if (dsc->separatestencilenable) {
                    pipe->stencil_control |=
                        S_02842C_STENCILFAIL_BF(vk_stencil_op_to_pm4(ds->back.failOp)) |
                        S_02842C_STENCILZPASS_BF(vk_stencil_op_to_pm4(ds->back.passOp)) |
                        S_02842C_STENCILZFAIL_BF(vk_stencil_op_to_pm4(ds->back.depthFailOp));
                }

                /* Pre-compute DB_STENCILREFMASK (front) */
                pipe->stencil_refmask =
                    S_028430_STENCILTESTVAL(ds->front.reference) |
                    S_028430_STENCILMASK(ds->front.compareMask) |
                    S_028430_STENCILWRITEMASK(ds->front.writeMask);

                /* Pre-compute DB_STENCILREFMASK_BF (back) */
                pipe->stencil_refmask_bf =
                    S_028434_STENCILTESTVAL_BF(ds->back.reference) |
                    S_028434_STENCILMASK_BF(ds->back.compareMask) |
                    S_028434_STENCILWRITEMASK_BF(ds->back.writeMask);
            }
        }
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
                    /* The shader compiler may output different binary types
                     * depending on the pipeline configuration:
                     * - GNM_SHADER_VERTEX: standard VS (VS_VS)
                     * - GNM_SHADER_LOCAL: LS (VS before tessellation)
                     * - GNM_SHADER_EXPORT: ES (VS before geometry shader)
                     * All three start with GnmShaderCommonData, but the
                     * stage registers differ. */
                    const uint8_t *stage_ptr = (const uint8_t *)metadata.stage;
                    if (metadata.type == GNM_SHADER_LOCAL) {
                        /* LS: GnmShaderCommonData + GnmLsStageRegisters */
                        const GnmLsStageRegisters *ls_regs =
                            (const GnmLsStageRegisters *)(stage_ptr + sizeof(GnmShaderCommonData));
                        pipe->ls_regs = *ls_regs;
                        pipe->has_ls = true;
                        /* LS doesn't have vertex input semantics in the same
                         * format — skip semantic extraction for LS */
                    } else if (metadata.type == GNM_SHADER_EXPORT) {
                        /* ES: GnmShaderCommonData + GnmEsStageRegisters */
                        const GnmEsStageRegisters *es_regs =
                            (const GnmEsStageRegisters *)(stage_ptr + sizeof(GnmShaderCommonData));
                        pipe->es_regs = *es_regs;
                        pipe->has_es = true;
                    } else {
                        /* Standard VS: GnmVsShader (common + regs + semantics) */
                        const GnmVsShader *vs = (const GnmVsShader *)metadata.stage;
                        pipe->vs_regs = vs->registers;
                        /* Extract vertex input semantics */
                        uint32_t nsemantics = vs->numinputsemantics;
                        if (nsemantics > VK_PS4_MAX_INPUT_USAGE_SLOTS) nsemantics = VK_PS4_MAX_INPUT_USAGE_SLOTS;
                        const GnmVertexInputSemantic *semantics = sceGnmVsShaderInputSemanticTable(vs);
                        if (semantics && nsemantics > 0) {
                            memcpy(pipe->vs_input_semantics, semantics, nsemantics * sizeof(GnmVertexInputSemantic));
                            pipe->vs_input_semantic_count = nsemantics;
                        }
                    }
                    pipe->vs_module = mod;
                    vs_found = true;
                    if (slots && nslots > 0) {
                        memcpy(pipe->vs_input_usage_slots, slots, nslots * sizeof(GnmInputUsageSlot));
                        pipe->vs_input_usage_slot_count = nslots;
                    }
                    break;
                }
                case VK_SHADER_STAGE_FRAGMENT_BIT: {
                    const GnmPsShader *ps = (const GnmPsShader *)metadata.stage;
                    pipe->ps_regs = ps->registers;
                    pipe->fs_module = mod;
                    ps_found = true;
                    pipe->has_ps = true;
                    if (slots && nslots > 0) {
                        memcpy(pipe->ps_input_usage_slots, slots, nslots * sizeof(GnmInputUsageSlot));
                        pipe->ps_input_usage_slot_count = nslots;
                    }
                    break;
                }
                case VK_SHADER_STAGE_GEOMETRY_BIT: {
                    /* GS shader binary: GnmShaderCommonData + GnmGsStageRegisters.
                     * NOTE: The current psbc compiler maps GS to GNM_SHADER_VERTEX
                     * with a GnmVsShader header. Only extract GS registers when
                     * the shader type is actually GNM_SHADER_GEOMETRY. */
                    if (metadata.type == GNM_SHADER_GEOMETRY) {
                        const uint8_t *stage_ptr = (const uint8_t *)metadata.stage;
                        const GnmGsStageRegisters *gs_regs =
                            (const GnmGsStageRegisters *)(stage_ptr + sizeof(GnmShaderCommonData));
                        pipe->gs_regs = *gs_regs;
                        pipe->has_gs = true;
                    }
                    /* Store the module regardless — it may be used later
                     * when the compiler properly outputs GS binaries */
                    pipe->gs_module = mod;
                    break;
                }
                case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: {
                    /* TCS (hull shader): GnmShaderCommonData + GnmHsStageRegisters.
                     * NOTE: The current psbc compiler maps TCS to GNM_SHADER_VERTEX
                     * with a GnmVsShader header. Only extract HS registers when
                     * the shader type is actually GNM_SHADER_HULL. */
                    if (metadata.type == GNM_SHADER_HULL) {
                        const uint8_t *stage_ptr = (const uint8_t *)metadata.stage;
                        const GnmHsStageRegisters *hs_regs =
                            (const GnmHsStageRegisters *)(stage_ptr + sizeof(GnmShaderCommonData));
                        pipe->hs_regs = *hs_regs;
                        pipe->has_hs = true;
                    }
                    pipe->tcs_module = mod;
                    break;
                }
                case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: {
                    /* TES (domain shader): compiled as VS (DS_VS) or ES (DS_ES).
                     * NOTE: The current psbc compiler maps TES to GNM_SHADER_VERTEX
                     * with a GnmVsShader header. Only extract ES registers when
                     * the shader type is GNM_SHADER_EXPORT. Otherwise, treat as VS. */
                    if (metadata.type == GNM_SHADER_EXPORT) {
                        const uint8_t *stage_ptr = (const uint8_t *)metadata.stage;
                        const GnmEsStageRegisters *es_regs =
                            (const GnmEsStageRegisters *)(stage_ptr + sizeof(GnmShaderCommonData));
                        pipe->es_regs = *es_regs;
                        pipe->has_es = true;
                    } else {
                        /* DS_VS or compiler fallback: treat as vertex shader.
                         * Extract VS registers so CmdBindPipeline can use them. */
                        const GnmVsShader *vs = (const GnmVsShader *)metadata.stage;
                        pipe->vs_regs = vs->registers;
                        pipe->has_ds_vs = true;
                    }
                    pipe->tes_module = mod;
                    break;
                }
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
            vk_ps4_free(alloc, pipe->vertex_bindings);
            vk_ps4_free(alloc, pipe->vertex_attributes);
            vk_ps4_free(alloc, pipe->blend_attachments);
            vk_ps4_free(alloc, pipe->fetch_shader);
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
            bool has_fs_slot = false;
            bool has_vb_slot = false;
            for (uint32_t s = 0; s < pipe->vs_input_usage_slot_count; s++) {
                if (pipe->vs_input_usage_slots[s].usagetype == GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER) {
                    pipe->fetch_shader_slot = pipe->vs_input_usage_slots[s].startregister;
                    has_fs_slot = true;
                } else if (pipe->vs_input_usage_slots[s].usagetype == GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE) {
                    pipe->vertex_buffer_table_slot = pipe->vs_input_usage_slots[s].startregister;
                    has_vb_slot = true;
                }
            }

            /* Only create the fetch shader if the shader has a fetch shader
             * slot in its usage table. Without it, the fetch shader pointer
             * can't be emitted to the GPU. */
            if (!has_fs_slot) {
                /* No fetch shader slot — skip fetch shader creation.
                 * Vertex input won't work, but this is a shader issue. */
                goto skip_fetch_shader;
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
                        pipe->has_fetch_shader_slot = has_fs_slot;
                        pipe->has_vb_table_slot = has_vb_slot;
                    } else {
                        vk_ps4_free(alloc, pipe->fetch_shader);
                        pipe->fetch_shader = NULL;
                        pipe->fetch_shader_size = 0;
                    }
                }
            }
        }
        skip_fetch_shader: ;

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

            /* For CS, sceGnmShaderBinaryGetMetadata falls through to the
             * default case: it doesn't set metadata.shadercode or
             * metadata.inputusageslots. We must compute them manually.
             *
             * The binary layout is:
             *   GnmShaderFileHeader (0x10 bytes)
             *   GnmShaderCommonData (0x8 bytes)
             *   [stage-specific header, if any]
             *   GnmInputUsageSlot[numinputusageslots]
             *   shader code (shadersize dwords)
             *
             * The header size (in bytes) is:
             *   headersizedwords * 4 (covers CommonData + stage header + slots)
             * The shader code starts right after the header.
             */
            const uint8_t *base = (const uint8_t *)metadata.common;
            uint32_t nslots = metadata.numinputusageslots;
            if (nslots > VK_PS4_MAX_INPUT_USAGE_SLOTS)
                nslots = VK_PS4_MAX_INPUT_USAGE_SLOTS;

            /* The header size (in bytes) is headersizedwords * 4.
             * When headersizedwords == 0, compute the header size manually:
             * CommonData + input usage slots. */
            uint32_t header_bytes = metadata.fileheader->headersizedwords
                ? metadata.fileheader->headersizedwords * 4u
                : (uint32_t)(sizeof(GnmShaderCommonData) +
                             nslots * sizeof(GnmInputUsageSlot));

            /* Shader code pointer = base + header_bytes */
            void *cs_code = (void *)(base + header_bytes);
            sceGnmCsRegsSetAddress(&pipe->cs_regs, cs_code);

            /* Extract CS input usage slots.
             * For CS, the slot table follows GnmShaderCommonData. */
            if (nslots > 0) {
                /* Bounds check: ensure slot table fits within the binary */
                uint32_t slots_bytes = (uint32_t)(sizeof(GnmShaderCommonData) +
                                   nslots * sizeof(GnmInputUsageSlot));
                if (slots_bytes <= mod->binary_size) {
                    const GnmInputUsageSlot *cs_slots =
                        (const GnmInputUsageSlot *)(base + sizeof(GnmShaderCommonData));
                    memcpy(pipe->vs_input_usage_slots, cs_slots,
                           nslots * sizeof(GnmInputUsageSlot));
                    pipe->vs_input_usage_slot_count = nslots;
                }
            }

            cs_ok = true;
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
    if (pipe->vertex_bindings) {
        vk_ps4_free(alloc, pipe->vertex_bindings);
    }
    if (pipe->vertex_attributes) {
        vk_ps4_free(alloc, pipe->vertex_attributes);
    }
    if (pipe->blend_attachments) {
        vk_ps4_free(alloc, pipe->blend_attachments);
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
                set->bindings[b].binding_number = set->layout->bindings[b].binding;
                set->bindings[b].resources_allocated = false;
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
