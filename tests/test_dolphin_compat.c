/*
 * test_dolphin_compat.c — Tests vulkan-ps4 compatibility with Dolphin emulator.
 *
 * Mimics Dolphin's VulkanContext initialization path:
 *   - vkEnumerateInstanceVersion (optional, Dolphin uses 1.1 if available)
 *   - Instance creation with VK_KHR_get_physical_device_properties_2
 *   - vkGetPhysicalDeviceProperties2 with VkPhysicalDeviceSubgroupProperties pNext
 *   - vkGetPhysicalDeviceFeatures (Dolphin queries specific features)
 *   - Device creation with Dolphin's feature set
 *   - VMA-like allocation (vkGetBufferMemoryRequirements2, vkBindBufferMemory2)
 *   - Render pass, framebuffer, command pool/buffer, pipeline cache
 *   - vkGetRenderAreaGranularity (Dolphin requires this)
 *
 * Run with:
 *   VK_ICD_FILENAMES=<build_dir>/vulkan_ps4_icd.json ./vk_ps4_dolphin_compat_test
 */

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_errors = 0;
static int g_warnings = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        g_errors++; \
    } else { \
        printf("OK: %s\n", msg); \
    } \
} while(0)

#define CHECK_VK(vr, msg) do { \
    if (vr != VK_SUCCESS) { \
        fprintf(stderr, "FAIL: %s (VkResult=%d)\n", msg, vr); \
        g_errors++; \
    } else { \
        printf("OK: %s\n", msg); \
    } \
} while(0)

