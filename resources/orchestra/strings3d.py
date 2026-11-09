# THE ORCHESTRA VIEW's string family (PR7-O): our own violin, viola, cello and double bass, modelled and rendered in
# the PR7 family's "Tron" look, as PR7-D's kit and PR7-I's guitar are (the same materials, neon gradient, grid floor,
# lights and bloom as PR7-I's guitar3d.py). Gloss black bodies with a neon edge running pink (the low string's side)
# to orange (the high string's), glowing purfling and f-holes, pearl bridges, strings lit in the family's string
# colours, on a gloss floor with a red grid. The outline is our own drawing of the violin family's shape (real
# proportions: the body's length, bouts and waist; the bass with sloping shoulders), no maker's model traced.
#
# blender -b -P strings3d.py -- <out.png> [samples] [width] [height] [view] [lit] [geometry.json]
#   view: "single:<instrument>" (one lying across, scroll right, low string on top) or "section" (all four standing,
#         low to high, left to right); lit: instruments glowing brighter ("violin,viola" or "none").
import bpy, sys, math, json, os
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else "/tmp/strings.png"
SAMPLES = int(argv[1]) if len(argv) > 1 else 32
WIDTH = int(argv[2]) if len(argv) > 2 else 2048
HEIGHT = int(argv[3]) if len(argv) > 3 else 842
VIEW = argv[4] if len(argv) > 4 else "section"
LIT = [] if len(argv) <= 5 or argv[5] == "none" else argv[5].split(",")
GEOMETRY = argv[6] if len(argv) > 6 else None

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# ================================================================ materials (PR7-I's guitar3d.py)
def srgb(h):
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple(((x + 0.055) / 1.055) ** 2.4 if x > 0.04045 else x / 12.92 for x in c) + (1.0,)

def setin(node, names, value):
    for n in names:
        if n in node.inputs:
            node.inputs[n].default_value = value
            return

