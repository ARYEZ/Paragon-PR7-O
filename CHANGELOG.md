# Changelog

## 0.11.0

**THE LIBRARY, and instruments that stream from disk** (the plan's stage 3; you: "Build it" after the browser
mock-up, then "Release it as 0.11.0").

**The library browser.** LOAD now opens a browser over the INSTRUMENT page instead of a file box. Add the folders your
libraries are in once (ADD FOLDER...: VSCO 2 CE, Sonatina, any plain WAV + SFZ library); PR7-O remembers them for every
project. It sorts every instrument file into a **section** (strings, brass, woodwinds, percussion, keys & harp, choir)
and an **instrument** (Violins, Solo Violin, Horns...) by its name: our own rule, made from the names VSCO and Sonatina
use. Click a file to see its keys and articulations, LOAD it, or ADD TO SECTION to build a section file by file (LOAD
SECTION). A search box looks through every section ("pizz", "horn"). OPEN FILE... is still there for anything else.

**Disk streaming.** Until now every recording was loaded whole into memory: VSCO's string section took 450 MB, all of
VSCO 5.6 GB. Now a long recording keeps only a few short stretches in memory (its first half second, where a legato
join starts it, the start of its tail after its loop) and the rest is read from disk just ahead of where it's playing,
loops included. Measured on VSCO: the string section is 73 MB in memory, all 75 VSCO instruments 811 MB.
- **It sounds the same.** A test passage (a 10-note chord on the string section with four players on the mod wheel and
  a legato line over it: 92 recordings streaming at once) came out identical, sample for sample, to the same passage
  played from memory, both bounced and played live at real speed from a cold disk; the same for Sonatina's violins,
  horn and organ (its release samples too).
- **Bouncing** (offline export) always waits for the disk, so a bounce is always complete.
- **Playing live**, if a disk ever couldn't keep up, the missing moments would be silent rather than stall your DAW.
  It never happened in our tests, but they ran on a fast SSD: a slow hard disk with a very big chord is the case we
  couldn't check here.
- The INSTRUMENT page's info shows the split ("MEMORY 72.8 MB in memory, 376.7 MB streamed from disk as it plays").
- Recordings under 2 s (staccatos, pizzicatos) stay whole in memory: streaming them would save little.

Nothing changes in your projects or presets.

## 0.10.0

**THE ENSEMBLE: a bigger section, and lines doubled in octaves** (the plan's next step; you: "Build it" after the
mock-up, then "Release it as 0.10.0" after the demos). A fourth page on the rack, **ENSEMBLE**:
- **PLAYERS** (x1 to x4): each note played by more players. Each extra one is a little out of tune (**DETUNE**, up to 8
  cents to start), comes in a little later (**TIMING**, up to 12 ms: never earlier, so what you play stays on time),
  starts a moment further into its recording and sits either side of the first. The level holds: a solo violin with
  four players is as loud as with one, just bigger.
- **DOUBLE** (off, octave up, octave down, both) at **DOUBLE LEVEL** (-6 dB to start): every note doubled as
  orchestrators do. In a section, the doubled note is played by whichever part has that key (a violin line doubled an
  octave down comes out of the violas and cellos). The doubled notes end with the key you played, count as the same note
  for POLYPHONY, and follow a LEGATO join.

Both start off (x1, no doubling): your projects and presets sound as before.

Worth knowing: extra players are copies of the same recording made to differ, not new players: pushed hard (big DETUNE
and TIMING) they can sound like a chorus effect; the starting values are small for that reason.

Not checked: listening beyond the demos you heard.

## 0.9.0

