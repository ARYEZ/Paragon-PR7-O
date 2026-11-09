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
| `orchestra.json` | Where each string's bridge and nut ends land (px), and each instrument's area: `single.<kind>`, `section.<kind>` |

## Making them again

Needs Blender 4.0 (`apt install blender`) and Python 3 with numpy and Pillow. With these scripts, in a scratch folder:

```bash
for k in violin viola cello bass; do blender -b -P strings3d.py -- $PWD/single-$k.png 64 2200 900 single:$k none $PWD/single-$k.json; done
blender -b -P strings3d.py -- $PWD/section.png 64 2200 900 section none $PWD/section.json
blender -b -P strings3d.py -- $PWD/section-lit.png 64 2200 900 section bass,cello,viola,violin $PWD/section-lit.json
python3 process_orchestra.py . <the repo>/resources/orchestra
```

- `strings3d.py`: the scene. Arguments: output, samples, width, height, view (`single:<kind>` or `section`), the
  instruments lit (`none`, or a list), the geometry file. Each about 2.5 minutes on 4 CPU cores at 64 samples.
- `process_orchestra.py`: every hue into the family's range (318 through 0 to 42 degrees: the tone mapping burns the
  brightest orange towards yellow and the bloom turns pink towards purple), JPEG, the geometry merged. JPEG still moves a
  few colours a few degrees, so `OrchestraView` pulls every hue into the range once more as it loads a picture.

The pictures are drawn filling their panel, centred (cropped at the edges at other window shapes, never stretched); the
geometry is mapped the same way.
