#include "../include/visky/vklite2.h"
#include "vklite2_utils.h"
#include <stdlib.h>

BEGIN_INCL_NO_WARN
#include <stb_image.h>
END_INCL_NO_WARN



/*************************************************************************************************/
/*  App                                                                                          */
/*************************************************************************************************/

VklApp* vkl_app(VklBackend backend)
{
    VklApp* app = calloc(1, sizeof(VklApp));
    obj_init(&app->obj, VKL_OBJECT_TYPE_APP);
    app->backend = backend;

    // Which extensions are required? Depends on the backend.
    uint32_t required_extension_count = 0;
    const char** required_extensions = backend_extensions(backend, &required_extension_count);

    // Create the instance.
    create_instance(
        required_extension_count, required_extensions, &app->instance, &app->debug_messenger);
    // debug_messenger != 0 means validation enabled
    obj_created(&app->obj);

    // Count the number of devices.
    VK_CHECK_RESULT(vkEnumeratePhysicalDevices(app->instance, &app->gpu_count, NULL));
    log_trace("found %d GPU(s)", app->gpu_count);
    if (app->gpu_count == 0)
    {
        log_error("no compatible device found! aborting");
        exit(1);
    }

    // Discover the available GPUs.
    // ----------------------------
    {
        // Initialize the GPU(s).
        VkPhysicalDevice* physical_devices = calloc(app->gpu_count, sizeof(VkPhysicalDevice));
        VK_CHECK_RESULT(
            vkEnumeratePhysicalDevices(app->instance, &app->gpu_count, physical_devices));
        ASSERT(app->gpu_count <= VKL_MAX_GPUS);
        app->gpus = calloc(app->gpu_count, sizeof(VklGpu));
        for (uint32_t i = 0; i < app->gpu_count; i++)
        {
            obj_init(&app->gpus[i].obj, VKL_OBJECT_TYPE_GPU);
            app->gpus[i].app = app;
            app->gpus[i].idx = i;
            discover_gpu(physical_devices[i], &app->gpus[i]);
            log_debug("found device #%d: %s", app->gpus[i].idx, app->gpus[i].name);
        }

        FREE(physical_devices);
    }

    INSTANCES_INIT(VklWindow, app, windows, VKL_MAX_WINDOWS, VKL_OBJECT_TYPE_WINDOW)
    // NOTE: init canvas in canvas.c instead, as the struct is defined there and not here

    return app;
}



void vkl_app_destroy(VklApp* app)
{
    log_trace("starting destruction of app...");


    // Destroy the GPUs.
    ASSERT(app->gpus != NULL);
    for (uint32_t i = 0; i < app->gpu_count; i++)
    {
        vkl_gpu_destroy(&app->gpus[i]);
    }
    INSTANCES_DESTROY(app->gpus);


    // Destroy the windows.
    ASSERT(app->windows != NULL);
    for (uint32_t i = 0; i < app->window_count; i++)
    {
        vkl_window_destroy(&app->windows[i]);
    }
    INSTANCES_DESTROY(app->windows)


    // Destroy the windows.
    if (app->canvases != NULL)
    {
        vkl_canvases_destroy(app->canvas_count, app->canvases);
        INSTANCES_DESTROY(app->canvases)
    }


    // Destroy the debug messenger.
    if (app->debug_messenger)
    {
        destroy_debug_utils_messenger_EXT(app->instance, app->debug_messenger, NULL);
        app->debug_messenger = NULL;
    }


    // Destroy the instance.
    log_trace("destroy Vulkan instance");
    if (app->instance != 0)
    {
        vkDestroyInstance(app->instance, NULL);
        app->instance = 0;
    }


    // Free the App memory.
    FREE(app);
    log_trace("app destroyed");
}



/*************************************************************************************************/
/*  GPU                                                                                          */
/*************************************************************************************************/

VklGpu* vkl_gpu(VklApp* app, uint32_t idx)
{
    if (idx >= app->gpu_count)
    {
        log_error("GPU index %d higher than number of GPUs %d", idx, app->gpu_count);
        idx = 0;
    }
    VklGpu* gpu = &app->gpus[idx];

    INSTANCES_INIT(VklCommands, gpu, commands, VKL_MAX_COMMANDS, VKL_OBJECT_TYPE_COMMANDS)
    INSTANCES_INIT(VklBuffer, gpu, buffers, VKL_MAX_BUFFERS, VKL_OBJECT_TYPE_BUFFER)
    INSTANCES_INIT(VklImages, gpu, images, VKL_MAX_IMAGES, VKL_OBJECT_TYPE_IMAGES)
    INSTANCES_INIT(VklSampler, gpu, samplers, VKL_MAX_BINDINGS, VKL_OBJECT_TYPE_SAMPLER)
    INSTANCES_INIT(VklBindings, gpu, bindings, VKL_MAX_BINDINGS, VKL_OBJECT_TYPE_BINDINGS)
    INSTANCES_INIT(VklSemaphores, gpu, semaphores, VKL_MAX_SEMAPHORES, VKL_OBJECT_TYPE_SEMAPHORES)
    INSTANCES_INIT(VklFences, gpu, fences, VKL_MAX_FENCES, VKL_OBJECT_TYPE_FENCES)
    INSTANCES_INIT(VklCompute, gpu, computes, VKL_MAX_COMPUTES, VKL_OBJECT_TYPE_COMPUTE)
    INSTANCES_INIT(VklGraphics, gpu, graphics, VKL_MAX_GRAPHICS, VKL_OBJECT_TYPE_GRAPHICS)

    return gpu;
}



void vkl_gpu_request_features(VklGpu* gpu, VkPhysicalDeviceFeatures requested_features)
{
    gpu->requested_features = requested_features;
}



void vkl_gpu_queue(VklGpu* gpu, VklQueueType type, uint32_t idx)
{
    ASSERT(gpu != NULL);
    VklQueues* q = &gpu->queues;
    ASSERT(q != NULL);
    ASSERT(idx < VKL_MAX_QUEUES);
    q->queue_types[idx] = type;
    ASSERT(idx == q->queue_count);
    q->queue_count++;
}



void vkl_gpu_create(VklGpu* gpu, VkSurfaceKHR surface)
{
    if (gpu->queues.queue_count == 0)
    {
        log_error(
            "you must request at least one queue with vkl_gpu_queue() before creating the GPU");
        exit(1);
    }
    log_trace(
        "starting creation of GPU #%d WITH%s surface...", gpu->idx, surface != 0 ? "" : "OUT");
    create_device(gpu, surface);

    VklQueues* q = &gpu->queues;

    // Create queues and command pools.
    uint32_t qf = 0;
    uint32_t nqf = 0;
    bool cmd_pool_created[VKL_MAX_QUEUE_FAMILIES] = {0};
    for (uint32_t i = 0; i < q->queue_count; i++)
    {
        qf = q->queue_families[i];
        vkGetDeviceQueue(gpu->device, qf, q->queue_indices[i], &q->queues[i]);
        // Only create 1 command pool per used queue family.
        if (!cmd_pool_created[qf])
        {
            create_command_pool(gpu->device, qf, &q->cmd_pools[nqf++]);
            cmd_pool_created[qf] = true;
        }
    }

    create_descriptor_pool(gpu->device, &gpu->dset_pool);

    obj_created(&gpu->obj);
    log_trace("GPU #%d created", gpu->idx);
}



void vkl_gpu_queue_wait(VklGpu* gpu, uint32_t queue_idx)
{
    ASSERT(gpu != NULL);
    ASSERT(queue_idx < VKL_MAX_QUEUES);
    log_trace("waiting for queue #%d", queue_idx);
    vkQueueWaitIdle(gpu->queues.queues[queue_idx]);
}



void vkl_gpu_wait(VklGpu* gpu)
{
    ASSERT(gpu != NULL);
    log_trace("waiting for device");
    vkDeviceWaitIdle(gpu->device);
}



