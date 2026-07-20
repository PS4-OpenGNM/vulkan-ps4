/*
 * vk_ps4_instance.c — VkInstance / VkPhysicalDevice implementation.
 */

#include "vk_ps4_internal.h"

#include <string.h>

static const char *g_device_name = "PS4 GPU (GNM)";
static const char *g_driver_name = "vulkan-ps4";
static const char *g_driver_info = "Vulkan 1.0 ICD over OpenGNM";

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateInstance(
    const VkInstanceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkInstance *pInstance
) {
    if (!pCreateInfo || !pInstance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkAllocationCallbacks default_alloc = {0};
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &default_alloc;

    VkPs4Instance *inst = vk_ps4_alloc_zero(alloc, sizeof(*inst), 16);
    if (!inst) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    inst->type = VK_PS4_OBJ_INSTANCE;
    if (pAllocator) {
        inst->allocator = *pAllocator;
    }

    /* Pre-allocate the single physical device so handles are stable */
    VkPs4PhysicalDevice *phys = vk_ps4_alloc_zero(alloc, sizeof(*phys), 16);
    if (!phys) {
        vk_ps4_free(alloc, inst);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    phys->type = VK_PS4_OBJ_PHYSICAL_DEVICE;
    phys->instance = inst;

    /* Fill in properties */
    memset(&phys->properties, 0, sizeof(phys->properties));
    phys->properties.apiVersion = VK_PS4_API_VERSION;
    phys->properties.driverVersion = VK_PS4_DRIVER_VERSION;
    phys->properties.vendorID = 0x1002;  /* AMD */
    phys->properties.deviceID = 0x9920;  /* PS4 GPU (Liverpool) */
    phys->properties.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    strncpy(phys->properties.deviceName, g_device_name,
            sizeof(phys->properties.deviceName) - 1);
    memset(phys->properties.pipelineCacheUUID, 0, VK_UUID_SIZE);

    /* Memory properties: two heaps (Onion + Garlic) */
    memset(&phys->memory_properties, 0, sizeof(phys->memory_properties));
    phys->memory_properties.memoryTypeCount = VK_PS4_MEMORY_TYPE_COUNT;
    phys->memory_properties.memoryTypes[VK_PS4_MEMORY_TYPE_ONION].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    phys->memory_properties.memoryTypes[VK_PS4_MEMORY_TYPE_ONION].heapIndex = 0;
    phys->memory_properties.memoryTypes[VK_PS4_MEMORY_TYPE_GARLIC].propertyFlags =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    phys->memory_properties.memoryTypes[VK_PS4_MEMORY_TYPE_GARLIC].heapIndex = 1;
    phys->memory_properties.memoryHeapCount = 2;
    phys->memory_properties.memoryHeaps[0].size = 2ULL * 1024 * 1024 * 1024;
    phys->memory_properties.memoryHeaps[0].flags = 0;
    phys->memory_properties.memoryHeaps[1].size = 4ULL * 1024 * 1024 * 1024;
    phys->memory_properties.memoryHeaps[1].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

    /* Features — minimal for Vulkan 1.0 */
    memset(&phys->features, 0, sizeof(phys->features));
    phys->features.robustBufferAccess = VK_TRUE;
    phys->features.fullDrawIndexUint32 = VK_TRUE;
    phys->features.imageCubeArray = VK_TRUE;
    phys->features.independentBlend = VK_TRUE;
    phys->features.geometryShader = VK_TRUE;
    phys->features.tessellationShader = VK_TRUE;
    phys->features.sampleRateShading = VK_TRUE;
    phys->features.dualSrcBlend = VK_TRUE;
    phys->features.logicOp = VK_TRUE;
    phys->features.multiDrawIndirect = VK_TRUE;
    phys->features.drawIndirectFirstInstance = VK_TRUE;
    phys->features.depthClamp = VK_TRUE;
    phys->features.depthBiasClamp = VK_TRUE;
    phys->features.fillModeNonSolid = VK_TRUE;
    phys->features.depthBounds = VK_TRUE;
    phys->features.wideLines = VK_FALSE;
    phys->features.largePoints = VK_TRUE;
    phys->features.alphaToOne = VK_TRUE;
    phys->features.multiViewport = VK_TRUE;
    phys->features.samplerAnisotropy = VK_TRUE;
    phys->features.textureCompressionBC = VK_TRUE;
    phys->features.occlusionQueryPrecise = VK_TRUE;
    phys->features.pipelineStatisticsQuery = VK_FALSE;
    phys->features.vertexPipelineStoresAndAtomics = VK_TRUE;
    phys->features.fragmentStoresAndAtomics = VK_TRUE;
    phys->features.shaderTessellationAndGeometryPointSize = VK_TRUE;
    phys->features.shaderImageGatherExtended = VK_TRUE;
    phys->features.shaderStorageImageExtendedFormats = VK_TRUE;
    phys->features.shaderStorageImageMultisample = VK_FALSE;
    phys->features.shaderStorageImageReadWithoutFormat = VK_TRUE;
    phys->features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    phys->features.shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
    phys->features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    phys->features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    phys->features.shaderStorageImageArrayDynamicIndexing = VK_TRUE;
    phys->features.shaderClipDistance = VK_TRUE;
    phys->features.shaderCullDistance = VK_TRUE;
    phys->features.shaderFloat64 = VK_FALSE;
    phys->features.shaderInt64 = VK_TRUE;
    phys->features.shaderInt16 = VK_FALSE;
    phys->features.shaderResourceResidency = VK_FALSE;
    phys->features.shaderResourceMinLod = VK_TRUE;
    phys->features.sparseBinding = VK_FALSE;
    phys->features.sparseResidencyBuffer = VK_FALSE;
    phys->features.sparseResidencyImage2D = VK_FALSE;
    phys->features.sparseResidencyImage3D = VK_FALSE;
    phys->features.sparseResidency2Samples = VK_FALSE;
    phys->features.sparseResidency4Samples = VK_FALSE;
    phys->features.sparseResidency8Samples = VK_FALSE;
    phys->features.sparseResidency16Samples = VK_FALSE;
    phys->features.sparseResidencyAliased = VK_FALSE;
    phys->features.variableMultisampleRate = VK_TRUE;
    phys->features.inheritedQueries = VK_FALSE;

    inst->physical_device = phys;
    *pInstance = (VkInstance)inst;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator) {
    if (!instance) {
        return;
    }
    VkPs4Instance *inst = (VkPs4Instance *)instance;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &inst->allocator;
    /* Free cached physical device */
    if (inst->physical_device) {
        vk_ps4_free(alloc, inst->physical_device);
        inst->physical_device = NULL;
    }
    vk_ps4_free(alloc, inst);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_EnumeratePhysicalDevices(
    VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices
) {
    if (!instance || !pPhysicalDeviceCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Instance *inst = (VkPs4Instance *)instance;

    if (!pPhysicalDevices) {
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }
    if (*pPhysicalDeviceCount < 1) {
        *pPhysicalDeviceCount = 1;
        return VK_INCOMPLETE;
    }

    /* Return the cached physical device handle (stable across calls) */
    *pPhysicalDevices = (VkPhysicalDevice)inst->physical_device;
    *pPhysicalDeviceCount = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties
) {
    if (!physicalDevice || !pProperties) {
        return;
    }
    VkPs4PhysicalDevice *phys = (VkPs4PhysicalDevice *)physicalDevice;
    *pProperties = phys->properties;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties
) {
    if (!physicalDevice || !pMemoryProperties) {
        return;
    }
    VkPs4PhysicalDevice *phys = (VkPs4PhysicalDevice *)physicalDevice;
    *pMemoryProperties = phys->memory_properties;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueFamilyProperties
) {
    (void)physicalDevice;
    if (!pQueueFamilyPropertyCount) {
        return;
    }
    if (!pQueueFamilyProperties) {
        *pQueueFamilyPropertyCount = 1;
        return;
    }
    if (*pQueueFamilyPropertyCount < 1) {
        return;
    }
    pQueueFamilyProperties[0].queueFlags =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    pQueueFamilyProperties[0].queueCount = 1;
    pQueueFamilyProperties[0].timestampValidBits = 64;
    pQueueFamilyProperties[0].minImageTransferGranularity = (VkExtent3D){1, 1, 1};
    *pQueueFamilyPropertyCount = 1;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures
) {
    if (!physicalDevice || !pFeatures) {
        return;
    }
    VkPs4PhysicalDevice *phys = (VkPs4PhysicalDevice *)physicalDevice;
    *pFeatures = phys->features;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties *pFormatProperties
) {
    (void)physicalDevice;
    if (!pFormatProperties) {
        return;
    }
    *pFormatProperties = vk_ps4_format_properties(format);
}
