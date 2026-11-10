# THE ORCHESTRA VIEW's brass (PR7-O 0.6.0): our own trumpet, horn, trombone and tuba, modelled and rendered in the PR7
# family's "Tron" look, as the strings are (strings3d.py: the same materials, neon gradient, grid floor, lights and
# bloom). Satin gunmetal tubes with a neon line along each, running pink at the mouthpiece end to orange at the bell;
# bright bell rims; neon rings on the valves. Our own drawings at real sizes (side views); no maker's instrument traced.
#
# blender -b -P brass3d.py -- <out.png> [samples] [width] [height] [view] [lit] [geometry.json]
#   view: "single:<trumpet|trombone|horn|tuba>" (one, bell right) or "section" (the tuba standing at the left, the
#         trombone across the top right, the horn and trumpet under it); lit: instruments glowing brighter.
#   BRASS_FRAME=whole: each whole instrument fitted (the tuba always is); BRASS_ACROSS: the common scale (metres
#   across the picture, 0.745).
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
CHROME = material("gunmetal", srgb("6a5e65"), metallic=1.0, rough=0.3)   # (the brass's tubes; strings3d's name kept)
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

# ================================================================ the brass (metres; our own drawings, side views)
# Each is drawn in its own plane: u forward (towards the bell), v up; z towards the camera (depth layers).
NEON_GLOW = {}
def neon_for(name, half, lit):
    key = (name, lit)
    if key not in NEON_GLOW:
        NEON_GLOW[key] = neon(name + (" lit" if lit else ""), 14.0 if lit else 5.0, half)
    return NEON_GLOW[key]
RIMNEON = {}
def rim_for(lit):
    if lit not in RIMNEON:
        RIMNEON[lit] = emission("rim" + (" lit" if lit else ""), srgb("ff6a24"), 22.0 if lit else 9.0)
    return RIMNEON[lit]

