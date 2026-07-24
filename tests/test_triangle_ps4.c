/*
 * test_triangle_ps4.c — PS4 Vulkan triangle test.
 *
 * Links against libvulkan_ps4.so and renders a colored triangle using
 * the Vulkan API.  The swapchain uses sceVideoOut internally.
 *
 * On PS4, there is no Vulkan loader — the app calls vk* symbols directly
 * from the ICD shared library.
 */

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Simple vertex data: position (vec2) + color (vec3) */
static const float g_vertices[] = {
    /* x      y     r     g     b    */
     0.0f, -0.5f,  1.0f, 0.0f, 0.0f,  /* top */
    -0.5f,  0.5f,  0.0f, 1.0f, 0.0f,  /* bottom left */
     0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  /* bottom right */
};

/* Minimal SPIR-V vertex shader:
 * #version 450
 * layout(location=0) in vec2 pos;
 * layout(location=1) in vec3 color;
 * layout(location=0) out vec3 vcolor;
 * void main() {
 *   gl_Position = vec4(pos, 0.0, 1.0);
 *   vcolor = color;
 * }
 */
static const uint32_t g_vert_spv[] = {
    0x07230203,0x00010000,0x00080001,0x0000001e,0x00000011,0x00000001,
    0x00080001,0x0000001e,0x00030002,0x000001c2,0x00040005,0x00000004,
    0x6e69616d,0x00000000,0x00060005,0x00000008,0x475f4c47,0x6f6c6f50,
    0x6e697473,0x00000000,0x00060006,0x00000008,0x00000000,0x00000023,
    0x00000000,0x00000047,0x00050005,0x0000000a,0x6f6c6f63,0x00726f6c,
    0x00000000,0x00060005,0x0000000c,0x6f6c6f76,0x0000726f,0x00030005,
    0x0000000e,0x00706f73,0x00040047,0x0000000e,0x00000000,0x00000001,
    0x00040047,0x0000000a,0x00000001,0x00000001,0x00030005,0x00000012,
    0x00070000,0x00040003,0x00000012,0x00000000,0x0004000b,0x00000012,
    0x00000000,0x00050004,0x00000012,0x00000000,0x00000023,0x0000000a,
    0x00050004,0x00000012,0x00000001,0x00000023,0x0000000c,0x00060005,
    0x00000012,0x00000000,0x74736f70,0x00000000,0x00050005,0x00000012,
    0x00000001,0x6f6c6f63,0x0000726f,0x00040047,0x00000012,0x00000001,
    0x00000001,0x00040047,0x00000012,0x00000000,0x00000001,0x00040047,
    0x00000012,0x00000001,0x00000001,0x00030005,0x0000001c,0x006f6c76,
    0x00060005,0x0000001c,0x00000000,0x6f6c6f63,0x0000726f,0x00040047,
    0x0000001c,0x00000000,0x00000001,0x00050048,0x0000001c,0x00000000,
    0x00000023,0x0000000a,0x00030047,0x0000001c,0x00000000,0x00000019,
    0x00040047,0x0000001c,0x00000001,0x0000001b,0x00040047,0x0000001c,
    0x00000001,0x0000001b,0x00050048,0x0000001c,0x00000001,0x00000023,
    0x0000000c,0x00030047,0x0000001c,0x00000001,0x00000019,0x00040047,
    0x0000001c,0x00000002,0x0000001b,0x00040047,0x0000001c,0x00000002,
    0x0000001b,0x00050048,0x0000001c,0x00000002,0x00000023,0x0000000c,
    0x00030047,0x0000001c,0x00000002,0x00000019,0x00040047,0x00000024,
    0x00000000,0x0000001e,0x00020013,0x00000002,0x00030021,0x00000003,
    0x00000002,0x00040015,0x00000006,0x00000020,0x00000000,0x00040017,
    0x00000007,0x00000006,0x00000003,0x00040020,0x00000008,0x00000003,
    0x00000007,0x0004003b,0x00000008,0x0000000a,0x00000007,0x00040015,
    0x0000000b,0x00000020,0x00000001,0x0004002b,0x0000000b,0x0000000d,
    0x00000001,0x00040020,0x0000000e,0x00000003,0x00000006,0x00040017,
    0x0000000f,0x00000006,0x00000002,0x00040020,0x00000011,0x00000001,
    0x0000000f,0x0004003b,0x00000011,0x00000012,0x0000000f,0x00040020,
    0x0000001c,0x00000009,0x00000007,0x0004003b,0x0000001c,0x0000001d,
    0x00000007,0x00040020,0x00000024,0x00000009,0x00000007,0x00040017,
    0x00000025,0x00000006,0x00000004,0x00040015,0x00000027,0x00000020,
    0x00000000,0x0004002b,0x00000027,0x00000028,0x00000000,0x0004002b,
    0x00000027,0x0000002a,0x00000001,0x0004001e,0x0000002b,0x00000025,
    0x00000028,0x0000002a,0x00040020,0x0000002c,0x00000009,0x0000002b,
    0x0004003b,0x0000002c,0x0000002d,0x0000002b,0x00040015,0x0000002e,
    0x00000020,0x00000003,0x0004002b,0x0000002e,0x00000030,0x00000003,
    0x00040020,0x00000031,0x00000001,0x0000002b,0x0004003b,0x00000031,
    0x00000032,0x0000002b,0x00050036,0x00000002,0x00000004,0x00000000,
    0x00000003,0x000200f8,0x00000005,0x0004003d,0x0000000f,0x00000012,
    0x0000000e,0x00050041,0x0000001c,0x0000001d,0x00000012,0x0000000d,
    0x00050041,0x00000024,0x00000033,0x0000001d,0x00000030,0x0004003d,
    0x00000025,0x0000002d,0x0000002c,0x0004003d,0x00000007,0x00000034,
    0x0000001d,0x00050051,0x00000006,0x00000035,0x00000034,0x00000028,
    0x00050051,0x00000006,0x00000036,0x00000034,0x0000002a,0x00070050,
    0x00000025,0x00000037,0x00000035,0x00000036,0x00000028,0x0000002a,
    0x00050091,0x00000025,0x00000038,0x00000037,0x00000030,0x00050041,
    0x00000024,0x00000039,0x00000033,0x00000030,0x0004003d,0x00000007,
    0x0000003a,0x00000039,0x000300f7,0x00000038,0x00000000,0x0004003d,
    0x00000007,0x0000003b,0x0000001d,0x0003001e,0x0000003c,0x0000003b,
    0x000300f7,0x0000003c,0x00000000,0x0004003d,0x0000000f,0x0000003d,
    0x00000012,0x00050041,0x0000001c,0x0000003e,0x00000012,0x0000000d,
    0x00050041,0x00000024,0x0000003f,0x0000003e,0x00000030,0x0004003d,
    0x00000025,0x00000040,0x0000002d,0x0004003d,0x00000007,0x00000041,
    0x0000003f,0x00050051,0x00000006,0x00000042,0x00000041,0x00000028,
    0x00050051,0x00000006,0x00000043,0x00000041,0x0000002a,0x00070050,
    0x00000025,0x00000044,0x00000042,0x00000043,0x00000028,0x0000002a,
    0x00050091,0x00000025,0x00000045,0x00000044,0x00000030,0x00050041,
    0x00000024,0x00000046,0x0000003f,0x00000030,0x0004003d,0x00000007,
    0x00000047,0x00000046,0x000300f7,0x00000045,0x00000000,0x0004003d,
    0x00000007,0x00000048,0x0000003f,0x0003001e,0x00000049,0x00000048,
    0x000300f7,0x00000049,0x00000000,0x000100fd,0x00010038,
};

