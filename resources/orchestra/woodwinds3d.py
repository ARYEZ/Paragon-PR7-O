# THE ORCHESTRA VIEW's woodwinds (PR7-O 0.12.0): our own flute, piccolo, oboe, clarinet and bassoon, modelled and
# rendered in the PR7 family's "Tron" look, as the strings and brass are (brass3d.py's materials, lights, floor, camera
# and bloom, copied in). Black wood bodies (the flute: gunmetal), neon along both edges running pink at the mouthpiece
# end to orange at the bell, neon rings on the joints; gunmetal keys whose cups face the camera, each finger key's
# rim a neon ring. Our own drawings at real sizes (side views from above); no maker's instrument traced.
#
# blender -b -P woodwinds3d.py -- <out.png> [samples] [width] [height] [view] [lit] [geometry.json]
#   view: "single:<flute|piccolo|oboe|clarinet|bassoon|englishhorn|bassclarinet|contrabassoon>" (one, lying across,
#         mouthpiece left; the bassoon, contrabassoon, bass clarinet and English horn fitted whole) or "section" (the bassoon across the bottom, flute and piccolo above it, oboe and clarinet
#         between); lit: instruments glowing brighter. WOOD_ACROSS: the common scale (metres across, 0.745).
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


WOOD = material("grenadilla", srgb("2a2026"), rough=0.26, coat=1.0, spec=0.6)
HOLE = emission("hole", srgb("020102"), 0.0)

class Wind(Builder):
    # A woodwind lying along u (mouthpiece at low u), its bore's axis at v = 0, height z0 above the floor.
    def __init__(self, name, origin, rotation, half, lit, z0=0.03):
        super().__init__(name, origin, rotation, half, lit)
        self.z0 = z0
        self.profile = []
        self.heights = {}   # each named point's height (its key's top), for the geometry file
    def lathe(self, prof, mat, seg=28, edges=True, v=0.0):
        # a body turned round its axis: prof = [(u, r)...]
        verts, faces = [], []
        for u, r in prof:
            for j in range(seg):
                f = 2 * math.pi * j / seg
                verts.append((u, v + r * math.cos(f), self.z0 + r * math.sin(f)))
        for i in range(len(prof) - 1):
            for j in range(seg):
                a, b = i * seg + j, i * seg + (j + 1) % seg
                faces.append([a, b, b + seg, a + seg])
        o = mesh_from(self.name("body"), verts, faces, mat, self.root)
        for p in o.data.polygons: p.use_smooth = True
        if edges:
            for side in (1, -1):
                tube(self.name("edge"), [(u, v + side * r * 1.004, self.z0) for u, r in prof], 0.0007 + 0.04 * prof[0][1] * 0.3, self.neon, self.root)
        self.profile += [(u, r, v) for u, r in prof]
        return o
    def radius(self, u, v=0.0):
        best = None
        for (u0, r0, w0), (u1, r1, w1) in zip(self.profile, self.profile[1:]):
            if w0 == v and w1 == v and u0 <= u <= u1 and u1 > u0:
                best = r0 + (r1 - r0) * (u - u0) / (u1 - u0)
        return best if best is not None else 0.01
    def joint(self, u, v=0.0, scale=1.06):
        self.ring((u, v), (1, 0), self.radius(u, v) * scale, self.z0, width=0.0011)
    def cup(self, key, u, kr, v=0.0, open_hole=False, ring_key=False):
        # a key's cup facing the camera (or a plain hole with a ring round it), its rim a neon ring
        top = self.z0 + self.radius(u, v)
        if ring_key:
            tube(self.name("hole"), [(u, v, top - 0.001), (u, v, top + 0.0008)], kr * 0.62, HOLE, self.root)
            pts = [(u + kr * math.cos(t), v + kr * math.sin(t), top + 0.0012) for t in [2 * math.pi * i / 32 for i in range(32)]]
            tube(self.name("ring key"), pts, 0.0009, CHROME, self.root, closed=True)
        else:
            tube(self.name("cup"), [(u, v, top), (u, v, top + 0.003)], kr, CHROME, self.root)
            if open_hole:
                tube(self.name("open"), [(u, v, top + 0.0031), (u, v, top + 0.0034)], kr * 0.36, HOLE, self.root)
        pts = [(u + kr * 1.03 * math.cos(t), v + kr * 1.03 * math.sin(t), top + 0.0034) for t in [2 * math.pi * i / 32 for i in range(32)]]
        tube(self.name("rim"), pts, 0.0008 + kr * 0.04, self.neon, self.root, closed=True)
        self.points[key] = (u, v)
        self.heights[key] = top + 0.0034
    def pad(self, u, kr, v=0.0, side=1):
        # a closed key (no finger on it) sitting on the body's near side: a gunmetal cup, no neon
        top = self.z0 + self.radius(u, v)
        tube(self.name("pad"), [(u, v + side * self.radius(u, v) * 0.55, top - 0.002), (u, v + side * self.radius(u, v) * 0.55, top + 0.0015)], kr, CHROME, self.root)
    def rod(self, u0, u1, v=0.0, side=1):
        r = self.radius((u0 + u1) / 2, v)
        tube(self.name("rod"), [(u0, v + side * r * 0.82, self.z0 + r * 0.62), (u1, v + side * r * 0.82, self.z0 + r * 0.62)], 0.0016, CHROME, self.root)
    def lever(self, key, a, b, v=0.0):
        # a touch (a pinky or thumb key) from a to b, its end a neon dot
        za = self.z0 + self.radius(a[0], v) + 0.002
        tube(self.name("lever"), [(a[0], a[1], za), (b[0], b[1], za + 0.001)], 0.0022, CHROME, self.root)
        tube(self.name("touch"), [(b[0] - 0.004, b[1], za + 0.001), (b[0] + 0.004, b[1], za + 0.001)], 0.003, CHROME, self.root)
        pts = [(b[0] + 0.0055 * math.cos(t), b[1] + 0.004 * math.sin(t), za + 0.004) for t in [2 * math.pi * i / 24 for i in range(24)]]
        tube(self.name("touch rim"), pts, 0.0007, self.neon, self.root, closed=True)
        self.points[key] = b
        self.heights[key] = za + 0.004

