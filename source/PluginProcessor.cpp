#include "PluginProcessor.h"

#include "NoteNames.h"
#include "Parameters.h"
#include "PluginEditor.h"

namespace
{
    constexpr double gainRampSeconds = 0.02;

    // Extra settings kept in the saved state (DAW project).
    const juce::Identifier rigNameKey { "rigName" };
    const juce::Identifier rigFileKey { "rigFile" };
    const juce::Identifier rigFactoryKey { "rigFactory" };
    const juce::Identifier rigModifiedKey { "rigModified" };
    // Which version's parameters a project was saved with, as for presets (see Rig.cpp). 1: PR7-O 0.1.0.
    const juce::Identifier parameterVersionKey { "parameterVersion" };
    constexpr int parameterVersion = 1;
    const juce::Identifier instrumentKey { "Instrument" };
    const juce::Identifier midiLinksKey { "MidiLinks" };
    // A replaced instrument is kept this long after the audio thread has moved on from it (its voices fade in 20 ms).
    constexpr double retiredMs = 2000.0;
}

//==============================================================================
ParagonX1AudioProcessor::ParagonX1AudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ParagonX1", createParameterLayout())
{
    for (int b = 0; b < ParamIDs::numInsertBuses; ++b)
    {
        auto& bus = rack->buses[(size_t) b];
        const auto find = [this, b] (const char* what)
        {
            auto* value = parameters.getRawParameterValue (ParamIDs::ins (b, what));
            jassert (value != nullptr);
            return value;
        };
        for (size_t s = 0; s < bus.drive_.size(); ++s) bus.drive_[s] = find (ParamIDs::insDriveSettings[s]);
        for (size_t s = 0; s < bus.glitch_.size(); ++s) bus.glitch_[s] = find (ParamIDs::insGlitchSettings[s]);
        for (size_t s = 0; s < bus.lofi_.size(); ++s) bus.lofi_[s] = find (ParamIDs::insLofiSettings[s]);
        bus.sends_ = { find ("SendDelay"), find ("SendReverb") };
    }

    // THE RACK: each band's parameters, read every block.
    for (int band = 0; band < ParamIDs::eqNumBands; ++band)
        for (size_t i = 0; i < std::size (ParamIDs::eqBandSettings); ++i)
            rack->eqBandParameters[(size_t) band].values[i] = parameters.getRawParameterValue (ParamIDs::eqBand (band, ParamIDs::eqBandSettings[i]));
    for (int band = 0; band < ParamIDs::domNumBands; ++band)
        for (size_t i = 0; i < std::size (ParamIDs::domBandSettings); ++i)
            rack->dominionBandParameters[(size_t) band].values[i] = parameters.getRawParameterValue (ParamIDs::domBand (band, ParamIDs::domBandSettings[i]));
    for (int band = 0; band < ParamIDs::mbNumBands; ++band)
        for (size_t i = 0; i < std::size (ParamIDs::mbBandSettings); ++i)
            rack->multibandBandParameters[(size_t) band].values[i] = parameters.getRawParameterValue (ParamIDs::mbBand (band, ParamIDs::mbBandSettings[i]));

    // Every parameter's value by its ID in a hash table, for `param` (PR7-D 0.49.0's: APVTS's own lookup walks a
    // sorted map with a string compare per step).
    for (auto* p : juce::AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (auto* value = parameters.getRawParameterValue (ranged->getParameterID()))
                rawValues.emplace (std::string (ranged->getParameterID().toRawUTF8()), value);

    rigLibrary = std::make_unique<RigLibrary>();
    rack->setFolder (rigLibrary->getFolder());
    // MIDI LEARN: a new instance starts with the default links, if some were saved.
    if (midiLinksDefaultFile().existsAsFile())
        importMidiLinks (midiLinksDefaultFile());
    // A new PR7-O is the Init preset (every parameter at its default, no instrument).
    currentRig = { "Init", {}, "Init" };

    // Any change to a parameter that presets store marks the preset as modified, and finishing an edit to one records
    // an undo step.
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            if (! ParamIDs::notInRigs.contains (withId->paramID))
            {
                parameters.addParameterListener (withId->paramID, this);
                parameter->addListener (this);
            }

    // Housekeeping off the audio thread: record undo steps, let go of replaced instruments, MIDI learn.
    startTimerHz (30);

    // The starting point, so the first edit can be undone.
    history.record (takeSnapshot());
    checkpointWanted = false;
}

ParagonX1AudioProcessor::~ParagonX1AudioProcessor()
{
    // The instrument's loads: none may install after this.
    lifeToken.reset();
    ++loadGeneration;
    if (loaderPool != nullptr)
        loaderPool->removeAllJobs (true, 30000);
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
        {
            parameters.removeParameterListener (withId->paramID, this);
            parameter->removeListener (this);
        }
    stopTimer();
}

void ParagonX1AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedBlockSize = juce::jmax (1, samplesPerBlock);
    keyboardMidi.ensureSize (2048);
    keyboardState.reset();
    sampler.prepare (sampleRate);
    instrumentBuffer.setSize (2, preparedBlockSize);

    outputGain.reset (sampleRate, gainRampSeconds);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (ParamIDs::outputGain) + param (ParamIDs::patchLevel)));
    bypassAmount.reset (sampleRate, gainRampSeconds);
    bypassAmount.setCurrentAndTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);
    sendDelayGain.reset (sampleRate, gainRampSeconds);
    sendDelayGain.setCurrentAndTargetValue (param (ParamIDs::instSendDelay));
    sendReverbGain.reset (sampleRate, gainRampSeconds);
    sendReverbGain.setCurrentAndTargetValue (param (ParamIDs::instSendReverb));

    // THE RACK.
    rack->sampleRate = sampleRate;
    rack->parametricEq.prepare (sampleRate, preparedBlockSize);
    rack->dominionProcessor.prepare (sampleRate, preparedBlockSize);
    rack->compressor.prepare (sampleRate, preparedBlockSize);
    rack->multiband.prepare (sampleRate, preparedBlockSize);
    rack->compSidechain.setSize (2, preparedBlockSize);
    rack->limiter.prepare (sampleRate, preparedBlockSize);
    rack->eqInputTap.setSampleRate (sampleRate);
    rack->delay.prepare (sampleRate, preparedBlockSize);
    rack->reverb.prepare (sampleRate, preparedBlockSize);
    for (auto& block : rack->modulation)
        block.prepare (sampleRate, preparedBlockSize);
    for (auto& bus : rack->buses)
    {
        for (int c = 0; c < 2; ++c)
        {
            bus.drive[(size_t) c].prepare (sampleRate, preparedBlockSize);
            bus.glitch[(size_t) c].prepare (sampleRate, preparedBlockSize);
        }
        bus.modulation.prepare (sampleRate, preparedBlockSize);
        bus.loFi.prepare (sampleRate, preparedBlockSize);
        bus.buffer.setSize (2, preparedBlockSize);
    }
    rack->loFi.prepare (sampleRate, preparedBlockSize);
    for (auto& send : rack->sends)
        send.setSize (2, preparedBlockSize);
    rack->eqOutputTap.setSampleRate (sampleRate);
    updateLatency();
}