def material(name, colour, metallic=0.0, rough=0.4, coat=0.0, spec=0.5, emission=None, strength=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = colour
    b.inputs["Metallic"].default_value = metallic
    b.inputs["Roughness"].default_value = rough
    setin(b, ("Coat Weight", "Clearcoat"), coat)
    setin(b, ("Coat Roughness",), 0.09)
    setin(b, ("Specular IOR Level", "Specular"), spec)
    if emission is not None:
        setin(b, ("Emission Color", "Emission"), emission)
        setin(b, ("Emission Strength",), strength)
    return m

def emission(name, colour, strength):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = colour
    em.inputs["Strength"].default_value = strength
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    return m

def neon(name, strength, half_width):
    # Pink at the low string's side, red in the middle, orange at the high string's: by each point's X in the
    # instrument's own coordinates (its strings run along Y, the low one at -X).
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Strength"].default_value = strength
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    rng = nt.nodes.new("ShaderNodeMapRange")
    rng.inputs["From Min"].default_value = -half_width
    rng.inputs["From Max"].default_value = half_width
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    e = ramp.color_ramp.elements
    e[0].position, e[0].color = 0.0, srgb("ff2d6e")
    e[1].position, e[1].color = 1.0, srgb("ff6a24")
    mid = e.new(0.5)
    mid.color = srgb("ff2a2e")
    nt.links.new(tc.outputs["Object"], sep.inputs["Vector"])
    nt.links.new(sep.outputs["X"], rng.inputs["Value"])
    nt.links.new(rng.outputs["Result"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], em.inputs["Color"])
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    return m

def grid_floor(px_per_m):
    # Gloss black with a faint red grid (one line every 48 px at the plugin's 2x), fading out from the middle.
    m = material("grid floor", srgb("030203"), rough=0.25, spec=0.5)
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    coords = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(coords.outputs["Object"], sep.inputs["Vector"])
    def line(axis):
        a = nt.nodes.new("ShaderNodeMath"); a.operation = "MULTIPLY"; a.inputs[1].default_value = px_per_m / 48.0
        f = nt.nodes.new("ShaderNodeMath"); f.operation = "FRACT"
        c = nt.nodes.new("ShaderNodeMath"); c.operation = "SUBTRACT"; c.inputs[1].default_value = 0.5
        ab = nt.nodes.new("ShaderNodeMath"); ab.operation = "ABSOLUTE"
        g = nt.nodes.new("ShaderNodeMath"); g.operation = "GREATER_THAN"; g.inputs[1].default_value = 0.482
        nt.links.new(sep.outputs[axis], a.inputs[0]); nt.links.new(a.outputs[0], f.inputs[0])
        nt.links.new(f.outputs[0], c.inputs[0]); nt.links.new(c.outputs[0], ab.inputs[0]); nt.links.new(ab.outputs[0], g.inputs[0])
        return g
    mx = nt.nodes.new("ShaderNodeMath"); mx.operation = "MAXIMUM"
    nt.links.new(line("X").outputs[0], mx.inputs[0]); nt.links.new(line("Y").outputs[0], mx.inputs[1])
    dist = nt.nodes.new("ShaderNodeVectorMath"); dist.operation = "LENGTH"
    nt.links.new(coords.outputs["Object"], dist.inputs[0])
    fade = nt.nodes.new("ShaderNodeMapRange")
    reach = WIDTH / 2.0 / px_per_m / 2.0   # (the picture is 2x)
    fade.inputs["From Min"].default_value = 0.15 * reach; fade.inputs["From Max"].default_value = 1.1 * reach
    fade.inputs["To Min"].default_value = 0.24; fade.inputs["To Max"].default_value = 0.05
    nt.links.new(dist.outputs["Value"], fade.inputs["Value"])
    k = nt.nodes.new("ShaderNodeMath"); k.operation = "MULTIPLY"
    nt.links.new(mx.outputs[0], k.inputs[0]); nt.links.new(fade.outputs["Result"], k.inputs[1])
    setin(b, ("Emission Color", "Emission"), srgb("c0102c"))
    nt.links.new(k.outputs[0], b.inputs["Emission Strength"])
    return m

LACQUER = material("onyx lacquer", srgb("0a080b"), rough=0.22, coat=1.0, spec=0.5)
EBONY = material("ebony", srgb("0b0909"), rough=0.5, spec=0.35)
CHROME = material("black chrome", srgb("2a2629"), metallic=1.0, rough=0.16)
PEARL = material("bridge", srgb("e8e0e3"), rough=0.35, emission=srgb("f4eef1"), strength=0.6)

def link(o):
    bpy.context.collection.objects.link(o)
    return o

def assign(o, m):
    o.data.materials.clear()
    o.data.materials.append(m)

def mesh_from(name, verts, faces, mat, parent):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    o = link(bpy.data.objects.new(name, me))
    assign(o, mat)
    o.parent = parent
    return o

def tube(name, pts, r, mat, parent, closed=False):
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = r
    cu.bevel_resolution = 3
    sp = cu.splines.new("POLY")
    sp.points.add(len(pts) - 1)
    for p, q in zip(sp.points, pts):
        p.co = (q[0], q[1], q[2], 1.0)
    sp.use_cyclic_u = closed
    o = link(bpy.data.objects.new(name, cu))
    o.data.materials.append(mat)
    o.parent = parent
    return o

def catmull(pts, n=10, closed=True):
    out = []
    count = len(pts)
    last = count if closed else count - 1
    for i in range(last):
        p0, p1, p2, p3 = (pts[(i + k) % count] if closed else pts[max(0, min(count - 1, i + k))] for k in (-1, 0, 1, 2))
        for s in range(n):
            t = s / n
            out.append(tuple(0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t
                                    + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t * t * t) for j in range(2)))
    if not closed:
        out.append(tuple(pts[-1]))
    return out

