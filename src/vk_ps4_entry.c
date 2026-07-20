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
    /* We support ICD interface version 5 (Vulkan 1.0 loader) */
    if (*pVersion >= 5) {
        *pVersion = 5;
        return VK_SUCCESS;
    }
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_icdEnumerateInstanceExtensionProperties(
    const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties
) {
    (void)pLayerName;

    /* We support one WSI extension: VK_KHR_swapchain (via VideoOut) */
    static const VkExtensionProperties extensions[] = {
        {VK_KHR_SURFACE_EXTENSION_NAME, 1},
        {VK_KHR_SWAPCHAIN_EXTENSION_NAME, 1},
    };
    const uint32_t count = sizeof(extensions) / sizeof(extensions[0]);

    if (!pProperties) {
        *pPropertyCount = count;
        return VK_SUCCESS;
    }
    if (*pPropertyCount < count) {
        *pPropertyCount = count;
        return VK_INCOMPLETE;
    }
    memcpy(pProperties, extensions, sizeof(extensions));
    *pPropertyCount = count;
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
