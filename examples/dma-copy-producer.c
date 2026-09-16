#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#define CHECK(call)                                                                                          \
    do {                                                                                                     \
        VkResult r = (call);                                                                                 \
        if (r) {                                                                                             \
            fprintf(stderr, "%s: %d\n", #call, r);                                                           \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    int socket = atoi(argv[1]);
    VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &ai};
    VkInstance instance;
    CHECK(vkCreateInstance(&ii, NULL, &instance));
    uint32_t n = 1;
    VkPhysicalDevice physical;
    CHECK(vkEnumeratePhysicalDevices(instance, &n, &physical));
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    fprintf(stderr, "producer=%s\n", props.deviceName);
    VkPhysicalDeviceDriverProperties driver = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 props2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                          .pNext = &driver};
    vkGetPhysicalDeviceProperties2(physical, &props2);
    fprintf(stderr, "producer driver=%s %s\n", driver.driverName, driver.driverInfo);
    float priority = 1;
    VkDeviceQueueCreateInfo qi = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &priority};
    const char* extensions[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                                VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME};
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                             .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qi,
                             .enabledExtensionCount = 2,
                             .ppEnabledExtensionNames = extensions};
    VkDevice device;
    CHECK(vkCreateDevice(physical, &di, NULL, &device));
    VkExternalMemoryBufferCreateInfo eb = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                                           .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                             .pNext = &eb,
                             .size = 384000,
                             .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    VkBuffer buffer;
    CHECK(vkCreateBuffer(device, &bi, NULL, &buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    uint32_t type = 0;
    for (; type < mp.memoryTypeCount; type++)
        if ((req.memoryTypeBits & (1u << type)) &&
            (mp.memoryTypes[type].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            break;
    if (type == mp.memoryTypeCount)
        return 3;
    VkExportMemoryAllocateInfo ei = {.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
                                     .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkMemoryAllocateInfo mi = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                               .pNext = &ei,
                               .allocationSize = 385024,
                               .memoryTypeIndex = type};
    VkDeviceMemory memory;
    CHECK(vkAllocateMemory(device, &mi, NULL, &memory));
    CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    uint32_t* data;
    CHECK(vkMapMemory(device, memory, 0, 384000, 0, (void**)&data));
    for (unsigned i = 0; i < 96000; i++)
        data[i] = 0xff000000u | (i * 167u & 0xffffff);
    vkUnmapMemory(device, memory);
    VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &pi, NULL, &pool));
    VkCommandBufferAllocateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                      .commandPool = pool,
                                      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                      .commandBufferCount = 1};
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(device, &ci, &cmd));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(cmd, &begin));
    VkBufferMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                     .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
                                     .srcQueueFamilyIndex = 0,
                                     .dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL,
                                     .buffer = buffer,
                                     .offset = 0,
                                     .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 1,
                         &barrier, 0, NULL);
    CHECK(vkEndCommandBuffer(cmd));
    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));
    PFN_vkGetMemoryFdKHR getFd = (void*)vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR");
    VkMemoryGetFdInfoKHR fi = {.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
                               .memory = memory,
                               .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    int fd;
    CHECK(getFd(device, &fi, &fd));
    char byte = 'x', control[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec io = {&byte, 1};
    struct msghdr msg = {
        .msg_iov = &io, .msg_iovlen = 1, .msg_control = control, .msg_controllen = sizeof(control)};
    struct cmsghdr* h = CMSG_FIRSTHDR(&msg);
    h->cmsg_level = SOL_SOCKET;
    h->cmsg_type = SCM_RIGHTS;
    h->cmsg_len = CMSG_LEN(sizeof(int));
    *(int*)CMSG_DATA(h) = fd;
    if (sendmsg(socket, &msg, 0) != 1)
        return 4;
    close(fd);
    read(socket, &byte, 1);
    close(socket);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
}
