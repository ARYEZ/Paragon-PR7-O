// MOD, MOD 2 and LO-FI (PR7-K's stage 7c): each side kept its own (where PR7-G's mono chain summed them), where
// they sit (after the sends' returns, before the LIMITER), off changing nothing, the displays against the sound,
// CPU, the FX page's view and pages. Played with a saw as the instrument (PluginTestUtils.h). (The effects themselves: ModFxTests.cpp, PR7-G's own tests.)
#include "TestUtils.h"
#include "PluginTestUtils.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/effects/LoFi.h"
#include "dsp/effects/Modulation.h"
#include "ui/FxPage.h"
#include "ui/MainView.h"

namespace
{
    using Type = effects::Modulation::Type;
    using LoFiType = effects::LoFi::Type;

    struct Plugin
    {
        ParagonX1AudioProcessor processor;
        Plugin()
        {
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            processor.seedRandomForTest (1);
        }
        void set (const juce::String& id, float value)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            REQUIRE (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        }
        // A saw at `note`'s pitch, `held` seconds long, the instrument, played at the start; `seconds` in all. Both channels.
        std::array<std::vector<float>, 2> play (int note, double held, double seconds)
        {
            juce::MidiBuffer starts;   // as MIDI from the DAW (the on-screen keyboard's presses land by the clock: not repeatable)
            std::array<std::vector<float>, 2> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            testkit::useSample (processor, testkit::saw (noteHz (note), held));
            starts.addEvent (juce::MidiMessage::noteOn (1, testkit::keyFor (0), 0.8f), 0);
            const auto blocks = (int) (seconds * sampleRate) / blockSize;
            for (int b = 0; b < blocks; ++b)
            {
                auto midi = std::exchange (starts, {});   // the hits in the first block, at its first sample
                buffer.clear();
                processor.processBlock (buffer, midi);
                for (int c = 0; c < 2; ++c)
                    out[(size_t) c].insert (out[(size_t) c].end(), buffer.getReadPointer (c), buffer.getReadPointer (c) + blockSize);
            }
            return out;
        }
    };

    double peakOf (const std::vector<float>& x, double from = 0.0)
    {
        auto peak = 0.0f;
        for (auto i = (size_t) (from * sampleRate); i < x.size(); ++i)
            peak = std::max (peak, std::abs (x[i]));
        return peak;
    }

    // An effect by itself, both sides given: what comes out of each.
    template <typename Effect, typename Settings>
    std::array<std::vector<float>, 2> runEffect (const Settings& settings, const std::vector<float>& left, const std::vector<float>& right)
    {
        auto effect = std::make_unique<Effect>();
        effect->prepare (sampleRate, blockSize);
        if constexpr (std::is_same_v<Effect, effects::LoFi>)
            effect->seedRandom (1);   // the cassette's wander is random: the same each run
        std::array<std::vector<float>, 2> out { left, right };
        for (size_t start = 0; start < left.size(); start += blockSize)
        {
            float* channels[] = { out[0].data() + start, out[1].data() + start };
            effect->process (juce::dsp::AudioBlock<float> (channels, 2, std::min<size_t> (blockSize, left.size() - start)), settings);
        }
        return out;
    }

    std::vector<float> sineOf (double hz, float amplitude, double seconds)
    {
        std::vector<float> x ((size_t) (seconds * sampleRate));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate);
        return x;
    }
}

