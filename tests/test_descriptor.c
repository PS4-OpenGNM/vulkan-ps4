/*
 * test_descriptor.c — Phase 2 descriptor set test.
 *
 * Tests:
 *   1. Create descriptor set layout with multiple binding types
 *   2. Create descriptor pool
 *   3. Allocate descriptor sets
 *   4. Create a UBO buffer and update descriptor sets
 *   5. Create a sampler and update descriptor sets
 *   6. Create a pipeline layout
 *   7. Verify that CmdBindDescriptorSets doesn't crash
 */

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_PS4_IMPLEMENTATION_DIRECT
#include "vk_ps4_internal.h"

int main(void) {
    VkResult vr;

    setbuf(stdout, NULL);  /* Disable buffering so we see output before crash */
    printf("=== vulkan-ps4 Phase 2 Descriptor Test ===\n\n");

    /* 1. Create instance */
    VkInstanceCreateInfo ici = {0};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    VkInstance inst;
    vr = vk_ps4_CreateInstance(&ici, NULL, &inst);
    if (vr != VK_SUCCESS) { printf("FAIL: CreateInstance (%d)\n", vr); return 1; }
    printf("  CreateInstance... OK\n");

    /* 2. Enumerate physical device */
    VkPhysicalDevice phys;
    uint32_t gpu_count = 1;
    vr = vk_ps4_EnumeratePhysicalDevices(inst, &gpu_count, &phys);
    if (vr != VK_SUCCESS) { printf("FAIL: EnumeratePhysicalDevices\n"); return 1; }
    printf("  EnumeratePhysicalDevices... OK\n");

    /* 3. Create device */
    float queue_prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {0};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = 0;
    qci.queueCount = 1;
    qci.pQueuePriorities = &queue_prio;

    VkDeviceCreateInfo dci = {0};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    VkDevice dev;
    vr = vk_ps4_CreateDevice(phys, &dci, NULL, &dev);
    if (vr != VK_SUCCESS) { printf("FAIL: CreateDevice\n"); return 1; }
    printf("  CreateDevice... OK\n");

    /* 4. Allocate memory for UBO */
    VkMemoryAllocateInfo mai = {0};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = 256;
    mai.memoryTypeIndex = 0;  /* Onion */
    VkDeviceMemory mem;
    vr = vk_ps4_AllocateMemory(dev, &mai, NULL, &mem);
    if (vr != VK_SUCCESS) { printf("FAIL: AllocateMemory\n"); return 1; }
    printf("  AllocateMemory (256 bytes)... OK\n");

    /* 5. Create UBO buffer */
    VkBufferCreateInfo bci = {0};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = 256;
    bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer ubo_buf;
    vr = vk_ps4_CreateBuffer(dev, &bci, NULL, &ubo_buf);
    if (vr != VK_SUCCESS) { printf("FAIL: CreateBuffer\n"); return 1; }
    printf("  CreateBuffer (UBO, 256 bytes)... OK\n");

    /* Bind buffer memory */
    vr = vk_ps4_BindBufferMemory(dev, ubo_buf, mem, 0);
    if (vr != VK_SUCCESS) { printf("FAIL: BindBufferMemory\n"); return 1; }
    printf("  BindBufferMemory... OK\n");

    /* 6. Create descriptor set layout with 2 bindings */
    VkDescriptorSetLayoutBinding bindings[2] = {0};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslci = {0};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 2;
    dslci.pBindings = bindings;

    VkDescriptorSetLayout dsl;
    vr = vk_ps4_CreateDescriptorSetLayout(dev, &dslci, NULL, &dsl);
    if (vr != VK_SUCCESS) { printf("FAIL: CreateDescriptorSetLayout\n"); return 1; }
    printf("  CreateDescriptorSetLayout (2 bindings)... OK\n");

    /* 7. Create descriptor pool */
    VkDescriptorPoolSize pool_sizes[2] = {0};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = 1;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo dpci = {0};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = pool_sizes;

    VkDescriptorPool dpool;
    vr = vk_ps4_CreateDescriptorPool(dev, &dpci, NULL, &dpool);
    if (vr != VK_SUCCESS) { printf("FAIL: CreateDescriptorPool\n"); return 1; }
    printf("  CreateDescriptorPool... OK\n");

    /* 8. Allocate descriptor set */
    VkDescriptorSetAllocateInfo dsai = {0};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &dsl;

    VkDescriptorSet dset;
    vr = vk_ps4_AllocateDescriptorSets(dev, &dsai, &dset);
    if (vr != VK_SUCCESS) { printf("FAIL: AllocateDescriptorSets\n"); return 1; }
    printf("  AllocateDescriptorSets... OK\n");

    /* 9. Create sampler */
    VkSamplerCreateInfo sci = {0};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.maxAnisotropy = 1.0f;
    sci.maxLod = 1.0f;

    VkSampler sampler;
    vr = vk_ps4_CreateSampler(dev, &sci, NULL, &sampler);
    if (vr != VK_SUCCESS) { printf("FAIL: CreateSampler\n"); return 1; }
    printf("  CreateSampler... OK\n");

    /* 10. Update descriptor sets */
    VkDescriptorBufferInfo ubo_info = {0};
    ubo_info.buffer = ubo_buf;
    ubo_info.offset = 0;
    ubo_info.range = 256;

    VkDescriptorImageInfo img_info = {0};
    img_info.sampler = sampler;
    img_info.imageView = VK_NULL_HANDLE;
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[2] = {0};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = dset;
    writes[0].dstBinding = 0;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &ubo_info;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = dset;
    writes[1].dstBinding = 1;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &img_info;

    vk_ps4_UpdateDescriptorSets(dev, 2, writes, 0, NULL);
    printf("  UpdateDescriptorSets... OK\n");

    /* 11. Create pipeline layout */
    VkPipelineLayoutCreateInfo plci = {0};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &dsl;

    VkPipelineLayout pipeline_layout;
    vr = vk_ps4_CreatePipelineLayout(dev, &plci, NULL, &pipeline_layout);
    if (vr != VK_SUCCESS) { printf("FAIL: CreatePipelineLayout\n"); return 1; }
    printf("  CreatePipelineLayout... OK\n");

    /* 12. Create command pool + buffer, record CmdBindDescriptorSets */
    VkCommandPoolCreateInfo cpci = {0};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = 0;
    VkCommandPool cmd_pool;
    vr = vk_ps4_CreateCommandPool(dev, &cpci, NULL, &cmd_pool);
    if (vr != VK_SUCCESS) { printf("FAIL: CreateCommandPool\n"); return 1; }

    VkCommandBufferAllocateInfo cbai = {0};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vr = vk_ps4_AllocateCommandBuffers(dev, &cbai, &cmd);
    if (vr != VK_SUCCESS) { printf("FAIL: AllocateCommandBuffers\n"); return 1; }

    VkCommandBufferBeginInfo cbbi = {0};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vr = vk_ps4_BeginCommandBuffer(cmd, &cbbi);
    if (vr != VK_SUCCESS) { printf("FAIL: BeginCommandBuffer\n"); return 1; }

    /* This should not crash even without a bound pipeline */
    vk_ps4_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
                                 0, 1, &dset, 0, NULL);
    printf("  CmdBindDescriptorSets... OK\n");

    vk_ps4_EndCommandBuffer(cmd);
    printf("  EndCommandBuffer... OK\n");

    /* 13. Cleanup */
    vk_ps4_FreeCommandBuffers(dev, cmd_pool, 1, &cmd);
    vk_ps4_DestroyCommandPool(dev, cmd_pool, NULL);
    vk_ps4_DestroyPipelineLayout(dev, pipeline_layout, NULL);
    vk_ps4_DestroySampler(dev, sampler, NULL);
    vk_ps4_FreeDescriptorSets(dev, dpool, 1, &dset);
    vk_ps4_DestroyDescriptorPool(dev, dpool, NULL);
    vk_ps4_DestroyDescriptorSetLayout(dev, dsl, NULL);
    vk_ps4_DestroyBuffer(dev, ubo_buf, NULL);
    vk_ps4_FreeMemory(dev, mem, NULL);
    vk_ps4_DestroyDevice(dev, NULL);
    vk_ps4_DestroyInstance(inst, NULL);

    printf("\n=== All Phase 2 descriptor tests passed ===\n");
    return 0;
}
