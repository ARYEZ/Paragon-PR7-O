// THE RACK (PR7-K's stage 7a): PR7-E 0.18.1's ECHELON, DOMINION, CONSTRICTOR and LIMITER after the instrument,
// measured from what the plugin plays (steady sines as the instrument's regions: PluginTestUtils.h). (Each effect's own tests came with it from PR7-E: EqTests, DominionTests,
// CompressorTests, MultibandTests, LimiterTests, MasterLimiterTests.)
#include "TestUtils.h"
#include "PluginTestUtils.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/FxPage.h"
#include "ui/MainView.h"

#include <set>

namespace
{
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
        // Each note's own pan and level (by its place in `play`'s list), as PR7-D's drum slots had.
        std::map<int, std::pair<float, float>> voices;
        // Notes (MIDI note numbers, as pitches) held for `seconds`: each a sine at that pitch, a region of its own on its
        // key. Both channels.
        std::array<std::vector<float>, 2> play (std::initializer_list<int> notes, double seconds, float velocity = 0.8f)
        {
            juce::MidiBuffer starts;   // as MIDI from the DAW (the on-screen keyboard's presses land by the clock: not repeatable)
            std::vector<testkit::Tone> tones;
            std::set<int> keys;
            int index = 0;
            for (const auto note : notes)
            {
                const auto [pan, level] = voices.count (index) ? voices[index] : std::pair { 0.0f, 0.0f };
                tones.push_back ({ note, testkit::sine (noteHz (note), seconds + 1.0), pan, level });
                ++index;
                if (keys.insert (note).second)
                    starts.addEvent (juce::MidiMessage::noteOn (1, note, velocity), 0);
            }
            testkit::useTones (processor, tones);
            std::array<std::vector<float>, 2> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int done = 0; done < (int) (seconds * sampleRate); done += blockSize)
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

    // A note's level in the last half second (after the rack has settled).
    double toneAt (const std::vector<float>& x, double hz)
    {
        const auto length = (size_t) (0.5 * sampleRate);
        return levelAt (x, hz, x.size() - length, length);
    }
    double peakOf (const std::vector<float>& x, size_t from = 0)
    {
        auto peak = 0.0f;
        for (auto i = from; i < x.size(); ++i)
            peak = std::max (peak, std::abs (x[i]));
        return peak;
    }
}

TEST_CASE ("RACK: off, it changes nothing, sample for sample, even with its settings made", "[rack]")
{
    Plugin plain;
    const auto reference = plain.play ({ 57, 64 }, 0.5);

    Plugin set;
    set.set (ParamIDs::eqBand (0, "Used"), 1.0f);
    set.set (ParamIDs::eqBand (0, "Gain"), -9.0f);
    set.set (ParamIDs::domDepth, 15.0f);
    set.set (ParamIDs::compThreshold, -40.0f);
    set.set (ParamIDs::limitGain, 12.0f);
    const auto out = set.play ({ 57, 64 }, 0.5);
    REQUIRE (out[0].size() == reference[0].size());
    CHECK (out[0] == reference[0]);
    CHECK (out[1] == reference[1]);
    CHECK (set.processor.getLatencySamples() == 0);
}

TEST_CASE ("RACK: ECHELON cuts the band it's set to, on the kit", "[rack]")
{
    // A4 (440 Hz) and E5 (659 Hz) sines; a -12 dB bell on 440 Hz takes A4 down 12 dB and leaves E5.
    Plugin dry;
    const auto before = dry.play ({ 69, 76 }, 1.0);
    Plugin cut;
    cut.set (ParamIDs::eqOn, 1.0f);
    cut.set (ParamIDs::eqBand (0, "Used"), 1.0f);
    cut.set (ParamIDs::eqBand (0, "Freq"), 440.0f);
    cut.set (ParamIDs::eqBand (0, "Gain"), -12.0f);
    cut.set (ParamIDs::eqBand (0, "Q"), 8.0f);
    const auto after = cut.play ({ 69, 76 }, 1.0);
    for (int c = 0; c < 2; ++c)
    {
        CHECK_THAT (db (toneAt (after[(size_t) c], 440.0) / toneAt (before[(size_t) c], 440.0)), WithinAbs (-12.0, 0.5));
        CHECK_THAT (db (toneAt (after[(size_t) c], noteHz (76)) / toneAt (before[(size_t) c], noteHz (76))), WithinAbs (0.0, 0.5));
    }
}