TEST_CASE ("MOD / LO-FI: every type keeps a sound on the left on the left (PR7-G's mono chain summed them)", "[modfx]")
{
    // A sound panned hard left through each type: the right stays exactly silent. On PR7-G, AEON, the Rotary, the
    // Doubler, the Vortex's STEREO and the speakers summed or crossed the sides.
    for (int type = 0; type < effects::Modulation::typeNames.size(); ++type)
        for (const auto stereo : { false, true })
        {
            if (stereo && type != (int) Type::chorus)
                continue;
            INFO (effects::Modulation::typeNames[type] << (stereo ? " STEREO" : ""));
            Plugin p;
            p.set (ParamIDs::instPan, -1.0f);
            p.set (ParamIDs::modOn, 1.0f);
            p.set (ParamIDs::modType, (float) type);
            p.set (ParamIDs::modStereo, stereo ? 1.0f : 0.0f);
            p.set (ParamIDs::modDepth, 0.8f);
            p.set (ParamIDs::modMix, 0.7f);
            const auto out = p.play (57, 0.3, 0.5);
            CHECK (rms (out[0], (size_t) (0.05 * sampleRate), out[0].size()) > 0.02);
            CHECK (peakOf (out[1]) == 0.0);
        }
    for (int type = 0; type < effects::LoFi::typeNames.size(); ++type)
    {
        INFO (effects::LoFi::typeNames[type]);
        Plugin p;
        p.set (ParamIDs::instPan, -1.0f);
        p.set (ParamIDs::lofiOn, 1.0f);
        p.set (ParamIDs::lofiType, (float) type);
        p.set (ParamIDs::lofiNoise, 0.0f);   // (NOISE's hiss is on both sides, as a phone line's)
        const auto out = p.play (57, 0.3, 0.5);
        CHECK (rms (out[0], (size_t) (0.05 * sampleRate), out[0].size()) > 0.005);
        CHECK (peakOf (out[1]) == 0.0);
    }
}

TEST_CASE ("MOD / LO-FI: each side is worked on its own: the left comes out the same whatever's on the right", "[modfx]")
{
    // Sample for sample, for every type (LO-FI's NOISE off: its hiss is one sound for both sides).
    const auto left = sineOf (330.0, 0.3f, 0.6), quiet = std::vector<float> (left.size(), 0.0f), other = sineOf (523.0, 0.4f, 0.6);
    for (int type = 0; type < effects::Modulation::typeNames.size(); ++type)
        for (const auto stereo : { false, true })
        {
            INFO (effects::Modulation::typeNames[type] << (stereo ? " STEREO" : ""));
            effects::Modulation::Settings s;
            s.on = true;
            s.type = (Type) type;
            s.depth = 0.8f;
            s.mix = 0.7f;
            s.stereoOutputs = stereo;
            const auto alone = runEffect<effects::Modulation> (s, left, quiet);
            const auto beside = runEffect<effects::Modulation> (s, left, other);
            CHECK (alone[0] == beside[0]);
        }
    for (int type = 0; type < effects::LoFi::typeNames.size(); ++type)
    {
        INFO (effects::LoFi::typeNames[type]);
        effects::LoFi::Settings s;
        s.on = true;
        s.type = (LoFiType) type;
        s.noise = 0.0f;
        CHECK (runEffect<effects::LoFi> (s, left, quiet)[0] == runEffect<effects::LoFi> (s, left, other)[0]);
    }
}

TEST_CASE ("MOD / LO-FI: switched off they change nothing, whatever their settings", "[modfx]")
{
    const auto run = [] (bool changed)
    {
        Plugin p;
        if (changed)
        {
            p.set (ParamIDs::modType, (float) (int) Type::rotary);
            p.set (ParamIDs::modDepth, 1.0f);
            p.set (ParamIDs::mod2Type, (float) (int) Type::autoPan);
            p.set (ParamIDs::lofiType, (float) (int) LoFiType::bitcrusher);
            p.set (ParamIDs::lofiAmount, 9.0f);
        }
        return p.play (57, 0.3, 0.5);
    };
    const auto plain = run (false), changed = run (true);
    CHECK (plain[0] == changed[0]);
    CHECK (plain[1] == changed[1]);
}