void vkl_gpu_destroy(VklGpu* gpu)
{
    log_trace("starting destruction of GPU #%d...", gpu->idx);
    ASSERT(gpu != NULL);
    if (gpu->obj.status < VKL_OBJECT_STATUS_CREATED)
    {

        log_trace("skip destruction of GPU as it was not properly created");
        ASSERT(gpu->device == 0);
        return;
    }
    VkDevice device = gpu->device;
    ASSERT(device != 0);

    // Destroy the command pool.
    log_trace("GPU destroy %d command pool(s)", gpu->queues.queue_family_count);
    for (uint32_t i = 0; i < gpu->queues.queue_family_count; i++)
    {
        if (gpu->queues.cmd_pools[i] != 0)
        {
            vkDestroyCommandPool(device, gpu->queues.cmd_pools[i], NULL);
            gpu->queues.cmd_pools[i] = 0;
        }
    }

    log_trace("GPU destroy %d buffers", gpu->buffers_count);
    for (uint32_t i = 0; i < gpu->buffers_count; i++)
    {
        vkl_buffer_destroy(&gpu->buffers[i]);
    }

    log_trace("GPU destroy %d sets of images", gpu->images_count);
    for (uint32_t i = 0; i < gpu->images_count; i++)
    {
        vkl_images_destroy(&gpu->images[i]);
    }

    log_trace("GPU destroy %d samplers", gpu->sampler_count);
    for (uint32_t i = 0; i < gpu->sampler_count; i++)
    {
        vkl_sampler_destroy(&gpu->samplers[i]);
    }

    log_trace("GPU destroy %d bindings", gpu->bindings_count);
    for (uint32_t i = 0; i < gpu->bindings_count; i++)
    {
        vkl_bindings_destroy(&gpu->bindings[i]);
    }

    log_trace("GPU destroy %d computes", gpu->compute_count);
    for (uint32_t i = 0; i < gpu->compute_count; i++)
    {
        vkl_compute_destroy(&gpu->computes[i]);
    }

    log_trace("GPU destroy %d graphics", gpu->graphics_count);
    for (uint32_t i = 0; i < gpu->graphics_count; i++)
    {
        vkl_graphics_destroy(&gpu->graphics[i]);
    }

    log_trace("GPU destroy %d semaphores", gpu->semaphores_count);
    for (uint32_t i = 0; i < gpu->semaphores_count; i++)
    {
        vkl_semaphores_destroy(&gpu->semaphores[i]);
    }

    log_trace("GPU destroy %d fences", gpu->fences_count);
    for (uint32_t i = 0; i < gpu->fences_count; i++)
    {
        vkl_fences_destroy(&gpu->fences[i]);
    }

    if (gpu->dset_pool != 0)
    {
        log_trace("destroy descriptor pool");
        vkDestroyDescriptorPool(gpu->device, gpu->dset_pool, NULL);
    }

    // Destroy the device.
    log_trace("destroy device");
    vkDestroyDevice(gpu->device, NULL);
    gpu->device = 0;

    INSTANCES_DESTROY(gpu->commands)
    INSTANCES_DESTROY(gpu->buffers)
    INSTANCES_DESTROY(gpu->images)
    INSTANCES_DESTROY(gpu->samplers)
    INSTANCES_DESTROY(gpu->bindings)
    INSTANCES_DESTROY(gpu->computes)
    INSTANCES_DESTROY(gpu->graphics)
    INSTANCES_DESTROY(gpu->renderpasses)

    obj_destroyed(&gpu->obj);
    log_trace("GPU #%d destroyed", gpu->idx);
}



/*************************************************************************************************/
/*  Window                                                                                       */
/*************************************************************************************************/

VklWindow* vkl_window(VklApp* app, uint32_t width, uint32_t height)
{
    INSTANCE_NEW(VklWindow, window, app->windows, app->window_count)

    ASSERT(window->obj.type == VKL_OBJECT_TYPE_WINDOW);
    ASSERT(window->obj.status == VKL_OBJECT_STATUS_INIT);
    window->app = app;

    window->width = width;
    window->height = height;

    // Create the window, depending on the backend.
    window->backend_window =
        backend_window(app->instance, app->backend, width, height, &window->surface);

    return window;
}



void vkl_window_destroy(VklWindow* window)
{
    if (window == NULL || window->obj.status == VKL_OBJECT_STATUS_DESTROYED)
    {
        log_trace("skip destruction of already-destroyed window");
        return;
    }
    backend_window_destroy(
        window->app->instance, window->app->backend, window->backend_window, window->surface);
    obj_destroyed(&window->obj);
}



/*************************************************************************************************/
/*  Swapchain                                                                                    */
/*************************************************************************************************/

VklSwapchain* vkl_swapchain(VklGpu* gpu, VklWindow* window, uint32_t min_img_count)
{
    ASSERT(gpu != NULL);
    ASSERT(window != NULL);

    VklSwapchain* swapchain = calloc(1, sizeof(VklSwapchain));

    swapchain->gpu = gpu;
    swapchain->window = window;
    swapchain->img_count = min_img_count;
    return swapchain;
}



void vkl_swapchain_create(VklSwapchain* swapchain, VkFormat format, VkPresentModeKHR present_mode)
{
    ASSERT(swapchain != NULL);
    ASSERT(swapchain->gpu != NULL);

    log_trace("starting creation of swapchain...");

    // Create swapchain
    create_swapchain(
        swapchain->gpu->device, swapchain->gpu->physical_device, swapchain->window->surface,
        swapchain->img_count, format, present_mode, &swapchain->gpu->queues,
        &swapchain->window->caps, &swapchain->swapchain);

    obj_created(&swapchain->obj);
    log_trace("swapchain created");
}



void vkl_swapchain_destroy(VklSwapchain* swapchain)
{
    log_trace("starting destruction of swapchain...");

    if (swapchain->swapchain != 0)
        vkDestroySwapchainKHR(swapchain->gpu->device, swapchain->swapchain, NULL);

    FREE(swapchain);
    log_trace("swapchain destroyed");
}



/*************************************************************************************************/
/*  Commands */
/*************************************************************************************************/

VklCommands* vkl_commands(VklGpu* gpu, uint32_t queue, uint32_t count)
{
    ASSERT(gpu != NULL);
    ASSERT(gpu->obj.status >= VKL_OBJECT_STATUS_CREATED);

    INSTANCE_NEW(VklCommands, commands, gpu->commands, gpu->commands_count)

    ASSERT(count <= VKL_MAX_COMMAND_BUFFERS_PER_SET);
    ASSERT(queue <= gpu->queues.queue_count);
    ASSERT(count > 0);
    ASSERT(gpu->queues.cmd_pools[queue] != 0);

    commands->gpu = gpu;
    commands->queue_idx = queue;
    commands->count = count;
    allocate_command_buffers(gpu->device, gpu->queues.cmd_pools[queue], count, commands->cmds);

    obj_created(&commands->obj);

    return commands;
}



void vkl_cmd_begin(VklCommands* cmds)
{
    ASSERT(cmds != NULL);
    ASSERT(cmds->count > 0);

    log_trace("begin %d command buffer(s)", cmds->count);
    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    for (uint32_t i = 0; i < cmds->count; i++)
        VK_CHECK_RESULT(vkBeginCommandBuffer(cmds->cmds[i], &begin_info));
}



void vkl_cmd_end(VklCommands* cmds)
{
    ASSERT(cmds != NULL);
    ASSERT(cmds->count > 0);

    log_trace("end %d command buffer(s)", cmds->count);
    for (uint32_t i = 0; i < cmds->count; i++)
        VK_CHECK_RESULT(vkEndCommandBuffer(cmds->cmds[i]));
}