def flute(origin, rotation, lit):
    # C flute, 0.67 m, silver-looking (gunmetal): head joint with its lip plate (left), body with the open-hole keys,
    # foot joint (right). The left thumb's key is on the far side: shown as a touch above the body.
    b = Wind("flute", origin, rotation, 0.34, lit, z0=0.012)
    b.lathe([(-0.335, 0.0105), (-0.33, 0.0105), (-0.10, 0.0098), (0.335, 0.0098)], CHROME)
    b.ring((-0.333, 0.0), (1, 0), 0.0108, b.z0, width=0.0015)   # the crown
    for u in (-0.10, 0.215):
        b.joint(u)
    # the lip plate and its hole
    slab(b.name("lip plate"), [(-0.27 + 0.014 * math.cos(t), 0.0115 * math.sin(t)) for t in [2 * math.pi * i / 28 for i in range(28)]],
         b.z0 + 0.0098, b.z0 + 0.0118, CHROME, b.root, bevel=0.001)
    tube(b.name("embouchure"), [(-0.27, 0.0, b.z0 + 0.0119), (-0.27, 0.0, b.z0 + 0.0121)], 0.005, HOLE, b.root)
    b.points["mouthpiece"] = (-0.27, 0.0)
    tube(b.name("lip rim"), [(-0.27 + 0.014 * 1.02 * math.cos(t), 0.0115 * 1.02 * math.sin(t), b.z0 + 0.0118 + 0.0003) for t in [2 * math.pi * i / 32 for i in range(32)]], 0.0007, b.neon, b.root, closed=True)
    b.rod(-0.055, 0.205)
    for key, u in (("L1", -0.035), ("L2", 0.0), ("L3", 0.035), ("R1", 0.105), ("R2", 0.14), ("R3", 0.175)):
        b.cup(key, u, 0.0072, open_hole=True)
    for u in (0.06, 0.08, 0.255, 0.285, 0.31):
        b.pad(u, 0.0062)
    b.rod(0.24, 0.32)
    b.lever("thumb", (-0.04, 0.008), (-0.045, 0.024))
    b.lever("Eb", (0.205, -0.008), (0.215, -0.024))
    return b

