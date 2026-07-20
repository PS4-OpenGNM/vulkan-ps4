/*
 * test_format.c — Test VkFormat to GnmDataFormat mapping.
 */

#include "vk_ps4_internal.h"

#include <stdio.h>
#include <string.h>

static int test_count = 0;
static int test_pass = 0;

#define CHECK(cond, msg) do { \
    test_count++; \
    if (cond) { \
        test_pass++; \
    } else { \
        printf("FAIL: %s (line %d)\n", msg, __LINE__); \
    } \
} while (0)

int main(void) {
    /* Test: BGRA8 UNORM (PS4 VideoOut native format) */
    {
        GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(VK_FORMAT_B8G8R8A8_UNORM);
        CHECK(fmt.surfacefmt == GNM_IMG_DATA_FORMAT_8_8_8_8, "BGRA8 surface format");
        CHECK(fmt.chantype == GNM_IMG_NUM_FORMAT_UNORM, "BGRA8 channel type");
        CHECK(fmt.chanx == GNM_CHAN_Z, "BGRA8 chan X = Z (B)");
        CHECK(fmt.chany == GNM_CHAN_Y, "BGRA8 chan Y = Y (G)");
        CHECK(fmt.chanz == GNM_CHAN_X, "BGRA8 chan Z = X (R)");
        CHECK(fmt.chanw == GNM_CHAN_W, "BGRA8 chan W = W (A)");
    }

    /* Test: RGBA8 SRGB */
    {
        GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(VK_FORMAT_R8G8B8A8_SRGB);
        CHECK(fmt.surfacefmt == GNM_IMG_DATA_FORMAT_8_8_8_8, "RGBA8 SRGB surface format");
        CHECK(fmt.chantype == GNM_IMG_NUM_FORMAT_SRGB, "RGBA8 SRGB channel type");
        CHECK(fmt.chanx == GNM_CHAN_X, "RGBA8 SRGB chan X");
    }

    /* Test: R32 SFLOAT */
    {
        GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(VK_FORMAT_R32_SFLOAT);
        CHECK(fmt.surfacefmt == GNM_IMG_DATA_FORMAT_32, "R32 FLOAT surface format");
        CHECK(fmt.chantype == GNM_IMG_NUM_FORMAT_FLOAT, "R32 FLOAT channel type");
    }

    /* Test: R16G16B16A16 SFLOAT */
    {
        GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(VK_FORMAT_R16G16B16A16_SFLOAT);
        CHECK(fmt.surfacefmt == GNM_IMG_DATA_FORMAT_16_16_16_16, "RGBA16 FLOAT surface format");
        CHECK(fmt.chantype == GNM_IMG_NUM_FORMAT_FLOAT, "RGBA16 FLOAT channel type");
    }

    /* Test: D32 SFLOAT */
    {
        GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(VK_FORMAT_D32_SFLOAT);
        CHECK(fmt.surfacefmt == GNM_IMG_DATA_FORMAT_32, "D32 FLOAT surface format");
        CHECK(fmt.chantype == GNM_IMG_NUM_FORMAT_FLOAT, "D32 FLOAT channel type");
    }

    /* Test: BC1 UNORM */
    {
        GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(VK_FORMAT_BC1_RGB_UNORM_BLOCK);
        CHECK(fmt.surfacefmt == GNM_IMG_DATA_FORMAT_BC1, "BC1 surface format");
        CHECK(fmt.chantype == GNM_IMG_NUM_FORMAT_UNORM, "BC1 channel type");
    }

    /* Test: unsupported format returns INVALID */
    {
        GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(VK_FORMAT_UNDEFINED);
        CHECK(fmt.surfacefmt == GNM_IMG_DATA_FORMAT_INVALID, "Undefined format is invalid");
    }

    /* Test: format properties */
    {
        VkFormatProperties props = vk_ps4_format_properties(VK_FORMAT_B8G8R8A8_UNORM);
        CHECK(props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
              "BGRA8 supports color attachment");
        CHECK(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
              "BGRA8 supports sampled image");
    }

    /* Test: depth format properties */
    {
        VkFormatProperties props = vk_ps4_format_properties(VK_FORMAT_D32_SFLOAT);
        CHECK(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
              "D32 supports depth attachment");
    }

    /* Test: instance creation */
    {
        VkInstance inst = VK_NULL_HANDLE;
        VkInstanceCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        };
        VkResult res = vkCreateInstance(&ci, NULL, &inst);
        CHECK(res == VK_SUCCESS, "vkCreateInstance succeeds");
        CHECK(inst != VK_NULL_HANDLE, "Instance handle is valid");

        /* Enumerate physical devices */
        uint32_t count = 0;
        res = vkEnumeratePhysicalDevices(inst, &count, NULL);
        CHECK(res == VK_SUCCESS, "EnumeratePhysicalDevices (count) succeeds");
        CHECK(count == 1, "One physical device");

        VkPhysicalDevice phys = VK_NULL_HANDLE;
        res = vkEnumeratePhysicalDevices(inst, &count, &phys);
        CHECK(res == VK_SUCCESS, "EnumeratePhysicalDevices (get) succeeds");
        CHECK(phys != VK_NULL_HANDLE, "Physical device handle is valid");

        /* Get properties */
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(phys, &props);
        CHECK(props.apiVersion == VK_MAKE_VERSION(1, 0, 0), "API version is 1.0");
        CHECK(props.vendorID == 0x1002, "Vendor ID is AMD");

        /* Get memory properties */
        VkPhysicalDeviceMemoryProperties mem_props;
        vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);
        CHECK(mem_props.memoryTypeCount == 2, "Two memory types (Onion + Garlic)");
        CHECK(mem_props.memoryHeapCount == 2, "Two memory heaps");

        /* Create device */
        float queue_priority = 1.0f;
        VkDeviceQueueCreateInfo qci = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = 0,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        };
        VkDeviceCreateInfo dci = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &qci,
        };
        VkDevice dev = VK_NULL_HANDLE;
        res = vkCreateDevice(phys, &dci, NULL, &dev);
        CHECK(res == VK_SUCCESS, "vkCreateDevice succeeds");
        CHECK(dev != VK_NULL_HANDLE, "Device handle is valid");

        /* Get queue */
        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(dev, 0, 0, &queue);
        CHECK(queue != VK_NULL_HANDLE, "GetDeviceQueue returns valid queue");

        vkDestroyDevice(dev, NULL);
        vkDestroyInstance(inst, NULL);
    }

    /* Test: vkGetInstanceProcAddr */
    {
        VkInstance inst = VK_NULL_HANDLE;
        VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        vkCreateInstance(&ci, NULL, &inst);

        PFN_vkVoidFunction fn = vkGetInstanceProcAddr(inst, "vkCreateDevice");
        CHECK(fn != NULL, "GetInstanceProcAddr finds vkCreateDevice");

        fn = vkGetInstanceProcAddr(inst, "vkCmdDraw");
        CHECK(fn != NULL, "GetInstanceProcAddr finds vkCmdDraw");

        fn = vkGetInstanceProcAddr(inst, "vkNonexistentFunction");
        CHECK(fn == NULL, "GetInstanceProcAddr returns NULL for unknown");

        vkDestroyInstance(inst, NULL);
    }

    printf("\n%d/%d tests passed\n", test_pass, test_count);
    return (test_pass == test_count) ? 0 : 1;
}
