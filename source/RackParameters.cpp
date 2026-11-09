// THE RACK's parameters (stage 7a, 0.17.0): PR7-E 0.18.1's ECHELON, CONSTRICTOR (with its bands), DOMINION and
// LIMITER, copied from PR7-E's Parameters.cpp with the same IDs, ranges, defaults and choice lists (so a preset saved
// in one plugin loads in the other). Only the version hints differ: all PR7-K's 16 (0.17.0). In their own file so
// PR7-E's helpers (logRange, ratioRange...) don't clash with the synth's.
#include "Parameters.h"

#include "dsp/dominion/Dominion.h"
#include "dsp/effects/Delay.h"
#include "dsp/effects/Drive.h"
#include "dsp/effects/Glitch.h"
#include "dsp/effects/LoFi.h"
#include "dsp/effects/Modulation.h"
#include "dsp/effects/MasterLimiter.h"
#include "dsp/effects/Reverb.h"
#include "dsp/effects/MultibandComp.h"
#include "dsp/eq/Dynamics.h"
#include "dsp/eq/EqDesign.h"

namespace
{
    using Range = juce::NormalisableRange<float>;
    using ToText = std::function<juce::String (float, int)>;

    const ToText asDb = [] (float value, int) { return juce::String (value, 1) + " dB"; };
    const ToText asPercent = [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; };
    const ToText asFrequency = [] (float hz, int)
    {
        return hz < 1000.0f ? juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz" : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
    };
    const ToText asQ = [] (float q, int) { return juce::String (q, q < 10.0f ? 2 : 1); };

    // A range whose knob travel is logarithmic (frequency, Q): each octave
    // takes the same distance.
    Range logRange (double min, double max)
    {
        return { (float) min, (float) max,
                 [] (float start, float end, float normalised) { return start * std::pow (end / start, normalised); },
                 [] (float start, float end, float value) { return std::log (value / start) / std::log (end / start); },
                 [] (float start, float end, float value) { return juce::jlimit (start, end, value); } };
    }

    // From PR7-C (Parameters.cpp), for COMP's RATIO: a knob whose printed
    // marks sit evenly round it: between two marks the travel is even in log
    // (or straight, from a mark of 0). `snap` tidies a value (the SOFT stretch).
    Range markedRange (std::vector<float> marks, std::function<float (float)> snap = {})
    {
        const auto steps = (float) (marks.size() - 1);
        auto between = [] (float low, float high, float t) { return low > 0.0f ? low * std::pow (high / low, t) : low + (high - low) * t; };
        auto within = [] (float low, float high, float value) { return low > 0.0f ? std::log (value / low) / std::log (high / low) : (value - low) / (high - low); };
        return { marks.front(), marks.back(),
                 [marks, steps, between] (float, float, float proportion)
                 {
                     const auto position = juce::jlimit (0.0f, 1.0f, proportion) * steps;
                     const auto below = juce::jmin ((int) position, (int) steps - 1);
                     return between (marks[(size_t) below], marks[(size_t) below + 1], position - (float) below);
                 },
                 [marks, steps, within] (float, float, float value)
                 {
                     for (size_t i = 1; i < marks.size(); ++i)
                         if (value <= marks[i])
                             return ((float) (i - 1) + within (marks[i - 1], marks[i], juce::jmax (value, marks[i - 1]))) / steps;
                     return 1.0f;
                 },
                 [marks, snap] (float, float, float value)
                 {
                     value = juce::jlimit (marks.front(), marks.back(), value);
                     return snap ? snap (value) : value;
                 } };
    }

    // RATIO: SOFT, then 1.5:1 to infinite. Below 1.25 is the SOFT position.
    Range ratioRange()
    {
        using ParamRanges::compRatioMarks;
        return markedRange ({ std::begin (compRatioMarks), std::end (compRatioMarks) },
                            [] (float ratio) { return ratio < 1.25f ? 1.0f : ratio; });
    }

    const ToText asRatio = [] (float ratio, int)
    {
        if (ratio < 1.25f)
            return juce::String ("Soft");
        if (ratio >= 29.5f)
            return juce::String ("Inf:1");
        return juce::String (ratio, ratio < 10.0f ? 1 : 0) + ":1";
    };

    const ToText asMs = [] (float ms, int)
    {
        return ms < 1000.0f ? juce::String (ms, ms < 10.0f ? 1 : 0) + " ms" : juce::String (ms / 1000.0f, 2) + " s";
    };

