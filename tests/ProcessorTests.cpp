// The plugin as a DAW sees it: an instrument, MIDI in and sound out.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginTestUtils.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    using testkit::set;

    std::unique_ptr<ParagonX1AudioProcessor> makeProcessor (int outputs = 2)
    {
        auto processor = std::make_unique<ParagonX1AudioProcessor>();
        processor->setPlayConfigDetails (0, outputs, sampleRate, blockSize);
        processor->prepareToPlay (sampleRate, blockSize);
        return processor;
    }

    // Plays `seconds`, with `midi` (positions from the start) spread over the blocks; returns channel 0 (and 1).
    std::array<std::vector<float>, 2> play (ParagonX1AudioProcessor& processor, double seconds, const juce::MidiBuffer& midi = {}, int channels = 2,
                                            int block = blockSize)
    {
        std::array<std::vector<float>, 2> out;
        juce::AudioBuffer<float> buffer (channels, block);
        for (int start = 0; start < (int) (seconds * sampleRate); start += block)
        {
            juce::MidiBuffer slice;
            slice.addEvents (midi, start, block, -start);
            buffer.clear();
            processor.processBlock (buffer, slice);
            for (int ch = 0; ch < channels; ++ch)
                out[(size_t) ch].insert (out[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + block);
        }
        return out;
    }

    juce::MidiBuffer note (int number, int at = 0, int length = 1 << 30, float velocity = 1.0f)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, number, velocity), at);
        if (length < (1 << 30))
            midi.addEvent (juce::MidiMessage::noteOff (1, number), at + length);
        return midi;
    }

    // Frequency of a steady tone from its zero crossings (rising), over the second half.
    double frequencyOf (const std::vector<float>& samples)
    {
        const auto from = samples.size() / 2;
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (auto i = from + 1; i < samples.size(); ++i)
            if (samples[i - 1] < 0.0f && samples[i] >= 0.0f)
            {
                const auto exact = (double) (i - 1) + samples[i - 1] / (samples[i - 1] - samples[i]);
                if (first < 0.0) first = exact; else ++crossings;
                last = exact;
            }
        return crossings > 0 ? sampleRate * crossings / (last - first) : 0.0;
    }

    double rms (const std::vector<float>& samples, size_t from, size_t to)
    {
        double sum = 0.0;
        for (auto i = from; i < to; ++i)
            sum += (double) samples[i] * samples[i];
        return std::sqrt (sum / (double) (to - from));
    }
}

TEST_CASE ("PR7-O is an instrument: no audio in, MIDI in, mono or stereo out", "[processor]")
{
    ParagonX1AudioProcessor processor;
    CHECK (processor.acceptsMidi());
    CHECK_FALSE (processor.producesMidi());
    CHECK (processor.getBusCount (true) == 0);
    REQUIRE (processor.getBusCount (false) == 1);
    CHECK (processor.getMainBusNumOutputChannels() == 2);
    CHECK (processor.getBus (false, 0)->getName() == "Main");
    CHECK (juce::String (JucePlugin_Name) == "Paragon PR7-O");
    CHECK (juce::String (JucePlugin_VersionString) == "0.12.1");

    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (processor.checkBusesLayoutSupported (layout));
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (processor.checkBusesLayoutSupported (layout));
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    CHECK_FALSE (processor.checkBusesLayoutSupported (layout));
}

TEST_CASE ("Silent with no instrument, and until a key plays; a note starts on its own sample, as recorded", "[processor]")
{
    auto processor = makeProcessor();
    const auto none = play (*processor, 0.3, note (60));
    CHECK (*std::max_element (none[0].begin(), none[0].end()) == 0.0f);

    const auto tone = testkit::sine (440.0);
    testkit::useSample (*processor, tone);
    const auto silent = play (*processor, 0.2);
    CHECK (*std::max_element (silent[0].begin(), silent[0].end()) == 0.0f);

    const auto out = play (*processor, 1.0, note (testkit::keyFor(), 100));
    for (int i = 0; i < 100; ++i)
        REQUIRE (out[0][(size_t) i] == 0.0f);   // nothing before its sample
    // From its sample on: the recording, sample for sample (its root key, the host's rate, LEVEL 0 dB, OUTPUT 0 dB).
    auto fresh = makeProcessor();
    testkit::useSample (*fresh, tone);
    set (*fresh, ParamIDs::outputGain, 0.0f);
    fresh->prepareToPlay (sampleRate, blockSize);   // (OUTPUT starts where it's set, not ramping from -6 dB)
    const auto exact = play (*fresh, 0.5, note (testkit::keyFor(), 100));
    for (size_t i = 0; i < 4000; ++i)
        REQUIRE (exact[1][100 + i] == tone->channels[0][i]);
    CHECK_THAT (frequencyOf (out[0]), WithinAbs (440.0, 0.05));
}

