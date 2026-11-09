// THE INSERT BUSES (PR7-K's stage 7d): the instrument routed to a bus A to D, the bus's DRIVE, GLITCH, MOD and LO-FI
// working on it, before the sends (the bus's own sends feeding DELAY and REVERB), measured from what the plugin plays
// (steady sines as the instrument: PluginTestUtils.h). (PR7-G's own DRIVE and GLITCH tests: DriveGlitchTests.cpp.)
#include "TestUtils.h"
#include "PluginTestUtils.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/effects/Drive.h"
#include "dsp/effects/Glitch.h"
#include "ui/FxPage.h"
#include "ui/MainView.h"

namespace
{
    using Model = effects::Drive::Model;

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
        void driveOn (int bus, Model model, float drive)
        {
            set (ParamIDs::ins (bus, "DriveOn"), 1.0f);
            set (ParamIDs::ins (bus, "DriveModel"), (float) (int) model);
            set (ParamIDs::ins (bus, "DriveGain"), drive);
        }
        // A sine at `note`'s pitch, `held` seconds long, the instrument, played at the start.
        std::array<std::vector<float>, 2> play (int note, double held, double seconds)
        {
            juce::MidiBuffer starts;   // as MIDI from the DAW (the on-screen keyboard's presses land by the clock: not repeatable)
            std::array<std::vector<float>, 2> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            testkit::useSample (processor, testkit::sine (noteHz (note), held));
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

    double tone (const std::vector<float>& x, double hz, double from, double length = 0.1)
    {
        return levelAt (x, hz, (size_t) (from * sampleRate), (size_t) (length * sampleRate));
    }
    double peakOf (const std::vector<float>& x, double from = 0.0)
    {
        auto peak = 0.0f;
        for (auto i = (size_t) (from * sampleRate); i < x.size(); ++i)
            peak = std::max (peak, std::abs (x[i]));
        return peak;
    }
    // A frequency's level over [from, from + length) seconds, through a Hann window: next to a loud note a few
    // tens of Hz away, a plain (rectangular) measurement leaks far more than these tests look for.
    double windowed (const std::vector<float>& x, double hz, double from, double length = 0.25)
    {
        const auto start = (size_t) (from * sampleRate), n = (size_t) (length * sampleRate);
        double re = 0.0, im = 0.0, weights = 0.0;
        for (size_t i = 0; i < n && start + i < x.size(); ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (double) i / (double) n);
            const auto phase = juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate;
            re += w * x[start + i] * std::cos (phase);
            im += w * x[start + i] * std::sin (phase);
            weights += w;
        }
        return 2.0 * std::sqrt (re * re + im * im) / weights;
    }
    std::vector<float> minus (const std::vector<float>& a, const std::vector<float>& b)
    {
        std::vector<float> d (std::min (a.size(), b.size()));
        for (size_t i = 0; i < d.size(); ++i)
            d[i] = a[i] - b[i];
        return d;
    }
    float maxDifference (const std::vector<float>& a, const std::vector<float>& b)
    {
        float result = 0.0f;
        for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            result = std::max (result, std::abs (a[i] - b[i]));
        return result;
    }
}

TEST_CASE ("INSERTS: with the instrument on no bus, its effects change nothing, sample for sample", "[inserts]")
{
    const auto run = [] (bool busesSet)
    {
        Plugin p;
        if (busesSet)
            for (int b = 0; b < ParamIDs::numInsertBuses; ++b)
            {
                p.driveOn (b, Model::rabid, 9.0f);
                p.set (ParamIDs::ins (b, "GlitchOn"), 1.0f);
                p.set (ParamIDs::insModIds (b).on, 1.0f);
                p.set (ParamIDs::ins (b, "LofiOn"), 1.0f);
            }
        return p.play (57, 0.3, 0.5);
    };
    const auto plain = run (false), set = run (true);
    CHECK (plain[0] == set[0]);
    CHECK (plain[1] == set[1]);
}