/* Minimal SPIR-V fragment shader:
 * #version 450
 * layout(location=0) in vec3 vcolor;
 * layout(location=0) out vec4 fragColor;
 * void main() {
 *   fragColor = vec4(vcolor, 1.0);
 * }
 */
static const uint32_t g_frag_spv[] = {
    0x07230203,0x00010000,0x00080001,0x00000012,0x00000006,0x00000001,
    0x00080001,0x00000012,0x00030002,0x000001c2,0x00040005,0x00000004,
    0x6e69616d,0x00000000,0x00060005,0x00000008,0x475f4c47,0x6f6c6f50,
    0x6e697473,0x00000000,0x00060006,0x00000008,0x00000000,0x00000023,
    0x00000000,0x00000047,0x00050005,0x0000000a,0x6f6c6f63,0x00726f6c,
    0x00000000,0x00030005,0x0000000c,0x67617266,0x00040005,0x0000000e,
    0x6f6c6f76,0x0000726f,0x00040047,0x0000000e,0x00000000,0x00000001,
    0x00040047,0x0000000a,0x00000001,0x00000001,0x00030005,0x00000012,
    0x00070000,0x00040003,0x00000012,0x00000000,0x0004000b,0x00000012,
    0x00000000,0x00050004,0x00000012,0x00000000,0x00000023,0x0000000a,
    0x00050004,0x00000012,0x00000001,0x00000023,0x0000000c,0x00060005,
    0x00000012,0x00000000,0x6f6c6f76,0x0000726f,0x00050005,0x00000012,
    0x00000001,0x6f6c6f63,0x0000726f,0x00040047,0x00000012,0x00000001,
    0x00000001,0x00040047,0x00000012,0x00000000,0x00000001,0x00040047,
    0x00000012,0x00000001,0x00000001,0x00030005,0x0000001c,0x006f6c76,
    0x00060005,0x0000001c,0x00000000,0x6f6c6f63,0x0000726f,0x00040047,
    0x0000001c,0x00000000,0x00000001,0x00050048,0x0000001c,0x00000000,
    0x00000023,0x0000000a,0x00030047,0x0000001c,0x00000000,0x00000019,
    0x00040047,0x0000001c,0x00000001,0x0000001b,0x00040047,0x0000001c,
    0x00000001,0x0000001b,0x00050048,0x0000001c,0x00000001,0x00000023,
    0x0000000c,0x00030047,0x0000001c,0x00000001,0x00000019,0x00040047,
    0x0000001c,0x00000002,0x0000001b,0x00040047,0x0000001c,0x00000002,
    0x0000001b,0x00050048,0x0000001c,0x00000002,0x00000023,0x0000000c,
    0x00030047,0x0000001c,0x00000002,0x00000019,0x00040047,0x00000020,
    0x00000000,0x0000001e,0x00020013,0x00000002,0x00030021,0x00000003,
    0x00000002,0x00040015,0x00000006,0x00000020,0x00000000,0x00040017,
    0x00000007,0x00000006,0x00000003,0x00040020,0x00000008,0x00000003,
    0x00000007,0x0004003b,0x00000008,0x0000000a,0x00000007,0x00040020,
    0x0000000e,0x00000003,0x00000006,0x00040017,0x0000000f,0x00000006,
    0x00000002,0x00040020,0x00000011,0x00000001,0x0000000f,0x0004003b,
    0x00000011,0x00000012,0x0000000f,0x00040020,0x0000001c,0x00000009,
    0x00000007,0x0004003b,0x0000001c,0x0000001d,0x00000007,0x00040020,
    0x00000020,0x00000009,0x00000007,0x00040017,0x00000021,0x00000006,
    0x00000004,0x00040015,0x00000023,0x00000020,0x00000000,0x0004002b,
    0x00000023,0x00000024,0x00000000,0x0004002b,0x00000023,0x00000026,
    0x00000001,0x0004001e,0x00000027,0x00000021,0x00000024,0x00000026,
    0x00040020,0x00000028,0x00000009,0x00000027,0x0004003b,0x00000028,
    0x00000029,0x00000027,0x00040015,0x0000002a,0x00000020,0x00000003,
    0x0004002b,0x0000002a,0x0000002c,0x00000003,0x00040020,0x0000002d,
    0x00000001,0x00000027,0x0004003b,0x0000002d,0x0000002e,0x00000027,
    0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,
    0x00000005,0x0004003d,0x0000000f,0x00000012,0x0000000e,0x00050041,
    0x0000001c,0x0000001d,0x00000012,0x00000024,0x00050041,0x00000020,
    0x0000002f,0x0000001d,0x0000002c,0x0004003d,0x00000021,0x00000030,
    0x0000002e,0x0004003d,0x00000007,0x00000031,0x0000001d,0x00050051,
    0x00000006,0x00000032,0x00000031,0x00000024,0x00050051,0x00000006,
    0x00000033,0x00000031,0x00000026,0x00070050,0x00000021,0x00000034,
    0x00000032,0x00000033,0x00000024,0x00000026,0x00050091,0x00000021,
    0x00000035,0x00000034,0x0000002c,0x00050041,0x00000020,0x00000036,
    0x0000002f,0x0000002c,0x0004003d,0x00000007,0x00000037,0x00000036,
    0x000300f7,0x00000035,0x00000000,0x0004003d,0x00000007,0x00000038,
    0x0000001d,0x0003001e,0x00000039,0x00000038,0x000300f7,0x00000039,
    0x00000000,0x0004003d,0x0000000f,0x0000003a,0x00000012,0x00050041,
    0x0000001c,0x0000003b,0x00000012,0x00000024,0x00050041,0x00000020,
    0x0000003c,0x0000003b,0x0000002c,0x0004003d,0x00000021,0x0000003d,
    0x0000002e,0x0004003d,0x00000007,0x0000003e,0x0000003c,0x00050051,
    0x00000006,0x0000003f,0x0000003e,0x00000024,0x00050051,0x00000006,
    0x00000040,0x0000003e,0x00000026,0x00070050,0x00000021,0x00000041,
    0x0000003f,0x00000040,0x00000024,0x00000026,0x00050091,0x00000021,
    0x00000042,0x00000041,0x0000002c,0x00050041,0x00000020,0x00000043,
    0x0000003b,0x0000002c,0x0004003d,0x00000007,0x00000044,0x00000043,
    0x000300f7,0x00000042,0x00000000,0x0004003d,0x00000007,0x00000045,
    0x0000003b,0x0003001e,0x00000046,0x00000045,0x000300f7,0x00000046,
    0x00000000,0x000100fd,0x00010038,
};