void vkl_cmd_reset(VklCommands* cmds)
{
    ASSERT(cmds != NULL);
    ASSERT(cmds->count > 0);

    log_trace("reset %d command buffer(s)", cmds->count);
    for (uint32_t i = 0; i < cmds->count; i++)
    {
        ASSERT(cmds->cmds[i] != 0);
        VK_CHECK_RESULT(vkResetCommandBuffer(cmds->cmds[i], 0));
    }
}



void vkl_cmd_free(VklCommands* cmds)
{
    ASSERT(cmds != NULL);
    ASSERT(cmds->count > 0);
    ASSERT(cmds->gpu != NULL);
    ASSERT(cmds->gpu->device != 0);

    log_trace("free %d command buffer(s)", cmds->count);
    vkFreeCommandBuffers(
        cmds->gpu->device, cmds->gpu->queues.cmd_pools[cmds->queue_idx], //
        cmds->count, cmds->cmds);
}



void vkl_cmd_submit_sync(VklCommands* cmds, uint32_t queue_idx)
{
    log_debug("[SLOW] submit %d command buffer(s)", cmds->count);

    VklQueues* q = &cmds->gpu->queues;
    ASSERT(queue_idx < q->queue_count);
    VkQueue queue = q->queues[queue_idx];

    vkQueueWaitIdle(queue);
    VkSubmitInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.commandBufferCount = cmds->count;
    info.pCommandBuffers = cmds->cmds;
    vkQueueSubmit(queue, 1, &info, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
}



/*************************************************************************************************/
/*  Buffers                                                                                      */
/*************************************************************************************************/

VklBuffer* vkl_buffer(VklGpu* gpu)
{
    ASSERT(gpu != NULL);
    ASSERT(gpu->obj.status >= VKL_OBJECT_STATUS_CREATED);

    INSTANCE_NEW(VklBuffer, buffer, gpu->buffers, gpu->buffers_count)

    buffer->gpu = gpu;

    // Default values.
    buffer->memory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return buffer;
}



void vkl_buffer_size(VklBuffer* buffer, VkDeviceSize size, VkDeviceSize item_size)
{
    ASSERT(buffer != NULL);
    buffer->size = size;
    buffer->item_size = item_size;
}



void vkl_buffer_usage(VklBuffer* buffer, VkBufferUsageFlags usage)
{
    ASSERT(buffer != NULL);
    buffer->usage = usage;
}



void vkl_buffer_memory(VklBuffer* buffer, VkMemoryPropertyFlags memory)
{
    ASSERT(buffer != NULL);
    buffer->memory = memory;
}



void vkl_buffer_queue_access(VklBuffer* buffer, uint32_t queue)
{
    ASSERT(buffer != NULL);
    ASSERT(queue < buffer->gpu->queues.queue_count);
    buffer->queues[buffer->queue_count++] = queue;
}



void vkl_buffer_create(VklBuffer* buffer)
{
    ASSERT(buffer != NULL);
    ASSERT(buffer->gpu != NULL);
    ASSERT(buffer->gpu->device != 0);
    ASSERT(buffer->size > 0);
    ASSERT(buffer->usage != 0);
    ASSERT(buffer->memory != 0);

    log_trace("starting creation of buffer...");
    create_buffer2(
        buffer->gpu->device,                                           //
        &buffer->gpu->queues, buffer->queue_count, buffer->queues,     //
        buffer->usage, buffer->memory, buffer->gpu->memory_properties, //
        buffer->size, &buffer->buffer, &buffer->device_memory);

    obj_created(&buffer->obj);
    log_trace("buffer created");
}



VklBufferRegions
vkl_buffer_regions(VklBuffer* buffer, uint32_t count, VkDeviceSize size, VkDeviceSize* offsets)
{
    ASSERT(buffer != NULL);
    ASSERT(buffer->gpu != NULL);
    ASSERT(buffer->gpu->device != 0);
    ASSERT(buffer->obj.status >= VKL_OBJECT_STATUS_CREATED);
    ASSERT(count <= VKL_MAX_BUFFER_REGIONS_PER_SET);

    VklBufferRegions regions = {0};
    regions.buffer = buffer;
    regions.count = count;
    regions.size = size;
    if (offsets != NULL)
        memcpy(regions.offsets, offsets, count * sizeof(VkDeviceSize));

    return regions;
}



void* vkl_buffer_regions_map(VklBufferRegions* buffer_regions, uint32_t idx)
{
    ASSERT(buffer_regions != NULL);
    VklBuffer* buffer = buffer_regions->buffer;

    ASSERT(buffer != NULL);
    ASSERT(buffer->gpu != NULL);
    ASSERT(buffer->gpu->device != 0);
    ASSERT(buffer->obj.status >= VKL_OBJECT_STATUS_CREATED);
    ASSERT(idx < buffer_regions->count);

    ASSERT(
        (buffer->memory & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && //
        (buffer->memory & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

    log_trace("map buffer region #%d", idx);
    void* cdata = NULL;
    VK_CHECK_RESULT(vkMapMemory(
        buffer->gpu->device, buffer->device_memory, //
        buffer_regions->offsets[idx], buffer_regions->size, 0, &cdata));
    return cdata;
}



void vkl_buffer_regions_unmap(VklBufferRegions* buffer_regions, uint32_t idx)
{
    ASSERT(buffer_regions != NULL);
    VklBuffer* buffer = buffer_regions->buffer;
    ASSERT(buffer != NULL);

    ASSERT(buffer->gpu != NULL);
    ASSERT(buffer->gpu->device != 0);
    ASSERT(buffer->obj.status >= VKL_OBJECT_STATUS_CREATED);

    ASSERT(
        (buffer->memory & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && //
        (buffer->memory & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

    log_trace("unmap buffer region #%d", idx);
    vkUnmapMemory(buffer->gpu->device, buffer->device_memory);
}



void vkl_buffer_upload(VklBuffer* buffer, VkDeviceSize offset, VkDeviceSize size, const void* data)
{
    log_trace("uploading %d bytes to GPU buffer", size);
    VklBufferRegions br = {0};
    br.buffer = buffer;
    br.count = 1;
    br.offsets[0] = offset;
    br.size = size;
    void* mapped = vkl_buffer_regions_map(&br, 0);
    ASSERT(mapped != NULL);
    memcpy(mapped, data, size);
    vkl_buffer_regions_unmap(&br, 0);
}



void vkl_buffer_download(VklBuffer* buffer, VkDeviceSize offset, VkDeviceSize size, void* data)
{
    log_trace("downloading %d bytes from GPU buffer", size);
    VklBufferRegions br = {0};
    br.buffer = buffer;
    br.count = 1;
    br.offsets[0] = offset;
    br.size = size;
    void* mapped = vkl_buffer_regions_map(&br, 0);
    memcpy(data, mapped, size);
    vkl_buffer_regions_unmap(&br, 0);
}



void vkl_buffer_destroy(VklBuffer* buffer)
{
    ASSERT(buffer != NULL);
    if (buffer->obj.status < VKL_OBJECT_STATUS_CREATED)
    {
        log_trace("skip destruction of already-destroyed buffer");
        return;
    }
    log_trace("destroy buffer");
    vkDestroyBuffer(buffer->gpu->device, buffer->buffer, NULL);
    vkFreeMemory(buffer->gpu->device, buffer->device_memory, NULL);
    obj_destroyed(&buffer->obj);
}



/*************************************************************************************************/
/*  Images                                                                                       */
/*************************************************************************************************/

VklImages* vkl_images(VklGpu* gpu, VkImageType type, uint32_t count)
{
    ASSERT(gpu != NULL);
    ASSERT(gpu->obj.status >= VKL_OBJECT_STATUS_CREATED);

    INSTANCE_NEW(VklImages, images, gpu->images, gpu->images_count)

    images->gpu = gpu;
    images->image_type = type;
    images->count = count;

    // Default options.
    images->tiling = VK_IMAGE_TILING_OPTIMAL;
    images->memory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return images;
}



void vkl_images_format(VklImages* images, VkFormat format)
{
    ASSERT(images != NULL);
    images->format = format;
}



void vkl_images_size(VklImages* images, uint32_t width, uint32_t height, uint32_t depth)
{
    ASSERT(images != NULL);

    check_dims(images->image_type, width, height, depth);

    images->width = width;
    images->height = height;
    images->depth = depth;
}



void vkl_images_tiling(VklImages* images, VkImageTiling tiling)
{
    ASSERT(images != NULL);
    images->tiling = tiling;
}



void vkl_images_usage(VklImages* images, VkImageUsageFlags usage)
{
    ASSERT(images != NULL);
    images->usage = usage;
}



void vkl_images_memory(VklImages* images, VkMemoryPropertyFlags memory)
{
    ASSERT(images != NULL);
    images->memory = memory;
}



void vkl_images_queue_access(VklImages* images, uint32_t queue)
{
    ASSERT(images != NULL);
    ASSERT(queue < images->gpu->queues.queue_count);
    images->queues[images->queue_count++] = queue;
}



void vkl_images_create(VklImages* images)
{
    ASSERT(images != NULL);
    ASSERT(images->gpu != NULL);
    ASSERT(images->gpu->device != 0);

    check_dims(images->image_type, images->width, images->height, images->depth);
    VklGpu* gpu = images->gpu;

    log_trace("starting creation of %d images...", images->count);

    for (uint32_t i = 0; i < images->count; i++)
    {
        create_image2(
            gpu->device, &gpu->queues, images->queue_count, images->queues, images->image_type,
            images->width, images->height, images->depth, images->format, images->tiling,
            images->usage, images->memory, gpu->memory_properties, &images->images[i],
            &images->memories[i]);

        create_image_view2(
            gpu->device, images->images[i], images->image_type, images->format,
            VK_IMAGE_ASPECT_COLOR_BIT, &images->image_views[i]);
    }

    obj_created(&images->obj);
    log_trace("%d images created", images->count);
}



void vkl_images_destroy(VklImages* images)
{
    ASSERT(images != NULL);
    if (images->obj.status < VKL_OBJECT_STATUS_CREATED)
    {
        log_trace("skip destruction of already-destroyed images");
        return;
    }
    log_trace("destroy %d images", images->count);

    for (uint32_t i = 0; i < images->count; i++)
    {
        vkDestroyImageView(images->gpu->device, images->image_views[i], NULL);
        vkDestroyImage(images->gpu->device, images->images[i], NULL);
        vkFreeMemory(images->gpu->device, images->memories[i], NULL);
    }

    obj_destroyed(&images->obj);
}



/*************************************************************************************************/
/*  Sampler                                                                                      */
/*************************************************************************************************/

VklSampler* vkl_sampler(VklGpu* gpu)
{
    ASSERT(gpu != NULL);
    ASSERT(gpu->obj.status >= VKL_OBJECT_STATUS_CREATED);

    INSTANCE_NEW(VklSampler, samplers, gpu->samplers, gpu->sampler_count)

    samplers->gpu = gpu;

    return samplers;
}



void vkl_sampler_min_filter(VklSampler* sampler, VkFilter filter)
{
    ASSERT(sampler != NULL);
    sampler->min_filter = filter;
}



void vkl_sampler_mag_filter(VklSampler* sampler, VkFilter filter)
{
    ASSERT(sampler != NULL);
    sampler->mag_filter = filter;
}



void vkl_sampler_address_mode(
    VklSampler* sampler, VklTextureAxis axis, VkSamplerAddressMode address_mode)
{
    ASSERT(sampler != NULL);
    ASSERT(axis <= 2);
    sampler->address_modes[axis] = address_mode;
}



void vkl_sampler_create(VklSampler* sampler)
{
    ASSERT(sampler != NULL);
    ASSERT(sampler->gpu != NULL);
    ASSERT(sampler->gpu->device != 0);

    log_trace("starting creation of sampler...");

    create_texture_sampler2(
        sampler->gpu->device, sampler->mag_filter, sampler->min_filter, //
        sampler->address_modes, false, &sampler->sampler);

    obj_created(&sampler->obj);
    log_trace("sampler created");
}



void vkl_sampler_destroy(VklSampler* sampler)
{
    ASSERT(sampler != NULL);
    if (sampler->obj.status < VKL_OBJECT_STATUS_CREATED)
    {
        log_trace("skip destruction of already-destroyed sampler");
        return;
    }
    log_trace("destroy sampler");
    vkDestroySampler(sampler->gpu->device, sampler->sampler, NULL);
    obj_destroyed(&sampler->obj);
}



/*************************************************************************************************/
/*  Bindings                                                                                     */
/*************************************************************************************************/

VklBindings* vkl_bindings(VklGpu* gpu)
{
    ASSERT(gpu != NULL);
    ASSERT(gpu->obj.status >= VKL_OBJECT_STATUS_CREATED);

    INSTANCE_NEW(VklBindings, bindings, gpu->bindings, gpu->bindings_count)

    bindings->gpu = gpu;

    return bindings;
}



void vkl_bindings_slot(VklBindings* bindings, uint32_t idx, VkDescriptorType type)
{
    ASSERT(bindings != NULL);
    ASSERT(idx == bindings->bindings_count);
    ASSERT(idx < VKL_MAX_BINDINGS_SIZE);
    bindings->types[bindings->bindings_count++] = type;
}



void vkl_bindings_create(VklBindings* bindings, uint32_t dset_count)
{
    ASSERT(bindings != NULL);
    ASSERT(bindings->gpu != NULL);
    ASSERT(bindings->gpu->device != 0);

    log_trace("starting creation of bindings...");
    bindings->dset_count = dset_count;

    create_descriptor_set_layout(
        bindings->gpu->device, bindings->bindings_count, bindings->types, &bindings->dset_layout);

    create_pipeline_layout(
        bindings->gpu->device, &bindings->dset_layout, &bindings->pipeline_layout);

    allocate_descriptor_sets(
        bindings->gpu->device, bindings->gpu->dset_pool, bindings->dset_layout,
        bindings->dset_count, bindings->dsets);

    obj_created(&bindings->obj);
    log_trace("bindings created");
}



void vkl_bindings_buffer(VklBindings* bindings, uint32_t idx, VklBufferRegions* buffer_regions)
{
    ASSERT(bindings != NULL);
    ASSERT(buffer_regions != NULL);
    ASSERT(buffer_regions->count == 1 || buffer_regions->count == bindings->dset_count);

    bindings->buffer_regions[idx] = *buffer_regions;
    if (bindings->obj.status == VKL_OBJECT_STATUS_CREATED)
        bindings->obj.status = VKL_OBJECT_STATUS_NEED_UPDATE;
}



void vkl_bindings_texture(
    VklBindings* bindings, uint32_t idx, VklImages* images, VklSampler* sampler)
{
    ASSERT(bindings != NULL);
    ASSERT(images != NULL);
    ASSERT(sampler != NULL);
    ASSERT(images->count == 1 || images->count == bindings->dset_count);

    bindings->images[idx] = images;
    bindings->samplers[idx] = sampler;

    if (bindings->obj.status == VKL_OBJECT_STATUS_CREATED)
        bindings->obj.status = VKL_OBJECT_STATUS_NEED_UPDATE;
}



void vkl_bindings_update(VklBindings* bindings)
{
    log_trace("update bindings");
    ASSERT(bindings->dset_count <= VKL_MAX_SWAPCHAIN_IMAGES);
    for (uint32_t i = 0; i < bindings->dset_count; i++)
    {
        update_descriptor_set(
            bindings->gpu->device, bindings->bindings_count, bindings->types,
            bindings->buffer_regions, bindings->images, bindings->samplers, //
            i, bindings->dsets[i]);
    }
}



void vkl_bindings_destroy(VklBindings* bindings)
{
    ASSERT(bindings != NULL);
    ASSERT(bindings->gpu != NULL);
    if (bindings->obj.status < VKL_OBJECT_STATUS_CREATED)
    {
        log_trace("skip destruction of already-destroyed bindings");
        return;
    }
    log_trace("destroy bindings");
    VkDevice device = bindings->gpu->device;
    vkDestroyPipelineLayout(device, bindings->pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(device, bindings->dset_layout, NULL);
    obj_destroyed(&bindings->obj);
}



/*************************************************************************************************/
/*  Compute                                                                                      */
/*************************************************************************************************/

VklCompute* vkl_compute(VklGpu* gpu, const char* shader_path)
{
    ASSERT(gpu != NULL);
    ASSERT(gpu->obj.status >= VKL_OBJECT_STATUS_CREATED);

    INSTANCE_NEW(VklCompute, compute, gpu->computes, gpu->compute_count)

    compute->gpu = gpu;
    strcpy(compute->shader_path, shader_path);

    return compute;
}



void vkl_compute_bindings(VklCompute* compute, VklBindings* bindings)
{
    ASSERT(compute != NULL);
    compute->bindings = bindings;
}



void vkl_compute_create(VklCompute* compute)
{
    ASSERT(compute != NULL);
    ASSERT(compute->gpu != NULL);
    ASSERT(compute->gpu->device != 0);
    ASSERT(compute->shader_path != NULL);

    if (compute->bindings == NULL)
    {
        log_error("vkl_compute_bindings() must be called before creating the compute");
        exit(1);
    }

    log_trace("starting creation of compute...");

    compute->shader_module =
        create_shader_module_from_file(compute->gpu->device, compute->shader_path);

    create_compute_pipeline(
        compute->gpu->device, compute->shader_module, //
        compute->bindings->pipeline_layout, &compute->pipeline);

    obj_created(&compute->obj);
    log_trace("compute created");
}



void vkl_compute_destroy(VklCompute* compute)
{
    ASSERT(compute != NULL);
    ASSERT(compute->gpu != NULL);
    if (compute->obj.status < VKL_OBJECT_STATUS_CREATED)
    {
        log_trace("skip destruction of already-destroyed compute");
        return;
    }
    log_trace("destroy compute");

    VkDevice device = compute->gpu->device;
    vkDestroyShaderModule(device, compute->shader_module, NULL);
    vkDestroyPipeline(device, compute->pipeline, NULL);

    obj_destroyed(&compute->obj);
}



/*************************************************************************************************/
/*  Graphics                                                                                     */
/*************************************************************************************************/

VklGraphics* vkl_graphics(VklGpu* gpu)
{
    ASSERT(gpu != NULL);
    ASSERT(gpu->obj.status >= VKL_OBJECT_STATUS_CREATED);

    INSTANCE_NEW(VklGraphics, graphics, gpu->graphics, gpu->graphics_count)

    graphics->gpu = gpu;

    return graphics;
}


void vkl_graphics_renderpass(VklGraphics* graphics, VklRenderpass* renderpass, uint32_t subpass)
{
    ASSERT(graphics != NULL);
    graphics->renderpass = renderpass;
    graphics->subpass = subpass;
}



void vkl_graphics_topology(VklGraphics* graphics, VkPrimitiveTopology topology)
{
    ASSERT(graphics != NULL);
    graphics->topology = topology;
}



void vkl_graphics_shader(
    VklGraphics* graphics, VkShaderStageFlagBits stage, const char* shader_path)
{
    ASSERT(graphics != NULL);
    ASSERT(graphics->gpu != NULL);
    ASSERT(graphics->gpu->device != 0);
    strcpy(graphics->shader_path, shader_path);

    graphics->shader_modules[graphics->shader_count++] =
        create_shader_module_from_file(graphics->gpu->device, graphics->shader_path);
}



void vkl_graphics_vertex_binding(VklGraphics* graphics, uint32_t binding, VkDeviceSize stride)
{
    ASSERT(graphics != NULL);
    VklVertexBinding* vb = &graphics->vertex_bindings[graphics->vertex_binding_count++];
    vb->binding = binding;
    vb->stride = stride;
}



void vkl_graphics_vertex_attr(
    VklGraphics* graphics, uint32_t binding, uint32_t location, VkFormat format,
    VkDeviceSize offset)
{
    ASSERT(graphics != NULL);
    VklVertexAttr* va = &graphics->vertex_attrs[graphics->vertex_attr_count++];
    va->binding = binding;
    va->location = location;
    va->format = format;
}



void vkl_graphics_blend(VklGraphics* graphics, VklBlendType blend_type)
{
    ASSERT(graphics != NULL);
    graphics->blend_type = blend_type;
}



void vkl_graphics_depth_test(VklGraphics* graphics, VklDepthTest depth_test)
{
    ASSERT(graphics != NULL);
    graphics->depth_test = depth_test;
}



void vkl_graphics_polygon_mode(VklGraphics* graphics, VkPolygonMode polygon_mode)
{
    ASSERT(graphics != NULL);
    graphics->polygon_mode = polygon_mode;
}



void vkl_graphics_cull_mode(VklGraphics* graphics, VkCullModeFlags cull_mode)
{
    ASSERT(graphics != NULL);
    graphics->cull_mode = cull_mode;
}



void vkl_graphics_front_face(VklGraphics* graphics, VkFrontFace front_face)
{
    ASSERT(graphics != NULL);
    graphics->front_face = front_face;
}



void vkl_graphics_create(VklGraphics* graphics)
{
    ASSERT(graphics != NULL);
    log_trace("starting creation of graphics pipeline...");

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {0};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // Vertex bindings.
    VkVertexInputBindingDescription bindings_info[VKL_MAX_VERTEX_BINDINGS] = {0};
    for (uint32_t i = 0; i < graphics->vertex_binding_count; i++)
    {
        bindings_info[i].binding = graphics->vertex_bindings[i].binding;
        bindings_info[i].stride = graphics->vertex_bindings[i].stride;
        bindings_info[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    }
    vertex_input_info.vertexBindingDescriptionCount = graphics->vertex_binding_count;
    vertex_input_info.pVertexBindingDescriptions = bindings_info;

    // Vertex attributes.
    VkVertexInputAttributeDescription attrs_info[VKL_MAX_VERTEX_ATTRS] = {0};
    for (uint32_t i = 0; i < graphics->vertex_attr_count; i++)
    {
        attrs_info[i].binding = graphics->vertex_attrs[i].binding;
        attrs_info[i].location = graphics->vertex_attrs[i].location;
        attrs_info[i].format = graphics->vertex_attrs[i].format;
        attrs_info[i].offset = graphics->vertex_attrs[i].offset;
    }
    vertex_input_info.vertexAttributeDescriptionCount = graphics->vertex_attr_count;
    vertex_input_info.pVertexAttributeDescriptions = attrs_info;

    // Shaders.
    VkPipelineShaderStageCreateInfo shader_stages[VKL_MAX_SHADERS_PER_GRAPHICS] = {0};
    for (uint32_t i = 0; i < graphics->shader_count; i++)
    {
        shader_stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_stages[i].stage = graphics->shader_stages[i];
        shader_stages[i].module = graphics->shader_modules[i];
        shader_stages[i].pName = "main";
    }

    // Pipeline.
    VkPipelineInputAssemblyStateCreateInfo input_assembly =
        create_input_assembly(graphics->topology);
    VkPipelineRasterizationStateCreateInfo rasterizer = create_rasterizer();
    VkPipelineMultisampleStateCreateInfo multisampling = create_multisampling();
    VkPipelineColorBlendAttachmentState color_blend_attachment = create_color_blend_attachment();
    VkPipelineColorBlendStateCreateInfo color_blending =
        create_color_blending(&color_blend_attachment);
    VkPipelineDepthStencilStateCreateInfo depth_stencil =
        create_depth_stencil((bool)graphics->depth_test);
    VkPipelineViewportStateCreateInfo viewport_state = create_viewport_state();
    VkPipelineDynamicStateCreateInfo dynamic_state = create_dynamic_states(
        2, (VkDynamicState[]){VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR});

    // Finally create the pipeline.
    VkGraphicsPipelineCreateInfo pipelineInfo = {0};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = graphics->shader_count;
    pipelineInfo.pStages = shader_stages;
    pipelineInfo.pVertexInputState = &vertex_input_info;
    pipelineInfo.pInputAssemblyState = &input_assembly;
    pipelineInfo.pViewportState = &viewport_state;
    pipelineInfo.pDynamicState = &dynamic_state;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &color_blending;
    pipelineInfo.pDepthStencilState = &depth_stencil;
    pipelineInfo.layout = graphics->bindings->pipeline_layout;
    pipelineInfo.renderPass = graphics->renderpass->renderpass;
    pipelineInfo.subpass = graphics->subpass;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(
        graphics->gpu->device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &graphics->pipeline));
    log_trace("graphics pipeline created");
}



void vkl_graphics_bindings(VklGraphics* graphics, VklBindings* bindings)
{
    ASSERT(graphics != NULL);
    graphics->bindings = bindings;
}



void vkl_graphics_destroy(VklGraphics* graphics)
{
    ASSERT(graphics != NULL);
    ASSERT(graphics->gpu != NULL);
    if (graphics->obj.status < VKL_OBJECT_STATUS_CREATED)
    {
        log_trace("skip destruction of already-destroyed graphics");
        return;
    }
    log_trace("destroy graphics");

    VkDevice device = graphics->gpu->device;
    for (uint32_t i = 0; i < graphics->shader_count; i++)
        vkDestroyShaderModule(device, graphics->shader_modules[i], NULL);
    vkDestroyPipeline(device, graphics->pipeline, NULL);

    obj_destroyed(&graphics->obj);
}



/*************************************************************************************************/
/*  Barrier                                                                                      */
/*************************************************************************************************/

VklBarrier vkl_barrier(VklGpu* gpu)
{
    ASSERT(gpu != NULL);
    ASSERT(gpu->obj.status >= VKL_OBJECT_STATUS_CREATED);

    VklBarrier barrier = {0};
    barrier.gpu = gpu;
    return barrier;
}



void vkl_barrier_stages(
    VklBarrier* barrier, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
    ASSERT(barrier != NULL);
    barrier->src_stage = src_stage;
    barrier->dst_stage = dst_stage;
}



void vkl_barrier_buffer(VklBarrier* barrier, VklBufferRegions* buffer_regions)
{
    ASSERT(barrier != NULL);

    VklBarrierBuffer* b = &barrier->buffer_barriers[barrier->buffer_barrier_count++];

    b->buffer_regions = *buffer_regions;
}



void vkl_barrier_buffer_queue(VklBarrier* barrier, uint32_t src_queue, uint32_t dst_queue)
{
    ASSERT(barrier != NULL);

    VklBarrierBuffer* b = &barrier->buffer_barriers[barrier->buffer_barrier_count - 1];
    ASSERT(b->buffer_regions.buffer != NULL);

    b->src_queue = src_queue;
    b->dst_queue = dst_queue;
}



void vkl_barrier_buffer_access(
    VklBarrier* barrier, VkAccessFlags src_access, VkAccessFlags dst_access)
{
    ASSERT(barrier != NULL);

    VklBarrierBuffer* b = &barrier->buffer_barriers[barrier->buffer_barrier_count - 1];
    ASSERT(b->buffer_regions.buffer != NULL);

    b->src_access = src_access;
    b->dst_access = dst_access;
}



void vkl_barrier_images(VklBarrier* barrier, VklImages* images)
{
    ASSERT(barrier != NULL);

    VklBarrierImage* b = &barrier->image_barriers[barrier->image_barrier_count++];

    b->images = images;
}



void vkl_barrier_images_layout(
    VklBarrier* barrier, VkImageLayout src_layout, VkImageLayout dst_layout)
{
    ASSERT(barrier != NULL);

    VklBarrierImage* b = &barrier->image_barriers[barrier->image_barrier_count - 1];
    ASSERT(b->images != NULL);

    b->src_layout = src_layout;
    b->dst_layout = dst_layout;
}



void vkl_barrier_images_access(
    VklBarrier* barrier, VkAccessFlags src_access, VkAccessFlags dst_access)
{
    ASSERT(barrier != NULL);

    VklBarrierImage* b = &barrier->image_barriers[barrier->image_barrier_count - 1];
    ASSERT(b->images != NULL);

    b->src_access = src_access;
    b->dst_access = dst_access;
}



void vkl_barrier_images_queue(VklBarrier* barrier, uint32_t src_queue, uint32_t dst_queue)
{
    ASSERT(barrier != NULL);

    VklBarrierImage* b = &barrier->image_barriers[barrier->image_barrier_count - 1];
    ASSERT(b->images != NULL);

    b->src_queue = src_queue;
    b->dst_queue = dst_queue;
}



/*************************************************************************************************/
/*  Semaphores                                                                                   */
/*************************************************************************************************/

VklSemaphores* vkl_semaphores(VklGpu* gpu, uint32_t count)
{
    ASSERT(gpu != NULL);
    ASSERT(gpu->obj.status >= VKL_OBJECT_STATUS_CREATED);

    INSTANCE_NEW(VklSemaphores, semaphores, gpu->semaphores, gpu->semaphores_count)

    ASSERT(count > 0);
    log_trace("create set of %d semaphore(s)", count);

    semaphores->gpu = gpu;
    semaphores->count = count;

    VkSemaphoreCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < count; i++)
        VK_CHECK_RESULT(vkCreateSemaphore(gpu->device, &info, NULL, &semaphores->semaphores[i]));

    obj_created(&semaphores->obj);

    return semaphores;
}



void vkl_semaphores_destroy(VklSemaphores* semaphores)
{
    ASSERT(semaphores != NULL);
    if (semaphores->obj.status < VKL_OBJECT_STATUS_CREATED)
    {
        log_trace("skip destruction of already-destroyed semaphores");
        return;
    }

    ASSERT(semaphores->count > 0);
    log_trace("destroy set of %d semaphore(s)", semaphores->count);

    for (uint32_t i = 0; i < semaphores->count; i++)
        vkDestroySemaphore(semaphores->gpu->device, semaphores->semaphores[i], NULL);
    obj_destroyed(&semaphores->obj);
}



/*************************************************************************************************/
/*  Fences                                                                                       */
/*************************************************************************************************/

VklFences* vkl_fences(VklGpu* gpu, uint32_t count)
{
    ASSERT(gpu != NULL);
    ASSERT(gpu->obj.status >= VKL_OBJECT_STATUS_CREATED);

    INSTANCE_NEW(VklFences, fences, gpu->fences, gpu->fences_count)

    ASSERT(count > 0);
    log_trace("create set of %d fences(s)", count);

    fences->gpu = gpu;
    fences->count = count;

    VkFenceCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < count; i++)
        VK_CHECK_RESULT(vkCreateFence(gpu->device, &info, NULL, &fences->fences[i]));

    obj_created(&fences->obj);

    return fences;
}



void vkl_fences_wait(VklFences* fences, uint32_t idx)
{
    ASSERT(fences != NULL);
    vkWaitForFences(fences->gpu->device, 1, &fences->fences[idx], VK_TRUE, UINT64_MAX);
}



void vkl_fences_reset(VklFences* fences, uint32_t idx)
{
    ASSERT(fences != NULL);
    vkResetFences(fences->gpu->device, 1, &fences->fences[idx]);
}



void vkl_fences_destroy(VklFences* fences)
{
    ASSERT(fences != NULL);
    if (fences->obj.status < VKL_OBJECT_STATUS_CREATED)
    {
        log_trace("skip destruction of already-destroyed fences");
        return;
    }

    ASSERT(fences->count > 0);
    log_trace("destroy set of %d fences(s)", fences->count);

    for (uint32_t i = 0; i < fences->count; i++)
        vkDestroyFence(fences->gpu->device, fences->fences[i], NULL);
    obj_destroyed(&fences->obj);
}



/*************************************************************************************************/
/*  Renderpass                                                                                   */
/*************************************************************************************************/

VklRenderpass* vkl_renderpass(VklGpu* gpu)
{
    ASSERT(gpu != NULL);
    ASSERT(gpu->obj.status >= VKL_OBJECT_STATUS_CREATED);

    INSTANCE_NEW(VklRenderpass, renderpass, gpu->renderpasses, gpu->renderpass_count)

    renderpass->gpu = gpu;

    return renderpass;
}



void vkl_renderpass_attachment(
    VklRenderpass* renderpass, uint32_t idx, VklRenderpassAttachmentType type, VkFormat format,
    VkImageLayout ref_layout)
{
    ASSERT(renderpass != NULL);
    renderpass->attachments[idx].ref_layout = ref_layout;
    renderpass->attachments[idx].type = type;
    renderpass->attachments[idx].format = format;
    renderpass->attachment_count = MAX(renderpass->attachment_count, idx + 1);
}



void vkl_renderpass_attachment_layout(
    VklRenderpass* renderpass, uint32_t idx, VkImageLayout src_layout, VkImageLayout dst_layout)
{
    ASSERT(renderpass != NULL);
    renderpass->attachments[idx].src_layout = src_layout;
    renderpass->attachments[idx].dst_layout = dst_layout;
    renderpass->attachment_count = MAX(renderpass->attachment_count, idx + 1);
}



void vkl_renderpass_attachment_ops(
    VklRenderpass* renderpass, uint32_t idx, //
    VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op)
{
    ASSERT(renderpass != NULL);
    renderpass->attachments[idx].load_op = load_op;
    renderpass->attachments[idx].store_op = store_op;
    renderpass->attachment_count = MAX(renderpass->attachment_count, idx + 1);
}



void vkl_renderpass_subpass_attachment(
    VklRenderpass* renderpass, uint32_t subpass_idx, uint32_t attachment_idx)
{
    ASSERT(renderpass != NULL);
    renderpass->subpasses[subpass_idx]
        .attachments[renderpass->subpasses[subpass_idx].attachment_count++] = attachment_idx;
    renderpass->subpass_count = MAX(renderpass->subpass_count, subpass_idx + 1);
}



void vkl_renderpass_subpass_dependency(
    VklRenderpass* renderpass, uint32_t dependency_idx,       //
    uint32_t src_subpass, uint32_t dst_subpass,               //
    VkPipelineStageFlags src_stage, VkAccessFlags src_access, //
    VkPipelineStageFlags dst_stage, VkAccessFlags dst_access)
{
    ASSERT(renderpass != NULL);
    renderpass->dependencies[dependency_idx].src_subpass = src_subpass;
    renderpass->dependencies[dependency_idx].dst_subpass = dst_subpass;
    renderpass->dependencies[dependency_idx].src_stage = src_stage;
    renderpass->dependencies[dependency_idx].src_access = src_access;
    renderpass->dependencies[dependency_idx].dst_stage = dst_stage;
    renderpass->dependencies[dependency_idx].dst_access = dst_access;
    renderpass->dependency_count = MAX(renderpass->dependency_count, dependency_idx + 1);
}



void vkl_renderpass_create(VklRenderpass* renderpass)
{
    ASSERT(renderpass != NULL);

    log_trace("starting creation of render pass...");

    // Attachments.
    VkAttachmentDescription attachments[VKL_MAX_ATTACHMENTS_PER_RENDERPASS] = {0};
    VkAttachmentReference attachment_refs[VKL_MAX_ATTACHMENTS_PER_RENDERPASS] = {0};
    for (uint32_t i = 0; i < renderpass->attachment_count; i++)
    {
        attachments[i] = create_attachment(
            renderpass->attachments[i].format,                                           //
            renderpass->attachments[i].load_op, renderpass->attachments[i].store_op,     //
            renderpass->attachments[i].src_layout, renderpass->attachments[i].dst_layout //
        );
        attachment_refs[i] = create_attachment_ref(i, renderpass->attachments[i].ref_layout);
    }

    // Subpasses.
    VkSubpassDescription subpasses[VKL_MAX_SUBPASSES_PER_RENDERPASS] = {0};
    VkAttachmentReference attachment_refs_matrix[VKL_MAX_ATTACHMENTS_PER_RENDERPASS]
                                                [VKL_MAX_ATTACHMENTS_PER_RENDERPASS] = {0};
    uint32_t attachment = 0;
    uint32_t k = 0;
    // bool has_depth_attachment = false;
    for (uint32_t i = 0; i < renderpass->subpass_count; i++)
    {
        k = 0;
        subpasses[i].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        for (uint32_t j = 0; j < renderpass->subpasses[i].attachment_count; j++)
        {
            attachment = renderpass->subpasses[i].attachments[j];
            ASSERT(attachment < renderpass->attachment_count);
            if (renderpass->attachments[attachment].type == VKL_RENDERPASS_ATTACHMENT_DEPTH)
            {
                subpasses[i].pDepthStencilAttachment = &attachment_refs[j];
            }
            else
            {
                attachment_refs_matrix[i][k++] =
                    create_attachment_ref(i, renderpass->attachments[i].ref_layout);
            }
        }
        subpasses[i].colorAttachmentCount = k;
        subpasses[i].pColorAttachments = attachment_refs_matrix[i];
    }

    // Dependencies.
    VkSubpassDependency dependencies[VKL_MAX_DEPENDENCIES_PER_RENDERPASS] = {0};
    for (uint32_t i = 0; i < renderpass->dependency_count; i++)
    {
        dependencies[i].srcSubpass = renderpass->dependencies[i].src_subpass;
        dependencies[i].srcAccessMask = renderpass->dependencies[i].src_access;
        dependencies[i].srcStageMask = renderpass->dependencies[i].src_stage;

        dependencies[i].dstSubpass = renderpass->dependencies[i].dst_subpass;
        dependencies[i].dstAccessMask = renderpass->dependencies[i].dst_access;
        dependencies[i].dstStageMask = renderpass->dependencies[i].dst_stage;
    }

    // Create render pass.
    VkRenderPassCreateInfo render_pass_info = {0};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

    render_pass_info.attachmentCount = renderpass->attachment_count;
    render_pass_info.pAttachments = attachments;

    render_pass_info.subpassCount = renderpass->subpass_count;
    render_pass_info.pSubpasses = subpasses;

    render_pass_info.dependencyCount = renderpass->dependency_count;
    render_pass_info.pDependencies = dependencies;

    VK_CHECK_RESULT(vkCreateRenderPass(
        renderpass->gpu->device, &render_pass_info, NULL, &renderpass->renderpass));

    log_trace("render pass created");
}



void vkl_renderpass_destroy(VklRenderpass* renderpass)
{
    ASSERT(renderpass != NULL);
    if (renderpass->obj.status < VKL_OBJECT_STATUS_CREATED)
    {
        log_trace("skip destruction of already-destroyed renderpass");
        return;
    }

    log_trace("destroy renderpass");
    // TODO
    obj_destroyed(&renderpass->obj);
}



/*************************************************************************************************/
/*  Submit                                                                                       */
/*************************************************************************************************/

VklSubmit vkl_submit(VklGpu* gpu)
{
    ASSERT(gpu != NULL);
    ASSERT(gpu->obj.status >= VKL_OBJECT_STATUS_CREATED);

    // INSTANCE_NEW(VklSubmit, submit, gpu->submits, gpu->submit_count)

    VklSubmit submit = {0};
    submit.gpu = gpu;

    return submit;
}



void vkl_submit_commands(VklSubmit* submit, VklCommands* commands, int32_t idx)
{
    ASSERT(submit != NULL);
    ASSERT(commands != NULL);

    uint32_t n = submit->commands_count;
    ASSERT(n < VKL_MAX_COMMANDS_PER_SUBMIT);
    submit->commands[n] = commands;
    submit->commands_idx[n] = idx;
    submit->commands_count++;
}



void vkl_submit_wait_semaphores(
    VklSubmit* submit, VkPipelineStageFlags stage, VklSemaphores* semaphores, int32_t idx)
{
    ASSERT(submit != NULL);

    ASSERT(idx >= 0); // TODO: support idx=-1 <==> all semaphores
    ASSERT(idx < VKL_MAX_SEMAPHORES_PER_SET);
    uint32_t n = submit->wait_semaphores_count;
    ASSERT(n < VKL_MAX_SEMAPHORES_PER_SUBMIT);

    submit->wait_semaphores[n] = semaphores;
    submit->wait_stages[n] = stage;
    submit->wait_semaphores_idx[n] = idx;

    submit->wait_semaphores_count++;
}



void vkl_submit_signal_semaphores(VklSubmit* submit, VklSemaphores* semaphores, int32_t idx)
{
    ASSERT(submit != NULL);

    ASSERT(idx >= 0); // TODO: support idx=-1 <==> all semaphores
    ASSERT(idx < VKL_MAX_SEMAPHORES_PER_SET);
    uint32_t n = submit->signal_semaphores_count;
    ASSERT(n < VKL_MAX_SEMAPHORES_PER_SUBMIT);

    submit->signal_semaphores[n] = semaphores;
    submit->signal_semaphores_idx[n] = idx;

    submit->signal_semaphores_count++;
}



void vkl_submit_send(VklSubmit* submit, uint32_t queue_idx, VklFences* fence, uint32_t fence_idx)
{
    ASSERT(submit != NULL);
    log_trace("starting command buffer submission...");

    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores[VKL_MAX_SEMAPHORES_PER_SUBMIT] = {0};
    for (uint32_t i = 0; i < submit->wait_semaphores_count; i++)
    {
        wait_semaphores[i] =
            submit->wait_semaphores[i]->semaphores[submit->wait_semaphores_idx[i]];
        ASSERT(submit->wait_stages[i] != 0);
    }

    VkSemaphore signal_semaphores[VKL_MAX_SEMAPHORES_PER_SUBMIT] = {0};
    for (uint32_t i = 0; i < submit->signal_semaphores_count; i++)
    {
        signal_semaphores[i] =
            submit->signal_semaphores[i]->semaphores[submit->signal_semaphores_idx[i]];
    }

    VkCommandBuffer cmd_bufs[VKL_MAX_COMMANDS_PER_SUBMIT] = {0};
    for (uint32_t i = 0; i < submit->commands_count; i++)
    {
        cmd_bufs[i] = submit->commands[i]->cmds[submit->commands_idx[i]];
    }

    submit_info.commandBufferCount = submit->commands_count;
    submit_info.pCommandBuffers = cmd_bufs;

    submit_info.waitSemaphoreCount = submit->wait_semaphores_count;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = submit->wait_stages;

    submit_info.signalSemaphoreCount = submit->signal_semaphores_count;
    submit_info.pSignalSemaphores = signal_semaphores;

    VkFence vfence = fence == NULL ? 0 : fence->fences[fence_idx];
    uint32_t fence_count = fence == NULL ? 0 : 1;

    if (fence != NULL)
        VK_CHECK_RESULT(vkResetFences(submit->gpu->device, fence_count, &vfence));
    VK_CHECK_RESULT(vkQueueSubmit(submit->gpu->queues.queues[queue_idx], 1, &submit_info, vfence));

    log_trace("submit done");
}



/*************************************************************************************************/
/*  Command buffer filling                                                                       */
/*************************************************************************************************/

void vkl_cmd_compute(VklCommands* cmds, VklCompute* compute, uvec3 size)
{
    CMD_START
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute->pipeline);
    vkCmdBindDescriptorSets(
        cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute->bindings->pipeline_layout, 0, 1,
        compute->bindings->dsets, 0, 0);
    vkCmdDispatch(cb, size[0], size[1], size[2]);
    CMD_END
}



void vkl_cmd_barrier(VklCommands* cmds, VklBarrier* barrier)
{
    ASSERT(barrier != NULL);
    VklQueues* q = &cmds->gpu->queues;
    CMD_START

    // Buffer barriers
    VkBufferMemoryBarrier buffer_barriers[VKL_MAX_BARRIERS_PER_SET] = {0};
    VkBufferMemoryBarrier* buffer_barrier = NULL;
    VklBarrierBuffer* buffer_info = NULL;

    for (uint32_t j = 0; j < barrier->buffer_barrier_count; j++)
    {
        buffer_barrier = &buffer_barriers[j];
        buffer_info = &barrier->buffer_barriers[j];

        buffer_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        buffer_barrier->buffer = buffer_info->buffer_regions.buffer->buffer;
        buffer_barrier->size = buffer_info->buffer_regions.size;
        ASSERT(i < buffer_info->buffer_regions.count);
        buffer_barrier->offset = buffer_info->buffer_regions.offsets[i];

        buffer_barrier->srcAccessMask = buffer_info->src_access;
        buffer_barrier->srcQueueFamilyIndex = q->queue_families[buffer_info->src_queue];

        buffer_barrier->dstAccessMask = buffer_info->dst_access;
        buffer_barrier->dstQueueFamilyIndex = q->queue_families[buffer_info->dst_queue];
    }

    // Image barriers
    VkImageMemoryBarrier image_barriers[VKL_MAX_BARRIERS_PER_SET] = {0};
    VkImageMemoryBarrier* image_barrier = NULL;
    VklBarrierImage* image_info = NULL;

    for (uint32_t j = 0; j < barrier->image_barrier_count; j++)
    {
        image_barrier = &image_barriers[j];
        image_info = &barrier->image_barriers[j];

        image_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ASSERT(i < image_info->images->count);
        image_barrier->image = image_info->images->images[i];
        image_barrier->oldLayout = image_info->src_layout;
        image_barrier->newLayout = image_info->dst_layout;

        image_barrier->srcAccessMask = image_info->src_access;
        image_barrier->srcQueueFamilyIndex = q->queue_families[image_info->src_queue];

        image_barrier->dstAccessMask = image_info->dst_access;
        image_barrier->dstQueueFamilyIndex = q->queue_families[image_info->dst_queue];

        image_barrier->subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_barrier->subresourceRange.baseMipLevel = 0;
        image_barrier->subresourceRange.levelCount = 1;
        image_barrier->subresourceRange.baseArrayLayer = 0;
        image_barrier->subresourceRange.layerCount = 1;
    }

    vkCmdPipelineBarrier(
        cb, barrier->src_stage, barrier->dst_stage, 0, 0, NULL, //
        barrier->buffer_barrier_count, buffer_barriers,         //
        barrier->image_barrier_count, image_barriers);          //

    CMD_END
}



void vkl_cmd_copy_buffer_to_image(VklCommands* cmds, VklBuffer* buffer, VklImages* images)
{
    CMD_START

    VkBufferImageCopy region = {0};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset.x = 0;
    region.imageOffset.y = 0;
    region.imageOffset.z = 0;

    region.imageExtent.width = images->width;
    region.imageExtent.height = images->height;
    region.imageExtent.depth = images->depth;

    vkCmdCopyBufferToImage(
        cb, buffer->buffer, images->images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    CMD_END
}