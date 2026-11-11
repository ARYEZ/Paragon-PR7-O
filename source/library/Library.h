#pragma once

#include <juce_core/juce_core.h>

#include <vector>

// THE LIBRARY (PR7-O 0.11.0; the plan's stage 3, "an instrument browser by section"): the SFZ instruments in the
// folders the user adds (VSCO 2 CE, Sonatina Symphonic Orchestra, any plain WAV + SFZ library on their disk), sorted
// into SECTIONS (strings, brass, woodwinds, percussion, keys and harp, choir, other) and INSTRUMENTS (Violins, Solo
// Violin, Horns...) by their names: our own rule, from the names those libraries use. The folders are remembered in
// PR7-O's own settings file (every project, every instance). An #include folder's fragments ("includes") aren't listed.
namespace library
{
    enum class Section { strings, brass, woodwinds, percussion, keys, choir, other };
    inline constexpr int numSections = 7;
    juce::String nameOf (Section);

    struct Entry
    {
        juce::File file;
        juce::String library;       // the folder it was found under (its name)
        Section section = Section::other;
        juce::String instrument;    // "Violins", "Solo Violin", "Horns"...
        juce::String name;          // the file's name, without .sfz
        juce::String folder;        // the folder it's in under the library ("Strings - Performance"; empty: the top)
    };

    // Where an instrument file belongs, from its name (and its folder's, for what the name alone doesn't say).
    juce::String instrumentOf (const juce::String& name, const juce::String& folder = {});
    // 0.14.0 (the owner: "show plain section names"): a section part's name as the page shows it, by the same word
    // table: "ViolinEns-KS" -> "Violins", "Contrabass-KS" -> "Basses", "SViolin" -> "Solo Violin" (a name the table
    // doesn't know: itself, its "-KS" left off). Only for showing: seats and the rest go by the part's real name.
    juce::String partLabel (const juce::String& partName);
    Section sectionOf (const juce::String& name, const juce::String& folder = {});

    // Every instrument file under these folders, sorted by section, instrument, name.
    std::vector<Entry> scan (const juce::Array<juce::File>& folders);

    // The folders remembered.
    juce::Array<juce::File> folders();
    void setFolders (const juce::Array<juce::File>&);
}
