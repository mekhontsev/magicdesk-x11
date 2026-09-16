#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_ANDROID_KHR
#include "dma_copy.h"
#include <android/log.h>
#include <dlfcn.h>
#include <linux/dma-buf.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#define DEVICE_FUNCTIONS(X)                                                                                  \
    X(DestroyDevice)                                                                                         \
    X(GetDeviceQueue) X(GetBufferMemoryRequirements) X(CreateBuffer) X(DestroyBuffer) X(AllocateMemory)      \
        X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CreateImage) X(DestroyImage)         \
            X(BindImageMemory) X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers)          \
                X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdPipelineBarrier)        \
                    X(CmdCopyBufferToImage) X(CreateFence) X(DestroyFence) X(ResetFences) X(QueueSubmit)     \
                        X(WaitForFences) X(DeviceWaitIdle) X(GetMemoryFdPropertiesKHR)                       \
                            X(GetAndroidHardwareBufferPropertiesANDROID)

struct DmaCopyContext {
    unsigned references;
    bool failed;
    void* library;
    VkInstance instance;
    VkDevice device;
    VkQueue queue;
    uint32_t queueFamily;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    VkCommandPool pool;
    VkCommandBuffer command;
    VkFence fence;
    uint64_t stagedBytes;
    PFN_vkDestroyInstance DestroyInstance;
#define DECLARE(name) PFN_vk##name name;
    DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
};

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void* mapped;
} CopyBuffer;

struct DmaCopySource {
    DmaCopyContext* context;
    CopyBuffer imported, tail;
    VkDeviceSize offset, tailSize;
    unsigned width, height, strideBytes, gpuRows;
    const void* pixels;
};

struct DmaCopyDestination {
    DmaCopyContext* context;
    VkImage image;
    VkDeviceMemory memory;
    AHardwareBuffer* buffer;
    unsigned width, height;
};

static void releaseBuffer(DmaCopyContext* c, CopyBuffer* b) {
    if (b->mapped)
        c->UnmapMemory(c->device, b->memory);
    if (b->buffer)
        c->DestroyBuffer(c->device, b->buffer, NULL);
    if (b->memory)
        c->FreeMemory(c->device, b->memory, NULL);
}

void dmaCopyRelease(DmaCopyContext* c) {
    if (!c || --c->references)
        return;
    if (c->fence)
        c->DestroyFence(c->device, c->fence, NULL);
    if (c->pool)
        c->DestroyCommandPool(c->device, c->pool, NULL);
    if (c->device && c->DestroyDevice)
        c->DestroyDevice(c->device, NULL);
    if (c->instance && c->DestroyInstance)
        c->DestroyInstance(c->instance, NULL);
    if (c->library)
        dlclose(c->library);
    free(c);
}

static bool hasExtension(const VkExtensionProperties* extensions, unsigned count, const char* name) {
    for (unsigned i = 0; i < count; i++)
        if (!strcmp(extensions[i].extensionName, name))
            return true;
    return false;
}

DmaCopyContext* dmaCopyCreate(void) {
    DmaCopyContext* c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->references = 1;
    c->library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!c->library)
        goto fail;
    PFN_vkGetInstanceProcAddr get = (PFN_vkGetInstanceProcAddr)dlsym(c->library, "vkGetInstanceProcAddr");
    if (!get)
        goto fail;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)get(NULL, "vkCreateInstance");
    if (!create)
        goto fail;
    VkApplicationInfo application = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                     .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo instanceInfo = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                         .pApplicationInfo = &application};
    if (create(&instanceInfo, NULL, &c->instance) != VK_SUCCESS)
        goto fail;
