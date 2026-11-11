#pragma once

#include "dsp/sampler/Sfz.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

// THE SAMPLER (PR7-O 0.1.0): an SFZ Program played from MIDI. Our own code.
//
//   - Note on: every region whose keys, velocities, MIDI channel and random range take the note, and whose turn it is
//     in its round robin (each region counts the notes it could have played: it plays when that count, wrapped by
//     seq_length, is its seq_position), starts a voice. 0.2.0: and whose KEYSWITCH is the one chosen: a key in the
//     program's switching range (sw_lokey to sw_hikey) chooses (it doesn't sound); the program starts on sw_default (none
//     given: its first articulation, our choice: SFZ would leave those regions silent until a switch is pressed).
//   - A voice reads its sample at the pitch the key asks (its root key, keytrack, tune, transpose, the pitch wheel's
//     bend range, the file's rate against the host's), with cubic (Hermite) interpolation: a sample at its root key
//     and the host's rate comes out sample for sample.
//   - Its level: the region's volume, velocity through amp_veltrack (SFZ's squared curve: full veltrack, velocity 64
//     about -12 dB), its DAHDSR envelope (attack straight up, decay and release exponential: the release falls 80 dB
//     over its time; never under 5 ms, so a note let go never clicks), its pan (a balance: the centre leaves both sides
//     at full level). 0.2.0: CC 11 (EXPRESSION) on everything, the same squared curve.
//   - 0.2.0, DYNAMICS ON THE MOD WHEEL (our own design; SFZ files can say it with xfin / xfout, VSCO 2 CE's don't): a
//     held recording (Region::held) starts every velocity layer at once, and the mod wheel (CC 1) crossfades between
//     them (equal power between neighbouring layers' middles) and sets their level as velocity would. The note's own
//     velocity then only chooses shorts (spiccato, pizzicato).
//   - 0.2.0, LOOPS: a region's loop (loop_continuous; loop_sustain until the note is let go) plays round, crossfading
//     its end into the frames before its start over loop_crossfade, with a curve that keeps the level for how alike the
//     two sides are (r = their correlation: weights (1 - t) and t, divided by sqrt (w1² + w2² + 2 r w1 w2): linear
//     when they match, equal power when they're unrelated). AUTO SUSTAIN (our own design) gives a
//     held recording without loop points one, found where the recording is steady (findSustain), so a note held past
//     the recording's end keeps sounding. 0.3.0: its loop TILTED (Region::loopTilt: the gain rising across the loop,
//     in dB, to make up how much quieter its end is than the sound it goes back to; held at its end value past the
//     loop, so letting go doesn't step), for notes still settling as they're held (a timpani roll).
//   - Note off releases it (one_shot regions play on to their end); the sustain pedal (CC 64) holds note-offs until
//     it's lifted; CC 120 / 123 stop everything.
//   - POLYPHONY counts NOTES (0.2.0; all the voices one note started are one). Past it, a note fades out over 10 ms:
//     the oldest let go, else the oldest held that isn't the chord's lowest or highest (a big chord keeps its bass and
//     its top line).
//   - A new Program fades every voice of the old one over 20 ms (the processor keeps the old one 2 s).
//   - 0.7.0, LEGATO (our own design: the open libraries have no recorded transitions): while it's on, a note pressed
//     while a held recording (Region::held) still sounds from a key pressed at least `chordWindow` before it (so a
//     chord isn't joined), the nearest within an octave, JOINS it: the old note fades out as the new one fades in (equal
//     power), the new one started past its attack (Region::legatoStart). The crossfade follows how fast you play: a
//     quarter of the time since the old note began, 30 to 150 ms. GLIDE slides both notes' pitch from the old note's to
//     the new one's. Let go of the joined note while the key it came from is still down and it joins back (a trill).
//     Shorts (not held) are never joined.
//   - 0.10.0, THE ENSEMBLE (our own design): PLAYERS - each region a note plays starts that many voices: the first as
//     ever, each other one a little out of tune (within +-detuneCents: a spread of our choosing, a little different each
//     note), a little later (up to timingMs: never earlier, which would need playing ahead of the note), a little
//     further into its recording (up to 8 ms) and somewhere else across (a balance, the others either side of the first);
//     each side scaled by 1 / sqrt (the sum of its players' squared gains), so its level holds. DOUBLE - the note an octave up and / or down too (from whatever region plays that key: in a section,
//     the part that has it), at doubleGain; it ends with the key played, counts as the same note for POLYPHONY, and a
//     legato join takes it along.
// On the audio thread nothing is allocated.
namespace sampler
{
    class Sampler
    {
    public:
        static constexpr int numSlots = 1024;      // voices, the fading ones included (0.2.0: several a note on the mod wheel)
        static constexpr int maxPolyphony = 192;   // notes