effects::MasterLimiter::Settings ParagonX1AudioProcessor::getLimiterSettings() const
{
    effects::MasterLimiter::Settings s;
    s.on = isOn (ParamIDs::limitOn);
    s.style = (effects::limiter::Style) juce::jlimit (0, ParamRanges::limitModes.size() - 1, juce::roundToInt (param (ParamIDs::limitMode)));
    s.gainDb = param (ParamIDs::limitGain);
    s.ceilingDb = param (ParamIDs::limitCeiling);
    s.lookaheadMs = param (ParamIDs::limitLookahead);
    s.attackMs = param (ParamIDs::limitAttack);
    s.releaseMs = param (ParamIDs::limitRelease);
    s.transientLink = param (ParamIDs::limitLinkTransient) / 100.0f;
    s.releaseLink = param (ParamIDs::limitLinkRelease) / 100.0f;
    s.truePeak = isOn (ParamIDs::limitTruePeak);
    s.oversampling = juce::roundToInt (param (ParamIDs::limitOversampling));
    s.dither = juce::roundToInt (param (ParamIDs::limitDither));
    s.shaping = juce::roundToInt (param (ParamIDs::limitShaping));
    s.unity = isOn (ParamIDs::limitUnity);
    s.delta = isOn (ParamIDs::limitDelta);
    s.dcFilter = isOn (ParamIDs::limitDcFilter);
    s.trimDb = param (ParamIDs::limitTrim);
    s.mix = param (ParamIDs::limitMix) / 100.0f;
    s.link = isOn (ParamIDs::limitLink);
    return s;
}

