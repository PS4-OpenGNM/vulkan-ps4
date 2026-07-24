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
    VK_PS4_LOG_ENTRY();
    vk_ps4_log("CreateGraphicsPipelines: count=%u", createInfoCount);

    if (!device || !pCreateInfos || !pPipelines) {
        vk_ps4_log_raw("CreateGraphicsPipelines: NULL args, FAIL");
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

            /* Check pipeline cache first — avoid recompiling if cached */
            uint64_t cache_hash = 0;
            size_t cached_size = 0;
            void *cached_binary = NULL;
            if (pipelineCache && mod->binary && mod->binary_size > 0) {
                cache_hash = vk_ps4_pipeline_cache_hash(mod->binary, mod->binary_size,
                                                        (uint32_t)stage->stage);
                cached_binary = vk_ps4_pipeline_cache_lookup(pipelineCache, cache_hash,
                                                             (uint32_t)stage->stage,
                                                             &cached_size);
            }

            if (cached_binary && cached_size > 0) {
                /* Cache hit — copy the binary (we need our own copy because
                 * the cache may be destroyed before the pipeline) */
                binary = vk_ps4_alloc(alloc, cached_size, 16);
                if (binary) {
                    memcpy(binary, cached_binary, cached_size);
                    binary_size = cached_size;
                    /* Parse metadata from the cached binary */
                    sceGnmShaderBinaryGetMetadata(binary, binary_size, &metadata);
                }
            } else {
                /* Cache miss — compile the shader */
                VkResult vr = vk_ps4_compile_shader_module(
                    mod, stage->stage, alloc, &binary, &binary_size, &metadata
                );
                if (vr != VK_SUCCESS) {
                    if (binary && binary != mod->binary) {
                        vk_ps4_free(alloc, binary);
                    }
                    compile_ok = false;
                    break;
                }
                /* Insert into pipeline cache */
                if (pipelineCache && binary && binary_size > 0 && mod->binary && mod->binary_size > 0) {
                    vk_ps4_pipeline_cache_insert(pipelineCache, cache_hash,
                                                 (uint32_t)stage->stage,
                                                 (uint32_t)mod->binary_size,
                                                 binary, binary_size);
                }
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
                        /* Patch the shader code address from file-offset
                         * to actual GPU address in the compiled binary. */
                        if (metadata.shadercode) {
                            sceGnmLsRegsSetAddress(&pipe->ls_regs,
                                (void *)metadata.shadercode);
                        }
                        /* LS doesn't have vertex input semantics in the same
                         * format — skip semantic extraction for LS */
                    } else if (metadata.type == GNM_SHADER_EXPORT) {
                        /* ES: GnmShaderCommonData + GnmEsStageRegisters */
                        const GnmEsStageRegisters *es_regs =
                            (const GnmEsStageRegisters *)(stage_ptr + sizeof(GnmShaderCommonData));
                        pipe->es_regs = *es_regs;
                        pipe->has_es = true;
                        if (metadata.shadercode) {
                            sceGnmEsRegsSetAddress(&pipe->es_regs,
                                (void *)metadata.shadercode);
                        }
                    } else {
                        /* Standard VS: GnmVsShader (common + regs + semantics) */
                        const GnmVsShader *vs = (const GnmVsShader *)metadata.stage;
                        pipe->vs_regs = vs->registers;
                        if (metadata.shadercode) {
                            sceGnmVsRegsSetAddress(&pipe->vs_regs,
                                (void *)metadata.shadercode);
                        }
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
                    if (metadata.shadercode) {
                        sceGnmPsRegsSetAddress(&pipe->ps_regs,
                            (void *)metadata.shadercode);
                    }
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
                        if (metadata.shadercode) {
                            sceGnmGsRegsSetAddress(&pipe->gs_regs,
                                (void *)metadata.shadercode);
                        }
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
                        if (metadata.shadercode) {
                            sceGnmHsRegsSetAddress(&pipe->hs_regs,
                                (void *)metadata.shadercode);
                        }
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
                        if (metadata.shadercode) {
                            sceGnmEsRegsSetAddress(&pipe->es_regs,
                                (void *)metadata.shadercode);
                        }
                    } else {
                        /* DS_VS or compiler fallback: treat as vertex shader.
                         * Extract VS registers so CmdBindPipeline can use them. */
                        const GnmVsShader *vs = (const GnmVsShader *)metadata.stage;
                        pipe->vs_regs = vs->registers;
                        pipe->has_ds_vs = true;
                        if (metadata.shadercode) {
                            sceGnmVsRegsSetAddress(&pipe->vs_regs,
                                (void *)metadata.shadercode);
                        }
                    }
                    pipe->tes_module = mod;
                    break;
                }
                default:
                    break;
                }
            }

            /* Keep the compiled binary alive for the pipeline's lifetime.
             * The stage registers contain GPU addresses that point into
             * this buffer (patched via sceGnm*RegsSetAddress above).
             * Store in the per-stage field; freed in DestroyPipeline.
             * Skip if binary == mod->binary (stub mode, no compilation). */
            if (binary && binary != mod->binary) {
                switch (stage->stage) {
                case VK_SHADER_STAGE_VERTEX_BIT:            pipe->vs_binary = binary; break;
                case VK_SHADER_STAGE_FRAGMENT_BIT:          pipe->ps_binary = binary; break;
                case VK_SHADER_STAGE_GEOMETRY_BIT:          pipe->gs_binary = binary; break;
                case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:    pipe->tcs_binary = binary; break;
                case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: pipe->tes_binary = binary; break;
                default: vk_ps4_free(alloc, binary); break;
                }
            }
        }

        /* A graphics pipeline must have at least VS with valid registers */
        if (!compile_ok || !vs_found) {
            vk_ps4_free(alloc, pipe->vertex_bindings);
            vk_ps4_free(alloc, pipe->vertex_attributes);
            vk_ps4_free(alloc, pipe->blend_attachments);
            vk_ps4_free(alloc, pipe->fetch_shader);
            if (pipe->vs_binary)  vk_ps4_free(alloc, pipe->vs_binary);
            if (pipe->ps_binary)  vk_ps4_free(alloc, pipe->ps_binary);
            if (pipe->gs_binary)  vk_ps4_free(alloc, pipe->gs_binary);
            if (pipe->tcs_binary) vk_ps4_free(alloc, pipe->tcs_binary);
            if (pipe->tes_binary) vk_ps4_free(alloc, pipe->tes_binary);
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

        /* Extract push constant inline register mapping from input usage slots.
         * psbc emits IMM_ALUFLOATCONST slots for each inlined push constant
         * dword, with apislot = push constant dword index and
         * startregister = user-data register. */
        for (uint32_t s = 0; s < pipe->vs_input_usage_slot_count; s++) {
            if (pipe->vs_input_usage_slots[s].usagetype == GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST) {
                uint8_t apislot = pipe->vs_input_usage_slots[s].apislot;
                if (apislot == 0xFE) {
                    /* base_vertex (vertexOffset) */
                    pipe->vs_base_vertex_reg = pipe->vs_input_usage_slots[s].startregister;
                    pipe->has_base_vertex_reg = true;
                } else if (apislot == 0xFF) {
                    /* start_instance (firstInstance) */
                    pipe->vs_start_instance_reg = pipe->vs_input_usage_slots[s].startregister;
                    pipe->has_start_instance_reg = true;
                } else if (pipe->vs_push_const_slot_count < VK_PS4_MAX_PUSH_CONST_DWORDS) {
                    /* Regular push constant dword */
                    pipe->vs_push_const_slots[pipe->vs_push_const_slot_count].dword_index = apislot;
                    pipe->vs_push_const_slots[pipe->vs_push_const_slot_count].user_data_reg =
                        pipe->vs_input_usage_slots[s].startregister;
                    pipe->vs_push_const_slot_count++;
                }
            }
        }
        for (uint32_t s = 0; s < pipe->ps_input_usage_slot_count; s++) {
            if (pipe->ps_input_usage_slots[s].usagetype == GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST) {
                uint8_t apislot = pipe->ps_input_usage_slots[s].apislot;
                /* Skip 0xFE/0xFF — these are VS-only special slots
                 * for base_vertex/start_instance. They should never
                 * appear in PS, but filter defensively. */
                if (apislot >= 0xFE) continue;
                if (pipe->ps_push_const_slot_count < VK_PS4_MAX_PUSH_CONST_DWORDS) {
                    pipe->ps_push_const_slots[pipe->ps_push_const_slot_count].dword_index = apislot;
                    pipe->ps_push_const_slots[pipe->ps_push_const_slot_count].user_data_reg =
                        pipe->ps_input_usage_slots[s].startregister;
                    pipe->ps_push_const_slot_count++;
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

        /* Check pipeline cache first */
        uint64_t cache_hash = 0;
        size_t cached_size = 0;
        void *cached_binary = NULL;
        if (pipelineCache && mod->binary && mod->binary_size > 0) {
            cache_hash = vk_ps4_pipeline_cache_hash(mod->binary, mod->binary_size,
                                                    (uint32_t)ci->stage.stage);
            cached_binary = vk_ps4_pipeline_cache_lookup(pipelineCache, cache_hash,
                                                         (uint32_t)ci->stage.stage,
                                                         &cached_size);
        }

        if (cached_binary && cached_size > 0) {
            binary = vk_ps4_alloc(alloc, cached_size, 16);
            if (binary) {
                memcpy(binary, cached_binary, cached_size);
                binary_size = cached_size;
                sceGnmShaderBinaryGetMetadata(binary, binary_size, &metadata);
            }
        } else {
            VkResult vr = vk_ps4_compile_shader_module(
                mod, ci->stage.stage, alloc, &binary, &binary_size, &metadata
            );
            if (vr != VK_SUCCESS) {
                if (binary && binary != mod->binary) {
                    vk_ps4_free(alloc, binary);
                }
                vk_ps4_free(alloc, pipe);
                pPipelines[i] = VK_NULL_HANDLE;
                overall_result = VK_ERROR_FEATURE_NOT_PRESENT;
                continue;
            }
            if (pipelineCache && binary && binary_size > 0 && mod->binary && mod->binary_size > 0) {
                vk_ps4_pipeline_cache_insert(pipelineCache, cache_hash,
                                             (uint32_t)ci->stage.stage,
                                             (uint32_t)mod->binary_size,
                                             binary, binary_size);
            }
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
                if (slots_bytes <= binary_size) {
                    const GnmInputUsageSlot *cs_slots =
                        (const GnmInputUsageSlot *)(base + sizeof(GnmShaderCommonData));
                    memcpy(pipe->vs_input_usage_slots, cs_slots,
                           nslots * sizeof(GnmInputUsageSlot));
                    pipe->vs_input_usage_slot_count = nslots;
                }
            }

            /* Extract push constant inline register mapping for CS.
             * Filter out 0xFE/0xFF special slots (VS-only base_vertex/
             * start_instance) defensively — they should never appear
             * in a CS shader. */
            for (uint32_t s = 0; s < pipe->vs_input_usage_slot_count; s++) {
                if (pipe->vs_input_usage_slots[s].usagetype == GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST) {
                    uint8_t apislot = pipe->vs_input_usage_slots[s].apislot;
                    if (apislot >= 0xFE) continue;
                    if (pipe->cs_push_const_slot_count < VK_PS4_MAX_PUSH_CONST_DWORDS) {
                        pipe->cs_push_const_slots[pipe->cs_push_const_slot_count].dword_index = apislot;
                        pipe->cs_push_const_slots[pipe->cs_push_const_slot_count].user_data_reg =
                            pipe->vs_input_usage_slots[s].startregister;
                        pipe->cs_push_const_slot_count++;
                    }
                }
            }

            cs_ok = true;
        }

        /* Keep the compiled binary alive — cs_regs contains a GPU address
         * that points into this buffer.  Freed in DestroyPipeline. */
        if (binary && binary != mod->binary) {
            pipe->cs_binary = binary;
        }

        if (!cs_ok) {
            if (pipe->cs_binary) vk_ps4_free(alloc, pipe->cs_binary);
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
    /* Free compiled GCN shader binaries (kept alive because stage
     * registers contain GPU addresses pointing into them) */
    if (pipe->vs_binary)  vk_ps4_free(alloc, pipe->vs_binary);
    if (pipe->ps_binary)  vk_ps4_free(alloc, pipe->ps_binary);
    if (pipe->gs_binary)  vk_ps4_free(alloc, pipe->gs_binary);
    if (pipe->tcs_binary) vk_ps4_free(alloc, pipe->tcs_binary);
    if (pipe->tes_binary) vk_ps4_free(alloc, pipe->tes_binary);
    if (pipe->cs_binary)  vk_ps4_free(alloc, pipe->cs_binary);
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
    layout->variable_descriptor_binding = UINT32_MAX;

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

    /* VK_EXT_descriptor_indexing: extract binding flags from pNext chain */
    VkBaseInStructure *chain = (VkBaseInStructure *)pCreateInfo->pNext;
    while (chain) {
        if (chain->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT) {
            VkDescriptorSetLayoutBindingFlagsCreateInfo *flags_info =
                (VkDescriptorSetLayoutBindingFlagsCreateInfo *)chain;
            if (flags_info->bindingCount > 0 && flags_info->pBindingFlags) {
                uint32_t fcount = flags_info->bindingCount;
                if (fcount > layout->binding_count) fcount = layout->binding_count;
                layout->binding_flags = vk_ps4_alloc_zero(alloc,
                    layout->binding_count * sizeof(VkDescriptorBindingFlags), 16);
                if (!layout->binding_flags) {
                    vk_ps4_free(alloc, layout->bindings);
                    vk_ps4_free(alloc, layout);
                    return VK_ERROR_OUT_OF_HOST_MEMORY;
                }
                memcpy(layout->binding_flags, flags_info->pBindingFlags,
                       fcount * sizeof(VkDescriptorBindingFlags));
                /* Find the variable-count binding (must be the last one
                 * with VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT) */
                for (uint32_t i = 0; i < fcount; i++) {
                    if (flags_info->pBindingFlags[i] &
                        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT) {
                        layout->variable_descriptor_binding = i;
                    }
                }
            }
            break;
        }
        chain = (VkBaseInStructure *)chain->pNext;
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
    vk_ps4_free(alloc, layout->binding_flags);
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
    /* Free all descriptor sets in the free list */
    for (uint32_t i = 0; i < pool->free_count; i++) {
        if (pool->free_list[i]) vk_ps4_free(alloc, pool->free_list[i]);
    }
    pool->free_count = 0;
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
    VkPs4DescriptorPool *pool = (VkPs4DescriptorPool *)pAllocateInfo->descriptorPool;

    /* VK_EXT_descriptor_indexing: extract variable descriptor counts from pNext */
    const uint32_t *var_counts = NULL;
    uint32_t var_count_n = 0;
    VkBaseInStructure *chain = (VkBaseInStructure *)pAllocateInfo->pNext;
    while (chain) {
        if (chain->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT) {
            VkDescriptorSetVariableDescriptorCountAllocateInfo *vc =
                (VkDescriptorSetVariableDescriptorCountAllocateInfo *)chain;
            var_counts = vc->pDescriptorCounts;
            var_count_n = vc->descriptorSetCount;
            break;
        }
        chain = (VkBaseInStructure *)chain->pNext;
    }

    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
        VkPs4DescriptorSet *set = NULL;

        /* Try to reuse from the pool's free list first */
        if (pool && pool->free_count > 0) {
            set = pool->free_list[--pool->free_count];
            pool->free_list[pool->free_count] = NULL;
            /* Clear the set for reuse */
            memset(set, 0, sizeof(*set));
        }

        if (!set) {
            set = vk_ps4_alloc_zero(alloc, sizeof(*set), 16);
            if (!set) {
                for (uint32_t j = 0; j < i; j++) {
                    vk_ps4_free(alloc, (void *)pDescriptorSets[j]);
                    pDescriptorSets[j] = VK_NULL_HANDLE;
                }
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
        }

        set->type = VK_PS4_OBJ_DESCRIPTOR_SET;
        set->device = dev;
        set->pool = pool;
        set->layout = (VkPs4DescriptorSetLayout *)pAllocateInfo->pSetLayouts[i];
        set->variable_descriptor_count = 0;

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

            /* VK_EXT_descriptor_indexing: override the variable-count binding's
             * descriptor count if provided in the allocate info pNext chain. */
            if (set->layout->variable_descriptor_binding != UINT32_MAX &&
                var_counts && i < var_count_n) {
                uint32_t vb = set->layout->variable_descriptor_binding;
                set->bindings[vb].count = var_counts[i];
                set->variable_descriptor_count = var_counts[i];
            }
        }

        if (pool) pool->sets_allocated++;
        pDescriptorSets[i] = (VkDescriptorSet)set;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool,
                          uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets) {
    if (!device || !pDescriptorSets) return VK_SUCCESS;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4DescriptorPool *pool = (VkPs4DescriptorPool *)descriptorPool;
    for (uint32_t i = 0; i < descriptorSetCount; i++) {
        if (!pDescriptorSets[i]) continue;
        VkPs4DescriptorSet *set = (VkPs4DescriptorSet *)pDescriptorSets[i];
        /* Free binding resource arrays */
        for (uint32_t b = 0; b < set->binding_count; b++) {
            if (set->bindings[b].buffers) vk_ps4_free(alloc, set->bindings[b].buffers);
            if (set->bindings[b].textures) vk_ps4_free(alloc, set->bindings[b].textures);
            if (set->bindings[b].samplers) vk_ps4_free(alloc, set->bindings[b].samplers);
        }
        /* Add to the pool's free list for reuse, or free if pool is full */
        if (pool && pool->free_count < VK_PS4_MAX_POOLED_SETS) {
            pool->free_list[pool->free_count++] = set;
        } else {
            vk_ps4_free(alloc, set);
        }
        if (pool) pool->sets_freed++;
    }
    return VK_SUCCESS;
}

/* UpdateDescriptorSets moved to vk_ps4_descriptor.c */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) {
    (void)flags;
    if (!device || !descriptorPool) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4DescriptorPool *pool = (VkPs4DescriptorPool *)descriptorPool;

    /* Free all descriptor sets in the free list */
    for (uint32_t i = 0; i < pool->free_count; i++) {
        if (pool->free_list[i]) {
            vk_ps4_free(alloc, pool->free_list[i]);
            pool->free_list[i] = NULL;
        }
    }
    pool->free_count = 0;
    pool->sets_allocated = 0;
    pool->sets_freed = 0;
    return VK_SUCCESS;
}

/* === Sampler === */
/* (Moved to vk_ps4_descriptor.c for proper GnmSampler initialization) */
