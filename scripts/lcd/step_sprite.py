"""
Render a STEP model into a small pixel-art sprite for the LCD: the saver's motor drawn from the
manufacturer's CAD instead of by hand.

    step -> triangles (cascadio, the OpenCASCADE kernel) -> a turned, orthographic view ->
    a supersampled z-buffer with flat shading in the model's own colours -> downsampled ->
    16 colours (clear, outline, the cyan / hot-pink rim lights, and 12 from the model) ->
    a one-pixel outline round it

Needs: pip install cascadio trimesh numpy pillow (a tool for the generator, not the firmware).
"""

import math
import os
import tempfile
from typing import List, Optional, Sequence, Tuple

import numpy as np

RIM_TOP = (5, 217, 232)       # cyan, on the edges that face up and left
RIM_BOTTOM = (255, 41, 117)   # hot pink, on the edges that face down
OUTLINE = (10, 0, 18)


def load(step_path: str, tol_mm: float = 0.15) -> Tuple[np.ndarray, np.ndarray]:
    """The model's triangles (n, 3, 3) in mm and a colour per triangle (n, 3) 0..255."""
    import cascadio
    import trimesh
    with tempfile.TemporaryDirectory() as tmp:
        glb = os.path.join(tmp, "model.glb")
        cascadio.step_to_glb(step_path, glb, tol_linear=tol_mm, tol_angular=0.5, merge_primitives=True)
        scene = trimesh.load(glb, force="scene")
        tris, cols = [], []
        for mesh in scene.dump(concatenate=False):
            colour = (150, 150, 160)
            try:
                mat = mesh.visual.material
                base = getattr(mat, "baseColorFactor", None)
                if base is not None:
                    colour = tuple(int(c) for c in np.asarray(base)[:3])
                else:
                    colour = tuple(int(c) for c in mat.main_color[:3])
            except Exception:                 # noqa: BLE001 (no material: the default grey)
                pass
            tris.append(mesh.triangles)
            cols.append(np.tile(colour, (len(mesh.triangles), 1)))
    return np.concatenate(tris), np.concatenate(cols).astype(float)


def rotation(yaw_deg: float, pitch_deg: float, roll_deg: float) -> np.ndarray:
    y, p, r = (math.radians(a) for a in (yaw_deg, pitch_deg, roll_deg))
    rz = np.array([[math.cos(r), -math.sin(r), 0], [math.sin(r), math.cos(r), 0], [0, 0, 1]])
    ry = np.array([[math.cos(y), 0, math.sin(y)], [0, 1, 0], [-math.sin(y), 0, math.cos(y)]])
    rx = np.array([[1, 0, 0], [0, math.cos(p), -math.sin(p)], [0, math.sin(p), math.cos(p)]])
    return rx @ ry @ rz