void ParagonX1AudioProcessor::updateLatency()
{
    const auto latency = mainLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

effects::multiband::Settings ParagonX1AudioProcessor::getMultibandSettings() const noexcept
{
    namespace mb = effects::multiband;
    mb::Settings settings;
    settings.lookahead = isOn (ParamIDs::mbLookahead);
    settings.mix = param (ParamIDs::mbMix) / 100.0f;
    settings.soloBand = rack->multibandSolo.load();
    for (size_t i = 0; i < settings.bands.size(); ++i)
    {
        const auto& v = rack->multibandBandParameters[i].values;   // in mbBandSettings' order
        auto& band = settings.bands[i];
        band.used = v[0]->load() > 0.5f;
        band.on = v[1]->load() > 0.5f;
        band.lowHz = v[2]->load();
        band.highHz = v[3]->load();
        band.slope = juce::jlimit (0, mb::numSlopes - 1, juce::roundToInt (v[4]->load()));
        band.thresholdDb = v[5]->load();
        band.rangeDb = v[6]->load();
        band.mode = (mb::Mode) juce::jlimit (0, 1, juce::roundToInt (v[7]->load()));
        band.ratio = v[8]->load();
        band.kneeDb = v[9]->load();
        band.attackMs = v[10]->load();
        band.releaseMs = v[11]->load();
        band.character = (effects::Compressor::Character) juce::jlimit (0, 2, juce::roundToInt (v[12]->load()));
        band.stereo = (mb::Stereo) juce::jlimit (0, 2, juce::roundToInt (v[13]->load()));
        band.link = v[14]->load() / 100.0f;
        band.outputDb = v[15]->load();
        band.lookaheadMs = v[16]->load();
    }
    return settings;
}

eq::ParametricEq::Settings ParagonX1AudioProcessor::getEqSettings() const noexcept
{
    eq::ParametricEq::Settings settings;
    settings.on = isOn (ParamIDs::eqOn);
    settings.outputDb = param (ParamIDs::eqOutput);
    settings.gainScale = param (ParamIDs::eqGainScale);
    settings.autoGain = isOn (ParamIDs::eqAutoGain);
    settings.soloBand = rack->eqSoloBand.load();
    for (size_t i = 0; i < settings.bands.size(); ++i)
    {
        const auto& values = rack->eqBandParameters[i].values;   // in eqBandSettings' order
        auto& band = settings.bands[i];
        band.used = values[0]->load() > 0.5f;
        band.on = values[1]->load() > 0.5f;
        band.shape = (eq::Shape) juce::roundToInt (values[2]->load());
        band.frequency = values[3]->load();
        band.gainDb = values[4]->load();
        band.q = values[5]->load();
        band.slope = juce::roundToInt (values[6]->load());
        band.placement = (eq::Placement) juce::roundToInt (values[7]->load());
        band.dynamicRangeDb = values[8]->load();
        band.autoThreshold = values[9]->load() > 0.5f;
        band.thresholdDb = values[10]->load();
        band.attack = values[11]->load();
        band.release = values[12]->load();
    }
    return settings;
}

dominion::Settings ParagonX1AudioProcessor::getDominionSettings() const noexcept
{
    dominion::Settings s;
    s.on = isOn (ParamIDs::domOn);
    s.mode = (dominion::Mode) juce::jlimit (0, 1, juce::roundToInt (param (ParamIDs::domMode)));
    s.depth = param (ParamIDs::domDepth);
    s.detail = param (ParamIDs::domDetail);
    s.attack = param (ParamIDs::domAttack);
    s.release = param (ParamIDs::domRelease);
    s.stereo = (dominion::StereoMode) juce::jlimit (0, 1, juce::roundToInt (param (ParamIDs::domStereo)));
    s.link = param (ParamIDs::domLink);
    s.focus = param (ParamIDs::domFocus);
    s.detailTiltLow = param (ParamIDs::domDetailLow);
    s.detailTiltHigh = param (ParamIDs::domDetailHigh);
    s.attackTiltLow = param (ParamIDs::domAttackLow);
    s.attackTiltHigh = param (ParamIDs::domAttackHigh);
    s.releaseTiltLow = param (ParamIDs::domReleaseLow);
    s.releaseTiltHigh = param (ParamIDs::domReleaseHigh);
    s.maxCutDb = param (ParamIDs::domMaxCut);
    s.wetTrimDb = param (ParamIDs::domWetTrim);
    s.mix = param (ParamIDs::domMix);
    s.outDb = param (ParamIDs::domOut);
    s.delta = isOn (ParamIDs::domDelta);
    for (size_t i = 0; i < s.bands.size(); ++i)
    {
        const auto& values = rack->dominionBandParameters[i].values;   // in domBandSettings' order
        auto& band = s.bands[i];
        band.used = values[0]->load() > 0.5f;
        band.on = values[1]->load() > 0.5f;
        band.shape = (dominion::Shape) juce::jlimit (0, dominion::shapeNames.size() - 1, juce::roundToInt (values[2]->load()));
        band.frequency = values[3]->load();
        band.depthDb = values[4]->load();
        band.q = values[5]->load();
        band.slope = juce::roundToInt (values[6]->load());
        band.focus = values[7]->load();
    }
    s.listenBand = rack->dominionListen.load();
    return s;
}

effects::Delay::Settings ParagonX1AudioProcessor::getDelaySettings() const noexcept
{
    effects::Delay::Settings s;
    s.on = isOn (ParamIDs::delayOn);
    s.timeMs = param (ParamIDs::delayTime);
    s.syncIndex = juce::jlimit (0, effects::Delay::syncNames.size() - 1, juce::roundToInt (param (ParamIDs::delaySync)));
    s.feedback = param (ParamIDs::delayFeedback);
    s.tone = param (ParamIDs::delayTone);
    s.mix = param (ParamIDs::delayMix);
    s.pingPong = isOn (ParamIDs::delayPingPong);
    s.type = (effects::Delay::Type) juce::jlimit (0, effects::Delay::typeNames.size() - 1, juce::roundToInt (param (ParamIDs::delayType)));
    s.duck = param (ParamIDs::delayDuck);
    s.returnOnly = true;
    return s;
}

effects::Reverb::Settings ParagonX1AudioProcessor::getReverbSettings() const noexcept
{
    effects::Reverb::Settings s;
    s.on = isOn (ParamIDs::reverbOn);
    s.type = (effects::Reverb::Type) juce::jlimit (0, effects::Reverb::typeNames.size() - 1, juce::roundToInt (param (ParamIDs::reverbType)));
    s.size = param (ParamIDs::reverbSize);
    s.tone = param (ParamIDs::reverbTone);
    s.mix = param (ParamIDs::reverbMix);
    s.preDelayMs = param (ParamIDs::reverbPreDelay);
    s.duck = param (ParamIDs::reverbDuck);
    s.returnOnly = true;
    return s;
}

void ParagonX1AudioProcessor::processSends (juce::dsp::AudioBlock<float> block)
{
    // DELAY on its send (giving back just its echoes), then REVERB on its own send plus DELAY > REVERB's share of those
    // echoes; both returns join the sound before MOD / LO-FI and the rack. Each does nothing once it's off and its tail
    // is gone.
    auto& r = *rack;
    const auto numSamples = (int) block.getNumSamples();
    juce::dsp::AudioBlock<float> delayReturn (r.sends[0].getArrayOfWritePointers(), 2, (size_t) numSamples);
    juce::dsp::AudioBlock<float> reverbReturn (r.sends[1].getArrayOfWritePointers(), 2, (size_t) numSamples);
    r.delay.process (delayReturn, getDelaySettings(), juce::jmax (1.0, hostBpm.load()));
    if (const auto feed = param (ParamIDs::delayToReverb); feed > 0.0f)
        for (int c = 0; c < 2; ++c)
            r.sends[1].addFrom (c, 0, r.sends[0], c, 0, numSamples, feed);
    r.reverb.process (reverbReturn, getReverbSettings());
    for (int s = 0; s < fx::numSends; ++s)
        r.returnPeak[(size_t) s] = juce::jmax (r.sends[(size_t) s].getMagnitude (0, 0, numSamples), r.sends[(size_t) s].getMagnitude (1, 0, numSamples));
    const auto numChannels = (int) block.getNumChannels();
    for (int s = 0; s < fx::numSends; ++s)
    {
        const auto& send = r.sends[(size_t) s];
        for (int c = 0; c < numChannels; ++c)
        {
            auto* out = block.getChannelPointer ((size_t) c);
            for (int i = 0; i < numSamples; ++i)
                out[i] += numChannels == 1 ? 0.5f * (send.getSample (0, i) + send.getSample (1, i)) : send.getSample (c, i);
        }
    }
}

effects::Modulation::Settings ParagonX1AudioProcessor::getModSettings (int which) const noexcept
{
    return modSettingsFor (which == 0 ? ParamIDs::modIds : ParamIDs::mod2Ids);
}

effects::Modulation::Settings ParagonX1AudioProcessor::modSettingsFor (const ParamIDs::ModIds& ids) const noexcept
{
    // As PR7-G's modSettingsFor, without its expression pedal and POSITION.
    effects::Modulation::Settings s;
    s.on = isOn (ids.on);
    s.type = (effects::Modulation::Type) juce::jlimit (0, effects::Modulation::typeNames.size() - 1, juce::roundToInt (param (ids.type)));
    // SYNC: one LFO cycle per note length at the song's tempo.
    s.rateHz = param (ids.rate);
    if (const auto sync = juce::roundToInt (param (ids.sync)); sync > 0 && sync < (int) std::size (effects::Delay::syncBeats))
        s.rateHz = juce::jlimit (0.02f, 40.0f, (float) (juce::jmax (1.0, hostBpm.load()) / 60.0) / effects::Delay::syncBeats[sync]);   // 1/32 at 300 BPM
    s.depth = param (ids.depth);
    s.mix = param (ids.mix);
    s.eq = param (ids.eq);
    s.stereoOutputs = isOn (ids.stereo);
    s.manual = param (ids.manual);
    s.resonance = param (ids.res);
    s.hadronBlock = isOn (ids.hadron);
    s.wave = param (ids.wave);
    return s;
}

effects::LoFi::Settings ParagonX1AudioProcessor::getLoFiSettings() const noexcept { return loFiSettingsFor (-1); }

effects::LoFi::Settings ParagonX1AudioProcessor::getInsertLoFiSettings (int bus) const noexcept { return loFiSettingsFor (bus); }

effects::LoFi::Settings ParagonX1AudioProcessor::loFiSettingsFor (int bus) const noexcept
{
    // The whole sound's lofi* (bus -1), or a bus's "ins<n>Lofi*": the same settings in the same order.
    const auto value = [this, bus] (const char* whole, int index)
    {
        return bus < 0 ? param (whole) : rack->buses[(size_t) bus].lofi_[(size_t) index]->load();
    };
    effects::LoFi::Settings s;
    s.on = value (ParamIDs::lofiOn, 0) > 0.5f;
    s.type = (effects::LoFi::Type) juce::jlimit (0, effects::LoFi::typeNames.size() - 1, juce::roundToInt (value (ParamIDs::lofiType, 1)));
    s.amount = value (ParamIDs::lofiAmount, 2);
    s.noise = value (ParamIDs::lofiNoise, 3);
    s.mix = value (ParamIDs::lofiMix, 4);
    s.levelDb = value (ParamIDs::lofiLevel, 5);
    return s;
}

effects::Drive::Settings ParagonX1AudioProcessor::getInsertDriveSettings (int bus) const noexcept
{
    const auto& v = rack->buses[(size_t) juce::jlimit (0, fx::numInsertBuses - 1, bus)].drive_;   // insDriveSettings' order
    const auto value = [&v] (int index) { return v[(size_t) index]->load(); };
    effects::Drive::Settings s;
    s.on = value (0) > 0.5f;
    s.model = (effects::Drive::Model) juce::jlimit (0, effects::Drive::modelNames.size() - 1, juce::roundToInt (value (1)));
    s.drive = value (2);
    s.tone = value (3);
    s.levelDb = value (4);
    s.attack = juce::jlimit (0, effects::Drive::skylineAttackNames.size() - 1, juce::roundToInt (value (5)));
    s.gate = value (6);
    s.high = value (7);
    s.low = value (8);
    s.middle = value (9);
    s.midFreq = value (10);
    return s;
}

effects::Glitch::Settings ParagonX1AudioProcessor::getInsertGlitchSettings (int bus) const noexcept
{
    const auto& v = rack->buses[(size_t) juce::jlimit (0, fx::numInsertBuses - 1, bus)].glitch_;   // insGlitchSettings' order
    const auto value = [&v] (int index) { return v[(size_t) index]->load(); };
    effects::Glitch::Settings s;
    s.on = value (0) > 0.5f;
    s.mode = (effects::Glitch::Mode) juce::jlimit (0, effects::Glitch::modeNames.size() - 1, juce::roundToInt (value (1)));
    s.sync = value (2) > 0.5f;
    s.lengthMs = value (3);
    s.division = juce::jlimit (0, effects::Glitch::syncNames.size() - 1, juce::roundToInt (value (4)));
    s.density = value (5);
    s.pitch = value (6);
    s.chaos = value (7);
    s.mix = value (8);
    return s;
}

std::array<float, fx::numSends> ParagonX1AudioProcessor::getInsertSends (int bus) const noexcept
{
    const auto& v = rack->buses[(size_t) juce::jlimit (0, fx::numInsertBuses - 1, bus)].sends_;
    return { v[0]->load(), v[1]->load() };
}

void ParagonX1AudioProcessor::processInsertBuses (juce::dsp::AudioBlock<float> block, bool sendsReady)
{
    // The bus the instrument plays into (INSERT BUS): DRIVE and GLITCH (one a side: PR7-G's are mono), MOD, LO-FI, as on PR7-G's
    // chain (GLITCH POST, after the drive); then into the mix, and into the sends at the bus's amounts.
    const auto numSamples = (int) block.getNumSamples();
    const auto bpm = juce::jmax (1.0, hostBpm.load());
    for (int b = 0; b < fx::numInsertBuses; ++b)
    {
        auto& bus = rack->buses[(size_t) b];
        if (! bus.used)
        {
            bus.peak = 0.0f;
            continue;
        }
        auto busBlock = juce::dsp::AudioBlock<float> (bus.buffer).getSubBlock (0, (size_t) numSamples);
        const auto drive = getInsertDriveSettings (b);
        const auto glitch = getInsertGlitchSettings (b);
        // After the DRIVE, a DC blocker (5 Hz): RABID's circuit can leave its coupling capacitor charged, a steady
        // offset after loud playing. On PR7-G the amp after the pedal blocks it; here nothing would. Only while the
        // drive is on; switched off, what it was taking off fades out over about 50 ms, then the bus is untouched.
        const auto dcCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 5.0f / (float) getSampleRate());
        const auto dcFade = std::exp (-1.0f / (0.01f * (float) getSampleRate()));
        for (size_t c = 0; c < 2; ++c)
        {
            bus.drive[c].process (busBlock.getSingleChannelBlock (c), drive);
            auto& dc = bus.driveDc[c];
            auto* x = busBlock.getChannelPointer (c);
            if (drive.on)
                for (int i = 0; i < numSamples; ++i)
                {
                    dc += (x[i] - dc) * dcCoefficient;
                    x[i] -= dc;
                }
            else if (dc != 0.0f)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    dc *= dcFade;
                    x[i] -= dc;
                }
                if (std::abs (dc) < 1.0e-9f)
                    dc = 0.0f;
            }
            bus.glitch[c].process (busBlock.getSingleChannelBlock (c), glitch, bpm, hostPpq);
        }
        bus.modulation.process (busBlock, getInsertModSettings (b));
        bus.loFi.process (busBlock, getInsertLoFiSettings (b));
        bus.peak = juce::jmax (bus.buffer.getMagnitude (0, 0, numSamples), bus.buffer.getMagnitude (1, 0, numSamples));

        // Into the mix.
        const auto numChannels = (int) block.getNumChannels();
        for (int c = 0; c < numChannels; ++c)
        {
            auto* out = block.getChannelPointer ((size_t) c);
            if (numChannels == 1)
                for (int i = 0; i < numSamples; ++i)
                    out[i] += 0.5f * (bus.buffer.getSample (0, i) + bus.buffer.getSample (1, i));
            else
                juce::FloatVectorOperations::add (out, bus.buffer.getReadPointer (c), numSamples);
        }
        if (sendsReady)
        {
            const auto sends = getInsertSends (b);
            const auto on = std::array<bool, fx::numSends> { isOn (ParamIDs::delayOn), isOn (ParamIDs::reverbOn) };
            for (int s = 0; s < fx::numSends; ++s)
                if (on[(size_t) s] && sends[(size_t) s] > 0.0f)
                    for (int c = 0; c < 2; ++c)
                        rack->sends[(size_t) s].addFrom (c, 0, bus.buffer, c, 0, numSamples, sends[(size_t) s]);
        }
    }
}

