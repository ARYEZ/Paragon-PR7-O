# Paragon PR7-O: notes for Claude

This repository is **Paragon PR7-O**, the **orchestra instrument** of the PR7 family: a VST3 instrument that plays
sampled orchestral instruments from open libraries (plain WAV + SFZ on the user's disk), and in later stages *writes*
orchestral parts for heavy music. Seeded from **PR7-D 0.70.0** (commit ed3eb4e, full history: PR7-D's own notes for
Claude are in that commit's CLAUDE.md and in `ARYEZ/Paragon-PR7-D`). **Read `PR7-O PLAN.md`**: it's the plan agreed with
the owner, a release per stage. Siblings: PR7-G (guitar), PR7-B (bass), PR7-V (vocal), PR7-C, PR7-E (utility), PR7-K
(synth), PR7-D (drums); the owner's DAW is PR7-S (its own repo).

## Stage 1 done in 0.1.0: its own plugin

- **Identity:** `PRODUCT_NAME` "Paragon PR7-O", `BUNDLE_ID` `com.paragon.paragonpr7o`, `PLUGIN_CODE` **Pr7o** (never a
  sibling's: Px1a, Pr7b, Pr7v, Pr7c, Pr7e, Pr7k, Pr7d), `VST3_CATEGORIES Instrument Sampler`, one stereo output (the
  CMake target stays `ParagonX1`). Presets folder "Paragon PR7-O" (`RigLibrary.cpp`), settings file "Paragon PR7-O"
  (`Finish.cpp`), logo "-O" (`Logo.cpp`: `addO`, the P's bowl bevels on all four corners, a counter hole; the icon
  "7O"), "PR7-O SERIES" faceplates, CI release job on (`.github/workflows/build.yml`), CHANGELOG from 0.1.0.
- **Taken out:** the drums (`Drums.*`, sequencer, note map, articulations, MIDI IN, velocity curves, layers, MicSim,
  DrumGizmo, sample / mixed packs, render import), grooves (`source/grooves/`), the song track (`source/song/`), the
  kit view and its renders (`resources/kit/`), the drum machine, the drum MIXER page, PR7-D's 16 outputs and every
  drum / mixer / sequencer parameter; their tests.
- **Kept:** the FX page and every effect (PR7-E's rack, PR7-G's sends / MOD / LO-FI / insert buses), presets, undo,
  MIDI learn, the default preset, look-and-feel, progress box, PaintCache, the test set-up. **Kept but unused yet**
  (compiled and tested, for later stages): `dsp/mixer/Mixer.h` (trimmed to the generic strip / bus / send / effect-slot
  pieces: the drum-mic channels, bleed and mic routing are gone; 16 channels + 8 buses) and `dsp/mixer/MixerEffects.*`
  (stage 6's per-section strips); `dsp/sampler/DiskStreamer.*` (namespace `sampler`; stage 3's streaming).
  0.1.0 fixed a race in `Streamer::sample`'s wait-for-disk path (a stale `written` after `fill` returned false because
  the disk thread had filled the ring: silence, about 1 run in 3 of its test).

## The code (0.1.0)

- **`dsp/sampler/Sample.*`**: `sampler::Sample` (float channels at the file's rate), `sampleFiles::load` (whole file,
  up to 600 s; JUCE's basic formats).
- **`dsp/sampler/Sfz.*`**: `Region` (sample; keys, root, velocities, channels; seq_length / seq_position; lo / hirand;
  keytrack, tune cents (tune + 100 x transpose); gain (volume x amplitude); pan; velTrack; offset / end; DAHDSR in
  seconds, sustain 0-1; oneShot; bend up / down cents), `Program` (regions, `byKey` index, `problems`, `unread`
  ("opcode (n)" counts; `loop_mode=` values other than one_shot / no_loop listed with their value), bytes, numFiles,
  `mutable sequence` round-robin counters written only by the sampler playing it; `lowestKey` / `highestKey`,
  `velocityLayersAt`, `roundRobinsAt`, `index()`), `sfz::load` (PR7-D's `preprocess` / `parse`, `<control>`
  default_path / note_offset / octave_offset, backslashes to slashes, each file loaded once with `progress`;
  trigger != attack and `*` generators skipped and listed; names, labels, hints, `group`, `output`, polyphony don't
  count as unread), `sfz::noteNumber` (c4 = 60), `programFrom (sample, keyCenter)`.
- **`dsp/sampler/Sampler.*`**: `Sampler` (256 slots, POLYPHONY up to 192; `render (program, left, right, n, midi,
  midiStart, Settings)` adds into the buffers; note on: regions by key, `covers`, the round-robin count per region,
  one random number per note; Hermite read (exact at t = 0); velocity gain `1 - vt + vt (v/127)^2`; balance pan;
  DAHDSR (attack linear, decay -60 dB over its time towards sustain, release -80 dB over its time x RELEASE, never
  under 5 ms); sustain pedal, pitch wheel (`updatePitch`), CC 120 / 123; polyphony: the oldest (released first) fades
  10 ms; a new program fades all voices 20 ms; Settings' LEVEL / PAN ramped across each piece; `isKeyHeld`,
  `getActiveVoices`, `getLastNote`).
- **Processor (`PluginProcessor.*`)**: one stereo bus. `processBlock`: host bpm / ppq, MIDI learn, the keyboard
  state merged, `processChunk` in prepared-size pieces: the sampler into `instrumentBuffer`, then into the INSERT
  BUS's buffer or MAIN + the sends (`sendDelayGain` / `sendReverbGain` smoothed, only while their effect is on), then
  PR7-D's chain unchanged: insert buses, sends, MOD / LO-FI, the rack, OUTPUT x preset LEVEL, LIMITER, BYPASS, meter.
  The instrument: `loadInstrument` / `startLoading (file, undoStep)` (a 1-thread `ThreadPool`, `loadGeneration`
  makes older loads stale, `loadProgress`, `cancelLoad`; the result installed on the message thread by
  `installLoaded`; a failed load keeps its file with no sound and `getLoadProblem`), `clearInstrument`, `setProgram`
  (tests), `installProgram` (retired programs kept until the audio thread has moved on and 2 s have passed),
  `waitForInstrument`. `instrumentFile` counts as the preset's at once (saved, undone) while the audio follows.
  State: parameters + rig name / file / modified + "Instrument" (file) + MIDI links; `parameterVersion` 1.
  The timer keeps `notenames::middleCOctave` in step with NOTE NAMES.
- **Parameters (`Parameters.*`)**: global `outputGain`, `bypass`, `midiChannel`, `noteNames` (and the LIMITER's
  UNITY / DELTA) are `notInRigs`; in presets `patchLevel`, `instLevel` (-60..12 dB, -60 silent), `instPan`,
  `instTranspose`, `instTune`, `instRelease` (x0.1-10, log), `instVoices`, `instSendDelay` / `instSendReverb` (0 by
  default), `instInsert` (`insertBusNames`), and every effect's (PR7-E / PR7-G IDs, ranges, defaults and hints kept).
  PR7-O's own parameters start at **version hint 42** (`pr7o010`).
- **Presets (`rigs/`)**: `Rig::instrument` (the SFZ's full path; XML `<Instrument file="..."/>`), `parameterVersion` 1
  = PR7-O 0.1.0; no ModMatrix. Factory presets: A01 Init only (no instrument). Save As from Init doesn't take Init's
  tags or notes.
- **UI**: `MainView` (tabs INSTRUMENT, FX, PRESETS, SETTINGS: `Page::instrument, fx, presets, settings`, `tabEdges`;
  the keyboard strip with SHOW / HIDE KEYBOARD at OUTPUT's foot: `heightFor`, `onHeightChanged`, shown when a
  window opens, not saved; the progress box after a load runs 0.5 s). `ui/InstrumentPage.*`: `InstrumentKeyboard`
  (21-108, program keys tinted through `setFill`), `RegionMap` (boxes by key and velocity, `velocityColour`, counts,
  lit columns; keys 21-108 widened to the program's), `InstrumentPage` (LOAD... / drop, CLEAR, name, file, info text,
  HOW IT PLAYS, WHERE IT GOES). SETTINGS (`DockPanels.*`): MIDI (channel, note names), THE SOUNDS, PRESETS, MIDI LINKS,
  ABOUT. FX page: `SendMatrix` is the instrument's two sends + DELAY > REVERB; `InsertBusStrip`'s THE INSTRUMENT
  button routes it. Icon `Icons::Type::orchestra` (a violin).

## Stage 2 done in 0.2.0: VSCO 2 CE's strings (the owner: "Let's start VSCO 2 CE, strings first")

The owner chose **VSCO 2 Community Edition** (Versilian Studios; CC0, asks for credit and that the samples aren't sold
on their own) and **strings first**. The library was read from `github.com/sgossner/VSCO-2-CE` (branch `SFZ`: 75
instruments at its top level, samples under `Strings/...`, 44.1 kHz stereo 16-bit) into the scratchpad, never
committed. What its 31 string files use (counted): sample, lokey / hikey / pitch_keycenter, lovel / hivel, volume (+20 dB
on most regions: the recordings peak -19 to -41 dBFS), ampeg_attack / release / dynamic, seq / rand round robins on
shorts, tune, group_label, a `<control> default_path` per articulation, and keyswitches in the 5 `-KS` files (violins,
violas, solo violin at c2-d#2 = notes 36-39; cellos c6-d#6 and basses c6-e6 = 84-88, above their ranges). **No loops,
no release samples, no xfin / xfout.** Sustains are 7-17 s, steady, ending in a ~1 s bow-off. So stage 2 became:
- `Sfz.*`: `<control>` opcodes positional (each region carries "control.<name>": the 0.1.0 reader kept only the last
  default_path); keyswitches (`Region::swLoLast / swHiLast`, `playsUnder`; `Program::swLoKey / swHiKey / swDefault`,
  `articulations` {lo, hi, label}, `isKeyswitch`, `articulationOf`); loops (`Region::Loop` none / continuous / sustain,
  `loopStart / loopEnd` frames, `loopCrossfade` frames, `loopLikeness`); `held` and AUTO SUSTAIN points
  (`prepareRegions`, called by `sfz::load` and `programFrom`: tests that build regions by hand call it);
  `isSustained` (peak ≤ 13 dB over the median of the middle 3/5, the middle's first quarter ≤ 9 dB over its last;
  fitted on VSCO: 3 of 431 regions misjudged, all contrabass) + longer than 2.5 s + not one_shot = held;
  `findSustain` (levels in 50 ms windows; steady = within 3 dB of the body, +0.3 s / -0.1 s; loop up to 4 s, 80% of
  it; crossfade up to 1 s, 40% of the loop; start nudged ±25 ms (coarse 4 frames, then 1) to the best correlation of a
  40 ms window at the crossfade's middle; `loopLikeness` = the correlation over the whole crossfade);
  `velocityLayersAt / roundRobinsAt (key, keyswitch)`.
- `Sampler.*`: voices on the heap (`std::vector`, 1024 slots; `noteScratch` / `idScratch` preallocated: nothing big on
  the audio thread's stack); keyswitch state (program change: `swDefault`, else the first articulation (our choice);
  `chooseKeyswitch` from the page via `pendingSwitch`); loops read two positions in the crossfade with weights
  (1-t, t) / sqrt (w1² + w2² + 2 r w1 w2), r = the loop's likeness (linear when alike, equal power when not); a sustain
  loop stops at note-off (`stopAtWrap` inside its crossfade); `Settings::dynamics` (velocity / modWheel) and
  `autoSustain`; MOD WHEEL: held regions ignore velocity at note-on, all their layers start, each voice's `wheelGain` =
  sqrt share between neighbouring layers' middles (in velocity) x the velocity curve at the wheel's value, ramped
  across each piece; CC 11 squared, with LEVEL / PAN; POLYPHONY in notes (`noteId`), `makeRoom`: oldest released note,
  else the oldest held that isn't the lowest / highest of 3+ held keys; `getActiveNotes`, `getModWheel`,
  `getExpression`, `getKeyswitch`.
- Parameters (hint 43, `pr7o020`): `instDynamics` (`dynamicsNames` Velocity / Mod Wheel), `instAutoSustain` (on);
  POLYPHONY reads in notes. The processor keeps the chosen keyswitch in the project ("Instrument" child's
  `keyswitch`; applied once its instrument is installed: `pendingKeyswitch`).
- UI: the INSTRUMENT page's ARTICULATIONS buttons (`articulationName`: a label starting with its key's SFZ name gets
  the plugin's name, following NOTE NAMES), info per articulation (`updateInfo`), HELD / LOOPS / AUTO SUSTAIN counts,
  DYNAMICS and AUTO SUSTAIN controls; `InstrumentKeyboard::markSwitch` (keyswitch keys familyOrange, the chosen
  0.95, others 0.4); `RegionMap` filtered to the chosen articulation (`rebuild` on a switch).
- Checked on the real files (temporary probes, not committed): all 31 load, 0 problems, 0 unread; AUTO SUSTAIN's
  level over 12-24 s of looping vs the recording's own steady part on 64 notes of 5 instruments: the same spread
  (median 4-6 dB, the bowing's swell). Demo WAVs rendered for the owner. Not checked: listening.

## Stage 2 continued in 0.3.0: the rest of VSCO 2 CE (the owner: "Let's do the rest of VSCO")

The other 44 files (woodwinds, brass, organ, timpani + rolls, harp, pianos, mallets, bells, GM-StylePerc) use no opcode
the strings didn't (counted): all 75 load, 0 problems, 0 unread (paths with spaces fine). The work was the held rule
and AUTO SUSTAIN (`Sfz.cpp`), fitted on all 1914 distinct recordings (outside GM-StylePerc) against their file names:
- `levelsOf`: the **core** (first to last 50 ms window within 6 dB of the median of the middle 3/5) and `body` = the
  core's median (used by `findSustain` too). `isSustained`: on the core, peak ≤ 18 dB over body, first quarter ≤ 9 dB
  over last, core ≥ 0.8 s (plus > 2.5 s, not one_shot). 7 misjudged (0.2.0's rule: 41; 4 are TrumpetSusVib notes that
  really die away). Bass drum "rub", bowed cymbal, rolls, ratchets count as held; 6 kit sounds held but unlooped (swells).
- `findSustain`: the steady stretch is the longest without a **burst** (0.2 s level > body + 6 dB: VSCO's quiet organ);
  `minimumSteady` 1 s (was 1.5); the start searched ±0.1 s (was 25 ms): every peak of the 40 ms snippet match (> 0.5,
  coarse steps of 8, refined) judged by the whole crossfade (every 4th frame), the snippet's best if none; the
  crossfade the longest of 1, 1/2, 1/4, 1/8 s with likeness ≥ 0.7, else the longest (pure tones drift out of phase
  over 1 s: VSCO's piccolo); `tilt` (dB, ±`maximumTilt` 6) = level over the crossfade's length before the start less
  before the end. Load time: Flute-KS ~1 s (was 0.4).
- `Region::loopTilt` (AUTO SUSTAIN loops only); `Sampler`: the main read times `tiltGain` (exp (tiltK x frames past the
  loop start), 1 before it, `tiltEnd` past its end: no step at note-off), incremental (`tiltStep` per sample, `exp` per
  block and per wrap); the crossfade's back read untilted.
- Checked on the real files (probes in the scratchpad, not committed): 19 sustain instruments, 9-21 notes each held 25
  s: looping level spread ≤ the recording's own (strings 4-6 dB, brass 1.5-2.2, woodwinds 0.6-2.9, organ 1-3, rolls
  3.3, piccolo 2.9). Demo WAVs rendered for the owner. Not checked: listening.

## 0.4.0: SECTIONS (the owner: "Split let's build the all strings section")

- **`dsp/sampler/Section.*`** (namespace `sampler::section`): `Section { parts: Part { file, low, high } }`, kept as
  text (`toText`: `<Section><Part file low high/>...</Section>`) wherever an instrument's path goes: `instrumentFile`,
  `Rig::instrument`, the state's "Instrument" `file`, undo. `isSection`, `fromText`, `fromFiles` (parts by lowest key;
  each from its lowest to the key under the next's lowest, the top to its highest; read with
  `sfz::LoadOptions::audio = false`), `nameOf`, `describe` / `folderOf` (an instrument string, file or section),
  `load` (each part via `sfz::load` with `loKey / hiKey` (regions cut to them, the rest's samples never read) and
  `reuse`; problems / unread prefixed with the part's name; `kindOf` (sustain / sustain non-vibrato / tremolo / legato /
  marcato / spiccato / staccato / pizzicato, else the label without its key name) matches articulations; switches from
  the first C over the top note (under the lowest if past C8: `base`), default Sustain; a part's region copied once per
  section switch it plays under (its own kind, a sustain falling back to its other sustain); parts without switches
  play under all.
- `sfz::LoadOptions { loKey, hiKey, audio, reuse }`; `prepareRegions` caches each sample's shape and AUTO SUSTAIN points
  by sample (`analyses`, weak pointers) so reused samples aren't analysed again. The processor's loader passes the
  current program's samples as `reuse` for every load (files too); `loadSection`, `getSection`.
- UI: `InstrumentPage` NEW SECTION... (multi-select), several `.sfz` dropped, `SplitRow`s (name, keys, - / + from the
  second part: `moveSplit (part, semitones)`, each part ≥ 1 key; `editedSection` applied 0.3 s after the last click by
  the timer, `applyPendingSplits`); `InstrumentKeyboard::tintOf` (odd parts at 0.4); `RigBrowser` names a section's
  preset by its parts. Snapshot: `PARAGON_SNAPSHOT_SECTION=<file>,<file>...`.
- On VSCO's four string KS files: splits 24-35 / 36-47 / 48-54 / 55-86, switches 96-100 (C7-E7 SFZ), 450 MB, load
  2.3 s warm, a split moved 0.8 s with nothing read. A full chord peaks over 0 dBFS (+20 dB per file).

## 0.5.0: the INSTRUMENT page's "Tron" look and PR7-I's rack (the owner: "continue the concept we have used in both the PR7-D and PR7-I with the TRON style instruments"; then, after two mock-ups: "Build both ...only change is for the knob and button menu let's use the rack system from PR7-I")

0.4.0 was never released (CI failed: a Windows-only test path check, then the LIMITER crash below); 0.5.0 carries it.
- **The look** (as PR7-D's kit view and PR7-I's GuitarView): our own violin, viola, cello and double bass modelled
  and rendered in Blender 4.0 (`resources/orchestra/strings3d.py`: PR7-I's `guitar3d.py` materials, neon gradient
  pink `ff2d6e` / red `ff2a2e` / orange `ff6a24` across each instrument (low string's side pink), `c0102c` grid floor,
  lights, Filmic, fog glow), `process_orchestra.py` (hues into 322-38, every pixel with any colour, JPEG, geometry merged), README there.
  `OrchestraImages` binary data: `violin/viola/cello/bass.jpg` (one lying across, 2200 x 900), `section.jpg` /
  `section-lit.jpg` (all four standing, real sizes), `orchestra.json` (strings' bridge / nut px, each area).
- **`ui/OrchestraView.*`**: `orchestra::kindOf` (from names; bassoon, trombone, tuba, drums... excluded),
  `openStrings`, `stopsFor` (our rule: highest free open string at or under the note, one note a string, under the
  lowest string its lowest open, at most 28 semitones up); `Mode` single (the program's name) / section (parts named as
  strings: lit areas faded in by `glow`, 1/8 a tick at 30 Hz down) / none (region map). The picture fills the view,
  centred (cropped, never stretched; the geometry through the same transform). Notes: the sounding part of the string
  lit (three widths + a pearl core) and a dot (radius ≥ 6.5 px); keyswitch keys left out; + TRANSPOSE. Lit areas are
  prepared once: widened and their edges feathered over 70 px (no seams). Pictures' hues pulled into 322-38 again on
  load (JPEG; exactly 318 / 42 rounds past the range).
- **`ui/InstrumentRack.*`** (PR7-I's `GuitarRack`, same parts): badge "ORCHESTRA INSTRUMENT", PAGE keys PLAYING /
  ROUTING, the screen (`Screen` caption / name / corner / line1 / line2), the INSTRUMENT keys `loadKey`, `sectionKey`,
  `clearKey`, `knobCell` (6), `menuCell` (DYNAMICS / INSERT BUS on a painted screen), `pageKeyCell` (AUTO SUSTAIN),
  `routingKnobCell`; no OUTPUT column (the window's stays). `preferredHeight` 236. Ported from PR7-I: `PedalBody.*`,
  `SynthControls.*` (`setRack`, `setLabelBelow`, `knobForChoices`).
- **`InstrumentPage`**: the rack across the top (no head panel, no HOW IT PLAYS / WHERE IT GOES panels); left panel as
  before; the picture panel: `OrchestraView` or `RegionMap`, `chooseRegions`, INSTRUMENT / REGIONS buttons (shown only
  when there's a picture), the title painted over it (`paintOverChildren`); `updateScreen` each tick. Snapshot:
  `PARAGON_SNAPSHOT_ROUTING=1`, `PARAGON_SNAPSHOT_REGIONS=1`. The `[colours]` test scans each picture too.
- **The LIMITER crash** (PR7-D's `MasterLimiter`, so PR7-D and PR7-E have it too: port back): `getLatencySamples`
  read `oversamplers[...]`, replaced by `prepare()` on the host's thread and null before it; the processor's timer asks
  on the message thread (`updateLatency`). pluginval's Fuzz parameters crashed ~1 run in 8 (caught under gdb). Now
  `oversamplingLatency()` measures each factor once (after `initProcessing`, which sets the whole-sample latency).
- Blender 4.0 isn't on a fresh machine: `apt-get install -y blender`. Each picture ~2.5 min on 4 cores at 64 samples.

## 0.5.1: the single instruments at one common scale (the owner: "much closer to the scale that we used for the guitar in the PR7-I ... We don't need to see the entire body or the entire headstock"; after a mock-up: "all four instruments to one common scale"; of three, "C: halfway")

- `strings3d.py` single views: `ORCH_FRAME` common (default) / strings (each fills: the first mock-up) / whole
  (0.5.0's). Common: 0.654 m across the picture, the geometric middle of the violin's strings x1.22 and the bass's
  x1.05 (`ORCH_FIT`, `ORCH_SPAN` to choose another); an instrument whose strings are longer than 95% of it is framed
  on its nut end (the nut at 92% across), others centred on their strings. The nut now as high as the fingerboard's
  body end (`zFbNut = zFbEnd`: the strings had sunk into the board near the nut, hidden). All six pictures rendered again.
- `OrchestraView::paintStops`: a stop past the picture's left edge (cello / bass upper notes) lights 14 px in from it.

## The plan (`PR7-O PLAN.md`), next

Still from stage 2: release samples (`trigger=release`, `rt_decay`) and the files' own crossfades (`xfin_*` /
`xfout_*`, `amp_velcurve`), for libraries that use them (VSCO uses neither). Sections are split only (0.4.0; layered
was offered too: not asked for). Then (ask the owner): stage 3's library browser by
section and disk streaming (DiskStreamer: VSCO's string keyswitch files take 190-300 MB each in memory), Sonatina /
VPO, stage 4's CC / channel articulations, stage 5 legato, stage 6 the stage and mixer strips (dsp/mixer).

## Rules (from the owner and the family's CLAUDE.md files)

- **Libraries:** never EastWest / Hollywood Orchestra files or renders (its licence forbids using its audio for
  another instrument) unless EastWest grants written permission; never read Kontakt, Spitfire, Orchestral Tools or any
  other locked format, never get round copy protection. Only plain WAV / SFZ (open libraries, or ones the owner buys as
  plain files), read from the user's disk. **Sample libraries are never committed**: tests use generated tones and small
  invented SFZ files written by the test.
- **Names:** our own designs; no other product's names or branding on the plugin.
- **Colours: the PR7 family's only**: the red / pink / orange range (hue about 318° through 0° to 42°), pearl white
  and onyx. Draw the accent through `ParagonColours::setFill` (lettering through `setTextFill`), never
  `g.setColour (accent)`. `UiTests`' `[colours]` test checks the look-and-feel, the palette, the region map's
  colours and every pixel of the INSTRUMENT page with an instrument.
- Ask for source material (example SFZ files, libraries' layouts, riffs) rather than invent specifics; say when
  something is a guess or our own design; say what can't be checked here (listening).
- **UI changes: a mock-up first**, approved by the owner, as PR7-D's kit and PR7-I's guitar were (0.5.0: two
  mock-ups, "build both").

## The project

- AGPLv3, JUCE VST3, Windows + Linux in CI (macOS left out: GitHub stopped giving its job a machine).
- **Code layout:** `source/dsp/sampler/` (the instrument), `source/dsp/mixer/` (kept for stage 6), the effects in
  `source/dsp/effects/`, `dsp/eq/`, `dsp/dominion/`, the rack in `source/Rack.h` (`fx::numSends`, `fx::numInsertBuses`),
  UI in `source/ui/`, parameters in `source/Parameters.*` and `source/RackParameters.cpp`, presets ("rigs") in
  `source/rigs/`, tests in `tests/`.
- **Parameters** are saved by ID and choices by **index**: only ever *append* choices (`insertBusNames`,
  `midiChannelNames`, `notenames::choiceNames`, the effects' lists). New parameters get a new version hint. Migrations
  for changed defaults go in `setStateInformation` and `Rig::fromXml`.
- **The effects** came from PR7-E 0.18.1 (ECHELON, DOMINION, CONSTRICTOR, LIMITER) and PR7-G 0.52.0 (DELAY, REVERB,
  MOD, LO-FI, DRIVE, GLITCH) through PR7-K and PR7-D, with PR7-D's speed-ups (the true-peak detector, the compressor's
  fixed release: port back to PR7-E when syncing). When PR7-E or PR7-G changes these effects, port the change.

## Build, test, check

- **Build:** `cmake -B build && cmake --build build -j8` (on a fresh Linux machine, install the packages listed in
  `.github/workflows/build.yml` first).
- **Tests (Catch2):** `xvfb-run -a build/tests/ParagonX1Tests`. All must pass. Windows gives a thread 1 MB of stack: keep
  big data on the heap (`tests/StackSizeTests.cpp`); before a release run the tests once under `ulimit -s 1024`. New
  features get tests that check what they do to the sound (levels, timing to the sample, frequencies).
  - `tests/PluginTestUtils.h`: `testkit::sine`, `saw`, `useSample` (a one-region instrument rooted at 60, amp_veltrack
    0: note 60 plays it sample for sample), `useTones` (a region per key with its own pan / level), `keyFor
    (semitones)`, `writeWav`. `tests/TestUtils.h`: `levelAt`, `rms`, `frequencyOf`, `Spectrum`...
  - Notes in tests go in as MIDI at a sample position, not through the keyboard state (it spreads presses by the clock).
  - `tests/SamplerTests.cpp`: the SFZ reader and the sampler on invented files; also the kept MixerEffects and
    DiskStreamer.
- **pluginval:** `xvfb-run -a <pluginval> --strictness-level 10 --validate-in-process --validate
  "build/ParagonX1_artefacts/Release/VST3/Paragon PR7-O.vst3"`. Check its exit code. Linux pluginval sometimes crashes
  in its own host (its event loop's handler map, `registerEventHandler`, no plugin code on any thread): after SUCCESS,
  or while it deletes the plugin after its last test, before SUCCESS (about 1 run in 16-20 here). 0.5.0: CI makes a
  core dump and gdb reads it; only that crash gets another try (see the workflow). To hunt a crash locally: run
  pluginval in a loop with `ulimit -c unlimited` (a core in the folder it ran from; gdb's own slowness hides races:
  24 runs under gdb never crashed) and `gdb -batch -ex "thread apply all bt 40" pluginval core`: frames in
  "Paragon PR7-O.so" mean it's ours.
- **Screenshots** via `xvfb-run -a build/tests/ParagonX1Tests "[snapshot]"` with `PARAGON_SNAPSHOT=<png>`:
  `PARAGON_SNAPSHOT_WIDTH` (1600; 3200 for 2x), `PARAGON_SNAPSHOT_PAGE=instrument|fx|presets|settings`,
  `PARAGON_SNAPSHOT_INSTRUMENT=1` (a generated demo instrument: 16 zones x 3 layers x 2 round robins),
  `PARAGON_SNAPSHOT_SFZ=<file>` (an SFZ from disk, e.g. VSCO's `ViolinEns-KS.sfz` in the scratchpad),
  `PARAGON_SNAPSHOT_SWITCH=<note>` (that keyswitch chosen),
  `PARAGON_SNAPSHOT_NOTES=1` (a chord held), `PARAGON_SNAPSHOT_LOADING=1` (the progress box), `PARAGON_SNAPSHOT_KEYBOARD=0`,
  `PARAGON_SNAPSHOT_FX=eq|dominion|comp|limiter|sends|delay|reverb|modfx|mod|mod2|lofi|inserts|insdrive|insglitch|insmod|inslofi`
  (`PARAGON_SNAPSHOT_BUS=1-4`, `PARAGON_SNAPSHOT_MATCH=1`, `PARAGON_SNAPSHOT_RACK=1`), `PARAGON_SNAPSHOT_PRESET`,
  `PARAGON_SNAPSHOT_PARAMS=id=value,...`, `PARAGON_SNAPSHOT_FINISH=0|1|2`, `PARAGON_SNAPSHOT_MIDI=1`.
  `"[snapshot-menu]"` draws a drop-down menu. Render, look, crop and zoom before claiming a visual change is right.

## Releases

- Bump `project(ParagonX1 VERSION x.y.z)` in CMakeLists.txt, the version `ProcessorTests` checks ("PR7-O is an
  instrument..."), and add a matching `## x.y.z` section at the top of CHANGELOG.md; **re-run `cmake -B build`** before
  the release checks (the version is baked in at configure time: 0.3.0's first push failed CI because the local build
  still said 0.2.0 and so passed), then push. CI builds Windows and Linux and publishes a GitHub Release with the two zips
  (`ParagonPR7-O-vX.Y.Z-<os>-VST3.zip`), the changelog section as its notes. It publishes from a push to **any** branch
  whose version isn't released yet.
- After pushing, check the release published (both zips) about 15-20 minutes later, **and that its `target_commitish`
  is your commit**. Send the owner the link.
- **One chat per plugin at a time.** Before choosing a version, check the repo's tags *and* other branches for
  unreleased work; if another branch is ahead, merge it first.
- The owner installs from GitHub Releases on **Windows** (and is moving to **Linux**).

## How the owner likes to work

- **Explain the why**, specifically: what was wrong, what changed, the trade-offs. Plain language; the owner is a
  musician, not a DSP engineer. Offer choices with a recommendation.
- **Be honest about limits:** our design vs a copy; guesses; what can't be checked here (sound by ear); ask for source
  material rather than invent specifics.
- **Show it:** send a rendered screenshot of any visual change.
- **"merge it"** means: open a PR from the working branch to main and merge it, then reset the working branch to the
  new main.
- Commit messages explain what and why.