**More of the SFZ format: other open libraries now play as their makers wrote them** (you: "Let's implement what's
left on the plan"; this is its first step). VSCO 2 CE used little of SFZ; **Sonatina Symphonic Orchestra**, the plan's
second library (free, Creative Commons Sampling Plus), uses much more, and PR7-O skipped it. Counted over Sonatina's 762
files, PR7-O now reads and plays:
- **Release samples:** sounds that play when you let a key go (Sonatina's organ: the pipes stopping), after the sustain
  pedal too, quieter the longer the key was held; and **first / legato samples**, Sonatina's own legato: a different
  recording for the first note of a phrase and for notes played over a held one.
- **Filters:** the files' own tone shaping (low-pass, high-pass, band-pass, notch), following the key, the velocity and
  the mod wheel. Sonatina's mod wheel brightens as well as swells.
- **The files' own mod wheel crossfades** between soft and loud recordings, and level from controllers. When a file
  does this itself, PR7-O's own MOD WHEEL dynamics leaves it alone.
- **Crossfades on velocity and keys**, velocity curves, level by key, pitch by velocity, the envelope moved by velocity
  and controllers.
- **Groups** (a new note cutting off an old one), a start held back, a random start point, stereo width, a slow level
  wobble, controller starting values (Sonatina's violins start their mod wheel at 96).

Checked on five Sonatina instruments (violins sustain and legato, solo horn legato, trumpets marcato, an organ stop):
all load with no problems, and the files' own controls work (the violins swell 17 dB and brighten on the mod wheel; the
organ's release samples sound as each chord is let go). VSCO 2 CE plays exactly as before.

Not read yet: the newer numbered LFOs and envelopes some files use (Sonatina's strings add vibrato with them on CC 21,
which the files themselves start at 0: at their defaults you hear no difference), equalisers, and custom curves. They're
listed in the instrument's info as before.

Not checked: listening.

## 0.8.0

**THE STAGE** (you: "Let's do the stage", then "Build it" after the mock-up). Each part of what you load now has a
**seat** on our own neon concert stage, and with **STAGE** on it's heard from there, through a pair of mics in front of
the stage, in a room, so parts recorded apart sound like one orchestra in one space.

- **Where it is:** the picture panel's new **STAGE** view (between INSTRUMENT and REGIONS): the stage seen from behind
  the conductor, every part a lit zone at its seat, brighter while it plays. **Drag a part** to move it (point at one to
  see how far left or right and how far back it sits). The orchestra's usual places are drawn faintly as a guide.
- **Usual seats** come from each part's name: violins front left, violas centre right, cellos front right, basses
  behind them, woodwinds in the middle, horns left at the back, trumpets centre, trombones and tuba right, timpani and
  percussion on the top riser, harp and keyboards at the left edge. A single instrument gets its usual seat too.
- **The controls**, on the rack's new third page, **STAGE**: **MIC** (how far in front of the stage the mics stand:
  close, the parts clear and dry; far, more room; 2.5 m to start), **ROOM LEVEL** (the room's reflections and ring; -3 dB
  to start, as open samples carry some room of their own), **DECAY**, **SPREAD** (everyone towards the middle, or wider),
  the **ROOM** (Studio, Scoring Stage, Concert Hall, Cathedral), **STAGE** on / off, **RESET SEATS**.
- **How it sounds** (our own design, nothing measured from a real hall): from its seat a part reaches each mic directly
  (farther: a little later, quieter and duller; on the left: the left mic first and louder), then off the floor,
  ceiling and walls, then one room ring every part shares. A part in the middle of the stage keeps the level it plays
  at, so moving the mics changes how roomy it sounds, not how loud. The delay to the front of the stage is taken off:
  playing stays tight. Moving a seat while it plays glides (no clicks); switching STAGE on or off crossfades.
- **Seats are saved** with your project and in presets, and moving one is one undo step.

STAGE is off by default: your projects and presets sound as before.

Worth knowing: with VSCO's strings at the defaults, each section alone comes out about 4 dB to its side (violins
left, cellos right) plus the mics' timing; the basses barely move sideways (their low notes are mostly room by the time
they reach mics this far off): bring MIC closer for clearer seats. One plugin instance is one room: a section loaded in
one instance shares the room exactly; instances in your DAW set the same will match closely, but are separate rooms.

Not checked: listening (how the rooms and seats sound to you).

## 0.7.0

**LEGATO** (you chose it as the next step; our own design, since the open libraries have no recorded transitions
between notes). A new key on the rack, beside AUTO SUSTAIN. With it on, a note you play while still holding a sustained
one takes over from it instead of starting again:
- the old note fades out as the new one fades in, with no dip in loudness;
- the new note starts **past its attack** (no fresh bow stroke or tongued start), so the line flows. Where the attack is
  over is measured on each recording as it loads: VSCO's violins swell in slowly and skip about 0.75 s, cellos about
  0.15 s, horn and trombone about 0.1 s;
- the join follows how you play: quick notes join in about 30 ms, slow ones in up to 150 ms;
- let go of the new note while still holding the old key and it flows back (trills);
- notes pressed together as a chord (within 40 ms), short articulations (staccato, pizzicato) and notes more than an
  octave apart are never joined.

**GLIDE**, a seventh knob after POLYPHONY: with LEGATO, a joined note slides in pitch from the note before over this
time (0-500 ms), as a string player shifts.

LEGATO is off and GLIDE at 0 by default: your presets and projects play as before.

