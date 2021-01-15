#ifndef VKL_PANEL_HEADER
#define VKL_PANEL_HEADER

#include "transforms.h"
#include "visuals.h"



/*************************************************************************************************/
/*  Constants                                                                                    */
/*************************************************************************************************/

#define VKL_GRID_MAX_COLS         64
#define VKL_GRID_MAX_ROWS         64
#define VKL_MAX_PANELS            1024
#define VKL_MAX_VISUALS_PER_PANEL 64

// Group index of the set of panel VklCommands objects.
#define VKL_COMMANDS_GROUP_PANELS 1



/*************************************************************************************************/
/*  Enums                                                                                        */
/*************************************************************************************************/

// Panel mode.
typedef enum
{
    VKL_PANEL_GRID,
    VKL_PANEL_INSET,
    VKL_PANEL_FLOATING,
} VklPanelMode;



// Grid axis.
typedef enum
{
    VKL_GRID_HORIZONTAL,
    VKL_GRID_VERTICAL,
} VklGridAxis;



// Size unit.
typedef enum
{
    VKL_PANEL_UNIT_NORMALIZED,
    VKL_PANEL_UNIT_FRAMEBUFFER,
    VKL_PANEL_UNIT_SCREEN,
} VklPanelSizeUnit;



/*************************************************************************************************/
/*  Typedefs                                                                                     */
/*************************************************************************************************/

typedef struct VklGrid VklGrid;
typedef struct VklPanel VklPanel;
typedef struct VklController VklController;



/*************************************************************************************************/
/*  Structs                                                                                      */
/*************************************************************************************************/

struct VklPanel
{
    VklObject obj;
    VklScene* scene;

    VklGrid* grid;
    VklPanelMode mode;
    VklPanelSizeUnit size_unit; // the unit x, y, width, height are in
    VklDataCoords data_coords;  // data CPU transformation

    // User-specified:
    uint32_t row, col;
    uint32_t hspan, vspan;

    // Computed automatically from above and grid widths and heights:
    // Unless using inset
    float x, y;
    float width, height;

    // Visuals
    uint32_t visual_count;
    VklVisual* visuals[VKL_MAX_VISUALS_PER_PANEL];

    // Viewport.
    VklViewport viewport;

    // GPU objects
    VklBufferRegions br_mvp; // for the uniform buffer containing the MVP
    void* mvp_mmap;          // for permanent mapping of the MVP uniform buffer

    VklController* controller;
    VklCommands* cmds;
    int prority_max;
};



struct VklGrid
{
    VklCanvas* canvas;
    uint32_t n_rows, n_cols;

    // In normalized coordinates (in [0, 1]):
    float xs[VKL_GRID_MAX_COLS];
    float ys[VKL_GRID_MAX_ROWS];
    double widths[VKL_GRID_MAX_COLS];
    double heights[VKL_GRID_MAX_ROWS];

    VklContainer panels;
};



/*************************************************************************************************/
/*  Functions                                                                                    */
/*************************************************************************************************/

VKY_EXPORT VklGrid vkl_grid(VklCanvas* canvas, uint32_t rows, uint32_t cols);

VKY_EXPORT void vkl_grid_destroy(VklGrid* grid);

VKY_EXPORT VklPanel* vkl_panel(VklGrid* grid, uint32_t row, uint32_t col);

VKY_EXPORT void vkl_panel_update(VklPanel* panel);

VKY_EXPORT void vkl_panel_margins(VklPanel* panel, vec4 margins);

VKY_EXPORT void vkl_panel_dpi_scaling(VklPanel* panel, float scaling);

VKY_EXPORT void vkl_panel_unit(VklPanel* panel, VklPanelSizeUnit unit);

VKY_EXPORT void vkl_panel_mode(VklPanel* panel, VklPanelMode mode);

VKY_EXPORT void vkl_panel_visual(VklPanel* panel, VklVisual* visual);

VKY_EXPORT void vkl_panel_pos(VklPanel* panel, float x, float y);

VKY_EXPORT void vkl_panel_size(VklPanel* panel, VklGridAxis axis, float size);

VKY_EXPORT void vkl_panel_span(VklPanel* panel, VklGridAxis axis, uint32_t span);

VKY_EXPORT void vkl_panel_cell(VklPanel* panel, uint32_t row, uint32_t col);

VKY_EXPORT bool vkl_panel_contains(VklPanel* panel, vec2 pos);

VKY_EXPORT VklPanel* vkl_panel_at(VklGrid* grid, vec2 pos); // normalized coords

VKY_EXPORT void vkl_panel_destroy(VklPanel* panel);

VKY_EXPORT VklViewport vkl_panel_viewport(VklPanel* panel);



#endif