def piccolo(origin, rotation, lit):
    # Piccolo in C, 0.32 m, black wood with gunmetal keys and a gunmetal head joint.
    b = Wind("piccolo", origin, rotation, 0.16, lit, z0=0.01)
    b.lathe([(-0.16, 0.0068), (-0.07, 0.0066)], CHROME)
    b.lathe([(-0.07, 0.0072), (0.10, 0.0068), (0.16, 0.0062)], WOOD)
    b.joint(-0.07)
    slab(b.name("lip plate"), [(-0.125 + 0.009 * math.cos(t), 0.0076 * math.sin(t)) for t in [2 * math.pi * i / 28 for i in range(28)]],
         b.z0 + 0.0066, b.z0 + 0.008, CHROME, b.root, bevel=0.0008)
    tube(b.name("embouchure"), [(-0.125, 0.0, b.z0 + 0.0081), (-0.125, 0.0, b.z0 + 0.0083)], 0.0032, HOLE, b.root)
    b.points["mouthpiece"] = (-0.125, 0.0)
    tube(b.name("lip rim"), [(-0.125 + 0.009 * 1.02 * math.cos(t), 0.0076 * 1.02 * math.sin(t), b.z0 + 0.008 + 0.0003) for t in [2 * math.pi * i / 32 for i in range(32)]], 0.0007, b.neon, b.root, closed=True)
    b.rod(-0.045, 0.125)
    for key, u in (("L1", -0.03), ("L2", -0.012), ("L3", 0.006), ("R1", 0.05), ("R2", 0.068), ("R3", 0.086)):
        b.cup(key, u, 0.0046)
    for u in (0.026, 0.11, 0.13):
        b.pad(u, 0.004)
    b.lever("thumb", (-0.034, 0.005), (-0.038, 0.016))
    b.lever("Eb", (0.12, -0.005), (0.128, -0.016))
    return b

def oboe(origin, rotation, lit):
    # Oboe, 0.65 m with its reed: a narrow conical bore widening to a small bell; the top joint's plateau keys and
    # the octave keys, the lower joint's, the bell.
    b = Wind("oboe", origin, rotation, 0.33, lit, z0=0.016)
    tube(b.name("reed"), [(-0.335, 0.0, b.z0), (-0.30, 0.0, b.z0)], 0.0028, EBONY, b.root)
    b.lathe([(-0.30, 0.0034), (-0.285, 0.0034)], CHROME, edges=False)   # the staple
    b.points["mouthpiece"] = (-0.33, 0.0)
    b.lathe([(-0.285, 0.0095), (-0.27, 0.0095), (-0.05, 0.0115), (0.20, 0.0135), (0.235, 0.015), (0.30, 0.021), (0.315, 0.025)], WOOD)
    for u in (-0.27, -0.05, 0.20):
        b.joint(u, scale=1.12)
    b.rod(-0.24, 0.18)
    for key, u in (("L1", -0.19), ("L2", -0.155), ("L3", -0.12), ("R1", 0.0), ("R2", 0.035), ("R3", 0.07)):
        b.cup(key, u, 0.0068)
    for u in (-0.225, -0.095, 0.11, 0.135, 0.16, 0.26, 0.285):
        b.pad(u, 0.0055)
    b.lever("octave", (-0.225, 0.009), (-0.235, 0.024))
    b.lever("thumb", (-0.20, 0.011), (-0.205, 0.026))
    return b

def clarinet(origin, rotation, lit):
    # B-flat clarinet, 0.66 m: mouthpiece and reed with its ligature, barrel, upper joint (the left hand's ring keys,
    # the register key and thumb hole underneath), lower joint (the right hand's), the bell.
    b = Wind("clarinet", origin, rotation, 0.33, lit, z0=0.016)
    b.lathe([(-0.335, 0.005), (-0.325, 0.0085), (-0.27, 0.0125)], EBONY)   # the mouthpiece
    tube(b.name("ligature"), [(-0.30, 0.0, b.z0), (-0.285, 0.0, b.z0)], 0.0135, CHROME, b.root)
    b.points["mouthpiece"] = (-0.33, 0.0)
    b.lathe([(-0.27, 0.0145), (-0.265, 0.0155), (-0.215, 0.0145), (-0.21, 0.0125), (0.20, 0.0125), (0.23, 0.0145),
             (0.28, 0.019), (0.31, 0.026), (0.325, 0.033), (0.33, 0.034)], WOOD)
    for u in (-0.27, -0.21, 0.0, 0.20):
        b.joint(u, scale=1.14)
    b.rod(-0.175, 0.17)
    for key, u in (("L1", -0.13), ("L2", -0.095), ("L3", -0.06), ("R1", 0.05), ("R2", 0.085), ("R3", 0.12)):
        b.cup(key, u, 0.0078, ring_key=True)
    for u in (-0.16, -0.025, 0.145, 0.175):
        b.pad(u, 0.0065)
    b.lever("register", (-0.155, 0.011), (-0.16, 0.027))
    b.lever("thumb", (-0.14, 0.012), (-0.135, 0.028))
    return b

