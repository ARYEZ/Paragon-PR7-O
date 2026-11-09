#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "LevelMeterSource.h"
#include "Parameters.h"
#include "Rack.h"
#include "dsp/sampler/Sampler.h"
#include "rigs/RigLibrary.h"
#include "rigs/UndoHistory.h"
#include "rigs/MidiLinks.h"

// Which preset is loaded, for the header and the Save button. (Presets are "rigs" in the code, as in the rest of the
// PR7 family, whose rig browser, files and undo this shares.)
struct CurrentRig
{
    juce::String name { "Init" };
    juce::File file;              // set for the user's own presets
    juce::String factoryName;     // set for factory presets
    bool isUserRig() const { return file != juce::File(); }

    bool operator== (const CurrentRig& other) const
    {
        return name == other.name && file == other.file && factoryName == other.factoryName;
    }
};

// PR7-O: an instrument. MIDI in (the DAW's, and the INSTRUMENT page's keyboard), an SFZ instrument played by the
// sampler, then the effects kept from PR7-D (an insert bus, the DELAY and REVERB sends, MOD / MOD 2 / LO-FI, the
// MASTER rack), the preset's level and OUTPUT, the LIMITER, BYPASS; stereo out.
class ParagonX1AudioProcessor final : public juce::AudioProcessor,
                                      private juce::Timer,
                                      private juce::AudioProcessorValueTreeState::Listener,
                                      private juce::AudioProcessorParameter::Listener
{
public:
    ParagonX1AudioProcessor();
    ~ParagonX1AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // The DAW's own bypass button drives our BYPASS parameter, so the two can't disagree.
    juce::AudioProcessorParameter* getBypassParameter() const override;

    // Presets live in the preset browser, so the host's program list is a single entry.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Presets (message thread only)
    RigLibrary& getRigLibrary() noexcept { return *rigLibrary; }
    void useRigFolder (const juce::File& folder);   // another presets folder (tests)
    Rig captureRig() const;                          // the current sound as a preset (not saved anywhere yet)
    void loadRig (const RigLibrary::Entry& entry);
    // Saves over the current preset if it's one of the user's own and returns true; false if it isn't (a factory
    // preset or never saved): the caller asks for a name and uses saveCurrentRigAs.
    bool saveCurrentRig();
    bool saveCurrentRigAs (const juce::String& name);
    void stepRig (int direction);   // the next (+1) or previous (-1) preset in the library
    bool updateRigDetails (const RigLibrary::Entry& entry, const Rig& newDetails);
    bool deleteRig (const RigLibrary::Entry& entry);
    CurrentRig getCurrentRig() const { return currentRig; }
    const RigLibrary::Entry* findCurrentRigEntry() const;
    // THE DEFAULT PRESET (PR7-D 0.70.0's): the one every new PR7-O opens with ("Default Preset.xml" in the presets
    // folder). A new instance applies it on its first housekeeping tick unless the DAW gave it a saved state first.
    void setDefaultRig (const RigLibrary::Entry* entry);   // null: none (Init)
    bool isDefaultRig (const RigLibrary::Entry& entry) const;
    const RigLibrary::Entry* findDefaultRigEntry() const;
    bool applyDefaultRigIfPending();
    juce::File defaultRigFile() const;
    bool isRigModified() const noexcept { return rigModified.load(); }
    juce::ChangeBroadcaster rigBroadcaster;   // the preset list or current preset changed

    //==========================================================================
    // UNDO / REDO (message thread only)
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }
    void undo();
    void redo();
    void flushUndoStep() { recordCheckpointIfReady(); }   // tests: a finished edit recorded now

    //==========================================================================
    // THE INSTRUMENT (0.1.0): an SFZ file (or a single audio file, over the whole keyboard), loaded on a background
    // thread and swapped in on the message thread (one undo step). Presets and projects keep its file and load it again.
    void loadInstrument (const juce::File& file);
    void clearInstrument();
    // Tests (and anything that made one in memory): this program now, under `file`'s name (not loaded again from it).
    void setProgram (std::shared_ptr<const sampler::Program>, const juce::String& file = {});
    std::shared_ptr<const sampler::Program> getProgram() const noexcept { return program; }
    juce::String getInstrumentFile() const { return instrumentFile; }   // what's loaded, or loading (empty: none)
    bool isLoadingInstrument() const noexcept { return pendingLoads.load() > 0; }
    double getLoadProgress() const noexcept { return pendingLoads.load() > 0 ? loadProgress.load() : -1.0; }   // 0-1; -1 none
    void cancelLoad() noexcept { ++loadGeneration; }
    juce::String getLoadProblem() const;   // why the last load gave nothing (empty: it didn't fail)
    bool waitForInstrument (int timeoutMs = 60000);   // the load finished and installed (tests; snapshots)
    const sampler::Sampler& getSampler() const noexcept { return sampler; }
    // 0.2.0, KEYSWITCHES: the articulation chosen (its keyswitch key; -1 none), and choosing one (as pressing its key
    // does; applied at the next block). Kept in the project, not in presets (a preset's instrument starts on its default).
    int getKeyswitch() const noexcept { return sampler.getKeyswitch(); }
    void chooseKeyswitch (int key) noexcept { sampler.chooseKeyswitch (key); }
    juce::ChangeBroadcaster instrumentBroadcaster;   // a new instrument (or none) installed, a load started or failed

    // The keyboard on the INSTRUMENT page plays into this, with the DAW's MIDI.
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

    //==========================================================================
    // MIDI LEARN (PR7-K's stage 4f): controllers linked to knobs. A knob asks to learn; the next controller moved is
    // linked to it (from the timer).
    const MidiLinks& getMidiLinks() const noexcept { return midiLinks; }
    void setMidiLinks (const MidiLinks&);
    void startMidiLearn (const juce::String& parameterId);
    void cancelMidiLearn() { learning = {}; }
    void finishMidiLearn();
    const juce::String& getMidiLearnTarget() const noexcept { return learning; }
    int getLastController() const noexcept { return lastController.load(); }          // -1: none yet
    float getLastControllerValue() const noexcept { return lastControllerValue.load(); }
    juce::File midiLinksDefaultFile() const;
    bool saveMidiLinksAsDefault() const;
    bool exportMidiLinks (const juce::File&) const;
    bool importMidiLinks (const juce::File&);
    void controllerForTest (int controller, float value);
    void seedRandomForTest (juce::int64 seed) noexcept
    {
        rack->loFi.seedRandom (seed);
        sampler.seedRandom ((std::uint32_t) seed);
    }

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    LevelMeterSource& getOutputMeter() noexcept { return outputMeter; }
    double getHostBpm() const noexcept { return hostBpm.load(); }

    //==========================================================================
    // THE RACK (PR7-K's stage 7a): PR7-E 0.18.1's four effects, with PR7-E's names for what its pages read.
    eq::ParametricEq::Settings getEqSettings() const noexcept;
    const InputTap& getEqInputTap() const noexcept { return rack->eqInputTap; }
    const InputTap& getEqOutputTap() const noexcept { return rack->eqOutputTap; }
    void setEqSolo (int band) noexcept { rack->eqSoloBand = band; }
    int getEqSolo() const noexcept { return rack->eqSoloBand.load(); }
    double getEqAutoGainDb() const noexcept { return rack->parametricEq.getAutoGainDb(); }
    eq::ParametricEq::DynamicReading getEqDynamics (int band) const noexcept { return rack->parametricEq.getDynamics (band); }
    eq::match::Session& getEqMatch() noexcept { return rack->eqMatch; }

    dominion::Settings getDominionSettings() const noexcept;
    const dominion::Processor& getDominion() const noexcept { return rack->dominionProcessor; }
    void setDominionListen (int band) noexcept { rack->dominionListen = band; }
    int getDominionListen() const noexcept { return rack->dominionListen.load(); }
    DominionPresets& getDominionPresets() noexcept { return rack->dominionPresets; }
    void loadDominionPreset (const DominionPreset& preset) { DominionPresets::apply (preset, parameters); rack->dominionPreset = preset; }
    juce::String getDominionPresetName() const { return rack->dominionPreset.name; }
    bool isDominionPresetModified() { return ! DominionPresets::matches (rack->dominionPreset, parameters); }
    void rememberDominionPreset (const DominionPreset& preset) { rack->dominionPreset = preset; }

    CompPresets& getCompPresets() noexcept { return rack->compPresets; }
    void loadCompPreset (const CompPreset& preset) { CompPresets::apply (preset, parameters); rack->compPreset = preset; }
    juce::String getCompPresetName() const { return rack->compPreset.name; }
    bool isCompPresetModified() { return ! CompPresets::matches (rack->compPreset, parameters); }
    void rememberCompPreset (const CompPreset& preset) { rack->compPreset = preset; }

    effects::multiband::Settings getMultibandSettings() const noexcept;
    effects::multiband::Processor::Reading getMultibandReading (int band) const noexcept { return rack->multiband.getReading (band); }
    void setMultibandSolo (int band) noexcept { rack->multibandSolo = band; }
    void updateLatency();   // the delay CONSTRICTOR's LOOK-AHEAD and the LIMITER add, told to the DAW

    float getCompressorReductionDb (int channel) const noexcept { return rack->compressor.getReductionDb (channel); }
    float getLimiterReductionDb (int channel) const noexcept { return rack->limiter.getReductionDb (channel); }
    effects::MasterLimiter& getLimiter() noexcept { return rack->limiter; }
    effects::MasterLimiter::Settings getLimiterSettings() const;

    // THE SENDS (stage 7b), MOD / MOD 2 / LO-FI (7c), the insert buses (7d).
    effects::Delay::Settings getDelaySettings() const noexcept;
    effects::Reverb::Settings getReverbSettings() const noexcept;
    effects::Modulation::Settings getModSettings (int which) const noexcept;
    effects::LoFi::Settings getLoFiSettings() const noexcept;
    effects::Drive::Settings getInsertDriveSettings (int bus) const noexcept;
    effects::Glitch::Settings getInsertGlitchSettings (int bus) const noexcept;
    effects::Modulation::Settings getInsertModSettings (int bus) const noexcept { return modSettingsFor (ParamIDs::insModIds (bus)); }
    effects::LoFi::Settings getInsertLoFiSettings (int bus) const noexcept;
    std::array<float, fx::numSends> getInsertSends (int bus) const noexcept;
    float getInsertPeakDb (int bus) const noexcept
    {
        const auto peak = rack->buses[(size_t) juce::jlimit (0, fx::numInsertBuses - 1, bus)].peak.load();
        return peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -100.0f;
    }
    float getInsertModSweepPhase (int bus) const noexcept { return rack->buses[(size_t) juce::jlimit (0, fx::numInsertBuses - 1, bus)].modulation.getSweepPhase(); }
    float getModSweepPhase (int which) const noexcept { return rack->modulation[(size_t) juce::jlimit (0, 1, which)].getSweepPhase(); }
    float getLoFiPeakDb() const noexcept
    {
        const auto peak = rack->loFiPeak.load();
        return peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -100.0f;
    }
    float getReturnPeakDb (int send) const noexcept
    {
        const auto peak = rack->returnPeak[(size_t) juce::jlimit (0, fx::numSends - 1, send)].load();
        return peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -100.0f;
    }
    int mainLatencySamples() const noexcept;

