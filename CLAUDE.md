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

## The plan (`PR7-O PLAN.md`), next

Stage 2 (0.2.0), held notes: SFZ loops (`loop_mode`, `loop_start` / `loop_end`, crossfaded), release samples
(`trigger=release`, `rt_decay`), dynamics on the mod wheel (`xfin_*` / `xfout_*`, `amp_velcurve`), CC 11 expression, a
polyphony / stealing scheme for big chords. Ask the owner which library to start with (VSCO 2 CE is the most complete
single download) and which sections matter most for their music. Stage 3 brings disk streaming (DiskStreamer) and the
libraries' quirks (tested on generated copies laid out like them). Stage 6 gives sections mixer strips (dsp/mixer).

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
  in its own host at shutdown after SUCCESS; CI retries that case only (see the workflow).
- **Screenshots** via `xvfb-run -a build/tests/ParagonX1Tests "[snapshot]"` with `PARAGON_SNAPSHOT=<png>`:
  `PARAGON_SNAPSHOT_WIDTH` (1600; 3200 for 2x), `PARAGON_SNAPSHOT_PAGE=instrument|fx|presets|settings`,
  `PARAGON_SNAPSHOT_INSTRUMENT=1` (a generated demo instrument: 16 zones x 3 layers x 2 round robins),
  `PARAGON_SNAPSHOT_NOTES=1` (a chord held), `PARAGON_SNAPSHOT_LOADING=1` (the progress box), `PARAGON_SNAPSHOT_KEYBOARD=0`,
  `PARAGON_SNAPSHOT_FX=eq|dominion|comp|limiter|sends|delay|reverb|modfx|mod|mod2|lofi|inserts|insdrive|insglitch|insmod|inslofi`
  (`PARAGON_SNAPSHOT_BUS=1-4`, `PARAGON_SNAPSHOT_MATCH=1`, `PARAGON_SNAPSHOT_RACK=1`), `PARAGON_SNAPSHOT_PRESET`,
  `PARAGON_SNAPSHOT_PARAMS=id=value,...`, `PARAGON_SNAPSHOT_FINISH=0|1|2`, `PARAGON_SNAPSHOT_MIDI=1`.
  `"[snapshot-menu]"` draws a drop-down menu. Render, look, crop and zoom before claiming a visual change is right.

## Releases

- Bump `project(ParagonX1 VERSION x.y.z)` in CMakeLists.txt and add a matching `## x.y.z` section at the top of
  CHANGELOG.md, then push. CI builds Windows and Linux and publishes a GitHub Release with the two zips
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
