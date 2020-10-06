#include <math.h>

#include "../include/visky/visky.h"
#include "../src/axes.h"

typedef struct VkyTexturedVertex3D VkyTexturedVertex3D;
struct VkyTexturedVertex3D
{
    vec3 pos;
    vec3 coords;
};

#define XMIN -0.005738
#define XMAX -0.005636
#define YMIN -0.007775
#define YMAX +0.005400
#define ZMIN -0.007643
#define ZMAX +0.000332

#define ATLAS_25_SHAPE 320, 456, 528
#define ATLAS_10_SHAPE 800, 1140, 1320

#define LINE_COLOR                                                                                \
    {                                                                                             \
        240, 10, 0, 255                                                                           \
    }
#define LINE_WIDTH 4


static VkyVisual* top_lines = NULL;
static VkyVisual* v1 = NULL;



static VkyVisual* _add_image(VkyPanel* panel, int width, int height, void* image)
{
    VkyTextureParams params = vky_default_texture_params((int[]){(int)height, (int)width, 1});
    VkyVisual* visual = vky_visual(panel->scene, VKY_VISUAL_IMAGE, &params, NULL);
    vky_add_visual_to_panel(visual, panel, VKY_VIEWPORT_INNER, VKY_VISUAL_PRIORITY_NONE);

    VkyImageData data[1] = {{
        {-1, -1, 0},
        {+1, +1, 0},
        {0, 1},
        {1, 0},
    }};
    vky_visual_upload(visual, (VkyData){1, data});
    vky_visual_image_upload(visual, image);
    return visual;
}



static void _set_top_lines(float x, float y)
{
    VkySegmentVertex data[2] = {

        {{x, -1, 0},
         {x, +1, 0},
         {0, 0, 0, 0},
         LINE_COLOR,
         LINE_WIDTH,
         VKY_CAP_ROUND,
         VKY_CAP_ROUND,
         VKY_TRANSFORM_MODE_NORMAL},

        {{-1, y, 0},
         {+1, y, 0},
         {0, 0, 0, 0},
         LINE_COLOR,
         LINE_WIDTH,
         VKY_CAP_ROUND,
         VKY_CAP_ROUND,
         VKY_TRANSFORM_MODE_NORMAL},

    };
    vky_visual_upload(top_lines, (VkyData){2, data});
}



static void _update_v1(float u)
{
    double a = 1;
    VkyTexturedVertex3D vertices[] = {

        {{-a, -a, 0}, {1, u, 1}}, //
        {{-a, +a, 0}, {0, u, 1}}, //
        {{+a, -a, 0}, {1, u, 0}}, //
        {{+a, -a, 0}, {1, u, 0}}, //
        {{-a, +a, 0}, {0, u, 1}}, //
        {{+a, +a, 0}, {0, u, 0}}, //

    };
    vky_visual_upload(v1, (VkyData){0, NULL, 6, vertices, 0, NULL});
}



static void frame_callback(VkyCanvas* canvas)
{
    VkyMouse* mouse = canvas->event_controller->mouse;
    if (mouse->button != VKY_MOUSE_BUTTON_NONE)
    {
        if (vky_panel_from_mouse(canvas->scene, mouse->cur_pos)->col != 0)
            return;
        VkyPick pick = vky_pick(canvas->scene, mouse->cur_pos);
        log_debug("pick %f %f", pick.pos_data[0], pick.pos_data[1]);
        _set_top_lines(pick.pos_gpu[0], pick.pos_gpu[1]);
        _update_v1(.5 * (1 + pick.pos_gpu[0]));
    }
}