def slab(name, outline, z0, z1, mat, parent, bevel=0.0):
    # A flat shape (its outline, counter-clockwise) between two heights: top and bottom n-gons, the sides quads.
    n = len(outline)
    verts = [(x, y, z0) for x, y in outline] + [(x, y, z1) for x, y in outline]
    faces = [list(range(n - 1, -1, -1)), list(range(n, 2 * n))]
    faces += [[i, (i + 1) % n, n + (i + 1) % n, n + i] for i in range(n)]
    o = mesh_from(name, verts, faces, mat, parent)
    if bevel > 0:
        mod = o.modifiers.new("bevel", "BEVEL")
        mod.width = bevel
        mod.segments = 4
        mod.limit_method = "ANGLE"
    for p in o.data.polygons:
        p.use_smooth = True
    return o

# ================================================================ the family (metres)
# Real sizes: body length L, its depth (ribs), the neck (body to nut), the pegbox and scroll, the fingerboard's width at
# the nut and its end and how far it runs over the body, the strings' spacing at the bridge and nut, the endpin, the
# string length (nut to bridge), and the shoulders (the bass's slope).
FAMILY = {
    "violin":  dict(L=0.356, ribs=0.032, neck=0.130, scroll=0.105, fbN=0.024, fbE=0.042, over=0.135, gapB=0.0115, gapN=0.0055, pin=0.0, sloped=False),
    "viola":   dict(L=0.410, ribs=0.038, neck=0.150, scroll=0.118, fbN=0.025, fbE=0.045, over=0.150, gapB=0.0125, gapN=0.0060, pin=0.0, sloped=False),
    "cello":   dict(L=0.755, ribs=0.120, neck=0.280, scroll=0.200, fbN=0.032, fbE=0.065, over=0.300, gapB=0.0220, gapN=0.0085, pin=0.30, sloped=False),
    "bass":    dict(L=1.120, ribs=0.190, neck=0.420, scroll=0.270, fbN=0.045, fbE=0.100, over=0.420, gapB=0.0320, gapN=0.0120, pin=0.28, sloped=True),
}
# The outline: half widths along the body (bottom 0 to top 1), as fractions of its length. Our own drawing: the lower
# bout widest at a fifth of the way, the waist (C-bouts) between little corners, the upper bout; the bass's shoulders
# fall away into the neck.
VIOLIN_HALF = [(0.0, 0.0), (0.012, 0.12), (0.045, 0.215), (0.10, 0.268), (0.20, 0.29), (0.30, 0.279), (0.355, 0.256),
               (0.395, 0.238), (0.415, 0.200), (0.46, 0.166), (0.53, 0.156), (0.60, 0.168), (0.635, 0.195), (0.655, 0.219),
               (0.70, 0.232), (0.78, 0.236), (0.86, 0.224), (0.925, 0.185), (0.97, 0.118), (0.993, 0.045), (1.0, 0.0)]
BASS_HALF = [(0.0, 0.0), (0.012, 0.12), (0.045, 0.215), (0.10, 0.27), (0.20, 0.292), (0.30, 0.282), (0.355, 0.262),
             (0.395, 0.245), (0.415, 0.212), (0.46, 0.180), (0.53, 0.170), (0.60, 0.182), (0.635, 0.205), (0.655, 0.222),
             (0.70, 0.228), (0.76, 0.222), (0.83, 0.190), (0.89, 0.140), (0.94, 0.095), (0.975, 0.060), (0.995, 0.03), (1.0, 0.0)]
STRING_COLOURS = ["ff2d6e", "ff2b48", "ff3a28", "ff6a24"]   # low to high: pink, rose, red, orange (as PR7-I's strings)

