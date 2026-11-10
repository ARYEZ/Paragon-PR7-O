# THE STAGE (PR7-O 0.8.0): our own concert stage, seen from above and behind the conductor, in the PR7 family's "Tron"
# look (strings3d.py's materials, neon gradient, grid floor and bloom): the platform with its front curving towards the
# hall, three tiers of risers round the conductor, the podium, the back wall. Our own drawing (no hall's plan traced).
# The plugin draws each part's seat on it (OrchestraView's stage): the geometry file maps stage metres to pixels.
#
# blender -b -P stage3d.py -- <out.png> [samples] [width] [height] stage none [geometry.json]
#   metres: x across from the conductor's left (-9 to 9), y back from the stage's front (0 to 11.2), z up.
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

# ================================================================ THE STAGE (our own drawing; metres: x across from the
# conductor's left, y back from the stage's front, z up)
import math
def flat(o):
    for p in o.data.polygons:
        p.use_smooth = False
    return o
NEON = neon("stage neon", 7.0, 9.0)
NEON_DIM = neon("stage neon dim", 2.2, 9.0)
FLOORMAT = grid_floor(48.0 * 2)      # a line every metre (grid_floor's "px per metre" / 48)
TOP = grid_floor(48.0 * 4)           # the stage's own floor: a line every half metre
HALF = 8.6
def tier_height(y, x=0.0):
    for R, h in ((15.5, 0.6), (14.0, 0.4), (12.5, 0.2)):
        if y >= -6.0 + math.sqrt(max(0.0, R * R - x * x)):
            return h
    return 0.0
root = link(bpy.data.objects.new("stage", None))
# the platform: its front an arc bulging towards the hall, 0.8 m above the hall's floor
front = [(x, -1.4 * (1 - (x / 9.0) ** 2)) for x in [9.0 - 18.0 * i / 40 for i in range(41)]]
outline = front + [(-9.0, 11.2), (9.0, 11.2)]
outline = list(reversed(outline))   # counter-clockwise
flat(slab("platform", outline, -0.8, 0.0, TOP, root))
tube("front edge", [(x, y - 0.02, 0.0) for x, y in front], 0.035, NEON, root)
tube("front foot", [(x, y - 0.02, -0.8) for x, y in front], 0.02, NEON_DIM, root)
for sx in (-9.0, 9.0):
    tube("side edge", [(sx, 0.0, 0.0), (sx, 11.2, 0.0)], 0.03, NEON_DIM, root)
# the risers: three tiers, their fronts arcs round the conductor
for R, h in ((12.5, 0.2), (14.0, 0.4), (15.5, 0.6)):
    arc_pts = [(x, -6.0 + math.sqrt(R * R - x * x)) for x in [-HALF + 2 * HALF * i / 48 for i in range(49)]]
    poly = arc_pts + [(HALF, 11.2), (-HALF, 11.2)]
    poly = list(reversed(poly))
    flat(slab("riser %.1f" % R, poly, 0.0, h, LACQUER, root))
    tube("riser edge %.1f" % R, [(x, y - 0.01, h) for x, y in arc_pts], 0.028, NEON, root)
# the back wall (the shell), its foot and top lit
flat(slab("shell", [(-9.0, 11.2), (9.0, 11.2), (9.0, 11.5), (-9.0, 11.5)], 0.0, 3.2, EBONY, root))
tube("shell foot", [(-9.0, 11.18, 0.62), (9.0, 11.18, 0.62)], 0.025, NEON_DIM, root)
tube("shell top", [(-9.0, 11.18, 3.2), (9.0, 11.18, 3.2)], 0.03, NEON, root)
for x in [-9.0 + 18.0 * i / 8 for i in range(9)]:
    tube("shell rib", [(x, 11.18, 0.62), (x, 11.18, 3.2)], 0.012, NEON_DIM, root)
# the conductor's podium
slab("podium", [(-0.6, 0.3), (0.6, 0.3), (0.6, 1.3), (-0.6, 1.3)], 0.0, 0.25, LACQUER, root, bevel=0.03)
tube("podium edge", [(-0.6, 0.3, 0.25), (0.6, 0.3, 0.25), (0.6, 1.3, 0.25), (-0.6, 1.3, 0.25)], 0.02, NEON, root, closed=True)
# the hall's floor
bpy.ops.mesh.primitive_plane_add(size=60.0, location=(0, 0, -0.8))
assign(bpy.context.object, FLOORMAT)

world = bpy.data.worlds.new("dark")
scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = srgb("060407")
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.004

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
light("key", "AREA", (0, 3, 12), (0, 0, 0), 900, (1.0, 0.95, 0.96), size=14, size_y=8)
light("rim pink", "AREA", (-14, 5, 3), (0, math.radians(-80), 0), 700, srgb("ff3b6a"), size=12, size_y=2)
light("rim orange", "AREA", (14, 5, 3), (0, math.radians(80), 0), 700, srgb("ff5a2c"), size=12, size_y=2)
for n in ("key", "rim pink", "rim orange"):
    bpy.data.objects[n].visible_glossy = False

cam_data = bpy.data.cameras.new("camera")
cam_data.lens = 22
cam_data.sensor_width = 36
cam = link(bpy.data.objects.new("camera", cam_data))
cam.location = Vector((0.0, -6.5, 12.5))
direction = Vector((0.0, 4.5, -0.4)) - cam.location
cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
cam_data.clip_end = 100
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

if GEOMETRY:
    bpy.context.view_layer.update()
    def px(p):
        v = world_to_camera_view(scene, cam, Vector(p))
        return [round(v.x * WIDTH, 1), round((1 - v.y) * HEIGHT, 1)]
    grid = {}
    for i in range(37):
        for j in range(29):
            x, y = -9.0 + 0.5 * i, -2.0 + 0.5 * j
            grid["%d,%d" % (i, j)] = px((x, y, tier_height(y, x) if y > 0 else 0.0))
    json.dump({"width": WIDTH, "height": HEIGHT, "grid": grid, "mic": px((0.0, -0.5, 3.0)), "podium": px((0.0, 0.8, 0.25))}, open(GEOMETRY, "w"))
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("RENDERED", OUT)