TEST_CASE ("RACK: CONSTRICTOR turns a loud sound down; the LIMITER holds its ceiling", "[rack]")
{
    Plugin dry;
    const auto before = dry.play ({ 48, 55, 60, 64, 67 }, 1.0, 1.0f);
    Plugin squeezed;
    squeezed.set (ParamIDs::compOn, 1.0f);
    squeezed.set (ParamIDs::compThreshold, -30.0f);
    squeezed.set (ParamIDs::compRatio, 4.0f);
    const auto after = squeezed.play ({ 48, 55, 60, 64, 67 }, 1.0, 1.0f);
    const auto tail = (size_t) (0.5 * sampleRate);
    const auto change = db (rms (after[0], tail, after[0].size()) / rms (before[0], tail, before[0].size()));
    WARN ("CONSTRICTOR -30 dB 4:1 on five sines at once: " << change << " dB");
    CHECK (change < -6.0);
    CHECK (squeezed.processor.getCompressorReductionDb (0) > 6.0f);

    // The LIMITER (MODERN, OUTPUT -1 dB) after OUTPUT +12 dB and 12 dB of GAIN: no sample over -1 dBFS.
    Plugin loud;
    loud.set (ParamIDs::outputGain, 12.0f);
    loud.set (ParamIDs::limitOn, 1.0f);
    loud.set (ParamIDs::limitGain, 12.0f);
    loud.set (ParamIDs::limitCeiling, -1.0f);
    const auto limited = loud.play ({ 48, 55, 60, 64, 67 }, 1.0, 1.0f);
    for (int c = 0; c < 2; ++c)
    {
        const auto peak = db (peakOf (limited[(size_t) c], (size_t) (0.05 * sampleRate)));
        WARN ("LIMITER peak: " << peak << " dBFS");
        CHECK (peak <= -1.0 + 0.01);
        CHECK (peak > -2.0);   // and it is loud: right up at the ceiling
    }
}

TEST_CASE ("RACK: left and right stay apart through all four", "[rack]")
{
    // A sound on one side only, through every effect (unlinked: each side its own gain; DOMINION in L/R): the
    // other side stays exactly silent, each way round. (Measured this way because a level meter on one note
    // picks up a trace of a much louder other note on the same channel: a test of the meter, not the rack.)
    for (const auto side : { 0, 1 })
    {
        Plugin p;
        p.voices[0] = { side == 0 ? -1.0f : 1.0f, 0.0f };
        p.set (ParamIDs::eqOn, 1.0f);
        p.set (ParamIDs::eqBand (0, "Used"), 1.0f);
        p.set (ParamIDs::eqBand (0, "Freq"), 1000.0f);
        p.set (ParamIDs::eqBand (0, "Gain"), 4.0f);
        p.set (ParamIDs::eqBand (0, "Dyn"), -4.0f);
        p.set (ParamIDs::domOn, 1.0f);
        p.set (ParamIDs::domStereo, 0.0f);   // L/R
        p.set (ParamIDs::domLink, 0.0f);
        p.set (ParamIDs::compOn, 1.0f);
        p.set (ParamIDs::compThreshold, -30.0f);
        p.set (ParamIDs::compLink, 0.0f);
        p.set (ParamIDs::limitOn, 1.0f);
        p.set (ParamIDs::limitGain, 10.0f);
        p.set (ParamIDs::limitLinkTransient, 0.0f);
        p.set (ParamIDs::limitLinkRelease, 0.0f);
        const auto out = p.play ({ 69 }, 1.0);
        CHECK (peakOf (out[(size_t) side]) > 0.4);         // the sound, loud (driven into the LIMITER)
        CHECK (peakOf (out[(size_t) (1 - side)]) == 0.0);  // the other side: nothing at all
    }

    // LINK (on by default) turns both sides down together, so the stereo picture doesn't move: the quiet right side
    // is turned down with the loud left one.
    Plugin linked;
    linked.voices[0] = { -1.0f, 0.0f };
    linked.voices[1] = { 1.0f, -20.0f };
    linked.set (ParamIDs::compOn, 1.0f);
    linked.set (ParamIDs::compThreshold, -30.0f);
    linked.set (ParamIDs::compRatio, 10.0f);
    linked.play ({ 69, 69 }, 1.0);
    CHECK (linked.processor.getCompressorReductionDb (1) > 10.0f);
}

