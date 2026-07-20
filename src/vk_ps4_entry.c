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
    /* We support ICD interface version 5 (Vulkan 1.0 loader) */
    if (*pVersion >= 5) {
        *pVersion = 5;
        return VK_SUCCESS;
    }
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

/* Instance extensions: only VK_KHR_surface (platform surface is instance-level) */
static const VkExtensionProperties g_instance_extensions[] = {
    {VK_KHR_SURFACE_EXTENSION_NAME, 1},
};
static const uint32_t g_instance_ext_count =
    sizeof(g_instance_extensions) / sizeof(g_instance_extensions[0]);

/* Device extensions: only VK_KHR_swapchain (device-level) */
static const VkExtensionProperties g_device_extensions[] = {
    {VK_KHR_SWAPCHAIN_EXTENSION_NAME, 1},
};
static const uint32_t g_device_ext_count =
    sizeof(g_device_extensions) / sizeof(g_device_extensions[0]);

VKAPI_ATTR VkResult VKAPI_CALL
vk_icdEnumerateInstanceExtensionProperties(
    const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties
) {
    (void)pLayerName;

    if (!pProperties) {
        *pPropertyCount = g_instance_ext_count;
        return VK_SUCCESS;
    }
    uint32_t avail = *pPropertyCount;
    if (avail < g_instance_ext_count) {
        /* Write as many as fit */
        memcpy(pProperties, g_instance_extensions,
               avail * sizeof(VkExtensionProperties));
        *pPropertyCount = g_instance_ext_count;
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
    (void)pLayerName;
    if (!pProperties) {
        *pPropertyCount = g_device_ext_count;
        return VK_SUCCESS;
    }
    uint32_t avail = *pPropertyCount;
    if (avail < g_device_ext_count) {
        memcpy(pProperties, g_device_extensions,
               avail * sizeof(VkExtensionProperties));
        *pPropertyCount = g_device_ext_count;
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
    if (alignment <= sizeof(void *)) {
        return malloc(size);
    }
#if defined(_MSC_VER)
    return _aligned_malloc(size, alignment);
#else
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
#else
    free(ptr);
#endif
}
