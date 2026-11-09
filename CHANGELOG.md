# Changelog

## 0.2.0

**VSCO 2 Community Edition's strings play properly.** Built against the real files (Versilian Studios' open library,
CC0: its 31 string instruments, violin, viola and cello sections, solo violin, contrabass, read in the build machine's
scratch space, never shipped): all 31 now load with no problems and nothing left unread.

**Keyswitch instruments** (VSCO's `-KS` files, and any SFZ with `sw_lokey` / `sw_hikey` / `sw_last` / `sw_lolast` /
`sw_hilast` / `sw_default` / `sw_label`): the keys in the switching range choose an articulation and don't sound; it
starts on the file's default. Before, a keyswitch file played every articulation at once. The INSTRUMENT page lists the
**articulations** (click one to switch, the chosen one lit), the keyboard marks the switching keys in orange (the chosen
one brighter), and the region map shows the chosen articulation's regions. A label written with its key's name in SFZ's
naming ("C2 Sustain", middle C = C4) shows the plugin's name for that key instead, as the keyboard and NOTE NAMES do
(C1 with Cubase-style names). The articulation chosen is kept with the project.

**Each `<control>` header now holds for the regions after it** (`default_path`, `note_offset`, `octave_offset`).
VSCO's keyswitch files start each articulation with its own `<control>` and folder; 0.1.0 kept only the last, so most
of their samples would have been looked for in the wrong folder.

**AUTO SUSTAIN** (our own design; on by default): VSCO's sustains are 7-17 s recordings with no loop points that end
with the bow coming off, so a note held longer used to fade out. Now a held note crossfades back into the steady part
of its own recording before the bow-off, for as long as you hold it. Where to loop is found as the instrument loads:
the stretch within 3 dB of the note's typical level, the loop up to 4 s long with a crossfade up to 1 s, its start
nudged (by up to 25 ms) to where the sound lines up best, so the two sides don't cancel. Measured on 64 of VSCO's
sustain and tremolo notes held for 25 s: the level over the looped part (12-24 s) moves no more than over the recording's
own steady part (the spread of 0.2 s levels, median 4-6 dB in both: the bowing's natural swell). Not checked: whether the joins can be heard (your
ears).

**Loops in SFZ files** (`loop_mode` loop_continuous / loop_sustain, `loop_start`, `loop_end`, `loop_crossfade`), for
libraries that have them: crossfaded with a curve that keeps the level for how alike the loop's two ends are.

**DYNAMICS: MOD WHEEL** (our own design; VELOCITY is the default, as the file says): a held note (sustain, tremolo)
starts every one of its velocity layers at once and the mod wheel crossfades between them, soft to loud, while you hold
it (equal power between neighbouring layers), and sets the level as velocity would. Shorts (spiccato, pizzicato) still
go by velocity. Which recordings count as held is our own rule from the files' shapes (a held note stays near its level;
a plucked or bounced one peaks at its start and falls): of 431 VSCO string regions it misjudged 3, all contrabass.

**CC 11 (EXPRESSION)** turns the instrument down (the squared curve SFZ uses for velocity: 64 is about 12 dB down).

**POLYPHONY counts notes**, not voices (a note on the mod wheel plays several), and past it a big chord keeps its
lowest and highest notes: the oldest note let go goes first, then the oldest held inner note.

Also read now: `ampeg_dynamic` (it changes nothing here: PR7-O's envelope has no controllers to follow).

Not yet: release samples (`trigger=release`), crossfades written in the file (`xfin` / `xfout`): VSCO's strings use
neither. A tip: VSCO's files raise every sample by 20 dB, so a loud chord can go over 0 dBFS; turn LEVEL down a few dB.

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
