#pragma once

#include "Parameters.h"
#include "dsp/InputTap.h"
#include "dsp/dominion/Dominion.h"
#include "dsp/effects/Compressor.h"
#include "dsp/effects/Delay.h"
#include "dsp/effects/Drive.h"
#include "dsp/effects/Glitch.h"
#include "dsp/effects/LoFi.h"
#include "dsp/effects/Modulation.h"
#include "dsp/effects/MasterLimiter.h"
#include "dsp/effects/MultibandComp.h"
#include "dsp/effects/Reverb.h"
#include "dsp/eq/EqMatchSession.h"
#include "dsp/eq/ParametricEq.h"
#include "rigs/CompPresets.h"
#include "rigs/DominionPresets.h"

// The sends (DELAY, REVERB) and the insert buses (A to D).
namespace fx
{
    inline constexpr int numSends = 2, numInsertBuses = 4;
}

// THE RACK (PR7-K's stage 7a): PR7-E 0.18.1's four effects after the instrument, in PR7-E's order: ECHELON (the EQ), DOMINION,
// CONSTRICTOR (its bands, then the full-band compressor), [OUTPUT], the LIMITER. Kept together on the heap: the
// effects' filter banks and buffers are far bigger than a thread's stack can hold a copy of (Windows: 1 MB), and
// the tests build the processor on the stack.
struct Rack
{
    eq::ParametricEq parametricEq;
    struct EqBandParameters { std::atomic<float>* values[std::size (ParamIDs::eqBandSettings)] {}; };
    std::array<EqBandParameters, ParamIDs::eqNumBands> eqBandParameters {};
    std::atomic<int> eqSoloBand { -1 };
    InputTap eqInputTap { 16384 }, eqOutputTap { 16384 };   // what goes into and comes out of the EQ, for its analyser
    eq::match::Session eqMatch { {} };                       // EQ Match's listening (its folder is set with the presets folder)

    dominion::Processor dominionProcessor;
    struct DominionBandParameters { std::atomic<float>* values[std::size (ParamIDs::domBandSettings)] {}; };
    std::array<DominionBandParameters, ParamIDs::domNumBands> dominionBandParameters {};
    std::atomic<int> dominionListen { -1 };
    DominionPresets dominionPresets { {} };
    DominionPreset dominionPreset = DominionPresets::factory().front();   // "Default"

    CompPresets compPresets { {} };
    CompPreset compPreset = CompPresets::factory().front();
    effects::Compressor compressor;
    effects::multiband::Processor multiband;   // CONSTRICTOR's bands, before the full band
    struct MultibandBandParameters { std::atomic<float>* values[std::size (ParamIDs::mbBandSettings)] {}; };
    std::array<MultibandBandParameters, ParamIDs::mbNumBands> multibandBandParameters {};
    std::atomic<int> multibandSolo { -1 };
    juce::AudioBuffer<float> compSidechain;   // what it listens to: the audio itself

    effects::MasterLimiter limiter;   // with its OUTPUT TRIM and DITHER
    double sampleRate = 48000.0;

    // THE SENDS (PR7-K's stage 7b): PR7-G's DELAY and REVERB, each fed its own mix from the instrument (at its
    // SEND amount), returning only their echoes / reverb into the mix before ECHELON.
    effects::Delay delay;
    effects::Reverb reverb;
    std::array<juce::AudioBuffer<float>, fx::numSends> sends;   // 2 channels each, the prepared block size
    std::array<std::atomic<float>, fx::numSends> returnPeak {};   // each return's peak in the last block (the units' meters)

    // MOD, MOD 2 and LO-FI (stage 7c): PR7-G's modulation block twice and its LO-FI, on the whole sound (the returns
    // included), before ECHELON.
    std::array<effects::Modulation, 2> modulation;
    effects::LoFi loFi;
    std::atomic<float> loFiPeak { 0.0f };   // LO-FI's output peak in the last block, while it's on (its unit's meter)

    // THE INSERT BUSES (stage 7d): A to D, each PR7-G's DRIVE and GLITCH (mono there: one a side here), MOD and LO-FI,
    // on the instrument when it's routed to it, before the sends (the bus sends at its own amounts) and the mix.
    struct InsertBus
    {
        std::array<effects::Drive, 2> drive;
        std::array<effects::Glitch, 2> glitch;
        effects::Modulation modulation;
        effects::LoFi loFi;
        juce::AudioBuffer<float> buffer;   // 2 channels, the prepared block size: what the instrument plays into it
        std::atomic<float> peak { 0.0f };  // its output's peak in the last block (0 while nothing's routed to it)
        bool used = false;                 // the instrument is routed to it (only then does it run)
        std::array<float, 2> driveDc {};   // what the DC blocker after the DRIVE takes off each side (see processInsertBuses)
        // Its parameters (ParamIDs::insDriveSettings... order), found once: a bus's IDs are made strings.
        std::array<std::atomic<float>*, std::size (ParamIDs::insDriveSettings)> drive_ {};
        std::array<std::atomic<float>*, std::size (ParamIDs::insGlitchSettings)> glitch_ {};
        std::array<std::atomic<float>*, std::size (ParamIDs::insLofiSettings)> lofi_ {};
        std::array<std::atomic<float>*, fx::numSends> sends_ {};
    };
    std::array<InsertBus, fx::numInsertBuses> buses;

    void setFolder (const juce::File& presetsFolder)
    {
        eqMatch.setReferencesFolder (presetsFolder.getChildFile ("Match References"));
        dominionPresets.setFolder (presetsFolder.getChildFile ("Dominion Presets"));
        compPresets.setFolder (presetsFolder.getChildFile ("Comp Presets"));
    }
};
