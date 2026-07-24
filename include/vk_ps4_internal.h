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
#define VK_PS4_API_VERSION VK_MAKE_VERSION(1, 1, 0)
#define VK_PS4_DRIVER_VERSION VK_MAKE_VERSION(0, 1, 0)

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
typedef struct VkPs4BufferView VkPs4BufferView;
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

/* === Instance === */
struct VkPs4Instance {
    VkPs4ObjectType type;
    VkInstanceCreateInfo create_info;
    VkAllocationCallbacks allocator;
    VkPs4PhysicalDevice *physical_device;  /* cached, freed in DestroyInstance */
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
#define VK_PS4_MAX_QUEUES 16
struct VkPs4Device {
    VkPs4ObjectType type;
    VkPs4PhysicalDevice *physical_device;
    VkDeviceCreateInfo create_info;
    VkAllocationCallbacks allocator;
    /* Cached queues — allocated in CreateDevice, freed in DestroyDevice */
    VkPs4Queue *queues[VK_PS4_MAX_QUEUES];
    uint32_t queue_count;
    /* GNM state */
    bool gnm_initialized;
    /* Device-wide GNM init command buffer backing store.
     * Allocated from Garlic direct memory so the GPU can execute the
     * sceGnmDrawInitDefaultHardwareState preamble submitted at CreateDevice.
     * Held in the device so DestroyDevice can release it after the GPU is
     * quiesced. */
    GnmDirectMemory gnm_init_mem;
    uint32_t *gnm_init_cmd;
    uint32_t gnm_init_cmd_dwords;
    /* Epilogue command buffer used by QueueSubmit to emit EOP event writes
         * for fence/semaphore signaling.  Reused across submits — only one
         * submit is in flight at a time because QueueSubmit is serialized. */
    GnmDirectMemory gnm_epilogue_mem;
    uint32_t *gnm_epilogue_cmd;
    uint32_t gnm_epilogue_cmd_dwords;
    /* Embedded clear pixel shader for tiled RT clears.
     * The clear PS outputs a UBO vec4 to MRT0, enabling draw-based
     * clears on tiled surfaces where FillMemory doesn't work.
     * Allocated from regular memory (GPU-accessible on PS4) at
     * device init time. */
    void *clear_ps_binary;
    GnmPsStageRegisters clear_ps_regs;
    bool clear_ps_ready;
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
#define VK_PS4_MAX_COMMAND_BUFFERS_PER_POOL 256

struct VkPs4CommandPool {
    VkPs4ObjectType type;
    VkPs4Device *device;
    uint32_t queue_family_index;
    VkCommandPoolCreateFlags flags;
    /* Track allocated command buffers for cleanup on DestroyCommandPool */
    VkPs4CommandBuffer *command_buffers[VK_PS4_MAX_COMMAND_BUFFERS_PER_POOL];
    uint32_t command_buffer_count;
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
        uint32_t current_subpass;  /* index into pass->subpasses */
        VkClearValue clear_values[16];  /* deep-copied from CmdBeginRenderPass */
        uint32_t clear_value_count;
        /* For imageless framebuffers: attachment views from
         * VkRenderPassAttachmentBeginInfo at beginRenderPass time. */
        VkPs4ImageView *imageless_attachments[16];
        uint32_t imageless_attachment_count;
    } current_render_pass;
    /* Current pipeline */
    VkPs4Pipeline *current_pipeline;
    /* Bound vertex buffers */
    struct {
        VkBuffer buffer;
        VkDeviceSize offset;
    } vertex_buffers[VK_PS4_MAX_VERTEX_BINDINGS];
    uint32_t vertex_binding_count;
    /* GnmBuffer descriptors for bound vertex buffers — emitted to GPU
     * via SetPointerUserData at the PTR_VERTEXBUFFERTABLE slot.
     * This array must be in GPU-readable memory at draw time. */
    GnmBuffer gnm_vertex_buffers[VK_PS4_MAX_VERTEX_BINDINGS];
    bool vertex_buffers_dirty;  /* re-emit VB table on next draw */
    /* Bound index buffer */
    struct {
        VkBuffer buffer;
        VkDeviceSize offset;
        VkIndexType type;
    } index_buffer;
    /* Shadow stencil state for read-modify-write on dynamic stencil commands.
     * Without this, each CmdSetStencil* would clobber the other fields of
     * DB_STENCILREFMASK / DB_STENCILREFMASK_BF. */
    uint32_t stencil_refmask_front;   /* DB_STENCILREFMASK shadow */
    uint32_t stencil_refmask_back;    /* DB_STENCILREFMASK_BF shadow */
    bool stencil_shadow_valid;        /* initialized from pipeline bind */
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

/* === Buffer View === */
struct VkPs4BufferView {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPs4Buffer *buffer;
    GnmBuffer gnm_buffer;       /* V# descriptor for texel buffer access */
    VkFormat format;
    VkDeviceSize offset;
    VkDeviceSize range;
};

/* === Image === */
struct VkPs4Image {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkImageCreateInfo create_info;
    GnmTexture gnm_texture;
    GnmRenderTarget gnm_rt;        /* if used as render target */
    GnmDepthRenderTarget gnm_drt;  /* if used as depth target */
    bool is_render_target;
    bool is_depth_target;
    /* Swapchain image tracking: if this image is a swapchain buffer,
     * these fields identify the video out handle and buffer index so
     * that CmdBeginRenderPass can emit WaitUntilSafeForRendering. */
    bool is_swapchain_image;
    int32_t video_out_handle;
    uint32_t swapchain_buffer_index;
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
    /* Deep-copied subpass internal arrays — one slot per subpass.
     * Kept alive for the render pass's lifetime because CmdBeginRenderPass
     * and CmdNextSubpass dereference them after the caller's pCreateInfo
     * has been freed. */
    VkAttachmentReference **subpass_input_attachments;   /* [subpass_count], NULL slots ok */
    VkAttachmentReference **subpass_color_attachments;   /* [subpass_count] */
    VkAttachmentReference **subpass_resolve_attachments; /* [subpass_count], NULL slots ok */
    VkAttachmentReference **subpass_depth_stencil;       /* [subpass_count], NULL slots ok */
    uint32_t **subpass_preserve_attachments;             /* [subpass_count], NULL slots ok */
    uint32_t subpass_dependency_count;
    VkSubpassDependency *dependencies;
};

/* === Framebuffer === */
struct VkPs4Framebuffer {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPs4RenderPass *render_pass;
    VkFramebufferCreateInfo create_info;
    uint32_t attachment_count;
    VkPs4ImageView **attachments;   /* NULL for imageless framebuffers */
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    bool imageless;                 /* VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT_KHR */
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
    /* VK_EXT_descriptor_indexing: per-binding flags */
    VkDescriptorBindingFlags *binding_flags;
    /* Variable descriptor count: the last binding's max count can vary
     * at descriptor set allocation time. */
    uint32_t variable_descriptor_binding;  /* binding index, or UINT32_MAX */
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

/* === Descriptor / pipeline limits === */
#define VK_PS4_MAX_DESCRIPTOR_BINDINGS 64
#define VK_PS4_MAX_INPUT_USAGE_SLOTS 32

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
    /* Compiled GCN shader binaries — kept alive for the pipeline's lifetime
     * because the stage registers contain GPU addresses that point into
     * these buffers.  Freed in DestroyPipeline. */
    void *vs_binary;
    void *ps_binary;
    void *gs_binary;
    void *tcs_binary;
    void *tes_binary;
    void *cs_binary;
    /* GNM shader stage registers */
    GnmVsStageRegisters vs_regs;
    GnmPsStageRegisters ps_regs;
    GnmCsStageRegisters cs_regs;
    /* Tessellation / geometry shader stage registers */
    GnmHsStageRegisters hs_regs;
    GnmLsStageRegisters ls_regs;
    GnmGsStageRegisters gs_regs;
    GnmEsStageRegisters es_regs;
    bool has_hs;  /* tessellation control shader (hull shader) */
    bool has_ls;  /* tessellation control shader (LS = VS before tess) */
    bool has_gs;  /* geometry shader */
    bool has_es;  /* geometry shader (ES = VS before GS) */
    bool has_ds_vs;  /* TES compiled as DS_VS (post-tessellation VS) */
    uint32_t tess_patch_control_points;  /* from VkPipelineTessellationStateCreateInfo */
    /* Pipeline state */
    VkPipelineVertexInputStateCreateInfo vertex_input_state;
    VkVertexInputBindingDescription *vertex_bindings;      /* deep copy */
    VkVertexInputAttributeDescription *vertex_attributes;  /* deep copy */
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
    VkPipelineRasterizationStateCreateInfo rasterization_state;
    VkPipelineColorBlendStateCreateInfo color_blend_state;
    VkPipelineColorBlendAttachmentState *blend_attachments;  /* deep copy */
    GnmBlendControl blend_controls[8];  /* pre-computed per RT slot */
    uint32_t blend_control_count;       /* number of valid blend_controls */
    uint32_t color_write_mask;          /* packed RT mask (4 bits per RT) */
    float blend_constants[4];
    bool has_blend_state;
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
    VkPipelineViewportStateCreateInfo viewport_state;
    VkPipelineMultisampleStateCreateInfo multisample_state;
    /* Fetch shader (generated from vertex input) */
    void *fetch_shader;
    size_t fetch_shader_size;
    bool has_fetch_shader;
    bool has_ps;          /* true if fragment shader was set */
    bool has_fetch_shader_slot;  /* true if fetch_shader_slot is valid */
    bool has_vb_table_slot;      /* true if vertex_buffer_table_slot is valid */
    /* User-data slot for fetch shader pointer (SUBPTR_FETCHSHADER) */
    uint32_t fetch_shader_slot;
    /* User-data slot for vertex buffer table (PTR_VERTEXBUFFERTABLE) */
    uint32_t vertex_buffer_table_slot;
    /* Input usage slot tables extracted from compiled shader binaries.
     * These map Vulkan binding numbers (apislot) to GNM user-data
     * registers (startregister) for each shader stage. */
    GnmInputUsageSlot vs_input_usage_slots[VK_PS4_MAX_INPUT_USAGE_SLOTS];
    uint32_t vs_input_usage_slot_count;
    GnmInputUsageSlot ps_input_usage_slots[VK_PS4_MAX_INPUT_USAGE_SLOTS];
    uint32_t ps_input_usage_slot_count;
    /* Vertex input semantics extracted from VS shader binary */
    GnmVertexInputSemantic vs_input_semantics[VK_PS4_MAX_INPUT_USAGE_SLOTS];
    uint32_t vs_input_semantic_count;
    /* Push constant inline register mapping — extracted from
     * IMM_ALUFLOATCONST input usage slots emitted by psbc.
     * Each entry maps a push constant dword index to a user-data
     * register for that shader stage. */
#define VK_PS4_MAX_PUSH_CONST_DWORDS 32  /* 128 bytes / 4 */
    struct {
        uint8_t dword_index;     /* push constant dword offset */
        uint8_t user_data_reg;   /* GNM user-data register */
    } vs_push_const_slots[VK_PS4_MAX_PUSH_CONST_DWORDS];
    uint32_t vs_push_const_slot_count;
    struct {
        uint8_t dword_index;
        uint8_t user_data_reg;
    } ps_push_const_slots[VK_PS4_MAX_PUSH_CONST_DWORDS];
    uint32_t ps_push_const_slot_count;
    struct {
        uint8_t dword_index;
        uint8_t user_data_reg;
    } cs_push_const_slots[VK_PS4_MAX_PUSH_CONST_DWORDS];
    uint32_t cs_push_const_slot_count;
    /* VS draw offset registers — extracted from IMM_ALUFLOATCONST slots
     * with special apislot values emitted by psbc.
     * 0xFE = base_vertex (vertexOffset), 0xFF = start_instance (firstInstance). */
    uint8_t vs_base_vertex_reg;      /* user-data reg for vertexOffset */
    uint8_t vs_start_instance_reg;   /* user-data reg for firstInstance */
    bool has_base_vertex_reg;
    bool has_start_instance_reg;
    /* Pre-computed depth/stencil GNM state (converted from VkPipelineDepthStencilStateCreateInfo) */
    GnmDepthStencilControl depth_stencil_control;
    bool has_depth_stencil_state;  /* true if pDepthStencilState was non-NULL */
    /* Stencil ref/mask register values (pre-computed for CmdBindPipeline) */
    uint32_t stencil_refmask;      /* DB_STENCILREFMASK (front) */
    uint32_t stencil_refmask_bf;   /* DB_STENCILREFMASK_BF (back) */
    uint32_t stencil_control;      /* DB_STENCIL_CONTROL (ops front+back) */
};

/* === Descriptor === */
typedef struct {
    VkDescriptorType type;
    uint32_t count;
    uint32_t binding_number;  /* Vulkan binding number (may be sparse) */
    bool resources_allocated; /* true once resource arrays are alloc'd */
    /* Resource data — which array is valid depends on type */
    GnmBuffer *buffers;     /* UBO / SSBO / texel buffer */
    GnmTexture *textures;   /* sampled / storage image */
    GnmSampler *samplers;   /* sampler (incl. combined image sampler) */
} VkPs4DescriptorBinding;

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
    VkPs4DescriptorBinding bindings[VK_PS4_MAX_DESCRIPTOR_BINDINGS];
    uint32_t binding_count;
    /* VK_EXT_descriptor_indexing: variable descriptor count for the
     * last binding (0 if layout doesn't use variable count). */
    uint32_t variable_descriptor_count;
};

/* === Sync === */
struct VkPs4Fence {
    VkPs4ObjectType type;
    VkPs4Device *device;
    /* CPU-side fallback flag used when the device has no GNM epilogue
         * buffer (host tests) or before the first signal. */
    bool signaled;
    /* GPU signal label — allocated from Garlic direct memory.  The GPU
         * writes signal_value here via an EOP event write at the end of the
         * submit that signals this fence.  WaitForFences polls this label. */
    GnmDirectMemory label_mem;
    volatile uint32_t *label;
    uint32_t signal_value;
};

struct VkPs4Semaphore {
    VkPs4ObjectType type;
    VkPs4Device *device;
    bool signaled;
    /* GPU signal label — same EOP mechanism as fences.  Wait semaphores
         * block the next submit's first command buffer with WaitMem until
         * the label matches signal_value. */
    GnmDirectMemory label_mem;
    volatile uint32_t *label;
    uint32_t signal_value;
    /* VK_KHR_timeline_semaphore: timeline semaphore support.
     * Timeline semaphores maintain a monotonically increasing counter
     * instead of a binary signaled/unsignaled state.  The counter is
     * software-managed (CPU-side) since the GPU EOP label only supports
     * 32-bit values and we need 64-bit timeline values. */
    bool is_timeline;
    uint64_t timeline_value;      /* current signaled value */
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
    /* GPU-visible memory for query results.
     * Each query slot stores a uint64_t result.
     * For occlusion queries: ZPASS count.
     * For timestamp queries: GPU timestamp value. */
    GnmDirectMemory gnm_mem;     /* full direct memory handle for release */
    void *result_buffer;         /* CPU-mapped pointer to result memory */
    uint64_t result_gpu_addr;    /* GPU address of result memory */
    VkDeviceSize result_size;    /* total size in bytes */
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
    /* Per-image in-flight tracking for AcquireNextImageKHR.
     *
     * image_in_flight[] is the authoritative tracking — set to true
     * when an image is acquired and cleared when QueuePresentKHR
     * completes the flip.  This is independent of whether the caller
     * passed a fence, so semaphore-only sync patterns work correctly.
     *
     * image_fences[] stores the caller's fence (if provided) as an
     * optimization: when all images are in-flight, we wait on the
     * fence rather than busy-spinning.  May be NULL if the caller
     * used semaphore-only sync. */
    bool image_in_flight[GNM_VIDEO_OUT_MAX_BUFFERS];
    VkFence image_fences[GNM_VIDEO_OUT_MAX_BUFFERS];
};

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
GnmDataFormat vk_ps4_vk_format_to_gnm_buffer(VkFormat format);
bool vk_ps4_gnm_format_is_buffer_compatible(GnmDataFormat fmt);
VkFormatProperties vk_ps4_format_properties(VkFormat format);

/* === Device extension enumeration === */
VkResult vk_ps4_enumerate_device_extensions(
    const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties
);

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
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice, uint32_t *, VkQueueFamilyProperties2 *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceFeatures(VkPhysicalDevice, VkPhysicalDeviceFeatures *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceFormatProperties(VkPhysicalDevice, VkFormat, VkFormatProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice, VkFormat, VkImageType, VkSampleCountFlagBits, VkImageUsageFlags, VkImageTiling, uint32_t *, VkSparseImageFormatProperties *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice, VkFormat, VkImageType, VkImageTiling, VkImageUsageFlags, VkImageCreateFlags, VkImageFormatProperties *);

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
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetBufferMemoryRequirements2(VkDevice, const VkBufferMemoryRequirementsInfo2 *, VkMemoryRequirements2 *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);

/* Image */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateImage(VkDevice, const VkImageCreateInfo *, const VkAllocationCallbacks *, VkImage *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyImage(VkDevice, VkImage, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetImageMemoryRequirements2(VkDevice, const VkImageMemoryRequirementsInfo2 *, VkMemoryRequirements2 *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetImageSparseMemoryRequirements2(VkDevice, const VkImageSparseMemoryRequirementsInfo2 *, uint32_t *, VkSparseImageMemoryRequirements2 *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateImageView(VkDevice, const VkImageViewCreateInfo *, const VkAllocationCallbacks *, VkImageView *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateBufferView(VkDevice, const VkBufferViewCreateInfo *, const VkAllocationCallbacks *, VkBufferView *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyBufferView(VkDevice, VkBufferView, const VkAllocationCallbacks *);
VKAPI_ATTR VkDeviceAddress VKAPI_CALL vk_ps4_GetBufferDeviceAddress(VkDevice, const VkBufferDeviceAddressInfo *);

/* Misc */
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetRenderAreaGranularity(VkDevice, VkRenderPass, VkExtent2D *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetDeviceMemoryCommitment(VkDevice, VkDeviceMemory, VkDeviceSize *);

/* Render pass / framebuffer */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateRenderPass(VkDevice, const VkRenderPassCreateInfo *, const VkAllocationCallbacks *, VkRenderPass *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateRenderPass2(VkDevice, const VkRenderPassCreateInfo2 *, const VkAllocationCallbacks *, VkRenderPass *);
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

/* Pipeline cache */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreatePipelineCache(VkDevice, const VkPipelineCacheCreateInfo *, const VkAllocationCallbacks *, VkPipelineCache *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyPipelineCache(VkDevice, VkPipelineCache, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetPipelineCacheData(VkDevice, VkPipelineCache, size_t *, void *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_MergePipelineCaches(VkDevice, VkPipelineCache, uint32_t, const VkPipelineCache *);

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
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetLineWidth(VkCommandBuffer, float);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetDepthBias(VkCommandBuffer, float, float, float);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetBlendConstants(VkCommandBuffer, const float[4]);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetDepthBounds(VkCommandBuffer, float, float);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetStencilCompareMask(VkCommandBuffer, VkStencilFaceFlags, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetStencilWriteMask(VkCommandBuffer, VkStencilFaceFlags, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetStencilReference(VkCommandBuffer, VkStencilFaceFlags, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindDescriptorSets(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet *, uint32_t, const uint32_t *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindVertexBuffers(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer *, const VkDeviceSize *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindIndexBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDraw(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndexed(VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndexedIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDispatch(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDispatchIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyBuffer(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdFillBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdUpdateBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, const void *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBlitImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageBlit *, VkFilter);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdResolveImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageResolve *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyBufferToImage(VkCommandBuffer, VkBuffer, VkImage, VkImageLayout, uint32_t, const VkBufferImageCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyImageToBuffer(VkCommandBuffer, VkImage, VkImageLayout, VkBuffer, uint32_t, const VkBufferImageCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBeginRenderPass(VkCommandBuffer, const VkRenderPassBeginInfo *, VkSubpassContents);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdNextSubpass(VkCommandBuffer, VkSubpassContents);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdEndRenderPass(VkCommandBuffer);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBeginRenderPass2(VkCommandBuffer, const VkRenderPassBeginInfo *, const VkSubpassBeginInfo *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdNextSubpass2(VkCommandBuffer, const VkSubpassBeginInfo *, const VkSubpassEndInfo *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdEndRenderPass2(VkCommandBuffer, const VkSubpassEndInfo *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdPipelineBarrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags, uint32_t, const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetEvent(VkCommandBuffer, VkEvent, VkPipelineStageFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdResetEvent(VkCommandBuffer, VkEvent, VkPipelineStageFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdWaitEvents(VkCommandBuffer, uint32_t, const VkEvent *, VkPipelineStageFlags, VkPipelineStageFlags, uint32_t, const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearColorImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue *, uint32_t, const VkImageSubresourceRange *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearDepthStencilImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearDepthStencilValue *, uint32_t, const VkImageSubresourceRange *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearAttachments(VkCommandBuffer, uint32_t, const VkClearAttachment *, uint32_t, const VkClearRect *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdPushConstants(VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, uint32_t, uint32_t, const void *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdExecuteCommands(VkCommandBuffer, uint32_t, const VkCommandBuffer *);

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

/* VK_KHR_timeline_semaphore */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetSemaphoreCounterValueKHR(VkDevice, VkSemaphore, uint64_t *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_SignalSemaphoreKHR(VkDevice, const VkSemaphoreSignalInfoKHR *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_WaitSemaphoresKHR(VkDevice, const VkSemaphoreWaitInfoKHR *, uint64_t);

/* Query */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateQueryPool(VkDevice, const VkQueryPoolCreateInfo *, const VkAllocationCallbacks *, VkQueryPool *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyQueryPool(VkDevice, VkQueryPool, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetQueryPoolResults(VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void *, VkDeviceSize, VkQueryResultFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdResetQueryPool(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_ResetQueryPoolEXT(VkDevice, VkQueryPool, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBeginQuery(VkCommandBuffer, VkQueryPool, uint32_t, VkQueryControlFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdEndQuery(VkCommandBuffer, VkQueryPool, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdWriteTimestamp(VkCommandBuffer, VkPipelineStageFlagBits, VkQueryPool, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyQueryPoolResults(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t, VkBuffer, VkDeviceSize, VkDeviceSize, VkQueryResultFlags);

/* Swapchain */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSwapchainKHR(VkDevice, const VkSwapchainCreateInfoKHR *, const VkAllocationCallbacks *, VkSwapchainKHR *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySwapchainKHR(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetSwapchainImagesKHR(VkDevice, VkSwapchainKHR, uint32_t *, VkImage *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AcquireNextImageKHR(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueuePresentKHR(VkQueue, const VkPresentInfoKHR *);

/* Sampler */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSampler(VkDevice, const VkSamplerCreateInfo *, const VkAllocationCallbacks *, VkSampler *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySampler(VkDevice, VkSampler, const VkAllocationCallbacks *);

/* Command pool management */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetCommandPool(VkDevice, VkCommandPool, VkCommandPoolResetFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_TrimCommandPool(VkDevice, VkCommandPool, VkCommandPoolTrimFlags);

/* Descriptor pool management */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetDescriptorPool(VkDevice, VkDescriptorPool, VkDescriptorPoolResetFlags);

/* Image subresource layout */
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetImageSubresourceLayout(VkDevice, VkImage, const VkImageSubresource *, VkSubresourceLayout *);

/* === Vulkan 1.1 core functions === */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_EnumerateInstanceVersion(uint32_t *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_EnumeratePhysicalDeviceGroups(VkInstance, uint32_t *, VkPhysicalDeviceGroupProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceProperties2(VkPhysicalDevice, VkPhysicalDeviceProperties2 *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceFeatures2(VkPhysicalDevice, VkPhysicalDeviceFeatures2 *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice, VkFormat, VkFormatProperties2 *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2 *, VkImageFormatProperties2 *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice, const VkPhysicalDeviceExternalBufferInfo *, VkExternalBufferProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice, const VkPhysicalDeviceExternalFenceInfo *, VkExternalFenceProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo *, VkExternalSemaphoreProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties2 *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2 *, uint32_t *, VkSparseImageFormatProperties2 *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSamplerYcbcrConversion(VkDevice, const VkSamplerYcbcrConversionCreateInfo *, const VkAllocationCallbacks *, VkSamplerYcbcrConversion *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySamplerYcbcrConversion(VkDevice, VkSamplerYcbcrConversion, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetDeviceQueue2(VkDevice, const VkDeviceQueueInfo2 *, VkQueue *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindBufferMemory2(VkDevice, uint32_t, const VkBindBufferMemoryInfo *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindImageMemory2(VkDevice, uint32_t, const VkBindImageMemoryInfo *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetDeviceGroupPeerMemoryFeatures(VkDevice, uint32_t, uint32_t, uint32_t, VkPeerMemoryFeatureFlags *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetDescriptorSetLayoutSupport(VkDevice, const VkDescriptorSetLayoutCreateInfo *, VkDescriptorSetLayoutSupport *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateDescriptorUpdateTemplate(VkDevice, const VkDescriptorUpdateTemplateCreateInfo *, const VkAllocationCallbacks *, VkDescriptorUpdateTemplate *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyDescriptorUpdateTemplate(VkDevice, VkDescriptorUpdateTemplate, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_UpdateDescriptorSetWithTemplate(VkDevice, VkDescriptorSet, VkDescriptorUpdateTemplate, const void *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetDeviceMask(VkCommandBuffer, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDispatchBase(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* VK_PS4_INTERNAL_H */