int main(void) {
    printf("=== vulkan-ps4 PS4 Triangle Test ===\n");

    /* 1. Create instance */
    VkInstanceCreateInfo inst_ci = {0};
    inst_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    VkInstance inst = VK_NULL_HANDLE;
    VkResult vr = vkCreateInstance(&inst_ci, NULL, &inst);
    if (vr != VK_SUCCESS) {
        printf("vkCreateInstance failed: %d\n", vr);
        return 1;
    }
    printf("Instance created OK\n");

    /* 2. Enumerate physical device */
    uint32_t phys_count = 0;
    vkEnumeratePhysicalDevices(inst, &phys_count, NULL);
    if (phys_count == 0) {
        printf("No physical devices\n");
        vkDestroyInstance(inst, NULL);
        return 1;
    }
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    vkEnumeratePhysicalDevices(inst, &phys_count, &phys);
    printf("Physical device: %u\n", phys_count);

    /* 3. Get queue family */
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qf_count, NULL);
    VkQueueFamilyProperties qf = {0};
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qf_count, &qf);
    printf("Queue family: graphics=%d, count=%u\n",
           !!(qf.queueFlags & VK_QUEUE_GRAPHICS_BIT), qf.queueCount);

    /* 4. Create device + queue */
    float queue_pri = 1.0f;
    VkDeviceQueueCreateInfo q_ci = {0};
    q_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    q_ci.queueFamilyIndex = 0;
    q_ci.queueCount = 1;
    q_ci.pQueuePriorities = &queue_pri;

    const char *dev_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dev_ci = {0};
    dev_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_ci.queueCreateInfoCount = 1;
    dev_ci.pQueueCreateInfos = &q_ci;
    dev_ci.enabledExtensionCount = 1;
    dev_ci.ppEnabledExtensionNames = dev_exts;

    VkDevice dev = VK_NULL_HANDLE;
    vr = vkCreateDevice(phys, &dev_ci, NULL, &dev);
    if (vr != VK_SUCCESS) {
        printf("vkCreateDevice failed: %d\n", vr);
        vkDestroyInstance(inst, NULL);
        return 1;
    }
    printf("Device created OK\n");

    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    /* 5. Create swapchain */
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    /* On PS4, the swapchain creates the surface internally via sceVideoOut.
     * We pass VK_NULL_HANDLE as the surface — the ICD handles it. */
    VkSwapchainCreateInfoKHR sw_ci = {0};
    sw_ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sw_ci.surface = surface;
    sw_ci.minImageCount = 2;
    sw_ci.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
    sw_ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sw_ci.imageExtent.width = 1920;
    sw_ci.imageExtent.height = 1080;
    sw_ci.imageArrayLayers = 1;
    sw_ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sw_ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sw_ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sw_ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sw_ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sw_ci.clipped = VK_TRUE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    vr = vkCreateSwapchainKHR(dev, &sw_ci, NULL, &swapchain);
    if (vr != VK_SUCCESS) {
        printf("vkCreateSwapchainKHR failed: %d\n", vr);
        vkDestroyDevice(dev, NULL);
        vkDestroyInstance(inst, NULL);
        return 1;
    }
    printf("Swapchain created OK (1920x1080)\n");

    /* 6. Get swapchain images */
    uint32_t sw_img_count = 0;
    vkGetSwapchainImagesKHR(dev, swapchain, &sw_img_count, NULL);
    VkImage *sw_images = malloc(sw_img_count * sizeof(*sw_images));
    vkGetSwapchainImagesKHR(dev, swapchain, &sw_img_count, sw_images);
    printf("Swapchain images: %u\n", sw_img_count);

    /* 7. Create render pass */
    VkAttachmentDescription att = {0};
    att.format = VK_FORMAT_R8G8B8A8_UNORM;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;

    VkRenderPassCreateInfo rp_ci = {0};
    rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_ci.attachmentCount = 1;
    rp_ci.pAttachments = &att;
    rp_ci.subpassCount = 1;
    rp_ci.pSubpasses = &subpass;

    VkRenderPass rp = VK_NULL_HANDLE;
    vkCreateRenderPass(dev, &rp_ci, NULL, &rp);
    printf("Render pass created OK\n");

    /* 8. Create image views + framebuffers */
    VkImageView *sw_views = malloc(sw_img_count * sizeof(*sw_views));
    VkFramebuffer *fbs = malloc(sw_img_count * sizeof(*fbs));
    for (uint32_t i = 0; i < sw_img_count; i++) {
        VkImageViewCreateInfo iv_ci = {0};
        iv_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv_ci.image = sw_images[i];
        iv_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        iv_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv_ci.subresourceRange.levelCount = 1;
        iv_ci.subresourceRange.layerCount = 1;
        vkCreateImageView(dev, &iv_ci, NULL, &sw_views[i]);

        VkFramebufferCreateInfo fb_ci = {0};
        fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_ci.renderPass = rp;
        fb_ci.attachmentCount = 1;
        fb_ci.pAttachments = &sw_views[i];
        fb_ci.width = 1920;
        fb_ci.height = 1080;
        fb_ci.layers = 1;
        vkCreateFramebuffer(dev, &fb_ci, NULL, &fbs[i]);
    }
    printf("Image views + framebuffers created OK\n");

    /* 9. Create vertex buffer */
    VkBufferCreateInfo vb_ci = {0};
    vb_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vb_ci.size = sizeof(g_vertices);
    vb_ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vb_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer vbuf;
    vkCreateBuffer(dev, &vb_ci, NULL, &vbuf);

    VkMemoryRequirements vbuf_mem_req;
    vkGetBufferMemoryRequirements(dev, vbuf, &vbuf_mem_req);

    VkMemoryAllocateInfo vbuf_mem_ai = {0};
    vbuf_mem_ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vbuf_mem_ai.allocationSize = vbuf_mem_req.size;
    vbuf_mem_ai.memoryTypeIndex = 0;  /* Garlic (GPU-visible) */

    VkDeviceMemory vbuf_mem;
    vkAllocateMemory(dev, &vbuf_mem_ai, NULL, &vbuf_mem);
    vkBindBufferMemory(dev, vbuf, vbuf_mem, 0);

    /* Copy vertex data */
    void *mapped = NULL;
    vkMapMemory(dev, vbuf_mem, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (mapped) {
        memcpy(mapped, g_vertices, sizeof(g_vertices));
        vkUnmapMemory(dev, vbuf_mem);
    }
    printf("Vertex buffer created OK\n");

    /* 10. Create shader modules */
    VkShaderModuleCreateInfo vs_ci = {0};
    vs_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vs_ci.codeSize = sizeof(g_vert_spv);
    vs_ci.pCode = g_vert_spv;
    VkShaderModule vs_mod;
    vkCreateShaderModule(dev, &vs_ci, NULL, &vs_mod);

    VkShaderModuleCreateInfo fs_ci = {0};
    fs_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fs_ci.codeSize = sizeof(g_frag_spv);
    fs_ci.pCode = g_frag_spv;
    VkShaderModule fs_mod;
    vkCreateShaderModule(dev, &fs_ci, NULL, &fs_mod);
    printf("Shader modules created OK\n");

    /* 11. Create graphics pipeline */
    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs_mod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs_mod;
    stages[1].pName = "main";

    VkVertexInputBindingDescription vibd = {0};
    vibd.binding = 0;
    vibd.stride = 5 * sizeof(float);
    vibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription viads[2] = {0};
    viads[0].location = 0;
    viads[0].binding = 0;
    viads[0].format = VK_FORMAT_R32G32_SFLOAT;
    viads[0].offset = 0;
    viads[1].location = 1;
    viads[1].binding = 0;
    viads[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    viads[1].offset = 2 * sizeof(float);

    VkPipelineVertexInputStateCreateInfo vii = {0};
    vii.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vii.vertexBindingDescriptionCount = 1;
    vii.pVertexBindingDescriptions = &vibd;
    vii.vertexAttributeDescriptionCount = 2;
    vii.pVertexAttributeDescriptions = viads;

    VkPipelineInputAssemblyStateCreateInfo iai = {0};
    iai.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iai.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp = {0, 0, 1920.0f, 1080.0f, 0.0f, 1.0f};
    VkRect2D sc = {{0, 0}, {1920, 1080}};
    VkPipelineViewportStateCreateInfo vpsi = {0};
    vpsi.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpsi.viewportCount = 1;
    vpsi.pViewports = &vp;
    vpsi.scissorCount = 1;
    vpsi.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rsi = {0};
    rsi.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsi.polygonMode = VK_POLYGON_MODE_FILL;
    rsi.lineWidth = 1.0f;
    rsi.cullMode = VK_CULL_MODE_NONE;

    VkPipelineMultisampleStateCreateInfo msi = {0};
    msi.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cbsi = {0};
    cbsi.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbsi.attachmentCount = 1;
    cbsi.pAttachments = &cba;

    VkPipelineLayoutCreateInfo pl_ci = {0};
    pl_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkPipelineLayout pl;
    vkCreatePipelineLayout(dev, &pl_ci, NULL, &pl);

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
    gpci.layout = pl;
    gpci.renderPass = rp;
    gpci.subpass = 0;

    VkPipeline pipeline;
    vr = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline);
    if (vr != VK_SUCCESS) {
        printf("vkCreateGraphicsPipelines failed: %d\n", vr);
    } else {
        printf("Pipeline created OK\n");
    }

    /* 12. Create command pool + buffer */
    VkCommandPoolCreateInfo cp_ci = {0};
    cp_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cp_ci.queueFamilyIndex = 0;
    VkCommandPool cmd_pool;
    vkCreateCommandPool(dev, &cp_ci, NULL, &cmd_pool);

    VkCommandBufferAllocateInfo cb_ai = {0};
    cb_ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_ai.commandPool = cmd_pool;
    cb_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(dev, &cb_ai, &cmd);

    /* 13. Create sync objects */
    VkSemaphoreCreateInfo sem_ci = {0};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore image_avail, render_done;
    vkCreateSemaphore(dev, &sem_ci, NULL, &image_avail);
    vkCreateSemaphore(dev, &sem_ci, NULL, &render_done);

    VkFenceCreateInfo fence_ci = {0};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence flight_fence;
    vkCreateFence(dev, &fence_ci, NULL, &flight_fence);

    /* 14. Render loop — 60 frames then exit */
    printf("Starting render loop (60 frames)...\n");
    for (int frame = 0; frame < 60; frame++) {
        vkWaitForFences(dev, 1, &flight_fence, VK_TRUE, 1000000000ULL);
        vkResetFences(dev, 1, &flight_fence);

        uint32_t img_idx = 0;
        vr = vkAcquireNextImageKHR(dev, swapchain, 1000000000ULL,
                                   image_avail, VK_NULL_HANDLE, &img_idx);
        if (vr != VK_SUCCESS) {
            printf("vkAcquireNextImageKHR failed: %d (frame %d)\n", vr, frame);
            break;
        }

        VkCommandBufferBeginInfo cmd_bi = {0};
        cmd_bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &cmd_bi);

        VkRenderPassBeginInfo rpbi = {0};
        rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass = rp;
        rpbi.framebuffer = fbs[img_idx];
        rpbi.renderArea.offset = (VkOffset2D){0, 0};
        rpbi.renderArea.extent = (VkExtent2D){1920, 1080};
        VkClearValue clear = {0};
        clear.color.float32[0] = 0.1f;
        clear.color.float32[1] = 0.1f;
        clear.color.float32[2] = 0.2f;
        clear.color.float32[3] = 1.0f;
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clear;

        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &offset);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit = {0};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &image_avail;
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &render_done;

        vr = vkQueueSubmit(queue, 1, &submit, flight_fence);
        if (vr != VK_SUCCESS) {
            printf("vkQueueSubmit failed: %d (frame %d)\n", vr, frame);
            break;
        }

        VkPresentInfoKHR present = {0};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &render_done;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &img_idx;

        vr = vkQueuePresentKHR(queue, &present);
        if (vr != VK_SUCCESS) {
            printf("vkQueuePresentKHR failed: %d (frame %d)\n", vr, frame);
            break;
        }

        if (frame % 10 == 0)
            printf("  Frame %d OK\n", frame);
    }
    printf("Render loop done\n");

    /* 15. Cleanup */
    vkDeviceWaitIdle(dev);
    vkDestroyFence(dev, flight_fence, NULL);
    vkDestroySemaphore(dev, image_avail, NULL);
    vkDestroySemaphore(dev, render_done, NULL);
    vkDestroyCommandPool(dev, cmd_pool, NULL);
    vkDestroyPipeline(dev, pipeline, NULL);
    vkDestroyPipelineLayout(dev, pl, NULL);
    vkDestroyShaderModule(dev, vs_mod, NULL);
    vkDestroyShaderModule(dev, fs_mod, NULL);
    vkFreeMemory(dev, vbuf_mem, NULL);
    vkDestroyBuffer(dev, vbuf, NULL);
    for (uint32_t i = 0; i < sw_img_count; i++) {
        vkDestroyFramebuffer(dev, fbs[i], NULL);
        vkDestroyImageView(dev, sw_views[i], NULL);
    }
    free(fbs);
    free(sw_views);
    free(sw_images);
    vkDestroyRenderPass(dev, rp, NULL);
    vkDestroySwapchainKHR(dev, swapchain, NULL);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);

    printf("=== Test complete ===\n");
    return 0;
}