void ParagonX1AudioProcessor::processModFx (juce::dsp::AudioBlock<float> block)
{
    // MOD, then MOD 2, then LO-FI, as on PR7-G (where LO-FI comes after the reverb too, so the echoes go "through
    // the phone" with the rest). Each passes the sound untouched while off.
    rack->modulation[0].process (block, getModSettings (0));
    rack->modulation[1].process (block, getModSettings (1));
    const auto loFi = getLoFiSettings();
    rack->loFi.process (block, loFi);
    auto peak = 0.0f;
    if (loFi.on)
        for (size_t c = 0; c < block.getNumChannels(); ++c)
            peak = juce::jmax (peak, juce::FloatVectorOperations::findMaximum (block.getChannelPointer (c), (int) block.getNumSamples()),
                               -juce::FloatVectorOperations::findMinimum (block.getChannelPointer (c), (int) block.getNumSamples()));
    rack->loFiPeak = peak;
}

void ParagonX1AudioProcessor::processRack (juce::dsp::AudioBlock<float> block)
{
    // As PR7-E 0.18.1's processChunk, from its EQ to its COMP: ECHELON, with what goes in and comes out kept for its
    // analyser (and EQ Match); DOMINION after it (so it hears what the EQ made); CONSTRICTOR's bands, then its full band
    // (PR7-C's compressor, which takes whole buffers and the same audio to listen to). Each does nothing while off.
    auto& r = *rack;
    const auto numChannels = (int) block.getNumChannels();
    const auto numSamples = (int) block.getNumSamples();
    if (r.compSidechain.getNumSamples() < numSamples)
        return;   // not prepared (prepareToPlay sizes everything): pass the sound through rather than read past a buffer
    const auto writeTap = [&] (InputTap& tap)
    {
        if (numChannels > 1)
            tap.write (block.getChannelPointer (0), block.getChannelPointer (1), numSamples);
        else
            tap.write (block.getChannelPointer (0), numSamples);
    };
    writeTap (r.eqInputTap);
    r.parametricEq.process (block, getEqSettings());
    writeTap (r.eqOutputTap);

    r.dominionProcessor.process (block, getDominionSettings());

    r.multiband.process (block, getMultibandSettings(), isOn (ParamIDs::compOn));
    float* channels[effects::maxChannels] {};
    for (int channel = 0; channel < numChannels; ++channel)
    {
        channels[channel] = block.getChannelPointer ((size_t) channel);
        r.compSidechain.copyFrom (channel, 0, channels[channel], numSamples);
    }
    juce::AudioBuffer<float> audio (channels, numChannels, numSamples);
    const juce::AudioBuffer<float> detector (r.compSidechain.getArrayOfWritePointers(), numChannels, numSamples);
    r.compressor.process (audio, detector, numSamples,
                          { isOn (ParamIDs::compOn) && isOn (ParamIDs::compWide), param (ParamIDs::compThreshold), param (ParamIDs::compRatio),
                            param (ParamIDs::compAttack), param (ParamIDs::compRelease), isOn (ParamIDs::compAuto),
                            param (ParamIDs::compMakeUp),
                            (effects::Compressor::Character) juce::jlimit (0, 2, juce::roundToInt (param (ParamIDs::compCharacter))),
                            isOn (ParamIDs::compLink) });
}

