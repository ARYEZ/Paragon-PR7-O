# The orchestra pictures for the plugin (PR7-O 0.5.0), from strings3d.py's renders: every hue pulled into the family's
# range (318 through 0 to 42 degrees: the render's tone mapping burns the brightest orange towards yellow and the bloom
# turns pink towards purple), saved as JPEG, and the renders' geometry merged into orchestra.json.
#   python3 process_orchestra.py <the renders' folder> <resources/orchestra>
# The renders' folder holds single-<violin|viola|cello|bass>.png/.json, section.png/.json and section-lit.png.
# 0.6.0, the brass (brass3d.py): a second folder, with single-<trumpet|trombone|horn|tuba>.png/.json, section.png/.json
# and section-lit.png:
#   python3 process_orchestra.py <the strings' renders> <resources/orchestra> <the brass's renders>
# Without it, the brass's pictures and geometry already in <resources/orchestra> are kept.
import json, os, sys
import numpy as np
from PIL import Image

raw, out = sys.argv[1], sys.argv[2]
brass = sys.argv[3] if len(sys.argv) > 3 else None

def family_hues(path):
    rgb = np.asarray(Image.open(path).convert("RGB")).astype(np.float32) / 255.0
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx, mn = rgb.max(-1), rgb.min(-1)
    delta = mx - mn
    hue = np.zeros_like(mx)
    nz = delta > 1e-6
    rr = nz & (mx == r)
    gg = nz & (mx == g) & ~rr
    bb = nz & ~rr & ~gg
    hue[rr] = ((g - b)[rr] / delta[rr]) % 6.0
    hue[gg] = (b - r)[gg] / delta[gg] + 2.0
    hue[bb] = (r - g)[bb] / delta[bb] + 4.0
    hue = hue * 60.0
    sat = np.where(mx > 0, delta / np.maximum(mx, 1e-6), 0.0)
    # hues outside 318..42 moved to the nearer end, 4 degrees inside it (8-bit rounding and JPEG move a colour a degree
    # or two); every pixel with any colour, the nearly grey too (scaled down, a lilac grey blended into a pink makes a
    # pink past the range)
    outside = (hue > 38.0) & (hue < 322.0) & (sat > 0.0)
    nearer = np.where(hue - 38.0 < 322.0 - hue, 38.0, 322.0)
    hue = np.where(outside, nearer, hue)
    # back to RGB (value and saturation kept)
    h6 = (hue / 60.0) % 6.0
    c = mx * sat
    x = c * (1 - np.abs(h6 % 2 - 1))
    m = mx - c
    z = np.zeros_like(c)
    sector = np.floor(h6).astype(int)
    parts = [(c, x, z), (x, c, z), (z, c, x), (z, x, c), (x, z, c), (c, z, x)]
    out_rgb = np.zeros_like(rgb)
    for i, (a, bq, cq) in enumerate(parts):
        sel = sector == i
        out_rgb[..., 0][sel] = (a + m)[sel]
        out_rgb[..., 1][sel] = (bq + m)[sel]
        out_rgb[..., 2][sel] = (cq + m)[sel]
    return Image.fromarray((np.clip(out_rgb, 0, 1) * 255 + 0.5).astype(np.uint8))

geometry = {"single": {}, "section": {}}
for kind in ("violin", "viola", "cello", "bass"):
    family_hues(os.path.join(raw, "single-%s.png" % kind)).save(os.path.join(out, "%s.jpg" % kind), quality=88, optimize=True)
    g = json.load(open(os.path.join(raw, "single-%s.json" % kind)))
    geometry["single"][kind] = g["instruments"][kind]
    geometry["single"][kind]["size"] = [g["width"], g["height"]]
family_hues(os.path.join(raw, "section.png")).save(os.path.join(out, "section.jpg"), quality=88, optimize=True)
family_hues(os.path.join(raw, "section-lit.png")).save(os.path.join(out, "section-lit.jpg"), quality=88, optimize=True)
g = json.load(open(os.path.join(raw, "section.json")))
geometry["section"] = g["instruments"]
geometry["sectionSize"] = [g["width"], g["height"]]

def brass_geometry(g):
    # the valves (valve 1 first; the horn's thumb valve last) and the trombone's hand at 1st and 7th position: the
    # slide's brace, 0.60 m behind its crook at 1st, 0.55 m further at 7th (brass3d.py exports the crook at 1st and
    # 0.55 m on)
    out = {"area": g["area"]}
    points = g["points"]
    out["valves"] = [points[n] for n in ("valve1", "valve2", "valve3", "valve4", "thumb") if n in points]
    if "slide" in points:
        (sx, sy), (ex, ey) = points["slide"], g["slideEnd"]
        hx, hy = sx - (ex - sx) * 0.60 / 0.55, sy - (ey - sy) * 0.60 / 0.55
        out["slide"] = [[round(hx, 2), round(hy, 2)], [round(hx + ex - sx, 2), round(hy + ey - sy, 2)]]
    return out

if brass:
    for kind in ("trumpet", "trombone", "horn", "tuba"):
        family_hues(os.path.join(brass, "single-%s.png" % kind)).save(os.path.join(out, "%s.jpg" % kind), quality=88, optimize=True)
        g = json.load(open(os.path.join(brass, "single-%s.json" % kind)))
        geometry["single"][kind] = brass_geometry(g["instruments"][kind])
        geometry["single"][kind]["size"] = [g["width"], g["height"]]
    family_hues(os.path.join(brass, "section.png")).save(os.path.join(out, "brass-section.jpg"), quality=88, optimize=True)
    family_hues(os.path.join(brass, "section-lit.png")).save(os.path.join(out, "brass-section-lit.jpg"), quality=88, optimize=True)
    g = json.load(open(os.path.join(brass, "section.json")))
    geometry["brassSection"] = {k: brass_geometry(v) for k, v in g["instruments"].items()}
else:
    kept = json.load(open(os.path.join(out, "orchestra.json")))
    for kind in ("trumpet", "trombone", "horn", "tuba"):
        if kind in kept["single"]:
            geometry["single"][kind] = kept["single"][kind]
    if "brassSection" in kept:
        geometry["brassSection"] = kept["brassSection"]
with open(os.path.join(out, "orchestra.json"), "w") as f:
    json.dump(geometry, f, indent=1)
print("wrote", out)
