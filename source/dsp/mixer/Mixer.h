#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

// THE MIXER's shared pieces, kept from PR7-D (its 0.20.0-0.39.0 mixer) for PR7-O's stage (plan stage 6), where each
// orchestral section will get a channel strip: a strip's settings, the buses, the sends, and the four effect slots
// MixerEffects.h runs. PR7-D's drum-mic channels (kick in, snare top...), bleed between drums and mic routing were the
// drum kit's and are gone. Nothing plays through these yet in 0.1.0: the single instrument goes straight to the FX
// page's chain. Saved-by-index lists: only append.
namespace mixer
{
    // A channel strip's settings.
    struct ChannelSettings
    {
        float gain = 1.0f;             // the fader (linear)
        float pan = 0.0f;              // -1 to 1, a balance
        bool mute = false, solo = false, invert = false;
        int output = 0;                // 0 MAIN
        int route = 0;                 // routeNames' index (0: its OUTPUT; else a bus)
    };

    // THE CHANNELS: one per section (stage 6), then the buses. A bus feeds only later buses, so nothing loops back.
    inline constexpr int numChannels = 16;
    inline constexpr int numBuses = 8;
    inline const juce::StringArray busNames { "BUS 1", "BUS 2", "BUS 3", "BUS 4", "BUS 5", "BUS 6", "BUS 7", "BUS 8" };
    // A ROUTE, by index: 0 its OUTPUT, 1-8 BUS 1-8, 9-12 FX BUS A-D (the FX page's insert buses).
    inline const juce::StringArray routeNames { "Output", "Bus 1", "Bus 2", "Bus 3", "Bus 4", "Bus 5", "Bus 6", "Bus 7", "Bus 8",
                                                "FX Bus A", "FX Bus B", "FX Bus C", "FX Bus D" };
    inline constexpr int firstFxRoute = 9, numFxBuses = 4;
    // A send's TARGET, by index: 0 off, 1-8 BUS 1-8, 9-12 FX BUS A-D, 13 DELAY, 14 REVERB.
    inline const juce::StringArray sendTargetNames { "Off", "Bus 1", "Bus 2", "Bus 3", "Bus 4", "Bus 5", "Bus 6", "Bus 7", "Bus 8",
                                                     "FX Bus A", "FX Bus B", "FX Bus C", "FX Bus D", "Delay", "Reverb" };
    inline constexpr int delayTarget = 13, reverbTarget = 14;
    inline int busOfTarget (int target) noexcept { return target >= 1 && target <= numBuses ? target - 1 : -1; }
    inline int fxBusOfTarget (int target) noexcept { return target >= firstFxRoute && target < firstFxRoute + numFxBuses ? target - firstFxRoute : -1; }

    struct BusSettings
    {
        float gain = 1.0f, pan = 0.0f;
        bool mute = false, solo = false, invert = false;
        int output = 0, route = 0;
    };

    // A SEND: a copy of a strip into a target, its own level and pan; POST FADER, PRE FADER (after the effects) or PRE
    // EFFECTS.
    enum class SendMode { postFader, preFader, preEffects };
    inline const juce::StringArray sendModeNames { "Post Fader", "Pre Fader", "Pre Effects" };
    struct Send
    {
        int target = 0;
        float levelDb = 0.0f;   // -60 (off) to +12
        float pan = 0.0f;
        SendMode mode = SendMode::postFader;

        bool isOn() const noexcept { return target > 0; }
        float gain() const noexcept { return levelDb > -59.9f ? juce::Decibels::decibelsToGain (levelDb) : 0.0f; }
        bool operator== (const Send& o) const noexcept
        {
            return target == o.target && std::abs (levelDb - o.levelDb) < 1.0e-4f && std::abs (pan - o.pan) < 1.0e-4f && mode == o.mode;
        }
    };
    inline constexpr int numSendSlots = 2;

    inline constexpr int numStrips = numChannels + numBuses;   // the channels, then the buses
    inline constexpr bool isBusStrip (int strip) noexcept { return strip >= numChannels && strip < numStrips; }
    using SendTable = std::array<std::array<Send, numSendSlots>, numStrips>;

    // THE EFFECTS: four slots on each strip, each one effect (MixerEffects.h: EQ, compressor, gate, transient shaper,
    // drive, lo-fi, mod, saturation), in order, before the strip's pan and fader. A slot's settings are its type's
    // values by index.
    inline constexpr int numFxSlots = 4, maxFxValues = 20;
    struct FxSlot
    {
        int type = 0;              // mixerfx::typeNames' index (0: empty)
        bool on = true;            // off: bypassed (it fades out, its settings kept)
        float outputDb = 0.0f;     // OUTPUT, -24 to +12
        std::array<float, maxFxValues> values {};

        bool isLoaded() const noexcept { return type > 0; }
        bool operator== (const FxSlot& o) const noexcept
        {
            if (type != o.type || on != o.on || std::abs (outputDb - o.outputDb) > 1.0e-4f)
                return false;
            for (size_t i = 0; i < values.size(); ++i)
                if (std::abs (values[i] - o.values[i]) > 1.0e-5f)
                    return false;
            return true;
        }
    };
    using FxTable = std::array<std::array<FxSlot, numFxSlots>, numStrips>;

    // What runs a strip's effects (on the audio thread): its loaded slots in order, in place.
    struct EffectHost
    {
        virtual ~EffectHost() = default;
        virtual bool hasEffects (int strip) const noexcept = 0;
        virtual void process (int strip, float* left, float* right, int numSamples) noexcept = 0;
    };

    // Whether strip `from` may go into BUS `bus`: channels into any, a bus only into a later one.
    inline bool canFeedBus (int from, int bus) noexcept
    {
        return bus >= 0 && bus < numBuses && (! isBusStrip (from) || bus > from - numChannels);
    }
}