def render(tris: np.ndarray, cols: np.ndarray, w: int, h: int, rot: np.ndarray,
           supersample: int = 4, light=(-0.45, -0.6, 0.65)) -> Tuple[np.ndarray, np.ndarray]:
    """RGB (h, w, 3) and coverage (h, w) 0..1: orthographic, x right, y down, z toward the viewer."""
    S = supersample
    v = tris.reshape(-1, 3) @ rot.T
    lo, hi = v.min(axis=0), v.max(axis=0)
    scale = min((w * S - 2 * S) / (hi[0] - lo[0]), (h * S - 2 * S) / (hi[1] - lo[1]))
    sx = (v[:, 0] - (lo[0] + hi[0]) / 2) * scale + w * S / 2
    sy = -(v[:, 1] - (lo[1] + hi[1]) / 2) * scale + h * S / 2
    sz = v[:, 2]
    P = np.stack([sx, sy, sz], axis=1).reshape(-1, 3, 3)
    # flat shading from each face's normal (in view space)
    n = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    n[:, 1] *= -1                                    # screen y is down
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    n *= np.sign(n[:, 2:3] + 1e-12)                  # face the viewer
    L = np.array(light, float); L /= np.linalg.norm(L)
    lit = 0.38 + 0.62 * np.clip(n @ L, 0, 1)
    spec = np.clip(n @ (L + np.array([0, 0, 1.0])) / np.linalg.norm(L + np.array([0, 0, 1.0])), 0, 1) ** 24
    shade = np.clip(cols * lit[:, None] + 255 * 0.35 * spec[:, None], 0, 255)
    W, H = w * S, h * S
    zbuf = np.full((H, W), -np.inf)
    img = np.zeros((H, W, 3))
    for t in range(len(P)):
        (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = P[t]
        minx, maxx = int(max(0, math.floor(min(x0, x1, x2)))), int(min(W - 1, math.ceil(max(x0, x1, x2))))
        miny, maxy = int(max(0, math.floor(min(y0, y1, y2)))), int(min(H - 1, math.ceil(max(y0, y1, y2))))
        if minx > maxx or miny > maxy:
            continue
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-9:
            continue
        xs, ys = np.meshgrid(np.arange(minx, maxx + 1) + 0.5, np.arange(miny, maxy + 1) + 0.5)
        a = ((y1 - y2) * (xs - x2) + (x2 - x1) * (ys - y2)) / den
        b = ((y2 - y0) * (xs - x2) + (x0 - x2) * (ys - y2)) / den
        c = 1 - a - b
        inside = (a >= 0) & (b >= 0) & (c >= 0)
        if not inside.any():
            continue
        z = a * z0 + b * z1 + c * z2
        region = zbuf[miny:maxy + 1, minx:maxx + 1]
        nearer = inside & (z > region)
        region[nearer] = z[nearer]
        img[miny:maxy + 1, minx:maxx + 1][nearer] = shade[t]
    cover = np.isfinite(zbuf).astype(float)
    rgb = img.reshape(h, S, w, S, 3).sum(axis=(1, 3))
    cov = cover.reshape(h, S, w, S).sum(axis=(1, 3))
    out = np.where(cov[..., None] > 0, rgb / np.maximum(cov[..., None], 1), 0)
    return out, cov / (S * S)


def to_indices(rgb: np.ndarray, cov: np.ndarray, model_colours: int = 12) -> Tuple[List[List[int]], List[Tuple[int, int, int]]]:
    """Palette indices (0 clear) and the palette: clear, outline, cyan, pink, then the model's."""
    from PIL import Image
    h, w = cov.shape
    solid = cov >= 0.5
    pixels = rgb[solid].astype(np.uint8)
    strip = Image.fromarray(pixels.reshape(1, -1, 3), "RGB")
    q = strip.quantize(colors=model_colours, method=Image.Quantize.MEDIANCUT)
    pal = q.getpalette()[:model_colours * 3]
    model = [tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)]
    palette = [(0, 0, 0), OUTLINE, RIM_TOP, RIM_BOTTOM] + model
    palette += [(0, 0, 0)] * (16 - len(palette))
    grid = [[0] * w for _ in range(h)]
    qi = list(q.getdata())
    k = 0
    for y in range(h):
        for x in range(w):
            if solid[y, x]:
                grid[y][x] = 4 + qi[k]
                k += 1
    # the rim lights: the silhouette's top and left edges cyan, its bottom edge hot pink
    def clear(x, y):
        return not (0 <= x < w and 0 <= y < h) or grid[y][x] == 0
    rim = [row[:] for row in grid]
    for y in range(h):
        for x in range(w):
            if grid[y][x] == 0:
                continue
            if clear(x, y - 1) or clear(x - 1, y):
                rim[y][x] = 2
            elif clear(x, y + 1):
                rim[y][x] = 3
    out = [row[:] for row in rim]
    for y in range(h):
        for x in range(w):
            if rim[y][x] == 0 and any(not clear(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                out[y][x] = 1
    return out, palette


def sprite_from_step(step_path: str, w: int, h: int, yaw: float, pitch: float, roll: float):
    tris, cols = load(step_path)
    rgb, cov = render(tris, cols, w, h, rotation(yaw, pitch, roll))
    return to_indices(rgb, cov)