TEST_CASE ("Keys play the recording at their pitch: an octave up twice the frequency; TRANSPOSE, TUNE and the pitch wheel", "[processor]")
{
    const auto pitchOf = [] (int key, float transpose, float tune, int wheel)
    {
        auto processor = makeProcessor();
        testkit::useSample (*processor, testkit::sine (220.0));
        set (*processor, ParamIDs::instTranspose, transpose);
        set (*processor, ParamIDs::instTune, tune);
        auto midi = note (key);
        if (wheel != 8192)
            midi.addEvent (juce::MidiMessage::pitchWheel (1, wheel), 0);
        return frequencyOf (play (*processor, 0.6, midi)[0]);
    };
    CHECK_THAT (pitchOf (60, 0, 0, 8192), WithinAbs (220.0, 0.05));
    CHECK_THAT (pitchOf (72, 0, 0, 8192), WithinAbs (440.0, 0.1));
    CHECK_THAT (pitchOf (55, 0, 0, 8192), WithinAbs (220.0 * std::pow (2.0, -5.0 / 12.0), 0.05));
    CHECK_THAT (pitchOf (60, 7, 0, 8192), WithinAbs (220.0 * std::pow (2.0, 7.0 / 12.0), 0.1));
    CHECK_THAT (pitchOf (60, 0, 50, 8192), WithinAbs (220.0 * std::pow (2.0, 0.5 / 12.0), 0.05));
    CHECK_THAT (pitchOf (60, 0, 0, 16383), WithinAbs (220.0 * std::pow (2.0, (2.0 * 8191.0 / 8192.0) / 12.0), 0.1));   // +2 st
    CHECK_THAT (pitchOf (60, 0, 0, 0), WithinAbs (220.0 * std::pow (2.0, -2.0 / 12.0), 0.1));                         // -2 st
}

TEST_CASE ("Letting go releases a note over its release time; the sustain pedal holds it; RELEASE scales the time", "[processor]")
{
    const auto run = [] (float releaseScale, bool pedal)
    {
        auto processor = makeProcessor();
        auto program = sampler::programFrom (testkit::sine (220.0), 60);
        program->regions[0].release = 0.2f;   // 80 dB over 0.2 s
        program->regions[0].velTrack = 0.0f;
        processor->setProgram (std::move (program));
        set (*processor, ParamIDs::instRelease, releaseScale);
        auto midi = note (60, 0, 12000);   // let go at 0.25 s
        if (pedal)
        {
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 36000);   // lifted at 0.75 s
        }
        return play (*processor, 1.2, midi)[0];
    };
    const auto window = [] (const std::vector<float>& x, double at) { return rms (x, (size_t) (at * sampleRate), (size_t) ((at + 0.02) * sampleRate)); };
    const auto plain = run (1.0f, false);
    const auto held = window (plain, 0.2);
    CHECK_THAT (20.0 * std::log10 (window (plain, 0.35) / held), WithinAbs (-40.0, 3.5));   // 0.1 s in: half the 80 dB
    CHECK (window (plain, 0.5) == 0.0);                                                      // gone after 0.2 s
    const auto slower = run (2.0f, false);
    CHECK_THAT (20.0 * std::log10 (window (slower, 0.35) / held), WithinAbs (-20.0, 2.0));   // RELEASE x2: half as fast
    const auto pedalled = run (1.0f, true);
    CHECK_THAT (window (pedalled, 0.6) / held, WithinAbs (1.0, 0.02));                       // held past its note-off
    CHECK (window (pedalled, 1.05) == 0.0);                                                  // released when the pedal's lifted
}

