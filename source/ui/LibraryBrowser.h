#pragma once

#include "library/Library.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// THE LIBRARY BROWSER (PR7-O 0.11.0; the plan's stage 3): LOAD opens it over the INSTRUMENT page, below the rack. Left
// to right: the LIBRARIES (the folders added: ADD FOLDER..., REMOVE), the SECTIONS (strings, brass...: how many
// instruments each), the INSTRUMENTS (grouped: Violins, Solo Violin..., each file under its instrument), and the
// chosen file: its keys and articulations, LOAD, ADD TO SECTION (a section built file by file: LOAD SECTION). A search
// box over it all; OPEN FILE... for anything outside the libraries. Our own design.
class LibraryBrowser final : public juce::Component
{
public:
    LibraryBrowser();
    ~LibraryBrowser() override;

    void setFolders (const juce::Array<juce::File>&, bool remember);   // rescans
    void rescan();
    const std::vector<library::Entry>& getEntries() const noexcept { return entries; }
    // What's shown: the library (-1: all), the section, the search; the instrument rows (tests and snapshots).
    void chooseLibrary (int index);
    void chooseSection (library::Section);
    void chooseFile (const juce::File&);
    void setSearch (const juce::String&);
    juce::StringArray getInstrumentRows() const;
    const juce::File& getChosen() const noexcept { return chosen; }
    const juce::Array<juce::File>& getSectionFiles() const noexcept { return sectionFiles; }

    std::function<void (const juce::File&)> onLoad;
    std::function<void (const juce::StringArray&)> onLoadSection;
    std::function<void()> onOpenFile, onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row { bool header = false; juce::String text; int entry = -1; };
    class List;
    void rebuild();
    void choose (int entry);

    juce::Array<juce::File> folders;
    std::vector<library::Entry> entries;
    int libraryIndex = -1;
    library::Section section = library::Section::strings;
    std::vector<Row> rows;
    juce::File chosen;
    juce::String chosenInfo;
    juce::Array<juce::File> sectionFiles;

    std::unique_ptr<List> libraries, sections, instruments;
    juce::TextEditor search;
    juce::TextButton addFolder { "ADD FOLDER..." }, removeFolder { "REMOVE" }, openFile { "OPEN FILE..." }, close { "CLOSE" },
                     load { "LOAD" }, addToSection { "ADD TO SECTION" }, loadSection { "LOAD SECTION" }, clearSection { "CLEAR" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> librariesArea, sectionsArea, instrumentsArea, detailsArea;
};
