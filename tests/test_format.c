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
        CHECK(props.apiVersion == VK_MAKE_VERSION(1, 1, 0), "API version is 1.1");
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

        /* NULL pName should not crash */
        fn = vkGetInstanceProcAddr(inst, NULL);
        CHECK(fn == NULL, "GetInstanceProcAddr returns NULL for NULL pName");

        /* Missing core functions should be found */
        fn = vkGetInstanceProcAddr(inst, "vkCreateSampler");
        CHECK(fn != NULL, "GetInstanceProcAddr finds vkCreateSampler");
        fn = vkGetInstanceProcAddr(inst, "vkDestroySampler");
        CHECK(fn != NULL, "GetInstanceProcAddr finds vkDestroySampler");
        fn = vkGetInstanceProcAddr(inst, "vkResetCommandPool");
        CHECK(fn != NULL, "GetInstanceProcAddr finds vkResetCommandPool");
        fn = vkGetInstanceProcAddr(inst, "vkResetDescriptorPool");
        CHECK(fn != NULL, "GetInstanceProcAddr finds vkResetDescriptorPool");
        fn = vkGetInstanceProcAddr(inst, "vkTrimCommandPool");
        CHECK(fn != NULL, "GetInstanceProcAddr finds vkTrimCommandPool");
        fn = vkGetInstanceProcAddr(inst, "vkGetImageSubresourceLayout");
        CHECK(fn != NULL, "GetInstanceProcAddr finds vkGetImageSubresourceLayout");
        fn = vkGetInstanceProcAddr(inst, "vkCmdPushConstants");
        CHECK(fn != NULL, "GetInstanceProcAddr finds vkCmdPushConstants");

        vkDestroyInstance(inst, NULL);
    }

    /* Test: R5G5B5A1 channel order (was Bug 1 — channels were scrambled) */
    {
        GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(VK_FORMAT_R5G5B5A1_UNORM_PACK16);
        CHECK(fmt.surfacefmt == GNM_IMG_DATA_FORMAT_1_5_5_5, "R5G5B5A1 surface format");
        CHECK(fmt.chanx == GNM_CHAN_X, "R5G5B5A1 chan X = X (R)");
        CHECK(fmt.chany == GNM_CHAN_Y, "R5G5B5A1 chan Y = Y (G)");
        CHECK(fmt.chanz == GNM_CHAN_Z, "R5G5B5A1 chan Z = Z (B)");
        CHECK(fmt.chanw == GNM_CHAN_W, "R5G5B5A1 chan W = W (A)");
    }

    /* Test: D24_UNORM_S8_UINT (was Bug 2 — mapped to wrong 8_24 instead of 24_8) */
    {
        GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(VK_FORMAT_D24_UNORM_S8_UINT);
        CHECK(fmt.surfacefmt == GNM_IMG_DATA_FORMAT_24_8, "D24S8 surface format is 24_8 (not 8_24)");
    }

    /* Test: B5G6R5 channel swap */
    {
        GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(VK_FORMAT_B5G6R5_UNORM_PACK16);
        CHECK(fmt.surfacefmt == GNM_IMG_DATA_FORMAT_5_6_5, "B5G6R5 surface format");
        CHECK(fmt.chanx == GNM_CHAN_Z, "B5G6R5 chan X = Z (B)");
        CHECK(fmt.chany == GNM_CHAN_Y, "B5G6R5 chan Y = Y (G)");
        CHECK(fmt.chanz == GNM_CHAN_X, "B5G6R5 chan Z = X (R)");
    }

    /* Test: B4G4R4A4 channel swap */
    {
        GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(VK_FORMAT_B4G4R4A4_UNORM_PACK16);
        CHECK(fmt.surfacefmt == GNM_IMG_DATA_FORMAT_4_4_4_4, "B4G4R4A4 surface format");
        CHECK(fmt.chanx == GNM_CHAN_Z, "B4G4R4A4 chan X = Z (B)");
        CHECK(fmt.chany == GNM_CHAN_Y, "B4G4R4A4 chan Y = Y (G)");
        CHECK(fmt.chanz == GNM_CHAN_X, "B4G4R4A4 chan Z = X (R)");
    }

    /* Test: A2R10G10B10 channel swap */
    {
        GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(VK_FORMAT_A2R10G10B10_UNORM_PACK32);
        CHECK(fmt.surfacefmt == GNM_IMG_DATA_FORMAT_2_10_10_10, "A2R10G10B10 surface format");
        CHECK(fmt.chanx == GNM_CHAN_Z, "A2R10G10B10 chan X = Z (B)");
        CHECK(fmt.chany == GNM_CHAN_Y, "A2R10G10B10 chan Y = Y (G)");
        CHECK(fmt.chanz == GNM_CHAN_X, "A2R10G10B10 chan Z = X (R)");
    }

    /* Test: Physical device handle stability (was Bug 4) */
    {
        VkInstance inst = VK_NULL_HANDLE;
        VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        vkCreateInstance(&ci, NULL, &inst);

        VkPhysicalDevice phys1 = VK_NULL_HANDLE, phys2 = VK_NULL_HANDLE;
        uint32_t count = 1;
        vkEnumeratePhysicalDevices(inst, &count, &phys1);
        CHECK(phys1 != VK_NULL_HANDLE, "First EnumeratePhysicalDevices returns handle");
        count = 1;
        vkEnumeratePhysicalDevices(inst, &count, &phys2);
        CHECK(phys2 == phys1, "Physical device handle is stable across calls");

        vkDestroyInstance(inst, NULL);
    }

    /* Test: Queue handle stability (was Bug 5) */
    {
        VkInstance inst = VK_NULL_HANDLE;
        VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        vkCreateInstance(&ci, NULL, &inst);
        VkPhysicalDevice phys = VK_NULL_HANDLE;
        uint32_t count = 1;
        vkEnumeratePhysicalDevices(inst, &count, &phys);

        float queue_priorities[] = {1.0f};
        VkDeviceQueueCreateInfo qci = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = 0,
            .queueCount = 1,
            .pQueuePriorities = queue_priorities,
        };
        VkDeviceCreateInfo dci = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pQueueCreateInfos = &qci,
            .queueCreateInfoCount = 1,
        };
        VkDevice dev = VK_NULL_HANDLE;
        vkCreateDevice(phys, &dci, NULL, &dev);

        VkQueue q1 = VK_NULL_HANDLE, q2 = VK_NULL_HANDLE;
        vkGetDeviceQueue(dev, 0, 0, &q1);
        vkGetDeviceQueue(dev, 0, 0, &q2);
        CHECK(q1 != VK_NULL_HANDLE, "First GetDeviceQueue returns handle");
        CHECK(q1 == q2, "Queue handle is stable across calls");

        vkDestroyDevice(dev, NULL);
        vkDestroyInstance(inst, NULL);
    }

    /* Test: ShaderModule create+destroy (was Bug 6 — leak) */
    {
        VkInstance inst = VK_NULL_HANDLE;
        VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        vkCreateInstance(&ci, NULL, &inst);
        VkPhysicalDevice phys = VK_NULL_HANDLE;
        uint32_t count = 1;
        vkEnumeratePhysicalDevices(inst, &count, &phys);

        float qp[] = {1.0f};
        VkDeviceQueueCreateInfo qci = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = qp,
        };
        VkDeviceCreateInfo dci = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pQueueCreateInfos = &qci, .queueCreateInfoCount = 1,
        };
        VkDevice dev = VK_NULL_HANDLE;
        vkCreateDevice(phys, &dci, NULL, &dev);

        /* Minimal valid SPIR-V vertex shader: void main() {}
         * Generated by glslangValidator -V from "#version 450\nvoid main(){}" */
        static const uint32_t spirv[] = {
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
        VkShaderModuleCreateInfo smci = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = sizeof(spirv),
            .pCode = spirv,
        };
        VkShaderModule mod = VK_NULL_HANDLE;
        VkResult res = vkCreateShaderModule(dev, &smci, NULL, &mod);
        CHECK(res == VK_SUCCESS, "CreateShaderModule succeeds");
        CHECK(mod != VK_NULL_HANDLE, "ShaderModule handle valid");
        vkDestroyShaderModule(dev, mod, NULL);

        vkDestroyDevice(dev, NULL);
        vkDestroyInstance(inst, NULL);
    }

    /* Test: Device extension enumeration (was Bug 11 — surface reported as device ext) */
    {
        VkInstance inst = VK_NULL_HANDLE;
        VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        vkCreateInstance(&ci, NULL, &inst);
        VkPhysicalDevice phys = VK_NULL_HANDLE;
        uint32_t count = 1;
        vkEnumeratePhysicalDevices(inst, &count, &phys);

        uint32_t dev_ext_count = 0;
        vkEnumerateDeviceExtensionProperties(phys, NULL, &dev_ext_count, NULL);
        CHECK(dev_ext_count >= 2, "Device extension count >= 2 (swapchain + renderpass2)");

        VkExtensionProperties ext[32];
        vkEnumerateDeviceExtensionProperties(phys, NULL, &dev_ext_count, ext);
        bool has_swapchain = false;
        bool has_renderpass2 = false;
        for (uint32_t i = 0; i < dev_ext_count; i++) {
            if (strcmp(ext[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                has_swapchain = true;
            if (strcmp(ext[i].extensionName, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME) == 0)
                has_renderpass2 = true;
        }
        CHECK(has_swapchain, "Device extensions include VK_KHR_swapchain");
        CHECK(has_renderpass2, "Device extensions include VK_KHR_create_renderpass2");

        uint32_t inst_ext_count = 0;
        vkEnumerateInstanceExtensionProperties(NULL, &inst_ext_count, NULL);
        CHECK(inst_ext_count >= 1, "Instance extension count >= 1 (surface)");

        vkDestroyInstance(inst, NULL);
    }

    /* Test: Buffer view format compatibility (Bug: SRGB/BC silently broken) */
    {
        /* R8_UNORM should be buffer-compatible */
        GnmDataFormat fmt_r8 = vk_ps4_vk_format_to_gnm(VK_FORMAT_R8_UNORM);
        CHECK(vk_ps4_gnm_format_is_buffer_compatible(fmt_r8) == true,
              "R8_UNORM is buffer-compatible");

        /* R32_SFLOAT should be buffer-compatible */
        GnmDataFormat fmt_r32 = vk_ps4_vk_format_to_gnm(VK_FORMAT_R32_SFLOAT);
        CHECK(vk_ps4_gnm_format_is_buffer_compatible(fmt_r32) == true,
              "R32_SFLOAT is buffer-compatible");

        /* R8G8B8A8_SRGB should NOT be buffer-compatible (no SRGB in buffer format) */
        GnmDataFormat fmt_srgb = vk_ps4_vk_format_to_gnm(VK_FORMAT_R8G8B8A8_SRGB);
        CHECK(vk_ps4_gnm_format_is_buffer_compatible(fmt_srgb) == false,
              "R8G8B8A8_SRGB is NOT buffer-compatible");

        /* BC1_RGB_UNORM should NOT be buffer-compatible (no BC in buffer format) */
        GnmDataFormat fmt_bc1 = vk_ps4_vk_format_to_gnm(VK_FORMAT_BC1_RGB_UNORM_BLOCK);
        CHECK(vk_ps4_gnm_format_is_buffer_compatible(fmt_bc1) == false,
              "BC1_RGB_UNORM is NOT buffer-compatible");

        /* vk_ps4_vk_format_to_gnm_buffer should return INVALID for SRGB */
        GnmDataFormat buf_srgb = vk_ps4_vk_format_to_gnm_buffer(VK_FORMAT_R8G8B8A8_SRGB);
        CHECK(buf_srgb.asuint == 0, "vk_format_to_gnm_buffer returns INVALID for SRGB");

        /* vk_ps4_vk_format_to_gnm_buffer should return valid for UNORM */
        GnmDataFormat buf_unorm = vk_ps4_vk_format_to_gnm_buffer(VK_FORMAT_R8G8B8A8_UNORM);
        CHECK(buf_unorm.asuint != 0, "vk_format_to_gnm_buffer returns valid for UNORM");
    }

    /* Test: Buffer features only advertise texel buffer for compatible formats */
    {
        /* R8G8B8A8_UNORM should have texel buffer features */
        VkFormatProperties props_unorm = vk_ps4_format_properties(VK_FORMAT_R8G8B8A8_UNORM);
        CHECK((props_unorm.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT) != 0,
              "R8G8B8A8_UNORM has UNIFORM_TEXEL_BUFFER feature");

        /* R8G8B8A8_SRGB should NOT have texel buffer features */
        VkFormatProperties props_srgb = vk_ps4_format_properties(VK_FORMAT_R8G8B8A8_SRGB);
        CHECK((props_srgb.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT) == 0,
              "R8G8B8A8_SRGB does NOT have UNIFORM_TEXEL_BUFFER feature");

        /* Both should still have VERTEX_BUFFER feature */
        CHECK((props_unorm.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) != 0,
              "R8G8B8A8_UNORM has VERTEX_BUFFER feature");
        CHECK((props_srgb.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) != 0,
              "R8G8B8A8_SRGB has VERTEX_BUFFER feature");
    }

    printf("\n%d/%d tests passed\n", test_pass, test_count);
    return (test_pass == test_count) ? 0 : 1;
}
