/*
 * test_validation.c — Vulkan Validation Layer test.
 *
 * This test goes through the Vulkan loader (not direct dispatch) so that
 * the Khronos Validation Layer (VK_LAYER_KHRONOS_validation) can intercept
 * all API calls and report spec violations.
 *
 * Coverage:
 *   1. Instance + device creation
 *   2. Memory allocation, mapping, buffer creation/binding
 *   3. Image creation, memory binding
 *   4. Render pass + framebuffer
 *   5. Shader module creation (real SPIR-V)
 *   6. Graphics pipeline creation + binding
 *   7. Command buffer recording: begin render pass, clear, draw, end
 *   8. Descriptor set pool, alloc, update, bind
 *   9. Image layout transitions + pipeline barriers
 *  10. Copy commands (CmdCopyBuffer, CmdCopyBufferToImage)
 *  11. Queue submit + wait idle
 *  12. Indexed draws (CmdDrawIndexed with vertexOffset/firstInstance)
 *  13. Push constants (CmdPushConstants with VS push constant range)
 *  14. Compute pipeline (vkCreateComputePipelines + CmdDispatch)
 *  15. Multi-subpass render pass (CmdNextSubpass with input attachment)
 *  16. Indirect draws (CmdDrawIndirect + CmdDrawIndexedIndirect)
 *  17. Depth/stencil attachment (D32_SFLOAT_S8_UINT render pass + clear)
 *
 * Run with:
 *   VK_ICD_FILENAMES=<build_dir>/vulkan_ps4_icd.json \
 *   VK_LAYER_PATH=/opt/homebrew/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d \
 *   DYLD_LIBRARY_PATH=/opt/homebrew/opt/vulkan-validationlayers/lib \
 *   ./vk_ps4_validation_test
 *
 * Any validation errors are printed to stderr by the debug callback.
 */

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* === Helpers === */

static uint32_t *load_spirv(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    uint32_t *data = malloc(len);
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, len, f) != (size_t)len) {
        free(data); fclose(f); return NULL;
    }
    fclose(f);
    *out_size = (size_t)len;
    return data;
}

/* === Validation counters === */

typedef struct {
    int errors;
    int warnings;
} ValidationCounters;

static VKAPI_ATTR VkBool32 VKAPI_CALL
counting_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                  VkDebugUtilsMessageTypeFlagsEXT type,
                  const VkDebugUtilsMessengerCallbackDataEXT *data,
                  void *user_data) {
    (void)type;
    ValidationCounters *vc = (ValidationCounters *)user_data;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        vc->errors++;
        fprintf(stderr, "VVL ERROR: %s\n", data->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        vc->warnings++;
        fprintf(stderr, "VVL WARN: %s\n", data->pMessage);
    }
    return VK_FALSE;
}