def build(kind, origin, rotation, lit):
    p = FAMILY[kind]
    L, ribs = p["L"], p["ribs"]
    root = link(bpy.data.objects.new(kind, None))
    root.location = origin
    root.rotation_euler = (0, 0, rotation)
    glow = 4.0 if lit else 1.0
    half = 0.30 * L
    edge = neon(kind + " neon", 8.0 * glow, half)
    soft = neon(kind + " neon soft", 3.0 * glow, half)

    # the body: the outline as a slab, its edge rounded, a neon tube round its widest line
    halfs = BASS_HALF if p["sloped"] else VIOLIN_HALF
    right = [(w * L, y * L) for y, w in halfs]
    left = [(-x, y) for x, y in reversed(right[1:-1])]
    outline = catmull(right + left, 8)
    slab(kind + " body", outline, 0.0, ribs, LACQUER, root, bevel=0.06 * ribs)
    tube(kind + " edge", [(x * 1.004, y, ribs * 0.55) for x, y in outline], 0.0045 * L, edge, root, closed=True)
    # purfling: a soft line just inside the edge, on the top
    inset = [(x * 0.955, 0.02 * L + y * 0.962) for x, y in outline]
    tube(kind + " purfling", [(x, y, ribs + 0.0015) for x, y in inset], 0.0016 * L, soft, root, closed=True)

    # the bridge, the f-holes beside it, the tailpiece
    yB = 0.455 * L
    zTop = ribs
    zString = ribs + 0.085 * L
    for s in (-1, 1):
        pts = []
        for i in range(31):
            t = i / 30
            y = yB - 0.12 * L + t * 0.24 * L
            # an S along the strings, its upper end leaning in towards the strings
            x = s * (0.088 * L + 0.016 * L * math.sin(math.pi * (2 * t - 1)) - 0.014 * L * (t - 0.5))
            pts.append((x, y, zTop + 0.002))
        tube(kind + " f-hole %d" % s, pts, 0.0035 * L, soft, root)
        for (x, y, z), r in ((pts[0], 0.010 * L), (pts[-1], 0.008 * L)):
            ring = [(x + r * math.cos(a * math.pi / 8), y + r * math.sin(a * math.pi / 8), z) for a in range(16)]
            tube(kind + " f-eye", ring, 0.0025 * L, soft, root, closed=True)
    bw = 0.125 * L
    bridge = slab(kind + " bridge", [(-bw / 2, yB - 0.006 * L), (bw / 2, yB - 0.006 * L), (bw / 2, yB + 0.006 * L), (-bw / 2, yB + 0.006 * L)],
                  zTop, zString, PEARL, root)
    tail = catmull([(-0.018 * L, 0.03 * L), (0.018 * L, 0.03 * L), (0.058 * L, 0.21 * L), (0.0, 0.225 * L), (-0.058 * L, 0.21 * L)], 6)
    slab(kind + " tailpiece", tail, zTop, zTop + 0.025 * L, EBONY, root, bevel=0.004 * L)
    tube(kind + " tailpiece edge", [(x, y, zTop + 0.026 * L) for x, y in tail], 0.0018 * L, soft, root, closed=True)

    # the neck and fingerboard, over the body from its end to the nut
    yEnd = L - p["over"]
    yNut = L + p["neck"]
    # (the fingerboard is a flat slab: its nut end as high as its body end, so the strings run above it all the way;
    # 0.5.0 had the nut lower and the strings sank into the board near it, hidden: seen once the view was zoomed in)
    zFbEnd, zFbNut = ribs + 0.060 * L, ribs + 0.060 * L
    hE, hN = p["fbE"] / 2, p["fbN"] / 2
    slab(kind + " neck", [(-hN * 0.9, L - 0.02 * L), (hN * 0.9, L - 0.02 * L), (hN * 0.85, yNut), (-hN * 0.85, yNut)], zTop * 0.3, zFbNut - 0.004 * L, LACQUER, root)
    fb = [(-hE, yEnd), (hE, yEnd), (hN, yNut), (-hN, yNut)]
    o = slab(kind + " fingerboard", fb, zFbEnd - 0.01 * L, zFbEnd, EBONY, root, bevel=0.002 * L)
    tube(kind + " fingerboard edge L", [(-hE, yEnd, zFbEnd), (-hN, yNut, zFbNut)], 0.0020 * L, soft, root)
    tube(kind + " fingerboard edge R", [(hE, yEnd, zFbEnd), (hN, yNut, zFbNut)], 0.0020 * L, soft, root)
    slab(kind + " nut", [(-hN, yNut - 0.004 * L), (hN, yNut - 0.004 * L), (hN, yNut), (-hN, yNut)], zFbNut - 0.004 * L, zFbNut + 0.006 * L, PEARL, root)

    # the pegbox, its pegs, and the scroll seen from above: a disc with a glowing spiral
    sc = p["scroll"]
    box = [(-hN * 0.9, yNut), (hN * 0.9, yNut), (hN * 0.8, yNut + 0.62 * sc), (-hN * 0.8, yNut + 0.62 * sc)]
    slab(kind + " pegbox", box, zFbNut - 0.03 * L, zFbNut, LACQUER, root, bevel=0.002 * L)
    tube(kind + " pegbox edge", [(x, y, zFbNut + 0.001) for x, y in box[1:] + box[:1]], 0.0018 * L, soft, root)
    for i in range(4):
        side = -1 if i % 2 == 0 else 1
        y = yNut + (0.12 + 0.13 * i) * sc
        peg = [(side * hN * 0.8, y - 0.010 * L), (side * (hN + 0.07 * L), y - 0.012 * L), (side * (hN + 0.075 * L), y + 0.012 * L), (side * hN * 0.8, y + 0.010 * L)]
        if side < 0:
            peg = peg[::-1]
        slab(kind + " peg %d" % i, peg, zFbNut - 0.012 * L, zFbNut - 0.002 * L, CHROME, root, bevel=0.002 * L)
        tube(kind + " peg %d glow" % i, [(side * hN * 0.85, y, zFbNut - 0.001), (side * (hN + 0.07 * L), y, zFbNut - 0.001)], 0.0018 * L, soft, root)
        tube(kind + " peg end %d" % i, [(side * (hN + 0.075 * L), y - 0.013 * L, zFbNut - 0.004 * L), (side * (hN + 0.075 * L), y + 0.013 * L, zFbNut - 0.004 * L)],
             0.0022 * L, soft, root)
    cy = yNut + 0.80 * sc
    rS = 0.034 * L
    disc = [(rS * math.cos(a * math.pi / 16), cy + rS * math.sin(a * math.pi / 16)) for a in range(32)]
    slab(kind + " scroll", disc, zFbNut - 0.03 * L, zFbNut + 0.004 * L, LACQUER, root, bevel=0.004 * L)
    tube(kind + " scroll edge", [(x, y, zFbNut + 0.005 * L) for x, y in disc], 0.0018 * L, soft, root, closed=True)
    spiral = [((rS * 0.92) * (1 - t / 2.4) * math.cos(t * math.pi - math.pi / 2), cy + (rS * 0.92) * (1 - t / 2.4) * math.sin(t * math.pi - math.pi / 2),
               zFbNut + 0.006 * L) for t in [i / 20 for i in range(41)]]
    tube(kind + " scroll spiral", spiral, 0.0022 * L, soft, root)

    # the endpin (cello, bass)
    if p["pin"] > 0:
        tube(kind + " endpin", [(0, 0, ribs * 0.5), (0, -p["pin"], ribs * 0.5)], 0.0035 * L, soft, root)
        tube(kind + " endpin tip", [(0, -p["pin"], ribs * 0.5), (0, -p["pin"] - 0.012 * L, ribs * 0.5)], 0.007 * L, edge, root)

    # the strings: tailpiece over the bridge to the nut, each its colour
    strings = []
    for i in range(4):
        off = i - 1.5
        xB, xN, xT = off * p["gapB"], off * p["gapN"], off * p["gapB"] * 0.75
        m = emission("%s string %d" % (kind, i), srgb(STRING_COLOURS[i]), 5.0 * (2.0 if lit else 1.0))
        r = max(0.00045, p["gapB"] * 0.075)
        tube("%s string %d" % (kind, i), [(xT, 0.205 * L, zTop + 0.03 * L), (xB, yB, zString), (xN, yNut, zFbNut + 0.006 * L)], r, m, root)
        strings.append(((xB, yB, zString), (xN, yNut, zFbNut + 0.006 * L)))
    return root, strings, p