int main()
{
    log_set_level_env();

    VkyApp* app = vky_create_app(VKY_DEFAULT_BACKEND);
    VkyCanvas* canvas = vky_create_canvas(app, VKY_DEFAULT_WIDTH, VKY_DEFAULT_HEIGHT);
    VkyScene* scene = vky_create_scene(canvas, VKY_CLEAR_COLOR_BLACK, 1, 3);


    // Top panel.
    VkyPanel* panel = vky_get_panel(scene, 0, 0);


    // Set the panel controller.
    VkyAxes2DParams axparams = vky_default_axes_2D_params();

    axparams.xscale.vmin = XMIN;
    axparams.xscale.vmax = XMAX;
    axparams.yscale.vmin = YMIN;
    axparams.yscale.vmax = YMAX;

    axparams.enable_panzoom = false;
    vky_set_controller(panel, VKY_CONTROLLER_AXES_2D, &axparams);
    vky_axes_toggle_tick(panel->controller, VKY_AXES_TICK_GRID);
    vky_axes_toggle_tick(panel->controller, VKY_AXES_TICK_USER_0);


    // Top image.
    const int top_width = 528;
    const int top_height = 456;
    char path[1024];
    snprintf(path, sizeof(path), "%s/volume/%s", DATA_DIR, "atlas_25_top.img");
    char* pixels = read_file(path, NULL);
    _add_image(panel, top_width, top_height, pixels);
    free(pixels);


    // Slice position.
    top_lines = vky_visual(scene, VKY_VISUAL_SEGMENT, NULL, NULL);
    vky_add_visual_to_panel(top_lines, panel, VKY_VIEWPORT_INNER, VKY_VISUAL_PRIORITY_NONE);
    _set_top_lines(0, 0);



    // 3D volume
    {
        panel = vky_get_panel(scene, 0, 1);

        axparams = vky_default_axes_2D_params();

        axparams.xscale.vmin = YMIN;
        axparams.xscale.vmax = YMAX;
        axparams.yscale.vmin = ZMIN;
        axparams.yscale.vmax = ZMAX;

        vky_set_controller(panel, VKY_CONTROLLER_AXES_2D, &axparams);

        vky_axes_toggle_tick(panel->controller, VKY_AXES_TICK_GRID);
        vky_axes_toggle_tick(panel->controller, VKY_AXES_TICK_USER_0);

        // Create the visual.
        VkyVisual* visual = vky_create_visual(scene, VKY_VISUAL_UNDEFINED);
        v1 = visual;
        vky_add_visual_to_panel(visual, panel, VKY_VIEWPORT_INNER, VKY_VISUAL_PRIORITY_NONE);

        // Shaders.
        VkyShaders shaders = vky_create_shaders(canvas->gpu);
        vky_add_shader(&shaders, VK_SHADER_STAGE_VERTEX_BIT, "volume_slice.vert.spv");
        vky_add_shader(&shaders, VK_SHADER_STAGE_FRAGMENT_BIT, "volume_slice.frag.spv");

        // Vertex layout.
        VkyVertexLayout vertex_layout =
            vky_create_vertex_layout(canvas->gpu, 0, sizeof(VkyTexturedVertex3D));
        vky_add_vertex_attribute(
            &vertex_layout, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkyTexturedVertex3D, pos));
        vky_add_vertex_attribute(
            &vertex_layout, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkyTexturedVertex3D, coords));

        // Resource layout.
        VkyResourceLayout resource_layout =
            vky_create_resource_layout(canvas->gpu, canvas->image_count);
        vky_add_resource_binding(&resource_layout, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
        vky_add_resource_binding(&resource_layout, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        // Pipeline.
        visual->pipeline = vky_create_graphics_pipeline(
            canvas, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, shaders, vertex_layout, resource_layout,
            (VkyGraphicsPipelineParams){false});

        // 3D texture.
        VkyTextureParams params = {ATLAS_25_SHAPE,
                                   2,
                                   VK_FORMAT_R16_UNORM,
                                   VK_FILTER_LINEAR,
                                   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                   0,
                                   false};
        VkyTexture* tex = vky_add_texture(canvas->gpu, &params);

        // Resources.
        vky_add_uniform_buffer_resource(visual, &scene->grid->dynamic_buffer);
        vky_add_texture_resource(visual, tex);

        _update_v1(.5);

        snprintf(path, sizeof(path), "%s/volume/%s", DATA_DIR, "atlas_25.img");
        uint32_t size = 0;
        pixels = read_file(path, &size);
        vky_upload_texture(tex, pixels);
        free(pixels);
    }



    vky_add_frame_callback(canvas, frame_callback);
    vky_run_app(app);
    vky_destroy_app(app);
    return 0;
}
