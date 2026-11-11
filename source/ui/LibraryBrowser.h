#pragma once

#include "library/Downloads.h"
#include "library/Library.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// THE LIBRARY BROWSER (PR7-O 0.11.0; the plan's stage 3): LOAD opens it over the INSTRUMENT page, below the rack. Left
// to right: the LIBRARIES (the folders added: ADD FOLDER..., REMOVE), the SECTIONS (strings, brass...: how many
// instruments each), the INSTRUMENTS (grouped: Violins, Solo Violin..., each file under its instrument), and the
// chosen file: its keys and articulations, LOAD, ADD TO SECTION (a section built file by file: LOAD SECTION). A search
// box over it all; OPEN FILE... for anything outside the libraries. Our own design.
// 0.13.0: GET buttons under the libraries for the ones PR7-O can download (VSCO 2 CE, Sonatina) that aren't there yet:
// the download's progress there (CANCEL), then the library listed with the folders the user added (not removable).
class LibraryBrowser final : public juce::Component, private juce::Timer
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

    // 0.13.0: the processor's download (shown), and what GET does.
    void setDownload (downloads::Download* d) { download = d; refreshDownloads(); }
    std::function<void (const downloads::Library&)> onGet;
    // A library's name as listed (a downloaded one by its name, not its folder's).
    static juce::String displayName (const juce::File& folder);
    bool isDownloadFolder (int index) const;

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
    void timerCallback() override;
    void refreshDownloads();   // the GET buttons and the download's line, for what's there now

    juce::Array<juce::File> userFolders;   // the folders added (remembered)
    juce::Array<juce::File> folders;       // those and the downloaded libraries (listed)
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
    downloads::Download* download = nullptr;
    juce::OwnedArray<juce::TextButton> getButtons;   // one a library: GET <name>
    std::vector<const downloads::Library*> getFor;
    juce::TextButton cancelDownload { "CANCEL" };
    juce::String downloadLine;                       // "VSCO 2 CE: DOWNLOADING 34%" / why it failed
    downloads::Download::State shownState = downloads::Download::State::idle;
    juce::Rectangle<int> downloadArea;
    juce::Rectangle<int> librariesArea, sectionsArea, instrumentsArea, detailsArea;
};