Worth knowing: holding a chord with one hand while playing a melody with the other, a melody note can take over the
chord note nearest it (within an octave): for pads under a melody, leave LEGATO off. On loud violins the difference is
subtle (they reach full level in about 0.2 s anyway); on soft and slowly swelling notes it's large. The join times and
the chord window are our guesses: tell us how they feel.

## 0.6.0

**The brass in the neon look** (you: "Build it but make the metal a bit more visible", after the mock-up). Load a
trumpet, horn, trombone or tuba (by its file's name: VSCO 2 CE's TrumpetSus, FHornSus, TromboneSus, Tuba-KS...) and the
INSTRUMENT page shows our own model of it, lying across with its bell to the right:
- **trumpet, horn, tuba:** the valves a player presses for the note you play light up (the usual charts: B-flat
  trumpet, F horn on its F side, 4-valve B-flat tuba; sounding pitch, TRANSPOSE included). A brass instrument plays one
  note at a time, so with a chord held it shows the highest;
- **trombone:** the slide's positions 1-7 are marked where the player's hand goes, the note's position lit with the
  slide's run to it (B-flat tenor without the F trigger: the notes it can't reach light nothing).
Trumpet, horn and trombone share one scale (the trombone framed on its bell and slide end); the tuba, 92 cm tall, is
shown whole. **The metal is more visible than in the mock-up:** satin gunmetal with two lights that shine on the
instruments only, so the floor stays black.

**A brass section** (NEW SECTION... with the brass files) shows the four together: the tuba standing at the left, the
trombone across the top, the horn and trumpet under it, each lit while its part plays, with its valves or slide
position. A section mixing strings and brass shows the family with more parts.

**Smarter split points for a section.** 0.4.0 started each part at its own lowest note, which works for the strings
but not the brass (VSCO's horn recordings start under the trombone's: the horn got a key or two). Now the parts are
ordered by the middle of their ranges, and a part starts at its own lowest note only if the part below keeps at least
7 keys; otherwise the two split the notes they share down the middle. VSCO's brass: tuba F0-B1, trombone C2-G#2, horn
A2-G3, trumpet G#3-C5 (MIDI 29-47, 48-56, 57-67, 68-84; middle C = C3, as the page names them). The strings' splits
are as before. You can still move any split with - / +.

Not checked: listening (how the brass section sounds with these splits), and how the pictures look on your screen.

## 0.5.1

**The single instruments bigger, at one common scale** (you: "much closer to the scale that we used for the guitar in
the PR7-I ... We don't need to see the entire body or the entire headstock", then "all four instruments to one common
scale", and of three common scales you chose the halfway one). A violin, viola, cello or double bass now lies across
the picture panel at the same scale as the others, so their sizes compare as in life:
- the **violin and viola** whole, about as big as 0.5.0's violin (0.5.0 fitted each instrument to the panel, so a
  violin looked as big as a double bass);
- the **cello and double bass** much bigger, framed on their nut end where low-position notes are played: the scroll
  at the right, the upper body showing, the bridge end running off the left edge. A note stopped beyond the edge (their
  highest, above about 15-18 semitones up a string) lights at the edge, on its string, so whatever sounds shows.
The scale sits halfway between the violin's strings filling the panel and the bass's (0.654 m across the panel).

