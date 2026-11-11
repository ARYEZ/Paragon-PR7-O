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
  (stage 6's per-section strips); `dsp/sampler/DiskStreamer.*` (namespace `sampler`; reworked and used in 0.11.0).
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
  load (JPEG; exactly 318 / 42 rounds past the range; 0.14.0: to 6 degrees inside, 36 / 324: one pale 43-degree pixel on
  Windows with the section picture drawn bigger).
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

## 0.6.0: the brass (the owner: "Do we have brass section in our library?", "Let's do it"; after a mock-up: "Build it but make the metal a bit more visible")

- **Split points** (`section::fromFiles`): parts ordered by the middle of their ranges; each starts at its own lowest
  key unless that leaves the part beneath fewer than `fewestKeys` (7: the violas' share in VSCO's strings section),
  or is under its start: then the two split their shared range at its middle. VSCO brass sustains (tuba 29-62,
  trombone 34-65, horn 33-77, trumpet 52-84): 29-47 / 48-56 / 57-67 / 68-84; strings unchanged. (A first try
  without the 7-key minimum passed the test with the trombone *staccato*'s range and failed on the real sustain file.)
- **Pictures** (`resources/orchestra/brass3d.py`: strings3d's helpers copied in; side views of our own design at real
  sizes; satin gunmetal `6a5e65` metallic rough 0.3, lit by two extra lights linked to the instruments only (Cycles'
  light linking) so the gloss floor stays black; a neon spine along each tube, bell edges and rims, valve rings).
  `trumpet/horn/trombone/tuba.jpg`, `brass-section(-lit).jpg` (tuba standing left, trombone across the top right,
  horn and trumpet under it: four bells-up on stands made horn and trumpet tiny). Trumpet, horn, trombone at 0.745 m
  across (the trombone framed on its bell end so its crook shows), the tuba whole (the owner chose this). JSON:
  `valves` (button tops / rotary faces, valve 1 first, horn's thumb last), `slide` (the hand at 1st and 7th: 0.60 m
  behind the crook at 1st, +0.55 m), `brassSection.<kind>`. `process_orchestra.py <strings> <out> [<brass>]`.
- **`OrchestraView`**: `Kind` trumpet / horn / trombone / tuba (`kindOf`: trombone, tuba, trumpet / tpt, horn except
  english / basset / flugel); `isBrass`; `fingeringFor (kind, note)` (our rule from the usual charts, sounding pitch:
  the lowest harmonic at or over the note (7th, 11th, 13th, 14th left out), lowered by the valves 2 / 1 / 1+2 / 2+3 /
  1+3 / 1+2+3, or the slide 1st-7th; B-flat trumpet harmonics 2-12, F horn 2-16 (F side, thumb never), B-flat tenor
  trombone 1-10 (no trigger: 35-39 unplayable), 4-valve B-flat tuba 1-10 (4 = five, 2+4 six... 1+2+3+4 eleven, only
  under its 2nd harmonic)); the highest held note shows (`getBrassNote`); `paintFingering` (valve dots as the strings'
  stop dots; slide: 7 marks with numbers, the run to the note's lit). A section shows the family with more pictured
  parts (`brassSection`, `showsBrass`); `section (lit, brass)`, `getValves`, `hasSlide` (tests). Titles: "THE TRUMPET:
  EACH NOTE'S VALVES LIT", "THE TROMBONE: EACH NOTE'S SLIDE POSITION LIT". `pictureArea`: one trombone's picture moves
  sideways to keep its slide's run (1st-7th + 90 px) in view, a little smaller where even that won't fit (the panel
  crops the picture's sides at usual window shapes: positions 1-2 were off the left edge).

## 0.7.0: LEGATO and GLIDE (the owner chose legato as the next step; after the demos and the rack screenshot: "Release it at 0.7.0")

- `Sfz`: `settledFrame` (the first 50 ms window of the core within 3 dB of its body), cached in `Analysis::settled`;
  `Region::legatoStart` (held only, else -1; at most half the recording, never past its loop start). VSCO medians:
  violins 0.75 s (pp layer up to 1.45 s+), cellos 0.15, horn / trombone / flute 0.1.
- `Sampler`: `Settings::legato`, `glideSeconds`; key state (`keyDown`, `keyVelocity`, `keyChannel`, `keyStart` on the
  sample `clock` / `now`, `cameFrom`); `noteOn` -> `joinableFrom` (sounding, held, pressed >= `chordWindow` 40 ms ago,
  nearest within 12 semitones, the most recent on a tie) -> `playNote` (joined only if a chosen region is held: the
  old note's voices `fadeOut` + `legatoOut`, gliding to the new pitch; new voices at `legatoStart`, attack over the
  join, `legatoIn`); `joinSeconds` = 0.25 x time since the old note began, 30-150 ms; equal power (sin of the linear
  ramps); `release` converts a legatoIn attack's level; noteOff joins back to the nearest still-down key along
  `cameFrom` (not with the pedal down); glide per sample (`glideRatio` x `glideStep`, `glideCount`, `glideTarget`).
- Parameters (hint 44, `pr7o070`): `instLegato` (off), `instGlide` (0-0.5 s, skewed, "Off" at 0). Rack: `numKnobs` 7
  (GLIDE), `pageKeyCell (0 | 1)` (AUTO SUSTAIN, LEGATO). Known limit: a melody over a held chord joins the nearest
  chord note (the owner was told; a smarter rule offered).
- Checked on VSCO (probes, not committed): joins happen, the old note -20 dB within 0.1 s; forte violins only ~0.1 s
  sooner at level (their attack is short), slow swells much more. Demo WAVs sent. Not checked: listening.

## 0.8.0: THE STAGE (the owner: "Let's do the stage"; after the mock-up: "Build it"; after the demos: "Release it at 0.8.0")

- **Look** (`resources/orchestra/stage3d.py`: our neon stage, platform with a curved front, three risers round a point
  6 m in front, podium, back wall; camera above and behind the conductor): `stage.jpg` (2048 x 842) and orchestra.json's
  `stage` grid (metres every 0.5 m from x -9, y -2: 37 x 29 picture points, `podium`); `process_orchestra.py <strings>
  <out> <brass | -> [<stage>]`.
- **`dsp/stage/Seating.*`**: `Seat { x, y, width }` (metres: x across from the conductor's left, -8.6..8.6; y back,
  0.3..11), `clamp`, `seatFor (name)` (our layout after the modern plan), `places()` (the guide).
- **`dsp/stage/Stage.*`**: per part (up to `maxParts` 8, the sampler's `Region::part`) its seat 1.2 m up plus
  `riserHeight`; its L / R at the seat's edges (width x SPREAD / 4 each side) by **level only**: every path's length
  from the seat's middle (separate paths comb-filtered a mono part: up to 20 dB holes; a test sweeps 100 Hz-3 kHz).
  Mics 2 m apart, 3.2 m up, MIC out, aimed at (+-5, 5), pattern 0.37 + 0.63 cos. Direct + 6 image sources (floor,
  ceiling, side walls, back wall, front wall), 343 m/s, 1 / d, reflections x `Room::reflection` x ROOM LEVEL; air:
  one-pole at 16 kHz x exp (-d / 40) per part per mic; walls: one-pole at `Room::damping`. `pointWeight` 0.5 and
  `norm` (the centre seat 5 m back through the left mic) keep a centre part's level; the delay to the stage's front
  (less 0.3 m) taken off. Tail: 8-line FDN (31-74 ms x `Room::scale`, Hadamard, damped), gains for `Room::seconds` x
  DECAY, out level = reference / `criticalDistance` x ROOM LEVEL x sqrt (1 - g^2) x 2. Rooms: Studio (9.5 / 5 m, 0.5
  s, rc 3), Scoring Stage (13 / 11, 1.4 s, rc 5), Concert Hall (14 / 16, 2.1 s, rc 7), Cathedral (17 / 28, 4.8 s, rc
  9). Taps: still = nearest whole sample (linear reads dulled highs by seat); moving = at most `maxSlew` 0.04 samples a
  sample; the first `aim` after a start snaps. Seats by a sequence lock; silent parts skipped once their rings empty.
- **Sampler**: `render (..., const PartOutputs*)`: each part into its own pair (ramps per part); `scratchLeft/Right`
  per part.
- **Processor**: STAGE on (or fading): parts apart -> `stage.process`, 20 ms crossfade with the plain sum when switched
  (`stageMix`; a stage turned on from off is reset). `pushSeats` (by part number: a section's parts, else the file;
  on install, `setSeat`, the timer), `movedSeats` by part name in the state ("Stage" / "Seat") and `Rig::seats` (XML
  `<Stage><Seat name x y width/>`), via captureRig / applyRig so undo covers them; `seatMoveFinished` (modified +
  checkpoint); `getSeatsVersion` (the view follows).
- **UI**: rack `Page::stage` (three PAGE keys; MIC, ROOM LEVEL, DECAY, SPREAD in `routingKnobCell`; ROOM menu;
  STAGE / RESET SEATS keys), `StageView` (the panel's STAGE view: zones from the grid, labels backed for readability,
  distances on hover / drag, `toStage` by Newton steps), `InstrumentPage::chooseStage`. Snapshot:
  `PARAGON_SNAPSHOT_STAGE=1`.
- Parameters (hint 45, `pr7o080`): `stageOn` (off), `stageRoom` (`stageRoomNames`, Concert Hall), `stageMic` (1-25 m,
  log, 2.5), `stageRoomLevel` (-24..6 dB, -3), `stageDecay` (x0.5-2, log), `stageSpread` (0-150%).
- Checked on VSCO (probes, not committed): string sections alone ~4 dB to their side at the defaults, peaks within
  0.7 dB of STAGE off, rooms ringing as set; demo WAVs sent. Not checked: listening. Ensemble stacking and octave
  doubling (the plan's stage 6 too) not done: ask.

## 0.9.0: the rest of SFZ's sound (the owner: "Let's implement what's left on the plan"; step 1 of an order told to them:
0.9.0 SFZ engine, 0.10.0 stacking / doubling, 0.11.0 browser + streaming, 0.12.0 woodwind pictures, then the writing
tools, which need the owner's riffs)

- **Sonatina Symphonic Orchestra** (`github.com/peastman/sso`, CC Sampling Plus 1.0: read from the user's disk only):
  cloned into the scratchpad with `--filter=blob:none` and the 762 SFZ files checked out; opcodes counted
  (`/tmp/claude-0/probes/sso/count.py`). **Never `git ls-tree -l` on that clone** (sizes fetch every blob: it started
  downloading all 3062 samples). Sample paths are relative to the top-level file, not an #include's.
- `Sfz`: `Region::Trigger` (attack / release / releaseKey / first / legato), `rtDecay`, `ccConditions` (locc / hicc),
  `filters[2]` (type, cutoff, resonance dB, keytrack, keycenter, veltrack, cc cents), `gainCC`, `amplitudeCC`,
  `ccFades` (xfin / xfout_loccN / hiccN), vel / key fades, `*FadePower` (xf_*curve, power default), `velCurve`
  (amp_velcurve_N), `velToEnvelope` / `ccToEnvelope` ([delay, attack, hold, decay, sustain, release]), `ampKeytrack`,
  `pitchVeltrack`, `startDelay` / `startDelayRandom`, `offsetRandom`, `width`, `group` / `offBy` / `offMode` /
  `offTime`, `lfo*` (amplfo), `usesController`; `Program::initialCC` (set_ccN), `sustainCC`, `ccLabels`, `ownWheel`;
  group_ / master_ / global_ volume, amplitude, tune added; `isNumbered` (opcodes with a controller number); `readMore`.
- `Sampler`: `cc[128]` (set_cc on a program change; CC 1 / 11 into wheel / expression); the pedal on `sustainCC`;
  `releaseDue` (keys let go under the pedal), `playReleases` (after the note's own voices are let go: before, they were
  let go with them), release voices never loop; `phraseGoingOn` for first / legato; `conditionsMet`; per voice
  `controlGain` (gain / amplitude / cc fades, ramped), `waiting` (delay), `FilterState` x2 (TPT state-variable, 1-pole
  for lpf_1p / hpf_1p; cutoffs re-set by `updateFilters` on `controllersChanged`), width (mid / side), LFO (block-rate,
  ramped); groups in `startVoice` (off_by voices: normal = release, else fade 6 ms / off_time); `onWheel` only when
  the region doesn't follow CC 1 itself.
- Checked on SSO (probes, not committed): 5 instruments, 0 problems, unread only lfo01_ / eq1_ (CC 21 vibrato, default
  0); violins +17 dB and brighter on the wheel; organ release tails. Demo WAVs. Not checked: listening.

## 0.10.0: THE ENSEMBLE (the owner: "Build it" after the mock-up; "Release it as 0.10.0" after the demos)

- Parameters (hint 46, `pr7o0100`): `ensPlayers` (x1-4), `ensDetune` (0-30 ct, 8), `ensTiming` (0-40 ms, 12),
  `ensDouble` (`doubleNames`: Off, Octave Up, Octave Down, Octaves Up + Down), `ensDoubleLevel` (-24..0 dB, -6).
- Rack: `Page::ensemble`, four PAGE keys (40 px wide, names 6.5 pt); its knobs in `routingKnobCell`, DOUBLE in
  `menuCell`. Snapshot: `PARAGON_SNAPSHOT_ENSEMBLE=1`.
- `Sampler`: `Settings::players / detuneCents / timingMs / doubleMode / doubleGain`. In `playNote` each region starts
  `players` voices (`Copy`: copy 0 untouched; others detune `detunes[c]` x DETUNE plus +-25% random, delay 25-100% of
  TIMING via `waiting` (never earlier: no look-ahead; the mock-up said "early or late", corrected), up to 8 ms
  `offsetSeconds`, pans `pansFor` (x2: both centred; x3 / x4: +-0.6), each side scaled 1 / sqrt (sum of its balance
  gains squared): a plain 1 / sqrt N left three players 1.85 dB down). DOUBLE in `noteOn`: `playNote` for key +-12 with
  `NoteExtras` (owner = the played key, the main note's `noteId` shared: no `makeRoom`, one note for POLYPHONY; legato
  `joinFrom` = from +-12, join time from the played key's start); `Voice::owner` (noteOff releases by it),
  `Voice::isDouble` (never joined from). GCC refuses `const NoteExtras& = {}` as a default inside the class: pass it.
- Checked on VSCO (probes, not committed): RMS held within 0.2 dB (solo violin x4, violins x3); the strings section's
  line doubled down +0.6 dB. Demo WAVs. Not checked: listening.

## 0.11.0: THE LIBRARY and DISK STREAMING (the owner: "Build it" after the browser mock-up)

- **`library/Library.*`**: `Section` (strings, brass, woodwinds, percussion, keys, choir, other), `Entry`,
  `instrumentOf` / `sectionOf` (our word table from VSCO's / Sonatina's names; VSCO's "S" prefix = solo; a name that
  says nothing takes its folder's), `scan` (sorted; `include*` / `inc` folders skipped), `folders` / `setFolders` (key
  "libraryFolders" in the "Paragon PR7-O" settings file). **`ui/LibraryBrowser.*`**: over the INSTRUMENT page below the
  rack (LOAD toggles it): libraries, sections, instruments (grouped; folder labels), the chosen file's details (read
  with `audio = false`), LOAD, ADD TO SECTION / LOAD SECTION / CLEAR, search (every section), OPEN FILE...
  Snapshot: `PARAGON_SNAPSHOT_LIBRARY=<folders>` (+ `_LIBRARY_SECTION`, `_SEARCH`). Tests: `tests/LibraryTests.cpp`.
- **Streaming** (`DiskStreamer.*` reworked; PR7-D's multi-mic API gone): `Sample` is whole (`channels`) or streamed
  (`pieces` in memory, `frames`, `streamedChannels`, `stream` = its `StreamSource` with `resident` ranges);
  `sampleFiles::streamedFrom`. `sfz::keptInMemory (region)`: 0.5 s (`streamHeadSeconds`) from the offset (+ offset
  random + 8 ms), from `legatoStart`, from the loop end (the tail when let go), and +-4 frames round the loop start;
  `sfz::streamSamples (program)` makes each recording of 2 s or more (`shortestStreamedSeconds`: CI failed with 1 s ones played faster than real time) streamed if that keeps < 70% of it, carries its `Analysis` to
  the new pointer (loop likeness cached by loop in `Analysis::likeness`; a streamed sample never analysed again),
  sets `Program::bytes` / `wholeBytes`. The processor's loader calls it on every load (`setStreamFromDisk`, on); the
  streamer (`getStreamer`) is made with the first streamed instrument (rings 64 MB), declared last.
- `Streamer`: 256 streams, ring 1 << 14 frames x (main L/R + back L/R). A stream follows its voice's TIMELINE
  (`StreamPlan` start / loop / crossfade; `fileFrame (plan, t, exitAt)`: linear, then round the loop, then past its
  end from `exitAt`); `back` holds the crossfade's back reads (file frame - loop length) for frames in the zone
  `inBackZone` (loopEnd - crossfade - 4 to loopEnd). `pastResident` skips frames in memory (never through the back
  zone); **room = pastResident (min (need, written)) + ring - written** (two wrong rules first: from `need` alone the
  ring had no room after the 0.5 s head; a skip-raised floor lost the frames just before a piece). `span` stops at the
  next resident piece and at the timeline's next going-back (so a window never covers memory's frames), checks
  t >= need and t < written, and refuses t >= exitAt until the disk thread has applied `stopLooping` (written rewound
  to exitAt). `fillNow` for bouncing; `waitUntilFilled` for tests.
- `Sampler`: `setStreamer` (atomic, taken per render), `Settings::waitForDisk` (the processor: `isNonRealtime()`).
  Voice: `stream`, `lap` (timeline - position: + loop length at each wrap), `Window inMemory / ahead / behind`;
  `startStream` at the end of `startVoice` (plan start = position - 2), `endStream` (done, slot reused, `reset`),
  `leaveLoop` (release outside the crossfade, or the wrap after `stopAtWrap`: `stopLooping (loopEnd + lap)`, windows
  cleared), `consumed (position + lap - 2)` at each block's end. Reads: memory window by file frame, ring window by
  timeline (inline), else `streamedFrame` (another piece, `span`; bouncing: `fillNow` until there, one more look after
  an idle read: the disk thread may have read it in between; live: silent and counted in `getUnderruns`).
- Info: "MEMORY x MB in memory, y MB streamed from disk as it plays". Snapshot: `PARAGON_SNAPSHOT_STREAM=0`.
- Checked (probe `/tmp/claude-0/probes/ScratchStream.cpp`, not committed): all 75 VSCO files 5615 MB -> 811 MB in
  memory; the strings section 450 -> 73 MB (trim 14 ms); a 14 s passage (10-note chord x4 players on the wheel, a
  legato line: 92 streams) bounced and played live at real-time pace from a cold file cache: **identical sample for
  sample, 0 underruns**; audio-thread time 0.9-1.1 s vs 0.74 s whole. Sonatina's 1st violins, looped horn, organ
  (FLAC, release samples, pedal): identical. Not checked: a slow hard disk, Windows' disk.

## 0.12.0: THE WOODWINDS (the owner: "Build it, and add the three Sonatina ones too" after the mock-up)

- **Pictures** (`resources/orchestra/woodwinds3d.py`: brass3d.py's helpers, `Builder` and tail copied in; `Wind`: `lathe`
  bodies along u, `cup` (neon rim; `ring_key` for clarinet / bassoon holes), `pad`, `rod`, `lever`, `heights` so the
  geometry is at each key's top (the mock-up's dots sat 3 px low)). Eight singles (flute, piccolo, oboe, clarinet at
  0.745 m across; English horn, bass clarinet, bassoon, contrabassoon whole) and `woodwind-section(-lit).jpg` (flute +
  piccolo, oboe + clarinet, bassoon). `process_orchestra.py - <out> - - <winds>` keeps the rest (`kept`); JSON
  `keys` = thumb, L1-L3, R1-R3, extra (Eb / octave / register / whisper), `woodwindSection.<kind>`.
- **`OrchestraView`**: `Kind` flute / piccolo / oboe / englishHorn / clarinet / bassClarinet / bassoon / contrabassoon
  (`kindOf` checks them first: a contrabassoon isn't a double bass); `isWoodwind`, `familyOf`, `sectionKindOf` (English
  horn -> oboe, bass clarinet -> clarinet, contrabassoon -> bassoon in the section picture; an instrument's own part
  first), `displayNameOf`; `windFingeringFor` (our simplified chart, bits thumb / L1-3 / R1-3 / extra; flute C4-A6,
  oboe Bb3-C6, clarinet written E3-C6 (upper register = low + register key, the extension all down), bassoon Bb1-D3 and
  G3-A3; transposers read it written: piccolo -12, English horn +7, clarinet +2, bass clarinet +14, contrabassoon +12;
  `charted` false elsewhere: nothing lit); `paintKeys`; `Family` and `section (lit, Family)`, `sectionFamily` (most
  pictured parts; ties strings, brass), `partKinds` (the part's real kind for its chart). Titles "THE FLUTE: EACH
  NOTE'S FINGERING LIT". The mock-up's clarinet showed concert C as T + L1-3 + R1-2: wrong (that's written A3 / B3
  territory); the chart's written D4 is T + L1 + L2.
- `Seating`: contrabassoon with the bassoons (it went to the basses: "contrabass" in its name).

## 0.12.1: a crash fixed, a round of optimization (the owner's PR7-S log; "run another round of optimization")

- **Crash** (PR7-S log: access violation reading 0x6567617473 = "stage", the screen's thread, 7 s after reopening the
  window): matched to the released 0.12.0 Windows build (13 / 14 return addresses after a call there, none in 0.2.0:
  PR7-S shows the version from its first scan) and its .pdata function table (`objdump -p`): `LibraryBrowser`'s
  SECTIONS `paintRow` -> `library::nameOf (row)` with row 7+ (JUCE's lists paint the empty rows under the last one,
  `row` past `getNumRows`: documented). Every browser list now returns past its count (and clicks); `nameOf` checks
  its table. **When a list's model paints, check `row` against the count.** To map a Windows crash: download the
  release zip, `objdump -p` (ImageBase, Function Table), find the function holding each RVA, disassemble it and the
  strings it uses (`/tmp/claude-0/wwscripts/match.py`, `strings_near.py`).
- **Optimization** (probe `/tmp/claude-0/probes/ScratchBench.cpp`, not committed: `[bench]` scenarios on VSCO's strings
  section, `[reference]` dumps a long passage to compare old / new sample for sample; callgrind needs the probe's
  `CALLGRIND_TOGGLE_COLLECT` (name filters never matched) and a long load timeout under valgrind; build
  `build-prof` RelWithDebInfo for lines). Before: chord x4 on the wheel 8.9% of a core, + STAGE 10.0%, + double both
  20.9%; after: ~5.7 / 6.3 / 13%. `renderVoices`: `readBoth` (both sides, the place once; four frames straight from
  `inMemory`, else from the ring window by timeline: ring windows never hold resident frames, so that's equivalent;
  else `readAt` frame by frame); `muted` voices (on the wheel, `wheelGain` and target exactly 0 for the piece, no
  filters) skip reading and mixing, still move. Output identical (the reference: 0 samples differ). Left: Hermite
  arithmetic ~14%, per-voice bookkeeping, STAGE ~9% (its tap loop's ring mask): a SIMD / batched voice loop would be
  the next step (not asked yet).

## 0.13.0: GET the libraries, then the section presets (the owner: "Release 0.12.1, then do both libraries with GET buttons"; before that, expansion banks "with pre-built layouts where it's the entire string section or the entire woodwind section or the entire brass section")

- **`library/Downloads.*`** (namespace `downloads`; PR7-K 0.71.0's `SampleLibraries` ported: its tar.gz reader and
  `Download` thread unchanged but for names): `vsco()` (the SFZ branch pinned to commit 6dd651d: GitHub archive of the
  commit, folder `VSCO-2-CE-<sha>`, marker `ViolinEns-KS.sfz`; it has no release), `sonatina()` (PR7-K's: tag v4.0,
  `sso-4.0`, `browse` "Sonatina Symphonic Orchestra"); `folder()` = "Libraries" beside the presets' `Rigs` folder
  (`setFolder` for tests); `isInstalled`, `installedFolders`; references `{sfz:<folder>}/<path>` (`refFor`, `isRef`,
  `libraryFor`), `resolve (ref, userFolders)` (the download, else the path or its tail in a user's library folder),
  `portable (path)` (a file in a download made a reference). This session's proxy refuses GitHub's archive URLs for
  other people's repos (git reads work): the real downloads are untested here; `tests/DownloadTests.cpp` uses a
  stand-in archive from a file:// URL (PR7-K's `TarWriter`).
- Processor: `getDownload`, `getLibrary (library, url)`; `loadInstrument` / `loadSection` store `portable` names; the
  loader resolves each (section parts too) and says "needs VSCO 2 CE: GET it in the library (LOAD)" when a library's
  missing. `section::nameOf` / `describe` / `folderOf` read references by text (a juce::File can't hold one).
- `LibraryBrowser`: `userFolders` (remembered) + the downloads (`folders`, listed by `displayName`: "VSCO 2 CE"; REMOVE
  off for them; entries' `library` renamed to match); GET buttons (one a library not installed, VSCO on top), the
  download's line (DOWNLOADING / UNPACKING n%, or why it failed) and CANCEL; a 5 Hz timer; the list rescans when it's
  done. Snapshot: `PARAGON_SNAPSHOT_DOWNLOAD=<0-100>` (with LIBRARY). SETTINGS' THE SOUNDS credits both libraries.

- **Expansions** (the owner: "Build it" after the preset mock-up; PR7-K's system): `rigs/Expansions.h` (`list`: VSCO
  ORCHESTRA "VO", SONATINA "SO", descriptions); `Rig::numBanksWithExpansions`, `isExpansionBank` (bank >= 5),
  `bankName` / `getSlotName` ("VO01"); `toXml` writes A for an expansion bank (user files never hold one); Save As
  from an expansion preset files in bank A. `FactoryRigs.cpp`: `sectionRig` (parts as library references; one part
  over 0-127: a plain reference), `layoutOf` (notes written from the splits, "C1 to B1", middle C = C3); VO01-VO08,
  SO01-SO06 (instLevel -6, mod wheel, STAGE on). **Factory presets are found by name** (`findFactory`: projects,
  THE DEFAULT): names must be unique ("Sonatina Strings", not a second "String Section"). `RigBrowser`: A-E on one
  row, EXPANSIONS heading (`expansionsHeading`), bankBox lists the expansions disabled, the details add the bank's
  description and, when a part's library isn't found (`resolve`), "PLAYS X: NOT DOWNLOADED YET" + `getButton` (GET,
  progress through a 4 Hz timer). Tests `[expansions]` in `RigTests.cpp`; the ordering tests now end at SO06.

## 0.13.1: the expansion presets were silent (the owner: "whenever I load a preset I get no volume whatsoever")

- DYNAMICS on the mod wheel: `wheel` started at 0 and `wheelGainOf` read it as velocity 1-127 (0 = -84 dB with full
  velocity tracking): every VO preset silent until the wheel moved (a probe loading the factory preset reproduced it).
  Now `Sampler::wheelStart` 100 / 127 (until a CC 1 or the file's set_cc1; `shownWheel` too) and `wheelFloor` 32
  (the wheel runs over velocities 32-127). `cc[1]` for SFZ opcodes still starts at 0 (SFZ's default; Sonatina sets
  set_cc1=96 itself). **Probes for presets must play without moving the wheel**: mine had sent CC 1 = 100 and missed it.

## 0.14.0: ARTICULATIONS on the rack, the stage the page's width (the owner, on a screenshot: "the keyswitch box ... can become a 2nd page in the rack unit ... stretch out the tron display of our stage and use all the space"; after the mock-up: "Build it and show plain section names")

- `InstrumentRack::Page::articulations` (appended to the enum; its key `articulationsKey`, "SWITCHES", second of five
  PAGE keys, 32 px wide, names 5.8 pt): no side column (`hasSide`); `setArticulationNames`, `setNumSplits`,
  `articulationCell` (one row with splits, else up to 10 a row), `splitScreenCell`, `splitKeyCell`; the rack stamps
  the names and paints the split screens (glass in `paintOverChildren`). `InstrumentPage`: articulation buttons
  (pedal keys, name in `getName`) and `SplitRow` labels / - + keys are **rack children**, shown by `showRackPage`; no
  left panel (`mapArea` = the width); `info` beside the region map, visible with it. Snapshot
  `PARAGON_SNAPSHOT_SWITCHES=1`.
- `stage3d.py`: a picture wider than 2048 x 842 uses a lower camera further back (`STAGE_CAM` / `STAGE_LOOK` /
  `STAGE_LENS` override): `stage.jpg` 3200 x 920, orchestra.json's `stage` re-measured (`process_orchestra.py - <out> -
  <stage>`). Rendered in ~3 min at 64 samples.
- Names shown: `section::labelOf` (the "-KS" ending off), `library::partLabel` (labelOf, then the library's word
  table: "ViolinEns-KS" -> "Violins", "Contrabass-KS" -> "Basses"); stage labels, split screens, the rack screen's
  section name. Seats and everything saved go by `nameOf`. Test: "THE INSTRUMENT page 0.14.0".

## 0.15.0: THE MIXER (the owner: "Let's do mixer strips next"; on the rack-style mock-up: "OMG that Tron style mixer looks amazing!!! Build it in that style and give it its own mixer tab")

- **`dsp/mixer/Strip.h`** (light: presets include it): `mixer::StripSettings` (levelDb -60 off..+12, pan, mute, solo,
  invert, delayDb / reverbDb -60 off..+6, `fx[4]` FxSlot), `isPlain`, XML (`toXml` / `fromXml`; `mixerXml` /
  `readMixerXml`: <Mixer><Strip name/>..<Section/>, only non-plain strips); `maxPartStrips` 8, `sectionStrip` 8.
- **`dsp/mixer/PartMixer.*`**: per strip `processStrip`: `mixerfx::Host` effects (table indexed by strip: parts at
  their numbers, the section at 8), then gains (fader x balance x polarity, 0 muted / not soloed) smoothed 20 ms
  (`SmoothedValue`), sends post-fader into the given pairs (null: off), peaks (`takePeak`). `setStrips` (message
  thread: `host.sync`, then a SpinLock'd pending copy), `beginBlock` (try-lock take). A plain strip: no work.
- **Processor**: the sampler **always** renders parts apart (`PartOutputs`); `partMixer.processPart` each, then STAGE
  or the sum, then `processSection`; the delay / reverb send buffers cleared first, the strips add (gated on
  delayOn / reverbOn), the instrument's own sends now `+=`. `partStrips` (by part name) / `sectionStrip`; `getStrip`,
  `setStrip (name, s, final)` (final: modified + checkpoint), `setSectionStrip`, `pushMixer` (by part number, from
  `pushSeats` and the timer; `getMixerVersion`), `takeStripPeak`. State child "Mixer"; `Rig::strips` /
  `sectionStrip`; captureRig / applyRig. **Undo `Snapshot::operator==` now compares seats and strips** (a seat moved
  alone made no undo step since 0.8.0).
- **`ui/MixerPage.*`** (`MainView::Page::mixer`, tab between INSTRUMENT and FX, `Icons::Type::mixer`; five tabs:
  `tabEdges` re-spaced): strips (`Strip`: PedalBody units, `SlotScreen`s, rack knobs `pedalKnobProperty`, `Fader`
  (scale +12 / 0 at a quarter / -60, a pedal key as its cap, double-click 0 dB), meter, keys; the section's SOLO
  hidden; OUTPUT text from stageOn / instInsert), `EffectEditor` (over the strips, sized to rows of up to 10 knobs,
  OUTPUT beside the keys; click outside closes); names by `library::partLabel`. Test hooks: `getKey`, `getEffectKey`,
  `getPanKnob`, `moveFader`, `getEditorKnob`... Snapshot `PARAGON_SNAPSHOT_PAGE=mixer` (`_MIXERDEMO=1`,
  `_MIXERFX=<strip>,<slot>`). **Tests' sliders: `setValue (v, juce::sendNotificationSync)`** (the default is async).
- Tests: `tests/MixerTests.cpp` (sound, state, undo; its invented recordings are 1.8 s: 2 s and longer stream from
  disk and a test playing faster than real time loses frames), UiTests "THE MIXER tab 0.15.0" (controls, editor,
  every pixel in family colours).

## The plan (`PR7-O PLAN.md`), next

Sections are split only (0.4.0; layered was offered too: not asked for). Next in the order told to the owner:
stage 4's CC / channel articulations, mixer strips (dsp/mixer), then the writing
tools (ORCHESTRATE MY RIFF, CHORDS, SONG STRUCTURE: the owner's riffs asked for).

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
  - **Paths in tests: build absolute ones from a temp folder** (`"/x/y.sfz"` isn't absolute on Windows: 0.4.0 and
    0.13.0 both failed CI's Windows job on this). After a push, check CI passed even when no release is due.
  - Notes in tests go in as MIDI at a sample position, not through the keyboard state (it spreads presses by the clock).
  - `tests/SamplerTests.cpp`: the SFZ reader and the sampler on invented files; also the kept MixerEffects and
    DiskStreamer.
- **CI's Linux machine and DNS** (0.15.0): GitHub's Ubuntu runners looked Ubuntu's mirrors up through Azure's name
  service, which failed for hours ("Temporary failure resolving 'azure.archive.ubuntu.com'", the job stopping in apt
  before building). The workflow now points systemd-resolved at Cloudflare (1.1.1.1, 1.0.0.1; 8.8.8.8 fallback) and apt
  retries 5 times. A failure there isn't our code: read the log before "fixing" anything.
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