TEST_CASE ("MOD / LO-FI: after the sends' returns (the reverb's tail goes through the phone too), before the LIMITER", "[modfx][sends]")
{
    // A saw on A3 into a long hall, 0.1 s long: in its tail, the fundamental (220 Hz) against harmonic 4 (880 Hz). The
    // phone's band starts around 300 Hz, so through it the tail's bottom drops. (PR7-K measured harmonic 37, 8.1 kHz:
    // with the saw as a sample, the hall's tail has almost none left to cut, about -118 dB.)
    const auto tail = [] (bool phone)
    {
        Plugin p;
        p.set (ParamIDs::reverbOn, 1.0f);
        p.set (ParamIDs::instSendReverb, 1.0f);
        p.set (ParamIDs::reverbMix, 1.0f);
        p.set (ParamIDs::reverbType, 2.0f);   // HALL
        p.set (ParamIDs::reverbSize, 0.8f);
        if (phone)
        {
            p.set (ParamIDs::lofiOn, 1.0f);
            p.set (ParamIDs::lofiNoise, 0.0f);
        }
        // (the saw itself gone after 0.1 s: what's left is the tail)
        const auto out = p.play (57, 0.1, 1.2);
        const auto from = (size_t) (0.6 * sampleRate), length = (size_t) (0.5 * sampleRate);
        return std::pair { levelAt (out[0], 220.0, from, length), levelAt (out[0], 220.0 * 4, from, length) };
    };
    const auto [plainLow, plainMid] = tail (false);
    const auto [phoneLow, phoneMid] = tail (true);
    REQUIRE (plainMid > 1.0e-4);   // the tail is there
    CHECK (phoneMid > 0.1 * plainMid);   // and still there through the phone
    WARN ("the tail's 220 Hz against 880 Hz: plain " << db (plainLow / plainMid) << " dB, through the phone " << db (phoneLow / phoneMid) << " dB");
    CHECK (db (phoneLow / phoneMid) < db (plainLow / plainMid) - 12.0);   // its bottom cut by the phone

    // LO-FI's LEVEL turned all the way up into the LIMITER: held at its ceiling.
    Plugin loud;
    loud.set (ParamIDs::lofiOn, 1.0f);
    loud.set (ParamIDs::lofiType, (float) (int) LoFiType::megaphone);
    loud.set (ParamIDs::lofiLevel, 12.0f);
    loud.set (ParamIDs::outputGain, 12.0f);
    loud.set (ParamIDs::limitOn, 1.0f);
    loud.set (ParamIDs::limitCeiling, -1.0f);
    const auto out = loud.play (57, 0.5, 0.6);
    for (int c = 0; c < 2; ++c)
        CHECK (db (peakOf (out[(size_t) c], 0.05)) <= -1.0 + 0.01);
}