juce::AudioProcessorParameter* ParagonX1AudioProcessor::getBypassParameter() const
{
    return parameters.getParameter (ParamIDs::bypass);
}

float ParagonX1AudioProcessor::param (const char* id) const
{
    // Looked up by ID each block: 0.49.0 through a hash table made in the constructor (no allocation: the key is
    // only viewed); APVTS's own (sorted map) lookup for any ID not in it.
    if (const auto it = rawValues.find (std::string_view (id)); it != rawValues.end())
        return it->second->load();
    return parameters.getRawParameterValue (id)->load();
}

int ParagonX1AudioProcessor::mainLatencySamples() const noexcept
{
    // CONSTRICTOR's LOOK-AHEAD, and the LIMITER's (with its oversampling and true peak): the sampler itself adds none.
    return (isOn (ParamIDs::mbLookahead) ? (int) std::lround (effects::multiband::lookaheadSeconds * rack->sampleRate) : 0)
           + rack->limiter.getLatencySamples (getLimiterSettings());
}

bool ParagonX1AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // An instrument: no audio in; out mono or stereo.
    const auto output = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled()
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

sampler::Sampler::Settings ParagonX1AudioProcessor::getSamplerSettings() const noexcept
{
    sampler::Sampler::Settings s;
    const auto levelDb = param (ParamIDs::instLevel);
    s.gain = levelDb <= -59.95f ? 0.0f : juce::Decibels::decibelsToGain (levelDb);
    s.pan = param (ParamIDs::instPan);
    s.transpose = param (ParamIDs::instTranspose);
    s.tuneCents = param (ParamIDs::instTune);
    s.releaseScale = param (ParamIDs::instRelease);
    s.polyphony = juce::roundToInt (param (ParamIDs::instVoices));
    s.channel = juce::roundToInt (param (ParamIDs::midiChannel));
    return s;
}

void ParagonX1AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0 || numSamples == 0)
        return;

    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                hostBpm = *bpm;
            const auto ppq = position->getPpqPosition();
            hostPpq = position->getIsPlaying() && ppq.hasValue() ? *ppq : -1.0;
        }

    // MIDI LEARN: the last controller moved (for learning), and the knobs linked to controllers.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (! message.isController())
            continue;
        const auto cc = message.getControllerNumber();
        if (cc < 0 || cc >= 120)
            continue;   // 120-127: channel mode messages (all notes off...)
        const auto value = (float) message.getControllerValue() / 127.0f;
        lastController = cc;
        lastControllerValue = value;
        ++controllerCount;
        if (auto* target = controllerTargets[(size_t) cc].load())
            target->setValueNotifyingHost (value);
    }

    // The page's keyboard joins the DAW's notes.
    keyboardMidi.clear();
    keyboardMidi.addEvents (midi, 0, numSamples, 0);
    keyboardState.processNextMidiBuffer (keyboardMidi, 0, numSamples, true);

    // Some hosts occasionally send more samples than they promised in prepareToPlay; work through an oversized block
    // in promised-size pieces.
    for (int start = 0; start < numSamples; start += preparedBlockSize)
        processChunk (buffer, keyboardMidi, start, juce::jmin (preparedBlockSize, numSamples - start));
    midi.clear();   // an instrument: nothing goes on down the MIDI chain
}

void ParagonX1AudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, int start, int numSamples)
{
    const auto numChannels = juce::jmin (buffer.getNumChannels(), 2);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, start, numSamples);
    if (instrumentBuffer.getNumSamples() < numSamples)
        return;   // not prepared

    // THE INSTRUMENT, into its own buffer first.
    instrumentBuffer.clear (0, numSamples);
    auto* left = instrumentBuffer.getWritePointer (0);
    auto* right = instrumentBuffer.getWritePointer (1);
    sampler.render (programForAudio.load(), left, right, numSamples, midi, start, getSamplerSettings());

    // Where it goes: an INSERT BUS (A to D: its effects, then its own sends), or straight into the mix and its own
    // sends (each only while its effect is on, as on PR7-D).
    const auto sendsReady = rack->sends[0].getNumSamples() >= numSamples;
    const auto insertBus = juce::roundToInt (param (ParamIDs::instInsert)) - 1;
    for (int b = 0; b < fx::numInsertBuses; ++b)
        rack->buses[(size_t) b].used = b == insertBus;
    if (sendsReady)
    {
        for (auto& send : rack->sends)
            send.clear (0, numSamples);
        for (auto& bus : rack->buses)
            if (bus.used)
                bus.buffer.clear (0, numSamples);
    }
    sendDelayGain.setTargetValue (isOn (ParamIDs::delayOn) ? param (ParamIDs::instSendDelay) : 0.0f);
    sendReverbGain.setTargetValue (isOn (ParamIDs::reverbOn) ? param (ParamIDs::instSendReverb) : 0.0f);
    if (insertBus >= 0 && insertBus < fx::numInsertBuses && sendsReady)
    {
        auto& bus = rack->buses[(size_t) insertBus];
        bus.buffer.addFrom (0, 0, left, numSamples);
        bus.buffer.addFrom (1, 0, right, numSamples);
        sendDelayGain.skip (numSamples);
        sendReverbGain.skip (numSamples);
    }
    else
    {
        if (numChannels == 1)
            for (int i = 0; i < numSamples; ++i)
                buffer.getWritePointer (0, start)[i] = 0.5f * (left[i] + right[i]);
        else
        {
            buffer.copyFrom (0, start, left, numSamples);
            buffer.copyFrom (1, start, right, numSamples);
        }
        if (sendsReady)
        {
            auto* delayLeft = rack->sends[0].getWritePointer (0);
            auto* delayRight = rack->sends[0].getWritePointer (1);
            auto* reverbLeft = rack->sends[1].getWritePointer (0);
            auto* reverbRight = rack->sends[1].getWritePointer (1);
            for (int i = 0; i < numSamples; ++i)
            {
                const auto d = sendDelayGain.getNextValue();
                const auto r = sendReverbGain.getNextValue();
                delayLeft[i] = left[i] * d;
                delayRight[i] = right[i] * d;
                reverbLeft[i] = left[i] * r;
                reverbRight[i] = right[i] * r;
            }
        }
    }

    // The insert bus into the mix and the sends; the sends' returns; MOD, MOD 2 and LO-FI; THE RACK: ECHELON,
    // DOMINION, CONSTRICTOR; then the preset's own level and OUTPUT; then the LIMITER, last, so nothing after it can
    // push a peak past its ceiling; then BYPASS (to silence: there's nothing "clean" to pass through on an instrument).
    const auto block = juce::dsp::AudioBlock<float> (buffer).getSubBlock ((size_t) start, (size_t) numSamples)
                                                            .getSubsetChannelBlock (0, (size_t) numChannels);
    if (sendsReady)
    {
        processInsertBuses (block, true);
        processSends (block);
        processModFx (block);   // after the returns, so the echoes and reverb are part of what they shape
    }
    processRack (block);
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (param (ParamIDs::outputGain) + param (ParamIDs::patchLevel)));
    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = outputGain.getNextValue();
        for (int channel = 0; channel < numChannels; ++channel)
            buffer.getWritePointer (channel, start)[i] *= gain;
    }
    if (rack->compSidechain.getNumSamples() >= numSamples)
        rack->limiter.process (block, getLimiterSettings());
    bypassAmount.setTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);
    if (bypassAmount.isSmoothing() || bypassAmount.getTargetValue() > 0.5f)
        for (int i = 0; i < numSamples; ++i)
        {
            const auto gain = 1.0f - bypassAmount.getNextValue();
            for (int channel = 0; channel < numChannels; ++channel)
                buffer.getWritePointer (channel, start)[i] *= gain;
        }

    for (int channel = 0; channel < numChannels; ++channel)
        outputMeter.pushSamples (buffer.getReadPointer (channel, start), numSamples);
    // Past the first channels (a host asking for more than stereo): silent.
}

