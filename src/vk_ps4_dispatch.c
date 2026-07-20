/*
 * vk_ps4_dispatch.c — Vulkan function dispatch table.
 *
 * Maps Vulkan function names to implementation pointers.
 * vkGetInstanceProcAddr and vkGetDeviceProcAddr look up names here.
 *
 * Functions not yet implemented return VK_ERROR_NOT_IMPLEMENTED or are
 * stubbed. As phases progress, stubs are replaced with real implementations.
 */

#include "vk_ps4.h"
#include "vk_ps4_internal.h"

#include <string.h>

/* === Forward declarations of all implemented functions === */

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

/* Swapchain (WSI) */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSwapchainKHR(VkDevice, const VkSwapchainCreateInfoKHR *, const VkAllocationCallbacks *, VkSwapchainKHR *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySwapchainKHR(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetSwapchainImagesKHR(VkDevice, VkSwapchainKHR, uint32_t *, VkImage *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AcquireNextImageKHR(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueuePresentKHR(VkQueue, const VkPresentInfoKHR *);

/* === Not-yet-implemented stubs === */
#define STUB0(name, rettype, retval) \
    VKAPI_ATTR rettype VKAPI_CALL name(void) { return retval; }
#define STUB_NOT_IMPL(name, rettype, retval) \
    VKAPI_ATTR rettype VKAPI_CALL name(void) { return retval; }

/* Functions we haven't implemented yet — return VK_ERROR_NOT_IMPLEMENTED */
STUB_NOT_IMPL(vk_ps4_EnumerateInstanceExtensionProperties, VkResult, VK_ERROR_NOT_IMPLEMENTED)
STUB_NOT_IMPL(vk_ps4_EnumerateInstanceLayerProperties, VkResult, VK_ERROR_NOT_IMPLEMENTED)
STUB_NOT_IMPL(vk_ps4_EnumerateDeviceExtensionProperties, VkResult, VK_ERROR_NOT_IMPLEMENTED)
STUB_NOT_IMPL(vk_ps4_EnumerateDeviceLayerProperties, VkResult, VK_ERROR_NOT_IMPLEMENTED)
STUB_NOT_IMPL(vk_ps4_GetPhysicalDeviceSparseImageFormatProperties, void, )
STUB_NOT_IMPL(vk_ps4_GetPhysicalDeviceImageFormatProperties, VkResult, VK_ERROR_NOT_IMPLEMENTED)
STUB_NOT_IMPL(vk_ps4_GetBufferMemoryRequirements2, void, )
STUB_NOT_IMPL(vk_ps4_GetImageMemoryRequirements2, void, )
STUB_NOT_IMPL(vk_ps4_CreateBufferView, VkResult, VK_ERROR_NOT_IMPLEMENTED)
STUB_NOT_IMPL(vk_ps4_DestroyBufferView, void, )
STUB_NOT_IMPL(vk_ps4_CreatePipelineCache, VkResult, VK_ERROR_NOT_IMPLEMENTED)
STUB_NOT_IMPL(vk_ps4_DestroyPipelineCache, void, )
STUB_NOT_IMPL(vk_ps4_GetPipelineCacheData, VkResult, VK_ERROR_NOT_IMPLEMENTED)
STUB_NOT_IMPL(vk_ps4_MergePipelineCaches, VkResult, VK_ERROR_NOT_IMPLEMENTED)
STUB_NOT_IMPL(vk_ps4_GetRenderAreaGranularity, void, )
STUB_NOT_IMPL(vk_ps4_CmdSetLineWidth, void, )
STUB_NOT_IMPL(vk_ps4_CmdSetDepthBias, void, )
STUB_NOT_IMPL(vk_ps4_CmdSetBlendConstants, void, )
STUB_NOT_IMPL(vk_ps4_CmdSetDepthBounds, void, )
STUB_NOT_IMPL(vk_ps4_CmdSetStencilCompareMask, void, )
STUB_NOT_IMPL(vk_ps4_CmdSetStencilWriteMask, void, )
STUB_NOT_IMPL(vk_ps4_CmdSetStencilReference, void, )
STUB_NOT_IMPL(vk_ps4_CmdExecuteCommands, void, )
STUB_NOT_IMPL(vk_ps4_CmdNextSubpass, void, )
STUB_NOT_IMPL(vk_ps4_CmdCopyQueryPoolResults, void, )
STUB_NOT_IMPL(vk_ps4_CmdBeginRenderPass2, void, )
STUB_NOT_IMPL(vk_ps4_CmdEndRenderPass2, void, )
STUB_NOT_IMPL(vk_ps4_CmdDispatchIndirect, void, )
STUB_NOT_IMPL(vk_ps4_CmdFillBuffer, void, )
STUB_NOT_IMPL(vk_ps4_CmdUpdateBuffer, void, )
STUB_NOT_IMPL(vk_ps4_CmdResolveImage, void, )
STUB_NOT_IMPL(vk_ps4_CmdWaitEvents, void, )
STUB_NOT_IMPL(vk_ps4_CmdResetEvent, void, )
STUB_NOT_IMPL(vk_ps4_CmdSetEvent, void, )
STUB_NOT_IMPL(vk_ps4_GetDeviceMemoryCommitment, void, )
STUB_NOT_IMPL(vk_ps4_GetImageSparseMemoryRequirements, void, )
STUB_NOT_IMPL(vk_ps4_GetImageSparseMemoryRequirements2, void, )
STUB_NOT_IMPL(vk_ps4_GetDeviceProcAddr, PFN_vkVoidFunction, NULL)
STUB_NOT_IMPL(vk_ps4_QueueBindSparse, VkResult, VK_ERROR_NOT_IMPLEMENTED)
STUB_NOT_IMPL(vk_ps4_GetPhysicalDeviceQueueFamilyProperties2, void, )

/* === Name → function lookup table === */
typedef struct {
    const char *name;
    PFN_vkVoidFunction func;
} VkPs4NameEntry;

#define ENTRY(name) { #name, (PFN_vkVoidFunction)name }

static const VkPs4NameEntry g_instance_funcs[] = {
    ENTRY(vkCreateInstance),
    ENTRY(vkDestroyInstance),
    ENTRY(vkEnumeratePhysicalDevices),
    ENTRY(vkGetPhysicalDeviceProperties),
    ENTRY(vkGetPhysicalDeviceMemoryProperties),
    ENTRY(vkGetPhysicalDeviceQueueFamilyProperties),
    ENTRY(vkGetPhysicalDeviceFeatures),
    ENTRY(vkGetPhysicalDeviceFormatProperties),
    ENTRY(vkGetInstanceProcAddr),
    ENTRY(vkEnumerateInstanceExtensionProperties),
    ENTRY(vkEnumerateInstanceLayerProperties),
    ENTRY(vkEnumerateDeviceExtensionProperties),
    ENTRY(vkEnumerateDeviceLayerProperties),
    ENTRY(vkGetPhysicalDeviceSparseImageFormatProperties),
    ENTRY(vkGetPhysicalDeviceImageFormatProperties),
    ENTRY(vkCreateDevice),
};

static const VkPs4NameEntry g_device_funcs[] = {
    ENTRY(vkDestroyDevice),
    ENTRY(vkGetDeviceQueue),
    ENTRY(vkGetDeviceProcAddr),
    ENTRY(vkAllocateMemory),
    ENTRY(vkFreeMemory),
    ENTRY(vkMapMemory),
    ENTRY(vkUnmapMemory),
    ENTRY(vkFlushMappedMemoryRanges),
    ENTRY(vkInvalidateMappedMemoryRanges),
    ENTRY(vkCreateBuffer),
    ENTRY(vkDestroyBuffer),
    ENTRY(vkGetBufferMemoryRequirements),
    ENTRY(vkBindBufferMemory),
    ENTRY(vkCreateImage),
    ENTRY(vkDestroyImage),
    ENTRY(vkGetImageMemoryRequirements),
    ENTRY(vkBindImageMemory),
    ENTRY(vkCreateImageView),
    ENTRY(vkDestroyImageView),
    ENTRY(vkCreateRenderPass),
    ENTRY(vkDestroyRenderPass),
    ENTRY(vkCreateFramebuffer),
    ENTRY(vkDestroyFramebuffer),
    ENTRY(vkCreateShaderModule),
    ENTRY(vkDestroyShaderModule),
    ENTRY(vkCreatePipelineLayout),
    ENTRY(vkDestroyPipelineLayout),
    ENTRY(vkCreateGraphicsPipelines),
    ENTRY(vkCreateComputePipelines),
    ENTRY(vkDestroyPipeline),
    ENTRY(vkCreateDescriptorSetLayout),
    ENTRY(vkDestroyDescriptorSetLayout),
    ENTRY(vkCreateDescriptorPool),
    ENTRY(vkDestroyDescriptorPool),
    ENTRY(vkAllocateDescriptorSets),
    ENTRY(vkFreeDescriptorSets),
    ENTRY(vkUpdateDescriptorSets),
    ENTRY(vkCreateCommandPool),
    ENTRY(vkDestroyCommandPool),
    ENTRY(vkAllocateCommandBuffers),
    ENTRY(vkFreeCommandBuffers),
    ENTRY(vkBeginCommandBuffer),
    ENTRY(vkEndCommandBuffer),
    ENTRY(vkResetCommandBuffer),
    ENTRY(vkQueueSubmit),
    ENTRY(vkQueueWaitIdle),
    ENTRY(vkDeviceWaitIdle),
    ENTRY(vkCreateFence),
    ENTRY(vkDestroyFence),
    ENTRY(vkWaitForFences),
    ENTRY(vkResetFences),
    ENTRY(vkGetFenceStatus),
    ENTRY(vkCreateSemaphore),
    ENTRY(vkDestroySemaphore),
    ENTRY(vkCreateEvent),
    ENTRY(vkDestroyEvent),
    ENTRY(vkGetEventStatus),
    ENTRY(vkSetEvent),
    ENTRY(vkResetEvent),
    ENTRY(vkCreateQueryPool),
    ENTRY(vkDestroyQueryPool),
    ENTRY(vkGetQueryPoolResults),
    ENTRY(vkCreateSwapchainKHR),
    ENTRY(vkDestroySwapchainKHR),
    ENTRY(vkGetSwapchainImagesKHR),
    ENTRY(vkAcquireNextImageKHR),
    ENTRY(vkQueuePresentKHR),
    ENTRY(vkCreateBufferView),
    ENTRY(vkDestroyBufferView),
    ENTRY(vkCreatePipelineCache),
    ENTRY(vkDestroyPipelineCache),
    ENTRY(vkGetPipelineCacheData),
    ENTRY(vkMergePipelineCaches),
    ENTRY(vkGetRenderAreaGranularity),
    ENTRY(vkGetDeviceMemoryCommitment),
    ENTRY(vkGetImageSparseMemoryRequirements),
    ENTRY(vkQueueBindSparse),
};

static const VkPs4NameEntry g_cmd_funcs[] = {
    ENTRY(vkCmdBindPipeline),
    ENTRY(vkCmdSetViewport),
    ENTRY(vkCmdSetScissor),
    ENTRY(vkCmdBindDescriptorSets),
    ENTRY(vkCmdBindVertexBuffers),
    ENTRY(vkCmdBindIndexBuffer),
    ENTRY(vkCmdDraw),
    ENTRY(vkCmdDrawIndexed),
    ENTRY(vkCmdDrawIndirect),
    ENTRY(vkCmdDrawIndexedIndirect),
    ENTRY(vkCmdDispatch),
    ENTRY(vkCmdCopyBuffer),
    ENTRY(vkCmdCopyImage),
    ENTRY(vkCmdBlitImage),
    ENTRY(vkCmdCopyBufferToImage),
    ENTRY(vkCmdCopyImageToBuffer),
    ENTRY(vkCmdBeginRenderPass),
    ENTRY(vkCmdEndRenderPass),
    ENTRY(vkCmdPipelineBarrier),
    ENTRY(vkCmdClearColorImage),
    ENTRY(vkCmdClearDepthStencilImage),
    ENTRY(vkCmdClearAttachments),
    ENTRY(vkCmdResetQueryPool),
    ENTRY(vkCmdBeginQuery),
    ENTRY(vkCmdEndQuery),
    ENTRY(vkCmdWriteTimestamp),
    ENTRY(vkCmdSetLineWidth),
    ENTRY(vkCmdSetDepthBias),
    ENTRY(vkCmdSetBlendConstants),
    ENTRY(vkCmdSetDepthBounds),
    ENTRY(vkCmdSetStencilCompareMask),
    ENTRY(vkCmdSetStencilWriteMask),
    ENTRY(vkCmdSetStencilReference),
    ENTRY(vkCmdExecuteCommands),
    ENTRY(vkCmdNextSubpass),
    ENTRY(vkCmdCopyQueryPoolResults),
    ENTRY(vkCmdDispatchIndirect),
    ENTRY(vkCmdFillBuffer),
    ENTRY(vkCmdUpdateBuffer),
    ENTRY(vkCmdResolveImage),
    ENTRY(vkCmdWaitEvents),
    ENTRY(vkCmdResetEvent),
    ENTRY(vkCmdSetEvent),
};

/* === vkGetInstanceProcAddr === */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName) {
    (void)instance;

    /* Check instance-level functions */
    for (size_t i = 0; i < sizeof(g_instance_funcs) / sizeof(g_instance_funcs[0]); i++) {
        if (strcmp(g_instance_funcs[i].name, pName) == 0) {
            return g_instance_funcs[i].func;
        }
    }
    /* Device-level functions are also retrievable via getInstanceProcAddr */
    for (size_t i = 0; i < sizeof(g_device_funcs) / sizeof(g_device_funcs[0]); i++) {
        if (strcmp(g_device_funcs[i].name, pName) == 0) {
            return g_device_funcs[i].func;
        }
    }
    /* Command buffer functions */
    for (size_t i = 0; i < sizeof(g_cmd_funcs) / sizeof(g_cmd_funcs[0]); i++) {
        if (strcmp(g_cmd_funcs[i].name, pName) == 0) {
            return g_cmd_funcs[i].func;
        }
    }
    return NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName) {
    return vkGetInstanceProcAddr(instance, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName) {
    return vkGetInstanceProcAddr(instance, pName);
}

/* === vkGetDeviceProcAddr === */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char *pName) {
    (void)device;

    for (size_t i = 0; i < sizeof(g_device_funcs) / sizeof(g_device_funcs[0]); i++) {
        if (strcmp(g_device_funcs[i].name, pName) == 0) {
            return g_device_funcs[i].func;
        }
    }
    for (size_t i = 0; i < sizeof(g_cmd_funcs) / sizeof(g_cmd_funcs[0]); i++) {
        if (strcmp(g_cmd_funcs[i].name, pName) == 0) {
            return g_cmd_funcs[i].func;
        }
    }
    return NULL;
}