TEST_CASE ("RACK: the delay it adds is told to the DAW", "[rack]")
{
    Plugin p;
    p.processor.updateLatency();
    CHECK (p.processor.getLatencySamples() == 0);
    p.set (ParamIDs::limitOn, 1.0f);   // MODERN, 1 ms look-ahead
    p.processor.updateLatency();
    CHECK (p.processor.getLatencySamples() == 48);
    p.set (ParamIDs::mbLookahead, 1.0f);   // CONSTRICTOR's look-ahead: 20 ms more
    p.processor.updateLatency();
    CHECK (p.processor.getLatencySamples() == 48 + 960);
    p.set (ParamIDs::limitOn, 0.0f);
    p.set (ParamIDs::mbLookahead, 0.0f);
    p.processor.updateLatency();
    CHECK (p.processor.getLatencySamples() == 0);
}

TEST_CASE ("RACK: a preset keeps the rack's settings; CONSTRICTOR's and DOMINION's own presets load", "[rack][rigs]")
{
    Plugin p;
    p.set (ParamIDs::eqOn, 1.0f);
    p.set (ParamIDs::eqBand (5, "Used"), 1.0f);
    p.set (ParamIDs::eqBand (5, "Freq"), 2500.0f);
    p.set (ParamIDs::limitOn, 1.0f);
    p.set (ParamIDs::limitCeiling, -2.0f);
    p.set (ParamIDs::limitUnity, 1.0f);   // a listening aid: not part of a preset
    const auto rig = p.processor.captureRig();
    CHECK (rig.parameters.at (ParamIDs::eqOn) == 1.0f);
    CHECK_THAT (rig.parameters.at (ParamIDs::eqBand (5, "Freq")), Catch::Matchers::WithinRel (2500.0f, 0.001f));
    CHECK_THAT (rig.parameters.at (ParamIDs::limitCeiling), WithinAbs (-2.0, 1.0e-4));   // (through the 0-1 scale: within a hair)
    CHECK (rig.parameters.count (ParamIDs::limitUnity) == 0);

    // Saved as a preset, then loaded into another instance.
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonRackTestPresets");
    folder.deleteRecursively();
    p.processor.useRigFolder (folder);
    REQUIRE (p.processor.saveCurrentRigAs ("Racked"));
    Plugin other;
    other.processor.useRigFolder (folder);
    const RigLibrary::Entry* saved = nullptr;
    for (const auto& entry : other.processor.getRigLibrary().getEntries())
        if (entry.rig.name == "Racked")
            saved = &entry;
    REQUIRE (saved != nullptr);
    other.processor.loadRig (*saved);
    const auto value = [&] (const juce::String& id) { return other.processor.getParameters().getRawParameterValue (id)->load(); };
    CHECK (value (ParamIDs::eqOn) == 1.0f);
    CHECK_THAT (value (ParamIDs::eqBand (5, "Freq")), Catch::Matchers::WithinRel (2500.0f, 0.001f));
    CHECK_THAT (value (ParamIDs::limitCeiling), WithinAbs (-2.0, 1.0e-4));

    // Each effect's own factory presets (its settings, not whether it's on).
    const auto& comp = CompPresets::factory();
    REQUIRE (comp.size() > 3);
    other.processor.loadCompPreset (comp[2]);
    CHECK (other.processor.getCompPresetName() == comp[2].name);
    CHECK_FALSE (other.processor.isCompPresetModified());
    const auto& dom = DominionPresets::factory();
    REQUIRE (dom.size() > 3);
    other.processor.loadDominionPreset (dom[3]);
    CHECK (other.processor.getDominionPresetName() == dom[3].name);
    CHECK_FALSE (other.processor.isDominionPresetModified());
    folder.deleteRecursively();
}