//==============================================================================
void ParagonX1AudioProcessor::timerCallback()
{
    finishMidiLearn();
    retireOldData();
    updateLatency();
    rack->eqMatch.update (rack->eqInputTap);   // EQ Match listens with its window closed too
    recordCheckpointIfReady();
    applyDefaultRigIfPending();
    notenames::middleCOctave = notenames::octaveForChoice (juce::roundToInt (param (ParamIDs::noteNames)));
}

void ParagonX1AudioProcessor::retireOldData()
{
    // A replaced instrument goes once the audio thread has moved on from it (its voices fade in 20 ms) and 2 s have
    // passed. If the audio thread has stopped, it's kept: a voice may still point into it when blocks start again.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto* playing = programForAudio.load();
    std::erase_if (retiredPrograms, [now, playing] (const auto& retired)
    {
        return retired.first.get() != playing && now - retired.second > retiredMs;
    });
}

//==============================================================================
// THE INSTRUMENT
void ParagonX1AudioProcessor::loadInstrument (const juce::File& file)
{
    startLoading (file.getFullPathName(), true);
}

void ParagonX1AudioProcessor::clearInstrument()
{
    ++loadGeneration;   // a load still running puts nothing in
    {
        const std::scoped_lock lock (loadedLock);
        loadProblem = {};
    }
    installProgram (nullptr, {});
    markModified();
    requestCheckpoint();
}

void ParagonX1AudioProcessor::setProgram (std::shared_ptr<const sampler::Program> newProgram, const juce::String& file)
{
    ++loadGeneration;
    installProgram (std::move (newProgram), file);
}

void ParagonX1AudioProcessor::installProgram (std::shared_ptr<const sampler::Program> newProgram, const juce::String& file)
{
    if (program != nullptr && program != newProgram)
        retiredPrograms.emplace_back (program, juce::Time::getMillisecondCounterHiRes());
    program = std::move (newProgram);
    programForAudio = program.get();
    instrumentFile = file;
    instrumentBroadcaster.sendChangeMessage();
}

void ParagonX1AudioProcessor::startLoading (const juce::String& file, bool undoStep)
{
    const auto generation = ++loadGeneration;
    instrumentFile = file;   // what's loading counts as the preset's at once (saved, undone), the audio follows
    ++pendingLoads;
    loadProgress = 0.0;
    std::weak_ptr<int> alive = lifeToken;
    if (loaderPool == nullptr)
        loaderPool = std::make_unique<juce::ThreadPool> (1);
    loaderPool->addJob ([this, file, generation, undoStep, alive]
    {
        Loaded result { nullptr, file, generation, undoStep };
        juce::String problem;
        if (! alive.expired() && loadGeneration.load() == generation)
        {
            const juce::File source (file);
            std::shared_ptr<sampler::Program> made;
            if (! source.existsAsFile())
                problem = "not found: " + file;
            else if (source.hasFileExtension ("sfz"))
                made = sampler::sfz::load (source, [this, generation, alive] (double done)
                {
                    loadProgress = done;
                    return ! alive.expired() && loadGeneration.load() == generation;
                });
            else
                made = sampler::programFrom (sampler::sampleFiles::load (source), 60);   // a single sample, its root C3 / C4
            if (made != nullptr && made->regions.empty())
                problem = "nothing playable in " + source.getFileName()
                        + (made->problems.isEmpty() ? juce::String() : " (" + made->problems[0] + ")");
            else if (made == nullptr && problem.isEmpty())
                problem = "couldn't read " + source.getFileName();
            if (made != nullptr && ! made->regions.empty())
                result.program = std::move (made);
        }
        {
            const std::scoped_lock lock (loadedLock);
            if (loadGeneration.load() == generation)
                loadProblem = problem;
            loaded.push_back (std::move (result));
        }
        --pendingLoads;
        juce::MessageManager::callAsync ([this, alive]
        {
            if (! alive.expired())
                installLoaded();
        });
    });
    instrumentBroadcaster.sendChangeMessage();
}

void ParagonX1AudioProcessor::installLoaded()
{
    std::vector<Loaded> ready;
    {
        const std::scoped_lock lock (loadedLock);
        ready.swap (loaded);
    }
    // Only the latest load counts (a later one, or taking it away, makes the earlier ones stale).
    for (auto& item : ready)
        if (item.generation == loadGeneration.load())
        {
            installProgram (item.program, item.file);   // a failed load keeps its file (saved with the preset) and no sound
            if (item.undoStep)
            {
                markModified();
                requestCheckpoint();
            }
        }
}

juce::String ParagonX1AudioProcessor::getLoadProblem() const
{
    const std::scoped_lock lock (loadedLock);
    return loadProblem;
}

bool ParagonX1AudioProcessor::waitForInstrument (int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (pendingLoads.load() > 0 && juce::Time::getMillisecondCounter() < until)
        juce::Thread::sleep (5);
    installLoaded();
    return pendingLoads.load() == 0;
}