#define INSTANCE(type, variable, name)                                                                       \
    PFN_vk##type variable = (PFN_vk##type)get(c->instance, "vk" #name);                                      \
    if (!variable)                                                                                           \
    goto fail
    c->DestroyInstance = (PFN_vkDestroyInstance)get(c->instance, "vkDestroyInstance");
    if (!c->DestroyInstance)
        goto fail;
    INSTANCE(EnumeratePhysicalDevices, enumerate, EnumeratePhysicalDevices);
    INSTANCE(EnumerateDeviceExtensionProperties, enumerateExtensions, EnumerateDeviceExtensionProperties);
    INSTANCE(GetPhysicalDeviceQueueFamilyProperties, getQueues, GetPhysicalDeviceQueueFamilyProperties);
    INSTANCE(GetPhysicalDeviceExternalBufferProperties, getExternal,
             GetPhysicalDeviceExternalBufferProperties);
    INSTANCE(GetPhysicalDeviceMemoryProperties, getMemory, GetPhysicalDeviceMemoryProperties);
    INSTANCE(CreateDevice, createDevice, CreateDevice);
    INSTANCE(GetDeviceProcAddr, getDevice, GetDeviceProcAddr);
#undef INSTANCE
    uint32_t count = 0;
    if (enumerate(c->instance, &count, NULL) || !count)
        goto fail;
    VkPhysicalDevice* devices = calloc(count, sizeof(*devices));
    if (!devices)
        goto fail;
    if (enumerate(c->instance, &count, devices)) {
        free(devices);
        goto fail;
    }
    const char* required[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                              VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
                              VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
                              VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
    for (unsigned i = 0; i < count && !c->device; i++) {
        uint32_t extensionCount = 0;
        if (enumerateExtensions(devices[i], NULL, &extensionCount, NULL))
            continue;
        VkExtensionProperties* extensions = calloc(extensionCount, sizeof(*extensions));
        if (!extensions)
            continue;
        bool supported = enumerateExtensions(devices[i], NULL, &extensionCount, extensions) == VK_SUCCESS;
        for (unsigned e = 0; e < sizeof(required) / sizeof(*required); e++)
            supported &= hasExtension(extensions, extensionCount, required[e]);
        free(extensions);
        if (!supported)
            continue;
        VkPhysicalDeviceExternalBufferInfo externalInfo = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
        VkExternalBufferProperties properties = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
        getExternal(devices[i], &externalInfo, &properties);
        if (!(properties.externalMemoryProperties.externalMemoryFeatures &
              VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
            (properties.externalMemoryProperties.externalMemoryFeatures &
             VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT))
            continue;
        uint32_t queueCount = 0;
        getQueues(devices[i], &queueCount, NULL);
        VkQueueFamilyProperties* queues = calloc(queueCount, sizeof(*queues));
        if (!queues)
            continue;
        getQueues(devices[i], &queueCount, queues);
        unsigned q = 0;
        while (q < queueCount && !(queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            q++;
        free(queues);
        if (q == queueCount)
            continue;
        float priority = 1;
        VkDeviceQueueCreateInfo queueInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                             .queueFamilyIndex = q,
                                             .queueCount = 1,
                                             .pQueuePriorities = &priority};
        VkDeviceCreateInfo deviceInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                         .queueCreateInfoCount = 1,
                                         .pQueueCreateInfos = &queueInfo,
                                         .enabledExtensionCount = sizeof(required) / sizeof(*required),
                                         .ppEnabledExtensionNames = required};
        if (createDevice(devices[i], &deviceInfo, NULL, &c->device) != VK_SUCCESS)
            continue;
        c->queueFamily = q;
        getMemory(devices[i], &c->memoryProperties);
    }
    free(devices);
    if (!c->device)
        goto fail;
#define LOAD(name)                                                                                           \
    c->name = (PFN_vk##name)getDevice(c->device, "vk" #name);                                                \
    if (!c->name)                                                                                            \
        goto fail;
    DEVICE_FUNCTIONS(LOAD)
#undef LOAD
    c->GetDeviceQueue(c->device, c->queueFamily, 0, &c->queue);
    VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                        .queueFamilyIndex = c->queueFamily};
    if (c->CreateCommandPool(c->device, &poolInfo, NULL, &c->pool))
        goto fail;
    VkCommandBufferAllocateInfo commandInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                               .commandPool = c->pool,
                                               .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                               .commandBufferCount = 1};
    if (c->AllocateCommandBuffers(c->device, &commandInfo, &c->command))
        goto fail;
    VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (c->CreateFence(c->device, &fenceInfo, NULL, &c->fence))
        goto fail;
    return c;
fail:
    dmaCopyRelease(c);
    return NULL;
}