def arc(c, R, a0, a1, n=14):
    return [(c[0] + R * math.cos(a0 + (a1 - a0) * i / n), c[1] + R * math.sin(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]

class Builder:
    def __init__(self, name, origin, rotation, half, lit):
        self.root = link(bpy.data.objects.new(name, None))
        self.root.location = origin
        self.root.rotation_euler = (0, 0, rotation)
        self.neon = neon_for("neon " + name, half, lit)
        self.rim = rim_for(lit)
        self.lit = lit
        self.points = {}   # named points (root coordinates) for the geometry file
        self.n = 0
    def name(self, s):
        self.n += 1
        return "%s %s %d" % (self.root.name, s, self.n)
    def pipe(self, pts, r, z, spine=True, smooth=True):
        pts = catmull(pts, 6, closed=False) if smooth and len(pts) > 2 else pts
        tube(self.name("pipe"), [(p[0], p[1], z) for p in pts], r, CHROME, self.root)
        if spine:
            tube(self.name("spine"), [(p[0], p[1], z + r * 0.9) for p in pts], max(0.0007, r * 0.16), self.neon, self.root)
    def ring(self, c, d, r, z, width=0.0012, mat=None):
        # a ring round the tube at c, its axis along d (a ferrule / a bell's rim)
        a = math.atan2(d[1], d[0]); nx, ny = -math.sin(a), math.cos(a)
        pts = [(c[0] + nx * r * math.cos(t), c[1] + ny * r * math.cos(t), z + r * math.sin(t)) for t in [2 * math.pi * i / 32 for i in range(32)]]
        tube(self.name("ring"), pts, width, mat or self.neon, self.root, closed=True)
    def bell(self, start, angle, length, r0, R, z, power=4.0, rings=40):
        d = (math.cos(angle), math.sin(angle)); nx, ny = -d[1], d[0]
        verts, faces, seg = [], [], 28
        prof = []
        for i in range(rings + 1):
            t = i / rings
            s = length * (1 - (1 - t) ** 1.6)   # rings closer near the mouth
            tt = s / length
            r = r0 + (R - r0) * (tt ** power) * 0.85 + (R - r0) * 0.15 * tt * tt
            prof.append((s, r))
        prof[-1] = (length, R)
        for s, r in prof:
            cx, cy = start[0] + d[0] * s, start[1] + d[1] * s
            for j in range(seg):
                f = 2 * math.pi * j / seg
                verts.append((cx + nx * r * math.cos(f), cy + ny * r * math.cos(f), z + r * math.sin(f)))
        for i in range(rings):
            for j in range(seg):
                a, b = i * seg + j, i * seg + (j + 1) % seg
                faces.append([a, b, b + seg, a + seg])
        o = mesh_from(self.name("bell"), verts, faces, CHROME, self.root)
        for p in o.data.polygons: p.use_smooth = True
        m = o.modifiers.new("solid", "SOLIDIFY"); m.thickness = 0.0012
        # its outline: neon along both edges (as the strings' bodies are edged), and the rim
        for side in (1, -1):
            edge = [(start[0] + d[0] * s + side * nx * r * 1.002, start[1] + d[1] * s + side * ny * r * 1.002, z) for s, r in prof]
            tube(self.name("bell edge"), edge, 0.0011 + 0.0006 * (R / 0.06), self.neon, self.root)
        mouth = (start[0] + d[0] * length, start[1] + d[1] * length)
        self.ring(mouth, d, R * 1.01, z, width=0.0018 + 0.001 * (R / 0.06), mat=self.rim)
        self.ring((start[0], start[1]), d, r0 * 1.25, z, width=0.0009)
        return mouth
    def mouthpiece(self, tip, angle, z):
        d = (math.cos(angle), math.sin(angle))
        # cup at the tip, shank towards the pipe
        self.bell((tip[0] + d[0] * 0.075, tip[1] + d[1] * 0.075), angle + math.pi, 0.075, 0.0045, 0.0135, z, power=6.0, rings=16)
        self.points["mouthpiece"] = tip
    def piston(self, key, u, v0, v1, r, z, button=0.03):
        # a piston valve standing across the tubing: its casing, neon caps top and bottom, its stem and finger button
        tube(self.name("casing"), [(u, v0, z), (u, v1, z)], r, CHROME, self.root)
        for v in (v0 + 0.004, v1 - 0.004):
            self.ring((u, v), (0, 1), r * 1.06, z, width=0.0011 + r * 0.04)
        tube(self.name("stem"), [(u, v1, z), (u, v1 + button, z)], r * 0.22, CHROME, self.root)
        top = (u, v1 + button)
        tube(self.name("button"), [(u, top[1] - 0.002, z), (u, top[1] + 0.004, z)], r * 0.85, EBONY, self.root)
        self.ring(top, (0, 1), r * 0.88, z, width=0.0012 + r * 0.03)
        self.points[key] = top
    def rotary(self, key, c, r, z, lever):
        # a rotary valve seen end on: its casing a short drum towards the camera, a neon ring on its face, its lever
        tube(self.name("drum"), [(c[0], c[1], z - r * 0.6), (c[0], c[1], z + r * 0.6)], r, CHROME, self.root)
        pts = [(c[0] + r * 1.02 * math.cos(t), c[1] + r * 1.02 * math.sin(t), z + r * 0.6) for t in [2 * math.pi * i / 32 for i in range(32)]]
        tube(self.name("face"), pts, 0.0012 + r * 0.05, self.neon, self.root, closed=True)
        tube(self.name("lever"), [(c[0], c[1], z + r * 0.7), (lever[0], lever[1], z + r * 0.7)], 0.0022, CHROME, self.root)
        tube(self.name("paddle"), [(lever[0], lever[1], z + r * 0.7), (lever[0] - 0.012, lever[1] - 0.004, z + r * 0.7)], 0.0055, EBONY, self.root)
        self.points[key] = c
    def brace(self, a, b, z, r=0.0022):
        tube(self.name("brace"), [(a[0], a[1], z), (b[0], b[1], z)], r, CHROME, self.root)

def trumpet(origin, rotation, lit):
    # B-flat trumpet, 0.48 m: mouthpiece at the back (left), leadpipe forward to the main slide, three pistons, the
    # rear bow and the bell (12.3 cm) forward.
    b = Builder("trumpet", origin, rotation, 0.24, lit)
    r = 0.0058
    b.mouthpiece((-0.245, 0.0), 0.0, 0.03)
    b.pipe([(-0.17, 0.0), (0.0, -0.002), (0.12, -0.006)] + arc((0.12, -0.042), 0.036, math.pi / 2, -math.pi / 2)[1:] + [(0.04, -0.078)], r, 0.03)
    b.ring((-0.17, 0.0), (1, 0), r * 1.5, 0.03); b.ring((0.07, -0.004), (1, 0), r * 1.3, 0.03)
    for i, u in enumerate((-0.036, -0.009, 0.018)):
        b.piston("valve%d" % (i + 1), u, -0.098, -0.004, 0.0118, 0.02)
    b.pipe([(0.018, -0.068), (0.085, -0.068)] + arc((0.085, -0.084), 0.016, math.pi / 2, -math.pi / 2)[1:] + [(0.018, -0.1)], r * 0.85, 0.012)
    b.pipe([(-0.036, -0.068), (-0.085, -0.068)] + arc((-0.085, -0.082), 0.014, math.pi / 2, 3 * math.pi / 2)[1:] + [(-0.036, -0.096)], r * 0.85, 0.012)
    b.pipe([(-0.009, -0.03), (-0.15, -0.035)] + arc((-0.15, 0.0), 0.035, -math.pi / 2, -3 * math.pi / 2)[1:] + [(-0.06, 0.035), (0.0, 0.035)], r, 0.005)
    b.ring((-0.1, -0.035), (1, 0), r * 1.3, 0.005)
    b.points["bell"] = b.bell((0.0, 0.035), 0.0, 0.245, r, 0.0615, 0.005)
    b.brace((-0.11, 0.0), (-0.11, 0.035), 0.01); b.brace((0.06, -0.004), (0.06, 0.035), 0.01)
    return b

def trombone(origin, rotation, lit):
    # Tenor trombone, 1.2 m closed: mouthpiece at the back, the hand slide forward (two tubes 10 cm apart, its crook at
    # the front), the bell section behind it (21.6 cm bell), the tuning slide at the back.
    b = Builder("trombone", origin, rotation, 0.6, lit)
    r = 0.0068
    b.mouthpiece((-0.14, 0.0), 0.0, 0.04)
    b.pipe([(-0.065, 0.0), (0.62, 0.0)], r, 0.04, smooth=False)
    b.pipe([(0.62, -0.10), (-0.03, -0.10)], r, 0.04, smooth=False)
    b.pipe(arc((0.62, -0.05), 0.05, math.pi / 2, -math.pi / 2), r * 1.1, 0.04)
    b.ring((0.66, -0.05), (0, 1), r * 1.3, 0.04)
    # the outer slide's sleeves: thicker from the handle forward, and its brace (the hand grips it)
    for v in (0.0, -0.10):
        tube(b.name("sleeve"), [(0.01, v, 0.04), (0.6, v, 0.04)], r * 1.18, CHROME, b.root)
        tube(b.name("sleeve spine"), [(0.01, v, 0.04 + r * 1.1), (0.6, v, 0.04 + r * 1.1)], r * 0.2, b.neon, b.root)
    b.brace((0.02, 0.0), (0.02, -0.10), 0.045, 0.004)
    b.brace((-0.04, 0.0), (-0.04, -0.10), 0.045, 0.0035)
    b.points["slide"] = (0.62, -0.05)   # the crook's middle at 1st position
    b.points["slideAxis"] = (1.0, 0.0)
    # the bell section: from the slide's lower tube back (goose neck) to the tuning slide, then forward as the bell
    b.pipe([(-0.03, -0.10), (-0.10, -0.085), (-0.38, -0.085)] + arc((-0.38, -0.06), 0.025, -math.pi / 2, -3 * math.pi / 2)[1:] + [(-0.33, -0.035), (-0.28, -0.035)], r, 0.0)
    b.ring((-0.2, -0.085), (1, 0), r * 1.3, 0.0)
    b.points["bell"] = b.bell((-0.28, -0.035), 0.0, 0.50, r * 1.05, 0.108, -0.01, power=5.0)
    b.brace((-0.22, -0.085), (-0.22, -0.035), 0.0); b.brace((-0.06, 0.0), (-0.06, -0.035), 0.02)
    return b

def horn(origin, rotation, lit):
    # Double horn in F / B-flat, its hoops 27 cm across: the leadpipe from the mouthpiece (upper left) into the four
    # rotary valves (the thumb valve under them), the hoops round, and the bell (31 cm) out to the right.
    b = Builder("horn", origin, rotation, 0.3, lit)
    r = 0.0055
    for R, z in ((0.135, 0.0), (0.122, 0.006), (0.109, 0.012)):
        b.pipe(arc((0.0, 0.0), R, math.radians(-95), math.radians(250), 40), r, z)
    b.mouthpiece((-0.235, 0.115), math.radians(-12), 0.03)
    b.pipe([(-0.162, 0.10), (-0.10, 0.085), (-0.06, 0.06)], r, 0.03)
    for i, (c, lever) in enumerate(((( -0.055, 0.03), (-0.09, 0.075)), ((-0.02, 0.03), (-0.075, 0.085)), ((0.015, 0.03), (-0.06, 0.095)))):
        b.rotary("valve%d" % (i + 1), c, 0.0155, 0.03, lever)
    b.rotary("thumb", (-0.055, -0.01), 0.0155, 0.03, (-0.105, 0.02))
    for c, w in (((-0.055, 0.03), 0.05), ((-0.02, 0.03), 0.065), ((0.015, 0.03), 0.08)):
        b.pipe([(c[0] - 0.006, c[1] + 0.012), (c[0] - 0.006, c[1] + w)] + arc((c[0], c[1] + w), 0.008, math.pi, 0.0)[1:] + [(c[0] + 0.008, c[1] + 0.012)], r * 0.85, 0.022)
    b.pipe([(0.03, 0.02), (0.09, 0.0), (0.11, -0.06)], r, 0.02)
    b.pipe([(-0.05, -0.03), (-0.04, -0.09), (0.0, -0.135)], r, 0.02)
    b.points["bell"] = b.bell((0.0, -0.137), math.radians(14), 0.36, r, 0.155, 0.0, power=5.0)
    return b

def tuba(origin, rotation, lit):
    # B-flat tuba, 0.92 m tall, standing (bell up, 42 cm): the bell's long body down to the bottom bow, up into four
    # pistons, the mouthpiece and leadpipe from the left, the valves' slides and the upper loop behind.
    b = Builder("tuba", origin, rotation, 0.3, lit)
    r = 0.034
    b.points["bell"] = b.bell((0.12, -0.05), math.pi / 2, 0.51, 0.04, 0.21, -0.03, power=3.0)
    b.pipe([(0.12, -0.05), (0.12, -0.30)] + arc((0.0, -0.30), 0.12, 0.0, -math.pi, 16)[1:] + [(-0.12, -0.15), (-0.11, -0.10)], 0.04, -0.03)
    b.ring((0.12, -0.2), (0, 1), 0.046, -0.03); b.ring((-0.12, -0.2), (0, 1), 0.046, -0.03)
    for i, u in enumerate((-0.1, -0.058, -0.016, 0.026)):
        b.piston("valve%d" % (i + 1), u, -0.08, 0.13, 0.0175, 0.03, button=0.035)
    b.pipe([(-0.11, -0.10), (0.04, -0.09)], 0.022, 0.0)
    b.mouthpiece((-0.33, 0.21), math.radians(-25), 0.05)
    b.pipe([(-0.26, 0.178), (-0.18, 0.13), (-0.12, 0.10)], 0.011, 0.05)
    b.pipe([(0.04, 0.08), (0.075, 0.12), (0.075, 0.24)] + arc((-0.02, 0.24), 0.095, 0.0, math.pi, 14)[1:] + [(-0.115, 0.13)], 0.018, 0.0)
    for u, w in ((-0.1, 0.12), (-0.058, 0.09), (-0.016, 0.07)):
        b.pipe([(u, -0.03), (u - w, -0.03)] + arc((u - w, -0.06), 0.03, math.pi / 2, 3 * math.pi / 2)[1:] + [(u, -0.09)], 0.013, -0.015)
    b.pipe([(0.026, -0.03), (0.09, -0.06), (0.09, -0.16)] + arc((0.06, -0.16), 0.03, 0.0, -math.pi)[1:] + [(0.03, -0.09)], 0.013, -0.015)
    return b

BRASS = {"trumpet": trumpet, "trombone": trombone, "horn": horn, "tuba": tuba}
# extents in its own plane (u0, v0, u1, v1), for framing
EXTENT = {"trumpet": (-0.25, -0.13, 0.25, 0.10), "trombone": (-0.48, -0.15, 0.68, 0.075),
          "horn": (-0.24, -0.16, 0.37, 0.15), "tuba": (-0.34, -0.37, 0.34, 0.46)}

LIGHT = 0.2
RIM = 0.25
ROT = {"trumpet": 0.0, "trombone": 0.0, "horn": 0.0, "tuba": -math.pi / 2}
def extent(k, rot):
    u0, v0, u1, v1 = EXTENT[k]
    if rot == 0.0: return u0, v0, u1, v1
    return v0, -u1, v1, -u0   # turned -90 degrees: (u, v) -> (v, -u)
if VIEW.startswith("single:"):
    kind = VIEW.split(":")[1]
    x0, y0, x1, y1 = extent(kind, ROT[kind])
    # 0.6.0, the owner's choice from the mock-up: trumpet, trombone and horn at one common scale (0.745 m across: the
    # geometric middle of the trumpet filling the picture and the trombone; a longer one framed on its bell end, so the
    # trombone's slide crook stays in), the tuba (92 cm tall: only its valves would show) fitted whole.
    frame = "whole" if kind == "tuba" else os.environ.get("BRASS_FRAME", "common")
    if frame == "whole":
        across = max(x1 - x0, (y1 - y0) * (WIDTH / HEIGHT)) * 1.06
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    else:
        across = float(os.environ.get("BRASS_ACROSS", "0.745"))
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        if x1 - x0 > across * 0.95:   # framed on its bell end
            cx = x1 - across * 0.47
        cx = float(os.environ.get("BRASS_CX", cx)); cy = float(os.environ.get("BRASS_CY", cy))
    PX_PER_M = (WIDTH / 2) / across
    b = BRASS[kind]((-cx, -cy, 0), ROT[kind], kind in LIT)
    pieces = [(kind, b)]
else:
    # the tuba standing at the left (bell up); the trombone lying across the top right, the horn and trumpet under it
    E = {k: EXTENT[k] for k in BRASS}
    gap = 0.07
    tw, th = E["tuba"][2] - E["tuba"][0], E["tuba"][3] - E["tuba"][1]
    rw = max(E["trombone"][2] - E["trombone"][0], E["horn"][2] - E["horn"][0] + gap + E["trumpet"][2] - E["trumpet"][0])
    total_w, total_h = tw + gap + rw, th
    PX_PER_M = min((HEIGHT / 2) * 0.80 / total_h, (WIDTH / 2) * 0.90 / total_w)
    x0, y0 = -total_w / 2, -total_h / 2 - 0.035
    def place(k, ux, vy):
        return BRASS[k]((ux - E[k][0], vy - E[k][1], 0), 0.0, k in LIT)
    rx = x0 + tw + gap
    hb = E["trombone"][3] - E["trombone"][1]; hh = E["horn"][3] - E["horn"][1]
    pieces = [("tuba", place("tuba", x0, y0)),
              ("trombone", place("trombone", rx + (rw - (E["trombone"][2] - E["trombone"][0])) / 2, y0 + total_h * 0.5 + 0.06)),
              ("horn", place("horn", rx, y0 + 0.02)),
              ("trumpet", place("trumpet", rx + (E["horn"][2] - E["horn"][0]) + gap, y0 + 0.02 + (hh - (E["trumpet"][3] - E["trumpet"][1])) / 2))]

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
# 0.6.0 (the owner: "make the metal a bit more visible"): metal shows only what it reflects, and the key and sheen
# lights are hidden from reflections (they'd light the gloss floor up), so two more lights shine on the instruments
# alone (Cycles' light linking: the floor doesn't receive them), satin gunmetal catching them.
metal_only = bpy.data.collections.new("brass receivers")
for o in bpy.data.objects:
    if o.type in ("MESH", "CURVE") and o.parent is not None:
        metal_only.objects.link(o)
for name, loc, rot, energy, size in (("metal key", (-0.3 * span, 0.5 * span, 0.9 * span), (math.radians(-35), math.radians(-25), 0), 3.2, 0.5),
                                     ("metal sheen", (0.2 * span, -0.2 * span, 1.2 * span), (math.radians(10), math.radians(8), 0), 2.8, 0.4)):
    o = light(name, "AREA", loc, rot, energy * LIGHT * s2, (1.0, 0.95, 0.96), size=size * span)
    o.light_linking.receiver_collection = metal_only

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
    for k, b in pieces:
        root = b.root
        # its strings (bridge and nut ends) and the area it covers (its parts' corners, with room for the glow)
        xs, ys = [], []
        for o in bpy.data.objects:
            if o.parent == root:
                for corner in o.bound_box:
                    x, y = px(o.matrix_world @ Vector(corner))
                    xs.append(x); ys.append(y)
        margin = 0.03 * PX_PER_M * 2
        geo["instruments"][k] = {"points": {n: px(root.matrix_world @ Vector((q[0], q[1], 0.0))) for n, q in b.points.items() if n != "slideAxis"},
                                 "area": [round(min(xs) - margin), round(min(ys) - margin), round(max(xs) + margin), round(max(ys) + margin)]}
        if "slide" in b.points:
            geo["instruments"][k]["slideEnd"] = px(root.matrix_world @ Vector((b.points["slide"][0] + 0.55, b.points["slide"][1], 0.0)))
    with open(GEOMETRY, "w") as f:
        json.dump(geo, f)

scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("RENDERED", OUT)
