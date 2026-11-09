# Paragon PR7-O: the plan (a brief for its own chat)

**What it is:** the orchestra instrument of the PR7 family. A VST3 instrument that plays sampled orchestral sections from libraries we're allowed to load, and that *writes* orchestral parts for heavy music (riffs, chords, song sections). It works on its own too. It runs natively on Linux (the owner is moving everything to Linux) as well as Windows.

**Why it's built this way (decided with the owner, November 2026):**
- The owner owns EastWest Hollywood Orchestra Diamond, but Opus doesn't run on Linux, its files are in EastWest's locked format, and EastWest's licence forbids using its audio "to create sounds for any kind of synthesizer, virtual instrument, sample library, sample-based product ... or any competitive product" without a written agreement. So: **no Hollywood Orchestra files or renders, ever** (unless EastWest grants written permission). The same goes for any Kontakt, Spitfire, Orchestral Tools or other locked library: never read their files, never get round copy protection.
- The sound comes from **open libraries** (plain WAV + SFZ). They're below Hollywood Orchestra's quality, so PR7-O's own strength is the **orchestration brain** (stages 7-9), which no other instrument offers for heavy music. Better plain-file libraries can be added later without changing the rest.

## Where it starts

- A new repo, **ARYEZ/Paragon-PR7-O** (the owner creates it), and **its own chat** (one chat per plugin at a time).
- Seeded from **PR7-D** (0.70.0, commit ed3eb4e), full history, the way PR7-D was seeded from PR7-K. PR7-D's notes for Claude (CLAUDE.md) explain the code it inherits.
- **Kept from PR7-D:** the sample engine (`dsp/synth/Instrument.*`: layers, round robins, SFZ reading with `#define` / `#include` / `<control>` / header inheritance), disk streaming (`DiskStreamer.*`), the mixer (channels, mic positions, buses, sends, effects), the FX rack, presets, MIDI learn, the look-and-feel (the family gradient, white lettering), the progress box, the test set-up.
- **Taken out:** the drums (kit, sequencer, grooves, song track, kit view, drum machine, sample packs, DrumGizmo, render import).

## The rules it inherits

- AGPLv3, JUCE VST3, Windows + Linux builds in CI, a GitHub Release per version (bump CMake + CHANGELOG).
- `PLUGIN_CODE` **Pr7o** (never a sibling's: Px1a, Pr7b, Pr7v, Pr7c, Pr7e, Pr7k, Pr7d), `PRODUCT_NAME` "Paragon PR7-O", its own presets folder and settings file, version restarted at 0.1.0.
- The family's colours only (red / pink / orange gradient, pearl white, onyx). No other product's names or branding.
- Sample libraries are never committed (tests use generated tones and small invented SFZ files).
- Say what's our own design, what's a guess, and what can't be checked here (listening).

## The open libraries (check each licence before relying on it)

- **VSCO 2 Community Edition** (Versilian Studios): SFZ + raw WAV. Released free; check its licence text for redistribution (we'd only read it from the owner's disk, never ship it).
- **Sonatina Symphonic Orchestra** (SSO, v4.0 on SFZ Instruments): WAV/FLAC + SFZ, CC Sampling Plus 1.0.
- **Virtual Playing Orchestra** (Paul Battersby): WAV + SFZ, built partly on the two above plus others; each part's licence follows its source.
- Any library sold as plain WAV/SFZ the owner buys later.

## The stages (each a release, one at a time, with the owner)

1. **0.1.0: its own plugin.** The PR7-D seed renamed and stripped of the drums; an empty instrument that loads an SFZ and plays it.
2. **0.2.0: held notes.** What drums never needed: SFZ loops (`loop_mode`, `loop_start` / `loop_end`, crossfaded loops), release samples (`trigger=release`, `rt_decay`), dynamics on the mod wheel (CC1 crossfades between recorded layers while a note is held: `xfin_*` / `xfout_*`, `amp_velcurve`), CC11 expression, a polyphony and voice-stealing scheme for big chords.
3. **0.3.0: the libraries.** Import of VSCO 2 CE, SSO and VPO (their SFZ quirks tested on generated copies laid out like them), an instrument browser by section (strings, brass, woodwinds, choir, percussion), loading many instruments at once with streaming.
4. **0.4.0: articulations.** Sustain, staccato, spiccato, pizzicato, tremolo, marcato... per instrument; switched by keyswitch notes (`sw_last`, `sw_lokey` / `sw_hikey`), CC, or MIDI channel; an articulation list on screen.
5. **0.5.0: legato.** Our own: overlapping notes crossfade into the next note's sample started past its attack, timed by how fast you play; portamento for strings. (No library's legato scripting is copied; the open libraries mostly have no recorded transitions, so this is our design.)
6. **0.6.0: the stage.** A seating view: each section placed on a drawn stage (our artwork), simulated mic positions and room (PR7-D's MicSim ideas: delay, distance, pan, early reflections) so dry open samples sit in one shared space; ensemble stacking and octave doubling.
7. **0.7.0: ORCHESTRATE MY RIFF.** Drop a guitar riff (audio or MIDI; PR7-D's RIFF MATCH onset and accent reading reused): string ostinatos on its rhythm, brass stabs on its accents, low strings doubling its roots, each in the section's real range.
8. **0.8.0: CHORDS.** Chords in (played MIDI, or a chord track sent from PR7-S as MIDI): choir pads, held strings, horn swells, voiced per section; how busy each part is.
9. **0.9.0: SONG STRUCTURE.** Intro / verse / chorus / breakdown sections with intensity rising through the song (which sections play, how hard, how high), drawn as a strip like PR7-D's song track; parts dragged out as MIDI.

## To ask the owner as it goes

- Which open library to start with (VSCO 2 CE is the most complete single download).
- Which sections matter most for their music (likely string shorts / ostinatos, brass stabs, choir, held strings).
- Example riffs and songs for the orchestration stages.
- Whether PR7-S should send its chord track as MIDI to plugins (a brief for PR7-S's own chat).

## What can't be promised

- Hollywood Orchestra quality from open samples. The plan aims for a very good, unique, Linux-native instrument whose writing tools make up the difference in heavy music.
- Anything about sound by ear: the owner listens and decides.
