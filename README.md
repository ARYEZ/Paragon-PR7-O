# Paragon PR7-O

[![Build](https://github.com/ARYEZ/Paragon-PR7-O/actions/workflows/build.yml/badge.svg)](https://github.com/ARYEZ/Paragon-PR7-O/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/ARYEZ/Paragon-PR7-O)](https://github.com/ARYEZ/Paragon-PR7-O/releases/latest)

The orchestra instrument of the Paragon PR7 family: a VST3 instrument that plays sampled orchestral instruments from
open libraries (plain WAV + SFZ files on your own computer), through the family's effects. Later stages add the part
that makes it its own: writing orchestral parts for heavy music (riffs, chords, song sections). Runs on Windows and
Linux. Built with [JUCE](https://juce.com).

Paragon PR7-O starts from [Paragon PR7-D](https://github.com/ARYEZ/Paragon-PR7-D) 0.70.0, the family's drum instrument,
with the drums taken out: it keeps PR7-D's effects, presets, undo, MIDI learn and look. Its siblings are
[PR7-G](https://github.com/ARYEZ/Paragon-PR7-G) (guitar), [PR7-B](https://github.com/ARYEZ/Paragon-PR7-B) (bass),
[PR7-V](https://github.com/ARYEZ/Paragon-PR7-V) (vocal), [PR7-C](https://github.com/ARYEZ/Paragon-PR7-C),
[PR7-E](https://github.com/ARYEZ/Paragon-PR7-E) (utility), [PR7-K](https://github.com/ARYEZ/Paragon-PR7-K) (synth) and
PR7-D (drums). It installs alongside all of them and keeps its own presets.

## Status

PR7-O is built in stages, each one released (the plan: `PR7-O PLAN.md`):

| Stage | What | Now |
|---|---|---|
| **1** | Its own plugin: an instrument that loads an SFZ and plays it | **Works (0.1.0)** |
| 2 | Held notes: loops, release samples, dynamics on the mod wheel, expression, voice stealing for big chords | Next |
| 3 | The libraries: VSCO 2 CE, Sonatina Symphonic Orchestra, Virtual Playing Orchestra; a browser by section; streaming | |
| 4 | Articulations: keyswitches, CC or channel; the list on screen | |
| 5 | Legato (our own) and portamento | |
| 6 | The stage: sections seated on a drawn stage, simulated mics and room | |
| 7 | ORCHESTRATE MY RIFF | |
| 8 | CHORDS | |
| 9 | SONG STRUCTURE | |

## Using it

- **INSTRUMENT** (the page it opens on): **LOAD...** an `.sfz` file (or one audio file, played across the keyboard
  from middle C), or drop one on the page. It shows what the instrument holds: its regions, keys, velocity layers and
  round robins, the memory it takes, anything in the file PR7-O doesn't play yet, and any files it couldn't find; and a
  **region map** (keys across, velocity up: where each recording plays, lit as notes sound). **HOW IT PLAYS**: LEVEL,
  PAN, TRANSPOSE, TUNE, RELEASE (times the instrument's own release), POLYPHONY. **WHERE IT GOES**: SEND DELAY, SEND
  REVERB, INSERT BUS. **CLEAR** unloads it.
- **The keyboard** under the pages: 88 keys, the instrument's tinted; click to play. **HIDE KEYBOARD** (at the foot of
  OUTPUT) makes the window shorter.
- **FX**: an insert bus's DRIVE, GLITCH, MOD and LO-FI (INSERTS); DELAY and REVERB on sends (SENDS); MOD, MOD 2 and
  LO-FI on the whole sound; the MASTER rack (ECHELON EQ, DOMINION, CONSTRICTOR, the LIMITER). All PR7-D's.
- **PRESETS**: the family's browser. A preset keeps where its SFZ file is (not the audio) and loads it again.
- **SETTINGS**: MIDI CHANNEL, NOTE NAMES (C3 or C4 for middle C), the presets folder, MIDI links.

What the SFZ reader plays in 0.1.0: `sample`, keys (`key`, `lokey`, `hikey`, `pitch_keycenter`; note names, c4 = 60),
velocities (`lovel`, `hivel`), MIDI channels, round robins (`seq_length`, `seq_position`), random choice (`lorand`,
`hirand`), pitch (`tune`, `transpose`, `pitch_keytrack`), level (`volume`, `amplitude`, `pan`, `amp_veltrack`),
`offset`, `end`, the amplitude envelope (`ampeg_delay` / `attack` / `hold` / `decay` / `sustain` / `release`),
`loop_mode=one_shot`, `bend_up` / `bend_down`; `#define`, `#include`, `<control>` `default_path`, `note_offset`,
`octave_offset`, and the `<global>` `<master>` `<group>` `<region>` headers. Not yet: loops, release samples,
crossfades (stage 2), keyswitches (stage 4), filters and other modulation.

PR7-O ships no samples, and never reads locked formats (Kontakt, Opus, Spitfire's player...). Check each library's
licence before releasing music made with it.

## Building

Linux needs the packages listed in `.github/workflows/build.yml`. Then:

```bash
cmake -B build && cmake --build build -j8
```

```bash
xvfb-run -a build/tests/ParagonX1Tests
```

## Licence

GNU AGPLv3 (see `LICENSE.md`). JUCE is used under its AGPLv3 licence; the Inter typeface under the SIL Open Font
Licence.
