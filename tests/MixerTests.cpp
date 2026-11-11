// THE MIXER (0.15.0): a strip per part and the section's, measured on the plugin's output.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginProcessor.h"
#include "Parameters.h"
#include "PluginTestUtils.h"
#include "TestUtils.h"
#include "dsp/mixer/MixerEffects.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double rate = 48000.0;

    struct Out { std::vector<float> left, right; };

    // A section of two parts written by the test: CELLOS 220 Hz on keys 36-59, VIOLINS 660 Hz on 60-90 (each rooted so
    // its key plays it as recorded), amp_veltrack 0.
    struct Rig2
    {
        juce::TemporaryFile folder;
        ParagonX1AudioProcessor processor;
        Rig2()
        {
            const auto dir = folder.getFile();
            dir.createDirectory();
            testkit::writeWav (dir.getChildFile ("low.wav"), *testkit::sine (220.0, 1.8), 24);   // (under 2 s: kept in memory, not streamed)
            testkit::writeWav (dir.getChildFile ("high.wav"), *testkit::sine (660.0, 1.8), 24);
            dir.getChildFile ("ViolinEns.sfz").replaceWithText ("<region> sample=high.wav lokey=60 hikey=90 pitch_keycenter=72 amp_veltrack=0\n");
            dir.getChildFile ("CelloEns.sfz").replaceWithText ("<region> sample=low.wav lokey=36 hikey=59 pitch_keycenter=48 amp_veltrack=0\n");
            processor.setPlayConfigDetails (0, 2, rate, 512);
            processor.prepareToPlay (rate, 512);
            processor.loadSection (sampler::section::fromFiles ({ dir.getChildFile ("CelloEns.sfz").getFullPathName(),
                                                                  dir.getChildFile ("ViolinEns.sfz").getFullPathName() }));
            REQUIRE (processor.waitForInstrument());
            juce::Timer::callPendingTimersSynchronously();
        }
        // Both notes (cellos 48, violins 72) for a second.
        Out play (int samples = 48000)
        {
            Out out { std::vector<float> ((size_t) samples), std::vector<float> ((size_t) samples) };
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
            for (int at = 0; at < samples; at += 512)
            {
                juce::AudioBuffer<float> buffer (2, 512);
                juce::MidiBuffer none;
                processor.processBlock (buffer, at == 0 ? midi : none);
                const auto n = std::min (512, samples - at);
                std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, out.left.begin() + at);
                std::copy (buffer.getReadPointer (1), buffer.getReadPointer (1) + n, out.right.begin() + at);
            }
            juce::MidiBuffer off;
            off.addEvent (juce::MidiMessage::allSoundOff (1), 0);
            for (int i = 0; i < 20; ++i)
            {
                juce::AudioBuffer<float> rest (2, 512);
                juce::MidiBuffer none;
                processor.processBlock (rest, i == 0 ? off : none);
            }
            return out;
        }
        // Each part's level (its own pitch) in one side, over the second half (past every ramp).
        static double cellos (const std::vector<float>& x) { return levelAt (x, 220.0, 24000, 24000); }
        static double violins (const std::vector<float>& x) { return levelAt (x, 660.0, 24000, 24000); }
        void strip (const juce::String& part, const std::function<void (mixer::StripSettings&)>& change)
        {
            auto s = processor.getStrip (part);
            change (s);
            processor.setStrip (part, s);
        }
    };
}

TEST_CASE ("MIXER 0.15.0: a strip per part, by the section's part names; plain strips leave the sound as it was", "[mixer][processor]")
{
    Rig2 r;
    const auto parts = r.processor.getStageParts();
    REQUIRE (parts.size() == 2);
    CHECK (parts[0].name == "CelloEns");
    CHECK (parts[1].name == "ViolinEns");
    CHECK (r.processor.getStrip ("CelloEns").isPlain());
    const auto before = r.play();
    CHECK (Rig2::cellos (before.left) > 0.05);
    CHECK (Rig2::violins (before.left) > 0.05);

    // Changed and back to plain: the same sound, sample for sample.
    r.strip ("CelloEns", [] (auto& s) { s.levelDb = -6.0f; });
    r.play();
    r.strip ("CelloEns", [] (auto& s) { s.levelDb = 0.0f; });
    r.play();   // (past the ramp back)
    const auto after = r.play();
    for (size_t i = 0; i < before.left.size(); i += 97)
        REQUIRE (after.left[i] == before.left[i]);
}