/* === Main === */

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("=== vulkan-ps4 Validation Layer Test (expanded) ===\n\n");

    ValidationCounters vc = {0, 0};

    /* --- 1. Instance + device --- */
    const char *layers[] = { "VK_LAYER_KHRONOS_validation" };
    const char *inst_exts[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
                                VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
                                VK_KHR_SURFACE_EXTENSION_NAME };

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "vulkan-ps4-validation-test";
    app_info.applicationVersion = 1;
    app_info.pEngineName = "vulkan-ps4";
    app_info.engineVersion = 1;
    app_info.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app_info;
    ci.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    ci.enabledLayerCount = 1;
    ci.ppEnabledLayerNames = layers;
    ci.enabledExtensionCount = 3;
    ci.ppEnabledExtensionNames = inst_exts;

    VkInstance inst = VK_NULL_HANDLE;
    VkResult vr = vkCreateInstance(&ci, NULL, &inst);
    if (vr != VK_SUCCESS) {
        fprintf(stderr, "vkCreateInstance failed: %d\n", vr);
        return 1;
    }
    printf("Instance created OK\n");

    /* Debug messenger */
    VkDebugUtilsMessengerCreateInfoEXT dmci = {0};
    dmci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dmci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    dmci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dmci.pfnUserCallback = counting_callback;
    dmci.pUserData = &vc;

    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            inst, "vkCreateDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    if (vkCreateDebugUtilsMessengerEXT) {
        vkCreateDebugUtilsMessengerEXT(inst, &dmci, NULL, &messenger);
        printf("Debug messenger set up OK\n");
    } else {
        fprintf(stderr, "WARNING: vkCreateDebugUtilsMessengerEXT not found\n");
    }

    /* Physical device */
    uint32_t phys_count = 0;
    vkEnumeratePhysicalDevices(inst, &phys_count, NULL);
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    vkEnumeratePhysicalDevices(inst, &phys_count, &phys);
    printf("Physical device count: %u\n", phys_count);

    VkPhysicalDeviceProperties props = {0};
    vkGetPhysicalDeviceProperties(phys, &props);
    printf("Device: %s\n", props.deviceName);

    /* Queue family */
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qf_count, NULL);
    VkQueueFamilyProperties qf = {0};
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qf_count, &qf);

    /* Device */
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {0};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = 0;
    qci.queueCount = 1;
    qci.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo dci = {0};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    /* Enable Phase 4 extensions + features */
    const char *dev_exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,
        VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
        VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
        VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,
        VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME,
        VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME,
        VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,
        VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SHADER_SUBGROUP_EXTENDED_TYPES_EXTENSION_NAME,
        VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME,
        VK_EXT_SEPARATE_STENCIL_USAGE_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
    };
    dci.enabledExtensionCount = sizeof(dev_exts) / sizeof(dev_exts[0]);
    dci.ppEnabledExtensionNames = dev_exts;

    /* Enable features via pNext chain */
    VkPhysicalDeviceScalarBlockLayoutFeatures sbl_feat = {0};
    sbl_feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
    sbl_feat.scalarBlockLayout = VK_TRUE;

    VkPhysicalDeviceUniformBufferStandardLayoutFeatures ubsl_feat = {0};
    ubsl_feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES;
    ubsl_feat.pNext = &sbl_feat;
    ubsl_feat.uniformBufferStandardLayout = VK_TRUE;

    VkPhysicalDeviceHostQueryResetFeatures hqr_feat = {0};
    hqr_feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES;
    hqr_feat.pNext = &ubsl_feat;
    hqr_feat.hostQueryReset = VK_TRUE;

    VkPhysicalDeviceShaderAtomicInt64Features ai_feat = {0};
    ai_feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES;
    ai_feat.pNext = &hqr_feat;
    ai_feat.shaderBufferInt64Atomics = VK_TRUE;

    VkPhysicalDeviceBufferDeviceAddressFeatures bda_feat = {0};
    bda_feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bda_feat.pNext = &ai_feat;
    bda_feat.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures sse_feat = {0};
    sse_feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES;
    sse_feat.pNext = &bda_feat;
    sse_feat.shaderSubgroupExtendedTypes = VK_TRUE;

    VkPhysicalDeviceVulkanMemoryModelFeatures vmm_feat = {0};
    vmm_feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
    vmm_feat.pNext = &sse_feat;
    vmm_feat.vulkanMemoryModel = VK_TRUE;

    VkPhysicalDeviceImagelessFramebufferFeatures imgless_feat = {0};
    imgless_feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES;
    imgless_feat.pNext = &vmm_feat;
    imgless_feat.imagelessFramebuffer = VK_TRUE;

    VkPhysicalDeviceDescriptorIndexingFeatures desc_idx_feat = {0};
    desc_idx_feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    desc_idx_feat.pNext = &imgless_feat;
    desc_idx_feat.descriptorBindingVariableDescriptorCount = VK_TRUE;
    desc_idx_feat.runtimeDescriptorArray = VK_TRUE;

    VkPhysicalDeviceTimelineSemaphoreFeatures tl_feat = {0};
    tl_feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    tl_feat.pNext = &desc_idx_feat;
    tl_feat.timelineSemaphore = VK_TRUE;

    dci.pNext = &tl_feat;

    VkDevice dev = VK_NULL_HANDLE;
    vr = vkCreateDevice(phys, &dci, NULL, &dev);
    if (vr != VK_SUCCESS) {
        fprintf(stderr, "vkCreateDevice failed: %d\n", vr);
        /* Destroy messenger before instance on error paths */
        if (vkCreateDebugUtilsMessengerEXT && messenger) {
            PFN_vkDestroyDebugUtilsMessengerEXT ddu =
                (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                    inst, "vkDestroyDebugUtilsMessengerEXT");
            if (ddu) ddu(inst, messenger, NULL);
        }
        vkDestroyInstance(inst, NULL);
        return 1;
    }
    printf("Device created OK\n");

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(dev, 0, 0, &queue);
    printf("Queue acquired OK\n\n");

    /* --- 2. Memory + buffer --- */
    printf("--- Memory & Buffer ---\n");
    VkBufferCreateInfo bci = {0};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = 512 * 1024;  /* 512KB — enough for 256x256x4 RGBA image copy */
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buf = VK_NULL_HANDLE;
    vkCreateBuffer(dev, &bci, NULL, &buf);
    printf("Buffer created OK\n");

    VkMemoryRequirements bmr = {0};
    vkGetBufferMemoryRequirements(dev, buf, &bmr);

    VkMemoryAllocateInfo bmai = {0};
    bmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bmai.allocationSize = bmr.size;
    bmai.memoryTypeIndex = 0;

    VkDeviceMemory buf_mem = VK_NULL_HANDLE;
    vkAllocateMemory(dev, &bmai, NULL, &buf_mem);
    vkBindBufferMemory(dev, buf, buf_mem, 0);
    printf("Buffer memory bound OK\n");

    /* Map and fill vertex data */
    void *mapped = NULL;
    vkMapMemory(dev, buf_mem, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (mapped) {
        /* Write 3 vertices: position(xy) + color(rgb) = 5 floats * 3 = 60 bytes */
        float vertices[] = {
             0.0f, -0.5f,  1.0f, 0.0f, 0.0f,
             0.5f,  0.5f,  0.0f, 1.0f, 0.0f,
            -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
        };
        memcpy(mapped, vertices, sizeof(vertices));
        /* Flush with VK_WHOLE_SIZE to avoid nonCoherentAtomSize alignment issues */
        VkMappedMemoryRange flush_range = {0};
        flush_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        flush_range.memory = buf_mem;
        flush_range.offset = 0;
        flush_range.size = VK_WHOLE_SIZE;
        vkFlushMappedMemoryRanges(dev, 1, &flush_range);
        vkUnmapMemory(dev, buf_mem);
        printf("Vertex data written OK\n");
    }

    /* --- 3. Image + memory --- */
    printf("\n--- Image ---\n");
    VkImageCreateInfo imci = {0};
    imci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imci.imageType = VK_IMAGE_TYPE_2D;
    imci.format = VK_FORMAT_R8G8B8A8_UNORM;
    imci.extent.width = 256;
    imci.extent.height = 256;
    imci.extent.depth = 1;
    imci.mipLevels = 1;
    imci.arrayLayers = 1;
    imci.samples = VK_SAMPLE_COUNT_1_BIT;
    imci.tiling = VK_IMAGE_TILING_OPTIMAL;
    imci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    imci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage img = VK_NULL_HANDLE;
    vkCreateImage(dev, &imci, NULL, &img);
    printf("Image created OK\n");

    VkMemoryRequirements imr = {0};
    vkGetImageMemoryRequirements(dev, img, &imr);

    VkMemoryAllocateInfo imai = {0};
    imai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imai.allocationSize = imr.size;
    imai.memoryTypeIndex = 0;

    VkDeviceMemory img_mem = VK_NULL_HANDLE;
    vkAllocateMemory(dev, &imai, NULL, &img_mem);
    vkBindImageMemory(dev, img, img_mem, 0);
    printf("Image memory bound OK\n");

    /* Image view */
    VkImageViewCreateInfo ivci = {0};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = img;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.baseMipLevel = 0;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.baseArrayLayer = 0;
    ivci.subresourceRange.layerCount = 1;

    VkImageView img_view = VK_NULL_HANDLE;
    vkCreateImageView(dev, &ivci, NULL, &img_view);
    printf("Image view created OK\n");

    /* --- 4. Render pass + framebuffer --- */
    printf("\n--- Render Pass & Framebuffer ---\n");
    VkAttachmentDescription att = {0};
    att.format = VK_FORMAT_R8G8B8A8_UNORM;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference ref = {0};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sp = {0};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ref;

    VkRenderPassCreateInfo rpci = {0};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sp;

    VkRenderPass rp = VK_NULL_HANDLE;
    vkCreateRenderPass(dev, &rpci, NULL, &rp);
    printf("Render pass created OK\n");

    VkFramebufferCreateInfo fbci = {0};
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = rp;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &img_view;
    fbci.width = 256;
    fbci.height = 256;
    fbci.layers = 1;

    VkFramebuffer fb = VK_NULL_HANDLE;
    vkCreateFramebuffer(dev, &fbci, NULL, &fb);
    printf("Framebuffer created OK\n");

    /* --- 5. Shader modules --- */
    printf("\n--- Shader Modules ---\n");
    size_t vert_spv_size = 0, frag_spv_size = 0;
    uint32_t *vert_spv = load_spirv("tests/shaders/triangle_vert.spv", &vert_spv_size);
    uint32_t *frag_spv = load_spirv("tests/shaders/triangle_frag.spv", &frag_spv_size);

    VkShaderModule vert_mod = VK_NULL_HANDLE, frag_mod = VK_NULL_HANDLE;
    if (vert_spv && frag_spv) {
        VkShaderModuleCreateInfo smci = {0};
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = vert_spv_size;
        smci.pCode = vert_spv;
        vkCreateShaderModule(dev, &smci, NULL, &vert_mod);
        printf("Vertex shader module created OK\n");

        smci.codeSize = frag_spv_size;
        smci.pCode = frag_spv;
        vkCreateShaderModule(dev, &smci, NULL, &frag_mod);
        printf("Fragment shader module created OK\n");
    } else {
        fprintf(stderr, "WARNING: SPIR-V shaders not found — skipping pipeline test\n");
    }

    /* --- 6. Descriptor set layout (needed before pipeline layout) --- */
    printf("\n--- Descriptor Sets ---\n");
    VkDescriptorPoolSize pool_sizes[2] = {0};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = 1;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo dpci = {0};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = pool_sizes;

    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    vkCreateDescriptorPool(dev, &dpci, NULL, &desc_pool);
    printf("Descriptor pool created OK\n");

    VkDescriptorSetLayoutBinding ds_bindings[2] = {0};
    ds_bindings[0].binding = 0;
    ds_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ds_bindings[0].descriptorCount = 1;
    ds_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ds_bindings[1].binding = 1;
    ds_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ds_bindings[1].descriptorCount = 1;
    ds_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslci = {0};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 2;
    dslci.pBindings = ds_bindings;

    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl);
    printf("Descriptor set layout created OK\n");

    VkDescriptorSetAllocateInfo dsai = {0};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = desc_pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &dsl;

    VkDescriptorSet desc_set = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(dev, &dsai, &desc_set);
    printf("Descriptor set allocated OK\n");

    /* --- 7. Graphics pipeline --- */
    printf("\n--- Graphics Pipeline ---\n");
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    if (vert_mod && frag_mod) {
        /* Pipeline layout includes the descriptor set layout */
        VkPipelineLayoutCreateInfo plci = {0};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &dsl;
        plci.pushConstantRangeCount = 0;
        vkCreatePipelineLayout(dev, &plci, NULL, &pl);
        printf("Pipeline layout created OK\n");

        /* No vertex input — the triangle.vert shader uses gl_VertexIndex
         * to generate positions procedurally, no vertex attributes needed. */
        VkPipelineShaderStageCreateInfo stages[2] = {0};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert_mod;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag_mod;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vii = {0};
        vii.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo iai = {0};
        iai.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        iai.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        iai.primitiveRestartEnable = VK_FALSE;

        /* Viewport state: count only, actual viewport/scissor set dynamically */
        VkPipelineViewportStateCreateInfo vpsi = {0};
        vpsi.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpsi.viewportCount = 1;
        vpsi.scissorCount = 1;

        /* Dynamic state: viewport + scissor */
        VkDynamicState dyn_states[2] = { VK_DYNAMIC_STATE_VIEWPORT,
                                         VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dsci = {0};
        dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dsci.dynamicStateCount = 2;
        dsci.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rsi = {0};
        rsi.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rsi.depthClampEnable = VK_FALSE;
        rsi.rasterizerDiscardEnable = VK_FALSE;
        rsi.polygonMode = VK_POLYGON_MODE_FILL;
        rsi.lineWidth = 1.0f;
        rsi.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo msi = {0};
        msi.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        msi.minSampleShading = 1.0f;

        VkPipelineColorBlendAttachmentState cba = {0};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cbsi = {0};
        cbsi.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbsi.attachmentCount = 1;
        cbsi.pAttachments = &cba;

        VkGraphicsPipelineCreateInfo gpci = {0};
        gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpci.stageCount = 2;
        gpci.pStages = stages;
        gpci.pVertexInputState = &vii;
        gpci.pInputAssemblyState = &iai;
        gpci.pViewportState = &vpsi;
        gpci.pRasterizationState = &rsi;
        gpci.pMultisampleState = &msi;
        gpci.pColorBlendState = &cbsi;
        gpci.pDynamicState = &dsci;
        gpci.layout = pl;
        gpci.renderPass = rp;
        gpci.subpass = 0;

        vr = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline);
        if (vr == VK_SUCCESS) {
            printf("Graphics pipeline created OK\n");
        } else {
            fprintf(stderr, "vkCreateGraphicsPipelines failed: %d (VVL may have more info)\n", vr);
        }
    }

    /* --- 8. Update descriptor sets --- */
    VkDescriptorBufferInfo dbi = {0};
    dbi.buffer = buf;
    dbi.offset = 0;
    dbi.range = 64;  /* small UBO range */

    VkWriteDescriptorSet wds = {0};
    wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds.dstSet = desc_set;
    wds.dstBinding = 0;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wds.descriptorCount = 1;
    wds.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);
    printf("Descriptor set updated OK\n");

    /* --- 8. Sampler --- */
    VkSamplerCreateInfo smpci = {0};
    smpci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    smpci.magFilter = VK_FILTER_LINEAR;
    smpci.minFilter = VK_FILTER_LINEAR;
    smpci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    smpci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    smpci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    smpci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    smpci.minLod = 0.0f;
    smpci.maxLod = 1000.0f;

    VkSampler sampler = VK_NULL_HANDLE;
    vkCreateSampler(dev, &smpci, NULL, &sampler);
    printf("Sampler created OK\n");

    /* Create a separate texture image for the combined image sampler
     * binding.  The render target image is in COLOR_ATTACHMENT_OPTIMAL
     * during the draw, so it cannot be simultaneously sampled. */
    VkImageCreateInfo tex_imci = {0};
    tex_imci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    tex_imci.imageType = VK_IMAGE_TYPE_2D;
    tex_imci.format = VK_FORMAT_R8G8B8A8_UNORM;
    tex_imci.extent.width = 64;
    tex_imci.extent.height = 64;
    tex_imci.extent.depth = 1;
    tex_imci.mipLevels = 1;
    tex_imci.arrayLayers = 1;
    tex_imci.samples = VK_SAMPLE_COUNT_1_BIT;
    tex_imci.tiling = VK_IMAGE_TILING_OPTIMAL;
    tex_imci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    tex_imci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    tex_imci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage tex_img = VK_NULL_HANDLE;
    vkCreateImage(dev, &tex_imci, NULL, &tex_img);

    VkMemoryRequirements tex_imr = {0};
    vkGetImageMemoryRequirements(dev, tex_img, &tex_imr);
    VkMemoryAllocateInfo tex_imai = {0};
    tex_imai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    tex_imai.allocationSize = tex_imr.size;
    tex_imai.memoryTypeIndex = 0;
    VkDeviceMemory tex_img_mem = VK_NULL_HANDLE;
    vkAllocateMemory(dev, &tex_imai, NULL, &tex_img_mem);
    vkBindImageMemory(dev, tex_img, tex_img_mem, 0);

    VkImageViewCreateInfo tex_ivci = {0};
    tex_ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    tex_ivci.image = tex_img;
    tex_ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    tex_ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
    tex_ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    tex_ivci.subresourceRange.levelCount = 1;
    tex_ivci.subresourceRange.layerCount = 1;
    VkImageView tex_img_view = VK_NULL_HANDLE;
    vkCreateImageView(dev, &tex_ivci, NULL, &tex_img_view);
    printf("Texture image + view created OK\n");

    /* Update descriptor set with combined image sampler */
    VkDescriptorImageInfo dii = {0};
    dii.sampler = sampler;
    dii.imageView = tex_img_view;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet wds2 = {0};
    wds2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds2.dstSet = desc_set;
    wds2.dstBinding = 1;
    wds2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wds2.descriptorCount = 1;
    wds2.pImageInfo = &dii;
    vkUpdateDescriptorSets(dev, 1, &wds2, 0, NULL);
    printf("Descriptor set updated with image sampler OK\n");

    /* --- 9. Command buffer + recording --- */
    printf("\n--- Command Buffer Recording ---\n");
    VkCommandPoolCreateInfo cpci = {0};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = 0;
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    vkCreateCommandPool(dev, &cpci, NULL, &cmd_pool);
    printf("Command pool created OK\n");

    VkCommandBufferAllocateInfo cbai = {0};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(dev, &cbai, &cmd);
    printf("Command buffer allocated OK\n");

    VkCommandBufferBeginInfo cbbi = {0};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &cbbi);
    printf("Command buffer begun OK\n");

    /* --- 10. Image layout transition: UNDEFINED → TRANSFER_DST_OPTIMAL --- */
    VkImageMemoryBarrier img_barrier = {0};
    img_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    img_barrier.srcAccessMask = 0;
    img_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    img_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    img_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier.image = img;
    img_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    img_barrier.subresourceRange.baseMipLevel = 0;
    img_barrier.subresourceRange.levelCount = 1;
    img_barrier.subresourceRange.baseArrayLayer = 0;
    img_barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &img_barrier);
    printf("Image layout transition (UNDEFINED → TRANSFER_DST) OK\n");

    /* --- 11. CmdCopyBufferToImage --- */
    VkBufferImageCopy copy_region = {0};
    copy_region.bufferOffset = 0;
    copy_region.bufferRowLength = 0;
    copy_region.bufferImageHeight = 0;
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageOffset = (VkOffset3D){0, 0, 0};
    copy_region.imageExtent = (VkExtent3D){256, 256, 1};

    vkCmdCopyBufferToImage(cmd, buf, img,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);
    printf("CmdCopyBufferToImage OK\n");

    /* --- 12. Image layout transition: TRANSFER_DST → COLOR_ATTACHMENT_OPTIMAL --- */
    VkImageMemoryBarrier img_barrier2 = {0};
    img_barrier2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    img_barrier2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    img_barrier2.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    img_barrier2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    img_barrier2.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    img_barrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier2.image = img;
    img_barrier2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    img_barrier2.subresourceRange.baseMipLevel = 0;
    img_barrier2.subresourceRange.levelCount = 1;
    img_barrier2.subresourceRange.baseArrayLayer = 0;
    img_barrier2.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, NULL, 0, NULL, 1, &img_barrier2);
    printf("Image layout transition (TRANSFER_DST → COLOR_ATTACHMENT) OK\n");

    /* --- 12b. Texture image layout transition: UNDEFINED → SHADER_READ_ONLY --- */
    VkImageMemoryBarrier tex_barrier = {0};
    tex_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    tex_barrier.srcAccessMask = 0;
    tex_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    tex_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    tex_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tex_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    tex_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    tex_barrier.image = tex_img;
    tex_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    tex_barrier.subresourceRange.baseMipLevel = 0;
    tex_barrier.subresourceRange.levelCount = 1;
    tex_barrier.subresourceRange.baseArrayLayer = 0;
    tex_barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &tex_barrier);
    printf("Texture image layout transition (UNDEFINED → SHADER_READ_ONLY) OK\n");

    /* --- 13. Render pass begin + clear + draw + end --- */
    VkViewport vp2 = {0, 0, 256.0f, 256.0f, 0.0f, 1.0f};
    VkRect2D sc2 = {{0, 0}, {256, 256}};
    if (pipeline) {
        VkRenderPassBeginInfo rpbi = {0};
        rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass = rp;
        rpbi.framebuffer = fb;
        rpbi.renderArea.offset = (VkOffset2D){0, 0};
        rpbi.renderArea.extent = (VkExtent2D){256, 256};
        VkClearValue clear_val = {0};
        clear_val.color.float32[0] = 0.2f;
        clear_val.color.float32[1] = 0.2f;
        clear_val.color.float32[2] = 0.2f;
        clear_val.color.float32[3] = 1.0f;
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clear_val;

        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        printf("Render pass begun OK\n");

        /* Bind pipeline */
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        printf("Pipeline bound OK\n");

        /* Bind descriptor set */
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pl, 0, 1, &desc_set, 0, NULL);
        printf("Descriptor set bound OK\n");

        /* No vertex buffer binding — shader uses gl_VertexIndex */

        /* Set viewport + scissor (dynamic state) */
        vkCmdSetViewport(cmd, 0, 1, &vp2);
        vkCmdSetScissor(cmd, 0, 1, &sc2);

        /* Draw */
        vkCmdDraw(cmd, 3, 1, 0, 0);
        printf("CmdDraw(3,1,0,0) OK\n");

        vkCmdEndRenderPass(cmd);
        printf("Render pass ended OK\n");
    } else {
        printf("(Skipping render pass draw — no pipeline)\n");
    }

    /* --- 13b. Indexed draw test (CmdDrawIndexed with vertexOffset/firstInstance) --- */
    VkBuffer idx_buf_cleanup = VK_NULL_HANDLE;
    VkDeviceMemory idx_mem_cleanup = VK_NULL_HANDLE;
    if (pipeline) {
        printf("\n--- Indexed Draw Test ---\n");
        /* Create an index buffer with indices [0, 1, 2] */
        VkBufferCreateInfo ibci = {0};
        ibci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ibci.size = 3 * sizeof(uint16_t);
        ibci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        ibci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer idx_buf = VK_NULL_HANDLE;
        vkCreateBuffer(dev, &ibci, NULL, &idx_buf);
        idx_buf_cleanup = idx_buf;

        VkMemoryRequirements imr2 = {0};
        vkGetBufferMemoryRequirements(dev, idx_buf, &imr2);
        VkMemoryAllocateInfo imai2 = {0};
        imai2.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        imai2.allocationSize = imr2.size;
        imai2.memoryTypeIndex = 0;
        VkDeviceMemory idx_mem = VK_NULL_HANDLE;
        vkAllocateMemory(dev, &imai2, NULL, &idx_mem);
        idx_mem_cleanup = idx_mem;
        vkBindBufferMemory(dev, idx_buf, idx_mem, 0);

        /* Map and fill index data */
        void *idx_mapped = NULL;
        vkMapMemory(dev, idx_mem, 0, VK_WHOLE_SIZE, 0, &idx_mapped);
        if (idx_mapped) {
            uint16_t indices[] = {0, 1, 2};
            memcpy(idx_mapped, indices, sizeof(indices));
            VkMappedMemoryRange idx_flush = {0};
            idx_flush.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            idx_flush.memory = idx_mem;
            idx_flush.offset = 0;
            idx_flush.size = VK_WHOLE_SIZE;
            vkFlushMappedMemoryRanges(dev, 1, &idx_flush);
            vkUnmapMemory(dev, idx_mem);
        }

        /* Begin a new render pass for the indexed draw */
        VkRenderPassBeginInfo rpbi2 = {0};
        rpbi2.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi2.renderPass = rp;
        rpbi2.framebuffer = fb;
        rpbi2.renderArea.offset = (VkOffset2D){0, 0};
        rpbi2.renderArea.extent = (VkExtent2D){256, 256};
        VkClearValue cv2 = {0};
        rpbi2.clearValueCount = 1;
        rpbi2.pClearValues = &cv2;

        vkCmdBeginRenderPass(cmd, &rpbi2, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pl, 0, 1, &desc_set, 0, NULL);
        vkCmdSetViewport(cmd, 0, 1, &vp2);
        vkCmdSetScissor(cmd, 0, 1, &sc2);

        /* Bind index buffer */
        vkCmdBindIndexBuffer(cmd, idx_buf, 0, VK_INDEX_TYPE_UINT16);
        printf("Index buffer bound OK\n");

        /* Basic indexed draw */
        vkCmdDrawIndexed(cmd, 3, 1, 0, 0, 0);
        printf("CmdDrawIndexed(3,1,0,0,0) OK\n");

        /* Indexed draw with vertexOffset and firstInstance — tests the
         * SET_SH_REG path for base_vertex/start_instance registers */
        vkCmdDrawIndexed(cmd, 3, 1, 0, 5, 2);
        printf("CmdDrawIndexed(3,1,0,5,2) OK (vertexOffset=5, firstInstance=2)\n");

        /* Indexed draw with zero vertexOffset — tests that sticky registers
         * are always written (regression test for the != 0 guard bug) */
        vkCmdDrawIndexed(cmd, 3, 1, 0, 0, 0);
        printf("CmdDrawIndexed(3,1,0,0,0) OK (regression: zero vertexOffset)\n");

        vkCmdEndRenderPass(cmd);
        printf("Indexed draw render pass ended OK\n");
    }

    /* --- 13c. Push constant test --- */
    /* Push constant resources deferred to after submit (same reason as compute). */
    VkPipeline pc_pipeline_cleanup = VK_NULL_HANDLE;
    VkPipelineLayout pc_pl_cleanup = VK_NULL_HANDLE;
    VkShaderModule pc_mod_cleanup = VK_NULL_HANDLE;
    uint32_t *pc_spv_cleanup = NULL;
    {
        printf("\n--- Push Constant Test ---\n");
        size_t pc_spv_size = 0;
        uint32_t *pc_spv = load_spirv("tests/shaders/push_constant_vert.spv", &pc_spv_size);

        if (pc_spv) {
            VkShaderModule pc_mod = VK_NULL_HANDLE;
            VkShaderModuleCreateInfo smci = {0};
            smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smci.codeSize = pc_spv_size;
            smci.pCode = pc_spv;
            vkCreateShaderModule(dev, &smci, NULL, &pc_mod);
            printf("Push constant VS module created OK\n");

            /* Pipeline layout with push constant range */
            VkPushConstantRange pc_range = {0};
            pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            pc_range.offset = 0;
            pc_range.size = 16;  /* vec4 = 16 bytes */

            VkPipelineLayoutCreateInfo pc_plci = {0};
            pc_plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pc_plci.setLayoutCount = 1;
            pc_plci.pSetLayouts = &dsl;
            pc_plci.pushConstantRangeCount = 1;
            pc_plci.pPushConstantRanges = &pc_range;

            VkPipelineLayout pc_pl = VK_NULL_HANDLE;
            vkCreatePipelineLayout(dev, &pc_plci, NULL, &pc_pl);
            printf("Push constant pipeline layout created OK\n");

            /* Create pipeline using push constant VS + existing FS */
            VkPipelineShaderStageCreateInfo pc_stages[2] = {0};
            pc_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            pc_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            pc_stages[0].module = pc_mod;
            pc_stages[0].pName = "main";
            pc_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            pc_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            pc_stages[1].module = frag_mod;
            pc_stages[1].pName = "main";

            VkPipelineVertexInputStateCreateInfo pc_vii = {0};
            pc_vii.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

            VkPipelineInputAssemblyStateCreateInfo pc_iai = {0};
            pc_iai.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            pc_iai.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkPipelineViewportStateCreateInfo pc_vpsi = {0};
            pc_vpsi.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            pc_vpsi.viewportCount = 1;
            pc_vpsi.scissorCount = 1;

            VkDynamicState pc_dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT,
                                          VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo pc_dsci = {0};
            pc_dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            pc_dsci.dynamicStateCount = 2;
            pc_dsci.pDynamicStates = pc_dyn;

            VkPipelineRasterizationStateCreateInfo pc_rsi = {0};
            pc_rsi.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            pc_rsi.polygonMode = VK_POLYGON_MODE_FILL;
            pc_rsi.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo pc_msi = {0};
            pc_msi.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            pc_msi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            pc_msi.minSampleShading = 1.0f;

            VkPipelineColorBlendAttachmentState pc_cba = {0};
            pc_cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo pc_cbsi = {0};
            pc_cbsi.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            pc_cbsi.attachmentCount = 1;
            pc_cbsi.pAttachments = &pc_cba;

            VkGraphicsPipelineCreateInfo pc_gpci = {0};
            pc_gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pc_gpci.stageCount = 2;
            pc_gpci.pStages = pc_stages;
            pc_gpci.pVertexInputState = &pc_vii;
            pc_gpci.pInputAssemblyState = &pc_iai;
            pc_gpci.pViewportState = &pc_vpsi;
            pc_gpci.pRasterizationState = &pc_rsi;
            pc_gpci.pMultisampleState = &pc_msi;
            pc_gpci.pColorBlendState = &pc_cbsi;
            pc_gpci.pDynamicState = &pc_dsci;
            pc_gpci.layout = pc_pl;
            pc_gpci.renderPass = rp;
            pc_gpci.subpass = 0;

            VkPipeline pc_pipeline = VK_NULL_HANDLE;
            vr = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pc_gpci, NULL, &pc_pipeline);
            if (vr == VK_SUCCESS) {
                printf("Push constant pipeline created OK\n");

                /* Record a draw with push constants */
                VkRenderPassBeginInfo rpbi3 = {0};
                rpbi3.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rpbi3.renderPass = rp;
                rpbi3.framebuffer = fb;
                rpbi3.renderArea.offset = (VkOffset2D){0, 0};
                rpbi3.renderArea.extent = (VkExtent2D){256, 256};
                VkClearValue cv3 = {0};
                rpbi3.clearValueCount = 1;
                rpbi3.pClearValues = &cv3;

                vkCmdBeginRenderPass(cmd, &rpbi3, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pc_pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pc_pl, 0, 1, &desc_set, 0, NULL);
                vkCmdSetViewport(cmd, 0, 1, &vp2);
                vkCmdSetScissor(cmd, 0, 1, &sc2);

                /* Push constants: vec4 color (red) */
                float pc_color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                vkCmdPushConstants(cmd, pc_pl, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, pc_color);
                printf("CmdPushConstants(16 bytes) OK\n");

                vkCmdDraw(cmd, 3, 1, 0, 0);
                printf("Push constant draw OK\n");

                /* Push constants again with different color — tests
                 * that push constants can be updated between draws */
                float pc_color2[4] = {0.0f, 1.0f, 0.0f, 1.0f};
                vkCmdPushConstants(cmd, pc_pl, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, pc_color2);
                vkCmdDraw(cmd, 3, 1, 0, 0);
                printf("Push constant re-update + draw OK\n");

                vkCmdEndRenderPass(cmd);
                printf("Push constant render pass ended OK\n");
            } else {
                fprintf(stderr, "Push constant pipeline creation failed: %d\n", vr);
            }

            /* Save push constant resources for deferred cleanup (after submit) */
            pc_pipeline_cleanup = pc_pipeline;
            pc_pl_cleanup = pc_pl;
            pc_mod_cleanup = pc_mod;
            pc_spv_cleanup = pc_spv;
        } else {
            fprintf(stderr, "WARNING: push_constant_vert.spv not found — skipping push constant test\n");
            free(pc_spv);
        }
    }

    /* --- 13d. Compute pipeline test (CmdDispatch) --- */
    /* Compute resources declared at outer scope so they can be cleaned up
     * after the command buffer is submitted and waited on.  Destroying
     * them while the command buffer is still recording triggers VVL errors. */
    VkPipeline comp_pipeline_cleanup = VK_NULL_HANDLE;
    VkPipelineLayout comp_pl_cleanup = VK_NULL_HANDLE;
    VkDescriptorPool comp_pool_cleanup = VK_NULL_HANDLE;
    VkDescriptorSetLayout comp_dsl_cleanup = VK_NULL_HANDLE;
    VkShaderModule comp_mod_cleanup = VK_NULL_HANDLE;
    VkBuffer ssbo_buf_cleanup = VK_NULL_HANDLE;
    VkDeviceMemory ssbo_mem_cleanup = VK_NULL_HANDLE;
    uint32_t *comp_spv_cleanup = NULL;
    {
        printf("\n--- Compute Pipeline Test ---\n");
        size_t comp_spv_size = 0;
        uint32_t *comp_spv = load_spirv("tests/shaders/test_comp.spv", &comp_spv_size);

        if (comp_spv) {
            VkShaderModule comp_mod = VK_NULL_HANDLE;
            VkShaderModuleCreateInfo csmci = {0};
            csmci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            csmci.codeSize = comp_spv_size;
            csmci.pCode = comp_spv;
            vkCreateShaderModule(dev, &csmci, NULL, &comp_mod);
            printf("Compute shader module created OK\n");

            /* Storage buffer for compute output */
            VkBufferCreateInfo ssbo_ci = {0};
            ssbo_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            ssbo_ci.size = 64 * sizeof(uint32_t);  /* 64 workgroups × 4 bytes */
            ssbo_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            ssbo_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VkBuffer ssbo_buf = VK_NULL_HANDLE;
            vkCreateBuffer(dev, &ssbo_ci, NULL, &ssbo_buf);

            VkMemoryRequirements ssbo_mr = {0};
            vkGetBufferMemoryRequirements(dev, ssbo_buf, &ssbo_mr);
            VkMemoryAllocateInfo ssbo_mai = {0};
            ssbo_mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ssbo_mai.allocationSize = ssbo_mr.size;
            ssbo_mai.memoryTypeIndex = 0;
            VkDeviceMemory ssbo_mem = VK_NULL_HANDLE;
            vkAllocateMemory(dev, &ssbo_mai, NULL, &ssbo_mem);
            vkBindBufferMemory(dev, ssbo_buf, ssbo_mem, 0);
            printf("SSBO created OK\n");

            /* Descriptor set layout for compute: one storage buffer */
            VkDescriptorSetLayoutBinding comp_binding = {0};
            comp_binding.binding = 0;
            comp_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            comp_binding.descriptorCount = 1;
            comp_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorSetLayoutCreateInfo comp_dslci = {0};
            comp_dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            comp_dslci.bindingCount = 1;
            comp_dslci.pBindings = &comp_binding;

            VkDescriptorSetLayout comp_dsl = VK_NULL_HANDLE;
            vkCreateDescriptorSetLayout(dev, &comp_dslci, NULL, &comp_dsl);
            printf("Compute descriptor set layout created OK\n");

            /* Descriptor pool for compute */
            VkDescriptorPoolSize comp_pool_size = {0};
            comp_pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            comp_pool_size.descriptorCount = 1;

            VkDescriptorPoolCreateInfo comp_dpci = {0};
            comp_dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            comp_dpci.maxSets = 1;
            comp_dpci.poolSizeCount = 1;
            comp_dpci.pPoolSizes = &comp_pool_size;

            VkDescriptorPool comp_pool = VK_NULL_HANDLE;
            vkCreateDescriptorPool(dev, &comp_dpci, NULL, &comp_pool);

            VkDescriptorSetAllocateInfo comp_dsai = {0};
            comp_dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            comp_dsai.descriptorPool = comp_pool;
            comp_dsai.descriptorSetCount = 1;
            comp_dsai.pSetLayouts = &comp_dsl;

            VkDescriptorSet comp_ds = VK_NULL_HANDLE;
            vkAllocateDescriptorSets(dev, &comp_dsai, &comp_ds);
            printf("Compute descriptor set allocated OK\n");

            /* Update descriptor set with SSBO */
            VkDescriptorBufferInfo comp_dbi = {0};
            comp_dbi.buffer = ssbo_buf;
            comp_dbi.offset = 0;
            comp_dbi.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet comp_wds = {0};
            comp_wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            comp_wds.dstSet = comp_ds;
            comp_wds.dstBinding = 0;
            comp_wds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            comp_wds.descriptorCount = 1;
            comp_wds.pBufferInfo = &comp_dbi;
            vkUpdateDescriptorSets(dev, 1, &comp_wds, 0, NULL);
            printf("Compute descriptor set updated OK\n");

            /* Compute pipeline layout */
            VkPipelineLayoutCreateInfo comp_plci = {0};
            comp_plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            comp_plci.setLayoutCount = 1;
            comp_plci.pSetLayouts = &comp_dsl;

            VkPipelineLayout comp_pl = VK_NULL_HANDLE;
            vkCreatePipelineLayout(dev, &comp_plci, NULL, &comp_pl);
            printf("Compute pipeline layout created OK\n");

            /* Compute pipeline */
            VkComputePipelineCreateInfo cpci = {0};
            cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpci.stage.module = comp_mod;
            cpci.stage.pName = "main";
            cpci.layout = comp_pl;

            VkPipeline comp_pipeline = VK_NULL_HANDLE;
            vr = vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &comp_pipeline);
            if (vr == VK_SUCCESS) {
                printf("Compute pipeline created OK\n");

                /* Record compute dispatch */
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, comp_pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    comp_pl, 0, 1, &comp_ds, 0, NULL);
                vkCmdDispatch(cmd, 1, 1, 1);
                printf("CmdDispatch(1,1,1) OK\n");

                /* Memory barrier to make compute output visible */
                VkBufferMemoryBarrier comp_barrier = {0};
                comp_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                comp_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                comp_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                comp_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                comp_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                comp_barrier.buffer = ssbo_buf;
                comp_barrier.offset = 0;
                comp_barrier.size = VK_WHOLE_SIZE;

                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_HOST_BIT,
                    0, 0, NULL, 1, &comp_barrier, 0, NULL);
                printf("Compute memory barrier OK\n");
            } else {
                fprintf(stderr, "Compute pipeline creation failed: %d\n", vr);
            }

            /* Save compute resources for deferred cleanup (after submit) */
            comp_pipeline_cleanup = comp_pipeline;
            comp_pl_cleanup = comp_pl;
            comp_pool_cleanup = comp_pool;
            comp_dsl_cleanup = comp_dsl;
            comp_mod_cleanup = comp_mod;
            ssbo_buf_cleanup = ssbo_buf;
            ssbo_mem_cleanup = ssbo_mem;
            comp_spv_cleanup = comp_spv;
        } else {
            fprintf(stderr, "WARNING: test_comp.spv not found — skipping compute test\n");
            free(comp_spv);
        }
    }

    /* --- 13e. Multi-subpass render pass test --- */
    /* Tests CmdNextSubpass with a 2-subpass render pass.
     * Subpass 0 renders to attachment 0 (color).
     * Subpass 1 reads attachment 0 as input attachment and writes to attachment 1.
     * This exercises the multi-subpass path in CmdBeginRenderPass/CmdNextSubpass. */
    VkFramebuffer ms_fb_cleanup = VK_NULL_HANDLE;
    VkRenderPass ms_rp_cleanup = VK_NULL_HANDLE;
    VkImageView ms_view2_cleanup = VK_NULL_HANDLE;
    VkImage ms_img2_cleanup = VK_NULL_HANDLE;
    VkDeviceMemory ms_img2_mem_cleanup = VK_NULL_HANDLE;
    {
        printf("\n--- Multi-Subpass Test ---\n");

        /* Create a second color image for subpass 1 output */
        VkImageCreateInfo ms_imci = {0};
        ms_imci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ms_imci.imageType = VK_IMAGE_TYPE_2D;
        ms_imci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ms_imci.extent.width = 256;
        ms_imci.extent.height = 256;
        ms_imci.extent.depth = 1;
        ms_imci.mipLevels = 1;
        ms_imci.arrayLayers = 1;
        ms_imci.samples = VK_SAMPLE_COUNT_1_BIT;
        ms_imci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ms_imci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ms_imci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ms_imci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImage ms_img2 = VK_NULL_HANDLE;
        vkCreateImage(dev, &ms_imci, NULL, &ms_img2);

        VkMemoryRequirements ms_imr2 = {0};
        vkGetImageMemoryRequirements(dev, ms_img2, &ms_imr2);
        VkMemoryAllocateInfo ms_imai2 = {0};
        ms_imai2.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ms_imai2.allocationSize = ms_imr2.size;
        ms_imai2.memoryTypeIndex = 0;
        VkDeviceMemory ms_img2_mem = VK_NULL_HANDLE;
        vkAllocateMemory(dev, &ms_imai2, NULL, &ms_img2_mem);
        vkBindImageMemory(dev, ms_img2, ms_img2_mem, 0);

        VkImageViewCreateInfo ms_ivci2 = {0};
        ms_ivci2.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ms_ivci2.image = ms_img2;
        ms_ivci2.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ms_ivci2.format = VK_FORMAT_R8G8B8A8_UNORM;
        ms_ivci2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ms_ivci2.subresourceRange.levelCount = 1;
        ms_ivci2.subresourceRange.layerCount = 1;
        VkImageView ms_view2 = VK_NULL_HANDLE;
        vkCreateImageView(dev, &ms_ivci2, NULL, &ms_view2);
        printf("Second color image + view created OK\n");

        /* 2-attachment, 2-subpass render pass */
        VkAttachmentDescription ms_att[2] = {0};
        ms_att[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        ms_att[0].samples = VK_SAMPLE_COUNT_1_BIT;
        ms_att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ms_att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ms_att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ms_att[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ms_att[1].format = VK_FORMAT_R8G8B8A8_UNORM;
        ms_att[1].samples = VK_SAMPLE_COUNT_1_BIT;
        ms_att[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        ms_att[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ms_att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ms_att[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference ms_color_ref0 = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference ms_color_ref1 = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference ms_input_ref = {0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        VkSubpassDescription ms_subpasses[2] = {0};
        ms_subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        ms_subpasses[0].colorAttachmentCount = 1;
        ms_subpasses[0].pColorAttachments = &ms_color_ref0;
        ms_subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        ms_subpasses[1].colorAttachmentCount = 1;
        ms_subpasses[1].pColorAttachments = &ms_color_ref1;
        ms_subpasses[1].inputAttachmentCount = 1;
        ms_subpasses[1].pInputAttachments = &ms_input_ref;

        /* Subpass dependency: subpass 1 waits for subpass 0 */
        VkSubpassDependency ms_dep = {0};
        ms_dep.srcSubpass = 0;
        ms_dep.dstSubpass = 1;
        ms_dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        ms_dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        ms_dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        ms_dep.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        ms_dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo ms_rpci = {0};
        ms_rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ms_rpci.attachmentCount = 2;
        ms_rpci.pAttachments = ms_att;
        ms_rpci.subpassCount = 2;
        ms_rpci.pSubpasses = ms_subpasses;
        ms_rpci.dependencyCount = 1;
        ms_rpci.pDependencies = &ms_dep;

        VkRenderPass ms_rp = VK_NULL_HANDLE;
        vr = vkCreateRenderPass(dev, &ms_rpci, NULL, &ms_rp);
        if (vr == VK_SUCCESS) {
            printf("Multi-subpass render pass created OK\n");

            /* Framebuffer with both attachments */
            VkImageView ms_fb_views[2] = { img_view, ms_view2 };
            VkFramebufferCreateInfo ms_fbci = {0};
            ms_fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ms_fbci.renderPass = ms_rp;
            ms_fbci.attachmentCount = 2;
            ms_fbci.pAttachments = ms_fb_views;
            ms_fbci.width = 256;
            ms_fbci.height = 256;
            ms_fbci.layers = 1;

            VkFramebuffer ms_fb = VK_NULL_HANDLE;
            vkCreateFramebuffer(dev, &ms_fbci, NULL, &ms_fb);
            printf("Multi-subpass framebuffer created OK\n");

            /* Record: begin render pass, draw subpass 0, next subpass, draw subpass 1, end */
            VkRenderPassBeginInfo ms_rpbi = {0};
            ms_rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            ms_rpbi.renderPass = ms_rp;
            ms_rpbi.framebuffer = ms_fb;
            ms_rpbi.renderArea.offset = (VkOffset2D){0, 0};
            ms_rpbi.renderArea.extent = (VkExtent2D){256, 256};
            VkClearValue ms_clears[2] = {0};
            ms_clears[0].color.float32[0] = 0.5f;
            ms_clears[0].color.float32[3] = 1.0f;
            ms_rpbi.clearValueCount = 2;
            ms_rpbi.pClearValues = ms_clears;

            vkCmdBeginRenderPass(cmd, &ms_rpbi, VK_SUBPASS_CONTENTS_INLINE);
            printf("Multi-subpass render pass begun OK\n");

            /* Subpass 0: set dynamic state (no draw — the existing pipeline
             * was created for the single-subpass render pass `rp`, which is
             * not compatible with `ms_rp`. Drawing would require a separate
             * pipeline created against `ms_rp`. The goal here is to test
             * CmdNextSubpass, not the draw itself.) */
            vkCmdSetViewport(cmd, 0, 1, &vp2);
            vkCmdSetScissor(cmd, 0, 1, &sc2);
            printf("Subpass 0 state set OK\n");

            /* Transition to subpass 1 */
            vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
            printf("CmdNextSubpass OK\n");

            /* Subpass 1: set dynamic state */
            vkCmdSetViewport(cmd, 0, 1, &vp2);
            vkCmdSetScissor(cmd, 0, 1, &sc2);
            printf("Subpass 1 state set OK\n");

            vkCmdEndRenderPass(cmd);
            printf("Multi-subpass render pass ended OK\n");

            /* Save for deferred cleanup (after submit) */
            ms_fb_cleanup = ms_fb;
            ms_rp_cleanup = ms_rp;
        } else {
            fprintf(stderr, "Multi-subpass render pass creation failed: %d\n", vr);
        }
        /* Save image resources for deferred cleanup */
        ms_view2_cleanup = ms_view2;
        ms_img2_cleanup = ms_img2;
        ms_img2_mem_cleanup = ms_img2_mem;
    }

    /* --- 13f. Indirect draw test (CmdDrawIndirect + CmdDrawIndexedIndirect) --- */
    /* Both implementations are complete but untested. This test fills a buffer
     * with draw parameters and issues indirect draws. */
    VkBuffer indirect_buf_cleanup = VK_NULL_HANDLE;
    VkDeviceMemory indirect_mem_cleanup = VK_NULL_HANDLE;
    {
        printf("\n--- Indirect Draw Test ---\n");

        /* Create a buffer for indirect draw args.
         * VkDrawIndirectCommand = { vertexCount, instanceCount, firstVertex, firstInstance }
         * = 4 uint32_t = 16 bytes */
        VkBufferCreateInfo idbci = {0};
        idbci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        idbci.size = 64;  /* enough for a few draw commands */
        idbci.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        idbci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer indirect_buf = VK_NULL_HANDLE;
        vkCreateBuffer(dev, &idbci, NULL, &indirect_buf);

        VkMemoryRequirements idmr = {0};
        vkGetBufferMemoryRequirements(dev, indirect_buf, &idmr);
        VkMemoryAllocateInfo idmai = {0};
        idmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        idmai.allocationSize = idmr.size;
        idmai.memoryTypeIndex = 0;
        VkDeviceMemory indirect_mem = VK_NULL_HANDLE;
        vkAllocateMemory(dev, &idmai, NULL, &indirect_mem);
        vkBindBufferMemory(dev, indirect_buf, indirect_mem, 0);

        /* Fill buffer with both draw commands at separate offsets:
         * Offset 0: VkDrawIndirectCommand {3,1,0,0} (16 bytes)
         * Offset 32: VkDrawIndexedIndirectCommand {3,1,0,0,0} (20 bytes)
         * Using separate offsets avoids the fragile prefix-overlap where
         * refilling the buffer could silently corrupt the first command. */
        void *id_mapped = NULL;
        vkMapMemory(dev, indirect_mem, 0, VK_WHOLE_SIZE, 0, &id_mapped);
        if (id_mapped) {
            uint32_t draw_args[4] = { 3, 1, 0, 0 };
            memcpy(id_mapped, draw_args, sizeof(draw_args));
            uint32_t indexed_args[5] = { 3, 1, 0, 0, 0 };
            memcpy((char *)id_mapped + 32, indexed_args, sizeof(indexed_args));
            VkMappedMemoryRange id_flush = {0};
            id_flush.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            id_flush.memory = indirect_mem;
            id_flush.offset = 0;
            id_flush.size = VK_WHOLE_SIZE;
            vkFlushMappedMemoryRanges(dev, 1, &id_flush);
            vkUnmapMemory(dev, indirect_mem);
        }
        printf("Indirect draw args buffer created OK\n");

        /* CmdDrawIndirect inside a render pass */
        if (pipeline) {
            VkRenderPassBeginInfo rpbi_id = {0};
            rpbi_id.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpbi_id.renderPass = rp;
            rpbi_id.framebuffer = fb;
            rpbi_id.renderArea.offset = (VkOffset2D){0, 0};
            rpbi_id.renderArea.extent = (VkExtent2D){256, 256};
            VkClearValue cv_id = {0};
            rpbi_id.clearValueCount = 1;
            rpbi_id.pClearValues = &cv_id;

            vkCmdBeginRenderPass(cmd, &rpbi_id, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pl, 0, 1, &desc_set, 0, NULL);
            vkCmdSetViewport(cmd, 0, 1, &vp2);
            vkCmdSetScissor(cmd, 0, 1, &sc2);

            vkCmdDrawIndirect(cmd, indirect_buf, 0, 1, sizeof(VkDrawIndirectCommand));
            printf("CmdDrawIndirect OK\n");

            vkCmdEndRenderPass(cmd);
            printf("Indirect draw render pass ended OK\n");
        }

        /* CmdDrawIndexedIndirect: uses the indexed draw args at offset 32.
         * { indexCount, instanceCount, firstIndex, vertexOffset, firstInstance }
         * = 5 uint32_t = 20 bytes. Also need an index buffer bound. */
        if (pipeline && idx_buf_cleanup) {
            VkRenderPassBeginInfo rpbi_id2 = {0};
            rpbi_id2.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpbi_id2.renderPass = rp;
            rpbi_id2.framebuffer = fb;
            rpbi_id2.renderArea.offset = (VkOffset2D){0, 0};
            rpbi_id2.renderArea.extent = (VkExtent2D){256, 256};
            VkClearValue cv_id2 = {0};
            rpbi_id2.clearValueCount = 1;
            rpbi_id2.pClearValues = &cv_id2;

            vkCmdBeginRenderPass(cmd, &rpbi_id2, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pl, 0, 1, &desc_set, 0, NULL);
            vkCmdSetViewport(cmd, 0, 1, &vp2);
            vkCmdSetScissor(cmd, 0, 1, &sc2);
            vkCmdBindIndexBuffer(cmd, idx_buf_cleanup, 0, VK_INDEX_TYPE_UINT16);

            vkCmdDrawIndexedIndirect(cmd, indirect_buf, 32, 1, sizeof(VkDrawIndexedIndirectCommand));
            printf("CmdDrawIndexedIndirect OK\n");

            vkCmdEndRenderPass(cmd);
            printf("Indexed indirect draw render pass ended OK\n");
        }

        /* Save indirect buffer for deferred cleanup (after submit) */
        indirect_buf_cleanup = indirect_buf;
        indirect_mem_cleanup = indirect_mem;
    }

    /* --- 13g. Depth/stencil attachment test --- */
    /* Tests a render pass with a depth/stencil attachment:
     * - Creates a D32_SFLOAT_S8_UINT depth/stencil image + view
     * - Creates a render pass with color + depth/stencil attachments
     * - Creates a framebuffer with both
     * - Begins render pass with depth/stencil clear, sets dynamic state, ends
     * This exercises the depth/stencil clear path in CmdBeginRenderPass. */
    VkFramebuffer ds_fb_cleanup = VK_NULL_HANDLE;
    VkRenderPass ds_rp_cleanup = VK_NULL_HANDLE;
    VkImageView ds_view_cleanup = VK_NULL_HANDLE;
    VkImage ds_img_cleanup = VK_NULL_HANDLE;
    VkDeviceMemory ds_img_mem_cleanup = VK_NULL_HANDLE;
    {
        printf("\n--- Depth/Stencil Attachment Test ---\n");

        /* Create a depth/stencil image (D32_SFLOAT_S8_UINT) */
        VkImageCreateInfo ds_imci = {0};
        ds_imci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ds_imci.imageType = VK_IMAGE_TYPE_2D;
        ds_imci.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
        ds_imci.extent.width = 256;
        ds_imci.extent.height = 256;
        ds_imci.extent.depth = 1;
        ds_imci.mipLevels = 1;
        ds_imci.arrayLayers = 1;
        ds_imci.samples = VK_SAMPLE_COUNT_1_BIT;
        ds_imci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ds_imci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ds_imci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ds_imci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImage ds_img = VK_NULL_HANDLE;
        vr = vkCreateImage(dev, &ds_imci, NULL, &ds_img);
        if (vr == VK_SUCCESS) {
            printf("Depth/stencil image created OK\n");
        } else {
            fprintf(stderr, "vkCreateImage (depth) failed: %d\n", vr);
        }

        VkMemoryRequirements ds_imr = {0};
        vkGetImageMemoryRequirements(dev, ds_img, &ds_imr);
        VkMemoryAllocateInfo ds_imai = {0};
        ds_imai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ds_imai.allocationSize = ds_imr.size;
        ds_imai.memoryTypeIndex = 0;
        VkDeviceMemory ds_img_mem = VK_NULL_HANDLE;
        vkAllocateMemory(dev, &ds_imai, NULL, &ds_img_mem);
        vkBindImageMemory(dev, ds_img, ds_img_mem, 0);

        /* Create depth/stencil image view — aspect mask must include
         * both DEPTH and STENCIL for a combined D+S format. */
        VkImageViewCreateInfo ds_ivci = {0};
        ds_ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ds_ivci.image = ds_img;
        ds_ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ds_ivci.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
        ds_ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                                              VK_IMAGE_ASPECT_STENCIL_BIT;
        ds_ivci.subresourceRange.levelCount = 1;
        ds_ivci.subresourceRange.layerCount = 1;
        VkImageView ds_view = VK_NULL_HANDLE;
        vr = vkCreateImageView(dev, &ds_ivci, NULL, &ds_view);
        if (vr == VK_SUCCESS) {
            printf("Depth/stencil image view created OK\n");
        } else {
            fprintf(stderr, "vkCreateImageView (depth) failed: %d\n", vr);
        }

        /* Render pass with color + depth/stencil attachments */
        VkAttachmentDescription ds_att[2] = {0};
        ds_att[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        ds_att[0].samples = VK_SAMPLE_COUNT_1_BIT;
        ds_att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ds_att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ds_att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ds_att[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ds_att[1].format = VK_FORMAT_D32_SFLOAT_S8_UINT;
        ds_att[1].samples = VK_SAMPLE_COUNT_1_BIT;
        ds_att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ds_att[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ds_att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ds_att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        ds_att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ds_att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference ds_color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference ds_depth_ref = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription ds_subpass = {0};
        ds_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        ds_subpass.colorAttachmentCount = 1;
        ds_subpass.pColorAttachments = &ds_color_ref;
        ds_subpass.pDepthStencilAttachment = &ds_depth_ref;

        VkRenderPassCreateInfo ds_rpci = {0};
        ds_rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ds_rpci.attachmentCount = 2;
        ds_rpci.pAttachments = ds_att;
        ds_rpci.subpassCount = 1;
        ds_rpci.pSubpasses = &ds_subpass;

        VkRenderPass ds_rp = VK_NULL_HANDLE;
        vr = vkCreateRenderPass(dev, &ds_rpci, NULL, &ds_rp);
        if (vr == VK_SUCCESS) {
            printf("Depth/stencil render pass created OK\n");

            /* Framebuffer with color + depth/stencil */
            VkImageView ds_fb_views[2] = { img_view, ds_view };
            VkFramebufferCreateInfo ds_fbci = {0};
            ds_fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ds_fbci.renderPass = ds_rp;
            ds_fbci.attachmentCount = 2;
            ds_fbci.pAttachments = ds_fb_views;
            ds_fbci.width = 256;
            ds_fbci.height = 256;
            ds_fbci.layers = 1;

            VkFramebuffer ds_fb = VK_NULL_HANDLE;
            vr = vkCreateFramebuffer(dev, &ds_fbci, NULL, &ds_fb);
            if (vr == VK_SUCCESS) {
                printf("Depth/stencil framebuffer created OK\n");

                /* Begin render pass with depth/stencil clear values */
                VkRenderPassBeginInfo ds_rpbi = {0};
                ds_rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                ds_rpbi.renderPass = ds_rp;
                ds_rpbi.framebuffer = ds_fb;
                ds_rpbi.renderArea.offset = (VkOffset2D){0, 0};
                ds_rpbi.renderArea.extent = (VkExtent2D){256, 256};
                VkClearValue ds_clears[2] = {0};
                ds_clears[0].color.float32[3] = 1.0f;
                ds_clears[1].depthStencil.depth = 1.0f;
                ds_clears[1].depthStencil.stencil = 0;
                ds_rpbi.clearValueCount = 2;
                ds_rpbi.pClearValues = ds_clears;

                vkCmdBeginRenderPass(cmd, &ds_rpbi, VK_SUBPASS_CONTENTS_INLINE);
                printf("Depth/stencil render pass begun OK\n");

                /* Set dynamic state (no draw — the existing pipeline was
                 * created for the single-attachment render pass `rp`, not
                 * `ds_rp`. Drawing would require a separate pipeline with
                 * depth/stencil state created against `ds_rp`. The goal
                 * here is to test the depth/stencil clear path.) */
                vkCmdSetViewport(cmd, 0, 1, &vp2);
                vkCmdSetScissor(cmd, 0, 1, &sc2);
                printf("Depth/stencil dynamic state set OK\n");

                vkCmdEndRenderPass(cmd);
                printf("Depth/stencil render pass ended OK\n");

                ds_fb_cleanup = ds_fb;
            } else {
                fprintf(stderr, "vkCreateFramebuffer (depth) failed: %d\n", vr);
            }
            ds_rp_cleanup = ds_rp;
        } else {
            fprintf(stderr, "vkCreateRenderPass (depth) failed: %d\n", vr);
        }
        /* Save resources for deferred cleanup (after submit) */
        ds_view_cleanup = ds_view;
        ds_img_cleanup = ds_img;
        ds_img_mem_cleanup = ds_img_mem;
    }

    /* --- 14. Buffer memory barrier after transfer read + UBO read --- */
    VkBufferMemoryBarrier buf_barrier = {0};
    buf_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    /* Buffer was read as transfer source and as a UBO by the vertex shader */
    buf_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT |
                                VK_ACCESS_UNIFORM_READ_BIT;
    buf_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    buf_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buf_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buf_barrier.buffer = buf;
    buf_barrier.offset = 0;
    buf_barrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, NULL, 1, &buf_barrier, 0, NULL);
    printf("Buffer memory barrier OK\n");

    vkEndCommandBuffer(cmd);
    printf("Command buffer ended OK\n");

    /* --- 15. Queue submit + wait --- */
    printf("\n--- Queue Submit ---\n");
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    VkFenceCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(dev, &fci, NULL, &fence);

    vr = vkQueueSubmit(queue, 1, &si, fence);
    if (vr == VK_SUCCESS) printf("Queue submit OK\n");
    else fprintf(stderr, "vkQueueSubmit failed: %d\n", vr);

    /* Wait for fence */
    vr = vkWaitForFences(dev, 1, &fence, VK_TRUE, 1000000000ULL);
    if (vr == VK_SUCCESS) printf("Fence waited OK\n");
    else fprintf(stderr, "vkWaitForFences failed: %d\n", vr);

    vkQueueWaitIdle(queue);
    printf("Queue wait idle OK\n");

    /* --- 16. Phase 4 extension tests --- */
    printf("\n--- Phase 4 Extension Tests ---\n");

    /* 16a. VK_KHR_driver_properties */
    {
        VkPhysicalDeviceDriverProperties driver_props = {0};
        driver_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        VkPhysicalDeviceProperties2 props2 = {0};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &driver_props;
        vkGetPhysicalDeviceProperties2(phys, &props2);
        if (driver_props.driverID != 0) {
            printf("VK_KHR_driver_properties: driverName='%s' driverInfo='%s' OK\n",
                   driver_props.driverName, driver_props.driverInfo);
        } else {
            fprintf(stderr, "VK_KHR_driver_properties: driverID is 0\n");
            vc.errors++;
        }
    }

    /* 16b. VK_KHR_imageless_framebuffer */
    {
        /* Create a simple render pass with one color attachment */
        VkAttachmentDescription att = {0};
        att.format = VK_FORMAT_R8G8B8A8_UNORM;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference ref = {0};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub = {0};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        VkRenderPassCreateInfo rpci = {0};
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1;
        rpci.pAttachments = &att;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sub;

        VkRenderPass il_rp = VK_NULL_HANDLE;
        vr = vkCreateRenderPass(dev, &rpci, NULL, &il_rp);
        if (vr == VK_SUCCESS) {
            /* Create imageless framebuffer */
            VkFramebufferAttachmentsCreateInfo fb_att_ci = {0};
            fb_att_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO;
            fb_att_ci.attachmentImageInfoCount = 1;
            VkFramebufferAttachmentImageInfo fb_img_info = {0};
            fb_img_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
            fb_img_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            fb_img_info.width = 64;
            fb_img_info.height = 64;
            fb_img_info.layerCount = 1;
            fb_img_info.viewFormatCount = 1;
            fb_img_info.pViewFormats = &att.format;
            fb_att_ci.pAttachmentImageInfos = &fb_img_info;

            VkFramebufferCreateInfo fbci = {0};
            fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbci.pNext = &fb_att_ci;
            fbci.flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT_KHR;
            fbci.renderPass = il_rp;
            fbci.attachmentCount = 1;
            fbci.width = 64;
            fbci.height = 64;
            fbci.layers = 1;

            VkFramebuffer il_fb = VK_NULL_HANDLE;
            vr = vkCreateFramebuffer(dev, &fbci, NULL, &il_fb);
            if (vr == VK_SUCCESS) {
                printf("VK_KHR_imageless_framebuffer: framebuffer created OK\n");
                vkDestroyFramebuffer(dev, il_fb, NULL);
            } else {
                fprintf(stderr, "VK_KHR_imageless_framebuffer: create failed: %d\n", vr);
                vc.errors++;
            }
            vkDestroyRenderPass(dev, il_rp, NULL);
        } else {
            fprintf(stderr, "VK_KHR_imageless_framebuffer: render pass create failed: %d\n", vr);
            vc.errors++;
        }
    }

    /* 16c. VK_EXT_descriptor_indexing — variable descriptor count */
    {
        VkDescriptorSetLayoutBinding bindings[2] = {0};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[1].descriptorCount = 100;  /* max */
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorBindingFlags binding_flags[2] = {0};
        binding_flags[0] = 0;
        binding_flags[1] = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;

        VkDescriptorSetLayoutBindingFlagsCreateInfo flags_ci = {0};
        flags_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
        flags_ci.bindingCount = 2;
        flags_ci.pBindingFlags = binding_flags;

        VkDescriptorSetLayoutCreateInfo dslci = {0};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.pNext = &flags_ci;
        dslci.bindingCount = 2;
        dslci.pBindings = bindings;

        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        vr = vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl);
        if (vr == VK_SUCCESS) {
            printf("VK_EXT_descriptor_indexing: layout with variable count OK\n");

            /* Allocate with a smaller variable count (10 instead of 100) */
            VkDescriptorPoolSize pool_sizes[2] = {0};
            pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            pool_sizes[0].descriptorCount = 1;
            pool_sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            pool_sizes[1].descriptorCount = 100;

            VkDescriptorPoolCreateInfo dpci = {0};
            dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            dpci.maxSets = 1;
            dpci.poolSizeCount = 2;
            dpci.pPoolSizes = pool_sizes;

            VkDescriptorPool pool = VK_NULL_HANDLE;
            vr = vkCreateDescriptorPool(dev, &dpci, NULL, &pool);
            if (vr == VK_SUCCESS) {
                uint32_t var_count = 10;
                VkDescriptorSetVariableDescriptorCountAllocateInfo vci = {0};
                vci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
                vci.descriptorSetCount = 1;
                vci.pDescriptorCounts = &var_count;

                VkDescriptorSetAllocateInfo dsai = {0};
                dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                dsai.pNext = &vci;
                dsai.descriptorPool = pool;
                dsai.descriptorSetCount = 1;
                dsai.pSetLayouts = &dsl;

                VkDescriptorSet ds = VK_NULL_HANDLE;
                vr = vkAllocateDescriptorSets(dev, &dsai, &ds);
                if (vr == VK_SUCCESS) {
                    printf("VK_EXT_descriptor_indexing: allocated set with var count 10 OK\n");
                } else {
                    fprintf(stderr, "VK_EXT_descriptor_indexing: allocate failed: %d\n", vr);
                    vc.errors++;
                }
                vkDestroyDescriptorPool(dev, pool, NULL);
            }
            vkDestroyDescriptorSetLayout(dev, dsl, NULL);
        } else {
            fprintf(stderr, "VK_EXT_descriptor_indexing: layout create failed: %d\n", vr);
            vc.errors++;
        }
    }

    /* 16d. VK_KHR_timeline_semaphore */
    {
        VkSemaphoreTypeCreateInfoKHR stci = {0};
        stci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
        stci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
        stci.initialValue = 0;

        VkSemaphoreCreateInfo sci = {0};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        sci.pNext = &stci;

        VkSemaphore tl_sem = VK_NULL_HANDLE;
        vr = vkCreateSemaphore(dev, &sci, NULL, &tl_sem);
        if (vr == VK_SUCCESS) {
            printf("VK_KHR_timeline_semaphore: timeline semaphore created OK\n");

            /* Get counter value — should be 0 */
            uint64_t val = 0;
            PFN_vkGetSemaphoreCounterValueKHR vkGetSemCounter =
                (PFN_vkGetSemaphoreCounterValueKHR)vkGetDeviceProcAddr(dev, "vkGetSemaphoreCounterValueKHR");
            if (vkGetSemCounter) {
                vr = vkGetSemCounter(dev, tl_sem, &val);
                if (vr == VK_SUCCESS && val == 0) {
                    printf("VK_KHR_timeline_semaphore: initial counter value 0 OK\n");
                } else {
                    fprintf(stderr, "VK_KHR_timeline_semaphore: get counter failed: %d val=%llu\n", vr, (unsigned long long)val);
                    vc.errors++;
                }
            }

            /* Signal with value 5 */
            PFN_vkSignalSemaphoreKHR vkSignalSem =
                (PFN_vkSignalSemaphoreKHR)vkGetDeviceProcAddr(dev, "vkSignalSemaphoreKHR");
            if (vkSignalSem) {
                VkSemaphoreSignalInfoKHR ssi = {0};
                ssi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR;
                ssi.semaphore = tl_sem;
                ssi.value = 5;
                vr = vkSignalSem(dev, &ssi);
                if (vr == VK_SUCCESS) {
                    printf("VK_KHR_timeline_semaphore: signal value 5 OK\n");
                    if (vkGetSemCounter) {
                        vkGetSemCounter(dev, tl_sem, &val);
                        if (val == 5) {
                            printf("VK_KHR_timeline_semaphore: counter value 5 OK\n");
                        } else {
                            fprintf(stderr, "VK_KHR_timeline_semaphore: counter value %llu != 5\n", (unsigned long long)val);
                            vc.errors++;
                        }
                    }
                } else {
                    fprintf(stderr, "VK_KHR_timeline_semaphore: signal failed: %d\n", vr);
                    vc.errors++;
                }
            }

            /* Wait for value 5 — should return immediately */
            PFN_vkWaitSemaphoresKHR vkWaitSems =
                (PFN_vkWaitSemaphoresKHR)vkGetDeviceProcAddr(dev, "vkWaitSemaphoresKHR");
            if (vkWaitSems) {
                VkSemaphoreWaitInfoKHR wi = {0};
                wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
                wi.semaphoreCount = 1;
                wi.pSemaphores = &tl_sem;
                wi.pValues = (uint64_t[]){5};
                vr = vkWaitSems(dev, &wi, 1000000000ULL);
                if (vr == VK_SUCCESS) {
                    printf("VK_KHR_timeline_semaphore: wait value 5 OK\n");
                } else {
                    fprintf(stderr, "VK_KHR_timeline_semaphore: wait failed: %d\n", vr);
                    vc.errors++;
                }
            }

            vkDestroySemaphore(dev, tl_sem, NULL);
        } else {
            fprintf(stderr, "VK_KHR_timeline_semaphore: create failed: %d\n", vr);
            vc.errors++;
        }
    }

    /* 16e. VK_KHR_create_renderpass2 */
    {
        VkAttachmentDescription2 att2 = {0};
        att2.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
        att2.format = VK_FORMAT_R8G8B8A8_UNORM;
        att2.samples = VK_SAMPLE_COUNT_1_BIT;
        att2.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att2.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att2.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att2.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference2 ref2 = {0};
        ref2.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
        ref2.attachment = 0;
        ref2.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription2 sub2 = {0};
        sub2.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
        sub2.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub2.colorAttachmentCount = 1;
        sub2.pColorAttachments = &ref2;

        VkRenderPassCreateInfo2 rp2ci = {0};
        rp2ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
        rp2ci.attachmentCount = 1;
        rp2ci.pAttachments = &att2;
        rp2ci.subpassCount = 1;
        rp2ci.pSubpasses = &sub2;

        VkRenderPass rp2 = VK_NULL_HANDLE;
        PFN_vkCreateRenderPass2KHR vkCreateRenderPass2KHR =
            (PFN_vkCreateRenderPass2KHR)vkGetDeviceProcAddr(dev, "vkCreateRenderPass2KHR");
        if (vkCreateRenderPass2KHR) {
            vr = vkCreateRenderPass2KHR(dev, &rp2ci, NULL, &rp2);
            if (vr == VK_SUCCESS) {
                printf("VK_KHR_create_renderpass2: render pass created OK\n");
                vkDestroyRenderPass(dev, rp2, NULL);
            } else {
                fprintf(stderr, "VK_KHR_create_renderpass2: create failed: %d\n", vr);
                vc.errors++;
            }
        } else {
            fprintf(stderr, "VK_KHR_create_renderpass2: function not found\n");
            vc.errors++;
        }
    }

    /* 16f. VK_KHR_depth_stencil_resolve — properties query */
    {
        VkPhysicalDeviceDepthStencilResolveProperties ds_resolve_props = {0};
        ds_resolve_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES;
        VkPhysicalDeviceProperties2 props2 = {0};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &ds_resolve_props;
        vkGetPhysicalDeviceProperties2(phys, &props2);
        if (ds_resolve_props.supportedDepthResolveModes != 0) {
            printf("VK_KHR_depth_stencil_resolve: depthResolveModes=0x%x OK\n",
                   ds_resolve_props.supportedDepthResolveModes);
        } else {
            fprintf(stderr, "VK_KHR_depth_stencil_resolve: no resolve modes\n");
            vc.errors++;
        }
    }

    /* 16g. VK_EXT_scalar_block_layout — feature query */
    {
        VkPhysicalDeviceScalarBlockLayoutFeatures sbl_features = {0};
        sbl_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
        VkPhysicalDeviceFeatures2 f2 = {0};
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &sbl_features;
        vkGetPhysicalDeviceFeatures2(phys, &f2);
        if (sbl_features.scalarBlockLayout) {
            printf("VK_EXT_scalar_block_layout: scalarBlockLayout=TRUE OK\n");
        } else {
            fprintf(stderr, "VK_EXT_scalar_block_layout: scalarBlockLayout=FALSE\n");
            vc.errors++;
        }
    }

    /* 16h. VK_EXT_host_query_reset — reset query pool on host */
    {
        VkPhysicalDeviceHostQueryResetFeatures hqr_features = {0};
        hqr_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES;
        VkPhysicalDeviceFeatures2 f2 = {0};
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &hqr_features;
        vkGetPhysicalDeviceFeatures2(phys, &f2);
        if (hqr_features.hostQueryReset) {
            /* Create a query pool and reset it on the host */
            VkQueryPoolCreateInfo qpci = {0};
            qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            qpci.queryType = VK_QUERY_TYPE_OCCLUSION;
            qpci.queryCount = 4;
            VkQueryPool pool = VK_NULL_HANDLE;
            vr = vkCreateQueryPool(dev, &qpci, NULL, &pool);
            if (vr == VK_SUCCESS) {
                PFN_vkResetQueryPoolEXT vkResetQueryPool =
                    (PFN_vkResetQueryPoolEXT)vkGetDeviceProcAddr(dev, "vkResetQueryPoolEXT");
                if (vkResetQueryPool) {
                    vkResetQueryPool(dev, pool, 0, 4);
                    printf("VK_EXT_host_query_reset: reset query pool OK\n");
                } else {
                    fprintf(stderr, "VK_EXT_host_query_reset: function not found\n");
                    vc.errors++;
                }
                vkDestroyQueryPool(dev, pool, NULL);
            } else {
                fprintf(stderr, "VK_EXT_host_query_reset: query pool create failed: %d\n", vr);
                vc.errors++;
            }
        } else {
            fprintf(stderr, "VK_EXT_host_query_reset: hostQueryReset=FALSE\n");
            vc.errors++;
        }
    }

    /* 16i. VK_KHR_buffer_device_address — get buffer address */
    {
        VkPhysicalDeviceBufferDeviceAddressFeatures bda_features = {0};
        bda_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        VkPhysicalDeviceFeatures2 f2 = {0};
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &bda_features;
        vkGetPhysicalDeviceFeatures2(phys, &f2);
        if (bda_features.bufferDeviceAddress) {
            VkBufferCreateInfo bci = {0};
            bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bci.size = 256;
            bci.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            VkBuffer buf = VK_NULL_HANDLE;
            vr = vkCreateBuffer(dev, &bci, NULL, &buf);
            if (vr == VK_SUCCESS) {
                VkMemoryRequirements mr = {0};
                vkGetBufferMemoryRequirements(dev, buf, &mr);
                /* Find memory type */
                VkPhysicalDeviceMemoryProperties mp = {0};
                vkGetPhysicalDeviceMemoryProperties(phys, &mp);
                uint32_t mem_type = 0;
                for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
                    if ((mr.memoryTypeBits & (1u << i)) &&
                        (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                        mem_type = i;
                        break;
                    }
                }
                VkMemoryAllocateFlagsInfo mafi = {0};
                mafi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
                mafi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

                VkMemoryAllocateInfo mai = {0};
                mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                mai.pNext = &mafi;
                mai.allocationSize = mr.size;
                mai.memoryTypeIndex = mem_type;
                VkDeviceMemory mem = VK_NULL_HANDLE;
                vr = vkAllocateMemory(dev, &mai, NULL, &mem);
                if (vr == VK_SUCCESS) {
                    vkBindBufferMemory(dev, buf, mem, 0);
                    PFN_vkGetBufferDeviceAddressKHR vkGetBDA =
                        (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(dev, "vkGetBufferDeviceAddressKHR");
                    if (vkGetBDA) {
                        VkBufferDeviceAddressInfo bdai = {0};
                        bdai.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                        bdai.buffer = buf;
                        VkDeviceAddress addr = vkGetBDA(dev, &bdai);
                        if (addr != 0) {
                            printf("VK_KHR_buffer_device_address: addr=0x%llx OK\n",
                                   (unsigned long long)addr);
                        } else {
                            fprintf(stderr, "VK_KHR_buffer_device_address: addr=0\n");
                            vc.errors++;
                        }
                    } else {
                        fprintf(stderr, "VK_KHR_buffer_device_address: function not found\n");
                        vc.errors++;
                    }
                    vkFreeMemory(dev, mem, NULL);
                }
                vkDestroyBuffer(dev, buf, NULL);
            }
        } else {
            fprintf(stderr, "VK_KHR_buffer_device_address: bufferDeviceAddress=FALSE\n");
            vc.errors++;
        }
    }

    /* 16j. VK_KHR_vulkan_memory_model — feature query */
    {
        VkPhysicalDeviceVulkanMemoryModelFeatures vmm_features = {0};
        vmm_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
        VkPhysicalDeviceFeatures2 f2 = {0};
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &vmm_features;
        vkGetPhysicalDeviceFeatures2(phys, &f2);
        if (vmm_features.vulkanMemoryModel) {
            printf("VK_KHR_vulkan_memory_model: vulkanMemoryModel=TRUE OK\n");
        } else {
            fprintf(stderr, "VK_KHR_vulkan_memory_model: vulkanMemoryModel=FALSE\n");
            vc.errors++;
        }
    }

    /* 16k. VK_KHR_shader_atomic_int64 — feature query */
    {
        VkPhysicalDeviceShaderAtomicInt64Features ai_features = {0};
        ai_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES;
        VkPhysicalDeviceFeatures2 f2 = {0};
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &ai_features;
        vkGetPhysicalDeviceFeatures2(phys, &f2);
        if (ai_features.shaderBufferInt64Atomics) {
            printf("VK_KHR_shader_atomic_int64: shaderBufferInt64Atomics=TRUE OK\n");
        } else {
            fprintf(stderr, "VK_KHR_shader_atomic_int64: shaderBufferInt64Atomics=FALSE\n");
            vc.errors++;
        }
    }

    /* 16l. VK_KHR_uniform_buffer_standard_layout — feature query */
    {
        VkPhysicalDeviceUniformBufferStandardLayoutFeatures ubsl_features = {0};
        ubsl_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES;
        VkPhysicalDeviceFeatures2 f2 = {0};
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &ubsl_features;
        vkGetPhysicalDeviceFeatures2(phys, &f2);
        if (ubsl_features.uniformBufferStandardLayout) {
            printf("VK_KHR_uniform_buffer_standard_layout: uniformBufferStandardLayout=TRUE OK\n");
        } else {
            fprintf(stderr, "VK_KHR_uniform_buffer_standard_layout: uniformBufferStandardLayout=FALSE\n");
            vc.errors++;
        }
    }

    /* 16m. VK_KHR_shader_subgroup_extended_types — feature query */
    {
        VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures sse_features = {0};
        sse_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES;
        VkPhysicalDeviceFeatures2 f2 = {0};
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &sse_features;
        vkGetPhysicalDeviceFeatures2(phys, &f2);
        if (sse_features.shaderSubgroupExtendedTypes) {
            printf("VK_KHR_shader_subgroup_extended_types: shaderSubgroupExtendedTypes=TRUE OK\n");
        } else {
            fprintf(stderr, "VK_KHR_shader_subgroup_extended_types: shaderSubgroupExtendedTypes=FALSE\n");
            vc.errors++;
        }
    }

    /* === Phase 5 Performance Tests === */

    /* 17a. Pipeline cache — create, get data size, get data, create from data */
    {
        VkPipelineCache cache = VK_NULL_HANDLE;
        VkPipelineCacheCreateInfo pcci = {0};
        pcci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        vr = vkCreatePipelineCache(dev, &pcci, NULL, &cache);
        if (vr == VK_SUCCESS) {
            /* Get data size */
            size_t data_size = 0;
            vr = vkGetPipelineCacheData(dev, cache, &data_size, NULL);
            if (vr == VK_SUCCESS && data_size >= 32) {
                /* Get data */
                void *data = malloc(data_size);
                if (data) {
                    vr = vkGetPipelineCacheData(dev, cache, &data_size, data);
                    if (vr == VK_SUCCESS) {
                        /* Verify header */
                        uint32_t *hdr = (uint32_t *)data;
                        if (hdr[0] >= 32 && hdr[1] == 1) {
                            printf("Pipeline cache: header OK (size=%zu, vendor=0x%x, device=0x%x)\n",
                                   data_size, hdr[2], hdr[3]);
                        } else {
                            fprintf(stderr, "Pipeline cache: bad header\n");
                            vc.errors++;
                        }
                        /* Create a second cache from the data */
                        VkPipelineCacheCreateInfo pcci2 = {0};
                        pcci2.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
                        pcci2.initialDataSize = data_size;
                        pcci2.pInitialData = data;
                        VkPipelineCache cache2 = VK_NULL_HANDLE;
                        vr = vkCreatePipelineCache(dev, &pcci2, NULL, &cache2);
                        if (vr == VK_SUCCESS) {
                            printf("Pipeline cache: create from initial data OK\n");
                            vkDestroyPipelineCache(dev, cache2, NULL);
                        }
                    }
                    free(data);
                }
            } else {
                fprintf(stderr, "Pipeline cache: get data size failed: %d\n", vr);
                vc.errors++;
            }
            vkDestroyPipelineCache(dev, cache, NULL);
        } else {
            fprintf(stderr, "Pipeline cache: create failed: %d\n", vr);
            vc.errors++;
        }
    }

    /* 17b. Descriptor set pooling — alloc, free, re-alloc (should reuse) */
    {
        VkDescriptorPoolSize pool_sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4},
        };
        VkDescriptorPoolCreateInfo dpci = {0};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        dpci.maxSets = 4;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = pool_sizes;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        vr = vkCreateDescriptorPool(dev, &dpci, NULL, &pool);
        if (vr == VK_SUCCESS) {
            /* Simple layout with 1 UBO binding */
            VkDescriptorSetLayoutBinding binding = {0};
            binding.binding = 0;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            VkDescriptorSetLayoutCreateInfo dlci = {0};
            dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            dlci.bindingCount = 1;
            dlci.pBindings = &binding;
            VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
            vr = vkCreateDescriptorSetLayout(dev, &dlci, NULL, &dsl);
            if (vr == VK_SUCCESS) {
                VkDescriptorSetLayout layouts[2] = {dsl, dsl};
                VkDescriptorSet sets[2] = {0};
                VkDescriptorSetAllocateInfo dsai = {0};
                dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                dsai.descriptorPool = pool;
                dsai.descriptorSetCount = 2;
                dsai.pSetLayouts = layouts;
                vr = vkAllocateDescriptorSets(dev, &dsai, sets);
                if (vr == VK_SUCCESS) {
                    printf("Descriptor set pooling: allocated 2 sets OK\n");
                    /* Free them */
                    vr = vkFreeDescriptorSets(dev, pool, 2, sets);
                    if (vr == VK_SUCCESS) {
                        printf("Descriptor set pooling: freed 2 sets OK\n");
                        /* Re-allocate — should reuse from free list */
                        VkDescriptorSet sets2[2] = {0};
                        vr = vkAllocateDescriptorSets(dev, &dsai, sets2);
                        if (vr == VK_SUCCESS) {
                            printf("Descriptor set pooling: re-allocated 2 sets OK\n");
                            /* Free for cleanup */
                            vkFreeDescriptorSets(dev, pool, 2, sets2);
                        } else {
                            fprintf(stderr, "Descriptor set pooling: re-alloc failed: %d\n", vr);
                            vc.errors++;
                        }
                    } else {
                        fprintf(stderr, "Descriptor set pooling: free failed: %d\n", vr);
                        vc.errors++;
                    }
                } else {
                    fprintf(stderr, "Descriptor set pooling: alloc failed: %d\n", vr);
                    vc.errors++;
                }
                vkDestroyDescriptorSetLayout(dev, dsl, NULL);
            }
            vkDestroyDescriptorPool(dev, pool, NULL);
        } else {
            fprintf(stderr, "Descriptor set pooling: create pool failed: %d\n", vr);
            vc.errors++;
        }
    }

    /* 17c. Command buffer pooling — alloc, free, re-alloc (should reuse) */
    {
        VkCommandPoolCreateInfo cpci = {0};
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpci.queueFamilyIndex = 0;
        VkCommandPool pool = VK_NULL_HANDLE;
        vr = vkCreateCommandPool(dev, &cpci, NULL, &pool);
        if (vr == VK_SUCCESS) {
            VkCommandBufferAllocateInfo cbai = {0};
            cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cbai.commandPool = pool;
            cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cbai.commandBufferCount = 2;
            VkCommandBuffer cmds[2] = {0};
            vr = vkAllocateCommandBuffers(dev, &cbai, cmds);
            if (vr == VK_SUCCESS) {
                printf("Command buffer pooling: allocated 2 buffers OK\n");
                vkFreeCommandBuffers(dev, pool, 2, cmds);
                printf("Command buffer pooling: freed 2 buffers OK\n");
                /* Re-allocate — should reuse from free list */
                VkCommandBuffer cmds2[2] = {0};
                vr = vkAllocateCommandBuffers(dev, &cbai, cmds2);
                if (vr == VK_SUCCESS) {
                    printf("Command buffer pooling: re-allocated 2 buffers OK\n");
                    vkFreeCommandBuffers(dev, pool, 2, cmds2);
                } else {
                    fprintf(stderr, "Command buffer pooling: re-alloc failed: %d\n", vr);
                    vc.errors++;
                }
            } else {
                fprintf(stderr, "Command buffer pooling: alloc failed: %d\n", vr);
                vc.errors++;
            }
            vkDestroyCommandPool(dev, pool, NULL);
        } else {
            fprintf(stderr, "Command buffer pooling: create pool failed: %d\n", vr);
            vc.errors++;
        }
    }

    /* 17d. Pipeline cache merge — create two caches, merge */
    {
        VkPipelineCacheCreateInfo pcci = {0};
        pcci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VkPipelineCache c1 = VK_NULL_HANDLE, c2 = VK_NULL_HANDLE;
        vkCreatePipelineCache(dev, &pcci, NULL, &c1);
        vkCreatePipelineCache(dev, &pcci, NULL, &c2);
        if (c1 && c2) {
            VkPipelineCache src[1] = {c2};
            vr = vkMergePipelineCaches(dev, c1, 1, src);
            if (vr == VK_SUCCESS) {
                printf("Pipeline cache merge: OK\n");
            } else {
                fprintf(stderr, "Pipeline cache merge: failed: %d\n", vr);
                vc.errors++;
            }
            vkDestroyPipelineCache(dev, c1, NULL);
            vkDestroyPipelineCache(dev, c2, NULL);
        }
    }

    /* 17e. SPIR-V validation — invalid SPIR-V rejected, valid accepted */
    {
        /* Invalid: bad magic number.
         * Note: VVL will also report an error for this (expected).
         * We decrement vc.errors to compensate for the expected VVL error. */
        uint32_t bad_spirv[] = {0xDEADBEEF, 0x00010000, 0, 0, 0};
        VkShaderModuleCreateInfo bad_smci = {0};
        bad_smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        bad_smci.codeSize = sizeof(bad_spirv);
        bad_smci.pCode = bad_spirv;
        VkShaderModule bad_mod = VK_NULL_HANDLE;
        int errors_before = vc.errors;
        vr = vkCreateShaderModule(dev, &bad_smci, NULL, &bad_mod);
        if (vr != VK_SUCCESS) {
            printf("SPIR-V validation: invalid magic rejected OK (vr=%d)\n", vr);
            /* Compensate for expected VVL error */
            if (vc.errors > errors_before) vc.errors = errors_before;
        } else {
            fprintf(stderr, "SPIR-V validation: bad magic should have been rejected\n");
            vkDestroyShaderModule(dev, bad_mod, NULL);
            vc.errors++;
        }

        /* Valid: minimal SPIR-V vertex shader (void main() {}) */
        static const uint32_t valid_spirv[] = {
            0x07230203, 0x00010000, 0x0008000b, 0x00000006,
            0x00000000, 0x00020011, 0x00000001, 0x0006000b,
            0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
            0x00000000, 0x0003000e, 0x00000000, 0x00000001,
            0x0005000f, 0x00000000, 0x00000004, 0x6e69616d,
            0x00000000, 0x00030003, 0x00000002, 0x000001c2,
            0x00040005, 0x00000004, 0x6e69616d, 0x00000000,
            0x00020013, 0x00000002, 0x00030021, 0x00000003,
            0x00000002, 0x00050036, 0x00000002, 0x00000004,
            0x00000000, 0x00000003, 0x000200f8, 0x00000005,
            0x000100fd, 0x00010038,
        };
        VkShaderModuleCreateInfo good_smci = {0};
        good_smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        good_smci.codeSize = sizeof(valid_spirv);
        good_smci.pCode = valid_spirv;
        VkShaderModule good_mod = VK_NULL_HANDLE;
        vr = vkCreateShaderModule(dev, &good_smci, NULL, &good_mod);
        if (vr == VK_SUCCESS) {
            printf("SPIR-V validation: valid shader accepted OK\n");
            vkDestroyShaderModule(dev, good_mod, NULL);
        } else {
            fprintf(stderr, "SPIR-V validation: valid shader rejected: %d\n", vr);
            vc.errors++;
        }
    }

    /* Deferred resource cleanup — must happen after the command
     * buffer has finished executing, not while it's still recording. */
    /* Push constant resources */
    if (pc_pipeline_cleanup) vkDestroyPipeline(dev, pc_pipeline_cleanup, NULL);
    if (pc_pl_cleanup) vkDestroyPipelineLayout(dev, pc_pl_cleanup, NULL);
    if (pc_mod_cleanup) vkDestroyShaderModule(dev, pc_mod_cleanup, NULL);
    free(pc_spv_cleanup);
    /* Multi-subpass resources */
    if (ms_fb_cleanup) vkDestroyFramebuffer(dev, ms_fb_cleanup, NULL);
    if (ms_rp_cleanup) vkDestroyRenderPass(dev, ms_rp_cleanup, NULL);
    if (ms_view2_cleanup) vkDestroyImageView(dev, ms_view2_cleanup, NULL);
    if (ms_img2_cleanup) vkDestroyImage(dev, ms_img2_cleanup, NULL);
    if (ms_img2_mem_cleanup) vkFreeMemory(dev, ms_img2_mem_cleanup, NULL);
    /* Indirect draw resources */
    if (indirect_buf_cleanup) vkDestroyBuffer(dev, indirect_buf_cleanup, NULL);
    if (indirect_mem_cleanup) vkFreeMemory(dev, indirect_mem_cleanup, NULL);
    /* Depth/stencil resources */
    if (ds_fb_cleanup) vkDestroyFramebuffer(dev, ds_fb_cleanup, NULL);
    if (ds_rp_cleanup) vkDestroyRenderPass(dev, ds_rp_cleanup, NULL);
    if (ds_view_cleanup) vkDestroyImageView(dev, ds_view_cleanup, NULL);
    if (ds_img_cleanup) vkDestroyImage(dev, ds_img_cleanup, NULL);
    if (ds_img_mem_cleanup) vkFreeMemory(dev, ds_img_mem_cleanup, NULL);
    if (comp_pipeline_cleanup) vkDestroyPipeline(dev, comp_pipeline_cleanup, NULL);
    if (comp_pl_cleanup) vkDestroyPipelineLayout(dev, comp_pl_cleanup, NULL);
    if (comp_pool_cleanup) vkDestroyDescriptorPool(dev, comp_pool_cleanup, NULL);
    if (comp_dsl_cleanup) vkDestroyDescriptorSetLayout(dev, comp_dsl_cleanup, NULL);
    if (comp_mod_cleanup) vkDestroyShaderModule(dev, comp_mod_cleanup, NULL);
    if (ssbo_buf_cleanup) vkDestroyBuffer(dev, ssbo_buf_cleanup, NULL);
    if (ssbo_mem_cleanup) vkFreeMemory(dev, ssbo_mem_cleanup, NULL);
    free(comp_spv_cleanup);

    /* --- Cleanup --- */
    printf("\n--- Cleanup ---\n");
    vkDestroyFence(dev, fence, NULL);
    vkFreeCommandBuffers(dev, cmd_pool, 1, &cmd);
    vkDestroyCommandPool(dev, cmd_pool, NULL);
    vkDestroySampler(dev, sampler, NULL);
    /* Destroy pipeline → pipeline layout → descriptor set layout (children first) */
    if (pipeline) vkDestroyPipeline(dev, pipeline, NULL);
    if (pl) vkDestroyPipelineLayout(dev, pl, NULL);
    vkDestroyDescriptorPool(dev, desc_pool, NULL);
    vkDestroyDescriptorSetLayout(dev, dsl, NULL);
    if (vert_mod) vkDestroyShaderModule(dev, vert_mod, NULL);
    if (frag_mod) vkDestroyShaderModule(dev, frag_mod, NULL);
    vkDestroyFramebuffer(dev, fb, NULL);
    vkDestroyRenderPass(dev, rp, NULL);
    vkDestroyImageView(dev, img_view, NULL);
    vkDestroyImage(dev, img, NULL);
    vkFreeMemory(dev, img_mem, NULL);
    /* Texture image cleanup */
    vkDestroyImageView(dev, tex_img_view, NULL);
    vkDestroyImage(dev, tex_img, NULL);
    vkFreeMemory(dev, tex_img_mem, NULL);
    vkDestroyBuffer(dev, buf, NULL);
    vkFreeMemory(dev, buf_mem, NULL);
    /* Index buffer cleanup (from indexed draw test) */
    if (idx_buf_cleanup) vkDestroyBuffer(dev, idx_buf_cleanup, NULL);
    if (idx_mem_cleanup) vkFreeMemory(dev, idx_mem_cleanup, NULL);
    vkDestroyDevice(dev, NULL);

    if (vkCreateDebugUtilsMessengerEXT) {
        PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                inst, "vkDestroyDebugUtilsMessengerEXT");
        if (vkDestroyDebugUtilsMessengerEXT && messenger) {
            vkDestroyDebugUtilsMessengerEXT(inst, messenger, NULL);
        }
    }
    vkDestroyInstance(inst, NULL);

    free(vert_spv);
    free(frag_spv);

    printf("\n=== Validation Summary ===\n");
    printf("Errors:   %d\n", vc.errors);
    printf("Warnings: %d\n", vc.warnings);
    if (vc.errors > 0) {
        printf("RESULT: FAIL (%d validation errors)\n", vc.errors);
        return 1;
    }
    printf("RESULT: PASS (no validation errors)\n");
    return 0;
}