def bassoon(origin, rotation, lit):
    # Bassoon, 1.34 m: lying across (fitted whole, as the tuba is), the boot at u 0, the wing joint and the long joint side
    # by side from it, the bell past the long joint, the bocal (crook) curving out of the wing joint with the reed.
    b = Wind("bassoon", origin, rotation, 0.67, lit, z0=0.04)
    wing, long = -0.024, 0.026
    b.lathe([(-0.67, 0.034), (-0.40, 0.034)], WOOD, v=0.0)   # the boot (both bores)
    b.lathe([(-0.40, 0.0145), (0.10, 0.017)], WOOD, v=wing)
    b.lathe([(-0.40, 0.019), (0.20, 0.021), (0.205, 0.024), (0.67, 0.026)], WOOD, v=long)
    b.ring((-0.67, 0.0), (1, 0), 0.035, b.z0, width=0.0016)
    b.ring((-0.40, 0.0), (1, 0), 0.0355, b.z0, width=0.0016)
    for u, v in ((0.10, wing), (0.20, long), (0.665, long)):
        b.joint(u, v=v, scale=1.08)
    bocal = [(0.10, wing), (0.14, wing - 0.004), (0.20, wing - 0.03), (0.25, wing - 0.075), (0.28, wing - 0.10)]
    b.pipe(bocal, 0.0035, b.z0 + 0.012)
    tube(b.name("reed"), [(0.28, wing - 0.10, b.z0 + 0.012), (0.305, wing - 0.118, b.z0 + 0.012)], 0.0042, EBONY, b.root)
    b.points["mouthpiece"] = (0.30, wing - 0.117)
    for key, u in (("L1", -0.10), ("L2", -0.145), ("L3", -0.19)):
        b.cup(key, u, 0.0068, v=wing, ring_key=True)
    for key, u in (("R1", -0.47), ("R2", -0.51), ("R3", -0.55)):
        b.cup(key, u, 0.0072, v=0.0, ring_key=True)
    b.rod(-0.25, 0.05, v=long)
    for u in (-0.06, -0.24, -0.30, 0.0):
        b.pad(u, 0.006, v=long)
    b.lever("thumb", (-0.12, wing - 0.012), (-0.13, wing - 0.032))
    b.lever("whisper", (0.0, wing - 0.012), (-0.01, wing - 0.032))
    return b

def english_horn(origin, rotation, lit):
    # English horn (cor anglais), 0.81 m with its bocal: the oboe's alto, longer, a bent metal bocal and the reed at the
    # left, its pear-shaped bell (the bulb) at the right. Its keys as the oboe's.
    b = Wind("english horn", origin, rotation, 0.41, lit, z0=0.02)
    b.pipe([(-0.345, 0.0), (-0.375, -0.006), (-0.395, -0.016)], 0.0028, b.z0)
    tube(b.name("reed"), [(-0.395, -0.016, b.z0), (-0.41, -0.024, b.z0)], 0.003, EBONY, b.root)
    b.points["mouthpiece"] = (-0.405, -0.022)
    b.lathe([(-0.345, 0.010), (-0.33, 0.0105), (-0.06, 0.0125), (0.22, 0.0145), (0.25, 0.016), (0.30, 0.026), (0.335, 0.030),
             (0.37, 0.026), (0.395, 0.020), (0.405, 0.021)], WOOD)
    for u in (-0.33, -0.06, 0.25):
        b.joint(u, scale=1.12)
    b.rod(-0.29, 0.20)
    for key, u in (("L1", -0.235), ("L2", -0.195), ("L3", -0.155), ("R1", -0.005), ("R2", 0.035), ("R3", 0.075)):
        b.cup(key, u, 0.0072)
    for u in (-0.275, -0.115, 0.12, 0.15, 0.18):
        b.pad(u, 0.006)
    b.lever("octave", (-0.275, 0.010), (-0.285, 0.026))
    b.lever("thumb", (-0.25, 0.011), (-0.255, 0.027))
    return b

