#ifndef VK_PS4_H
#define VK_PS4_H

/*
 * vulkan-ps4 — public header.
 *
 * This ICD implements Vulkan 1.0 over OpenGNM (PS4 GNM graphics API).
 * Applications link against libvulkan_ps4 and use standard Vulkan calls.
 *
 * On PS4, there is no standard Vulkan loader. Applications link
 * libvulkan_ps4 statically or load it via a custom path. The ICD
 * exports all vk* symbols directly.
 *
 * See VULKAN_PS4_PLAN.md for the full architecture and status.
 */

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ICD loader interface — used by the Vulkan loader on host for testing.
 * On PS4, applications call vk* symbols directly. */
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pVersion);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName);
VKAPI_ATTR VkResult VKAPI_CALL vk_icdEnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties);
VKAPI_ATTR VkResult VKAPI_CALL vk_icdEnumerateInstanceLayerProperties(uint32_t *pPropertyCount, VkLayerProperties *pProperties);

#ifdef __cplusplus
}
#endif

#endif /* VK_PS4_H */
