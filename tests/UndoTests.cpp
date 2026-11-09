// UNDO / REDO.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "rigs/UndoHistory.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    struct TestRig
    {
        TestRig()
        {
            folder.createDirectory();
            processor.useRigFolder (folder);
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
        }
        ~TestRig() { folder.deleteRecursively(); }

        juce::RangedAudioParameter& parameter (const juce::String& id) { return *processor.getParameters().getParameter (id); }
        float value (const juce::String& id) { return parameter (id).convertFrom0to1 (parameter (id).getValue()); }

        // A knob turned with the mouse: a gesture around the change, as the UI does.
        void turn (const juce::String& id, float plainValue)
        {
            auto& p = parameter (id);
            p.beginChangeGesture();
            p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
            p.endChangeGesture();
            processor.flushUndoStep();
        }

        juce::File folder { juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ParagonX1-perf-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())) };
        ParagonX1AudioProcessor processor;
    };
}

TEST_CASE ("Undo history steps back and forward and forgets redo after a new edit", "[undo]")
{
    UndoHistory<int> history;
    history.record (1);
    CHECK_FALSE (history.canUndo());
    history.record (2);
    history.record (2);   // no change: no extra step
    history.record (3);

    CHECK (*history.undo() == 2);
    CHECK (*history.undo() == 1);
    CHECK_FALSE (history.undo().has_value());
    CHECK (*history.redo() == 2);

    history.record (9);   // a new edit after undoing
    CHECK_FALSE (history.canRedo());
    CHECK (*history.undo() == 2);

    for (int i = 0; i < 500; ++i)
        history.record (100 + i);
    int steps = 0;
    while (history.undo())
        ++steps;
    CHECK (steps == (int) UndoHistory<int>::maxSteps - 1);
}

namespace
{
    const juce::String someTime = ParamIDs::instTune;        // cents, 0 by default
    const juce::String someFine = ParamIDs::instTranspose;   // semitones, 0 by default
}

TEST_CASE ("Undo and redo take back whole knob moves", "[undo]")
{
    TestRig rig;
    rig.turn (someTime, 8.0f);
    rig.turn (someFine, 6.0f);
    REQUIRE (rig.processor.canUndo());

    rig.processor.undo();
    CHECK_THAT (rig.value (someFine), WithinAbs (0.0, 1.0e-4));   // back to the default
    CHECK_THAT (rig.value (someTime), WithinAbs (8.0, 1.0e-3));   // untouched

    rig.processor.undo();
    CHECK_THAT (rig.value (someTime), WithinAbs (0.0, 1.0e-4));
    CHECK_FALSE (rig.processor.canUndo());

    rig.processor.redo();
    rig.processor.redo();
    CHECK_THAT (rig.value (someTime), WithinAbs (8.0, 1.0e-3));
    CHECK_THAT (rig.value (someFine), WithinAbs (6.0, 1.0e-4));
    CHECK_FALSE (rig.processor.canRedo());
}

TEST_CASE ("Undo doesn't take back the global settings", "[undo]")
{
    TestRig rig;
    rig.turn (someTime, 7.0f);
    rig.turn (ParamIDs::outputGain, -9.0f);
    rig.turn (ParamIDs::limitUnity, 1.0f);

    rig.processor.undo();
    CHECK_THAT (rig.value (someTime), WithinAbs (0.0, 1.0e-4));
    CHECK_THAT (rig.value (ParamIDs::outputGain), WithinAbs (-9.0, 1.0e-4));
    CHECK (rig.value (ParamIDs::limitUnity) > 0.5f);
}

TEST_CASE ("Undoing a preset change brings back the previous sound", "[undo][rigs]")
{
    TestRig rig;
    rig.turn (someFine, 5.0f);
    CHECK (rig.processor.isRigModified());

    const auto* init = rig.processor.getRigLibrary().findFactory ("Init");
    REQUIRE (init != nullptr);
    rig.processor.loadRig (*init);
    rig.processor.flushUndoStep();
    CHECK_THAT (rig.value (someFine), WithinAbs (0.0, 1.0e-4));
    CHECK_FALSE (rig.processor.isRigModified());

    rig.processor.undo();
    CHECK_THAT (rig.value (someFine), WithinAbs (5.0, 1.0e-4));
    CHECK (rig.processor.isRigModified());   // it was edited before the preset change

    rig.processor.redo();
    CHECK_THAT (rig.value (someFine), WithinAbs (0.0, 1.0e-4));
    CHECK_FALSE (rig.processor.isRigModified());
}

TEST_CASE ("A new PR7-O starts on the Init preset", "[undo][rigs]")
{
    ParagonX1AudioProcessor processor;
    CHECK (processor.getCurrentRig().name == "Init");
    const auto* entry = processor.findCurrentRigEntry();
    REQUIRE (entry != nullptr);
    CHECK (entry->isFactory);
    CHECK_FALSE (processor.isRigModified());
}