        enum class Dynamics { velocity, modWheel };
        struct Settings
        {
            float gain = 1.0f;          // LEVEL (linear)
            float pan = 0.0f;           // -1 to 1, a balance after the regions' own
            float transpose = 0.0f;     // semitones
            float tuneCents = 0.0f;
            float releaseScale = 1.0f;  // RELEASE: x the regions' release times
            int polyphony = 128;        // notes
            int channel = 0;            // MIDI channel listened to (0: any)
            // 0.16.0: ARTICULATIONS switched by the keys (0), a controller (1: `switchCC`'s value, 0-127 split evenly
            // between them) or the MIDI channel (2: a note on channel n plays the n-th). The keys always switch too.
            int switchBy = 0, switchCC = 20;
            Dynamics dynamics = Dynamics::velocity;
            bool autoSustain = true;
            bool legato = false;        // 0.7.0
            float glideSeconds = 0.0f;  // 0.7.0: a joined note's slide in pitch
            // 0.10.0, THE ENSEMBLE: players a note (1-4), how far out of tune and how late the extra ones may be, and the
            // octaves doubled (0 none, 1 up, 2 down, 3 both) at doubleGain.
            int players = 1;
            float detuneCents = 8.0f, timingMs = 12.0f;
            int doubleMode = 0;
            float doubleGain = 0.5f;
            // 0.11.0: bouncing (non-realtime): a streamed recording's frames not read from disk yet are read there and
            // then, instead of being silent.
            bool waitForDisk = false;
        };
        static constexpr double chordWindow = 0.04;                                  // seconds (0.7.0)
        static constexpr double shortestJoin = 0.03, longestJoin = 0.15;             // the legato crossfade's range
        // The legato crossfade for a note joined `since` seconds after the one it joins began.
        static double joinSeconds (double since) noexcept { return juce::jlimit (shortestJoin, longestJoin, 0.25 * since); }

        Sampler();
        void prepare (double sampleRate);
        void reset() noexcept;          // silence, now

        // Plays `numSamples` into (adds to) `left` / `right`, with `midi`'s events whose positions fall in
        // [midiStart, midiStart + numSamples). 0.8.0, THE STAGE: with `parts`, each region's part (Region::part; past
        // the last given, the last) is added into its own pair instead, and `left` / `right` are left alone.
        static constexpr int maxParts = 8;
        struct PartOutputs { std::array<float*, maxParts> left {}, right {}; int count = 0; };
        void render (const Program*, float* left, float* right, int numSamples, const juce::MidiBuffer& midi, int midiStart,
                     const Settings&, const PartOutputs* parts = nullptr) noexcept;

        // 0.11.0, DISK STREAMING: the streamer streamed recordings are read through (null: they play what's in memory
        // only). Any thread (taken at the next render); it must outlive the sampler.
        void setStreamer (Streamer* s) noexcept { streamerToUse.store (s, std::memory_order_release); }

        int getActiveVoices() const noexcept { return activeVoices.load(); }
        int getActiveNotes() const noexcept { return activeNotes.load(); }
        bool isKeyHeld (int key) const noexcept { return key >= 0 && key < 128 && held[(size_t) key].load(); }
        int getLastNote() const noexcept { return lastNote.load(); }   // -1: none yet
        void seedRandom (std::uint32_t seed) noexcept { random = seed != 0 ? seed : 1u; }
        // KEYSWITCHES: the switch chosen (-1: none), and choosing one from the page (applied at the next block).
        int getKeyswitch() const noexcept { return shownSwitch.load(); }
        void chooseKeyswitch (int key) noexcept { pendingSwitch = key; }
        // The controllers as they are now (0 to 1): the mod wheel and EXPRESSION.
        float getModWheel() const noexcept { return shownWheel.load(); }
        float getExpression() const noexcept { return shownExpression.load(); }