**The strings run above the fingerboard to the nut.** Zoomed in, a slip in our model showed: the strings sank into
the fingerboard near the nut and were hidden there (in every picture, the section's too). Fixed and rendered again.
(The lit notes are drawn by the plugin, so they were always right.)

Not checked: how it looks on your screen at your window size, and listening.

## 0.5.0

**THE INSTRUMENT page in the family's "Tron" look, with PR7-I's rack** (you chose both mock-ups, and the rack system
from PR7-I for the knobs and buttons). This release also brings 0.4.0's **sections** (below), which never reached a
release: its build failed (a test that only failed on Windows, then the crash fixed here).

**THE ORCHESTRA VIEW** (our own 3D string family, modelled and rendered in Blender as PR7-D's kit and PR7-I's guitar
are: the same gloss black, neon running pink to orange, grid floor and glow):
- **One instrument:** load a violin, viola, cello or double bass and it lies across the picture panel, scroll to the
  right, its lowest string on top. Each note you play lights up on its string where a player would stop it (low on the
  neck, one note a string, a chord's lower notes moving down a string), the string glowing from there to the bridge.
  TRANSPOSE moves them.
- **A section:** bass, cello, viola and violin standing side by side, low to high, at their real sizes; each one glows
  while its part of the split plays (fading out over a quarter second), its notes on its strings.
- Which instrument a file is comes from its name (VSCO 2 CE's ViolinEns, SViolin, ViolaEns, CelloEns, Contrabass; also
  "violin", "viola", "cello", "double bass"...). Brass, woodwinds and percussion aren't modelled yet: they show the
  region map, as before. **INSTRUMENT / REGIONS** over the panel switches between the picture and the region map.

**THE RACK** across the top of the page, drawn with PR7-I's parts (the pearl black unit, the PARAGON PR7-O badge, the
orange screen, the big knobs with their values on small screens, the small keys with lit stripes):
- the **screen**: the instrument's name, the articulation chosen (and its key), where it's from; LOADING with the
  percentage while it loads;
- **LOAD, SECTION, CLEAR** under it (LOAD... / NEW SECTION... / CLEAR before);
- two **PAGES**: **PLAYING** (LEVEL, PAN, TRANSPOSE, TUNE, RELEASE, POLYPHONY, the DYNAMICS menu, the AUTO SUSTAIN key)
  and **ROUTING** (SEND DELAY, SEND REVERB, the INSERT BUS menu). The window's OUTPUT stays beside the pages.
The articulations, SPLITS and the instrument's details stay in the left panel.

**A crash fixed** (inherited from PR7-D's LIMITER): the plugin asks the LIMITER how much delay it adds (for the DAW)
30 times a second, and that read the LIMITER's oversamplers, which the DAW replaces whenever it restarts the audio, and
which don't exist before the audio has started. With the LIMITER on and OVERSAMPLING set, a DAW starting or restarting
audio at the wrong moment could crash it. pluginval's parameter fuzzing caught it, here about one run in eight. The
delays are now measured once, so asking never touches them; tested from two threads at once.

**From 0.4.0: SECTIONS, several instruments split over one keyboard** (you asked for "the all strings section",
split). **SECTION** on the rack (or several `.sfz` files dropped on the page): VSCO 2 CE's `Contrabass-KS`,
`CelloEns-KS`, `ViolaEns-KS` and `ViolinEns-KS` play as one instrument, basses C0-B0, cellos C1-B1, violas C2-F#2,
violins G2 up (each starting at the bottom of its own range); **SPLITS** in the left panel, **- / +** move a split. One
set of keyswitches over the top note for all of them (C6 Sustain, C#6 Sustain Non-Vibrato, D6 Tremolo, D#6 Spiccato,
E6 Pizzicato), matched by name across the files. Each part loads only the recordings its keys need (450 MB for the
four instead of about 1 GB); moving a split reads only what's new. Kept in presets and projects, undoable. Details in
0.4.0's notes below.

Not checked: how it looks on your screen at your window size (the pictures fill their panel and are cropped at the
edges, never stretched), and listening.

## 0.4.0 (never released: its build failed; in 0.5.0)

**SECTIONS: several instruments split over one keyboard** (our own design; you asked for "the all strings section",
split). **NEW SECTION...** on the INSTRUMENT page (or drop several `.sfz` files on it at once): choose, say, VSCO 2 CE's
`Contrabass-KS`, `CelloEns-KS`, `ViolaEns-KS` and `ViolinEns-KS`, and they play as one instrument, each on its own keys:
basses at the bottom, then cellos, violas, violins.

- **Where the splits go:** PR7-O orders the files low to high and starts each at the bottom of its own range, the one
  under it stopping a key below. For VSCO's strings: basses C0-B0, cellos C1-B1, violas C2-F#2, violins G2 up (PR7-O's
  names, middle C = C3). **SPLITS** on the page lists each part's keys; **- / +** move a split a semitone (each part keeps
  at least one key). The keyboard shades the parts in turn so you can see them.