void ParagonX1AudioProcessor::parameterGestureChanged (int, bool gestureIsStarting)
{
    if (gestureIsStarting)
    {
        ++activeGestures;
    }
    else
    {
        // Never below zero, even if a host sends an unmatched "end".
        auto active = activeGestures.load();
        while (active > 0 && ! activeGestures.compare_exchange_weak (active, active - 1)) {}
        requestCheckpoint();
    }
}

ParagonX1AudioProcessor::Snapshot ParagonX1AudioProcessor::takeSnapshot() const
{
    return { captureRig(), currentRig, rigModified.load() };
}

void ParagonX1AudioProcessor::recordCheckpointIfReady()
{
    // Wait until the edit is really finished: no knob still held.
    if (! checkpointWanted.load() || activeGestures.load() > 0)
        return;

    checkpointWanted = false;
    history.record (takeSnapshot());
}

void ParagonX1AudioProcessor::restoreSnapshot (const Snapshot& snapshot)
{
    applyRig (snapshot.rig);
    currentRig = snapshot.current;
    rigModified = snapshot.modified;
    rigBroadcaster.sendChangeMessage();
}

void ParagonX1AudioProcessor::undo()
{
    // Record anything not yet recorded first, so undo takes back the latest edit rather than the one before it.
    recordCheckpointIfReady();
    if (const auto snapshot = history.undo())
        restoreSnapshot (*snapshot);
}

void ParagonX1AudioProcessor::redo()
{
    recordCheckpointIfReady();
    if (const auto snapshot = history.redo())
        restoreSnapshot (*snapshot);
}

//==============================================================================
void ParagonX1AudioProcessor::markModified() noexcept
{
    if (! applyingRig.load())
        rigModified = true;
}

void ParagonX1AudioProcessor::parameterChanged (const juce::String&, float)
{
    // Can be called on the audio thread (automation), so only touch the atomic.
    markModified();
}

void ParagonX1AudioProcessor::useRigFolder (const juce::File& folder)
{
    rigLibrary = std::make_unique<RigLibrary> (folder);
    rack->setFolder (folder);
    rigBroadcaster.sendChangeMessage();
}

Rig ParagonX1AudioProcessor::captureRig() const
{
    Rig rig;
    rig.name = currentRig.name;
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (! ParamIDs::notInRigs.contains (ranged->paramID))
                rig.parameters[ranged->paramID] = ranged->convertFrom0to1 (ranged->getValue());
    rig.instrument = instrumentFile;
    return rig;
}

void ParagonX1AudioProcessor::applyRig (const Rig& rig)
{
    applyingRig = true;

    for (auto* parameter : AudioProcessor::getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (ranged == nullptr || ParamIDs::notInRigs.contains (ranged->paramID))
            continue;

        // Anything the preset doesn't mention goes back to its default.
        const auto found = rig.parameters.find (ranged->paramID);
        const auto normalised = found != rig.parameters.end()
                                  ? ranged->convertTo0to1 (ranged->getNormalisableRange().snapToLegalValue (found->second))
                                  : ranged->getDefaultValue();
        ranged->setValueNotifyingHost (normalised);
    }

    // THE INSTRUMENT: the preset's, loaded again (the one it already has kept as it is).
    if (rig.instrument != instrumentFile)
    {
        if (rig.instrument.isEmpty())
            installProgram (nullptr, {});
        else
            startLoading (rig.instrument, false);
    }

    applyingRig = false;
    rigModified = false;
}

void ParagonX1AudioProcessor::loadRig (const RigLibrary::Entry& entry)
{
    // Copy first: applying parameters can't invalidate it, but be safe.
    const auto rig = entry.rig;
    currentRig = { rig.name, entry.file, entry.isFactory ? rig.name : juce::String() };
    applyRig (rig);
    requestCheckpoint();
    rigBroadcaster.sendChangeMessage();
}

juce::File ParagonX1AudioProcessor::defaultRigFile() const { return rigLibrary->getFolder().getChildFile ("Default Preset.xml"); }

void ParagonX1AudioProcessor::setDefaultRig (const RigLibrary::Entry* entry)
{
    if (entry == nullptr)
    {
        defaultRigFile().deleteFile();
        return;
    }
    juce::XmlElement xml ("DefaultPreset");
    if (entry->isFactory)
        xml.setAttribute ("factory", entry->rig.name);
    else
        xml.setAttribute ("file", entry->file.getFullPathName());
    rigLibrary->getFolder().createDirectory();
    xml.writeTo (defaultRigFile());
}

const RigLibrary::Entry* ParagonX1AudioProcessor::findDefaultRigEntry() const
{
    const auto xml = juce::XmlDocument::parse (defaultRigFile());
    if (xml == nullptr || ! xml->hasTagName ("DefaultPreset"))
        return nullptr;
    if (xml->hasAttribute ("factory"))
        return rigLibrary->findFactory (xml->getStringAttribute ("factory"));
    const juce::File file (xml->getStringAttribute ("file"));
    if (const auto* entry = rigLibrary->find (file))
        return entry;
    // (a preset renamed or moved in the folder since: the same name)
    return nullptr;
}

bool ParagonX1AudioProcessor::isDefaultRig (const RigLibrary::Entry& entry) const
{
    const auto* chosen = findDefaultRigEntry();
    return chosen != nullptr && chosen->isFactory == entry.isFactory
        && (entry.isFactory ? chosen->rig.name == entry.rig.name : chosen->file == entry.file);
}

bool ParagonX1AudioProcessor::applyDefaultRigIfPending()
{
    if (! defaultRigPending)
        return false;
    defaultRigPending = false;
    const auto* entry = findDefaultRigEntry();
    if (entry == nullptr)
        return false;
    loadRig (*entry);
    // It's where this instance starts: no undo step back to Init, not "modified".
    rigModified = false;
    history.clear();
    history.record (takeSnapshot());
    checkpointWanted = false;
    rigBroadcaster.sendChangeMessage();
    return true;
}

const RigLibrary::Entry* ParagonX1AudioProcessor::findCurrentRigEntry() const
{
    if (currentRig.isUserRig())
        return rigLibrary->find (currentRig.file);
    if (currentRig.factoryName.isNotEmpty())
        return rigLibrary->findFactory (currentRig.factoryName);
    return nullptr;
}

bool ParagonX1AudioProcessor::saveCurrentRig()
{
    const auto* entry = findCurrentRigEntry();
    if (entry == nullptr || entry->isFactory)
        return false;

    // Keep how it's filed; replace the sound.
    auto rig = captureRig();
    rig.name = entry->rig.name;
    rig.bank = entry->rig.bank;
    rig.number = entry->rig.number;
    rig.tags = entry->rig.tags;
    rig.favourite = entry->rig.favourite;
    rig.notes = entry->rig.notes;

    const auto file = entry->file;
    if (! rigLibrary->overwrite (file, rig))
        return false;

    rigModified = false;
    history.replaceCurrent (takeSnapshot());
    rigBroadcaster.sendChangeMessage();
    return true;
}