    private:
        struct Voice
        {
            bool active = false, released = false, pedalHeld = false;
            const Region* region = nullptr;
            const Sample* sample = nullptr;
            int note = 0, channel = 1;
            std::uint64_t noteId = 0;   // the note-on that started it (POLYPHONY counts these)
            double position = 0.0, increment = 1.0, baseIncrement = 1.0;
            float gainLeft = 1.0f, gainRight = 1.0f;
            juce::int64 lastFrame = 0;
            std::uint64_t age = 0;
            // The envelope.
            enum class Stage { delay, attack, hold, decay, sustain, release } stage = Stage::delay;
            float level = 0.0f, sustain = 1.0f, attackStep = 1.0f, decayCoef = 0.0f, releaseCoef = 0.0f;
            int countdown = 0, holdSamples = 0;
            bool fading = false;                  // a quick fade (stolen, program changed, all sound off)
            float fade = 1.0f, fadeStep = 0.0f;
            // The loop (looping: going round; stopAtWrap: let go inside its crossfade, so it finishes it first).
            bool looping = false, stopAtWrap = false;
            juce::int64 loopStart = 0, loopEnd = 0;
            int crossfade = 0;
            float likeness = 0.0f;   // the loop's two sides' correlation (the crossfade keeps the level for it)
            // 0.3.0, AUTO SUSTAIN's tilt: the gain rises across the loop (exp (tiltK x frames past its start), to
            // tiltEnd at its end, held there past it), so each time round it ends at the level it goes back to.
            bool tilted = false;
            double tiltK = 0.0;
            float tiltGain = 1.0f, tiltStep = 1.0f, tiltEnd = 1.0f;
            // DYNAMICS ON THE MOD WHEEL: this layer's middle and its neighbours' (in velocity, 1-127; -1: none), and the
            // gain the wheel gives it now (ramped across each piece).
            bool onWheel = false;
            float layerMiddle = 64.0f, lowerMiddle = -1.0f, upperMiddle = -1.0f, velTrack = 1.0f;
            float wheelGain = 1.0f, wheelTarget = 1.0f;
            // 0.7.0, LEGATO: fading in (its attack an equal-power rise) or out (its fade an equal-power fall) as a join,
            // and GLIDE: its pitch times glideRatio, moving by glideStep each sample for glideCount samples to glideTarget.
            bool legatoIn = false, legatoOut = false;
            double glideRatio = 1.0, glideStep = 1.0, glideTarget = 1.0;
            int glideCount = 0;
            // 0.9.0: the file's own controls. Level from controllers and controller crossfades (ramped across each
            // piece), a start held back (`waiting` samples), its filters (state-variable, both sides), its level LFO.
            float controlGain = 1.0f, controlTarget = 1.0f;
            int waiting = 0;
            int velocity = 100;
            struct FilterState
            {
                bool on = false, onePole = false;
                Region::Filter::Type type = Region::Filter::Type::none;
                float baseHz = 1000.0f, k = 1.4142f, g = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, G = 0.0f;
                std::array<float, 2> ic1 {}, ic2 {};
            };
            std::array<FilterState, 2> filters {};
            double lfoPhase = 0.0, lfoStep = 0.0;
            int lfoAge = 0;
            float lfoGain = 1.0f;
            // 0.10.0: the key whose note-off ends it (a doubled octave: the key played), and whether it's a doubling.
            int owner = 0;
            bool isDouble = false;
            // 0.11.0, DISK STREAMING: its stream (-1: none), how far its timeline is ahead of its position (the loop's
            // length each time round), and where it read last: in memory (file frames) and in the stream's ring
            // (timeline frames; `behind`: the crossfade's back reads).
            int stream = -1;
            juce::int64 lap = 0;
            struct Window { const float* left = nullptr; const float* right = nullptr; juce::int64 from = 0, to = 0; };
            Window inMemory, ahead, behind;
        };
        float streamedFrame (Voice&, int channel, juce::int64 frame, bool back, bool wait) noexcept;
        void startStream (Voice&) noexcept;
        void endStream (Voice&) noexcept;
        void leaveLoop (Voice&) noexcept;   // its loop stopped going round: the stream told
        // 0.10.0: how one player of a note differs (startVoice), and what a doubled octave carries (playNote).
        struct Copy { float cents = 0.0f, pan = 0.0f, gainLeft = 1.0f, gainRight = 1.0f; int delay = 0; double offsetSeconds = 0.0; int owner = -1; bool isDouble = false; };
        struct NoteExtras { int owner = -1; float gain = 1.0f; bool isDouble = false; std::uint64_t noteId = 0; int joinTimeKey = -1; };
        struct Join { int from = -1; double seconds = 0.0, glide = 0.0; };   // a legato join (from: the note joined)

