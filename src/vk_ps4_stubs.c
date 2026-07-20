/*
 * vk_ps4_stubs.c — Stub implementations for not-yet-implemented Vulkan functions.
 *
 * As phases progress, functions here are moved to their own files with
 * real implementations. For now, they return VK_ERROR_NOT_IMPLEMENTED or
 * are no-ops so the ICD links and the dispatch table resolves.
 */

#include "vk_ps4_internal.h"

/* === Memory === */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AllocateMemory(VkDevice d, const VkMemoryAllocateInfo *i, const VkAllocationCallbacks *a, VkDeviceMemory *m) { (void)d;(void)i;(void)a;(void)m; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_FreeMemory(VkDevice d, VkDeviceMemory m, const VkAllocationCallbacks *a) { (void)d;(void)m;(void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_MapMemory(VkDevice d, VkDeviceMemory m, VkDeviceSize o, VkDeviceSize s, VkMemoryMapFlags f, void **p) { (void)d;(void)m;(void)o;(void)s;(void)f;(void)p; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_UnmapMemory(VkDevice d, VkDeviceMemory m) { (void)d;(void)m; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_FlushMappedMemoryRanges(VkDevice d, uint32_t c, const VkMappedMemoryRange *r) { (void)d;(void)c;(void)r; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_InvalidateMappedMemoryRanges(VkDevice d, uint32_t c, const VkMappedMemoryRange *r) { (void)d;(void)c;(void)r; return VK_SUCCESS; }

/* === Buffer === */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateBuffer(VkDevice d, const VkBufferCreateInfo *i, const VkAllocationCallbacks *a, VkBuffer *b) { (void)d;(void)i;(void)a;(void)b; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyBuffer(VkDevice d, VkBuffer b, const VkAllocationCallbacks *a) { (void)d;(void)b;(void)a; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetBufferMemoryRequirements(VkDevice d, VkBuffer b, VkMemoryRequirements *r) { (void)d;(void)b; if(r){r->size=0;r->alignment=4;r->memoryTypeBits=0xFFFFFFFF;} }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindBufferMemory(VkDevice d, VkBuffer b, VkDeviceMemory m, VkDeviceSize o) { (void)d;(void)b;(void)m;(void)o; return VK_ERROR_NOT_IMPLEMENTED; }

/* === Image === */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateImage(VkDevice d, const VkImageCreateInfo *i, const VkAllocationCallbacks *a, VkImage *im) { (void)d;(void)i;(void)a;(void)im; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyImage(VkDevice d, VkImage im, const VkAllocationCallbacks *a) { (void)d;(void)im;(void)a; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetImageMemoryRequirements(VkDevice d, VkImage im, VkMemoryRequirements *r) { (void)d;(void)im; if(r){r->size=0;r->alignment=64;r->memoryTypeBits=0xFFFFFFFF;} }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindImageMemory(VkDevice d, VkImage im, VkDeviceMemory m, VkDeviceSize o) { (void)d;(void)im;(void)m;(void)o; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateImageView(VkDevice d, const VkImageViewCreateInfo *i, const VkAllocationCallbacks *a, VkImageView *v) { (void)d;(void)i;(void)a;(void)v; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyImageView(VkDevice d, VkImageView v, const VkAllocationCallbacks *a) { (void)d;(void)v;(void)a; }

/* === Render pass / framebuffer === */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateRenderPass(VkDevice d, const VkRenderPassCreateInfo *i, const VkAllocationCallbacks *a, VkRenderPass *r) { (void)d;(void)i;(void)a;(void)r; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyRenderPass(VkDevice d, VkRenderPass r, const VkAllocationCallbacks *a) { (void)d;(void)r;(void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateFramebuffer(VkDevice d, const VkFramebufferCreateInfo *i, const VkAllocationCallbacks *a, VkFramebuffer *f) { (void)d;(void)i;(void)a;(void)f; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyFramebuffer(VkDevice d, VkFramebuffer f, const VkAllocationCallbacks *a) { (void)d;(void)f;(void)a; }

/* === Shader / pipeline === */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateShaderModule(VkDevice d, const VkShaderModuleCreateInfo *i, const VkAllocationCallbacks *a, VkShaderModule *s) {
    (void)d;(void)a;
    if (!i || !s) return VK_ERROR_INITIALIZATION_FAILED;
    /* Stub: store SPIR-V as-is. Real implementation calls libpsbc. */
    VkPs4Device *dev = (VkPs4Device *)d;
    VkPs4ShaderModule *mod = vk_ps4_alloc_zero(&dev->allocator, sizeof(*mod), 16);
    if (!mod) return VK_ERROR_OUT_OF_HOST_MEMORY;
    mod->type = VK_PS4_OBJ_SHADER_MODULE;
    mod->device = dev;
    mod->binary = NULL;
    mod->binary_size = 0;
    mod->has_metadata = false;
    *s = (VkShaderModule)mod;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyShaderModule(VkDevice d, VkShaderModule s, const VkAllocationCallbacks *a) { (void)d;(void)s;(void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreatePipelineLayout(VkDevice d, const VkPipelineLayoutCreateInfo *i, const VkAllocationCallbacks *a, VkPipelineLayout *p) { (void)d;(void)i;(void)a;(void)p; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyPipelineLayout(VkDevice d, VkPipelineLayout p, const VkAllocationCallbacks *a) { (void)d;(void)p;(void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateGraphicsPipelines(VkDevice d, VkPipelineCache c, uint32_t n, const VkGraphicsPipelineCreateInfo *i, const VkAllocationCallbacks *a, VkPipeline *p) { (void)d;(void)c;(void)n;(void)i;(void)a;(void)p; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateComputePipelines(VkDevice d, VkPipelineCache c, uint32_t n, const VkComputePipelineCreateInfo *i, const VkAllocationCallbacks *a, VkPipeline *p) { (void)d;(void)c;(void)n;(void)i;(void)a;(void)p; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyPipeline(VkDevice d, VkPipeline p, const VkAllocationCallbacks *a) { (void)d;(void)p;(void)a; }

/* === Descriptor === */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateDescriptorSetLayout(VkDevice d, const VkDescriptorSetLayoutCreateInfo *i, const VkAllocationCallbacks *a, VkDescriptorSetLayout *l) { (void)d;(void)i;(void)a;(void)l; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyDescriptorSetLayout(VkDevice d, VkDescriptorSetLayout l, const VkAllocationCallbacks *a) { (void)d;(void)l;(void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateDescriptorPool(VkDevice d, const VkDescriptorPoolCreateInfo *i, const VkAllocationCallbacks *a, VkDescriptorPool *p) { (void)d;(void)i;(void)a;(void)p; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyDescriptorPool(VkDevice d, VkDescriptorPool p, const VkAllocationCallbacks *a) { (void)d;(void)p;(void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AllocateDescriptorSets(VkDevice d, const VkDescriptorSetAllocateInfo *i, VkDescriptorSet *s) { (void)d;(void)i;(void)s; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_FreeDescriptorSets(VkDevice d, VkDescriptorPool p, uint32_t c, const VkDescriptorSet *s) { (void)d;(void)p;(void)c;(void)s; return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_UpdateDescriptorSets(VkDevice d, uint32_t wc, const VkWriteDescriptorSet *w, uint32_t cc, const VkCopyDescriptorSet *c) { (void)d;(void)wc;(void)w;(void)cc;(void)c; }

/* === Command buffer === */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateCommandPool(VkDevice d, const VkCommandPoolCreateInfo *i, const VkAllocationCallbacks *a, VkCommandPool *p) { (void)d;(void)i;(void)a;(void)p; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyCommandPool(VkDevice d, VkCommandPool p, const VkAllocationCallbacks *a) { (void)d;(void)p;(void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AllocateCommandBuffers(VkDevice d, const VkCommandBufferAllocateInfo *i, VkCommandBuffer *c) { (void)d;(void)i;(void)c; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_FreeCommandBuffers(VkDevice d, VkCommandPool p, uint32_t c, const VkCommandBuffer *b) { (void)d;(void)p;(void)c;(void)b; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BeginCommandBuffer(VkCommandBuffer c, const VkCommandBufferBeginInfo *i) { (void)c;(void)i; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_EndCommandBuffer(VkCommandBuffer c) { (void)c; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetCommandBuffer(VkCommandBuffer c, VkCommandBufferResetFlags f) { (void)c;(void)f; return VK_SUCCESS; }

/* === Command buffer recording === */
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindPipeline(VkCommandBuffer c, VkPipelineBindPoint b, VkPipeline p) { (void)c;(void)b;(void)p; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetViewport(VkCommandBuffer c, uint32_t f, uint32_t n, const VkViewport *v) { (void)c;(void)f;(void)n;(void)v; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetScissor(VkCommandBuffer c, uint32_t f, uint32_t n, const VkRect2D *r) { (void)c;(void)f;(void)n;(void)r; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindDescriptorSets(VkCommandBuffer c, VkPipelineBindPoint b, VkPipelineLayout l, uint32_t f, uint32_t n, const VkDescriptorSet *s, uint32_t dc, const uint32_t *d) { (void)c;(void)b;(void)l;(void)f;(void)n;(void)s;(void)dc;(void)d; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindVertexBuffers(VkCommandBuffer c, uint32_t f, uint32_t n, const VkBuffer *b, const VkDeviceSize *o) { (void)c;(void)f;(void)n;(void)b;(void)o; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindIndexBuffer(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, VkIndexType t) { (void)c;(void)b;(void)o;(void)t; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDraw(VkCommandBuffer c, uint32_t v, uint32_t i, uint32_t fv, uint32_t fi) { (void)c;(void)v;(void)i;(void)fv;(void)fi; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndexed(VkCommandBuffer c, uint32_t ic, uint32_t i, uint32_t fv, int32_t vo, uint32_t fi) { (void)c;(void)ic;(void)i;(void)fv;(void)vo;(void)fi; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, uint32_t d, uint32_t s) { (void)c;(void)b;(void)o;(void)d;(void)s; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndexedIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, uint32_t d, uint32_t s) { (void)c;(void)b;(void)o;(void)d;(void)s; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDispatch(VkCommandBuffer c, uint32_t x, uint32_t y, uint32_t z) { (void)c;(void)x;(void)y;(void)z; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyBuffer(VkCommandBuffer c, VkBuffer s, VkBuffer d, uint32_t n, const VkBufferCopy *r) { (void)c;(void)s;(void)d;(void)n;(void)r; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d, VkImageLayout dl, uint32_t n, const VkImageCopy *r) { (void)c;(void)s;(void)sl;(void)d;(void)dl;(void)n;(void)r; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBlitImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d, VkImageLayout dl, uint32_t n, const VkImageBlit *r, VkFilter f) { (void)c;(void)s;(void)sl;(void)d;(void)dl;(void)n;(void)r;(void)f; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyBufferToImage(VkCommandBuffer c, VkBuffer b, VkImage i, VkImageLayout l, uint32_t n, const VkBufferImageCopy *r) { (void)c;(void)b;(void)i;(void)l;(void)n;(void)r; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyImageToBuffer(VkCommandBuffer c, VkImage i, VkImageLayout l, VkBuffer b, uint32_t n, const VkBufferImageCopy *r) { (void)c;(void)i;(void)l;(void)b;(void)n;(void)r; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBeginRenderPass(VkCommandBuffer c, const VkRenderPassBeginInfo *i, VkSubpassContents s) { (void)c;(void)i;(void)s; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdEndRenderPass(VkCommandBuffer c) { (void)c; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdPipelineBarrier(VkCommandBuffer c, VkPipelineStageFlags s, VkPipelineStageFlags d, VkDependencyFlags f, uint32_t mb, const VkMemoryBarrier *m, uint32_t bb, const VkBufferMemoryBarrier *b, uint32_t ib, const VkImageMemoryBarrier *i) { (void)c;(void)s;(void)d;(void)f;(void)mb;(void)m;(void)bb;(void)b;(void)ib;(void)i; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearColorImage(VkCommandBuffer c, VkImage i, VkImageLayout l, const VkClearColorValue *v, uint32_t n, const VkImageSubresourceRange *r) { (void)c;(void)i;(void)l;(void)v;(void)n;(void)r; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearDepthStencilImage(VkCommandBuffer c, VkImage i, VkImageLayout l, const VkClearDepthStencilValue *v, uint32_t n, const VkImageSubresourceRange *r) { (void)c;(void)i;(void)l;(void)v;(void)n;(void)r; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearAttachments(VkCommandBuffer c, uint32_t a, const VkClearAttachment *at, uint32_t n, const VkClearRect *r) { (void)c;(void)a;(void)at;(void)n;(void)r; }

/* === Queue === */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueueSubmit(VkQueue q, uint32_t c, const VkSubmitInfo *s, VkFence f) { (void)q;(void)c;(void)s;(void)f; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueueWaitIdle(VkQueue q) { (void)q; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_DeviceWaitIdle(VkDevice d) { (void)d; return VK_SUCCESS; }

/* === Sync === */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateFence(VkDevice d, const VkFenceCreateInfo *i, const VkAllocationCallbacks *a, VkFence *f) { (void)d;(void)i;(void)a;(void)f; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyFence(VkDevice d, VkFence f, const VkAllocationCallbacks *a) { (void)d;(void)f;(void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_WaitForFences(VkDevice d, uint32_t c, const VkFence *f, VkBool32 w, uint64_t t) { (void)d;(void)c;(void)f;(void)w;(void)t; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetFences(VkDevice d, uint32_t c, const VkFence *f) { (void)d;(void)c;(void)f; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetFenceStatus(VkDevice d, VkFence f) { (void)d;(void)f; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSemaphore(VkDevice d, const VkSemaphoreCreateInfo *i, const VkAllocationCallbacks *a, VkSemaphore *s) { (void)d;(void)i;(void)a;(void)s; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySemaphore(VkDevice d, VkSemaphore s, const VkAllocationCallbacks *a) { (void)d;(void)s;(void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateEvent(VkDevice d, const VkEventCreateInfo *i, const VkAllocationCallbacks *a, VkEvent *e) { (void)d;(void)i;(void)a;(void)e; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyEvent(VkDevice d, VkEvent e, const VkAllocationCallbacks *a) { (void)d;(void)e;(void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetEventStatus(VkDevice d, VkEvent e) { (void)d;(void)e; return VK_EVENT_SET; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_SetEvent(VkDevice d, VkEvent e) { (void)d;(void)e; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetEvent(VkDevice d, VkEvent e) { (void)d;(void)e; return VK_SUCCESS; }

/* === Query === */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateQueryPool(VkDevice d, const VkQueryPoolCreateInfo *i, const VkAllocationCallbacks *a, VkQueryPool *p) { (void)d;(void)i;(void)a;(void)p; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyQueryPool(VkDevice d, VkQueryPool p, const VkAllocationCallbacks *a) { (void)d;(void)p;(void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetQueryPoolResults(VkDevice d, VkQueryPool p, uint32_t s, uint32_t c, size_t sz, void *data, VkDeviceSize st, VkQueryResultFlags f) { (void)d;(void)p;(void)s;(void)c;(void)sz;(void)data;(void)st;(void)f; return VK_NOT_READY; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdResetQueryPool(VkCommandBuffer c, VkQueryPool p, uint32_t s, uint32_t n) { (void)c;(void)p;(void)s;(void)n; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBeginQuery(VkCommandBuffer c, VkQueryPool p, uint32_t q, VkQueryControlFlags f) { (void)c;(void)p;(void)q;(void)f; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdEndQuery(VkCommandBuffer c, VkQueryPool p, uint32_t q) { (void)c;(void)p;(void)q; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdWriteTimestamp(VkCommandBuffer c, VkPipelineStageFlagBits s, VkQueryPool p, uint32_t q) { (void)c;(void)s;(void)p;(void)q; }

/* === Swapchain === */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSwapchainKHR(VkDevice d, const VkSwapchainCreateInfoKHR *i, const VkAllocationCallbacks *a, VkSwapchainKHR *s) { (void)d;(void)i;(void)a;(void)s; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySwapchainKHR(VkDevice d, VkSwapchainKHR s, const VkAllocationCallbacks *a) { (void)d;(void)s;(void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetSwapchainImagesKHR(VkDevice d, VkSwapchainKHR s, uint32_t *c, VkImage *i) { (void)d;(void)s;(void)c;(void)i; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AcquireNextImageKHR(VkDevice d, VkSwapchainKHR s, uint64_t t, VkSemaphore sem, VkFence f, uint32_t *i) { (void)d;(void)s;(void)t;(void)sem;(void)f;(void)i; return VK_ERROR_NOT_IMPLEMENTED; }
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueuePresentKHR(VkQueue q, const VkPresentInfoKHR *i) { (void)q;(void)i; return VK_SUCCESS; }

/* === Stub shader (when libpsbc is not available) === */
VkResult vk_ps4_shader_compile_stub(
    const uint32_t *spirv, size_t spirv_size,
    VkShaderStageFlagBits stage,
    void **out_binary, size_t *out_binary_size
) {
    (void)spirv; (void)spirv_size; (void)stage;
    *out_binary = NULL;
    *out_binary_size = 0;
    return VK_ERROR_NOT_IMPLEMENTED;
}
