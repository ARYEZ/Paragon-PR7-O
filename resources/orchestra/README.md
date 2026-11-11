# THE ORCHESTRA VIEW's pictures (PR7-O 0.5.0)

Our own string family (violin, viola, cello, double bass), modelled and rendered for PR7-O in the PR7 family's "Tron"
look, as PR7-D's kit (`resources/kit` in PR7-D) and PR7-I's guitar (`resources/guitar` in PR7-I) are: the same
materials, neon gradient (pink at the low string's side, red, orange at the high string's), gloss floor with a red grid,
lights and bloom as PR7-I's `guitar3d.py`. Built into the plugin as `OrchestraImages::` binary data (see
`CMakeLists.txt`) and drawn by `source/ui/OrchestraView.cpp`. The outline is our own drawing of the family's shape (real
proportions; the bass with sloping shoulders); no maker's instrument traced.

| File | What |
|---|---|
| `violin.jpg`, `viola.jpg`, `cello.jpg`, `bass.jpg` | ONE INSTRUMENT: it lies across, scroll right, low string on top; 2200 x 900 |
| `section.jpg` | THE SECTION: bass, cello, viola, violin standing, low to high, at their real sizes; 2200 x 900 |
| `section-lit.jpg` | The same, every instrument's neon 4x brighter (a part's area is faded in over `section.jpg` while it plays) |
| `trumpet.jpg`, `horn.jpg`, `trombone.jpg`, `tuba.jpg` | 0.6.0, THE BRASS (`brass3d.py`): one lying across, bell right; 2200 x 900 |
| `brass-section.jpg` | The tuba standing at the left, the trombone across the top right, the horn and trumpet under it; 2200 x 900 |
| `brass-section-lit.jpg` | The same, the neon brighter (faded in per part, as `section-lit.jpg`) |
| `flute.jpg`, `piccolo.jpg`, `oboe.jpg`, `englishhorn.jpg`, `clarinet.jpg`, `bassclarinet.jpg`, `bassoon.jpg`, `contrabassoon.jpg` | 0.12.0, THE WOODWINDS (`woodwinds3d.py`): one lying across, mouthpiece left; 2200 x 900 |
| `woodwind-section.jpg` | Flute and piccolo on top, oboe and clarinet under them, the bassoon across the bottom; 2200 x 900 |
| `woodwind-section-lit.jpg` | The same, the neon brighter (faded in per part) |
| `stage.jpg` | 0.8.0, THE STAGE (`stage3d.py`): our concert stage seen from above and behind the conductor; 0.14.0: 3200 x 920, from a lower camera further back so it spreads across the page's full-width panel |
| `orchestra.json` | Where each string's bridge and nut ends land (px), each brass instrument's valves (`valves`, valve 1 first; the horn's thumb valve last) and the trombone's hand at 1st and 7th position (`slide`), and each instrument's area: `single.<kind>`, `section.<kind>`, `brassSection.<kind>`; 0.12.0: each woodwind's `keys` (thumb, L1-L3, R1-R3, then its octave / register / whisper / E-flat key) in `single.<kind>` and `woodwindSection.<kind>`; `stage`: a grid of picture points every 0.5 m of the stage (x from -9, y from -2; 37 x 29) and the podium |

## Making them again

Needs Blender 4.0 (`apt install blender`) and Python 3 with numpy and Pillow. With these scripts, in a scratch folder:

```bash
for k in violin viola cello bass; do blender -b -P strings3d.py -- $PWD/single-$k.png 64 2200 900 single:$k none $PWD/single-$k.json; done
blender -b -P strings3d.py -- $PWD/section.png 64 2200 900 section none $PWD/section.json
blender -b -P strings3d.py -- $PWD/section-lit.png 64 2200 900 section bass,cello,viola,violin $PWD/section-lit.json
python3 process_orchestra.py . <the repo>/resources/orchestra
```

The brass (0.6.0), in another scratch folder, then both folders together:

```bash
for k in trumpet trombone horn tuba; do blender -b -P brass3d.py -- $PWD/single-$k.png 64 2200 900 single:$k none $PWD/single-$k.json; done
blender -b -P brass3d.py -- $PWD/section.png 64 2200 900 section none $PWD/section.json
blender -b -P brass3d.py -- $PWD/section-lit.png 64 2200 900 section tuba,trombone,horn,trumpet $PWD/section-lit.json
python3 process_orchestra.py <the strings' folder> <the repo>/resources/orchestra <the brass's folder>
```

- `brass3d.py`: the brass's scene, the same arguments; strings3d.py's materials, floor, lights and bloom (copied in),
  satin gunmetal tubes (metal shows only what it reflects: two extra lights reach the instruments alone through Cycles'
  light linking, so the floor stays black), a neon line along each tube (pink at the mouthpiece end, orange at the bell),
  bright bell rims, neon rings on the valves. Our own side views at real sizes. Trumpet, horn and trombone at one scale
  (0.745 m across: the geometric middle of the trumpet filling the picture and the trombone; the trombone framed on its
  bell end), the tuba whole. Under 2 minutes each at 64 samples.

- `strings3d.py`: the scene. Arguments: output, samples, width, height, view (`single:<kind>` or `section`), the
  instruments lit (`none`, or a list), the geometry file. Each about 2.5 minutes on 4 CPU cores at 64 samples.
- `process_orchestra.py`: every hue into the family's range (318 through 0 to 42 degrees: the tone mapping burns the
  brightest orange towards yellow and the bloom turns pink towards purple), JPEG, the geometry merged. JPEG still moves a
  few colours a few degrees, so `OrchestraView` pulls every hue into the range once more as it loads a picture.