int main(void) {
    printf("=== Dolphin Vulkan Compatibility Test ===\n\n");

    /* --- 1. vkEnumerateInstanceVersion (Dolphin: optional, uses 1.1 if available) --- */
    uint32_t instance_version = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion pfn_enumerate_instance_version =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (pfn_enumerate_instance_version) {
        pfn_enumerate_instance_version(&instance_version);
        printf("vkEnumerateInstanceVersion available, version=0x%x (%d.%d)\n",
               instance_version,
               VK_VERSION_MAJOR(instance_version),
               VK_VERSION_MINOR(instance_version));
        CHECK(instance_version >= VK_API_VERSION_1_1,
              "Instance version >= 1.1 (Dolphin will use 1.1)");
    } else {
        printf("vkEnumerateInstanceVersion not available — Dolphin falls back to 1.0\n");
    }

    /* --- 2. Create instance (Dolphin: headless, no surface) --- */
    /* Dolphin enables VK_KHR_get_physical_device_properties_2 optionally */
    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &ext_count, NULL);
    VkExtensionProperties *exts = calloc(ext_count, sizeof(*exts));
    vkEnumerateInstanceExtensionProperties(NULL, &ext_count, exts);
    int has_gpdp2 = 0;
    for (uint32_t i = 0; i < ext_count; i++) {
        if (strcmp(exts[i].extensionName, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) == 0)
            has_gpdp2 = 1;
    }
    free(exts);
    printf("VK_KHR_get_physical_device_properties_2: %s\n", has_gpdp2 ? "available" : "not available");

    const char *inst_exts[3];
    uint32_t inst_ext_count = 0;
    if (has_gpdp2) {
        inst_exts[inst_ext_count++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    }
    /* Required by Vulkan loader 1.4+ for portability drivers (macOS) */
    inst_exts[inst_ext_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Dolphin Compat Test";
    app_info.applicationVersion = 0;
    app_info.pEngineName = "Dolphin";
    app_info.engineVersion = 0;
    app_info.apiVersion = instance_version >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;

    VkInstanceCreateInfo inst_ci = {0};
    inst_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_ci.pApplicationInfo = &app_info;
    inst_ci.enabledExtensionCount = inst_ext_count;
    inst_ci.ppEnabledExtensionNames = inst_exts;
    inst_ci.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult vr = vkCreateInstance(&inst_ci, NULL, &instance);
    CHECK_VK(vr, "vkCreateInstance (Dolphin headless)");

    if (vr != VK_SUCCESS) goto summary;

    /* --- 3. Enumerate physical devices --- */
    uint32_t phys_count = 0;
    vkEnumeratePhysicalDevices(instance, &phys_count, NULL);
    CHECK(phys_count > 0, "At least one physical device");
    if (phys_count == 0) goto summary;

    VkPhysicalDevice *phys = calloc(phys_count, sizeof(*phys));
    vkEnumeratePhysicalDevices(instance, &phys_count, phys);
    VkPhysicalDevice physical_device = phys[0];
    free(phys);
    printf("Physical device count: %u\n", phys_count);

    /* --- 4. vkGetPhysicalDeviceProperties2 with subgroup properties (Dolphin path) --- */
    /* Dolphin queries VkPhysicalDeviceSubgroupProperties via pNext chain */
    VkPhysicalDeviceProperties2 props2 = {0};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

    VkPhysicalDeviceSubgroupProperties subgroup_props = {0};
    subgroup_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    props2.pNext = &subgroup_props;

    PFN_vkGetPhysicalDeviceProperties2 pfn_get_props2 =
        (PFN_vkGetPhysicalDeviceProperties2)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2");
    if (pfn_get_props2) {
        pfn_get_props2(physical_device, &props2);
        printf("Device: %s (vendor=0x%x, device=0x%x, api=0x%x)\n",
               props2.properties.deviceName,
               props2.properties.vendorID,
               props2.properties.deviceID,
               props2.properties.apiVersion);
        printf("Subgroup: size=%u, supportedOps=0x%x, supportedStages=0x%x\n",
               subgroup_props.subgroupSize,
               subgroup_props.supportedOperations,
               subgroup_props.supportedStages);
        CHECK(props2.properties.apiVersion >= VK_API_VERSION_1_1,
              "Physical device apiVersion >= 1.1");
        CHECK(subgroup_props.subgroupSize > 0,
              "Subgroup size > 0 (Dolphin queries this)");
    } else {
        fprintf(stderr, "WARN: vkGetPhysicalDeviceProperties2 not available\n");
        g_warnings++;
        vkGetPhysicalDeviceProperties(physical_device, &props2.properties);
    }

    /* --- 5. vkGetPhysicalDeviceFeatures (Dolphin queries specific features) --- */
    VkPhysicalDeviceFeatures features = {0};
    vkGetPhysicalDeviceFeatures(physical_device, &features);

    /* Dolphin checks these features (lines 91-104 of VulkanContext.cpp) */
    CHECK(features.dualSrcBlend, "dualSrcBlend (Dolphin uses)");
    CHECK(features.geometryShader, "geometryShader (Dolphin: critical)");
    CHECK(features.samplerAnisotropy, "samplerAnisotropy (Dolphin uses)");
    CHECK(features.logicOp, "logicOp (Dolphin uses)");
    CHECK(features.fragmentStoresAndAtomics, "fragmentStoresAndAtomics (Dolphin: critical)");
    CHECK(features.sampleRateShading, "sampleRateShading (Dolphin uses)");
    CHECK(features.largePoints, "largePoints (Dolphin: optional but used)");
    CHECK(features.shaderTessellationAndGeometryPointSize, "shaderTessellationAndGeometryPointSize (Dolphin uses)");
    CHECK(features.occlusionQueryPrecise, "occlusionQueryPrecise (Dolphin: optional)");
    CHECK(features.shaderClipDistance, "shaderClipDistance (Dolphin uses)");
    CHECK(features.depthClamp, "depthClamp (Dolphin uses)");
    CHECK(features.textureCompressionBC, "textureCompressionBC (Dolphin: critical)");

    /* --- 6. Queue family properties (Dolphin needs graphics queue) --- */
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &qf_count, NULL);
    VkQueueFamilyProperties *qf_props = calloc(qf_count, sizeof(*qf_props));
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &qf_count, qf_props);
    int graphics_queue = -1;
    for (uint32_t i = 0; i < qf_count; i++) {
        if (qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_queue = i;
            break;
        }
    }
    CHECK(graphics_queue >= 0, "Graphics queue family found (Dolphon: required)");
    free(qf_props);
    if (graphics_queue < 0) goto summary;

    /* --- 7. Enumerate device extensions (Dolphin: swapchain required for non-headless) --- */
    uint32_t dev_ext_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, NULL, &dev_ext_count, NULL);
    VkExtensionProperties *dev_exts = calloc(dev_ext_count, sizeof(*dev_exts));
    vkEnumerateDeviceExtensionProperties(physical_device, NULL, &dev_ext_count, dev_exts);
    int has_swapchain = 0, has_memory_budget = 0;
    for (uint32_t i = 0; i < dev_ext_count; i++) {
        if (strcmp(dev_exts[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
            has_swapchain = 1;
        if (strcmp(dev_exts[i].extensionName, "VK_EXT_memory_budget") == 0)
            has_memory_budget = 1;
    }
    free(dev_exts);
    CHECK(has_swapchain, "VK_KHR_swapchain (Dolphin: required for non-headless)");
    printf("VK_EXT_memory_budget: %s (Dolphin: optional, used by VMA)\n",
           has_memory_budget ? "available" : "not available");

    /* --- 8. Create device (Dolphin's exact feature set) --- */
    /* Dolphin enables only the features that are supported (see features() method) */
    VkPhysicalDeviceFeatures enabled_features = {0};
    enabled_features.dualSrcBlend = features.dualSrcBlend;
    enabled_features.geometryShader = features.geometryShader;
    enabled_features.samplerAnisotropy = features.samplerAnisotropy;
    enabled_features.logicOp = features.logicOp;
    enabled_features.fragmentStoresAndAtomics = features.fragmentStoresAndAtomics;
    enabled_features.sampleRateShading = features.sampleRateShading;
    enabled_features.largePoints = features.largePoints;
    enabled_features.shaderStorageImageMultisample = features.shaderStorageImageMultisample;
    enabled_features.shaderTessellationAndGeometryPointSize = features.shaderTessellationAndGeometryPointSize;
    enabled_features.occlusionQueryPrecise = features.occlusionQueryPrecise;
    enabled_features.shaderClipDistance = features.shaderClipDistance;
    enabled_features.depthClamp = features.depthClamp;
    enabled_features.textureCompressionBC = features.textureCompressionBC;

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_ci = {0};
    queue_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_ci.queueFamilyIndex = graphics_queue;
    queue_ci.queueCount = 1;
    queue_ci.pQueuePriorities = &queue_priority;

    /* Dolphin enables VK_KHR_swapchain (required for non-headless).
     * VK_KHR_get_physical_device_properties_2 is an instance extension;
     * Dolphin tries to add it as a device extension but it's optional and
     * will be skipped if not found in device extensions. */
    const char *dev_ext_names[1];
    uint32_t dev_ext_name_count = 0;
    if (has_swapchain)
        dev_ext_names[dev_ext_name_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    VkDeviceCreateInfo dev_ci = {0};
    dev_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_ci.queueCreateInfoCount = 1;
    dev_ci.pQueueCreateInfos = &queue_ci;
    dev_ci.enabledExtensionCount = dev_ext_name_count;
    dev_ci.ppEnabledExtensionNames = dev_ext_names;
    dev_ci.pEnabledFeatures = &enabled_features;

    VkDevice device = VK_NULL_HANDLE;
    vr = vkCreateDevice(physical_device, &dev_ci, NULL, &device);
    CHECK_VK(vr, "vkCreateDevice (Dolphin feature set)");
    if (vr != VK_SUCCESS) goto summary;

    /* --- 9. Get device queue --- */
    VkQueue queue;
    vkGetDeviceQueue(device, graphics_queue, 0, &queue);
    CHECK(queue != VK_NULL_HANDLE, "vkGetDeviceQueue (graphics)");

    /* --- 10. VMA-like: vkGetBufferMemoryRequirements2 + vkBindBufferMemory2 --- */
    /* VMA uses these with VMA_VULKAN_VERSION 1002000 */
    VkBufferCreateInfo buf_ci = {0};
    buf_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size = 4096;
    buf_ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer;
    vr = vkCreateBuffer(device, &buf_ci, NULL, &buffer);
    CHECK_VK(vr, "vkCreateBuffer (VMA-like)");

    VkBufferMemoryRequirementsInfo2 buf_mem_info2 = {0};
    buf_mem_info2.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
    buf_mem_info2.buffer = buffer;

    VkMemoryRequirements2 buf_mem_req2 = {0};
    buf_mem_req2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;

    PFN_vkGetBufferMemoryRequirements2 pfn_get_buf_mem2 =
        (PFN_vkGetBufferMemoryRequirements2)vkGetDeviceProcAddr(device, "vkGetBufferMemoryRequirements2");
    CHECK(pfn_get_buf_mem2 != NULL, "vkGetDeviceProcAddr(vkGetBufferMemoryRequirements2) (VMA needs)");
    if (pfn_get_buf_mem2) {
        pfn_get_buf_mem2(device, &buf_mem_info2, &buf_mem_req2);
        printf("Buffer memory requirements: size=%llu, alignment=%llu\n",
               (unsigned long long)buf_mem_req2.memoryRequirements.size,
               (unsigned long long)buf_mem_req2.memoryRequirements.alignment);
    }

    /* Allocate memory */
    VkMemoryAllocateInfo mem_alloc = {0};
    mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mem_alloc.allocationSize = buf_mem_req2.memoryRequirements.size;
    mem_alloc.memoryTypeIndex = 0;

    VkDeviceMemory buffer_mem;
    vr = vkAllocateMemory(device, &mem_alloc, NULL, &buffer_mem);
    CHECK_VK(vr, "vkAllocateMemory (buffer)");

    /* VMA uses vkBindBufferMemory2 */
    VkBindBufferMemoryInfo bind_info = {0};
    bind_info.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
    bind_info.buffer = buffer;
    bind_info.memory = buffer_mem;
    bind_info.memoryOffset = 0;

    PFN_vkBindBufferMemory2 pfn_bind_buf2 =
        (PFN_vkBindBufferMemory2)vkGetDeviceProcAddr(device, "vkBindBufferMemory2");
    CHECK(pfn_bind_buf2 != NULL, "vkGetDeviceProcAddr(vkBindBufferMemory2) (VMA needs)");
    if (pfn_bind_buf2) {
        vr = pfn_bind_buf2(device, 1, &bind_info);
        CHECK_VK(vr, "vkBindBufferMemory2 (VMA-like)");
    }

    /* --- 11. VMA-like: vkGetImageMemoryRequirements2 + vkBindImageMemory2 --- */
    VkImageCreateInfo img_ci = {0};
    img_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_ci.imageType = VK_IMAGE_TYPE_2D;
    img_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    img_ci.extent.width = 256;
    img_ci.extent.height = 256;
    img_ci.extent.depth = 1;
    img_ci.mipLevels = 1;
    img_ci.arrayLayers = 1;
    img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image;
    vr = vkCreateImage(device, &img_ci, NULL, &image);
    CHECK_VK(vr, "vkCreateImage (VMA-like)");

    VkImageMemoryRequirementsInfo2 img_mem_info2 = {0};
    img_mem_info2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    img_mem_info2.image = image;

    VkMemoryRequirements2 img_mem_req2 = {0};
    img_mem_req2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;

    PFN_vkGetImageMemoryRequirements2 pfn_get_img_mem2 =
        (PFN_vkGetImageMemoryRequirements2)vkGetDeviceProcAddr(device, "vkGetImageMemoryRequirements2");
    CHECK(pfn_get_img_mem2 != NULL, "vkGetDeviceProcAddr(vkGetImageMemoryRequirements2) (VMA needs)");
    if (pfn_get_img_mem2) {
        pfn_get_img_mem2(device, &img_mem_info2, &img_mem_req2);
        printf("Image memory requirements: size=%llu, alignment=%llu\n",
               (unsigned long long)img_mem_req2.memoryRequirements.size,
               (unsigned long long)img_mem_req2.memoryRequirements.alignment);
    }

    mem_alloc.allocationSize = img_mem_req2.memoryRequirements.size;
    VkDeviceMemory image_mem;
    vr = vkAllocateMemory(device, &mem_alloc, NULL, &image_mem);
    CHECK_VK(vr, "vkAllocateMemory (image)");

    VkBindImageMemoryInfo img_bind_info = {0};
    img_bind_info.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
    img_bind_info.image = image;
    img_bind_info.memory = image_mem;
    img_bind_info.memoryOffset = 0;

    PFN_vkBindImageMemory2 pfn_bind_img2 =
        (PFN_vkBindImageMemory2)vkGetDeviceProcAddr(device, "vkBindImageMemory2");
    CHECK(pfn_bind_img2 != NULL, "vkGetDeviceProcAddr(vkBindImageMemory2) (VMA needs)");
    if (pfn_bind_img2) {
        vr = pfn_bind_img2(device, 1, &img_bind_info);
        CHECK_VK(vr, "vkBindImageMemory2 (VMA-like)");
    }

    /* --- 12. vkGetPhysicalDeviceMemoryProperties2 (VMA uses for memory budget) --- */
    PFN_vkGetPhysicalDeviceMemoryProperties2 pfn_get_mem_props2 =
        (PFN_vkGetPhysicalDeviceMemoryProperties2)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceMemoryProperties2");
    CHECK(pfn_get_mem_props2 != NULL, "vkGetPhysicalDeviceMemoryProperties2 (VMA uses)");
    if (pfn_get_mem_props2) {
        VkPhysicalDeviceMemoryProperties2 mem_props2 = {0};
        mem_props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        pfn_get_mem_props2(physical_device, &mem_props2);
        printf("Memory types: %u, memory heaps: %u\n",
               mem_props2.memoryProperties.memoryTypeCount,
               mem_props2.memoryProperties.memoryHeapCount);
    }

    /* --- 13. Pipeline cache (Dolphin requires this) --- */
    VkPipelineCacheCreateInfo pc_ci = {0};
    pc_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    VkPipelineCache pipeline_cache;
    vr = vkCreatePipelineCache(device, &pc_ci, NULL, &pipeline_cache);
    CHECK_VK(vr, "vkCreatePipelineCache (Dolphin: required)");

    /* --- 14. Render pass + vkGetRenderAreaGranularity (Dolphin requires) --- */
    VkAttachmentDescription rp_att = {0};
    rp_att.format = VK_FORMAT_R8G8B8A8_UNORM;
    rp_att.samples = VK_SAMPLE_COUNT_1_BIT;
    rp_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    rp_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    rp_att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    rp_att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference rp_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription rp_subpass = {0};
    rp_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    rp_subpass.colorAttachmentCount = 1;
    rp_subpass.pColorAttachments = &rp_ref;

    VkRenderPassCreateInfo rp_ci = {0};
    rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_ci.attachmentCount = 1;
    rp_ci.pAttachments = &rp_att;
    rp_ci.subpassCount = 1;
    rp_ci.pSubpasses = &rp_subpass;

    VkRenderPass render_pass;
    vr = vkCreateRenderPass(device, &rp_ci, NULL, &render_pass);
    CHECK_VK(vr, "vkCreateRenderPass (Dolphin: required)");

    /* vkGetRenderAreaGranularity — Dolphin requires this (entry point line 145) */
    VkExtent2D granularity;
    vkGetRenderAreaGranularity(device, render_pass, &granularity);
    printf("Render area granularity: %ux%u\n", granularity.width, granularity.height);
    CHECK(granularity.width > 0 && granularity.height > 0,
          "vkGetRenderAreaGranularity (Dolphin: required)");

    /* --- 15. Command pool + command buffer (Dolphin: required) --- */
    VkCommandPoolCreateInfo cp_ci = {0};
    cp_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cp_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cp_ci.queueFamilyIndex = graphics_queue;

    VkCommandPool cmd_pool;
    vr = vkCreateCommandPool(device, &cp_ci, NULL, &cmd_pool);
    CHECK_VK(vr, "vkCreateCommandPool (Dolphin: required)");

    VkCommandBufferAllocateInfo cb_ai = {0};
    cb_ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_ai.commandPool = cmd_pool;
    cb_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vr = vkAllocateCommandBuffers(device, &cb_ai, &cmd);
    CHECK_VK(vr, "vkAllocateCommandBuffers (Dolphin: required)");

    /* --- 16. Descriptor set layout (Dolphin: required) --- */
    VkDescriptorSetLayoutBinding dsl_binding = {0};
    dsl_binding.binding = 0;
    dsl_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    dsl_binding.descriptorCount = 1;
    dsl_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl_ci = {0};
    dsl_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl_ci.bindingCount = 1;
    dsl_ci.pBindings = &dsl_binding;

    VkDescriptorSetLayout dsl;
    vr = vkCreateDescriptorSetLayout(device, &dsl_ci, NULL, &dsl);
    CHECK_VK(vr, "vkCreateDescriptorSetLayout (Dolphin: required)");

    /* --- 17. Pipeline layout (Dolphin: required) --- */
    VkPipelineLayoutCreateInfo pl_ci = {0};
    pl_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl_ci.setLayoutCount = 1;
    pl_ci.pSetLayouts = &dsl;

    VkPipelineLayout pipeline_layout;
    vr = vkCreatePipelineLayout(device, &pl_ci, NULL, &pipeline_layout);
    CHECK_VK(vr, "vkCreatePipelineLayout (Dolphin: required)");

    /* --- 18. Sampler (Dolphin: required) --- */
    VkSamplerCreateInfo sampler_ci = {0};
    sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_ci.magFilter = VK_FILTER_LINEAR;
    sampler_ci.minFilter = VK_FILTER_LINEAR;
    sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_ci.maxAnisotropy = 1.0f;
    sampler_ci.minLod = 0.0f;
    sampler_ci.maxLod = 1000.0f;

    VkSampler sampler;
    vr = vkCreateSampler(device, &sampler_ci, NULL, &sampler);
    CHECK_VK(vr, "vkCreateSampler (Dolphin: required)");

    /* --- 19. Query pool (Dolphin: perf queries) --- */
    VkQueryPoolCreateInfo qp_ci = {0};
    qp_ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qp_ci.queryType = VK_QUERY_TYPE_OCCLUSION;
    qp_ci.queryCount = 16;

    VkQueryPool query_pool;
    vr = vkCreateQueryPool(device, &qp_ci, NULL, &query_pool);
    CHECK_VK(vr, "vkCreateQueryPool (Dolphin: perf queries)");

    /* --- 20. Fence + semaphore (Dolphin: command buffer sync) --- */
    VkFenceCreateInfo fence_ci = {0};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vr = vkCreateFence(device, &fence_ci, NULL, &fence);
    CHECK_VK(vr, "vkCreateFence (Dolphin: required)");

    VkSemaphoreCreateInfo sem_ci = {0};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore semaphore;
    vr = vkCreateSemaphore(device, &sem_ci, NULL, &semaphore);
    CHECK_VK(vr, "vkCreateSemaphore (Dolphin: required)");

    /* --- 21. Record + submit a command buffer (Dolphin: core loop) --- */
    VkCommandBufferBeginInfo cmd_bi = {0};
    cmd_bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmd_bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vr = vkBeginCommandBuffer(cmd, &cmd_bi);
    CHECK_VK(vr, "vkBeginCommandBuffer (Dolphin: core loop)");

    if (vr == VK_SUCCESS) {
        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit_info = {0};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd;

        vr = vkQueueSubmit(queue, 1, &submit_info, fence);
        CHECK_VK(vr, "vkQueueSubmit (Dolphin: core loop)");

        if (vr == VK_SUCCESS) {
            vkWaitForFences(device, 1, &fence, VK_TRUE, 1000000000ULL);
            CHECK_VK(vkResetFences(device, 1, &fence), "vkResetFences (Dolphin: reuses fences)");
        }
    }

    /* --- 22. Get pipeline cache data (Dolphin: caches pipelines) --- */
    size_t cache_data_size = 0;
    vr = vkGetPipelineCacheData(device, pipeline_cache, &cache_data_size, NULL);
    CHECK_VK(vr, "vkGetPipelineCacheData (Dolphin: caches pipelines)");
    printf("Pipeline cache data size: %zu\n", cache_data_size);

    /* --- Cleanup --- */
    vkDeviceWaitIdle(device);
    vkDestroySemaphore(device, semaphore, NULL);
    vkDestroyFence(device, fence, NULL);
    vkDestroyQueryPool(device, query_pool, NULL);
    vkDestroySampler(device, sampler, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(device, dsl, NULL);
    vkDestroyCommandPool(device, cmd_pool, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyPipelineCache(device, pipeline_cache, NULL);
    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, image_mem, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, buffer_mem, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

summary:
    printf("\n=== Dolphin Compatibility Summary ===\n");
    printf("Errors:   %d\n", g_errors);
    printf("Warnings: %d\n", g_warnings);
    if (g_errors == 0) {
        printf("RESULT: PASS — vulkan-ps4 is compatible with Dolphin's Vulkan backend\n");
        return 0;
    } else {
        printf("RESULT: FAIL — %d compatibility errors\n", g_errors);
        return 1;
    }
}
