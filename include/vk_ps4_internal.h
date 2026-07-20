#ifndef VK_PS4_INTERNAL_H
#define VK_PS4_INTERNAL_H

/*
 * vulkan-ps4 — internal header.
 *
 * Defines the dispatch table, object wrappers, and shared state for the
 * Vulkan 1.0 ICD over OpenGNM.
 *
 * All Vulkan handle types are wrapped in a VkPs4* struct that carries the
 * GNM state needed to translate Vulkan calls into PM4/sceGnm* calls.
 * The Vulkan handle (VkInstance, VkDevice, etc.) is a pointer to these
 * structs cast to the opaque handle type.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <vulkan/vulkan.h>

/* vk_icd.h is only needed for the loader interface on host.
 * On PS4, we don't use the loader. */
#ifndef VK_USE_PLATFORM_PS4
#include <vulkan/vk_icd.h>
#endif

#include <gnmdriver.h>
#include <gnm_commandbuffer.h>
#include <gnm_drawcommandbuffer.h>
#include <gnm_rendertarget.h>
#include <gnm_depthrendertarget.h>
#include <gnm_texture.h>
#include <gnm_buffer.h>
#include <gnm_sampler.h>
#include <gnm_shader.h>
#include <gnm_shaderbinary.h>
#include <gnm_helpers.h>
#include <gnm_dataformat.h>

