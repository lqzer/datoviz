#version 450
#include "common.glsl"
#include "colormaps.glsl"

#define STEP_SIZE 0.005
#define MAX_ITER 10 / STEP_SIZE

layout(std140, binding = USER_BINDING) uniform Params
{
    vec4 box_size;
    int cmap;
}
params;

layout(binding = (USER_BINDING + 1)) uniform sampler2D tex_cmap; // colormap texture
layout(binding = (USER_BINDING + 2)) uniform sampler3D tex;      // 3D volume

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_uvw;
layout(location = 2) in vec3 in_ray;

layout(location = 0) out vec4 out_color;


bool intersect_box(vec3 origin, vec3 dir, vec3 box_min, vec3 box_max, out float t0, out float t1)
{
    vec3 inv_r = 1.0 / dir;
    vec3 tbot = inv_r * (box_min-origin);
    vec3 ttop = inv_r * (box_max-origin);
    vec3 tmin = min(ttop, tbot);
    vec3 tmax = max(ttop, tbot);
    vec2 t = max(tmin.xx, tmin.yz);
    t0 = max(t.x, t.y);
    t = min(tmax.xx, tmax.yz);
    t1 = min(t.x, t.y);
    return t0 <= t1;
}



vec4 fetch_color(vec3 uvw) {
    float v = texture(tex, uvw).r;

    // Color component: colormap.
    vec4 color = colormap(params.cmap, v);
    // vec4 color = texture(tex_cmap, vec2(v, (params.cmap + .5) / 256.0));

    // Alpha value: value.
    color.a = v;
    return color;
}



void main()
{
    CLIP

    mat4 mi = inverse(mvp.model);
    vec3 u = (mi * vec4(normalize(in_ray), 1)).xyz;
    vec3 o = (mi * vec4(-mvp.view[3].xyz, 1)).xyz;

    // // Inner cube example.
    // float r = .25;
    // vec3 b0 = vec3(-r);
    // vec3 b1 = vec3(+r);
    // bool b = intersect_box(o, u, b0, b1);
    // float a = b ? .75 : .25;
    // out_color = vec4(in_uvw, 1);
    // out_color.xyz *= a;
    // // Inner sphere example.
    // // float delta = pow(dot(u, o-c), 2) - (dot(o-c, o-c)-r*r);

    float t0, t1;
    vec3 b0 = -params.box_size.xyz / 2;
    vec3 b1 = +params.box_size.xyz / 2;
    intersect_box(o, u, b0, b1, t0, t1);
    if (t0 < 0 || t1 < 0) discard;

    vec3 ray_start = o + u * t0;
    vec3 ray_stop = o + u * t1;

    vec3 pos = ray_stop;
    vec3 dl = normalize(ray_start - ray_stop) * STEP_SIZE;
    float travel = distance(ray_start, ray_stop);
    float max_intensity = 0.0;
    vec3 uvw = vec3(0);
    vec4 s = vec4(0);
    vec4 acc = vec4(0);
    float alpha = 0;
    for (int i = 0; i < MAX_ITER && travel > 0.0; ++i, pos += dl, travel -= STEP_SIZE) {
        uvw = (pos - b0) / (b1 - b0);
        s = fetch_color(uvw);
        alpha = s.a;
        acc = s + (1 - alpha) * acc;

        // MIP
        if (s.a > max_intensity) {
            max_intensity = s.a;
        }

    }
    // if (max_intensity < .001)
    //     discard;
    out_color = acc;
}