TEST_CASE ("MOD / LO-FI: what the pages draw is what plays", "[modfx]")
{
    SECTION ("The sweep's marker moves at RATE")
    {
        Plugin p;
        p.set (ParamIDs::modOn, 1.0f);
        p.set (ParamIDs::modType, (float) (int) Type::tremolo);
        p.set (ParamIDs::modRate, 1.0f);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        const auto blocks = (int) (0.25 * sampleRate) / blockSize;   // 46 blocks: 0.2453 s
        for (int b = 0; b < blocks; ++b)
            p.processor.processBlock (buffer, midi);
        CHECK_THAT (p.processor.getModSweepPhase (0), WithinAbs (blocks * blockSize / sampleRate, 1.0e-3));
        // And Razor's drawn volume is its WAVE's shape.
        effects::Modulation::Settings s;
        s.wave = 7.0f;
        CHECK_THAT (effects::Modulation::sweepShape (Type::tremolo, 0.3f, s), WithinAbs (2.0f * effects::Modulation::razorShape (0.3f, 7.0f) - 1.0f, 1.0e-6));
    }

    SECTION ("The speakers' band: about 6 dB down at each edge drawn (two 12 dB/octave stages, each 3 dB down there)")
    {
        for (const auto type : { LoFiType::telephone, LoFiType::amRadio })
        {
            INFO (effects::LoFi::typeNames[(int) type]);
            effects::LoFi::Settings s;
            s.on = true;
            s.type = type;
            s.amount = 2.0f;
            s.noise = 0.0f;
            const auto band = effects::LoFi::bandFor (type, s.amount, sampleRate);
            const auto gainAt = [&] (double hz)
            {
                const auto x = sineOf (hz, 0.01f, 0.5);   // quiet: the speaker's saturation stays out of it
                const auto out = runEffect<effects::LoFi> (s, x, x)[0];
                return db (rms (out, (size_t) (0.2 * sampleRate), out.size()) / rms (x, (size_t) (0.2 * sampleRate), x.size()));
            };
            const auto middle = gainAt (std::sqrt ((double) band.lowHz * band.highHz));
            CHECK_THAT (gainAt (band.lowHz) - middle, WithinAbs (-6.0, 2.5));
            CHECK_THAT (gainAt (band.highHz) - middle, WithinAbs (-6.0, 2.5));
        }
    }

    SECTION ("The bitcrusher holds each sample for the count drawn")
    {
        effects::LoFi::Settings s;
        s.on = true;
        s.type = LoFiType::bitcrusher;
        s.amount = 8.0f;
        s.noise = 0.0f;
        const auto hold = effects::LoFi::crusherHold (s.amount, sampleRate);
        REQUIRE (hold > 2);
        const auto x = sineOf (97.0, 0.8f, 1.0);
        const auto out = runEffect<effects::LoFi> (s, x, x)[0];
        int changes = 0;
        for (size_t i = 24000; i < out.size(); ++i)
            changes += out[i] != out[i - 1] ? 1 : 0;
        CHECK (changes <= (int) (out.size() - 24000) / hold + 1);
        CHECK (changes > (int) (out.size() - 24000) / hold / 4);   // (a slow sine: not every held sample is a new level)
    }
}

TEST_CASE ("MOD / LO-FI: CPU, the kit through AEON, the Rotary and the cassette", "[modfx][performance]")
{
    const auto cpu = [] (bool on)
    {
        auto best = 1.0e9;
        for (int run = 0; run < 3; ++run)
        {
            Plugin p;
            testkit::useSample (p.processor, testkit::saw (110.0, 2.0));
            if (on)
            {
                p.set (ParamIDs::modOn, 1.0f);
                p.set (ParamIDs::modType, (float) (int) Type::flanger);   // AEON: two pedals (one a side), oversampled
                p.set (ParamIDs::mod2On, 1.0f);
                p.set (ParamIDs::mod2Type, (float) (int) Type::rotary);
                p.set (ParamIDs::lofiOn, 1.0f);
                p.set (ParamIDs::lofiType, (float) (int) LoFiType::cassette);
            }
            juce::AudioBuffer<float> buffer (2, blockSize);
            const auto started = juce::Time::getMillisecondCounterHiRes();
            for (int b = 0; b < (int) (4.0 * sampleRate) / blockSize; ++b)
            {
                juce::MidiBuffer midi;   // a note every eighth of a second, round an octave
                if (b % 23 == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, testkit::keyFor ((b / 23) % 12), (juce::uint8) 100), 0);
                p.processor.processBlock (buffer, midi);
            }
            best = std::min (best, (juce::Time::getMillisecondCounterHiRes() - started) / 4000.0 * 100.0);
        }
        return best;
    };
    const auto off = cpu (false), on = cpu (true);
    WARN ("a 2 s saw, a note every eighth of a second: MOD / LO-FI off " << off << "% of one core; AEON, Rotary and cassette on " << on << "%");
    CHECK (on < off + 15.0);   // generous for CI machines
}