#ifdef __cplusplus
extern "C" {
#endif

/* === Version === */
#define VK_PS4_API_VERSION VK_MAKE_VERSION(1, 0, 0)
#define VK_PS4_DRIVER_VERSION VK_MAKE_VERSION(0, 1, 0)

/* VK_ERROR_NOT_IMPLEMENTED is not a standard Vulkan code.
 * We define it as a negative value in the implementation-specific range. */
#ifndef VK_ERROR_NOT_IMPLEMENTED
#define VK_ERROR_NOT_IMPLEMENTED ((VkResult)-1000500000)
#endif

/* === Memory types === */
/* PS4 has two memory types:
 *   0 = Onion  (CPU-coherent, GPU-visible — for staging)
 *   1 = Garlic (GPU-local, WC — for render targets, textures, buffers)
 */
#define VK_PS4_MEMORY_TYPE_ONION  0
#define VK_PS4_MEMORY_TYPE_GARLIC 1
#define VK_PS4_MEMORY_TYPE_COUNT  2

/* === Object type enum === */
enum VkPs4ObjectType {
    VK_PS4_OBJ_INSTANCE,
    VK_PS4_OBJ_PHYSICAL_DEVICE,
    VK_PS4_OBJ_DEVICE,
    VK_PS4_OBJ_QUEUE,
    VK_PS4_OBJ_COMMAND_POOL,
    VK_PS4_OBJ_COMMAND_BUFFER,
    VK_PS4_OBJ_DEVICE_MEMORY,
    VK_PS4_OBJ_BUFFER,
    VK_PS4_OBJ_BUFFER_VIEW,
    VK_PS4_OBJ_IMAGE,
    VK_PS4_OBJ_IMAGE_VIEW,
    VK_PS4_OBJ_RENDER_PASS,
    VK_PS4_OBJ_FRAMEBUFFER,
    VK_PS4_OBJ_SHADER_MODULE,
    VK_PS4_OBJ_PIPELINE_LAYOUT,
    VK_PS4_OBJ_PIPELINE,
    VK_PS4_OBJ_DESCRIPTOR_SET_LAYOUT,
    VK_PS4_OBJ_DESCRIPTOR_POOL,
    VK_PS4_OBJ_DESCRIPTOR_SET,
    VK_PS4_OBJ_FENCE,
    VK_PS4_OBJ_SEMAPHORE,
    VK_PS4_OBJ_EVENT,
    VK_PS4_OBJ_QUERY_POOL,
    VK_PS4_OBJ_SWAPCHAIN_KHR,
};
typedef enum VkPs4ObjectType VkPs4ObjectType;

/* Forward declarations — many structs reference each other */
typedef struct VkPs4Instance VkPs4Instance;
typedef struct VkPs4PhysicalDevice VkPs4PhysicalDevice;
typedef struct VkPs4Device VkPs4Device;
typedef struct VkPs4Queue VkPs4Queue;
typedef struct VkPs4CommandPool VkPs4CommandPool;
typedef struct VkPs4CommandBuffer VkPs4CommandBuffer;
typedef struct VkPs4DeviceMemory VkPs4DeviceMemory;
typedef struct VkPs4Buffer VkPs4Buffer;
typedef struct VkPs4Image VkPs4Image;
typedef struct VkPs4ImageView VkPs4ImageView;
typedef struct VkPs4RenderPass VkPs4RenderPass;
typedef struct VkPs4Framebuffer VkPs4Framebuffer;
typedef struct VkPs4ShaderModule VkPs4ShaderModule;
typedef struct VkPs4PipelineLayout VkPs4PipelineLayout;
typedef struct VkPs4Pipeline VkPs4Pipeline;
typedef struct VkPs4DescriptorSetLayout VkPs4DescriptorSetLayout;
typedef struct VkPs4DescriptorPool VkPs4DescriptorPool;
typedef struct VkPs4DescriptorSet VkPs4DescriptorSet;
typedef struct VkPs4Fence VkPs4Fence;
typedef struct VkPs4Semaphore VkPs4Semaphore;
typedef struct VkPs4Event VkPs4Event;
typedef struct VkPs4QueryPool VkPs4QueryPool;
typedef struct VkPs4Swapchain VkPs4Swapchain;
typedef struct VkPs4DispatchTable VkPs4DispatchTable;

/* === Instance === */
struct VkPs4Instance {
    VkPs4ObjectType type;
    VkInstanceCreateInfo create_info;
    VkAllocationCallbacks allocator;
};

/* === Physical device === */
struct VkPs4PhysicalDevice {
    VkPs4ObjectType type;
    VkPs4Instance *instance;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory_properties;
    VkPhysicalDeviceFeatures features;
};

/* === Device === */
struct VkPs4Device {
    VkPs4ObjectType type;
    VkPs4PhysicalDevice *physical_device;
    VkDeviceCreateInfo create_info;
    VkAllocationCallbacks allocator;
    /* GNM state */
    bool gnm_initialized;
};

/* === Queue === */
struct VkPs4Queue {
    VkPs4ObjectType type;
    VkPs4Device *device;
    uint32_t family_index;
    /* Submit serialization — opaque pointer to platform-specific mutex */
    void *submit_mutex;
};

/* === Command pool / buffer === */
#define VK_PS4_MAX_VERTEX_BINDINGS 16

struct VkPs4CommandPool {
    VkPs4ObjectType type;
    VkPs4Device *device;
    uint32_t queue_family_index;
    VkCommandPoolCreateFlags flags;
};

struct VkPs4CommandBuffer {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPs4CommandPool *pool;
    VkCommandBufferLevel level;
    /* GNM command buffer — the actual PM4 packet storage */
    GnmCommandBuffer gnm_cmd;
    uint32_t *pm4_buffer;      /* backing store */
    uint32_t pm4_buffer_size;  /* in dwords */
    uint32_t pm4_used;         /* in dwords */
    bool is_recording;
    bool is_begin;
    /* Current render pass state */
    struct {
        VkPs4RenderPass *pass;
        VkPs4Framebuffer *framebuffer;
        VkRect2D render_area;
    } current_render_pass;
    /* Current pipeline */
    VkPs4Pipeline *current_pipeline;
    /* Bound vertex buffers */
    struct {
        VkBuffer buffer;
        VkDeviceSize offset;
    } vertex_buffers[VK_PS4_MAX_VERTEX_BINDINGS];
    uint32_t vertex_binding_count;
    /* Bound index buffer */
    struct {
        VkBuffer buffer;
        VkDeviceSize offset;
        VkIndexType type;
    } index_buffer;
};

/* === Memory === */
struct VkPs4DeviceMemory {
    VkPs4ObjectType type;
    VkPs4Device *device;
    uint32_t memory_type_index;
    GnmDirectMemory gnm_mem;
    VkDeviceSize size;
    void *mapped_ptr;
    VkDeviceSize mapped_offset;
    VkDeviceSize mapped_size;
};

/* === Buffer === */
struct VkPs4Buffer {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkBufferCreateInfo create_info;
    GnmBuffer gnm_buffer;
    VkPs4DeviceMemory *memory;
    VkDeviceSize memory_offset;
};

/* === Image === */
struct VkPs4Image {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkImageCreateInfo create_info;
    GnmTexture gnm_texture;
    GnmRenderTarget gnm_rt;        /* if used as render target */
    bool is_render_target;
    VkPs4DeviceMemory *memory;
    VkDeviceSize memory_offset;
    VkImageLayout layout;
};

/* === Image view === */
struct VkPs4ImageView {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPs4Image *image;
    VkImageViewCreateInfo create_info;
    GnmTexture gnm_view;           /* texture view descriptor */
};

/* === Render pass === */
struct VkPs4RenderPass {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkRenderPassCreateInfo create_info;
    uint32_t attachment_count;
    VkAttachmentDescription *attachments;
    uint32_t subpass_count;
    VkSubpassDescription *subpasses;
};

/* === Framebuffer === */
struct VkPs4Framebuffer {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPs4RenderPass *render_pass;
    VkFramebufferCreateInfo create_info;
    uint32_t attachment_count;
    VkPs4ImageView **attachments;
    uint32_t width;
    uint32_t height;
    uint32_t layers;
};

/* === Shader module === */
struct VkPs4ShaderModule {
    VkPs4ObjectType type;
    VkPs4Device *device;
    /* Compiled GCN shader binary (GnmShaderFileHeader + stage header + code) */
    void *binary;
    size_t binary_size;
    GnmShaderMetadata metadata;
    bool has_metadata;
};

/* === Pipeline layout / descriptor set layout === */
struct VkPs4DescriptorSetLayout {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkDescriptorSetLayoutCreateInfo create_info;
    VkDescriptorSetLayoutBinding *bindings;
    uint32_t binding_count;
};

struct VkPs4PipelineLayout {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPipelineLayoutCreateInfo create_info;
    VkPs4DescriptorSetLayout **set_layouts;
    uint32_t set_layout_count;
    VkPushConstantRange *push_constant_ranges;
    uint32_t push_constant_range_count;
};

/* === Pipeline === */
struct VkPs4Pipeline {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPipelineBindPoint bind_point;
    /* Graphics pipeline */
    VkPs4ShaderModule *vs_module;
    VkPs4ShaderModule *fs_module;
    VkPs4ShaderModule *gs_module;
    VkPs4ShaderModule *tcs_module;
    VkPs4ShaderModule *tes_module;
    VkPs4ShaderModule *cs_module;
    /* GNM shader stage registers */
    GnmVsStageRegisters vs_regs;
    GnmPsStageRegisters ps_regs;
    GnmCsStageRegisters cs_regs;
    /* Pipeline state */
    VkPipelineVertexInputStateCreateInfo vertex_input_state;
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
    VkPipelineRasterizationStateCreateInfo rasterization_state;
    VkPipelineColorBlendStateCreateInfo color_blend_state;
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
    VkPipelineViewportStateCreateInfo viewport_state;
    VkPipelineMultisampleStateCreateInfo multisample_state;
    /* Fetch shader (generated from vertex input) */
    void *fetch_shader;
    size_t fetch_shader_size;
    bool has_fetch_shader;
};

/* === Descriptor === */
struct VkPs4DescriptorPool {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkDescriptorPoolCreateInfo create_info;
};

struct VkPs4DescriptorSet {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPs4DescriptorPool *pool;
    VkPs4DescriptorSetLayout *layout;
};

/* === Sync === */
struct VkPs4Fence {
    VkPs4ObjectType type;
    VkPs4Device *device;
    bool signaled;
};

struct VkPs4Semaphore {
    VkPs4ObjectType type;
    VkPs4Device *device;
    bool signaled;
};

struct VkPs4Event {
    VkPs4ObjectType type;
    VkPs4Device *device;
    bool signaled;
};

/* === Query pool === */
struct VkPs4QueryPool {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkQueryPoolCreateInfo create_info;
};

/* === Swapchain === */
struct VkPs4Swapchain {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkSwapchainCreateInfoKHR create_info;
    GnmVideoOut video_out;
    VkPs4Image *images;
    uint32_t image_count;
    uint32_t current_image;
};

/* === Dispatch table === */
/* The dispatch table is populated by vk_ps4_dispatch.c.
 * It maps Vulkan function names to our implementation pointers. */
struct VkPs4DispatchTable {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
    /* Core Vulkan 1.0 functions — populated in vk_ps4_dispatch.c */
    PFN_vkCreateInstance CreateInstance;
    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceFeatures GetPhysicalDeviceFeatures;
    PFN_vkGetPhysicalDeviceFormatProperties GetPhysicalDeviceFormatProperties;
    PFN_vkCreateDevice CreateDevice;
    PFN_vkDestroyDevice DestroyDevice;
    PFN_vkGetDeviceQueue GetDeviceQueue;
    PFN_vkAllocateMemory AllocateMemory;
    PFN_vkFreeMemory FreeMemory;
    PFN_vkMapMemory MapMemory;
    PFN_vkUnmapMemory UnmapMemory;
    PFN_vkFlushMappedMemoryRanges FlushMappedMemoryRanges;
    PFN_vkInvalidateMappedMemoryRanges InvalidateMappedMemoryRanges;
    PFN_vkCreateBuffer CreateBuffer;
    PFN_vkDestroyBuffer DestroyBuffer;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
    PFN_vkBindBufferMemory BindBufferMemory;
    PFN_vkCreateImage CreateImage;
    PFN_vkDestroyImage DestroyImage;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements;
    PFN_vkBindImageMemory BindImageMemory;
    PFN_vkCreateImageView CreateImageView;
    PFN_vkDestroyImageView DestroyImageView;
    PFN_vkCreateRenderPass CreateRenderPass;
    PFN_vkDestroyRenderPass DestroyRenderPass;
    PFN_vkCreateFramebuffer CreateFramebuffer;
    PFN_vkDestroyFramebuffer DestroyFramebuffer;
    PFN_vkCreateShaderModule CreateShaderModule;
    PFN_vkDestroyShaderModule DestroyShaderModule;
    PFN_vkCreatePipelineLayout CreatePipelineLayout;
    PFN_vkDestroyPipelineLayout DestroyPipelineLayout;
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines;
    PFN_vkCreateComputePipelines CreateComputePipelines;
    PFN_vkDestroyPipeline DestroyPipeline;
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout;
    PFN_vkCreateDescriptorPool CreateDescriptorPool;
    PFN_vkDestroyDescriptorPool DestroyDescriptorPool;
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets;
    PFN_vkFreeDescriptorSets FreeDescriptorSets;
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets;
    PFN_vkCreateCommandPool CreateCommandPool;
    PFN_vkDestroyCommandPool DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkFreeCommandBuffers FreeCommandBuffers;
    PFN_vkBeginCommandBuffer BeginCommandBuffer;
    PFN_vkEndCommandBuffer EndCommandBuffer;
    PFN_vkResetCommandBuffer ResetCommandBuffer;
    PFN_vkCmdBindPipeline CmdBindPipeline;
    PFN_vkCmdSetViewport CmdSetViewport;
    PFN_vkCmdSetScissor CmdSetScissor;
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets;
    PFN_vkCmdBindVertexBuffers CmdBindVertexBuffers;
    PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer;
    PFN_vkCmdDraw CmdDraw;
    PFN_vkCmdDrawIndexed CmdDrawIndexed;
    PFN_vkCmdDrawIndirect CmdDrawIndirect;
    PFN_vkCmdDrawIndexedIndirect CmdDrawIndexedIndirect;
    PFN_vkCmdDispatch CmdDispatch;
    PFN_vkCmdCopyBuffer CmdCopyBuffer;
    PFN_vkCmdCopyImage CmdCopyImage;
    PFN_vkCmdBlitImage CmdBlitImage;
    PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage;
    PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer;
    PFN_vkCmdBeginRenderPass CmdBeginRenderPass;
    PFN_vkCmdEndRenderPass CmdEndRenderPass;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkCmdClearColorImage CmdClearColorImage;
    PFN_vkCmdClearDepthStencilImage CmdClearDepthStencilImage;
    PFN_vkCmdClearAttachments CmdClearAttachments;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkQueueWaitIdle QueueWaitIdle;
    PFN_vkDeviceWaitIdle DeviceWaitIdle;
    PFN_vkCreateFence CreateFence;
    PFN_vkDestroyFence DestroyFence;
    PFN_vkWaitForFences WaitForFences;
    PFN_vkResetFences ResetFences;
    PFN_vkGetFenceStatus GetFenceStatus;
    PFN_vkCreateSemaphore CreateSemaphore;
    PFN_vkDestroySemaphore DestroySemaphore;
    PFN_vkCreateEvent CreateEvent;
    PFN_vkDestroyEvent DestroyEvent;
    PFN_vkGetEventStatus GetEventStatus;
    PFN_vkSetEvent SetEvent;
    PFN_vkResetEvent ResetEvent;
    PFN_vkCreateQueryPool CreateQueryPool;
    PFN_vkDestroyQueryPool DestroyQueryPool;
    PFN_vkGetQueryPoolResults GetQueryPoolResults;
    PFN_vkCmdResetQueryPool CmdResetQueryPool;
    PFN_vkCmdBeginQuery CmdBeginQuery;
    PFN_vkCmdEndQuery CmdEndQuery;
    PFN_vkCmdWriteTimestamp CmdWriteTimestamp;
    /* WSI */
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR;
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR;
    PFN_vkQueuePresentKHR QueuePresentKHR;
};
typedef struct VkPs4DispatchTable VkPs4DispatchTable;

/* Global dispatch table — set up in vk_ps4_dispatch.c */
extern VkPs4DispatchTable g_vk_ps4_dispatch;

/* === Utility macros === */
#define VK_PS4_CAST(handle) ((void*)(handle))
#define VK_PS4_TO_OBJ(handle) ((VkPs4ObjectType*)(handle))
#define VK_PS4_CHECK_OBJ(handle, expected_type) \
    (VK_PS4_TO_OBJ(handle) && *(VK_PS4_TO_OBJ(handle)) == (expected_type))

/* === Allocator helpers === */
void *vk_ps4_alloc(const VkAllocationCallbacks *alloc, size_t size, size_t alignment);
void *vk_ps4_alloc_zero(const VkAllocationCallbacks *alloc, size_t size, size_t alignment);
void vk_ps4_free(const VkAllocationCallbacks *alloc, void *ptr);

/* === Format mapping === */
GnmDataFormat vk_ps4_vk_format_to_gnm(VkFormat format);
VkFormatProperties vk_ps4_format_properties(VkFormat format);

/* === Stub shader (when libpsbc is not available) === */
VkResult vk_ps4_shader_compile_stub(
    const uint32_t *spirv, size_t spirv_size,
    VkShaderStageFlagBits stage,
    void **out_binary, size_t *out_binary_size
);

/* === Forward declarations of all vk_ps4_* implementation functions === */
/* These are declared here so vk_ps4_entrypoints.c can call them without
 * duplicating the forward declarations from vk_ps4_dispatch.c. */

/* Instance */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateInstance(const VkInstanceCreateInfo *, const VkAllocationCallbacks *, VkInstance *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyInstance(VkInstance, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_EnumeratePhysicalDevices(VkInstance, uint32_t *, VkPhysicalDevice *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceProperties(VkPhysicalDevice, VkPhysicalDeviceProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice, uint32_t *, VkQueueFamilyProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceFeatures(VkPhysicalDevice, VkPhysicalDeviceFeatures *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceFormatProperties(VkPhysicalDevice, VkFormat, VkFormatProperties *);

/* Device */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo *, const VkAllocationCallbacks *, VkDevice *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyDevice(VkDevice, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetDeviceQueue(VkDevice, uint32_t, uint32_t, VkQueue *);

/* Memory */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AllocateMemory(VkDevice, const VkMemoryAllocateInfo *, const VkAllocationCallbacks *, VkDeviceMemory *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_FreeMemory(VkDevice, VkDeviceMemory, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_MapMemory(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void **);
VKAPI_ATTR void VKAPI_CALL vk_ps4_UnmapMemory(VkDevice, VkDeviceMemory);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_FlushMappedMemoryRanges(VkDevice, uint32_t, const VkMappedMemoryRange *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_InvalidateMappedMemoryRanges(VkDevice, uint32_t, const VkMappedMemoryRange *);

/* Buffer */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateBuffer(VkDevice, const VkBufferCreateInfo *, const VkAllocationCallbacks *, VkBuffer *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyBuffer(VkDevice, VkBuffer, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetBufferMemoryRequirements(VkDevice, VkBuffer, VkMemoryRequirements *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);

/* Image */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateImage(VkDevice, const VkImageCreateInfo *, const VkAllocationCallbacks *, VkImage *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyImage(VkDevice, VkImage, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateImageView(VkDevice, const VkImageViewCreateInfo *, const VkAllocationCallbacks *, VkImageView *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks *);

/* Render pass / framebuffer */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateRenderPass(VkDevice, const VkRenderPassCreateInfo *, const VkAllocationCallbacks *, VkRenderPass *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyRenderPass(VkDevice, VkRenderPass, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateFramebuffer(VkDevice, const VkFramebufferCreateInfo *, const VkAllocationCallbacks *, VkFramebuffer *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyFramebuffer(VkDevice, VkFramebuffer, const VkAllocationCallbacks *);

/* Shader / pipeline */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateShaderModule(VkDevice, const VkShaderModuleCreateInfo *, const VkAllocationCallbacks *, VkShaderModule *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyShaderModule(VkDevice, VkShaderModule, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreatePipelineLayout(VkDevice, const VkPipelineLayoutCreateInfo *, const VkAllocationCallbacks *, VkPipelineLayout *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyPipelineLayout(VkDevice, VkPipelineLayout, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateGraphicsPipelines(VkDevice, VkPipelineCache, uint32_t, const VkGraphicsPipelineCreateInfo *, const VkAllocationCallbacks *, VkPipeline *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateComputePipelines(VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo *, const VkAllocationCallbacks *, VkPipeline *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyPipeline(VkDevice, VkPipeline, const VkAllocationCallbacks *);

/* Descriptor */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateDescriptorSetLayout(VkDevice, const VkDescriptorSetLayoutCreateInfo *, const VkAllocationCallbacks *, VkDescriptorSetLayout *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyDescriptorSetLayout(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateDescriptorPool(VkDevice, const VkDescriptorPoolCreateInfo *, const VkAllocationCallbacks *, VkDescriptorPool *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyDescriptorPool(VkDevice, VkDescriptorPool, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo *, VkDescriptorSet *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_FreeDescriptorSets(VkDevice, VkDescriptorPool, uint32_t, const VkDescriptorSet *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_UpdateDescriptorSets(VkDevice, uint32_t, const VkWriteDescriptorSet *, uint32_t, const VkCopyDescriptorSet *);

/* Command buffer */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateCommandPool(VkDevice, const VkCommandPoolCreateInfo *, const VkAllocationCallbacks *, VkCommandPool *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyCommandPool(VkDevice, VkCommandPool, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AllocateCommandBuffers(VkDevice, const VkCommandBufferAllocateInfo *, VkCommandBuffer *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_FreeCommandBuffers(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BeginCommandBuffer(VkCommandBuffer, const VkCommandBufferBeginInfo *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_EndCommandBuffer(VkCommandBuffer);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetCommandBuffer(VkCommandBuffer, VkCommandBufferResetFlags);

/* Command buffer recording */
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindPipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetViewport(VkCommandBuffer, uint32_t, uint32_t, const VkViewport *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetScissor(VkCommandBuffer, uint32_t, uint32_t, const VkRect2D *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindDescriptorSets(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet *, uint32_t, const uint32_t *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindVertexBuffers(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer *, const VkDeviceSize *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindIndexBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDraw(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndexed(VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndexedIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDispatch(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyBuffer(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBlitImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageBlit *, VkFilter);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyBufferToImage(VkCommandBuffer, VkBuffer, VkImage, VkImageLayout, uint32_t, const VkBufferImageCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyImageToBuffer(VkCommandBuffer, VkImage, VkImageLayout, VkBuffer, uint32_t, const VkBufferImageCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBeginRenderPass(VkCommandBuffer, const VkRenderPassBeginInfo *, VkSubpassContents);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdEndRenderPass(VkCommandBuffer);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdPipelineBarrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags, uint32_t, const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearColorImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue *, uint32_t, const VkImageSubresourceRange *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearDepthStencilImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearDepthStencilValue *, uint32_t, const VkImageSubresourceRange *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearAttachments(VkCommandBuffer, uint32_t, const VkClearAttachment *, uint32_t, const VkClearRect *);

/* Queue */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueueSubmit(VkQueue, uint32_t, const VkSubmitInfo *, VkFence);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueueWaitIdle(VkQueue);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_DeviceWaitIdle(VkDevice);

/* Sync */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateFence(VkDevice, const VkFenceCreateInfo *, const VkAllocationCallbacks *, VkFence *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyFence(VkDevice, VkFence, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_WaitForFences(VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetFences(VkDevice, uint32_t, const VkFence *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetFenceStatus(VkDevice, VkFence);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSemaphore(VkDevice, const VkSemaphoreCreateInfo *, const VkAllocationCallbacks *, VkSemaphore *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySemaphore(VkDevice, VkSemaphore, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateEvent(VkDevice, const VkEventCreateInfo *, const VkAllocationCallbacks *, VkEvent *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyEvent(VkDevice, VkEvent, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetEventStatus(VkDevice, VkEvent);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_SetEvent(VkDevice, VkEvent);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetEvent(VkDevice, VkEvent);

/* Query */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateQueryPool(VkDevice, const VkQueryPoolCreateInfo *, const VkAllocationCallbacks *, VkQueryPool *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyQueryPool(VkDevice, VkQueryPool, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetQueryPoolResults(VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void *, VkDeviceSize, VkQueryResultFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdResetQueryPool(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBeginQuery(VkCommandBuffer, VkQueryPool, uint32_t, VkQueryControlFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdEndQuery(VkCommandBuffer, VkQueryPool, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdWriteTimestamp(VkCommandBuffer, VkPipelineStageFlagBits, VkQueryPool, uint32_t);

/* Swapchain */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSwapchainKHR(VkDevice, const VkSwapchainCreateInfoKHR *, const VkAllocationCallbacks *, VkSwapchainKHR *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySwapchainKHR(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetSwapchainImagesKHR(VkDevice, VkSwapchainKHR, uint32_t *, VkImage *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AcquireNextImageKHR(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueuePresentKHR(VkQueue, const VkPresentInfoKHR *);

#ifdef __cplusplus
}
#endif

#endif /* VK_PS4_INTERNAL_H */
