#pragma once

#include "dsp/mixer/MixerEffects.h"
#include "dsp/mixer/Strip.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <map>
#include <atomic>

// THE MIXER (PR7-O 0.15.0; the owner, after the rack-style mock-up: "Build it in that style and give it its own mixer
// tab"): a strip for each part of the instrument (a section's parts, or the one instrument), between the sampler and
// THE STAGE, and one for the whole section after the stage. Our own design, from PR7-D's mixer pieces (Mixer.h, its
// effects in MixerEffects.h). Each strip, in this order:
//   EFFECTS   four slots (EQ, compressor, gate, transient, drive, lo-fi, mod, saturation), each on or off;
//   PHASE     the polarity flipped;
//   PAN       a balance (the far side down, the near side as it is), as the instrument's own PAN;
//   LEVEL     the fader, -60 dB (off) to +12 dB;
//   MUTE / SOLO (a part soloed: the parts not soloed are muted);
//   SENDS     a copy into DELAY and into REVERB, after the fader (only while that effect is on; the processor knows).
// Every gain moves over 20 ms (a flipped phase passes through silence), so no move clicks. A part strip left as it
// starts (no effects, 0 dB, centre, nothing sent, not muted) costs nothing.
namespace mixer
{
    class PartMixer
    {
    public:
        PartMixer();

        // With the audio stopped.
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        // Message thread: the strips by part number (then the SECTION strip at sectionStrip); `numParts` in use. The
        // effects are made here; the audio thread takes the new settings at its next block.
        void setStrips (const std::array<StripSettings, numMixerStrips>&, int numParts);
        void freeRetired() { host.freeRetired(); }

        // Audio thread, each block: take any new settings; then each part's strip, in place (its sends added into the
        // send pairs given, null for a send that's off); then the section's.
        void beginBlock() noexcept;
        void processPart (int part, float* left, float* right, int numSamples,
                          float* delayLeft, float* delayRight, float* reverbLeft, float* reverbRight) noexcept;
        void processSection (float* left, float* right, int numSamples,
                             float* delayLeft, float* delayRight, float* reverbLeft, float* reverbRight) noexcept;

        // Meters: a strip's peak level (0-1, either side) since it was last read, after its fader.
        float takePeak (int strip, int side) noexcept;
        const mixerfx::Host& getHost() const noexcept { return host; }

    private:
        struct Live
        {
            juce::SmoothedValue<float> left { 1.0f }, right { 1.0f }, delay { 0.0f }, reverb { 0.0f };
        };
        void processStrip (int strip, bool audible, float* left, float* right, int numSamples,
                           float* delayLeft, float* delayRight, float* reverbLeft, float* reverbRight) noexcept;

        mixerfx::Host host;
        juce::SpinLock lock;
        std::array<StripSettings, numMixerStrips> pending {}, current {};
        int pendingParts = 1, currentParts = 1;
        std::atomic<int> pendingVersion { 0 };
        int currentVersion = -1;
        FxTable table {};   // the audio thread's, for the host (parts at their numbers, the section at sectionStrip)
        std::array<Live, numMixerStrips> live;
        std::array<std::array<std::atomic<float>, 2>, numMixerStrips> peaks {};
        bool anySolo = false;
        double rate = 48000.0;
    };
}