# ================================================================ the scene
LIGHT = 0.2
RIM = 0.25
if VIEW.startswith("single:"):
    kind = VIEW.split(":")[1]
    p = FAMILY[kind]
    length = p["L"] + p["neck"] + p["scroll"] + p["pin"] + 0.04 * p["L"]
    PX_PER_M = (WIDTH / 2) * 0.93 / length
    # lying across: its strings along X, the scroll right, the low string on top (+Y)
    start = -length / 2 + p["pin"]
    # 0.5.1, ONE COMMON SCALE like PR7-I's guitar's (the owner: "much closer to the scale that we used for the guitar
    # in the PR7-I ... We don't need to see the entire body", then "all four instruments to one common scale"): the
    # largest scale at which every instrument's strings, bridge to nut, stay in the picture: the double bass's fill it
    # (ORCH_SPAN, 1.05, of them across), and the others are drawn at that same scale, each centred on its strings
    # (the cello's fill about three quarters, the violin's and viola's about a third). The body and scroll of the
    # bigger ones run off the edges. ORCH_FRAME=strings: each fills the picture instead (the first mock-up);
    # ORCH_FRAME=whole: 0.5.0's, each whole instrument fitted.
    # The owner chose from three (0.5.1): the scale halfway (geometric middle) between the violin's strings filling the
    # picture (x1.22) and the bass's (x1.05): 0.654 m across, the violin whole and about as big as 0.5.0's, the cello
    # and bass big. A bigger instrument's strings run past the picture: it's framed on its nut end (the nut at 92%
    # across, where low-position notes are played), the bridge off the left edge. ORCH_FIT / ORCH_SPAN: another scale
    # (the instrument whose strings set it, and how many of them across).
    frame = os.environ.get("ORCH_FRAME", "common")
    if frame in ("common", "strings"):
        def span(q): return q["L"] + q["neck"] - 0.455 * q["L"]   # bridge to nut
        halfway = math.sqrt (span (FAMILY["violin"]) * 1.22 * span (FAMILY["bass"]) * 1.05)
        fitted = FAMILY[os.environ.get("ORCH_FIT", "violin")] if frame == "common" else p
        across = span (fitted) * float(os.environ.get("ORCH_SPAN", str (halfway / span (FAMILY["violin"])) if frame == "common" else "1.22"))   # metres
        PX_PER_M = (WIDTH / 2) / across
        start = -(0.455 * p["L"] + p["L"] + p["neck"]) / 2
        if span (p) > across * 0.95:
            start = 0.42 * across - (p["L"] + p["neck"])
    root, strings, _ = build(kind, (start, 0, 0), -math.pi / 2, kind in LIT)
    pieces = [(kind, root, strings)]