bool ParagonX1AudioProcessor::saveCurrentRigAs (const juce::String& name)
{
    auto rig = captureRig();
    rig.name = name;

    // File it next to the preset it came from, in the first free slot, with its tags and notes (not Init's: a sound
    // made from the blank page isn't "a blank page").
    if (const auto* entry = findCurrentRigEntry(); entry != nullptr && ! (entry->isFactory && entry->rig.name == "Init"))
    {
        rig.bank = entry->rig.bank;
        rig.tags = entry->rig.tags;
        rig.notes = entry->rig.notes;
    }
    rig.number = rigLibrary->nextFreeNumber (rig.bank);

    const auto file = rigLibrary->saveAsNew (rig);
    if (file == juce::File())
        return false;

    const auto* saved = rigLibrary->find (file);
    currentRig = { saved != nullptr ? saved->rig.name : name, file, {} };
    rigModified = false;
    // Saving isn't an edit to undo, but undo shouldn't bring back the old name either.
    history.replaceCurrent (takeSnapshot());
    rigBroadcaster.sendChangeMessage();
    return true;
}

void ParagonX1AudioProcessor::stepRig (int direction)
{
    if (const auto* next = rigLibrary->step (findCurrentRigEntry(), direction))
        loadRig (*next);
}

bool ParagonX1AudioProcessor::updateRigDetails (const RigLibrary::Entry& entry, const Rig& newDetails)
{
    if (entry.isFactory)
        return false;

    auto rig = entry.rig;
    rig.name = newDetails.name.trim().isEmpty() ? rig.name : newDetails.name.trim();
    rig.bank = newDetails.bank;
    rig.number = newDetails.number;
    rig.tags = newDetails.tags;

    const auto file = entry.file;
    const auto isCurrent = currentRig.file == file;
    if (! rigLibrary->overwrite (file, rig))
        return false;

    if (isCurrent)
    {
        currentRig.name = rig.name;
        history.replaceCurrent (takeSnapshot());
    }
    rigBroadcaster.sendChangeMessage();
    return true;
}

bool ParagonX1AudioProcessor::deleteRig (const RigLibrary::Entry& entry)
{
    if (entry.isFactory)
        return false;

    const auto file = entry.file;
    if (! rigLibrary->remove (file))
        return false;

    // The sound is still loaded; it just isn't saved anywhere any more.
    if (currentRig.file == file)
    {
        currentRig.file = juce::File();
        rigModified = true;
        history.replaceCurrent (takeSnapshot());
    }
    rigBroadcaster.sendChangeMessage();
    return true;
}

//==============================================================================
juce::AudioProcessorEditor* ParagonX1AudioProcessor::createEditor()
{
    return new ParagonX1AudioProcessorEditor (*this);
}

void ParagonX1AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (parameterVersionKey, parameterVersion, nullptr);
    state.setProperty (rigNameKey, currentRig.name, nullptr);
    state.setProperty (rigFileKey, currentRig.file.getFullPathName(), nullptr);
    state.setProperty (rigFactoryKey, currentRig.factoryName, nullptr);
    state.setProperty (rigModifiedKey, rigModified.load(), nullptr);
    if (instrumentFile.isNotEmpty())
        state.appendChild (juce::ValueTree (instrumentKey, { { "file", instrumentFile } }), nullptr);
    if (! midiLinks.isEmpty())
        state.appendChild (juce::ValueTree::fromXml (*midiLinks.toXml()), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ParagonX1AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    defaultRigPending = false;   // the DAW's saved state, not the default preset
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    juce::String instrument;
    if (auto saved = state.getChildWithName (instrumentKey); saved.isValid())
    {
        instrument = saved["file"].toString();
        state.removeChild (saved, nullptr);
    }
    // MIDI LEARN's links: the project's, if it has any (else the ones this instance has: its default).
    if (auto saved = state.getChildWithName (midiLinksKey); saved.isValid())
    {
        if (const auto links = saved.createXml())
            setMidiLinks (MidiLinks::fromXml (*links));
        state.removeChild (saved, nullptr);
    }
    parameters.replaceState (state);
    if (instrument != instrumentFile || (instrument.isNotEmpty() && program == nullptr && ! isLoadingInstrument()))
    {
        if (instrument.isEmpty())
            setProgram (nullptr);
        else
            startLoading (instrument, false);
    }

    // Restoring the project changed parameters, which marked the preset as modified; put back what was saved instead.
    const auto rigFile = state[rigFileKey].toString();
    currentRig = { state.getProperty (rigNameKey, "Init").toString(),
                   rigFile.isNotEmpty() ? juce::File (rigFile) : juce::File(),
                   state[rigFactoryKey].toString() };
    rigModified = (bool) state.getProperty (rigModifiedKey, false);

    // A different project or preset: start the undo history afresh from it.
    history.clear();
    requestCheckpoint();
    rigBroadcaster.sendChangeMessage();
}

// JUCE calls this to create new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParagonX1AudioProcessor();
}

//==============================================================================
// MIDI LEARN
void ParagonX1AudioProcessor::setMidiLinks (const MidiLinks& links)
{
    midiLinks = links;
    std::array<juce::RangedAudioParameter*, 128> targets {};
    for (const auto& l : midiLinks.links)
        if (auto* p = parameters.getParameter (l.parameterId))
            targets[(size_t) l.controller] = p;
    for (size_t cc = 0; cc < targets.size(); ++cc)
        controllerTargets[cc] = targets[cc];
}

void ParagonX1AudioProcessor::finishMidiLearn()
{
    // A controller moved since a knob asked to learn: link them.
    if (learning.isNotEmpty() && controllerCount.load() != learnFromCount && lastController.load() >= 0)
    {
        auto links = midiLinks;
        links.link (lastController.load(), learning);
        learning = {};
        setMidiLinks (links);
    }
}

void ParagonX1AudioProcessor::startMidiLearn (const juce::String& parameterId)
{
    learning = parameterId;
    learnFromCount = controllerCount.load();
}

juce::File ParagonX1AudioProcessor::midiLinksDefaultFile() const
{
    return rigLibrary->getFolder().getChildFile ("MIDI Links.xml");
}

bool ParagonX1AudioProcessor::saveMidiLinksAsDefault() const
{
    return exportMidiLinks (midiLinksDefaultFile());
}

bool ParagonX1AudioProcessor::exportMidiLinks (const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    return midiLinks.toXml()->writeTo (file);
}

bool ParagonX1AudioProcessor::importMidiLinks (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("MidiLinks"))
        return false;
    setMidiLinks (MidiLinks::fromXml (*xml));
    return true;
}

void ParagonX1AudioProcessor::controllerForTest (int controller, float value)
{
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, controller, juce::roundToInt (value * 127.0f)), 0);
    juce::AudioBuffer<float> buffer (2, 64);
    buffer.clear();
    processBlock (buffer, midi);
}

//==============================================================================
// THE SONG TRACK (0.8.0)
