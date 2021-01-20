from pathlib import Path
import time

import numpy as np
from allensdk.core.mouse_connectivity_cache import MouseConnectivityCache
from ibllib.atlas import AllenAtlas

from visky import canvas, run


RESOLUTION = 25


# Download the mouse data.
mcc = MouseConnectivityCache(resolution=RESOLUTION)
structure_id = 997   # 315 for isocortex
cortex = mcc.get_structure_mesh(structure_id)
vertices, normals, triangles, tn = cortex
indices = triangles.ravel()
N = vertices.shape[0]
Nf = triangles.shape[0]
print(f"{N} vertices, {Nf} faces")


atlas = AllenAtlas(RESOLUTION)

def _transpose(M):
    # return M
    x, y, z = M.T
    return np.c_[-z, -y, -x]


def _index_of(arr, lookup):
    lookup = np.asarray(lookup, dtype=np.int32)
    m = (lookup.max() if len(lookup) else 0) + 1
    tmp = np.zeros(m + 1, dtype=np.int)
    # Ensure that -1 values are kept.
    tmp[-1] = -1
    if len(lookup):
        tmp[lookup] = np.arange(len(lookup))
    return tmp[arr]



vertices = _transpose(vertices)
normals = _transpose(normals)



# Create the scene.
canvas = canvas()
panel = canvas.panel(controller='arcball')

# Mesh.
mesh = panel.visual('mesh')
mesh.data('pos', vertices.astype(np.float64))
mesh.data('normal', normals.astype(np.float32))
mesh.data('index', indices.astype(np.uint32))
mesh.data('clip', np.array([1, 0, 0, 0]).astype(np.float32))




root = Path('/home/cyrille/git/visky-data/yanliang/').resolve()

x = np.load(root / 'single_unit_x.npy')
y = np.load(root / 'single_unit_y.npy')
z = np.load(root / 'single_unit_z.npy')
pos = np.c_[x, y, z]
pos_ccf = _transpose(atlas.xyz2ccf(pos, ccf_order='apdvml')[:, [2, 0, 1]])

l = atlas.get_labels(pos)
lr = _index_of(l, atlas.regions.id)
color = atlas.regions.rgb[lr]

visual = panel.visual('marker', depth_test=True)

N = x.size
color = np.hstack((color, 255 * np.ones((N, 1)))).astype(np.uint8)
ms = 5 * np.ones(1)

visual.data('pos', pos_ccf.astype(np.float64))
visual.data('color', color.astype(np.uint8))
visual.data('ms', ms.astype(np.float32))




# x0, y0, z0 = pos.min(axis=0)
# x1, y1, z1 = pos.max(axis=0)

# volume, info = mcc.get_template_volume()
# volume *= 100

# # Slice plane.
# plane = panel.visual('volume_slice')
# plane.data('pos', np.array([[0, +.25, +.5]], dtype=np.float64), idx=0)
# plane.data('pos', np.array([[0, +.25, -.5]], dtype=np.float64), idx=1)
# plane.data('pos', np.array([[0, -.45, -.5]], dtype=np.float64), idx=2)
# plane.data('pos', np.array([[0, -.45, +.5]], dtype=np.float64), idx=3)

# plane.data('texcoords', np.array([[0.5, 0, 0]], dtype=np.float32), idx=0)
# plane.data('texcoords', np.array([[0.5, 1, 0]], dtype=np.float32), idx=1)
# plane.data('texcoords', np.array([[0.5, 1, 1]], dtype=np.float32), idx=2)
# plane.data('texcoords', np.array([[0.5, 0, 1]], dtype=np.float32), idx=3)

# plane.data('colormap', np.array([[26]], dtype=np.int32))
# plane.data('transferx', np.array([[0, .1, 1, 1]], dtype=np.float32), idx=1)
# plane.data('transfery', np.array([[0, 1, 1, 1]], dtype=np.float32), idx=1)
# plane.volume(volume)

run()
