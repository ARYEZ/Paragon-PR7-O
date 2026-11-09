# Changelog

## 0.1.0

The first Paragon PR7-O: the PR7 family's orchestra instrument, made from Paragon PR7-D 0.70.0 with the drums taken
out. It loads an SFZ instrument and plays it. (PR7-D's history stays in its own repository's changelog.)

**THE INSTRUMENT page** (where it opens):
- **LOAD...** an SFZ file (or one audio file, played across the keyboard from middle C), or drop one on the page. It
  loads in the background; a big one shows the progress box (CANCEL stops it). Loading is one undo step; **CLEAR**
  another.
- What it holds: regions, files, the keys it plays, velocity layers and round robins in the middle of its range, the
  memory it takes; anything in the file PR7-O doesn't play yet (loops, crossfades, keyswitches...: listed with how many
  regions use each), and any samples it couldn't find.
- **The region map**: every region as a box over the keys it plays (across) and the velocities (up), coloured soft to
  loud, round robins counted, the keys sounding lit.
- **HOW IT PLAYS**: LEVEL, PAN, TRANSPOSE, TUNE, RELEASE (times the instrument's own release times), POLYPHONY.
- **WHERE IT GOES**: SEND DELAY, SEND REVERB, INSERT BUS (through a bus's DRIVE, GLITCH, MOD and LO-FI).

**The sampler** (our own code): every region whose keys, velocities, MIDI channel, random range and round-robin turn
take a note plays it; pitched from its root key with cubic interpolation (at its root key and the host's rate it plays
the recording sample for sample); its volume, pan and SFZ's velocity curve; its envelope (attack, hold, decay, sustain,
release; a note let go never cuts off quicker than 5 ms); the pitch wheel (two semitones unless the file says), the
sustain pedal, all notes off. Past POLYPHONY the oldest note (a released one first) fades out in 10 ms. A new
instrument fades the old one's notes over 20 ms. Samples are loaded whole into memory for now (disk streaming comes
with the libraries, stage 3).

**The SFZ reader** (PR7-D's text side: comments, `#define`, `#include`, header inheritance): sample, key ranges and
names, root key, velocities, channels, round robins, random choice, tune, transpose, keytrack, volume, amplitude, pan,
amp_veltrack, offset, end, the amplitude envelope, one_shot, bend range; `default_path` (Windows-style paths too),
`note_offset`, `octave_offset`.

**The keyboard**: 88 keys under the pages, the instrument's keys tinted, the notes sounding lit; click to play.
**SHOW / HIDE KEYBOARD** at the foot of OUTPUT (the window shorter without it).

**Kept from PR7-D**: the FX page (the insert buses, DELAY and REVERB on sends, MOD / MOD 2 / LO-FI, the MASTER rack with
ECHELON, DOMINION, CONSTRICTOR and the LIMITER), presets (a preset keeps where its SFZ is and loads it again), undo,
MIDI learn, the default preset, the family's look and colours. PR7-D's mixer effects and disk streaming are kept in the
code, with tests, for the stages that need them.

**Taken out**: the drums, drum sequencer, grooves, song track, kit view, drum machine, the drum mixer, sample pack and
DrumGizmo import, render import. One stereo output (PR7-D's 16 were per drum).

**Settings**: MIDI CHANNEL (any, or one), NOTE NAMES (C3 or C4 for middle C).

**Its own plugin**: named Paragon PR7-O (plugin code Pr7o), its own presets folder ("Paragon PR7-O") and settings
file, its own logo ("-O"), version 0.1.0. Factory presets: Init only (PR7-O ships no samples).

**A fix in PR7-D's disk streaming** (kept for later): reading ahead while bouncing could, rarely, give silence for audio
that had just been read (it checked a stale fill position); now read again after the wait.

Not checked here: listening (the owner's ears), real libraries' SFZ files (VSCO 2, Sonatina, VPO: their quirks are
stage 3), a DAW on Windows.