bool dmaCopyReady(DmaCopyContext* c) { return c && !c->failed; }

void dmaCopyReleaseSource(DmaCopySource* s) {
    if (!s)
        return;
    releaseBuffer(s->context, &s->tail);
    releaseBuffer(s->context, &s->imported);
    dmaCopyRelease(s->context);
    free(s);
}

static bool createTail(DmaCopySource* s) {
    DmaCopyContext* c = s->context;
    s->tailSize = (VkDeviceSize)(s->height - s->gpuRows - 1) * s->strideBytes + s->width * 4;
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .size = s->tailSize,
                               .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    if (c->CreateBuffer(c->device, &info, NULL, &s->tail.buffer))
        return false;
    VkMemoryRequirements requirements;
    c->GetBufferMemoryRequirements(c->device, s->tail.buffer, &requirements);
    unsigned type = 0;
    const uint32_t flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (; type < c->memoryProperties.memoryTypeCount; type++)
        if ((requirements.memoryTypeBits & (1u << type)) &&
            (c->memoryProperties.memoryTypes[type].propertyFlags & flags) == flags)
            break;
    if (type == c->memoryProperties.memoryTypeCount)
        return false;
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                       .allocationSize = requirements.size,
                                       .memoryTypeIndex = type};
    return c->AllocateMemory(c->device, &allocation, NULL, &s->tail.memory) == VK_SUCCESS &&
           c->BindBufferMemory(c->device, s->tail.buffer, s->tail.memory, 0) == VK_SUCCESS &&
           c->MapMemory(c->device, s->tail.memory, 0, s->tailSize, 0, &s->tail.mapped) == VK_SUCCESS;
}

DmaCopySource* dmaCopyImportSource(DmaCopyContext* c, int fd, off_t offset, int width, int height,
                                   int strideBytes, const void* pixels) {
    struct stat st;
    if (!dmaCopyReady(c) || !pixels || offset < 0 || offset % 4 || width <= 0 || height <= 0 ||
        strideBytes < (int64_t)width * 4 || strideBytes % 4 || fstat(fd, &st) || st.st_size <= 0)
        return NULL;
    uint64_t required = (uint64_t)offset + (uint64_t)(height - 1) * strideBytes + (uint64_t)width * 4;
    if (required > (uint64_t)st.st_size)
        return NULL;
    // DRI3 also permits ordinary mmap-able FDs. Never pass those to Vulkan as DMA-BUFs.
    struct dma_buf_sync sync = {.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ};
    if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync))
        return NULL;
    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
    if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync))
        return NULL;
    VkMemoryFdPropertiesKHR fdProperties = {.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
    if (c->GetMemoryFdPropertiesKHR(c->device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd,
                                    &fdProperties))
        return NULL;
    DmaCopySource* s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->context = c;
    c->references++;
    s->width = width;
    s->height = height;
    s->strideBytes = strideBytes;
    s->pixels = pixels;
    s->offset = offset;
    s->gpuRows = height;
    VkExternalMemoryBufferCreateInfo external = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .pNext = &external,
                               .size = required,
                               .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    if (c->CreateBuffer(c->device, &info, NULL, &s->imported.buffer))
        goto fail;
    VkMemoryRequirements requirements;
    c->GetBufferMemoryRequirements(c->device, s->imported.buffer, &requirements);
    if (requirements.size > (uint64_t)st.st_size) {
        // Some drivers require buffer padding not present in the producer's image allocation.
        // Import only complete rows that fit; upload the small remaining suffix separately.
        uint64_t overhead = requirements.size - required;
        if (overhead + offset + (uint64_t)width * 4 >= (uint64_t)st.st_size)
            goto fail;
        s->gpuRows = (st.st_size - overhead - offset - (uint64_t)width * 4) / strideBytes + 1;
        if (!s->gpuRows || s->gpuRows >= s->height)
            goto fail;
        c->DestroyBuffer(c->device, s->imported.buffer, NULL);
        s->imported.buffer = VK_NULL_HANDLE;
        info.size = offset + (VkDeviceSize)(s->gpuRows - 1) * strideBytes + width * 4;
        if (c->CreateBuffer(c->device, &info, NULL, &s->imported.buffer))
            goto fail;
        c->GetBufferMemoryRequirements(c->device, s->imported.buffer, &requirements);
        if (requirements.size > (uint64_t)st.st_size || !createTail(s))
            goto fail;
    }
    uint32_t mask = requirements.memoryTypeBits & fdProperties.memoryTypeBits;
    if (!mask)
        goto fail;
    int importedFd = dup(fd);
    if (importedFd < 0)
        goto fail;
    VkImportMemoryFdInfoKHR import = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
                                      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                                      .fd = importedFd};
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                       .pNext = &import,
                                       .allocationSize = st.st_size,
                                       .memoryTypeIndex = __builtin_ctz(mask)};
    if (c->AllocateMemory(c->device, &allocation, NULL, &s->imported.memory)) {
        close(importedFd);
        goto fail;
    }
    if (c->BindBufferMemory(c->device, s->imported.buffer, s->imported.memory, 0))
        goto fail;
    return s;