TEST_CASE ("RACK: CPU, four sounds through all four, and nothing when they're off", "[rack][performance]")
{
    const auto cpu = [] (bool on)
    {
        auto best = 1.0e9;
        for (int run = 0; run < 3; ++run)   // the fastest of three: a busy machine can only slow a run down
        {
            Plugin p;
            if (on)
            {
                p.set (ParamIDs::eqOn, 1.0f);
                for (int band = 0; band < 6; ++band)
                {
                    p.set (ParamIDs::eqBand (band, "Used"), 1.0f);
                    p.set (ParamIDs::eqBand (band, "Freq"), 100.0f * (float) std::pow (2.5, band));
                    p.set (ParamIDs::eqBand (band, "Gain"), band % 2 == 0 ? 3.0f : -3.0f);
                    p.set (ParamIDs::eqBand (band, "Dyn"), band == 3 ? -4.0f : 0.0f);
                }
                p.set (ParamIDs::domOn, 1.0f);
                p.set (ParamIDs::compOn, 1.0f);
                for (int band = 0; band < 3; ++band)
                    p.set (ParamIDs::mbBand (band, "Used"), 1.0f);
                p.set (ParamIDs::limitOn, 1.0f);
                p.set (ParamIDs::limitTruePeak, 1.0f);
            }
            const auto started = juce::Time::getMillisecondCounterHiRes();
            p.play ({ 48, 55, 60, 64 }, 4.0);
            best = std::min (best, (juce::Time::getMillisecondCounterHiRes() - started) / 4000.0 * 100.0);
        }
        return best;
    };
    const auto off = cpu (false), on = cpu (true);
    WARN ("four sines, the rack off: " << off << "% of one core; all four on (6 EQ bands, DOMINION, 3 bands and CONSTRICTOR, "
                                         "the LIMITER with TRUE PEAK): " << on << "%");
    // Measured in PR7-K (0.17.0) on a saw chord, each alone: ECHELON with 6 bands +0.4% (dynamic +0.9%), DOMINION +10.6%
    // (its bank of about 120 detectors, solved every block: by far the heaviest), CONSTRICTOR +0.3% (with 3 bands
    // +1.3%), the LIMITER with TRUE PEAK +3.9%; off, each costs nothing.
    CHECK (on < 30.0);   // generous for CI machines
}

TEST_CASE ("The FX page: four rack units in the chain's order, each opening its page; laid out cleanly", "[rack][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    view->showPage (MainView::Page::fx);
    auto& page = view->getFxPage();
    CHECK (page.isVisible());
    CHECK (page.getView() == FxPage::View::rack);

    const char* names[] = { "ECHELON", "DOMINION", "CONSTRICTOR", "LIMITER" };
    for (int i = 0; i < 4; ++i)
    {
        auto& unit = page.getUnit (i);
        INFO (unit.getBounds().toString());
        CHECK (unit.getSpec().name == names[i]);
        CHECK (unit.isVisible());
        CHECK (page.getLocalBounds().contains (unit.getBounds()));
        CHECK (unit.getWidth() > 1000);
        if (i > 0)
            CHECK (unit.getY() >= page.getUnit (i - 1).getBottom());
    }

    // POWER switches the effect (ECHELON's is eqOn).
    auto& eqOn = *processor.getParameters().getParameter (ParamIDs::eqOn);
    CHECK (eqOn.getValue() < 0.5f);
    page.getUnit (0).getPower().toggle();
    CHECK (eqOn.getValue() > 0.5f);

    // EDIT opens each page in the rack's place; BACK TO RACK comes back.
    const FxPage::View views[] = { FxPage::View::eq, FxPage::View::dominion, FxPage::View::comp, FxPage::View::limiter };
    std::vector<juce::Component*> pages { &page.getEqPage(), &page.getDominionPage(), &page.getCompPage(), &page.getLimiterPage() };
    for (int i = 0; i < 4; ++i)
    {
        page.getUnit (i).getEdit().onClick();
        CHECK (page.getView() == views[i]);
        CHECK (pages[(size_t) i]->isVisible());
        CHECK_FALSE (page.getUnit (i).isVisible());
        CHECK (pages[(size_t) i]->getBounds() == page.getLocalBounds());
        // Every control on the page inside it.
        for (auto* child : pages[(size_t) i]->getChildren())
            if (child->isVisible())
            {
                INFO (names[i] << ": " << child->getBounds().toString());
                CHECK (pages[(size_t) i]->getLocalBounds().contains (child->getBounds()));
            }
        page.show (FxPage::View::rack);
        CHECK (page.getUnit (i).isVisible());
    }
}