private:
    void processChunk (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, int start, int numSamples);
    void processSends (juce::dsp::AudioBlock<float> block);
    void processModFx (juce::dsp::AudioBlock<float> block);
    void processInsertBuses (juce::dsp::AudioBlock<float> block, bool sendsReady);
    void processRack (juce::dsp::AudioBlock<float> block);
    sampler::Sampler::Settings getSamplerSettings() const noexcept;
    effects::Modulation::Settings modSettingsFor (const ParamIDs::ModIds&) const noexcept;
    effects::LoFi::Settings loFiSettingsFor (int bus) const noexcept;   // -1: the whole sound's
    float param (const char* id) const;
    struct TransparentHash
    {
        using is_transparent = void;
        size_t operator() (std::string_view v) const noexcept { return std::hash<std::string_view>{} (v); }
    };
    std::unordered_map<std::string, std::atomic<float>*, TransparentHash, std::equal_to<>> rawValues;
    bool isOn (const char* id) const { return param (id) > 0.5f; }

    void timerCallback() override;
    void parameterChanged (const juce::String& parameterId, float newValue) override;
    void markModified() noexcept;
    void applyRig (const Rig& rig);
    // Undo snapshots: the sound and which preset it is.
    struct Snapshot
    {
        Rig rig;
        CurrentRig current;
        bool modified = false;   // restored too, but doesn't count as a change

        bool operator== (const Snapshot& other) const
        {
            return rig.parameters == other.rig.parameters && rig.instrument == other.rig.instrument && current == other.current;
        }
    };
    Snapshot takeSnapshot() const;
    void restoreSnapshot (const Snapshot&);
    void requestCheckpoint() noexcept { checkpointWanted = true; }
    void recordCheckpointIfReady();
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

    // THE INSTRUMENT: the program the message thread has, the audio thread's plain pointer to it, replaced ones kept
    // 2 s (a block may still be playing them), and the loader.
    void startLoading (const juce::String& file, bool undoStep);
    void installLoaded();
    void installProgram (std::shared_ptr<const sampler::Program>, const juce::String& file);
    std::shared_ptr<const sampler::Program> program;
    std::atomic<const sampler::Program*> programForAudio { nullptr };
    std::vector<std::pair<std::shared_ptr<const sampler::Program>, double>> retiredPrograms;
    juce::String instrumentFile;
    int pendingKeyswitch = -1;   // a project's articulation, waiting for its instrument
    std::unique_ptr<juce::ThreadPool> loaderPool;
    std::atomic<int> pendingLoads { 0 }, loadGeneration { 0 };
    std::atomic<double> loadProgress { 0.0 };
    mutable std::mutex loadedLock;
    struct Loaded { std::shared_ptr<const sampler::Program> program; juce::String file; int generation = 0; bool undoStep = false; };
    std::vector<Loaded> loaded;
    juce::String loadProblem;
    std::shared_ptr<int> lifeToken = std::make_shared<int> (0);
    sampler::Sampler sampler;

    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboardState;
    juce::MidiBuffer keyboardMidi;   // the DAW's MIDI plus the keyboard's, per block
    void retireOldData();

    // Smoothing stops zipper noise when OUTPUT, the preset's level or a send moves.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;
    juce::SmoothedValue<float> bypassAmount;   // 1: silent
    juce::SmoothedValue<float> sendDelayGain, sendReverbGain;

    std::unique_ptr<Rack> rack = std::make_unique<Rack>();   // made first: the rig folder sets its folders
    std::unique_ptr<RigLibrary> rigLibrary;
    CurrentRig currentRig;
    std::atomic<bool> rigModified { false }, applyingRig { false };

    UndoHistory<Snapshot> history;
    bool defaultRigPending = true;
    std::atomic<int> activeGestures { 0 };
    std::atomic<bool> checkpointWanted { true };

    int preparedBlockSize = 512;
    std::atomic<double> hostBpm { 120.0 };
    double hostPpq = -1.0;   // the song position at this block's start (-1: stopped); audio thread
    MidiLinks midiLinks;
    std::array<std::atomic<juce::RangedAudioParameter*>, 128> controllerTargets {};
    std::atomic<int> lastController { -1 }, controllerCount { 0 };
    std::atomic<float> lastControllerValue { 0.0f };
    juce::String learning;
    int learnFromCount = 0;
    juce::AudioBuffer<float> instrumentBuffer;   // what the sampler plays, before it goes to MAIN or its insert bus

    LevelMeterSource outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParagonX1AudioProcessor)
};