- **One keyswitch for all of them:** each file's own switch keys sit inside another part's range (the violins' C1-D#1
  are cello notes, the basses' and cellos' C5-E5 are violin notes), so a section has switches of its own, just above its
  top note: for the strings C6 Sustain, C#6 Sustain Non-Vibrato, D6 Tremolo, D#6 Spiccato, E6 Pizzicato, starting on
  Sustain. The articulations are matched by their names across the files (sustain, tremolo, spiccato, pizzicato...);
  where a part has no match it plays its nearest (Sustain Non-Vibrato: the basses' own, the others their sustain), and
  the old switch keys play notes again.
- **Memory:** each part loads only the recordings its keys need: 450 MB for the four string files split (the four whole
  would be about 1 GB). Moving a split reuses what's in memory and reads only what's newly needed: on VSCO's strings it
  took 0.8 s and read nothing new; the whole section loads in about 2.5 s (once the files are in the computer's cache).
- A section is kept in presets and projects like a file, and moving a split is one undo step (several clicks in a row
  count as one: it's applied 0.3 s after the last).

Checked on the real files (in the build machine's scratch space): every switch plays the right recording in each of
the four parts, nothing missing or unread. A demo is rendered. Not checked: listening.

A tip: four sections at VSCO's +20 dB make a big chord peak over 0 dBFS (a ten-note E minor chord reached +2 dBFS);
turn LEVEL down 6 dB or so for full chords.

## 0.3.0

**The rest of VSCO 2 Community Edition plays properly**: woodwinds (flute, piccolo, oboe, clarinet, bassoon), brass
(trumpet, horn, trombone, tuba, the mutes), the organ, timpani and rolls, harp, upright pianos, mallets, bells and the
percussion kit. Built against the real files (Versilian Studios, CC0; read in the build machine's scratch space, never
shipped): all 75 of VSCO's instruments load with no problems and nothing left unread. They needed nothing new from the
SFZ reader; what changed is how PR7-O tells a held note from a short one, and how AUTO SUSTAIN loops it.

**Held or short, judged on the note itself** (our own rule, refitted): 0.2.0's rule was fitted on the strings' long
bowed notes, and called these short: brass and wind notes recorded 3-5 s long (the player's breath runs out after 1-2 s
of steady tone), organ pipes (a "chiff" 16 dB over the note as the pipe speaks, then perfectly steady), accented
sustains and timpani rolls. So they faded out when held past their recording. Now the rule looks at the note's core
(from where it reaches its typical level to where it drops away) instead of a fixed middle part of the file, and
allows an onset up to 18 dB over the note. Checked on all 1914 of VSCO's recordings outside the percussion kit, against
what their file names say (sustain, vibrato, tremolo, roll, organ: held; staccato, spiccato, pizzicato, harp, mallets,
piano: not): it misjudges 7 (0.2.0's: 41). Four of those 7 are trumpet sus-vib notes that really do die away in the
recording; they play to their end. In the kit, the sounds that keep going (snare and tambourine rolls, ratchets, bowed
cymbal, bass drum rubs) count as held; hits don't.

**AUTO SUSTAIN, better loops** (our own design):
- **A burst stays out of the loop.** Some of VSCO's quiet organ notes have a burst of wind or key noise 6-11 dB over the
  note near their end; 0.2.0's loop took it in, so it came round every 4 s. Now the loop is the longest stretch without
  one.
- **The crossfade fits the sound.** A near-pure tone (piccolo, flute) whose pitch moves a few cents over the note
  drifts out of phase across a 1 s crossfade and dips partway through (VSCO's piccolo G5: 5 dB every time round). Now
  PR7-O tries 1, 1/2, 1/4 and 1/8 s and takes the longest whose two sides still match; the loop's start is searched over
  0.1 s (was 25 ms), judged by the whole crossfade rather than a 40 ms snippet.
- **A note still settling is tilted.** A timpani roll gets quieter as it's held (6-12 dB over 16 s); looped, it jumped
  back up each time round. Now the loop is turned up across its length by the difference (up to 6 dB), so it arrives at
  the level it goes back to; past the loop (when you let go) it stays at that, so nothing steps.
- A short note needs 1 s of steady tone to loop (was 1.5 s): VSCO's tuba notes have about that much.

Measured, holding 9-21 notes of each of 19 sustain instruments for 25 s: the level while looping (12-24 s) moves no more
than over the recording's own steady part. Strings 4-6 dB (the bowing, as in 0.2.0); brass 1.5-2.2 dB (trombone vibrato
3 dB, against 7 in its recordings); woodwinds 0.6-2.9 dB; organ 1-3 dB; timpani rolls 3.3 dB (its recording: 4.2);
piccolo 2.9 dB (worst note 4.6; 0.2.0: 7.8). Not checked: whether the joins can be heard (your ears). A roll's texture
repeats every few seconds once looping, which a trained ear may pick out.

Loading takes longer: the wider search for each loop's start makes a big keyswitch file (Flute-KS, 150 MB) take about
1 s instead of 0.4 s, in the background as before.

VSCO's keyswitches as their files set them: clarinet C1-C#1, flute C1-D#1, tuba C5-C#5 (PR7-O's names, middle C = C3).
Percussion, piano, harp and mallets ring on for their release when let go, as their files say.

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