TEST_CASE ("MIDI CHANNEL: ANY plays every channel; a channel chosen plays only its own", "[processor]")
{
    const auto peakOn = [] (int listen, int channel)
    {
        auto processor = makeProcessor();
        testkit::useSample (*processor, testkit::sine (220.0));
        set (*processor, ParamIDs::midiChannel, (float) listen);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (channel, 60, 1.0f), 0);
        const auto out = play (*processor, 0.2, midi);
        return *std::max_element (out[0].begin(), out[0].end());
    };
    CHECK (peakOn (0, 1) > 0.1f);
    CHECK (peakOn (0, 9) > 0.1f);
    CHECK (peakOn (3, 3) > 0.1f);
    CHECK (peakOn (3, 4) == 0.0f);
}

TEST_CASE ("OUTPUT, the preset's LEVEL and the instrument's LEVEL apply their dB; BYPASS fades to silence", "[processor]")
{
    const auto level = [] (float outputDb, float levelDb, float instrumentDb, bool bypass)
    {
        auto processor = makeProcessor();
        testkit::useSample (*processor, testkit::sine (220.0));
        set (*processor, ParamIDs::outputGain, outputDb);
        set (*processor, ParamIDs::patchLevel, levelDb);
        set (*processor, ParamIDs::instLevel, instrumentDb);
        set (*processor, ParamIDs::bypass, bypass ? 1.0f : 0.0f);
        const auto out = play (*processor, 0.5, note (testkit::keyFor()));
        return rms (out[0], 12000, 24000);
    };
    const auto reference = level (0.0f, 0.0f, 0.0f, false);
    REQUIRE (reference > 0.01);
    CHECK_THAT (level (-6.0f, 0.0f, 0.0f, false) / reference, WithinAbs (0.5012, 0.003));
    CHECK_THAT (level (0.0f, 6.0f, 0.0f, false) / reference, WithinAbs (1.9953, 0.01));
    CHECK_THAT (level (0.0f, 0.0f, -12.0f, false) / reference, WithinAbs (0.2512, 0.002));
    CHECK (level (0.0f, 0.0f, -60.0f, false) == 0.0);
    CHECK (level (0.0f, 0.0f, 0.0f, true) < 1.0e-6);
}

TEST_CASE ("OUTPUT starts at -6 dB, for headroom when notes stack up", "[processor]")
{
    ParagonX1AudioProcessor processor;
    CHECK_THAT (processor.getParameters().getRawParameterValue (ParamIDs::outputGain)->load(), WithinAbs (-6.0, 1.0e-6));
}

TEST_CASE ("The DAW's bypass button is our BYPASS", "[processor]")
{
    ParagonX1AudioProcessor processor;
    CHECK (processor.getBypassParameter() == processor.getParameters().getParameter (ParamIDs::bypass));
}

TEST_CASE ("A mono output plays both sides folded together", "[processor]")
{
    auto processor = makeProcessor (1);
    testkit::useSample (*processor, testkit::sine (220.0));
    set (*processor, ParamIDs::instPan, -1.0f);   // hard left: only the left side has it
    const auto out = play (*processor, 0.3, note (testkit::keyFor()), 1);
    CHECK (*std::max_element (out[0].begin(), out[0].end()) > 0.1f);
}

TEST_CASE ("Oversized blocks (more than the DAW promised) play the same", "[processor]")
{
    const auto render = [] (int block)
    {
        auto processor = makeProcessor();
        testkit::useSample (*processor, testkit::saw (110.0, 2.0));
        auto midi = note (64, 300, 6000);
        midi.addEvents (note (67, 900, 3000), 0, -1, 0);
        return play (*processor, 0.25, midi, 2, block)[0];
    };
    const auto normal = render (blockSize);
    const auto big = render (blockSize * 8);
    REQUIRE (normal.size() <= big.size());
    for (size_t i = 0; i < normal.size(); ++i)
        REQUIRE (std::abs (normal[i] - big[i]) < 1.0e-6f);
}