    std::unique_ptr<juce::AudioParameterFloat> makeFloat (const juce::String& id, const juce::String& name, Range range,
                                                          float defaultValue, int versionHint, const ToText& toText)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, versionHint }, name, range, defaultValue,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText));
    }

    std::unique_ptr<juce::AudioParameterBool> makeSwitch (const juce::String& id, const juce::String& name, int versionHint,
                                                          bool defaultValue = true)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, versionHint }, name, defaultValue);
    }

    std::unique_ptr<juce::AudioParameterChoice> makeChoice (const juce::String& id, const juce::String& name, int versionHint,
                                                            const juce::StringArray& choices, int defaultIndex)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, versionHint }, name, choices, defaultIndex);
    }

    constexpr int rackVersion = 16;   // 0.17.0
}

void addRackParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    // Stomp 4, the EQ (PR7-E 0.4.0): 24 band slots, each unused until a
    // band is created in it, then OUTPUT, GAIN SCALE and AUTO GAIN. eqOn
    // (version 8) was the graphic EQ's switch; it's the stomp's now.
    constexpr int v8 = rackVersion, v30 = rackVersion;
    layout.add (makeSwitch (ParamIDs::eqOn, "Echelon On", v8, false));
    for (int band = 0; band < ParamIDs::eqNumBands; ++band)
    {
        const auto id = [band] (const char* what) { return ParamIDs::eqBand (band, what); };
        const auto name = [band] (const juce::String& what) { return "Echelon Band " + juce::String (band + 1) + " " + what; };
        const eq::Band defaults;
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("Used"), v30 }, name ("Used"), false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("On"), v30 }, name ("On"), true));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id ("Shape"), v30 }, name ("Shape"), eq::shapeNames, 0));
        layout.add (makeFloat (id ("Freq").toRawUTF8(), name ("Frequency"), logRange (eq::minFrequency, eq::maxFrequency),
                               (float) defaults.frequency, v30, asFrequency));
        layout.add (makeFloat (id ("Gain").toRawUTF8(), name ("Gain"), { (float) -eq::maxGainDb, (float) eq::maxGainDb, 0.01f },
                               0.0f, v30, asDb));
        layout.add (makeFloat (id ("Q").toRawUTF8(), name ("Q"), logRange (eq::minQ, eq::maxQ), (float) defaults.q, v30, asQ));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id ("Slope"), v30 }, name ("Slope"), eq::slopeNames, eq::defaultSlope));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id ("Place"), v30 }, name ("Placement"), eq::placementNames, 0));
    }
    // Dynamic EQ (0.5.0), each band's, after all the bands' settings above
    // so those keep their places in the host's list.
    constexpr int v31 = rackVersion;
    for (int band = 0; band < ParamIDs::eqNumBands; ++band)
    {
        const auto id = [band] (const char* what) { return ParamIDs::eqBand (band, what); };
        const auto name = [band] (const juce::String& what) { return "Echelon Band " + juce::String (band + 1) + " " + what; };
        const eq::Band defaults;
        layout.add (makeFloat (id ("Dyn").toRawUTF8(), name ("Dynamic Range"),
                               { (float) -eq::dynamics::maxRangeDb, (float) eq::dynamics::maxRangeDb, 0.01f }, 0.0f, v31, asDb));
        layout.add (makeSwitch (id ("DynAuto").toRawUTF8(), name ("Auto Threshold"), v31, defaults.autoThreshold));
        layout.add (makeFloat (id ("Thresh").toRawUTF8(), name ("Threshold"),
                               { (float) eq::dynamics::minThresholdDb, (float) eq::dynamics::maxThresholdDb, 0.1f },
                               (float) defaults.thresholdDb, v31, asDb));
        layout.add (makeFloat (id ("Attack").toRawUTF8(), name ("Attack"), { 0.0f, 1.0f, 0.01f }, (float) defaults.attack, v31, asPercent));
        layout.add (makeFloat (id ("Release").toRawUTF8(), name ("Release"), { 0.0f, 1.0f, 0.01f }, (float) defaults.release, v31, asPercent));
    }
    // Stomp 2, COMP (0.9.0): ranges and defaults as on PR7-C.
    {
        using namespace ParamRanges;
        constexpr int v33 = rackVersion;
        layout.add (makeSwitch (ParamIDs::compOn, "Constrictor", v33, false));
        layout.add (makeFloat (ParamIDs::compThreshold, "Constrictor Threshold", { compThresholdMin, compThresholdMax, 0.1f }, -18.0f, v33, asDb));
        layout.add (makeFloat (ParamIDs::compRatio, "Constrictor Ratio", ratioRange(), 4.0f, v33, asRatio));
        layout.add (makeFloat (ParamIDs::compAttack, "Constrictor Attack", logRange (compAttackMin, compAttackMax), 10.0f, v33, asMs));
        layout.add (makeFloat (ParamIDs::compRelease, "Constrictor Release", logRange (compReleaseMin, compReleaseMax), 300.0f, v33, asMs));
        layout.add (makeSwitch (ParamIDs::compAuto, "Constrictor Auto Release", v33, false));
        layout.add (makeFloat (ParamIDs::compMakeUp, "Constrictor Make Up", { 0.0f, compMakeUpMax, 0.1f }, 0.0f, v33, asDb));
        layout.add (makeChoice (ParamIDs::compCharacter, "Constrictor Character", v33, compCharacters, 0));
        layout.add (makeSwitch (ParamIDs::compLink, "Constrictor Link", v33, true));
    }
    // COMP's bands (0.13.0).
    {
        namespace mb = effects::multiband;
        constexpr int v35 = rackVersion;
        const ToText percent = [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; };
        const ToText ratio = [] (float r, int) { return r >= 29.5f ? juce::String ("Inf:1") : juce::String (r, r < 10.0f ? 1 : 0) + ":1"; };
        const ToText edge = [] (float hz, int)
        {
            if (hz <= mb::minHz * 1.001f || hz >= mb::maxHz * 0.999f)
                return juce::String ("End");
            return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
        };
        juce::StringArray slopeNames;
        for (int i = 0; i < mb::numSlopes; ++i)
            slopeNames.add (eq::slopeNames[i]);
        layout.add (makeSwitch (ParamIDs::compWide, "Constrictor Full Band", v35, true));
        layout.add (makeSwitch (ParamIDs::mbLookahead, "Constrictor Look-ahead", v35, false));
        layout.add (makeFloat (ParamIDs::mbMix, "Constrictor Bands Mix", { 0.0f, 200.0f, 1.0f }, 100.0f, v35, percent));
        for (int band = 0; band < ParamIDs::mbNumBands; ++band)
        {
            const auto id = [band] (const char* what) { return ParamIDs::mbBand (band, what); };
            const auto name = [band] (const juce::String& what) { return "Constrictor Band " + juce::String (band + 1) + " " + what; };
            const mb::Band defaults;
            layout.add (makeSwitch (id ("Used").toRawUTF8(), name ("Used"), v35, false));
            layout.add (makeSwitch (id ("On").toRawUTF8(), name ("On"), v35, true));
            layout.add (makeFloat (id ("Low").toRawUTF8(), name ("Low"), logRange (mb::minHz, mb::maxHz), defaults.lowHz, v35, edge));
            layout.add (makeFloat (id ("High").toRawUTF8(), name ("High"), logRange (mb::minHz, mb::maxHz), defaults.highHz, v35, edge));
            layout.add (makeChoice (id ("Slope").toRawUTF8(), name ("Slope"), v35, slopeNames, mb::defaultSlope));
            layout.add (makeFloat (id ("Thresh").toRawUTF8(), name ("Threshold"), { -60.0f, 0.0f, 0.1f }, defaults.thresholdDb, v35, asDb));
            layout.add (makeFloat (id ("Range").toRawUTF8(), name ("Range"), { -mb::maxRangeDb, mb::maxRangeDb, 0.1f }, defaults.rangeDb, v35, asDb));
            layout.add (makeChoice (id ("Mode").toRawUTF8(), name ("Mode"), v35, mb::modeNames, 0));
            layout.add (makeFloat (id ("Ratio").toRawUTF8(), name ("Ratio"), logRange (1.0, 30.0), defaults.ratio, v35, ratio));
            layout.add (makeFloat (id ("Knee").toRawUTF8(), name ("Knee"), { 0.0f, 30.0f, 0.1f }, defaults.kneeDb, v35, asDb));
            layout.add (makeFloat (id ("Attack").toRawUTF8(), name ("Attack"), logRange (0.1, 100.0), defaults.attackMs, v35, asMs));
            layout.add (makeFloat (id ("Release").toRawUTF8(), name ("Release"), logRange (5.0, 3000.0), defaults.releaseMs, v35, asMs));
            layout.add (makeChoice (id ("Char").toRawUTF8(), name ("Character"), v35, ParamRanges::compCharacters, 0));
            layout.add (makeChoice (id ("Stereo").toRawUTF8(), name ("Stereo"), v35, mb::stereoNames, 0));
            layout.add (makeFloat (id ("Link").toRawUTF8(), name ("Link"), { 0.0f, 100.0f, 1.0f }, 100.0f, v35, percent));
            layout.add (makeFloat (id ("Out").toRawUTF8(), name ("Output"), { -24.0f, 24.0f, 0.1f }, 0.0f, v35, asDb));
            layout.add (makeFloat (id ("Look").toRawUTF8(), name ("Look-ahead"), { 0.0f, 20.0f, 0.1f }, 0.0f, v35, asMs));
        }
    }

    // Stomp 5, DOMINION (0.10.0).
    {
        constexpr int v34 = rackVersion;
        const ToText plain = [] (float v, int) { return juce::String (v, 1); };
        const ToText percent = [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; };
        const ToText tilt = [] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (juce::roundToInt (v)); };
        const ToText maxCut = [] (float v, int) { return v >= (float) dominion::maxCutLimitDb - 0.05f ? juce::String ("Off") : juce::String (v, 1) + " dB"; };
        layout.add (makeSwitch (ParamIDs::domOn, "Dominion", v34, false));
        layout.add (makeChoice (ParamIDs::domMode, "Dominion Mode", v34, dominion::modeNames, 0));
        layout.add (makeFloat (ParamIDs::domDepth, "Dominion Depth", { 0.0f, 20.0f, 0.01f }, 5.0f, v34, plain));
        layout.add (makeFloat (ParamIDs::domDetail, "Dominion Detail", { 0.0f, 100.0f, 0.1f }, 50.0f, v34, percent));
        layout.add (makeFloat (ParamIDs::domAttack, "Dominion Attack", { 0.0f, 100.0f, 0.1f }, 50.0f, v34, percent));
        layout.add (makeFloat (ParamIDs::domRelease, "Dominion Release", { 0.0f, 100.0f, 0.1f }, 50.0f, v34, percent));
        layout.add (makeChoice (ParamIDs::domStereo, "Dominion Stereo", v34, dominion::stereoNames, 1));
        layout.add (makeFloat (ParamIDs::domLink, "Dominion Link", { 0.0f, 100.0f, 1.0f }, 100.0f, v34, percent));
        layout.add (makeFloat (ParamIDs::domFocus, "Dominion Focus", { -100.0f, 100.0f, 1.0f }, 0.0f, v34, tilt));
        for (const auto* id : { ParamIDs::domDetailLow, ParamIDs::domDetailHigh, ParamIDs::domAttackLow, ParamIDs::domAttackHigh,
                                ParamIDs::domReleaseLow, ParamIDs::domReleaseHigh })
        {
            const auto name = juce::String ("Dominion ") + juce::String (id).fromFirstOccurrenceOf ("dom", false, false);
            layout.add (makeFloat (id, name, { -100.0f, 100.0f, 1.0f }, 0.0f, v34, tilt));
        }
        layout.add (makeFloat (ParamIDs::domMaxCut, "Dominion Max Cut", { 0.0f, (float) dominion::maxCutLimitDb, 0.1f },
                               (float) dominion::maxCutLimitDb, v34, maxCut));
        layout.add (makeFloat (ParamIDs::domWetTrim, "Dominion Wet Trim", { -12.0f, 12.0f, 0.1f }, 0.0f, v34, asDb));
        layout.add (makeFloat (ParamIDs::domMix, "Dominion Mix", { 0.0f, 100.0f, 1.0f }, 100.0f, v34, percent));
        layout.add (makeFloat (ParamIDs::domOut, "Dominion Out", { -12.0f, 12.0f, 0.1f }, 0.0f, v34, asDb));
        layout.add (makeSwitch (ParamIDs::domDelta, "Dominion Delta", v34, false));
        for (int band = 0; band < ParamIDs::domNumBands; ++band)
        {
            const auto id = [band] (const char* what) { return ParamIDs::domBand (band, what); };
            const auto name = [band] (const juce::String& what) { return "Dominion Band " + juce::String (band + 1) + " " + what; };
            const dominion::Band defaults;
            layout.add (makeSwitch (id ("Used").toRawUTF8(), name ("Used"), v34, false));
            layout.add (makeSwitch (id ("On").toRawUTF8(), name ("On"), v34, true));
            layout.add (makeChoice (id ("Shape").toRawUTF8(), name ("Shape"), v34, dominion::shapeNames, (int) dominion::Shape::bell));
            layout.add (makeFloat (id ("Freq").toRawUTF8(), name ("Frequency"), logRange (20.0, 20000.0), (float) defaults.frequency, v34, asFrequency));
            layout.add (makeFloat (id ("Depth").toRawUTF8(), name ("Depth"), { (float) -dominion::maxDepthDb, (float) dominion::maxDepthDb, 0.01f },
                                   0.0f, v34, asDb));
            layout.add (makeFloat (id ("Q").toRawUTF8(), name ("Q"), logRange (0.1, 10.0), (float) defaults.q, v34, asQ));
            layout.add (makeChoice (id ("Slope").toRawUTF8(), name ("Slope"), v34, eq::slopeNames, eq::defaultSlope));
            layout.add (makeFloat (id ("Focus").toRawUTF8(), name ("Focus"), { -100.0f, 100.0f, 1.0f }, 0.0f, v34, tilt));
        }
    }

    // Stomp 7, the LIMITER (0.8.0): ranges and defaults as on PR7-C.
    constexpr int v32 = rackVersion;
    layout.add (makeSwitch (ParamIDs::limitOn, "Limiter", v32, false));
    layout.add (makeFloat (ParamIDs::limitThreshold, "Limit Threshold", { ParamRanges::limitThresholdMin, 0.0f, 0.1f }, -1.0f, v32, asDb));
    layout.add (makeFloat (ParamIDs::limitMakeUp, "Limit Make Up", { 0.0f, ParamRanges::limitMakeUpMax, 0.1f }, 0.0f, v32, asDb));
    layout.add (makeChoice (ParamIDs::limitMode, "Limit Style", v32, ParamRanges::limitModes, ParamRanges::limitDefaultMode));
    layout.add (makeFloat (ParamIDs::limitMix, "Limit Mix", { 0.0f, 100.0f, 1.0f }, 100.0f, v32,
                           [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }));
    layout.add (makeSwitch (ParamIDs::limitLink, "Limit Link", v32, true));
    layout.add (makeFloat (ParamIDs::limitTrim, "Output Trim", { ParamRanges::limitTrimMin, ParamRanges::limitTrimMax, 0.1f }, 0.0f, v32, asDb));

    // ...and from 0.16.0, after Pro-L 2 (see effects::MasterLimiter).
    {
        constexpr int v36 = rackVersion;
        const ToText percent = [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; };
        const ToText ms = [] (float v, int) { return v < 10.0f ? juce::String (v, 2) + " ms" : v < 1000.0f ? juce::String (juce::roundToInt (v)) + " ms"
                                                                                                : juce::String (v / 1000.0f, 2) + " s"; };
        layout.add (makeFloat (ParamIDs::limitGain, "Limit Gain", { 0.0f, ParamRanges::limitGainMax, 0.01f }, 0.0f, v36, asDb));
        layout.add (makeFloat (ParamIDs::limitCeiling, "Limit Output", { ParamRanges::limitCeilingMin, 0.0f, 0.01f }, -1.0f, v36, asDb));
        layout.add (makeFloat (ParamIDs::limitLookahead, "Limit Lookahead", { 0.0f, effects::limiter::maxLookaheadMs, 0.01f, 0.5f }, 1.0f, v36, ms));
        layout.add (makeFloat (ParamIDs::limitAttack, "Limit Attack", logRange (0.5, 500.0), 20.0f, v36, ms));
        layout.add (makeFloat (ParamIDs::limitRelease, "Limit Release", logRange (10.0, 5000.0), 250.0f, v36, ms));
        layout.add (makeFloat (ParamIDs::limitLinkTransient, "Limit Link Transients", { 0.0f, 100.0f, 1.0f }, 75.0f, v36, percent));
        layout.add (makeFloat (ParamIDs::limitLinkRelease, "Limit Link Release", { 0.0f, 100.0f, 1.0f }, 100.0f, v36, percent));
        layout.add (makeSwitch (ParamIDs::limitTruePeak, "Limit True Peak", v36, false));
        layout.add (makeChoice (ParamIDs::limitOversampling, "Limit Oversampling", v36, effects::limiter::oversamplingNames, 0));
        layout.add (makeChoice (ParamIDs::limitDither, "Limit Dither", v36, effects::limiter::ditherNames, 0));
        layout.add (makeChoice (ParamIDs::limitShaping, "Limit Noise Shaping", v36, effects::limiter::shapingNames, 0));
        layout.add (makeSwitch (ParamIDs::limitUnity, "Limit Unity Gain", v36, false));
        layout.add (makeSwitch (ParamIDs::limitDelta, "Limit Delta", v36, false));
        layout.add (makeSwitch (ParamIDs::limitDcFilter, "Limit DC Filter", v36, false));
    }

    layout.add (makeFloat (ParamIDs::eqOutput, "Echelon Output", { -36.0f, 36.0f, 0.01f }, 0.0f, v30, asDb));
    layout.add (makeFloat (ParamIDs::eqGainScale, "Echelon Gain Scale", { 0.0f, 2.0f, 0.01f }, 1.0f, v30, asPercent));
    layout.add (makeSwitch (ParamIDs::eqAutoGain, "Echelon Auto Gain", v30, false));
}

