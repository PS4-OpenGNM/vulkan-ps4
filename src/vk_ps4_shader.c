/*
 * vk_ps4_shader.c — VkShaderModule implementation via libpsbc.
 *
 * vkCreateShaderModule compiles SPIR-V → GCN binary using libpsbc,
 * then extracts shader metadata (stage registers, input usage slots)
 * from the GnmShaderFileHeader.
 */

#include "vk_ps4_internal.h"

#include <string.h>

#ifdef VK_PS4_HAVE_PSBC
#include "psbc_compile.h"
#endif

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule) {
    if (!device || !pCreateInfo || !pShaderModule) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pCreateInfo->sType != VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4ShaderModule *mod = vk_ps4_alloc_zero(alloc, sizeof(*mod), 16);
    if (!mod) return VK_ERROR_OUT_OF_HOST_MEMORY;
    mod->type = VK_PS4_OBJ_SHADER_MODULE;
    mod->device = dev;
    mod->binary = NULL;
    mod->binary_size = 0;
    mod->has_metadata = false;

#ifdef VK_PS4_HAVE_PSBC
    /* Compile SPIR-V → GCN binary using libpsbc.
     * We don't know the stage from the module alone — SPIR-V doesn't encode
     * the stage in the module create info. The stage is determined when
     * creating the pipeline. So we store the SPIR-V and compile at pipeline
     * creation time.
     *
     * However, for the MVP we can try to detect the stage from SPIR-V
     * capabilities. For now, just store the SPIR-V and defer compilation. */

    /* Store a copy of the SPIR-V for later compilation */
    size_t spirv_size = pCreateInfo->codeSize;
    void *spirv_copy = vk_ps4_alloc(alloc, spirv_size, 4);
    if (!spirv_copy) {
        vk_ps4_free(alloc, mod);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(spirv_copy, pCreateInfo->pCode, spirv_size);

    /* Store SPIR-V as the "binary" for now — pipeline creation will compile it */
    mod->binary = spirv_copy;
    mod->binary_size = spirv_size;
#else
    /* No libpsbc — store SPIR-V as-is (stub mode) */
    size_t spirv_size = pCreateInfo->codeSize;
    void *spirv_copy = vk_ps4_alloc(alloc, spirv_size, 4);
    if (!spirv_copy) {
        vk_ps4_free(alloc, mod);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(spirv_copy, pCreateInfo->pCode, spirv_size);
    mod->binary = spirv_copy;
    mod->binary_size = spirv_size;
#endif

    *pShaderModule = (VkShaderModule)mod;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator) {
    if (!device || !shaderModule) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4ShaderModule *mod = (VkPs4ShaderModule *)shaderModule;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    if (mod->binary) {
        vk_ps4_free(alloc, mod->binary);
    }
    vk_ps4_free(alloc, mod);
}

/* Helper: compile a shader module for a specific stage using libpsbc.
 * Called from pipeline creation. Returns the compiled binary and metadata. */
VkResult vk_ps4_compile_shader_module(VkPs4ShaderModule *mod, VkShaderStageFlagBits stage,
                                      const VkAllocationCallbacks *alloc,
                                      void **out_binary, size_t *out_binary_size,
                                      GnmShaderMetadata *out_metadata) {
    if (!mod || !mod->binary || mod->binary_size == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

#ifdef VK_PS4_HAVE_PSBC
    /* Initialize libpsbc (refcounted) */
    psbc_init();

    /* Set up compile options */
    PsbcCompileOptions opts;
    opts.target = PSBC_TARGET_PS4_BASE;
    opts.entrypoint = "main";
    opts.optimise = true;

    /* Determine stage */
    switch (stage) {
    case VK_SHADER_STAGE_VERTEX_BIT: opts.stage = PSBC_STAGE_VERTEX; break;
    case VK_SHADER_STAGE_FRAGMENT_BIT: opts.stage = PSBC_STAGE_FRAGMENT; break;
    case VK_SHADER_STAGE_COMPUTE_BIT: opts.stage = PSBC_STAGE_COMPUTE; break;
    case VK_SHADER_STAGE_GEOMETRY_BIT: opts.stage = PSBC_STAGE_GEOMETRY; break;
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: opts.stage = PSBC_STAGE_TESS_CTRL; break;
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: opts.stage = PSBC_STAGE_TESS_EVAL; break;
    default: return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    /* Compile */
    PsbcShaderOutput output = {0};
    PsbcResult result = psbc_compile_shader(
        mod->binary,        /* SPIR-V data */
        mod->binary_size,   /* SPIR-V size */
        &opts,
        &output
    );

    if (result != PSBC_RESULT_OK) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    /* Copy the compiled binary */
    void *binary_copy = vk_ps4_alloc(alloc, output.size, 16);
    if (!binary_copy) {
        psbc_free_output(&output);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(binary_copy, output.data, output.size);

    *out_binary = binary_copy;
    *out_binary_size = output.size;

    /* Extract metadata from the compiled binary */
    if (out_metadata) {
        GnmError gnm_err = sceGnmShaderBinaryGetMetadata(
            binary_copy, output.size, out_metadata
        );
        if (gnm_err != GNM_ERROR_OK) {
            /* Metadata extraction failed — pipeline creation will handle this */
        }
    }

    psbc_free_output(&output);
    return VK_SUCCESS;
#else
    (void)stage;
    (void)out_metadata;
    /* No libpsbc — return the raw SPIR-V (stub mode, won't work on real GPU) */
    *out_binary = mod->binary;
    *out_binary_size = mod->binary_size;
    return VK_SUCCESS;
#endif
}
