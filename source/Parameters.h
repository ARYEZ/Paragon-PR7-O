#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <string_view>

// Parameter IDs are saved in DAW projects and presets, so never rename one
// once it has shipped. Add new parameters with a new ID instead.
//
// PR7-O keeps the PR7 family's IDs for the effects it took from PR7-D (PR7-E's rack, PR7-G's sends, MOD, LO-FI and
// insert buses), so the same names mean the same knobs across the family. The drums' are gone.
namespace ParamIDs
{
    // Global: not part of a preset.
    inline constexpr auto outputGain = "outputGain";   // the OUTPUT knob
    inline constexpr auto bypass     = "bypass";

    // Saved with each preset: its own level, to balance presets against each other.
    inline constexpr auto patchLevel = "patchLevel";

    // THE INSTRUMENT (PR7-O 0.1.0, version hint 42): the SFZ instrument loaded (its file is preset data, not a
    // parameter) and how it plays. In presets.
    inline constexpr auto instLevel     = "instLevel";       // dB (-60: silent)
    inline constexpr auto instPan       = "instPan";         // -1 to 1, a balance after the instrument's own
    inline constexpr auto instTranspose = "instTranspose";   // semitones
    inline constexpr auto instTune      = "instTune";        // cents
    inline constexpr auto instRelease   = "instRelease";     // x the instrument's release times
    inline constexpr auto instVoices    = "instVoices";      // POLYPHONY
    inline constexpr auto instSendDelay  = "instSendDelay";  // 0 to 1: into the DELAY
    inline constexpr auto instSendReverb = "instSendReverb"; // 0 to 1: into the REVERB
    inline constexpr auto instInsert    = "instInsert";      // insertBusNames' index: an insert bus it plays through (0 none)
    // 0.2.0 (version hint 43): DYNAMICS (dynamicsNames: velocity as the file says, or the mod wheel crossfading a held
    // note's layers) and AUTO SUSTAIN (held recordings without loops go round their steady part). In presets.
    inline constexpr auto instDynamics    = "instDynamics";
    inline constexpr auto instAutoSustain = "instAutoSustain";
    inline const juce::StringArray dynamicsNames { "Velocity", "Mod Wheel" };   // saved by index: only append
    // 0.7.0 (version hint 44): LEGATO (on: a note played over a held one joins it, past its attack) and GLIDE (seconds:
    // a joined note's slide in pitch). In presets; off and 0 by default (as 0.6.0 played).
    inline constexpr auto instLegato = "instLegato";
    // 0.8.0 (version hint 45): THE STAGE. STAGE on (each part heard from its seat through a pair of mics, with the
    // room's reflections and tail), the ROOM (stageRoomNames), MIC (the pair's distance from the stage's front, metres),
    // ROOM LEVEL (dB on the reflections and tail), DECAY (x the room's), SPREAD (the seats' width across, x). In presets;
    // off by default (as 0.7.0 played).
    inline constexpr auto stageOn        = "stageOn";
    inline constexpr auto stageRoom      = "stageRoom";
    inline constexpr auto stageMic       = "stageMic";
    inline constexpr auto stageRoomLevel = "stageRoomLevel";
    inline constexpr auto stageDecay     = "stageDecay";
    inline constexpr auto stageSpread    = "stageSpread";
    inline const juce::StringArray stageRoomNames { "Studio", "Scoring Stage", "Concert Hall", "Cathedral" };   // by index: only append
    // 0.10.0 (version hint 46): THE ENSEMBLE. PLAYERS (1-4: each note played by that many copies, each a little out of
    // tune by up to DETUNE cents, early or late by up to TIMING ms, across the width), DOUBLE (doubleNames: every note
    // also an octave up / down / both) at DOUBLE LEVEL (dB). In presets; 1 player and no doubling by default.
    inline constexpr auto ensPlayers     = "ensPlayers";
    inline constexpr auto ensDetune      = "ensDetune";
    inline constexpr auto ensTiming      = "ensTiming";
    inline constexpr auto ensDouble      = "ensDouble";
    inline constexpr auto ensDoubleLevel = "ensDoubleLevel";
    inline const juce::StringArray doubleNames { "Off", "Octave Up", "Octave Down", "Octaves Up + Down" };   // by index: only append
    inline constexpr auto instGlide  = "instGlide";
    // Global, like OUTPUT: they suit the player's controller, not a sound.
    inline constexpr auto midiChannel = "midiChannel";       // midiChannelNames' index (0: any)
    inline constexpr auto noteNames = "noteNames";           // notenames::choiceNames: middle C is C3 or C4 (PR7-D 0.35.1's)
    inline const juce::StringArray midiChannelNames { "Any", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" };

    // THE RACK (stage 7a, 0.17.0): PR7-E 0.18.1's four effects, with PR7-E's IDs (so their presets load in either
    // plugin) and its comments below (its "stomps" are our rack units; "rigs" our presets).
    // ECHELON, the EQ (inspired by FabFilter Pro-Q 4; see dsp/eq).
    inline constexpr auto eqOn        = "eqOn";
    inline constexpr auto eqOutput    = "eqOutput";
    inline constexpr auto eqGainScale = "eqGainScale";
    inline constexpr auto eqAutoGain  = "eqAutoGain";
    inline constexpr int eqNumBands = 24;
    // Each band's settings: "eqB1Used", "eqB1Freq"... `what` is one of the names below.
    // From 0.5.0, its dynamics: Dyn (the range), DynAuto, Thresh, Attack, Release.
    inline juce::String eqBand (int band, const char* what) { return "eqB" + juce::String (band + 1) + what; }
    inline constexpr const char* eqBandSettings[] = { "Used", "On", "Shape", "Freq", "Gain", "Q", "Slope", "Place",
                                                      "Dyn", "DynAuto", "Thresh", "Attack", "Release" };

    // Stomp 2, COMP (0.9.0): PR7-C's compressor, ported. After the EQ.
    inline constexpr auto compOn        = "compOn";
    inline constexpr auto compThreshold = "compThreshold";   // dBFS
    inline constexpr auto compRatio     = "compRatio";       // 1 = SOFT, 30 = infinite
    inline constexpr auto compAttack    = "compAttack";      // ms
    inline constexpr auto compRelease   = "compRelease";     // ms
    inline constexpr auto compAuto      = "compAuto";        // AUTO release
    inline constexpr auto compMakeUp    = "compMakeUp";      // dB
    inline constexpr auto compCharacter = "compCharacter";   // choice: VCA, OPTO, FET
    inline constexpr auto compLink      = "compLink";        // both channels together
    // COMP's bands (0.13.0): multiband compression and expansion, inspired by
    // FabFilter Pro-MB (our own design, see dsp/effects/MultibandComp.h).
    // The bands come first, then the full-band compressor above, which
    // compWide switches (on by default, so older rigs sound as they did).
    inline constexpr auto compWide    = "compWide";
    inline constexpr auto mbLookahead = "mbLookahead";   // COMP 20 ms late, the bands hearing ahead
    inline constexpr auto mbMix       = "mbMix";         // %, 0 to 200
    inline constexpr int mbNumBands = 6;
    // "mbB1Used", "mbB1Low"... `what` is one of the names below.
    inline juce::String mbBand (int band, const char* what) { return "mbB" + juce::String (band + 1) + what; }
    inline constexpr const char* mbBandSettings[] = { "Used", "On", "Low", "High", "Slope", "Thresh", "Range", "Mode", "Ratio",
                                                      "Knee", "Attack", "Release", "Char", "Stereo", "Link", "Out", "Look" };

    // Stomp 5, DOMINION (0.10.0): dynamic resonance suppression, inspired by
    // oeksound soothe 3 (our own design, see dsp/dominion/Dominion.h). After
    // the EQ, before COMP.
    inline constexpr auto domOn      = "domOn";
    inline constexpr auto domMode    = "domMode";      // choice: Soft, Hard
    inline constexpr auto domDepth   = "domDepth";     // 0 to 20
    inline constexpr auto domDetail  = "domDetail";    // 0 to 100
    inline constexpr auto domAttack  = "domAttack";    // 0 to 100
    inline constexpr auto domRelease = "domRelease";   // 0 to 100
    inline constexpr auto domStereo  = "domStereo";    // choice: L/R, M/S
    inline constexpr auto domLink    = "domLink";      // %
    inline constexpr auto domFocus   = "domFocus";     // -100 to 100
    inline constexpr auto domDetailLow = "domDetailLow", domDetailHigh = "domDetailHigh",
                          domAttackLow = "domAttackLow", domAttackHigh = "domAttackHigh",
                          domReleaseLow = "domReleaseLow", domReleaseHigh = "domReleaseHigh";   // the tilts, -100 to 100
    inline constexpr auto domMaxCut  = "domMaxCut";    // dB (40: no limit)
    inline constexpr auto domWetTrim = "domWetTrim";   // dB
    inline constexpr auto domMix     = "domMix";       // %
    inline constexpr auto domOut     = "domOut";       // dB
    inline constexpr auto domDelta   = "domDelta";
    inline constexpr int domNumBands = 8;
    // The depth curve's bands: "domB1Used", "domB1Freq"... `what` is one of the names below.
    inline juce::String domBand (int band, const char* what) { return "domB" + juce::String (band + 1) + what; }
    inline constexpr const char* domBandSettings[] = { "Used", "On", "Shape", "Freq", "Depth", "Q", "Slope", "Focus" };

    // Stomp 7, the LIMITER (0.8.0): PR7-C's limiter and output trim, ported.
    // The last thing in the chain, after OUTPUT, RIG LEVEL and the volume pedal.
    inline constexpr auto limitOn        = "limitOn";
    inline constexpr auto limitThreshold = "limitThreshold";   // dBFS
    inline constexpr auto limitMakeUp    = "limitMakeUp";      // dB
    inline constexpr auto limitMode      = "limitMode";        // choice: Brick, Soft, Auto
    inline constexpr auto limitMix       = "limitMix";         // %
    inline constexpr auto limitLink      = "limitLink";        // both channels together
    inline constexpr auto limitTrim      = "limitTrim";        // OUTPUT TRIM, dB, after the limiter
    // From 0.16.0, inspired by Pro-L 2 (version hint 36): GAIN drives into the
    // OUTPUT ceiling. limitThreshold and limitMakeUp became GAIN and OUTPUT
    // (projects and rigs from before are converted when they load); they're
    // kept only so old automation and files still find them.
    inline constexpr auto limitGain          = "limitGain";          // dB
    inline constexpr auto limitCeiling       = "limitCeiling";       // OUTPUT, dBFS (dBTP with TRUE PEAK)
    inline constexpr auto limitLookahead     = "limitLookahead";     // ms
    inline constexpr auto limitAttack        = "limitAttack";        // ms, the release stage
    inline constexpr auto limitRelease       = "limitRelease";       // ms
    inline constexpr auto limitLinkTransient = "limitLinkTransient"; // %
    inline constexpr auto limitLinkRelease   = "limitLinkRelease";   // %
    inline constexpr auto limitTruePeak      = "limitTruePeak";
    inline constexpr auto limitOversampling  = "limitOversampling";  // choice: Off, 2x, 4x, 8x
    inline constexpr auto limitDither        = "limitDither";        // choice: Off, 24 ... 16 bits
    inline constexpr auto limitShaping       = "limitShaping";       // choice: None, Basic, Optimized, Weighted
    inline constexpr auto limitUnity         = "limitUnity";         // listening aids: not in rigs
    inline constexpr auto limitDelta         = "limitDelta";
    inline constexpr auto limitDcFilter      = "limitDcFilter";


    // THE SENDS (stage 7b, 0.18.0): PR7-G 0.52.0's DELAY and REVERB, with PR7-G's IDs, ranges and defaults; each
    // instrument's SEND DELAY / SEND REVERB feeds them, and their MIX is the return's level.
    inline constexpr auto delayOn        = "delayOn";
    inline constexpr auto delayTime      = "delayTime";       // ms (SYNC off)
    inline constexpr auto delaySync      = "delaySync";       // choice: effects::Delay::syncNames
    inline constexpr auto delayFeedback  = "delayFeedback";   // 0 to 0.95
    inline constexpr auto delayTone      = "delayTone";       // 0 to 10
    inline constexpr auto delayMix       = "delayMix";        // 0 to 1: the return's level
    inline constexpr auto delayPingPong  = "delayPingPong";
    inline constexpr auto delayType      = "delayType";       // choice: effects::Delay::typeNames
    inline constexpr auto delayDuck      = "delayDuck";       // 0 to 1
    inline constexpr auto reverbOn       = "reverbOn";
    inline constexpr auto reverbSize     = "reverbSize";      // 0 to 1
    inline constexpr auto reverbTone     = "reverbTone";      // 0 to 10
    inline constexpr auto reverbMix      = "reverbMix";       // 0 to 1: the return's level
    inline constexpr auto reverbType     = "reverbType";      // choice: effects::Reverb::typeNames
    inline constexpr auto reverbPreDelay = "reverbPreDelay";  // ms
    inline constexpr auto reverbDuck     = "reverbDuck";      // 0 to 1
    inline constexpr auto delayToReverb  = "delayToReverb";   // PR7-K's own: how much of DELAY's echoes go on into REVERB

    // MOD, MOD 2 and LO-FI (stage 7c, version hint 18): PR7-G 0.52.0's IDs, ranges and defaults, so the same names
    // mean the same knobs across the family. Not PR7-G's MOD POSITION: there's no amp to put them before.
    inline constexpr auto modOn      = "modOn";
    inline constexpr auto modType    = "modType";      // choice: effects::Modulation::typeNames
    inline constexpr auto modRate    = "modRate";      // Hz (SYNC off)
    inline constexpr auto modDepth   = "modDepth";     // 0 to 1
    inline constexpr auto modMix     = "modMix";       // 0 to 1 (E.LEVEL for the Vortex)
    inline constexpr auto modSync    = "modSync";      // choice: effects::Delay::syncNames ("Off" first)
    inline constexpr auto modEq      = "modEq";        // the Vortex's EQ, -1 to 1
    inline constexpr auto modStereo  = "modStereo";    // the Vortex's STEREO
    inline constexpr auto modManual  = "modManual";    // AEON's MANUAL, 0 to 10
    inline constexpr auto modRes     = "modRes";       // AEON's RES, 0 to 10
    inline constexpr auto modHadron  = "modHadron";    // Hadron's SCRIPT / BLOCK circuit
    inline constexpr auto modWave    = "modWave";      // Razor's WAVE, 0 to 10
    inline constexpr auto mod2On     = "mod2On";
    inline constexpr auto mod2Type   = "mod2Type";
    inline constexpr auto mod2Rate   = "mod2Rate";
    inline constexpr auto mod2Depth  = "mod2Depth";
    inline constexpr auto mod2Mix    = "mod2Mix";
    inline constexpr auto mod2Sync   = "mod2Sync";
    inline constexpr auto mod2Eq     = "mod2Eq";
    inline constexpr auto mod2Stereo = "mod2Stereo";
    inline constexpr auto mod2Manual = "mod2Manual";
    inline constexpr auto mod2Res    = "mod2Res";
    inline constexpr auto mod2Hadron = "mod2Hadron";
    inline constexpr auto mod2Wave   = "mod2Wave";

    // A MOD block's controls, so MOD and MOD 2 share their code (as on PR7-G).
    struct ModIds
    {
        const char *on, *type, *rate, *depth, *mix, *sync, *eq, *stereo, *manual, *res, *hadron, *wave;
    };
    inline constexpr ModIds modIds { modOn, modType, modRate, modDepth, modMix, modSync, modEq, modStereo, modManual, modRes, modHadron, modWave };
    inline constexpr ModIds mod2Ids { mod2On, mod2Type, mod2Rate, mod2Depth, mod2Mix, mod2Sync, mod2Eq, mod2Stereo, mod2Manual, mod2Res, mod2Hadron, mod2Wave };

    inline constexpr auto lofiOn     = "lofiOn";
    inline constexpr auto lofiType   = "lofiType";     // choice: effects::LoFi::typeNames
    inline constexpr auto lofiAmount = "lofiAmount";   // 0 to 10
    inline constexpr auto lofiNoise  = "lofiNoise";    // 0 to 10
    inline constexpr auto lofiMix    = "lofiMix";      // 0 to 1
    inline constexpr auto lofiLevel  = "lofiLevel";    // dB

    // THE INSERT BUSES (stage 7d, version hint 19): A to D, each "ins1"... "ins4" plus a setting below: PR7-G's
    // DRIVE and GLITCH (their IDs after the prefix, capitalised), then MOD and LO-FI as above, then the bus's SENDS.
    inline constexpr int numInsertBuses = 4;
    inline juce::String ins (int bus, const char* what) { return "ins" + juce::String (bus + 1) + what; }
    inline const juce::StringArray insertBusNames { "None", "A", "B", "C", "D" };   // instInsert, by index: only append
    inline constexpr const char* insDriveSettings[] = { "DriveOn", "DriveModel", "DriveGain", "DriveTone", "DriveLevel", "DriveAttack",
                                                        "DriveGate", "DriveHigh", "DriveLow", "DriveMiddle", "DriveMidFreq" };
    inline constexpr const char* insGlitchSettings[] = { "GlitchOn", "GlitchMode", "GlitchSync", "GlitchTime", "GlitchDivision",
                                                         "GlitchDensity", "GlitchPitch", "GlitchChaos", "GlitchMix" };
    inline constexpr const char* insModSettings[] = { "ModOn", "ModType", "ModRate", "ModDepth", "ModMix", "ModSync", "ModEq", "ModStereo",
                                                      "ModManual", "ModRes", "ModHadron", "ModWave" };   // ModIds' order
    inline constexpr const char* insLofiSettings[] = { "LofiOn", "LofiType", "LofiAmount", "LofiNoise", "LofiMix", "LofiLevel" };
    // A bus's MOD as ModIds (the strings made once and kept).
    inline const ModIds& insModIds (int bus)
    {
        static const auto table = []
        {
            std::array<std::array<juce::String, std::size (insModSettings)>, numInsertBuses> names;
            for (int b = 0; b < numInsertBuses; ++b)
                for (size_t k = 0; k < std::size (insModSettings); ++k)
                    names[(size_t) b][k] = ins (b, insModSettings[k]);
            return names;
        }();
        static const auto ids = []
        {
            std::array<ModIds, numInsertBuses> result {};
            for (size_t b = 0; b < (size_t) numInsertBuses; ++b)
            {
                const auto& n = table[b];
                result[b] = { n[0].toRawUTF8(), n[1].toRawUTF8(), n[2].toRawUTF8(), n[3].toRawUTF8(), n[4].toRawUTF8(), n[5].toRawUTF8(),
                              n[6].toRawUTF8(), n[7].toRawUTF8(), n[8].toRawUTF8(), n[9].toRawUTF8(), n[10].toRawUTF8(), n[11].toRawUTF8() };
            }
            return result;
        }();
        return ids[(size_t) juce::jlimit (0, numInsertBuses - 1, bus)];
    }

    // Global settings: kept when switching presets. Everything else is part of a preset.
    // The LIMITER's UNITY and DELTA are for listening while you set it, not part of a sound.
    inline const juce::StringArray notInRigs { outputGain, bypass, limitUnity, limitDelta, midiChannel, noteNames };
}

namespace ParamRanges
{
    // COMP's, as on PR7-C.
    inline constexpr float compThresholdMin = -50.0f, compThresholdMax = 0.0f;
    inline constexpr float compAttackMin = 0.1f, compAttackMax = 30.0f;          // ms
    inline constexpr float compReleaseMin = 50.0f, compReleaseMax = 3000.0f;     // ms
    inline constexpr float compMakeUpMax = 20.0f;
    inline const juce::StringArray compCharacters { "VCA", "OPTO", "FET" };    // saved by index: only append
    inline constexpr float compRatioMarks[] = { 1.0f, 1.5f, 2.0f, 4.0f, 10.0f, 30.0f };   // SOFT, then to infinite

    // The LIMITER's, as on PR7-C.
    inline constexpr float limitThresholdMin = -20.0f, limitMakeUpMax = 12.0f;
    inline constexpr float limitTrimMin = -12.0f, limitTrimMax = 12.0f;
    // Its styles: PR7-C's three, then (0.16.0) eight of our own after Pro-L 2's (effects::limiter::Style).
    inline const juce::StringArray limitModes { "Brick", "Soft", "Auto",   // saved by index: only append
                                                "Clear", "Punchy", "Dynamic", "All-round", "Aggressive", "Modern", "Glue", "Safe" };
    inline constexpr int limitDefaultMode = 8;   // Modern
    inline constexpr float limitGainMax = 30.0f, limitCeilingMin = -30.0f;

    inline constexpr float gainMinDb = -24.0f;
    inline constexpr float gainMaxDb = 12.0f;
    inline constexpr float outputDefaultDb = -6.0f;   // headroom (PR7-K's default, kept)
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
void addRackParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);   // RackParameters.cpp
void addSendParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);   // RackParameters.cpp (stage 7b)
void addModFxParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);   // RackParameters.cpp (stage 7c)
void addInsertParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);   // RackParameters.cpp (stage 7d)