TEST_CASE ("The FX page's MOD / LO-FI view: three units, their pages, only the controls each type uses", "[modfx][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    view->showPage (MainView::Page::fx);
    auto& page = view->getFxPage();
    page.getModFxTab().onClick();
    CHECK (page.getView() == FxPage::View::modFx);
    const juce::String names[] = { "MOD", "MOD 2", "LO-FI" };
    std::vector<juce::Component*> shown { &page.getSendsTab(), &page.getModFxTab(), &page.getMasterTab() };
    for (int i = 0; i < 3; ++i)
    {
        CHECK (page.getModFxUnit (i).getSpec().name == names[i]);
        shown.push_back (&page.getModFxUnit (i));
    }
    for (size_t i = 0; i < shown.size(); ++i)
    {
        INFO (shown[i]->getBounds().toString());
        CHECK (shown[i]->isVisible());
        CHECK (page.getLocalBounds().contains (shown[i]->getBounds()));
        for (size_t j = i + 1; j < shown.size(); ++j)
            CHECK_FALSE (shown[i]->getBounds().intersects (shown[j]->getBounds()));
    }
    CHECK_FALSE (page.getUnit (0).isVisible());
    CHECK_FALSE (page.getSendUnit (0).isVisible());

    // POWER is each one's ON.
    const char* onIds[] = { ParamIDs::modOn, ParamIDs::mod2On, ParamIDs::lofiOn };
    for (int i = 0; i < 3; ++i)
    {
        page.getModFxUnit (i).getPower().toggle();
        CHECK (processor.getParameters().getParameter (onIds[i])->getValue() > 0.5f);
    }

    // EDIT opens each page; BACK TO RACK returns here.
    const FxPage::View views[] = { FxPage::View::mod, FxPage::View::mod2, FxPage::View::loFi };
    for (int i = 0; i < 3; ++i)
    {
        page.getModFxUnit (i).getEdit().onClick();
        CHECK (page.getView() == views[i]);
        auto& fxPage = page.getModFxPage (i);
        CHECK (fxPage.isVisible());
        for (auto* c : fxPage.getShownControls())
        {
            INFO (c->getParameterId() << " " << c->getBounds().toString());
            CHECK (fxPage.getLocalBounds().contains (c->getBounds()));
        }
        CHECK (fxPage.getLocalBounds().contains (fxPage.getDisplay().getBounds()));
        fxPage.onClose();
        CHECK (page.getView() == FxPage::View::modFx);
    }

    // MOD's page shows what the type uses, as PR7-G's panel: the Vortex its EQ and STEREO; AEON MANUAL and RES;
    // Hadron its CIRCUIT and neither DEPTH nor MIX; SYNC on hides RATE.
    auto set = [&] (const char* id, float v) { auto* q = processor.getParameters().getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    auto& modPage = page.getModFxPage (0);
    page.show (FxPage::View::mod);
    const auto shownIds = [&]
    {
        modPage.resized();
        juce::StringArray ids;
        for (auto* c : modPage.getShownControls())
            ids.add (c->getParameterId());
        return ids;
    };
    auto ids = shownIds();
    CHECK (ids.contains (ParamIDs::modEq));
    CHECK (ids.contains (ParamIDs::modStereo));
    CHECK_FALSE (ids.contains (ParamIDs::modManual));
    set (ParamIDs::modType, (float) (int) Type::flanger);
    ids = shownIds();
    CHECK (ids.contains (ParamIDs::modManual));
    CHECK (ids.contains (ParamIDs::modRes));
    CHECK_FALSE (ids.contains (ParamIDs::modEq));
    set (ParamIDs::modType, (float) (int) Type::phaser);
    ids = shownIds();
    CHECK (ids.contains (ParamIDs::modHadron));
    CHECK_FALSE (ids.contains (ParamIDs::modDepth));
    CHECK_FALSE (ids.contains (ParamIDs::modMix));
    CHECK (ids.contains (ParamIDs::modRate));
    set (ParamIDs::modSync, 3.0f);
    CHECK_FALSE (shownIds().contains (ParamIDs::modRate));
}