The pictures are drawn filling their panel, centred (cropped at the edges at other window shapes, never stretched); the
geometry is mapped the same way.

The stage (0.8.0), in a third scratch folder:

```bash
blender -b -P stage3d.py -- $PWD/stage.png 64 3200 920 stage none $PWD/stage.json   # (0.14.0; 0.8.0's: 2048 842)
python3 process_orchestra.py <the strings' folder> <the repo>/resources/orchestra - <the stage's folder>
```

- `stage3d.py`: our own stage (no hall's plan traced): the platform with its front curving towards the hall, three
  tiers of risers round a point 6 m in front of it, the podium, the back wall, in metres (x across from the conductor's
  left, y back from the front, z up). The geometry file maps stage metres to the picture's pixels; the plugin draws each
  part's seat through it (`StageView`) and hears it from the same metres (`dsp/stage/Stage`).

The woodwinds (0.12.0), in a fourth scratch folder; "-" keeps the strings', brass's and stage's pictures as they are:

```bash
for k in flute piccolo oboe englishhorn clarinet bassclarinet bassoon contrabassoon; do blender -b -P woodwinds3d.py -- $PWD/single-$k.png 64 2200 900 single:$k none $PWD/single-$k.json; done
blender -b -P woodwinds3d.py -- $PWD/section.png 64 2200 900 section none $PWD/section.json
blender -b -P woodwinds3d.py -- $PWD/section-lit.png 64 2200 900 section flute,piccolo,oboe,clarinet,bassoon $PWD/section-lit.json
python3 process_orchestra.py - <the repo>/resources/orchestra - - $PWD
```

- `woodwinds3d.py`: brass3d.py's materials, floor, lights, camera and bloom (copied in); black wood bodies (the flute's
  gunmetal) with neon along both edges, pink at the mouthpiece to orange at the bell, neon joint rings, gunmetal keys
  whose cups face the camera with neon rims; each key's position (at its top) into the geometry. Our own simplified
  side views at real sizes (the contrabassoon's folds our own simplification). Flute, piccolo, oboe and clarinet at the
  brass's common scale (0.745 m across); the English horn, bass clarinet, bassoon and contrabassoon whole.
