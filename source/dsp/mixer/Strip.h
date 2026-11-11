#pragma once

#include "dsp/mixer/Mixer.h"

#include <array>
#include <map>
#include <memory>

// THE MIXER's strip settings (PR7-O 0.15.0), apart from the engine (PartMixer.h) so presets can hold them without the
// effects' code: a part's strip, or the SECTION strip after the stage. See PartMixer.h for what each does.
namespace mixer
{
    inline constexpr int maxPartStrips = 8;              // the stage's (and the sampler's) most parts
    inline constexpr int sectionStrip = maxPartStrips;   // the strip after the stage
    inline constexpr int numMixerStrips = maxPartStrips + 1;
    inline constexpr float levelOffDb = -60.0f, levelTopDb = 12.0f, sendTopDb = 6.0f;

    struct StripSettings
    {
        float levelDb = 0.0f;             // the fader: levelOffDb (silent) to levelTopDb
        float pan = 0.0f;                 // -1 (left) to 1 (right)
        bool mute = false, solo = false, invert = false;
        float delayDb = levelOffDb;       // SEND DELAY, off to sendTopDb
        float reverbDb = levelOffDb;      // SEND REVERB
        std::array<FxSlot, numFxSlots> fx {};

        bool operator== (const StripSettings& o) const noexcept
        {
            return std::abs (levelDb - o.levelDb) < 1.0e-4f && std::abs (pan - o.pan) < 1.0e-4f && mute == o.mute && solo == o.solo
                && invert == o.invert && std::abs (delayDb - o.delayDb) < 1.0e-4f && std::abs (reverbDb - o.reverbDb) < 1.0e-4f && fx == o.fx;
        }
        bool isPlain() const noexcept { return *this == StripSettings {}; }
        bool hasEffects() const noexcept;

        static float gainOf (float db, float offDb = levelOffDb) noexcept
        {
            return db <= offDb + 0.05f ? 0.0f : juce::Decibels::decibelsToGain (db);
        }

        // Saved in projects and presets (only what differs from a plain strip is worth keeping; `name` its part's).
        std::unique_ptr<juce::XmlElement> toXml (const juce::String& tag, const juce::String& name) const;
        static StripSettings fromXml (const juce::XmlElement&);
    };

    // The mixer in projects and presets: <Mixer><Strip name="..." .../>...<Section .../></Mixer>, only strips that
    // differ from plain ones.
    std::unique_ptr<juce::XmlElement> mixerXml (const std::map<juce::String, StripSettings>& parts, const StripSettings& section);
    void readMixerXml (const juce::XmlElement&, std::map<juce::String, StripSettings>& parts, StripSettings& section);

}