def bass_clarinet(origin, rotation, lit):
    # Bass clarinet in B-flat, about 1.1 m as drawn lying: mouthpiece on its bent metal neck at the left, the straight
    # body with the clarinet's keys, larger, the metal bell turned up at the right.
    b = Wind("bass clarinet", origin, rotation, 0.55, lit, z0=0.03)
    neck = [(-0.40, 0.0), (-0.45, 0.004), (-0.49, -0.012), (-0.505, -0.04)]
    b.pipe(neck, 0.0075, b.z0)
    tube(b.name("mouthpiece"), [(-0.505, -0.04, b.z0), (-0.51, -0.075, b.z0)], 0.011, EBONY, b.root)
    tube(b.name("ligature"), [(-0.506, -0.05, b.z0), (-0.507, -0.06, b.z0)], 0.0125, CHROME, b.root)
    b.points["mouthpiece"] = (-0.51, -0.074)
    b.lathe([(-0.40, 0.020), (-0.39, 0.021), (0.32, 0.021)], WOOD)
    for u in (-0.39, 0.0, 0.32):
        b.joint(u, scale=1.1)
    b.pipe([(0.32, 0.0), (0.38, 0.004), (0.42, 0.03)], 0.021, b.z0)
    b.points["bell"] = b.bell((0.42, 0.03), math.radians(62), 0.17, 0.021, 0.062, b.z0, power=2.6)
    b.rod(-0.33, 0.27)
    for key, u in (("L1", -0.27), ("L2", -0.22), ("L3", -0.17), ("R1", 0.05), ("R2", 0.10), ("R3", 0.15)):
        b.cup(key, u, 0.0105)
    for u in (-0.31, -0.12, 0.20, 0.24, 0.28):
        b.pad(u, 0.009)
    b.lever("register", (-0.30, 0.018), (-0.31, 0.04))
    b.lever("thumb", (-0.28, 0.019), (-0.27, 0.041))
    return b

def contrabassoon(origin, rotation, lit):
    # Contrabassoon, an octave under the bassoon: its 5.5 m of tube folded four times; drawn lying, fitted whole, as our
    # own simplified fold: the boot at the left, the wing joint and two long tubes side by side, the metal bell turned
    # down at the right, the bocal coiled out of the wing joint.
    b = Wind("contrabassoon", origin, rotation, 0.80, lit, z0=0.05)
    wing, mid, top = -0.040, 0.012, 0.056
    b.lathe([(-0.80, 0.044), (-0.50, 0.044)], WOOD, v=0.0)
    b.lathe([(-0.50, 0.018), (0.02, 0.020)], WOOD, v=wing)
    b.lathe([(-0.50, 0.021), (0.62, 0.023)], WOOD, v=mid)
    b.lathe([(-0.50, 0.024), (0.70, 0.026)], WOOD, v=top)
    for u in (-0.80, -0.50):
        b.ring((u, 0.0), (1, 0), 0.045, b.z0, width=0.0018)
    for u, v in ((0.02, wing), (0.62, mid), (0.70, top)):
        b.joint(u, v=v, scale=1.08)
    b.pipe([(0.62, mid), (0.66, mid + 0.004), (0.70, top - 0.004)], 0.012, b.z0 + 0.02)
    b.pipe([(0.70, top), (0.75, top + 0.002), (0.78, top - 0.02)], 0.026, b.z0)
    b.points["bell"] = b.bell((0.78, top - 0.02), math.radians(-80), 0.10, 0.026, 0.055, b.z0, power=2.4)
    bocal = [(0.02, wing), (0.07, wing - 0.006), (0.12, wing - 0.04), (0.13, wing - 0.085), (0.10, wing - 0.11), (0.06, wing - 0.10)]
    b.pipe(bocal, 0.0042, b.z0 + 0.016)
    tube(b.name("reed"), [(0.06, wing - 0.10, b.z0 + 0.016), (0.035, wing - 0.092, b.z0 + 0.016)], 0.0048, EBONY, b.root)
    b.points["mouthpiece"] = (0.037, wing - 0.093)
    for key, u in (("L1", -0.13), ("L2", -0.185), ("L3", -0.24)):
        b.cup(key, u, 0.0085, v=wing, ring_key=True)
    for key, u in (("R1", -0.58), ("R2", -0.635), ("R3", -0.69)):
        b.cup(key, u, 0.009, v=0.0, ring_key=True)
    b.rod(-0.35, 0.40, v=top)
    for u in (-0.08, -0.30, -0.38, 0.10, 0.30):
        b.pad(u, 0.0075, v=top)
    b.lever("thumb", (-0.15, wing - 0.016), (-0.16, wing - 0.04))
    b.lever("whisper", (-0.02, wing - 0.016), (-0.03, wing - 0.04))
    return b