fail:
    dmaCopyReleaseSource(s);
    return NULL;
}

void dmaCopyReleaseDestination(DmaCopyDestination* d) {
    if (!d)
        return;
    DmaCopyContext* c = d->context;
    if (d->image)
        c->DestroyImage(c->device, d->image, NULL);
    if (d->memory)
        c->FreeMemory(c->device, d->memory, NULL);
    if (d->buffer)
        AHardwareBuffer_release(d->buffer);
    dmaCopyRelease(c);
    free(d);
}

DmaCopyDestination* dmaCopyImportDestination(DmaCopyContext* c, AHardwareBuffer* buffer) {
    if (!dmaCopyReady(c) || !buffer)
        return NULL;
    VkAndroidHardwareBufferFormatPropertiesANDROID format = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID properties = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID, .pNext = &format};
    if (c->GetAndroidHardwareBufferPropertiesANDROID(c->device, buffer, &properties) ||
        !properties.memoryTypeBits || format.format == VK_FORMAT_UNDEFINED)
        return NULL;
    AHardwareBuffer_Desc desc;
    AHardwareBuffer_describe(buffer, &desc);
    if (desc.layers != 1 || (format.format != VK_FORMAT_R8G8B8A8_UNORM &&
                            format.format != VK_FORMAT_B8G8R8A8_UNORM &&
                            format.format != VK_FORMAT_R8G8B8A8_SRGB &&
                            format.format != VK_FORMAT_B8G8R8A8_SRGB))
        return NULL;
    DmaCopyDestination* d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    d->context = c;
    c->references++;
    d->buffer = buffer;
    AHardwareBuffer_acquire(buffer);
    d->width = desc.width;
    d->height = desc.height;
    VkExternalMemoryImageCreateInfo external = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    VkImageCreateInfo imageInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                   .pNext = &external,
                                   .imageType = VK_IMAGE_TYPE_2D,
                                   .format = format.format,
                                   .extent = {desc.width, desc.height, 1},
                                   .mipLevels = 1,
                                   .arrayLayers = 1,
                                   .samples = VK_SAMPLE_COUNT_1_BIT,
                                   .tiling = VK_IMAGE_TILING_OPTIMAL,
                                   .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
    if (c->CreateImage(c->device, &imageInfo, NULL, &d->image))
        goto fail;
    VkImportAndroidHardwareBufferInfoANDROID import = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID, .buffer = buffer};
    VkMemoryDedicatedAllocateInfo dedicated = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .pNext = &import, .image = d->image};
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                       .pNext = &dedicated,
                                       .allocationSize = properties.allocationSize,
                                       .memoryTypeIndex = __builtin_ctz(properties.memoryTypeBits)};
    if (c->AllocateMemory(c->device, &allocation, NULL, &d->memory) ||
        c->BindImageMemory(c->device, d->image, d->memory, 0))
        goto fail;
    return d;
fail:
    dmaCopyReleaseDestination(d);
    return NULL;
}