TEST_CASE ("MIXER 0.15.0: LEVEL, PAN, MUTE, SOLO and PHASE do what they say to their part only", "[mixer][processor]")
{
    Rig2 r;
    const auto plain = r.play();
    const auto cellos = Rig2::cellos (plain.left), violins = Rig2::violins (plain.left);

    // LEVEL: the cellos 6 dB down; the violins as they were.
    r.strip ("CelloEns", [] (auto& s) { s.levelDb = -6.0f; });
    auto out = r.play();
    CHECK_THAT (db (Rig2::cellos (out.left) / cellos), WithinAbs (-6.0, 0.1));
    CHECK_THAT (db (Rig2::violins (out.left) / violins), WithinAbs (0.0, 0.05));
    // Off at the bottom.
    r.strip ("CelloEns", [] (auto& s) { s.levelDb = mixer::levelOffDb; });
    out = r.play();
    CHECK (Rig2::cellos (out.left) < cellos * 1.0e-3);

    // PAN: the cellos hard right: gone from the left, as they were on the right.
    r.strip ("CelloEns", [] (auto& s) { s.levelDb = 0.0f; s.pan = 1.0f; });
    out = r.play();
    CHECK (Rig2::cellos (out.left) < cellos * 1.0e-3);
    CHECK_THAT (db (Rig2::cellos (out.right) / Rig2::cellos (plain.right)), WithinAbs (0.0, 0.05));

    // MUTE: the cellos silent, the violins not.
    r.strip ("CelloEns", [] (auto& s) { s.pan = 0.0f; s.mute = true; });
    out = r.play();
    CHECK (Rig2::cellos (out.left) < cellos * 1.0e-3);
    CHECK_THAT (db (Rig2::violins (out.left) / violins), WithinAbs (0.0, 0.05));

    // SOLO: the violins soloed (the cellos unmuted): only the violins.
    r.strip ("CelloEns", [] (auto& s) { s.mute = false; });
    r.strip ("ViolinEns", [] (auto& s) { s.solo = true; });
    out = r.play();
    CHECK (Rig2::cellos (out.left) < cellos * 1.0e-3);
    CHECK_THAT (db (Rig2::violins (out.left) / violins), WithinAbs (0.0, 0.05));

    // PHASE: the violins alone, flipped: every sample the other way up.
    r.strip ("CelloEns", [] (auto& s) { s.mute = true; });
    r.strip ("ViolinEns", [] (auto& s) { s.solo = false; });
    const auto upright = r.play();
    r.strip ("ViolinEns", [] (auto& s) { s.invert = true; });
    r.play();   // (past the 20 ms turn)
    const auto flipped = r.play();
    for (size_t i = 24000; i < 48000; i += 89)
        REQUIRE (flipped.left[i] == -upright.left[i]);

    // The SECTION strip: after the stage, everything 6 dB down.
    r.strip ("CelloEns", [] (auto& s) { s = {}; });
    r.strip ("ViolinEns", [] (auto& s) { s = {}; });
    auto section = r.processor.getSectionStrip();
    section.levelDb = -6.0f;
    r.processor.setSectionStrip (section);
    out = r.play();
    CHECK_THAT (db (Rig2::cellos (out.left) / cellos), WithinAbs (-6.0, 0.1));
    CHECK_THAT (db (Rig2::violins (out.left) / violins), WithinAbs (-6.0, 0.1));
}

TEST_CASE ("MIXER 0.15.0: an effect in a part's slot shapes that part only; a send reaches its effect while it's on", "[mixer][processor]")
{
    Rig2 r;
    const auto plain = r.play();

    // An EQ with LOW CUT at 1 kHz in the cellos' first slot: the cellos (220 Hz) far down, the violins (660 Hz) not
    // touched (they're another part).
    r.strip ("CelloEns", [] (auto& s)
    {
        s.fx[0] = mixerfx::defaultSlot (mixerfx::eq);
        s.fx[0].values[0] = 1000.0f;   // LOW CUT
    });
    auto out = r.play();
    CHECK (db (Rig2::cellos (out.left) / Rig2::cellos (plain.left)) < -12.0);
    CHECK_THAT (db (Rig2::violins (out.left) / Rig2::violins (plain.left)), WithinAbs (0.0, 0.05));
    // Switched off: the cellos as they were (its settings kept).
    r.strip ("CelloEns", [] (auto& s) { s.fx[0].on = false; });
    r.play();
    out = r.play();
    CHECK_THAT (db (Rig2::cellos (out.left) / Rig2::cellos (plain.left)), WithinAbs (0.0, 0.1));

    // SEND REVERB from the violins: with REVERB on, the tail rings on after the notes stop; without the send, it
    // doesn't (the instrument's own sends are at 0).
    r.strip ("CelloEns", [] (auto& s) { s = {}; });
    testkit::set (r.processor, ParamIDs::reverbOn, 1.0f);
    const auto tail = [&r]
    {
        r.play (24000);
        // (play stops the notes and runs 20 blocks; the reverb's tail is what's left in the next half second)
        double energy = 0.0;
        for (int i = 0; i < 40; ++i)
        {
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer none;
            r.processor.processBlock (b, none);
            energy += b.getRMSLevel (0, 0, 512);
        }
        return energy;
    };
    const auto dry = tail();
    r.strip ("ViolinEns", [] (auto& s) { s.reverbDb = 0.0f; });
    const auto wet = tail();
    CHECK (wet > dry * 10.0 + 1.0e-4);
}

