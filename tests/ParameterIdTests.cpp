// Every parameter ID is the instrument's or the rack's alone. A release build doesn't check this (JUCE's check is a debug
// assertion), and two parameters sharing an ID would save, load and automate one as the other.
#include "PluginProcessor.h"
#include <catch2/catch_test_macros.hpp>
#include <set>

TEST_CASE ("Parameter IDs: none shared between the instrument and the rack", "[processor]")
{
    ParagonX1AudioProcessor processor;
    std::set<juce::String> ids;
    for (auto* parameter : static_cast<juce::AudioProcessor&> (processor).getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
        {
            INFO (withId->paramID);
            CHECK (ids.insert (withId->paramID).second);
        }
    CHECK (ids.size() > 600);   // the instrument's, the rack (24 EQ bands...), the sends, MOD / LO-FI and the insert buses
    for (const auto* id : { ParamIDs::instLevel, ParamIDs::instPan, ParamIDs::instTranspose, ParamIDs::instTune, ParamIDs::instRelease,
                            ParamIDs::instVoices, ParamIDs::instSendDelay, ParamIDs::instSendReverb, ParamIDs::instInsert,
                            ParamIDs::midiChannel, ParamIDs::noteNames })
        CHECK (ids.count (id) == 1);
    for (const auto& id : ids)
    {
        INFO (id);
        CHECK_FALSE (id.startsWith ("drum"));   // the drums are gone
        CHECK_FALSE (id.startsWith ("mix"));
        CHECK_FALSE (id.startsWith ("dseq"));
    }
}