TEST_CASE ("Settings and the instrument survive a save and reload, as in a DAW project", "[processor][state]")
{
    // A real SFZ on disk (a sine, two regions), loaded, saved with the project, loaded again from its file.
    juce::TemporaryFile folder;
    const auto dir = folder.getFile();
    dir.createDirectory();
    testkit::writeWav (dir.getChildFile ("a.wav"), *testkit::sine (220.0, 1.0), 24);
    dir.getChildFile ("strings.sfz").replaceWithText ("<group> pitch_keycenter=57\n<region> sample=a.wav lokey=40 hikey=60\n"
                                                      "<region> sample=a.wav lokey=61 hikey=80 volume=-6\n");
    juce::MemoryBlock state;
    {
        ParagonX1AudioProcessor processor;
        processor.loadInstrument (dir.getChildFile ("strings.sfz"));
        REQUIRE (processor.waitForInstrument());
        REQUIRE (processor.getProgram() != nullptr);
        CHECK (processor.getProgram()->regions.size() == 2);
        set (processor, ParamIDs::instTranspose, -5.0f);
        set (processor, ParamIDs::instSendReverb, 0.4f);
        set (processor, ParamIDs::outputGain, -9.0f);
        processor.getStateInformation (state);
    }
    ParagonX1AudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    const auto get = [&restored] (const juce::String& id) { return restored.getParameters().getRawParameterValue (id)->load(); };
    CHECK_THAT (get (ParamIDs::instTranspose), WithinAbs (-5.0, 1.0e-4));
    CHECK_THAT (get (ParamIDs::instSendReverb), WithinAbs (0.4, 1.0e-4));
    CHECK_THAT (get (ParamIDs::outputGain), WithinAbs (-9.0, 1.0e-4));
    CHECK (restored.getInstrumentFile() == dir.getChildFile ("strings.sfz").getFullPathName());
    REQUIRE (restored.waitForInstrument());
    REQUIRE (restored.getProgram() != nullptr);
    CHECK (restored.getProgram()->regions.size() == 2);
    CHECK (restored.getProgram()->lowestKey() == 40);
    CHECK (restored.getProgram()->highestKey() == 80);

    // Its file gone: the project keeps where it was (saved again as it was) and says why it's silent.
    dir.getChildFile ("strings.sfz").deleteFile();
    ParagonX1AudioProcessor missing;
    missing.setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (missing.waitForInstrument());
    CHECK (missing.getProgram() == nullptr);
    CHECK (missing.getInstrumentFile() == dir.getChildFile ("strings.sfz").getFullPathName());
    CHECK (missing.getLoadProblem().startsWith ("not found"));
    dir.deleteRecursively();
}

TEST_CASE ("Loading an instrument is one undo step; CLEAR another", "[processor][undo]")
{
    juce::TemporaryFile wavFile (".wav");
    testkit::writeWav (wavFile.getFile(), *testkit::sine (220.0, 0.5), 24);
    ParagonX1AudioProcessor processor;
    processor.flushUndoStep();
    processor.loadInstrument (wavFile.getFile());   // one audio file: over the whole keyboard, rooted at 60
    REQUIRE (processor.waitForInstrument());
    processor.flushUndoStep();
    REQUIRE (processor.getProgram() != nullptr);
    CHECK (processor.getProgram()->regions.size() == 1);
    CHECK (processor.getProgram()->regions[0].keyCenter == 60);
    CHECK (processor.isRigModified());
    processor.clearInstrument();
    processor.flushUndoStep();
    CHECK (processor.getProgram() == nullptr);
    processor.undo();   // the instrument back
    REQUIRE (processor.waitForInstrument());
    CHECK (processor.getInstrumentFile() == wavFile.getFile().getFullPathName());
    CHECK (processor.getProgram() != nullptr);
    processor.undo();   // none, as it started
    CHECK (processor.getInstrumentFile().isEmpty());
    CHECK (processor.getProgram() == nullptr);
    processor.redo();
    REQUIRE (processor.waitForInstrument());
    CHECK (processor.getProgram() != nullptr);
}