else:
    # all four standing (scroll up), low to high from the left, their bodies' bottoms on one line
    kinds = ["bass", "cello", "viola", "violin"]
    tallest = max(FAMILY[k]["L"] + FAMILY[k]["neck"] + FAMILY[k]["scroll"] + FAMILY[k]["pin"] for k in kinds)
    PX_PER_M = (HEIGHT / 2) * 0.90 / tallest
    widths = [0.62 * FAMILY[k]["L"] + 0.16 * FAMILY[k]["L"] for k in kinds]
    gap = 0.22
    total = sum(widths) + gap * (len(kinds) - 1)
    x = -total / 2
    base = -tallest / 2 + max(FAMILY[k]["pin"] for k in kinds) + 0.02
    pieces = []
    for k, w in zip(kinds, widths):
        root, strings, _ = build(k, (x + w / 2, base, 0), 0.0, k in LIT)
        pieces.append((k, root, strings))
        x += w + gap

FLOOR = grid_floor(PX_PER_M * 2)   # the picture is at 2x; the grid every 48 px of it
bpy.ops.mesh.primitive_plane_add(size=12.0, location=(0, 0, -0.002))
assign(bpy.context.object, FLOOR)

world = bpy.data.worlds.new("dark")
scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = srgb("060407")
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.02 * LIGHT