WINDS = {"flute": flute, "piccolo": piccolo, "oboe": oboe, "clarinet": clarinet, "bassoon": bassoon,
         "englishhorn": english_horn, "bassclarinet": bass_clarinet, "contrabassoon": contrabassoon}
# extents in its own plane (u0, v0, u1, v1), for framing
EXTENT = {"flute": (-0.34, -0.03, 0.34, 0.03), "piccolo": (-0.165, -0.02, 0.165, 0.02), "oboe": (-0.34, -0.03, 0.32, 0.03),
          "clarinet": (-0.34, -0.035, 0.34, 0.035), "bassoon": (-0.675, -0.15, 0.675, 0.06),
          "englishhorn": (-0.415, -0.035, 0.41, 0.035), "bassclarinet": (-0.53, -0.085, 0.53, 0.20),
          "contrabassoon": (-0.805, -0.16, 0.81, 0.09)}

LIGHT = 0.2
RIM = 0.25
ROT = {k: 0.0 for k in WINDS}
def extent(k, rot):
    u0, v0, u1, v1 = EXTENT[k]
    if rot == 0.0: return u0, v0, u1, v1
    return -v1, u0, -v0, u1   # turned +90 degrees: (u, v) -> (-v, u)
if VIEW.startswith("single:"):
    kind = VIEW.split(":")[1]
    x0, y0, x1, y1 = extent(kind, ROT[kind])
    if kind in ("bassoon", "contrabassoon", "bassclarinet", "englishhorn"):
        across = max(x1 - x0, (y1 - y0) * (WIDTH / HEIGHT)) * 1.06
    else:
        across = float(os.environ.get("WOOD_ACROSS", "0.745"))
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    PX_PER_M = (WIDTH / 2) / across
    b = WINDS[kind]((-cx, -cy, 0), ROT[kind], kind in LIT)
    pieces = [(kind, b)]
else:
    # the bassoon lying across the bottom, the flute and piccolo above it, the oboe and clarinet between
    E = EXTENT
    gap = 0.06
    rows = [["flute", "piccolo"], ["oboe", "clarinet"], ["bassoon"]]
    widths = [sum(E[k][2] - E[k][0] for k in row) + gap * (len(row) - 1) for row in rows]
    heights = [max(E[k][3] - E[k][1] for k in row) for row in rows]
    total_w, total_h = max(widths), sum(heights) + 2 * 0.09
    PX_PER_M = min((HEIGHT / 2) * 0.82 / total_h, (WIDTH / 2) * 0.90 / total_w)
    pieces = []
    y = total_h / 2
    for row, w, h in zip(rows, widths, heights):
        y -= h
        x = -w / 2
        for k in row:
            pieces.append((k, WINDS[k]((x - E[k][0], y + (h - (E[k][3] - E[k][1])) / 2 - E[k][1], 0), 0.0, k in LIT)))
            x += E[k][2] - E[k][0] + gap
        y -= 0.09

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
metal_only = bpy.data.collections.new("woodwind receivers")
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

# where each key lands in the picture (px), for the code that lights a note's fingering on top
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
        geo["instruments"][k] = {"points": {n: px(root.matrix_world @ Vector((q[0], q[1], getattr(b, "heights", {}).get(n, 0.0)))) for n, q in b.points.items() if n != "slideAxis"},
                                 "area": [round(min(xs) - margin), round(min(ys) - margin), round(max(xs) + margin), round(max(ys) + margin)]}
        if "slide" in b.points:
            geo["instruments"][k]["slideEnd"] = px(root.matrix_world @ Vector((b.points["slide"][0] + 0.55, b.points["slide"][1], 0.0)))
    with open(GEOMETRY, "w") as f:
        json.dump(geo, f)

scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("RENDERED", OUT)