TEST_CASE ("Fuzz: any settings, changed as notes play, never crash it or make it blow up", "[processor][fuzz]")
{
    auto processor = makeProcessor();
    testkit::useSample (*processor, testkit::saw (110.0, 3.0));
    juce::Random random (2026);
    const auto& parameters = static_cast<juce::AudioProcessor&> (*processor).getParameters();
    juce::AudioBuffer<float> buffer (2, blockSize);
    auto finite = true;
    float loudest = 0.0f;
    for (int block = 0; block < 3000; ++block)
    {
        // Every 20 blocks, a few parameters jump anywhere (except OUTPUT and BYPASS, to keep it audible).
        if (block % 20 == 0)
            for (int k = 0; k < 12; ++k)
            {
                auto* parameter = parameters[random.nextInt (parameters.size())];
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
                    if (withId->paramID != ParamIDs::outputGain && withId->paramID != ParamIDs::bypass && withId->paramID != ParamIDs::patchLevel
                        && withId->paramID != ParamIDs::midiChannel)
                        parameter->setValueNotifyingHost (random.nextFloat());
            }
        juce::MidiBuffer midi;
        if (random.nextInt (4) == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 24 + random.nextInt (84), (juce::uint8) (1 + random.nextInt (127))), random.nextInt (blockSize));
        if (random.nextInt (4) == 0)
            midi.addEvent (juce::MidiMessage::noteOff (1, 24 + random.nextInt (84)), random.nextInt (blockSize));
        if (random.nextInt (30) == 0)
            midi.addEvent (juce::MidiMessage::pitchWheel (1, random.nextInt (16384)), random.nextInt (blockSize));
        if (random.nextInt (60) == 0)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, random.nextBool() ? 127 : 0), random.nextInt (blockSize));
        if (random.nextInt (500) == 0)
            midi.addEvent (juce::MidiMessage::allNotesOff (1), random.nextInt (blockSize));
        processor->processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const auto x = buffer.getSample (ch, i);
                finite = finite && std::isfinite (x);
                loudest = juce::jmax (loudest, std::abs (x));
            }
    }
    CHECK (finite);
    CHECK (loudest < 1000.0f);   // nothing ran away (it can be loud: many notes at +12 dB, into the effects)
}

TEST_CASE ("0.2.0: a project reopens on the articulation it was saved with, once its instrument has loaded", "[processor][state][keyswitch]")
{
    juce::TemporaryFile folder;
    const auto dir = folder.getFile();
    dir.createDirectory();
    testkit::writeWav (dir.getChildFile ("a.wav"), *testkit::sine (220.0, 0.5), 24);
    dir.getChildFile ("ks.sfz").replaceWithText ("<group> sw_lokey=c2 sw_hikey=d#2 sw_default=c2 sw_last=c2 sw_label=C2 Sustain\n<region> sample=a.wav lokey=50 hikey=70\n"
                                                 "<group> sw_lokey=c2 sw_hikey=d#2 sw_last=d2 sw_label=D2 Spiccato\n<region> sample=a.wav lokey=50 hikey=70\n");
    juce::MemoryBlock state;
    {
        auto processor = makeProcessor();
        processor->loadInstrument (dir.getChildFile ("ks.sfz"));
        REQUIRE (processor->waitForInstrument());
        play (*processor, 0.01);
        CHECK (processor->getKeyswitch() == 36);   // its default
        processor->chooseKeyswitch (38);
        play (*processor, 0.01);
        CHECK (processor->getKeyswitch() == 38);
        processor->getStateInformation (state);
    }
    auto restored = makeProcessor();
    restored->setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (restored->waitForInstrument());
    play (*restored, 0.01);
    CHECK (restored->getKeyswitch() == 38);
    dir.deleteRecursively();
}