TEST_CASE ("INSERTS: on a bus with its effects off, the instrument sounds as it did", "[inserts]")
{
    const auto run = [] (bool onBus)
    {
        Plugin p;
        if (onBus)
            p.set (ParamIDs::instInsert, 2.0f);   // B
        return p.play (57, 0.3, 0.5);
    };
    const auto plain = run (false), bused = run (true);
    REQUIRE (peakOf (plain[0]) > 0.05);
    CHECK (maxDifference (plain[0], bused[0]) < 1.0e-6f);
    CHECK (maxDifference (plain[1], bused[1]) < 1.0e-6f);
}

TEST_CASE ("INSERTS: a bus's DRIVE works on the instrument on it", "[inserts]")
{
    // A3 (220 Hz) on bus A with BANSHEE driven hard: what the drive changes (driven minus clean) holds A3's odd
    // harmonics (660 Hz); the clean sound has none.
    const auto run = [] (bool driven)
    {
        Plugin p;
        p.set (ParamIDs::instInsert, 1.0f);
        if (driven)
            p.driveOn (0, Model::screamer, 9.0f);
        return p.play (57, 0.7, 0.7)[0];
    };
    const auto clean = run (false), driven = run (true);
    const auto change = minus (driven, clean);
    REQUIRE (windowed (clean, 220.0, 0.3) > 0.1);
    CHECK (windowed (change, 660.0, 0.3) > 0.01);   // A3's 3rd harmonic, from the drive
    CHECK (windowed (clean, 660.0, 0.3) < 1.0e-4);
}

TEST_CASE ("INSERTS: a bus sends at its own amounts, after its effects; the instrument's own sends aren't used", "[inserts][sends]")
{
    // A delay of plain 200 ms echoes at full MIX. The instrument on bus A (driven), its own DELAY send at 100% (unused).
    const auto echoes = [] (float busSend)
    {
        Plugin p;
        p.set (ParamIDs::delayOn, 1.0f);
        p.set (ParamIDs::delaySync, 0.0f);
        p.set (ParamIDs::delayTime, 200.0f);
        p.set (ParamIDs::delayFeedback, 0.0f);
        p.set (ParamIDs::delayMix, 1.0f);
        p.set (ParamIDs::instSendDelay, 1.0f);
        p.set (ParamIDs::instInsert, 1.0f);
        p.driveOn (0, Model::screamer, 9.0f);
        p.set (ParamIDs::ins (0, "SendDelay"), busSend);
        return p.play (57, 0.1, 0.5)[0];
    };
    const auto sent = echoes (1.0f), dry = echoes (0.0f), half = echoes (0.5f);
    // The echo (200-300 ms): there at the bus's 100%, with the drive's harmonic in it; none at 0%; 6 dB down at 50%.
    CHECK (tone (sent, 220.0, 0.21, 0.08) > 0.05);
    CHECK (tone (sent, 660.0, 0.21, 0.08) > 0.005);
    CHECK (tone (dry, 220.0, 0.21, 0.08) < 1.0e-4);
    CHECK_THAT (db (tone (half, 220.0, 0.21, 0.08) / tone (sent, 220.0, 0.21, 0.08)), WithinAbs (-6.0, 0.5));
}

TEST_CASE ("INSERTS: a sound on the left stays on the left through every effect on a bus", "[inserts]")
{
    Plugin p;
    p.set (ParamIDs::instPan, -1.0f);
    p.set (ParamIDs::instInsert, 3.0f);   // C
    p.driveOn (2, Model::rabid, 7.0f);
    p.set (ParamIDs::ins (2, "GlitchOn"), 1.0f);
    p.set (ParamIDs::ins (2, "GlitchMode"), (float) (int) effects::Glitch::Mode::interfere);
    p.set (ParamIDs::insModIds (2).on, 1.0f);
    p.set (ParamIDs::insModIds (2).type, (float) (int) effects::Modulation::Type::flanger);
    p.set (ParamIDs::ins (2, "LofiOn"), 1.0f);
    p.set (ParamIDs::ins (2, "LofiNoise"), 0.0f);
    const auto out = p.play (57, 0.4, 0.6);
    CHECK (rms (out[0], (size_t) (0.05 * sampleRate), out[0].size()) > 0.01);
    CHECK (peakOf (out[1]) == 0.0);
}