        void noteOn (const Program&, int note, int velocity, int channel, const Settings&) noexcept;
        bool playNote (const Program&, int note, int velocity, int channel, const Settings&, int joinFrom, const NoteExtras&) noexcept;
        std::uint64_t lastNoteId = 0;   // the note playNote last started (0: none)
        void noteOff (const Program*, int note, int channel, const Settings&) noexcept;
        // 0.9.0: release samples: at a key's going up (release_key; release too unless the pedal holds it), or at the
        // pedal's lifting (`pedalLifted`: release only).
        void playReleases (const Program&, int note, int channel, const Settings&, bool pedalLifted = false) noexcept;
        bool conditionsMet (const Region&) const noexcept;   // 0.9.0: its locc / hicc ranges
        float controlGainOf (const Voice&) const noexcept;   // 0.9.0: controllers' level and crossfades now
        void updateFilters (Voice&) noexcept;                 // 0.9.0: its cutoffs for the controllers now
        void controllersChanged() noexcept;
        int joinableFrom (int note) const noexcept;   // LEGATO: the sounding note a new one would join (-1: none)
        void startVoice (const Region&, int note, int velocity, int channel, const Settings&, std::uint64_t noteId,
                         float lowerMiddle, float upperMiddle, const Join*, float extraGain = 1.0f, const Copy* = nullptr) noexcept;
        void makeRoom (const Settings&) noexcept;   // POLYPHONY: a note faded if one more would pass it
        void release (Voice&) noexcept;
        void fadeOut (Voice&, double seconds) noexcept;
        void fadeNote (std::uint64_t noteId, double seconds) noexcept;
        void updatePitch (const Settings&) noexcept;
        void updateWheel() noexcept;   // each voice on the wheel: its gain for the wheel now
        float wheelGainOf (const Voice&) const noexcept;
        void renderVoices (int numSamples, int numParts) noexcept;   // into each part's scratch
        float nextRandom() noexcept;

        std::vector<Voice> voices;   // numSlots, made once (on the heap: too big to sit in the processor)
        // Scratch for counting notes (made once: the audio thread's stack stays small).
        struct NoteInfo { std::uint64_t id = 0; std::uint64_t age = 0; int key = 0; bool released = false; };
        std::vector<NoteInfo> noteScratch;
        std::vector<std::uint64_t> idScratch;
        const Program* current = nullptr;
        Streamer* streamer = nullptr;
        std::atomic<Streamer*> streamerToUse { nullptr };
        bool waitForDisk = false;       // 0.11.0: Settings::waitForDisk, for the piece being played
        double rate = 48000.0;
        float bend = 0.0f;              // the pitch wheel, -1 to 1
        // The mod wheel, 0 to 1. 0.13.1: it starts at 100 (a strong mf) until the keyboard sends one (or the file's
        // set_cc1 says otherwise): at 0, DYNAMICS on the mod wheel was silent before the wheel had been touched.
        static constexpr float wheelStart = 100.0f / 127.0f;
        static constexpr float wheelFloor = 32.0f;   // the wheel at 0 plays like this velocity
        float wheel = wheelStart;
        float expression = 1.0f;        // CC 11, 0 to 1
        std::array<float, 128> cc {};   // 0.9.0: every controller's value (0-127), the file's set_ccN at the start
        std::array<bool, 128> releaseDue {};   // 0.9.0: keys let go under the pedal (their release samples come with it)
        bool phraseGoingOn = false;    // 0.9.0: another key was down as this note began
        bool pedal = false;
        int keyswitch = -1;
        // 0.16.0: the articulation (its keyswitch key) a note on `channel` plays: by channel (switchBy 2: the n-th on
        // channel n, while there are that many), else the one chosen.
        int switchFor (const Program&, int channel, const Settings&) const noexcept;
        // 0.7.0: the keys as they're down (velocity, channel, when: the sample clock), and which key each was joined
        // from (-1: none).
        juce::int64 clock = 0, now = 0;
        std::array<bool, 128> keyDown {};
        std::array<int, 128> keyVelocity {}, keyChannel {}, cameFrom {};
        std::array<juce::int64, 128> keyStart {};
        std::uint64_t counter = 0, notes = 0;
        std::uint32_t random = 0x9e3779b9u;
        float shownGain = -1.0f, shownLeft = 1.0f, shownRight = 1.0f;   // LEVEL / PAN / EXPRESSION the last piece ended on
        static constexpr int scratchSize = 256;
        std::array<std::array<float, scratchSize>, maxParts> scratchLeft {}, scratchRight {};   // a part each
        std::array<float, scratchSize> rampLeft {}, rampRight {};
        std::atomic<int> activeVoices { 0 }, activeNotes { 0 }, lastNote { -1 }, shownSwitch { -1 }, pendingSwitch { -1 };
        std::atomic<float> shownWheel { wheelStart }, shownExpression { 1.0f };
        std::array<std::atomic<bool>, 128> held {};
    };
}