TEST_CASE ("MIXER 0.15.0: the strips are kept in projects and presets, and undo takes a change back", "[mixer][processor]")
{
    Rig2 r;
    mixer::StripSettings violins;
    violins.levelDb = -4.5f;
    violins.pan = -0.3f;
    violins.solo = true;
    violins.reverbDb = -10.0f;
    violins.fx[1] = mixerfx::defaultSlot (mixerfx::compressor);
    violins.fx[1].on = false;
    violins.fx[1].outputDb = 2.0f;
    r.processor.setStrip ("ViolinEns", violins);
    mixer::StripSettings section;
    section.levelDb = -2.0f;
    section.fx[0] = mixerfx::defaultSlot (mixerfx::eq);
    r.processor.setSectionStrip (section);

    // The project.
    juce::MemoryBlock state;
    r.processor.getStateInformation (state);
    ParagonX1AudioProcessor other;
    other.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (other.getStrip ("ViolinEns") == violins);
    CHECK (other.getStrip ("CelloEns").isPlain());
    CHECK (other.getSectionStrip() == section);

    // Undo takes a strip's change back; redo puts it again.
    r.processor.flushUndoStep();
    auto changed = violins;
    changed.levelDb = -12.0f;
    r.processor.setStrip ("ViolinEns", changed);
    r.processor.flushUndoStep();
    r.processor.undo();
    CHECK (r.processor.getStrip ("ViolinEns") == violins);
    r.processor.redo();
    CHECK (r.processor.getStrip ("ViolinEns") == changed);
    r.processor.setStrip ("ViolinEns", violins);
    r.processor.flushUndoStep();

    // A preset, written and read back.
    const auto rig = r.processor.captureRig();
    const auto back = Rig::fromXml (*rig.toXml());
    REQUIRE (back.has_value());
    CHECK (back->strips.at ("ViolinEns") == violins);
    CHECK (back->sectionStrip == section);

    // Through the preset library: a preset saved before the strips were set loads them plain, one saved after loads
    // them back.
    juce::TemporaryFile rigs;
    rigs.getFile().createDirectory();
    r.processor.useRigFolder (rigs.getFile());
    REQUIRE (r.processor.saveCurrentRigAs ("Mixed"));
    r.processor.setStrip ("ViolinEns", {});
    r.processor.setSectionStrip ({});
    REQUIRE (r.processor.saveCurrentRigAs ("Plain"));
    const auto entry = [&r] (const juce::String& name) -> const RigLibrary::Entry&
    {
        for (const auto& e : r.processor.getRigLibrary().getEntries())
            if (e.rig.name == name)
                return e;
        FAIL ("no preset " << name);
        return r.processor.getRigLibrary().getEntries().front();
    };
    r.processor.loadRig (entry ("Mixed"));
    CHECK (r.processor.getStrip ("ViolinEns") == violins);
    CHECK (r.processor.getSectionStrip() == section);
    r.processor.loadRig (entry ("Plain"));
    CHECK (r.processor.getStrip ("ViolinEns").isPlain());
    CHECK (r.processor.getSectionStrip().isPlain());
}

TEST_CASE ("STAGE, fixed in 0.15.0: a seat moved alone is one undo step (it wasn't compared: undo skipped it)", "[stage][undo]")
{
    Rig2 r;
    r.processor.flushUndoStep();
    const auto before = r.processor.getStageParts()[1].seat;
    r.processor.setSeat ("ViolinEns", { before.x + 2.0f, before.y + 1.0f, before.width });
    r.processor.seatMoveFinished();
    r.processor.flushUndoStep();
    CHECK (r.processor.getStageParts()[1].moved);
    r.processor.undo();
    CHECK_FALSE (r.processor.getStageParts()[1].moved);
    CHECK_THAT (r.processor.getStageParts()[1].seat.x, WithinAbs (before.x, 1.0e-4));
}