TEST_CASE ("INSERTS: each bus is its own: a drive on A leaves the instrument on B clean", "[inserts]")
{
    Plugin p;
    p.set (ParamIDs::instInsert, 2.0f);   // on B, nothing on
    p.driveOn (0, Model::screamer, 9.0f);
    const auto out = p.play (57, 0.7, 0.7)[0];
    CHECK (windowed (out, 220.0, 0.3) > 0.1);
    CHECK (windowed (out, 660.0, 0.3) < 1.0e-4);
    CHECK (p.processor.getInsertPeakDb (1) > -40.0f);
    CHECK (p.processor.getInsertPeakDb (0) < -99.0f);   // nothing on A
}

TEST_CASE ("INSERTS: CPU, the instrument on a bus through RABID (one a side, 8x oversampled) and GLITCH", "[inserts][performance]")
{
    const auto cpu = [] (int how)   // 0: no bus; 1: on a bus with its effects on; 2: the effects on, nothing routed
    {
        auto best = 1.0e9;
        for (int run = 0; run < 3; ++run)
        {
            Plugin p;
            testkit::useSample (p.processor, testkit::saw (110.0, 2.0));
            if (how > 0)
            {
                p.driveOn (0, Model::rabid, 7.0f);
                p.set (ParamIDs::ins (0, "GlitchOn"), 1.0f);
            }
            if (how == 1)
                p.set (ParamIDs::instInsert, 1.0f);
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
    const auto plain = cpu (0), bused = cpu (1), unrouted = cpu (2);
    WARN ("a 2 s saw, a note every eighth of a second: no bus " << plain << "% of one core; on bus A with RABID and GLITCH " << bused
          << "%; those on with nothing routed " << unrouted << "%");
    CHECK (bused < plain + 45.0);       // RABID solves its circuit at 8x, once a side (generous for CI machines)
    CHECK (unrouted < plain + 1.0);     // a bus nothing plays into costs nothing
}

TEST_CASE ("The FX page's INSERTS view: the bus strip, each bus's units and pages", "[inserts][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    view->showPage (MainView::Page::fx);
    auto& page = view->getFxPage();
    page.getInsertsTab().onClick();
    CHECK (page.getView() == FxPage::View::inserts);
    auto& strip = page.getInsertStrip();
    std::vector<juce::Component*> shown { &page.getInsertsTab(), &page.getSendsTab(), &page.getModFxTab(), &page.getMasterTab(), &strip };
    const juce::String names[] = { "DRIVE", "GLITCH", "MOD", "LO-FI" };
    for (int i = 0; i < 4; ++i)
    {
        CHECK (page.getInsertUnit (0, i).getSpec().name == names[i]);
        shown.push_back (&page.getInsertUnit (0, i));
    }
    for (size_t i = 0; i < shown.size(); ++i)
    {
        INFO (shown[i]->getBounds().toString());
        CHECK (shown[i]->isVisible());
        CHECK (page.getLocalBounds().contains (shown[i]->getBounds()));
        for (size_t j = i + 1; j < shown.size(); ++j)
            CHECK_FALSE (shown[i]->getBounds().intersects (shown[j]->getBounds()));
    }
    CHECK_FALSE (page.getInsertUnit (1, 0).isVisible());

    // Routing from the strip: THE INSTRUMENT puts it on the bus shown; on another bus it isn't lit; again takes it off.
    CHECK (strip.getLocalBounds().contains (strip.getRouteButton().getBounds()));
    CHECK (strip.getRouteButton().getRight() < strip.getSend (0).getX());
    auto& insert = *processor.getParameters().getParameter (ParamIDs::instInsert);
    strip.getRouteButton().onClick();
    CHECK (juce::roundToInt (insert.convertFrom0to1 (insert.getValue())) == 1);
    strip.getBusButton (2).onClick();   // bus C: its units, its sends
    CHECK (strip.getBus() == 2);
    CHECK (page.getInsertUnit (2, 0).isVisible());
    CHECK_FALSE (page.getInsertUnit (0, 0).isVisible());
    CHECK (strip.getSend (0).getParameterId() == ParamIDs::ins (2, "SendDelay"));
    CHECK_FALSE (strip.getRouteButton().getToggleState());   // it's on A, not C
    strip.getRouteButton().onClick();
    CHECK (juce::roundToInt (insert.convertFrom0to1 (insert.getValue())) == 3);
    strip.getRouteButton().onClick();
    CHECK (juce::roundToInt (insert.convertFrom0to1 (insert.getValue())) == 0);

    // POWER is each effect's ON on that bus; EDIT opens its page; BACK TO RACK returns.
    auto& driveOn = *processor.getParameters().getParameter (ParamIDs::ins (2, "DriveOn"));
    page.getInsertUnit (2, 0).getPower().toggle();
    CHECK (driveOn.getValue() > 0.5f);
    const FxPage::View views[] = { FxPage::View::insertDrive, FxPage::View::insertGlitch, FxPage::View::insertMod, FxPage::View::insertLoFi };
    for (int i = 0; i < 4; ++i)
    {
        page.getInsertUnit (2, i).getEdit().onClick();
        CHECK (page.getView() == views[i]);
        juce::Component& shownPage = i < 2 ? static_cast<juce::Component&> (page.getInsertEffectPage (2, i))
                                           : static_cast<juce::Component&> (page.getInsertModFxPage (2, i - 2));
        CHECK (shownPage.isVisible());
        if (i < 2)
        {
            auto& effectPage = page.getInsertEffectPage (2, i);
            for (auto* c : effectPage.getShownControls())
            {
                INFO (c->getParameterId());
                CHECK (c->getParameterId().startsWith ("ins3"));
                CHECK (effectPage.getLocalBounds().contains (c->getBounds()));
            }
            effectPage.onClose();
        }
        else
            page.getInsertModFxPage (2, i - 2).onClose();
        CHECK (page.getView() == FxPage::View::inserts);
    }

    // DRIVE's page shows what the model uses: ABYSS its EQ and no TONE; SKYLINE its ATTACK and GATE.
    auto set = [&] (const juce::String& id, float v) { auto* q = processor.getParameters().getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    auto& drivePage = page.getInsertEffectPage (2, 0);
    const auto shownIds = [&]
    {
        drivePage.resized();
        juce::StringArray ids;
        for (auto* c : drivePage.getShownControls())
            ids.add (c->getParameterId());
        return ids;
    };
    set (ParamIDs::ins (2, "DriveModel"), (float) (int) Model::abyss);
    auto ids = shownIds();
    CHECK (ids.contains (ParamIDs::ins (2, "DriveMidFreq")));
    CHECK_FALSE (ids.contains (ParamIDs::ins (2, "DriveTone")));
    set (ParamIDs::ins (2, "DriveModel"), (float) (int) Model::skyline);
    ids = shownIds();
    CHECK (ids.contains (ParamIDs::ins (2, "DriveAttack")));
    CHECK (ids.contains (ParamIDs::ins (2, "DriveTone")));
    CHECK_FALSE (ids.contains (ParamIDs::ins (2, "DriveHigh")));
}

TEST_CASE ("INSERTS: DRIVE's page draws what the drive does", "[inserts][ui]")
{
    ParagonX1AudioProcessor processor;
    auto set = [&] (const juce::String& id, float v) { auto* q = processor.getParameters().getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    DrivePreview preview (processor, 0);
    // BANSHEE clips both halves alike: odd harmonics, hardly any even; at DRIVE 0 the cycle out is nearly the sine in.
    set (ParamIDs::ins (0, "DriveModel"), (float) (int) Model::screamer);
    set (ParamIDs::ins (0, "DriveGain"), 10.0f);
    preview.render();
    const auto& h = preview.getHarmonicsDb();
    CHECK (h[2] > -40.0f);            // the 3rd
    CHECK (h[1] < h[2] - 20.0f);      // the 2nd far below
    // HALO clips one half harder: a strong 2nd.
    set (ParamIDs::ins (0, "DriveModel"), (float) (int) Model::halo);
    preview.render();
    CHECK (preview.getHarmonicsDb()[1] > -30.0f);
    CHECK (preview.getCycleOut().size() == 400);
}

TEST_CASE ("INSERTS: the DRIVE leaves no offset behind (RABID's charged capacitor is blocked)", "[inserts]")
{
    // A loud sound through RABID, ended: 3 seconds later the bus is silent (on PR7-G the amp blocks RABID's offset).
    Plugin p;
    p.set (ParamIDs::instInsert, 1.0f);
    p.set (ParamIDs::instLevel, 6.0f);
    p.driveOn (0, Model::rabid, 8.0f);
    const auto out = p.play (45, 0.5, 4.0);
    CHECK (peakOf (out[0], 3.5) < 1.0e-4);
    CHECK (peakOf (out[1], 3.5) < 1.0e-4);
}

TEST_CASE ("INSERTS: CPU of each DRIVE model, a side and a bus (both sides)", "[inserts][performance]")
{
    // Reported, not limited: PR7-G's circuits, solved at 8x. A bus runs one a side. Two inputs: a C major saw chord
    // at about a played level (what DRIVE's page quotes) and loud white noise, the circuits' worst case (HALO's solver
    // works hardest on it).
    for (int m = 0; m < effects::Drive::modelNames.size(); ++m)
        for (const auto noise : { false, true })
        {
            auto best = 1.0e9;
            for (int run = 0; run < 2; ++run)
            {
                auto drive = std::make_unique<effects::Drive>();
                drive->prepare (sampleRate, blockSize);
                effects::Drive::Settings s;
                s.on = true;
                s.model = (Model) m;
                s.drive = 7.0f;
                std::vector<float> x ((size_t) blockSize);
                juce::Random random (2);
                double phases[4] {};
                constexpr double chord[] = { 130.81, 164.81, 196.0, 261.63 };
                const auto started = juce::Time::getMillisecondCounterHiRes();
                for (int b = 0; b < (int) (2.0 * sampleRate) / blockSize; ++b)
                {
                    for (auto& v : x)
                    {
                        if (noise)
                        {
                            v = 0.3f * (random.nextFloat() - 0.5f);
                            continue;
                        }
                        auto sum = 0.0;
                        for (size_t k = 0; k < 4; ++k)
                        {
                            phases[k] += chord[k] / sampleRate;
                            phases[k] -= std::floor (phases[k]);
                            sum += 2.0 * phases[k] - 1.0;
                        }
                        v = (float) (0.5 * sum / 4.0);
                    }
                    float* channels[] = { x.data() };
                    drive->process (juce::dsp::AudioBlock<float> (channels, 1, (size_t) blockSize), s);
                }
                best = std::min (best, (juce::Time::getMillisecondCounterHiRes() - started) / 2000.0 * 100.0);
            }
            WARN (effects::Drive::modelNames[m] << (noise ? " (noise)" : " (chord)") << ": " << best
                                                << "% of one core a side, " << 2.0 * best << "% on a bus");
        }
}
