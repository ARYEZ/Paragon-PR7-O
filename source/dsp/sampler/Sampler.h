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
            Dynamics dynamics = Dynamics::velocity;
            bool autoSustain = true;
            bool legato = false;        // 0.7.0
            float glideSeconds = 0.0f;  // 0.7.0: a joined note's slide in pitch
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
        };
        struct Join { int from = -1; double seconds = 0.0, glide = 0.0; };   // a legato join (from: the note joined)

        void noteOn (const Program&, int note, int velocity, int channel, const Settings&) noexcept;
        bool playNote (const Program&, int note, int velocity, int channel, const Settings&, int joinFrom) noexcept;
        void noteOff (const Program*, int note, int channel, const Settings&) noexcept;
        int joinableFrom (int note) const noexcept;   // LEGATO: the sounding note a new one would join (-1: none)
        void startVoice (const Region&, int note, int velocity, int channel, const Settings&, std::uint64_t noteId,
                         float lowerMiddle, float upperMiddle, const Join*) noexcept;
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
        double rate = 48000.0;
        float bend = 0.0f;              // the pitch wheel, -1 to 1
        float wheel = 0.0f;             // the mod wheel, 0 to 1
        float expression = 1.0f;        // CC 11, 0 to 1
        bool pedal = false;
        int keyswitch = -1;
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
        std::atomic<float> shownWheel { 0.0f }, shownExpression { 1.0f };
        std::array<std::atomic<bool>, 128> held {};
    };
}