// THE SENDS (stage 7b, 0.18.0): PR7-G 0.52.0's DELAY and REVERB, with PR7-G's IDs, ranges, defaults and choice lists
// (its Parameters.cpp); version hint 17. DELAY > REVERB is PR7-K's own.
void addSendParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    constexpr int v = 17;   // 0.18.0
    const auto skewed = [] (float min, float max, float centre, float step)
    {
        Range range { min, max, step };
        range.setSkewForCentre (centre);
        return range;
    };
    const ToText asKnob = [] (float value, int) { return juce::String (value, 1); };
    const ToText asWholeMs = [] (float value, int) { return juce::String (juce::roundToInt (value)) + " ms"; };
    layout.add (makeSwitch (ParamIDs::delayOn, "Delay On", v, false));
    layout.add (makeFloat (ParamIDs::delayTime, "Delay Time", skewed (1.0f, effects::Delay::maxDelaySeconds * 1000.0f, 400.0f, 1.0f),
                           375.0f, v, asWholeMs));
    layout.add (makeChoice (ParamIDs::delaySync, "Delay Sync", v, effects::Delay::syncNames, 3));   // 1/4
    layout.add (makeFloat (ParamIDs::delayFeedback, "Delay Feedback", { 0.0f, 0.95f, 0.01f }, 0.35f, v, asPercent));
    layout.add (makeFloat (ParamIDs::delayTone, "Delay Tone", { 0.0f, 10.0f, 0.1f }, 7.0f, v, asKnob));
    layout.add (makeFloat (ParamIDs::delayMix, "Delay Mix", { 0.0f, 1.0f, 0.01f }, 0.25f, v, asPercent));
    layout.add (makeSwitch (ParamIDs::delayPingPong, "Delay Ping-Pong", v, false));
    layout.add (makeChoice (ParamIDs::delayType, "Delay Type", v, effects::Delay::typeNames, (int) effects::Delay::Type::digital));
    layout.add (makeFloat (ParamIDs::delayDuck, "Delay Duck", { 0.0f, 1.0f, 0.01f }, 0.0f, v, asPercent));
    layout.add (makeSwitch (ParamIDs::reverbOn, "Reverb On", v, false));
    layout.add (makeFloat (ParamIDs::reverbSize, "Reverb Size", { 0.0f, 1.0f, 0.01f }, 0.5f, v, asPercent));
    layout.add (makeFloat (ParamIDs::reverbTone, "Reverb Tone", { 0.0f, 10.0f, 0.1f }, 6.0f, v, asKnob));
    layout.add (makeFloat (ParamIDs::reverbMix, "Reverb Mix", { 0.0f, 1.0f, 0.01f }, 0.28f, v, asPercent));
    layout.add (makeChoice (ParamIDs::reverbType, "Reverb Type", v, effects::Reverb::typeNames, (int) effects::Reverb::Type::digital));
    layout.add (makeFloat (ParamIDs::reverbPreDelay, "Reverb Pre-Delay", { 0.0f, effects::Reverb::maxPreDelayMs, 1.0f }, 20.0f, v, asWholeMs));
    layout.add (makeFloat (ParamIDs::reverbDuck, "Reverb Duck", { 0.0f, 1.0f, 0.01f }, 0.0f, v, asPercent));
    layout.add (makeFloat (ParamIDs::delayToReverb, "Delay To Reverb", { 0.0f, 1.0f, 0.01f }, 0.0f, v, asPercent));
}

void addModFxParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    // PR7-G 0.52.0's, as there (its MOD 2 defaults to a flanger, to pair with MOD's chorus); one version hint here.
    constexpr int v = 18;   // 0.19.0
    const auto skewed = [] (float min, float max, float centre, float step)
    {
        Range range { min, max, step };
        range.setSkewForCentre (centre);
        return range;
    };
    const ToText asKnob = [] (float value, int) { return juce::String (value, 1); };
    const ToText asHz = [] (float value, int) { return juce::String (value, value < 1.0f ? 2 : 1) + " Hz"; };
    const ToText asVortexEq = [] (float value, int)
    {
        const auto db = value * effects::Modulation::vortexEqRangeDb;
        return std::abs (db) < 0.05f ? juce::String ("Flat") : (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    };
    const auto addMod = [&] (const ParamIDs::ModIds& ids, const juce::String& name, int type, float rate)
    {
        layout.add (makeSwitch (ids.on, name + " On", v, false));
        layout.add (makeChoice (ids.type, name + " Type", v, effects::Modulation::typeNames, type));
        layout.add (makeFloat (ids.rate, name + " Rate", skewed (0.05f, 10.0f, 1.0f, 0.01f), rate, v, asHz));
        layout.add (makeFloat (ids.depth, name + " Depth", { 0.0f, 1.0f, 0.01f }, 0.45f, v, asPercent));
        layout.add (makeFloat (ids.mix, name + " Mix", { 0.0f, 1.0f, 0.01f }, 0.5f, v, asPercent));
        layout.add (makeChoice (ids.sync, name + " Sync", v, effects::Delay::syncNames, 0));
        layout.add (makeFloat (ids.eq, name + " EQ", { -1.0f, 1.0f, 0.01f }, 0.0f, v, asVortexEq));
        layout.add (makeSwitch (ids.stereo, name + " Stereo", v, false));
        layout.add (makeFloat (ids.manual, name + " AEON Manual", { 0.0f, 10.0f, 0.1f }, 5.0f, v, asKnob));
        layout.add (makeFloat (ids.res, name + " AEON Res", { 0.0f, 10.0f, 0.1f }, 5.0f, v, asKnob));
        layout.add (makeChoice (ids.hadron, name + " Hadron Circuit", v, effects::Modulation::hadronCircuitNames, 0));
        layout.add (makeFloat (ids.wave, name + " Razor Wave", { 0.0f, 10.0f, 0.1f }, 0.0f, v, asKnob));
    };
    addMod (ParamIDs::modIds, "Mod", (int) effects::Modulation::Type::chorus, 0.8f);
    addMod (ParamIDs::mod2Ids, "Mod 2", (int) effects::Modulation::Type::flanger, 0.3f);

    layout.add (makeSwitch (ParamIDs::lofiOn, "Lo-Fi On", v, false));
    layout.add (makeChoice (ParamIDs::lofiType, "Lo-Fi Type", v, effects::LoFi::typeNames, 0));
    layout.add (makeFloat (ParamIDs::lofiAmount, "Lo-Fi Amount", { 0.0f, 10.0f, 0.1f }, 5.0f, v, asKnob));
    layout.add (makeFloat (ParamIDs::lofiNoise, "Lo-Fi Noise", { 0.0f, 10.0f, 0.1f }, 3.0f, v, asKnob));
    layout.add (makeFloat (ParamIDs::lofiMix, "Lo-Fi Mix", { 0.0f, 1.0f, 0.01f }, 1.0f, v, asPercent));
    layout.add (makeFloat (ParamIDs::lofiLevel, "Lo-Fi Level", { -12.0f, 12.0f, 0.1f }, 0.0f, v, asDb));
}

void addInsertParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    // Each bus: PR7-G 0.52.0's DRIVE and GLITCH (their ranges and defaults, not GLITCH's POSITION), MOD and LO-FI as
    // the whole sound's (MOD on the Vortex), and its own DELAY and REVERB sends (100%, as an oscillator's).
    constexpr int v = 19;   // 0.20.0
    const auto skewed = [] (float min, float max, float centre, float step)
    {
        Range range { min, max, step };
        range.setSkewForCentre (centre);
        return range;
    };
    const ToText asKnob = [] (float value, int) { return juce::String (value, 1); };
    const ToText asHz = [] (float value, int) { return juce::String (value, value < 1.0f ? 2 : 1) + " Hz"; };
    const ToText asMs = [] (float value, int) { return juce::String (juce::roundToInt (value)) + " ms"; };
    const ToText asSemitones = [] (float value, int)
    {
        const auto semitones = juce::roundToInt (value);
        if (semitones == 0)
            return juce::String ("Unison");
        if (std::abs (semitones) % 12 == 0)
            return juce::String (semitones > 0 ? "+" : "-") + juce::String (std::abs (semitones) / 12) + " oct";
        return (semitones > 0 ? "+" : "") + juce::String (semitones) + " st";
    };
    const ToText asVortexEq = [] (float value, int)
    {
        const auto db = value * effects::Modulation::vortexEqRangeDb;
        return std::abs (db) < 0.05f ? juce::String ("Flat") : (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    };
    for (int b = 0; b < ParamIDs::numInsertBuses; ++b)
    {
        const auto id = [b] (const char* what) { return ParamIDs::ins (b, what); };
        const auto name = "Insert " + ParamIDs::insertBusNames[b + 1] + " ";
        layout.add (makeSwitch (id ("DriveOn"), name + "Drive On", v, false));
        layout.add (makeChoice (id ("DriveModel"), name + "Drive Model", v, effects::Drive::modelNames, (int) effects::Drive::Model::screamer));
        layout.add (makeFloat (id ("DriveGain"), name + "Drive", { 0.0f, 10.0f, 0.1f }, 5.0f, v, asKnob));
        layout.add (makeFloat (id ("DriveTone"), name + "Drive Tone", { 0.0f, 10.0f, 0.1f }, 5.0f, v, asKnob));
        layout.add (makeFloat (id ("DriveLevel"), name + "Drive Level", { -24.0f, 12.0f, 0.1f }, 0.0f, v, asDb));
        layout.add (makeChoice (id ("DriveAttack"), name + "Skyline Attack", v, effects::Drive::skylineAttackNames, effects::Drive::skylineTubeScreamerAttack));
        layout.add (makeFloat (id ("DriveGate"), name + "Skyline Gate", { 0.0f, 10.0f, 0.1f }, 0.0f, v, asKnob));
        layout.add (makeFloat (id ("DriveHigh"), name + "Abyss High", { 0.0f, 10.0f, 0.1f }, 5.0f, v, asKnob));
        layout.add (makeFloat (id ("DriveLow"), name + "Abyss Low", { 0.0f, 10.0f, 0.1f }, 5.0f, v, asKnob));
        layout.add (makeFloat (id ("DriveMiddle"), name + "Abyss Middle", { 0.0f, 10.0f, 0.1f }, 5.0f, v, asKnob));
        layout.add (makeFloat (id ("DriveMidFreq"), name + "Abyss Mid Freq", { 0.0f, 10.0f, 0.1f }, 5.0f, v, asKnob));

        layout.add (makeSwitch (id ("GlitchOn"), name + "Glitch On", v, false));
        layout.add (makeChoice (id ("GlitchMode"), name + "Glitch Mode", v, effects::Glitch::modeNames, (int) effects::Glitch::Mode::sequence));
        layout.add (makeSwitch (id ("GlitchSync"), name + "Glitch Sync", v, true));
        layout.add (makeFloat (id ("GlitchTime"), name + "Glitch Length", skewed (10.0f, 1000.0f, 150.0f, 1.0f), 120.0f, v, asMs));
        layout.add (makeChoice (id ("GlitchDivision"), name + "Glitch Length (Sync)", v, effects::Glitch::syncNames, 3));
        layout.add (makeFloat (id ("GlitchDensity"), name + "Glitch Density", { 0.0f, 10.0f, 0.1f }, 5.0f, v, asKnob));
        layout.add (makeFloat (id ("GlitchPitch"), name + "Glitch Pitch", { -12.0f, 12.0f, 1.0f }, 0.0f, v, asSemitones));
        layout.add (makeFloat (id ("GlitchChaos"), name + "Glitch Chaos", { 0.0f, 10.0f, 0.1f }, 3.0f, v, asKnob));
        layout.add (makeFloat (id ("GlitchMix"), name + "Glitch Mix", { 0.0f, 1.0f, 0.01f }, 0.5f, v, asPercent));

        const auto& mod = ParamIDs::insModIds (b);
        layout.add (makeSwitch (mod.on, name + "Mod On", v, false));
        layout.add (makeChoice (mod.type, name + "Mod Type", v, effects::Modulation::typeNames, (int) effects::Modulation::Type::chorus));
        layout.add (makeFloat (mod.rate, name + "Mod Rate", skewed (0.05f, 10.0f, 1.0f, 0.01f), 0.8f, v, asHz));
        layout.add (makeFloat (mod.depth, name + "Mod Depth", { 0.0f, 1.0f, 0.01f }, 0.45f, v, asPercent));
        layout.add (makeFloat (mod.mix, name + "Mod Mix", { 0.0f, 1.0f, 0.01f }, 0.5f, v, asPercent));
        layout.add (makeChoice (mod.sync, name + "Mod Sync", v, effects::Delay::syncNames, 0));
        layout.add (makeFloat (mod.eq, name + "Mod EQ", { -1.0f, 1.0f, 0.01f }, 0.0f, v, asVortexEq));
        layout.add (makeSwitch (mod.stereo, name + "Mod Stereo", v, false));
        layout.add (makeFloat (mod.manual, name + "Mod AEON Manual", { 0.0f, 10.0f, 0.1f }, 5.0f, v, asKnob));
        layout.add (makeFloat (mod.res, name + "Mod AEON Res", { 0.0f, 10.0f, 0.1f }, 5.0f, v, asKnob));
        layout.add (makeChoice (mod.hadron, name + "Mod Hadron Circuit", v, effects::Modulation::hadronCircuitNames, 0));
        layout.add (makeFloat (mod.wave, name + "Mod Razor Wave", { 0.0f, 10.0f, 0.1f }, 0.0f, v, asKnob));

        layout.add (makeSwitch (id ("LofiOn"), name + "Lo-Fi On", v, false));
        layout.add (makeChoice (id ("LofiType"), name + "Lo-Fi Type", v, effects::LoFi::typeNames, 0));
        layout.add (makeFloat (id ("LofiAmount"), name + "Lo-Fi Amount", { 0.0f, 10.0f, 0.1f }, 5.0f, v, asKnob));
        layout.add (makeFloat (id ("LofiNoise"), name + "Lo-Fi Noise", { 0.0f, 10.0f, 0.1f }, 3.0f, v, asKnob));
        layout.add (makeFloat (id ("LofiMix"), name + "Lo-Fi Mix", { 0.0f, 1.0f, 0.01f }, 1.0f, v, asPercent));
        layout.add (makeFloat (id ("LofiLevel"), name + "Lo-Fi Level", { -12.0f, 12.0f, 0.1f }, 0.0f, v, asDb));

        layout.add (makeFloat (id ("SendDelay"), name + "Send Delay", { 0.0f, 1.0f, 0.01f }, 1.0f, v, asPercent));
        layout.add (makeFloat (id ("SendReverb"), name + "Send Reverb", { 0.0f, 1.0f, 0.01f }, 1.0f, v, asPercent));
    }
}
