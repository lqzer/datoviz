#ifndef VKL_VKLITE2_HEADER
#define VKL_VKLITE2_HEADER

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// #include <vulkan/vulkan.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "common.h"

BEGIN_INCL_NO_WARN
#include <cglm/struct.h>
END_INCL_NO_WARN


/*
TODO later
- rename Vkl/VKL/vkl to Vkl
- move constants to constants.c
- put glfw-specific code in the same place
*/



/*************************************************************************************************/
/*  Constants                                                                                    */
/*************************************************************************************************/

#define VKL_MAX_GPUS             64
#define VKL_MAX_WINDOWS          256
#define VKL_MAX_SWAPCHAIN_IMAGES 8



/*************************************************************************************************/
/*  Type definitions                                                                             */
/*************************************************************************************************/

typedef struct VklObject VklObject;
typedef struct VklApp VklApp;
typedef struct VklQueues VklQueues;
typedef struct VklGpu VklGpu;
typedef struct VklWindow VklWindow;
typedef struct VklSwapchain VklSwapchain;
typedef struct VklCanvas VklCanvas;
typedef struct VklCommands VklCommands;
typedef struct VklBuffers VklBuffers;
typedef struct VklImages VklImages;
typedef struct VklSampler VklSampler;
typedef struct VklBindings VklBindings;
typedef struct VklCompute VklCompute;
typedef struct VklPipeline VklPipeline;
typedef struct VklBarrier VklBarrier;
typedef struct VklFences VklFences;
typedef struct VklSemaphores VklSemaphores;
typedef struct VklRenderpass VklRenderpass;
typedef struct VklSubmit VklSubmit;



/*************************************************************************************************/
/*  Enums                                                                                        */
/*************************************************************************************************/

typedef enum
{
    VKL_OBJECT_TYPE_UNDEFINED,
    VKL_OBJECT_TYPE_APP,
    VKL_OBJECT_TYPE_GPU,
    VKL_OBJECT_TYPE_WINDOW,
    VKL_OBJECT_TYPE_SWAPCHAIN,
    VKL_OBJECT_TYPE_CANVAS,
    VKL_OBJECT_TYPE_COMMANDS,
    VKL_OBJECT_TYPE_BUFFER,
    VKL_OBJECT_TYPE_IMAGE,
    VKL_OBJECT_TYPE_SAMPLER,
    VKL_OBJECT_TYPE_BINDING,
    VKL_OBJECT_TYPE_COMPUTE,
    VKL_OBJECT_TYPE_PIPELINE,
    VKL_OBJECT_TYPE_BARRIER,
    VKL_OBJECT_TYPE_SYNC_CPU,
    VKL_OBJECT_TYPE_SYNC_GPU,
    VKL_OBJECT_TYPE_RENDERPASS,
    VKL_OBJECT_TYPE_SUBMIT,
    VKL_OBJECT_TYPE_CUSTOM,
} VklObjectType;


// NOTE: the order is important, status >= CREATED means the object has been created
typedef enum
{
    VKL_OBJECT_STATUS_UNDEFINED,     // invalid state
    VKL_OBJECT_STATUS_DESTROYED,     // after destruction
    VKL_OBJECT_STATUS_INIT,          // after memory allocation
    VKL_OBJECT_STATUS_CREATED,       // after proper creation on the GPU
    VKL_OBJECT_STATUS_NEED_RECREATE, // need to be recreated
    VKL_OBJECT_STATUS_NEED_UPDATE,   // need to be updated
} VklObjectStatus;


typedef enum
{
    VKL_BACKEND_NONE,
    VKL_BACKEND_GLFW,
    VKL_BACKEND_OFFSCREEN,
} VklBackend;


typedef enum
{
    VKL_QUEUE_GRAPHICS,
    VKL_QUEUE_COMPUTE,
    VKL_QUEUE_PRESENT,
} VklQueueType;


typedef enum
{
    VKL_COMMAND_TRANSFERS,
    VKL_COMMAND_GRAPHICS,
    VKL_COMMAND_COMPUTE,
    VKL_COMMAND_GUI,
} VklCommandBufferType;



