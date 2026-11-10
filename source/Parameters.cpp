#include "Parameters.h"
#include "NoteNames.h"

#include "dsp/sampler/Sampler.h"

namespace
{
    using Range = juce::NormalisableRange<float>;
    using ToText = std::function<juce::String (float, int)>;

    // Version hints. PR7-O's effects keep the hints PR7-D gave them (16-19, in RackParameters.cpp); PR7-O's own
    // parameters start at 42, after PR7-D's last (41).
    constexpr int firstVersion = 1;
    constexpr int pr7o010 = 42;   // PR7-O 0.1.0: the instrument, MIDI CHANNEL, NOTE NAMES
    constexpr int pr7o020 = 43;   // PR7-O 0.2.0: DYNAMICS, AUTO SUSTAIN
    constexpr int pr7o070 = 44;   // PR7-O 0.7.0: LEGATO, GLIDE
    constexpr int pr7o080 = 45;   // PR7-O 0.8.0: THE STAGE

    const ToText asDb = [] (float value, int) { return juce::String (value, 1) + " dB"; };
    const ToText asPercent = [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; };
    const ToText asPan = [] (float pan, int)
    {
        const auto amount = juce::roundToInt (std::abs (pan) * 100.0f);
        return amount == 0 ? juce::String ("C") : (pan < 0.0f ? "L" : "R") + juce::String (amount);
    };

    // A range whose knob travel is logarithmic: each octave takes the same distance.
    Range logRange (double min, double max)
    {
        return { (float) min, (float) max,
                 [] (float start, float end, float normalised) { return start * std::pow (end / start, normalised); },
                 [] (float start, float end, float value) { return std::log (value / start) / std::log (end / start); },
                 [] (float start, float end, float value) { return juce::jlimit (start, end, value); } };
    }

    void add (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& id, const juce::String& name,
              Range range, float defaultValue, const ToText& toText, int version = firstVersion)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, version }, name, range, defaultValue,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText)));
    }

    void addSwitch (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& id, const juce::String& name, bool defaultValue,
                    int version = firstVersion)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, version }, name, defaultValue));
    }

    void addChoice (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& id, const juce::String& name,
                    const juce::StringArray& choices, int defaultIndex, int version = firstVersion)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, version }, name, choices, defaultIndex));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using namespace ParamIDs;

    add (layout, outputGain, "Output", { ParamRanges::gainMinDb, ParamRanges::gainMaxDb, 0.1f }, ParamRanges::outputDefaultDb, asDb);
    addSwitch (layout, bypass, "Bypass", false);
    add (layout, patchLevel, "Preset Level", { ParamRanges::gainMinDb, ParamRanges::gainMaxDb, 0.1f }, 0.0f, asDb);

    // THE INSTRUMENT (0.1.0).
    {
        const ToText asLevel = [] (float db, int) { return db <= -59.95f ? juce::String ("-inf") : juce::String (db, 1) + " dB"; };
        const ToText asSemitones = [] (float st, int)
        {
            const auto n = juce::roundToInt (st);
            return (n > 0 ? "+" : "") + juce::String (n) + " st";
        };
        const ToText asCents = [] (float cents, int)
        {
            const auto n = juce::roundToInt (cents);
            return (n > 0 ? "+" : "") + juce::String (n) + " ct";
        };
        add (layout, instLevel, "Level", { -60.0f, 12.0f, 0.1f }, 0.0f, asLevel, pr7o010);
        add (layout, instPan, "Pan", { -1.0f, 1.0f, 0.01f }, 0.0f, asPan, pr7o010);
        add (layout, instTranspose, "Transpose", { -24.0f, 24.0f, 1.0f }, 0.0f, asSemitones, pr7o010);
        add (layout, instTune, "Tune", { -100.0f, 100.0f, 1.0f }, 0.0f, asCents, pr7o010);
        add (layout, instRelease, "Release", logRange (0.1, 10.0), 1.0f,
             [] (float v, int) { return "x" + juce::String (v, v < 1.0f ? 2 : 1); }, pr7o010);
        add (layout, instVoices, "Polyphony", { 8.0f, (float) sampler::Sampler::maxPolyphony, 1.0f }, 128.0f,
             [] (float v, int) { return juce::String (juce::roundToInt (v)) + " notes"; }, pr7o010);   // (0.2.0: notes, not voices)
        add (layout, instSendDelay, "Send Delay", { 0.0f, 1.0f, 0.01f }, 0.0f, asPercent, pr7o010);
        add (layout, instSendReverb, "Send Reverb", { 0.0f, 1.0f, 0.01f }, 0.0f, asPercent, pr7o010);
        addChoice (layout, instInsert, "Insert Bus", insertBusNames, 0, pr7o010);
        addChoice (layout, instDynamics, "Dynamics", dynamicsNames, 0, pr7o020);
        addSwitch (layout, instAutoSustain, "Auto Sustain", true, pr7o020);
        addSwitch (layout, instLegato, "Legato", false, pr7o070);
        add (layout, instGlide, "Glide", { 0.0f, 0.5f, 0.001f, 0.5f }, 0.0f,
             [] (float v, int) { return v < 0.0005f ? juce::String ("Off") : juce::String (juce::roundToInt (v * 1000.0f)) + " ms"; }, pr7o070);
        addSwitch (layout, stageOn, "Stage", false, pr7o080);
        addChoice (layout, stageRoom, "Room", stageRoomNames, 2, pr7o080);
        add (layout, stageMic, "Mic Distance", logRange (1.0, 25.0), 2.5f,   // (just behind the conductor: direct and room about equal)
             [] (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + " m"; }, pr7o080);
        add (layout, stageRoomLevel, "Room Level", { -24.0f, 6.0f, 0.1f }, -3.0f, asDb, pr7o080);   // (open samples carry some room)
        add (layout, stageDecay, "Decay", logRange (0.5, 2.0), 1.0f,
             [] (float v, int) { return "x" + juce::String (v, 2); }, pr7o080);
        add (layout, stageSpread, "Spread", { 0.0f, 1.5f, 0.01f }, 1.0f, asPercent, pr7o080);
        addChoice (layout, midiChannel, "MIDI Channel", midiChannelNames, 0, pr7o010);
        addChoice (layout, noteNames, "Note Names", notenames::choiceNames, 0, pr7o010);
    }

    addRackParameters (layout);   // the rack (PR7-K's 7a): RackParameters.cpp
    addSendParameters (layout);   // the sends (7b): RackParameters.cpp
    addModFxParameters (layout);  // MOD, MOD 2, LO-FI (7c): RackParameters.cpp
    addInsertParameters (layout); // the insert buses (7d): RackParameters.cpp
    return layout;
}
