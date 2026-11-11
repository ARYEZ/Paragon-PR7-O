#pragma once

#include "dsp/stage/Seating.h"

#include <juce_core/juce_core.h>


#include <map>
#include <optional>

// A preset ("rig" in the code, as across the PR7 family): one complete
// sound. The instrument, how it plays and the effects, and how it's filed (name, bank, number, tags).
//
// The OUTPUT knob isn't part of a preset: it suits the player's monitoring,
// not a particular sound, so switching presets leaves it alone. Each preset
// has its own LEVEL for balancing presets instead.
struct Rig
{
    static constexpr int numBanks = 5;          // A to E: the user's (and the first factory presets')
    static int numBanksWithExpansions();        // 0.13.0: A to E, then one bank per built-in expansion (Expansions.h)
    static bool isExpansionBank (int bankIndex) { return bankIndex >= numBanks; }
    static constexpr int maxNumber = 99;

    juce::String name { "New Rig" };
    int bank = 0;                               // 0 = A
    int number = 1;                             // 1 to maxNumber
    bool favourite = false;
    juce::StringArray tags;

    // Parameter values in their real units (dB, ms...), by parameter ID.
    // Storing real values rather than 0-1 positions means a rig still
    // loads correctly if a later version changes a knob's range. Loading a
    // rig resets any parameter it doesn't mention to its default, so rigs
    // saved before a parameter existed get that parameter's neutral setting.
    std::map<juce::String, float> parameters;

    // THE INSTRUMENT (PR7-O 0.1.0): the SFZ file it plays, by its full path (empty: none), loaded again from the
    // user's disk (a library can be gigabytes: the preset keeps where it is, not the audio). 0.13.0: or a library
    // reference ("{sfz:<library>}/<file>", library/Downloads.h); a section's text holds either.
    juce::String instrument;

    // THE STAGE (PR7-O 0.8.0): the seats moved off their usual place, by part name (none: every part in its usual seat).
    std::map<juce::String, stage::Seat> seats;

    // Free text shown with the rig: what it's for.
    juce::String notes;

    juce::String getSlotName() const;           // e.g. "A01" (an expansion's: its code, "VO01")
    static juce::String bankName (int bank);    // "A".."E", then the expansions' names

    std::unique_ptr<juce::XmlElement> toXml() const;
    // Returns nothing if the XML isn't a rig.
    static std::optional<Rig> fromXml (const juce::XmlElement&);

    static constexpr const char* fileExtension = ".pr7rig";
};