def light(name, kind, loc, rot, energy, colour, size=1.0, size_y=None):
    data = bpy.data.lights.new(name, kind)
    data.energy = energy
    data.color = colour[:3]
    if kind == "AREA":
        data.size = size
        if size_y:
            data.shape = "RECTANGLE"
            data.size_y = size_y
    o = link(bpy.data.objects.new(name, data))
    o.location = loc
    o.rotation_euler = rot
    return o

span = (WIDTH / 2) / PX_PER_M
s2 = span * span
light("sheen", "AREA", (-0.1 * span, 0.2 * span, 1.2 * span), (math.radians(-14), math.radians(-10), 0), 2.2 * LIGHT * s2, (1.0, 0.95, 0.96), size=0.3 * span, size_y=0.8 * span)
light("key", "AREA", (-0.3 * span, 0.5 * span, 0.9 * span), (math.radians(-35), math.radians(-25), 0), 1.2 * LIGHT * s2, (1.0, 0.96, 0.95), size=0.5 * span)
light("rim pink", "AREA", (0, 0.6 * span, 0.15 * span), (math.radians(-75), 0, 0), 9 * RIM * s2, srgb("ff3b6a"), size=1.2 * span, size_y=0.1 * span)
light("rim orange", "AREA", (0, -0.6 * span, 0.15 * span), (math.radians(75), 0, 0), 9 * RIM * s2, srgb("ff5a2c"), size=1.2 * span, size_y=0.1 * span)
for name in ("sheen", "key"):
    bpy.data.objects[name].visible_glossy = False

cam_data = bpy.data.cameras.new("camera")
cam_data.type = "ORTHO"
cam_data.ortho_scale = (WIDTH / 2) / PX_PER_M
cam = link(bpy.data.objects.new("camera", cam_data))
TILT = math.radians(5)
cam.location = Vector((0, -math.sin(TILT), math.cos(TILT))) * 4.0
cam.rotation_euler = (TILT, 0, 0)
cam_data.clip_end = 20
scene.camera = cam

scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = SAMPLES
scene.cycles.use_denoising = False
scene.render.resolution_x = WIDTH
scene.render.resolution_y = HEIGHT
enum = [v.identifier for v in scene.view_settings.bl_rna.properties["view_transform"].enum_items]
scene.view_settings.view_transform = "Filmic" if "Filmic" in enum else "Standard"
scene.cycles.seed = 7
scene.use_nodes = True
tree = scene.node_tree
tree.nodes.clear()
layers = tree.nodes.new("CompositorNodeRLayers")
glare = tree.nodes.new("CompositorNodeGlare")
glare.glare_type = "FOG_GLOW"
glare.quality = "HIGH"
glare.threshold = 0.9
glare.size = 8
glare.mix = -0.35
comp = tree.nodes.new("CompositorNodeComposite")
tree.links.new(layers.outputs["Image"], glare.inputs["Image"])
tree.links.new(glare.outputs["Image"], comp.inputs["Image"])

# where each string's bridge and nut end land in the picture (px), for the code that lights notes on top
if GEOMETRY:
    bpy.context.view_layer.update()
    def px(world):
        v = world_to_camera_view(scene, cam, world)
        return [round(v.x * WIDTH, 2), round((1 - v.y) * HEIGHT, 2)]
    geo = {"width": WIDTH, "height": HEIGHT, "instruments": {}}
    for k, root, strings in pieces:
        # its strings (bridge and nut ends) and the area it covers (its parts' corners, with room for the glow)
        xs, ys = [], []
        for o in bpy.data.objects:
            if o.parent == root:
                for corner in o.bound_box:
                    x, y = px(o.matrix_world @ Vector(corner))
                    xs.append(x); ys.append(y)
        margin = 0.05 * FAMILY[k]["L"] * PX_PER_M * 2
        geo["instruments"][k] = {"strings": [{"bridge": px(root.matrix_world @ Vector(b)), "nut": px(root.matrix_world @ Vector(n))} for b, n in strings],
                                 "area": [round(min(xs) - margin), round(min(ys) - margin), round(max(xs) + margin), round(max(ys) + margin)]}
    with open(GEOMETRY, "w") as f:
        json.dump(geo, f)

scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("RENDERED", OUT)