TEST_CASE ("SECTION 0.4.0: a section is kept with the project and comes back; moving a split is one undo step", "[processor][section][undo]")
{
    // Two invented parts on disk: a low one (40-64, a 110 Hz tone) and a high one (55-84, 440 Hz).
    juce::TemporaryFile folder;
    const auto dir = folder.getFile();
    dir.createDirectory();
    testkit::writeWav (dir.getChildFile ("low.wav"), *testkit::sine (110.0, 1.0), 24);
    testkit::writeWav (dir.getChildFile ("high.wav"), *testkit::sine (440.0, 1.0), 24);
    dir.getChildFile ("Cellos.sfz").replaceWithText ("<region> sample=low.wav lokey=40 hikey=64 pitch_keycenter=45\n");
    dir.getChildFile ("Violins.sfz").replaceWithText ("<region> sample=high.wav lokey=55 hikey=84 pitch_keycenter=69\n");
    const auto section = sampler::section::fromFiles ({ dir.getChildFile ("Cellos.sfz").getFullPathName(),
                                                        dir.getChildFile ("Violins.sfz").getFullPathName() });
    REQUIRE (section.parts.size() == 2);
    CHECK (section.parts[0].high == 54);

    juce::MemoryBlock state;
    {
        auto processor = makeProcessor();
        processor->flushUndoStep();
        processor->loadSection (section);
        REQUIRE (processor->waitForInstrument());
        processor->flushUndoStep();
        REQUIRE (processor->getProgram() != nullptr);
        REQUIRE (processor->getSection().has_value());
        CHECK (*processor->getSection() == section);
        // It plays: under the split the cellos, over it the violins.
        CHECK_THAT (frequencyOf (play (*processor, 0.6, note (45))[0]), WithinAbs (110.0, 0.5));
        CHECK_THAT (frequencyOf (play (*processor, 0.6, note (69))[0]), WithinAbs (440.0, 0.5));

        // The split moved up to 60: one undo step.
        auto moved = section;
        moved.parts[0].high = 59;
        moved.parts[1].low = 60;
        processor->loadSection (moved);
        REQUIRE (processor->waitForInstrument());
        processor->flushUndoStep();
        CHECK (*processor->getSection() == moved);
        processor->undo();
        REQUIRE (processor->waitForInstrument());
        REQUIRE (processor->getSection().has_value());
        CHECK (*processor->getSection() == section);
        processor->redo();
        REQUIRE (processor->waitForInstrument());
        CHECK (*processor->getSection() == moved);
        processor->getStateInformation (state);
    }
    // The project reopened: the same section, loaded again.
    auto restored = makeProcessor();
    restored->setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (restored->waitForInstrument());
    REQUIRE (restored->getSection().has_value());
    CHECK (restored->getSection()->parts[0].high == 59);
    REQUIRE (restored->getProgram() != nullptr);
    CHECK (restored->getProgram()->numFiles == 2);
    CHECK (restored->getProgram()->name == "Cellos + Violins");
}

TEST_CASE ("0.11.0: a long recording streams from disk; a bounce of it is what the whole one in memory gives", "[processor][streaming]")
{
    // 6 s gliding tones, a loop in the middle (an invented file).
    juce::TemporaryFile folder;
    const auto dir = folder.getFile();
    dir.createDirectory();
    {
        juce::AudioBuffer<float> audio (2, (int) (6.0 * sampleRate));
        for (int c = 0; c < 2; ++c)
        {
            double phase = 0.0;
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                phase += juce::MathConstants<double>::twoPi * (200.0 + 60.0 * i / sampleRate + 30.0 * c) / sampleRate;
                audio.setSample (c, i, 0.3f * (float) std::sin (phase));
            }
        }
        testkit::writeWav (dir.getChildFile ("long.wav"), audio, sampleRate);
        dir.getChildFile ("long.sfz").replaceWithText ("<region> sample=long.wav pitch_keycenter=60 amp_veltrack=0\n"
                                                       "loop_mode=loop_sustain loop_start=96000 loop_end=180000 loop_crossfade=0.25\n");
    }
    std::array<std::vector<float>, 2> results[2];
    for (int streaming = 0; streaming < 2; ++streaming)
    {
        auto processor = makeProcessor();
        processor->setStreamFromDisk (streaming == 1);
        processor->setNonRealtime (true);
        processor->loadInstrument (dir.getChildFile ("long.sfz"));
        REQUIRE (processor->waitForInstrument());
        REQUIRE (processor->getProgram() != nullptr);
        const auto& p = *processor->getProgram();
        if (streaming == 1)
        {
            CHECK (p.regions[0].sample->streamed());
            CHECK ((double) p.bytes < 0.2 * (double) p.wholeBytes);
            CHECK (processor->getStreamer() != nullptr);
        }
        else
            CHECK (! p.regions[0].sample->streamed());
        results[streaming] = play (*processor, 9.0, note (60, 0, (int) (6.5 * sampleRate)));
        if (streaming == 1)
            CHECK (processor->getStreamer()->getUnderruns() == 0);
    }
    auto most = 0.0f, peak = 0.0f;
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < results[0][c].size(); ++i)
        {
            most = std::max (most, std::abs (results[0][c][i] - results[1][c][i]));
            peak = std::max (peak, std::abs (results[0][c][i]));
        }
    CHECK (peak > 0.05f);
    CHECK (most < 1.0e-6f);
    dir.deleteRecursively();
}