/*************************************************************************************************/
/*  Macros                                                                                       */
/*************************************************************************************************/

#define INSTANCES_INIT(s, o, p, n, t)                                                             \
    log_trace("init %d object(s) %s", n, #s);                                                     \
    o->p = calloc(n, sizeof(s));                                                                  \
    for (uint32_t i = 0; i < n; i++)                                                              \
        obj_init(&o->p[i].obj, t);

#define INSTANCE_NEW(s, o, instances, n) s* o = &instances[n++];

/*
#define INSTANCE_GET(s, o, n, instances)                                                          \
    s* o = NULL;                                                                                  \
    for (uint32_t i = 0; i < n; i++)                                                              \
        if (instances[i].obj.status <= 1)                                                         \
        {                                                                                         \
            o = &instances[i];                                                                    \
            o->obj.status = VKL_OBJECT_STATUS_INIT;                                               \
        }                                                                                         \
    if (o == NULL)                                                                                \
        log_error("maximum number of %s instances reached", #s);                                  \
    exit(1);
*/

#define INSTANCES_DESTROY(o)                                                                      \
    log_trace("destroy objects %s", #o);                                                          \
    FREE(o);                                                                                      \
    o = NULL;



/*************************************************************************************************/
/*  Common                                                                                       */
/*************************************************************************************************/

struct VklObject
{
    VklObjectType type;
    VklObjectStatus status;
};



static void obj_init(VklObject* obj, VklObjectType type)
{
    obj->type = type;
    obj->status = VKL_OBJECT_STATUS_INIT;
}

static void obj_created(VklObject* obj) { obj->status = VKL_OBJECT_STATUS_CREATED; }

static void obj_destroyed(VklObject* obj) { obj->status = VKL_OBJECT_STATUS_DESTROYED; }



/*************************************************************************************************/
/*  Structs                                                                                      */
/*************************************************************************************************/

struct VklApp
{
    VklObject obj;

    // Backend
    VklBackend backend;

    // Vulkan objects.
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;

    // GPUs.
    uint32_t gpu_count;
    VklGpu* gpus;

    // Windows.
    uint32_t window_count;
    VklWindow* windows;

    // Canvas.
    uint32_t canvas_count;
    VklCanvas* canvases;
};



struct VklQueues
{
    VklObject obj;

    uint32_t queue_count;
    int32_t indices[3]; // graphics, compute, present
    VkQueue queues[3];  // graphics+compute, compute only, transfer
};



struct VklGpu
{
    VklObject obj;
    VklApp* app;

    uint32_t idx; // GPU index within the app
    const char* name;

    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties device_properties;
    VkPhysicalDeviceFeatures device_features;
    VkPhysicalDeviceMemoryProperties memory_properties;

    VklQueues queues;

    VkPhysicalDeviceFeatures requested_features;
    VkDevice device;

    uint32_t cmd_pool_count;
    VkCommandPool cmd_pools[2]; // graphics, compute
};



struct VklWindow
{
    VklObject obj;
    VklApp* app;

    void* backend_window;
    uint32_t width, height;

    VkSurfaceKHR surface;
    VkSurfaceCapabilitiesKHR caps;
};



struct VklSwapchain
{
    VklObject obj;
    VklGpu* gpu;
    VklWindow* window;

    uint32_t img_count;
    VkSwapchainKHR swapchain;
};



struct VklCommands
{
    VklObject obj;
    VklGpu* gpu;
};



struct VklBuffers
{
    VklObject obj;
    VklGpu* gpu;
};



struct VklImages
{
    VklObject obj;
    VklGpu* gpu;
};



struct VklSampler
{
    VklObject obj;
    VklGpu* gpu;
};



struct VklBindings
{
    VklObject obj;
    VklGpu* gpu;
};



struct VklCompute
{
    VklObject obj;
    VklGpu* gpu;
};



struct VklPipeline
{
    VklObject obj;
    VklGpu* gpu;
};



struct VklBarrier
{
    VklObject obj;
    VklGpu* gpu;
};



struct VklFences
{
    VklObject obj;
    VklGpu* gpu;
};



struct VklSemaphores
{
    VklObject obj;
    VklGpu* gpu;
};



struct VklRenderpass
{
    VklObject obj;
    VklGpu* gpu;

    // TODO: framebuffers
};



struct VklSubmit
{
    VklObject obj;
    VklGpu* gpu;
};



/*************************************************************************************************/
/*  App                                                                                          */
/*************************************************************************************************/

/**
 * Create an application instance.
 *
 * There is typically only one App object in a given application. This object holds a pointer to
 * the Vulkan instance and is responsible for discovering the available GPUs.
 *
 * @param backend the backend, typically either VKL_BACKEND_GLFW or VKL_BACKEND_OFFSCREEN
 * @returns a pointer to the created VklApp struct
 */
VKY_EXPORT VklApp* vkl_app(VklBackend backend);



/**
 * Destroy the application.
 *
 * This function automatically destroys all objects created within the application.
 *
 * @param app the application to destroy
 */
VKY_EXPORT void vkl_app_destroy(VklApp* app);



/*************************************************************************************************/
/*  GPU                                                                                          */
/*************************************************************************************************/

VKY_EXPORT VklGpu* vkl_gpu(VklApp* app, uint32_t idx);

VKY_EXPORT void vkl_gpu_request_features(VklGpu* gpu, VkPhysicalDeviceFeatures requested_features);

VKY_EXPORT void vkl_gpu_create(VklGpu* gpu, VkSurfaceKHR surface);

VKY_EXPORT void vkl_gpu_destroy(VklGpu* gpu);



/*************************************************************************************************/
/*  Window                                                                                       */
/*************************************************************************************************/

VKY_EXPORT VklWindow* vkl_window(VklApp* app, uint32_t width, uint32_t height);

// NOTE: to be called AFTER vkl_swapchain_destroy()
VKY_EXPORT void vkl_window_destroy(VklWindow* window);

VKY_EXPORT void vkl_canvas_destroy(VklCanvas* canvas);

VKY_EXPORT void vkl_canvases_destroy(uint32_t canvas_count, VklCanvas* canvases);



/*************************************************************************************************/
/*  Canvas                                                                                       */
/*************************************************************************************************/

VKY_EXPORT VklSwapchain* vkl_swapchain(VklGpu* gpu, VklWindow* window, uint32_t min_img_count);

VKY_EXPORT void
vkl_swapchain_create(VklSwapchain* swapchain, VkFormat format, VkPresentModeKHR present_mode);

// NOTE: to be called BEFORE vkl_window_destroy()
VKY_EXPORT void vkl_swapchain_destroy(VklSwapchain* swapchain);



/*************************************************************************************************/
/*  Commands                                                                                     */
/*************************************************************************************************/

VKY_EXPORT VklCommands* vkl_commands(VklGpu* gpu, VklQueueType queue, uint32_t count);

VKY_EXPORT void vkl_cmd_begin(VklCommands* cmds);

VKY_EXPORT void vkl_cmd_end(VklCommands* cmds);

VKY_EXPORT void vkl_cmd_reset(VklCommands* cmds);

VKY_EXPORT void vkl_cmd_free(VklCommands* cmds);



/*************************************************************************************************/
/*  Buffer                                                                                       */
/*************************************************************************************************/



/*************************************************************************************************/
/*  Image                                                                                        */
/*************************************************************************************************/



/*************************************************************************************************/
/*  Sampler                                                                                      */
/*************************************************************************************************/



/*************************************************************************************************/
/*  Binding                                                                                      */
/*************************************************************************************************/



/*************************************************************************************************/
/*  Compute                                                                                      */
/*************************************************************************************************/



/*************************************************************************************************/
/*  Pipeline                                                                                     */
/*************************************************************************************************/



/*************************************************************************************************/
/*  Barrier                                                                                      */
/*************************************************************************************************/



/*************************************************************************************************/
/*  Sync                                                                                         */
/*************************************************************************************************/



/*************************************************************************************************/
/*  Renderpass                                                                                   */
/*************************************************************************************************/



/*************************************************************************************************/
/*  Submit                                                                                       */
/*************************************************************************************************/



/*************************************************************************************************/
/*  Canvas                                                                                       */
/*************************************************************************************************/



#endif
