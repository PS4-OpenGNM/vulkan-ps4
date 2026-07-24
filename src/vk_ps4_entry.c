/*
 * vk_ps4_entry.c — ICD entry point and loader interface.
 *
 * On host (for testing), the Vulkan loader calls vk_icdNegotiateLoaderICDInterfaceVersion
 * and vk_icdGetInstanceProcAddr to discover our functions.
 *
 * On PS4 (no loader), applications link libvulkan_ps4 and call vk* symbols
 * directly. The dispatch table in vk_ps4_dispatch.c maps names to implementations.
 */

#include "vk_ps4.h"
#include "vk_ps4_internal.h"

#include <stdlib.h>
#include <string.h>

/* === Loader interface === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pVersion) {
    if (!pVersion) return VK_ERROR_INITIALIZATION_FAILED;
    /* We support ICD interface version 5 (Vulkan 1.0+ loader).
     * Version 5 is sufficient for Vulkan 1.1 — the loader interface
     * version is about the ICD-loader contract, not the API version. */
    if (*pVersion >= 5) {
        *pVersion = 5;
        return VK_SUCCESS;
    }
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

/* Instance extensions: VK_KHR_surface + Vulkan 1.1 promoted instance extensions */
static const VkExtensionProperties g_instance_extensions[] = {
    {VK_KHR_SURFACE_EXTENSION_NAME, 1},
    /* Vulkan 1.1 promoted extensions (advertised for backwards compat) */
    {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, 2},
    {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, 1},
    {VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME, 1},
    {VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME, 1},
    {VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME, 1},
};
static const uint32_t g_instance_ext_count =
    sizeof(g_instance_extensions) / sizeof(g_instance_extensions[0]);

/* Device extensions: VK_KHR_swapchain + Vulkan 1.1 promoted device extensions */
static const VkExtensionProperties g_device_extensions[] = {
    {VK_KHR_SWAPCHAIN_EXTENSION_NAME, 1},
    {VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME, 1},
    /* Vulkan 1.1 promoted extensions (advertised for backwards compat) */
    {VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, 1},
    {VK_KHR_BIND_MEMORY_2_EXTENSION_NAME, 1},
    {VK_KHR_MAINTENANCE1_EXTENSION_NAME, 2},
    {VK_KHR_MAINTENANCE2_EXTENSION_NAME, 1},
    {VK_KHR_MAINTENANCE3_EXTENSION_NAME, 1},
    {VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME, 1},
    {VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME, 3},
    {VK_KHR_DEVICE_GROUP_EXTENSION_NAME, 4},
    {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, 1},
    {VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME, 1},
    {VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME, 1},
    {VK_KHR_MULTIVIEW_EXTENSION_NAME, 1},
    {VK_KHR_VARIABLE_POINTERS_EXTENSION_NAME, 1},
    {VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME, 1},
    {VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME, 1},
    {VK_KHR_16BIT_STORAGE_EXTENSION_NAME, 1},
    /* Phase 4: Optional extensions */
    {VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME, 1},
    {VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME, 1},
    {VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME, 1},
    {VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, 2},
    {VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME, 2},
};
static const uint32_t g_device_ext_count =
    sizeof(g_device_extensions) / sizeof(g_device_extensions[0]);

VKAPI_ATTR VkResult VKAPI_CALL
vk_icdEnumerateInstanceExtensionProperties(
    const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties
) {
    /* When pLayerName is non-NULL, the spec requires returning only that
     * layer's extensions.  This ICD supports no layers, so return 0. */
    if (pLayerName) {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }

    if (!pProperties) {
        *pPropertyCount = g_instance_ext_count;
        return VK_SUCCESS;
    }
    uint32_t avail = *pPropertyCount;
    if (avail < g_instance_ext_count) {
        /* Write as many as fit; report the number actually written */
        memcpy(pProperties, g_instance_extensions,
               avail * sizeof(VkExtensionProperties));
        *pPropertyCount = avail;
        return VK_INCOMPLETE;
    }
    memcpy(pProperties, g_instance_extensions, sizeof(g_instance_extensions));
    *pPropertyCount = g_instance_ext_count;
    return VK_SUCCESS;
}

/* Device extension enumeration — used by vkEnumerateDeviceExtensionProperties */
VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_enumerate_device_extensions(
    const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties
) {
    if (pLayerName) {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }
    if (!pProperties) {
        *pPropertyCount = g_device_ext_count;
        return VK_SUCCESS;
    }
    uint32_t avail = *pPropertyCount;
    if (avail < g_device_ext_count) {
        memcpy(pProperties, g_device_extensions,
               avail * sizeof(VkExtensionProperties));
        *pPropertyCount = avail;
        return VK_INCOMPLETE;
    }
    memcpy(pProperties, g_device_extensions, sizeof(g_device_extensions));
    *pPropertyCount = g_device_ext_count;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_icdEnumerateInstanceLayerProperties(
    uint32_t *pPropertyCount, VkLayerProperties *pProperties
) {
    (void)pProperties;
    /* No layers */
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

/* === vkGetInstanceProcAddr / vkGetDeviceProcAddr === */
/* These are implemented in vk_ps4_dispatch.c, which has the full name → function map. */

/* === Allocator helpers === */

void *vk_ps4_alloc(const VkAllocationCallbacks *alloc, size_t size, size_t alignment) {
    if (alloc && alloc->pfnAllocation) {
        return alloc->pfnAllocation(alloc->pUserData, size, alignment, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    }
    /* Default: aligned alloc */
#if defined(_MSC_VER)
    if (alignment <= sizeof(void *)) {
        return malloc(size);
    }
    return _aligned_malloc(size, alignment);
#elif defined(__ORBIS__) || defined(__PS4__)
    /* PS4 FreeBSD-based kernel doesn't expose posix_memalign.
     * Always over-allocate and align manually, storing the raw
     * pointer immediately before the aligned address so vk_ps4_free
     * can recover it. This ensures a consistent free path regardless
     * of alignment size. */
    if (alignment < sizeof(void *)) {
        alignment = sizeof(void *);
    }
    void *raw = malloc(size + alignment + sizeof(void *));
    if (!raw) return NULL;
    uintptr_t addr = ((uintptr_t)raw + sizeof(void *) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void **)addr)[-1] = raw;
    return (void *)addr;
#else
    if (alignment <= sizeof(void *)) {
        return malloc(size);
    }
    void *ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return NULL;
    }
    return ptr;
#endif
}

void *vk_ps4_alloc_zero(const VkAllocationCallbacks *alloc, size_t size, size_t alignment) {
    void *ptr = vk_ps4_alloc(alloc, size, alignment);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void vk_ps4_free(const VkAllocationCallbacks *alloc, void *ptr) {
    if (!ptr) {
        return;
    }
    if (alloc && alloc->pfnFree) {
        alloc->pfnFree(alloc->pUserData, ptr);
        return;
    }
#if defined(_MSC_VER)
    _aligned_free(ptr);
#elif defined(__ORBIS__) || defined(__PS4__)
    /* Recover the original malloc pointer stored before the aligned address. */
    free(((void **)ptr)[-1]);
#else
    free(ptr);
#endif
}
