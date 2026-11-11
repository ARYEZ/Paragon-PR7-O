// 0.11.0: THE LIBRARY and its browser. The file names here are invented, written as VSCO 2 CE's and Sonatina's are
// (no library is ever committed).
#include <catch2/catch_test_macros.hpp>

#include "library/Library.h"
#include "ui/LibraryBrowser.h"

namespace
{
    struct Libraries
    {
        Libraries() { root.createDirectory(); }
        ~Libraries() { root.deleteRecursively(); }
        juce::File root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonPR7OLibrary").getChildFile (juce::Uuid().toString());
        juce::File add (const juce::String& path)
        {
            const auto file = root.getChildFile (path);
            file.getParentDirectory().createDirectory();
            file.replaceWithText ("<region> sample=a.wav\n");
            return file;
        }
    };
}

TEST_CASE ("The library sorts instruments by their names", "[library]")
{
    using library::Section;
    struct Case { const char* name; const char* folder; const char* instrument; Section section; };
    const Case cases[] {
        { "ViolinEns-KS", "", "Violins", Section::strings },
        { "SViolin-KS", "", "Solo Violin", Section::strings },
        { "CelloEns-SusVib", "", "Cellos", Section::strings },
        { "SCello-Pizz", "", "Solo Cello", Section::strings },
        { "Contrabass-Sus", "", "Basses", Section::strings },
        { "1st Violins Sustain", "Strings", "1st Violins", Section::strings },
        { "Bass Clarinet Sustain", "Woodwinds", "Bass Clarinet", Section::woodwinds },
        { "Bassoon-Stac", "", "Bassoons", Section::woodwinds },
        { "English Horn Sus", "", "English Horn", Section::woodwinds },   // (not a horn)
        { "Horn Solo Legato", "Brass", "Solo Horn", Section::brass },
        { "FHornSus", "", "Horns", Section::brass },
        { "Bass Trombone", "", "Bass Trombone", Section::brass },
        { "Tuba-Sus", "", "Tuba", Section::brass },
        { "Timpani Rolls", "", "Timpani", Section::percussion },
        { "Great - Bourdon 16ft", "Organ", "Organ", Section::keys },          // (its folder says)
        { "Harp-Gliss", "", "Harp", Section::keys },
        { "Something Else", "", "Something Else", Section::other },
    };
    for (const auto& c : cases)
    {
        INFO (c.name);
        CHECK (library::instrumentOf (c.name, c.folder) == juce::String (c.instrument));
        CHECK (library::sectionOf (c.name, c.folder) == c.section);
    }
}

TEST_CASE ("The library finds every instrument under its folders, in order, and leaves out #include fragments", "[library]")
{
    Libraries l;
    l.add ("VSCO/Strings/ViolinEns-KS.sfz");
    l.add ("VSCO/Brass/Tuba-Sus.sfz");
    l.add ("VSCO/Strings/CelloEns-KS.sfz");
    l.add ("VSCO/Strings/includes/vln-common.sfz");
    l.add ("SSO/Brass/Horn Solo Legato.sfz");
    l.add ("SSO/Brass/FHornSus.sfz");
    l.root.getChildFile ("SSO/Brass/notes.txt").replaceWithText ("x");
    const auto entries = library::scan ({ l.root.getChildFile ("VSCO"), l.root.getChildFile ("SSO") });
    REQUIRE (entries.size() == 5);
    // Strings first (Cellos, Violins), then brass (Horns, Solo Horn, Tuba).
    CHECK (entries[0].instrument == "Cellos");
    CHECK (entries[1].instrument == "Violins");
    CHECK (entries[1].library == "VSCO");
    CHECK (entries[1].folder == "Strings");
    CHECK (entries[2].instrument == "Horns");
    CHECK (entries[2].library == "SSO");
    CHECK (entries[3].instrument == "Solo Horn");
    CHECK (entries[4].instrument == "Tuba");
    for (const auto& e : entries)
        CHECK (! e.name.contains ("common"));
}

TEST_CASE ("The library browser lists a section's instruments, searches every section, gathers a section", "[library]")
{
    Libraries l;
    l.add ("VSCO/ViolinEns-KS.sfz");
    l.add ("VSCO/SViolin-KS.sfz");
    l.add ("VSCO/English Horn Sus.sfz");
    l.add ("VSCO/FHornSus.sfz");
    const auto tuba = l.add ("VSCO/Tuba-Sus.sfz");
    LibraryBrowser browser;
    browser.setSize (1200, 600);
    browser.setFolders ({ l.root.getChildFile ("VSCO") }, false);
    REQUIRE (browser.getEntries().size() == 5);

    browser.chooseSection (library::Section::strings);
    CHECK (browser.getInstrumentRows() == juce::StringArray { "# Solo Violin", "SViolin-KS", "# Violins", "ViolinEns-KS" });
    browser.chooseSection (library::Section::brass);
    CHECK (browser.getInstrumentRows() == juce::StringArray { "# Horns", "FHornSus", "# Tuba", "Tuba-Sus" });
    // A search looks through every section.
    browser.setSearch ("horn");
    CHECK (browser.getInstrumentRows() == juce::StringArray { "# Horns", "FHornSus", "# English Horn", "English Horn Sus" });
    browser.setSearch ({});

    // Choosing a file, loading it.
    juce::File loaded;
    browser.onLoad = [&] (const juce::File& f) { loaded = f; };
    browser.chooseFile (tuba);
    CHECK (browser.getChosen() == tuba);
}

TEST_CASE ("0.12.1: a tall browser draws its lists' empty rows safely (the owner's crash)", "[library]")
{
    // JUCE's lists paint the rows under their last one too, past the count. The sections list read an eighth name from
    // a table of seven there: on Windows that crashed the window.
    CHECK (library::nameOf (library::Section::other) == "OTHER");
    CHECK (library::nameOf ((library::Section) library::numSections).isEmpty());
    CHECK (library::nameOf ((library::Section) 40).isEmpty());
    Libraries l;
    l.add ("VSCO/ViolinEns-KS.sfz");
    LibraryBrowser browser;
    browser.setFolders ({ l.root.getChildFile ("VSCO") }, false);
    for (const auto height : { 600, 1400, 2400 })
    {
        browser.setSize (1600, height);   // (the sections list many rows taller than its seven)
        const auto image = browser.createComponentSnapshot (browser.getLocalBounds(), true, 1.0f);
        CHECK (image.getHeight() == height);
    }
}