static void recordCopy(DmaCopySource* s, DmaCopyDestination* d, int sx, int sy, int dx, int dy,
                       unsigned width, unsigned height, bool tail) {
    VkBufferImageCopy copy = {.bufferOffset = tail ? 0 : s->offset,
                              .bufferRowLength = s->strideBytes / 4,
                              .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                              .imageOffset = {dx, dy, 0},
                              .imageExtent = {width, height, 1}};
    copy.bufferOffset += (VkDeviceSize)(sy - (tail ? s->gpuRows : 0)) * s->strideBytes + sx * 4;
    s->context->CmdCopyBufferToImage(s->context->command, tail ? s->tail.buffer : s->imported.buffer,
                                     d->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
}

DmaCopyResult dmaCopyRect(DmaCopySource* s, DmaCopyDestination* d, int sx, int sy, int dx, int dy, int width,
                         int height) {
    if (!s || !d || s->context != d->context || !dmaCopyReady(s->context) || sx < 0 || sy < 0 || dx < 0 ||
        dy < 0 || width <= 0 || height <= 0 || (int64_t)sx + width > s->width ||
        (int64_t)sy + height > s->height || (int64_t)dx + width > d->width ||
        (int64_t)dy + height > d->height)
        return DMA_COPY_DECLINED;
    DmaCopyContext* c = s->context;
    if ((unsigned)(sy + height) > s->gpuRows) {
        memcpy(s->tail.mapped, (const char*)s->pixels + (size_t)s->gpuRows * s->strideBytes, s->tailSize);
        c->stagedBytes += s->tailSize;
    }
    if (c->ResetCommandBuffer(c->command, 0) || c->ResetFences(c->device, 1, &c->fence))
        goto fail;
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (c->BeginCommandBuffer(c->command, &begin))
        goto fail;
    VkBufferMemoryBarrier sourceBarrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                           .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                                           .srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
                                           .dstQueueFamilyIndex = c->queueFamily,
                                           .buffer = s->imported.buffer,
                                           .size = VK_WHOLE_SIZE};
    VkImageMemoryBarrier destinationBarrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                               .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                               .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                                               .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
                                               .dstQueueFamilyIndex = c->queueFamily,
                                               .image = d->image,
                                               .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    c->CmdPipelineBarrier(c->command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                          NULL, 1, &sourceBarrier, 1, &destinationBarrier);
    unsigned gpuHeight = (unsigned)sy < s->gpuRows ? s->gpuRows - sy : 0;
    if (gpuHeight > (unsigned)height)
        gpuHeight = height;
    if (gpuHeight)
        recordCopy(s, d, sx, sy, dx, dy, width, gpuHeight, false);
    if (gpuHeight < (unsigned)height)
        recordCopy(s, d, sx, sy + gpuHeight, dx, dy + gpuHeight, width, height - gpuHeight, true);
    sourceBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sourceBarrier.dstAccessMask = 0;
    sourceBarrier.srcQueueFamilyIndex = c->queueFamily;
    sourceBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
    destinationBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    destinationBarrier.dstAccessMask = 0;
    destinationBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    destinationBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    destinationBarrier.srcQueueFamilyIndex = c->queueFamily;
    destinationBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
    c->CmdPipelineBarrier(c->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                          0, NULL, 1, &sourceBarrier, 1, &destinationBarrier);
    if (c->EndCommandBuffer(c->command))
        goto fail;
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &c->command};
    if (c->QueueSubmit(c->queue, 1, &submit, c->fence))
        goto fail;
    // GPU completion fence: EXA may reuse either pixmap immediately after Copy returns.
    if (c->WaitForFences(c->device, 1, &c->fence, VK_TRUE, UINT64_MAX))
        goto fail;
    return DMA_COPY_COMPLETE;
fail:;
    VkResult idle = c->DeviceWaitIdle(c->device);
    c->failed = true;
    __android_log_print(ANDROID_LOG_WARN, "LorieNative", "DMA copy device failed; disabling Vulkan copy");
    return idle == VK_SUCCESS || idle == VK_ERROR_DEVICE_LOST ? DMA_COPY_DECLINED : DMA_COPY_UNCONFIRMED;
}

uint64_t dmaCopyTakeStagedBytes(DmaCopyContext* c) {
    if (!c)
        return 0;
    uint64_t result = c->stagedBytes;
    c->stagedBytes = 0;
    return result;
}
