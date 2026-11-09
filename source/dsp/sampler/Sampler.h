#pragma once

#include "dsp/sampler/Sfz.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>

// THE SAMPLER (PR7-O 0.1.0): an SFZ Program played from MIDI. Our own code.
//
//   - Note on: every region whose keys, velocities, MIDI channel and random range take the note, and whose turn it is
//     in its round robin (each region counts the notes it could have played: it plays when that count, wrapped by
//     seq_length, is its seq_position), starts a voice.
//   - A voice reads its sample at the pitch the key asks (its root key, keytrack, tune, transpose, the pitch wheel's
//     bend range, the file's rate against the host's), with cubic (Hermite) interpolation: a sample at its root key
//     and the host's rate comes out sample for sample.
//   - Its level: the region's volume, velocity through amp_veltrack (SFZ's squared curve: full veltrack, velocity 64
//     about -12 dB), its DAHDSR envelope (attack straight up, decay and release exponential: the release falls 80 dB
//     over its time; never under 5 ms, so a note let go never clicks), its pan (a balance: the centre leaves both sides
//     at full level).
//   - Note off releases it (one_shot regions play on to their end); the sustain pedal (CC 64) holds note-offs until
//     it's lifted; CC 120 / 123 stop everything.
//   - POLYPHONY: past it, the oldest voice (a released one first) fades out over 10 ms rather than cutting. A voice
//     ends at its sample's end (no loops until plan stage 2).
//   - A new Program fades every voice of the old one over 20 ms (the processor keeps the old one 2 s).
// On the audio thread nothing is allocated.
namespace sampler
{
    class Sampler
    {
    public:
        static constexpr int numSlots = 256;       // voices, the fading ones included
        static constexpr int maxPolyphony = 192;   // leaves room for fades

        struct Settings
        {
            float gain = 1.0f;          // LEVEL (linear)
            float pan = 0.0f;           // -1 to 1, a balance after the regions' own
            float transpose = 0.0f;     // semitones
            float tuneCents = 0.0f;
            float releaseScale = 1.0f;  // RELEASE: x the regions' release times
            int polyphony = 128;
            int channel = 0;            // MIDI channel listened to (0: any)
        };

        void prepare (double sampleRate);
        void reset() noexcept;          // silence, now

        // Plays `numSamples` into (adds to) `left` / `right`, with `midi`'s events whose positions fall in
        // [midiStart, midiStart + numSamples).
        void render (const Program*, float* left, float* right, int numSamples, const juce::MidiBuffer& midi, int midiStart,
                     const Settings&) noexcept;

        int getActiveVoices() const noexcept { return activeVoices.load(); }
        bool isKeyHeld (int key) const noexcept { return key >= 0 && key < 128 && held[(size_t) key].load(); }
        int getLastNote() const noexcept { return lastNote.load(); }   // -1: none yet
        void seedRandom (std::uint32_t seed) noexcept { random = seed != 0 ? seed : 1u; }

    private:
        struct Voice
        {
            bool active = false, released = false, pedalHeld = false;
            const Region* region = nullptr;
            const Sample* sample = nullptr;
            int note = 0, channel = 1;
            double position = 0.0, increment = 1.0, baseIncrement = 1.0;
            float gainLeft = 1.0f, gainRight = 1.0f;
            juce::int64 lastFrame = 0;
            std::uint64_t age = 0;
            // The envelope.
            enum class Stage { delay, attack, hold, decay, sustain, release } stage = Stage::delay;
            float level = 0.0f, sustain = 1.0f, attackStep = 1.0f, decayCoef = 0.0f, releaseCoef = 0.0f;
            int countdown = 0, holdSamples = 0;
            float fade = 1.0f, fadeStep = 0.0f;   // a quick fade (stolen, program changed, all sound off)
        };

        void noteOn (const Program&, int note, int velocity, int channel, const Settings&) noexcept;
        void noteOff (int note, int channel) noexcept;
        void startVoice (const Region&, int note, int velocity, int channel, const Settings&) noexcept;
        void release (Voice&) noexcept;
        void fadeOut (Voice&, double seconds) noexcept;
        void updatePitch (const Settings&) noexcept;
        void renderVoices (float* left, float* right, int numSamples) noexcept;
        float nextRandom() noexcept;

        std::array<Voice, numSlots> voices {};
        const Program* current = nullptr;
        double rate = 48000.0;
        float bend = 0.0f;              // the pitch wheel, -1 to 1
        bool pedal = false;
        std::uint64_t counter = 0;
        std::uint32_t random = 0x9e3779b9u;
        float shownGain = -1.0f, shownLeft = 1.0f, shownRight = 1.0f;   // the LEVEL / PAN the last block ended on
        std::array<float, 256> scratchLeft {}, scratchRight {};
        std::atomic<int> activeVoices { 0 }, lastNote { -1 };
        std::array<std::atomic<bool>, 128> held {};
    };
}
