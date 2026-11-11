#include "LibraryBrowser.h"

#include "dsp/sampler/Sfz.h"
#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"

// A list of painted rows (the browser's three columns).
class LibraryBrowser::List final : public juce::Component, private juce::ListBoxModel
{
public:
    std::function<int()> count;
    std::function<void (int, juce::Graphics&, int, int, bool)> paintRow;
    std::function<void (int)> clicked;
    std::function<bool (int)> selectable = [] (int) { return true; };
    juce::ListBox box;

    List()
    {
        box.setModel (this);
        box.setRowHeight (24);
        box.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        box.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (box);
    }
    void resized() override { box.setBounds (getLocalBounds()); }
    void update() { box.updateContent(); box.repaint(); }

private:
    int getNumRows() override { return count ? count() : 0; }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override { if (paintRow) paintRow (row, g, w, h, selected); }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (clicked && selectable (row))
            clicked (row);
    }
};

namespace
{
    void paintText (juce::Graphics& g, const juce::String& text, int w, int h, bool lit, bool dim, bool bold = false, int indent = 10)
    {
        if (lit)
        {
            ParagonColours::setFill (g, ParagonColours::accentDim, juce::Rectangle<int> (0, 1, w, h - 2));
            g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f), 4.0f);
        }
        g.setFont (juce::FontOptions (13.5f, bold ? juce::Font::bold : juce::Font::plain));
        if (lit)
            ParagonColours::setTextFill (g, ParagonColours::accent, juce::Rectangle<int> (indent, 0, w - indent, h));
        else
            g.setColour (dim ? ParagonColours::textDim : ParagonColours::text);
        g.drawFittedText (text, juce::Rectangle<int> (indent, 0, w - indent - 6, h), juce::Justification::centredLeft, 1, 0.8f);
    }
}

LibraryBrowser::LibraryBrowser()
    : libraries (std::make_unique<List>()), sections (std::make_unique<List>()), instruments (std::make_unique<List>())
{
    setOpaque (true);
    libraries->count = [this] { return folders.size() + 1; };
    // (0.12.1: JUCE's lists also paint the empty rows under their last one, `row` past the count: every list returns
    // there. The sections list didn't, and read an eighth section's name from a table of seven: on a tall enough window
    // that read whatever was next in memory as a name's address, and crashed the window: the owner's PR7-S log.)
    libraries->paintRow = [this] (int row, juce::Graphics& g, int w, int h, bool)
    {
        if (row < 0 || row > folders.size())
            return;
        int n = 0;
        for (const auto& e : entries)
            n += row == 0 || e.library == folders[row - 1].getFileName();
        paintText (g, (row == 0 ? juce::String ("ALL LIBRARIES") : folders[row - 1].getFileName()) + "  (" + juce::String (n) + ")",
                   w, h, libraryIndex == row - 1, false, row == 0);
    };
    libraries->clicked = [this] (int row) { if (row <= folders.size()) chooseLibrary (row - 1); };
    sections->count = [] { return library::numSections; };
    sections->paintRow = [this] (int row, juce::Graphics& g, int w, int h, bool)
    {
        if (row < 0 || row >= library::numSections)
            return;
        int n = 0;
        juce::StringArray instrumentsHere;
        for (const auto& e : entries)
            if ((int) e.section == row && (libraryIndex < 0 || e.library == folders[libraryIndex].getFileName()))
                ++n;
        paintText (g, library::nameOf ((library::Section) row) + "  (" + juce::String (n) + ")", w, h, (int) section == row, n == 0, true);
    };
    sections->clicked = [this] (int row) { if (row >= 0 && row < library::numSections) chooseSection ((library::Section) row); };
    instruments->count = [this] { return (int) rows.size(); };
    instruments->selectable = [this] (int row) { return row >= 0 && row < (int) rows.size() && ! rows[(size_t) row].header; };
    instruments->paintRow = [this] (int row, juce::Graphics& g, int w, int h, bool)
    {
        if (row < 0 || row >= (int) rows.size())
            return;
        const auto& r = rows[(size_t) row];
        if (! r.header && (r.entry < 0 || r.entry >= (int) entries.size()))
            return;
        if (r.header)
            paintText (g, r.text.toUpperCase(), w, h, false, false, true, 8);
        else
        {
            const auto lit = r.entry >= 0 && entries[(size_t) r.entry].file == chosen;
            const auto inSection = r.entry >= 0 && sectionFiles.contains (entries[(size_t) r.entry].file);
            paintText (g, (inSection ? "+ " : "") + r.text, w, h, lit, false, false, 26);
            // Its folder, faint at the right (a library's same file in two folders: Sonatina's Notation / Performance).
            if (const auto& folder = entries[(size_t) r.entry].folder; folder.isNotEmpty())
            {
                g.setFont (juce::FontOptions (11.5f));
                g.setColour (ParagonColours::textDim.withAlpha (lit ? 0.9f : 0.7f));
                g.drawFittedText (folder, juce::Rectangle<int> (w / 2, 0, w / 2 - 8, h), juce::Justification::centredRight, 1, 0.8f);
            }
        }
    };
    instruments->clicked = [this] (int row) { if (row >= 0 && row < (int) rows.size()) choose (rows[(size_t) row].entry); };
    for (auto* l : { libraries.get(), sections.get(), instruments.get() })
        addAndMakeVisible (*l);

    search.setTextToShowWhenEmpty ("SEARCH: violin, horn, pizz...", ParagonColours::textDim);
    search.setFont (juce::FontOptions (14.0f));
    search.onTextChange = [this] { rebuild(); };
    addAndMakeVisible (search);

    for (auto* b : { &addFolder, &removeFolder, &openFile, &close, &load, &addToSection, &loadSection, &clearSection })
        addAndMakeVisible (*b);
    addFolder.setTooltip ("Add a library: a folder with SFZ instruments (VSCO 2 CE, Sonatina Symphonic Orchestra, any plain WAV + SFZ library). Remembered for every project");
    removeFolder.setTooltip ("Take the chosen library off the list (nothing on disk is touched)");
    openFile.setTooltip ("Load an SFZ file (or one audio file) from anywhere");
    addToSection.setTooltip ("Add the chosen file to a section you're building (several instruments split over the keyboard)");
    addFolder.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a library folder", juce::File::getSpecialLocation (juce::File::userHomeDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().isDirectory())
            {
                auto list = folders;
                list.addIfNotAlreadyThere (fc.getResult());
                setFolders (list, true);
            }
        });
    };
    removeFolder.onClick = [this]
    {
        if (libraryIndex < 0)
            return;
        auto list = folders;
        list.remove (libraryIndex);
        libraryIndex = -1;
        setFolders (list, true);
    };
    openFile.onClick = [this] { if (onOpenFile) onOpenFile(); };
    close.onClick = [this] { if (onClose) onClose(); };
    load.onClick = [this] { if (chosen.existsAsFile() && onLoad) onLoad (chosen); };
    addToSection.onClick = [this]
    {
        if (chosen.existsAsFile())
            sectionFiles.addIfNotAlreadyThere (chosen);
        resized();
        repaint();
        instruments->update();
    };
    loadSection.onClick = [this]
    {
        juce::StringArray paths;
        for (const auto& f : sectionFiles)
            paths.add (f.getFullPathName());
        if (! paths.isEmpty() && onLoadSection)
            onLoadSection (paths);
    };
    clearSection.onClick = [this] { sectionFiles.clear(); resized(); repaint(); instruments->update(); };
}

LibraryBrowser::~LibraryBrowser() = default;

void LibraryBrowser::setFolders (const juce::Array<juce::File>& list, bool remember)
{
    folders = list;
    if (remember)
        library::setFolders (list);
    rescan();
}

void LibraryBrowser::rescan()
{
    entries = library::scan (folders);
    if (libraryIndex >= folders.size())
        libraryIndex = -1;
    rebuild();
}

void LibraryBrowser::chooseLibrary (int index) { libraryIndex = juce::jlimit (-1, folders.size() - 1, index); rebuild(); }
void LibraryBrowser::chooseSection (library::Section s) { section = s; rebuild(); }
void LibraryBrowser::setSearch (const juce::String& text) { search.setText (text, true); rebuild(); }

void LibraryBrowser::chooseFile (const juce::File& file)
{
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].file == file)
        {
            section = entries[i].section;
            rebuild();
            choose ((int) i);
            return;
        }
}

juce::StringArray LibraryBrowser::getInstrumentRows() const
{
    juce::StringArray list;
    for (const auto& r : rows)
        list.add ((r.header ? "# " : "") + r.text);
    return list;
}

void LibraryBrowser::rebuild()
{
    // The section's instruments (with the search: every section's matches), grouped.
    rows.clear();
    const auto query = search.getText().trim().toLowerCase();
    juce::String instrument;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        if (libraryIndex >= 0 && e.library != folders[libraryIndex].getFileName())
            continue;
        if (query.isEmpty() ? e.section != section
                            : ! (e.name.toLowerCase().contains (query) || e.instrument.toLowerCase().contains (query)))
            continue;
        const auto group = e.instrument + (libraryIndex < 0 && folders.size() > 1 ? "  -  " + e.library : juce::String());
        if (group != instrument)
        {
            rows.push_back ({ true, group, -1 });
            instrument = group;
        }
        rows.push_back ({ false, e.name, (int) i });
    }
    for (auto* l : { libraries.get(), sections.get(), instruments.get() })
        l->update();
    repaint();
}

void LibraryBrowser::choose (int entry)
{
    if (entry < 0 || entry >= (int) entries.size())
        return;
    chosen = entries[(size_t) entry].file;
    for (size_t i = 0; i < rows.size(); ++i)   // (in view)
        if (rows[i].entry == entry)
            instruments->box.scrollToEnsureRowIsOnscreen ((int) i);
    // What it plays (read without its samples: quick).
    sampler::sfz::LoadOptions options;
    options.audio = false;
    const auto program = sampler::sfz::load (chosen, {}, options);
    const auto name = [] (int key) { return key < 0 ? juce::String ("-") : juce::MidiMessage::getMidiNoteName (key, true, true, 3); };
    juce::String info;
    info << entries[(size_t) entry].instrument << "  -  " << entries[(size_t) entry].library << "\n";
    info << "KEYS  " << name (program->lowestKey()) << " to " << name (program->highestKey()) << "    REGIONS  " << (int) program->regions.size() << "\n";
    if (! program->articulations.empty())
    {
        juce::StringArray labels;
        for (const auto& a : program->articulations)
            labels.add (a.label);
        info << "ARTICULATIONS  " << labels.joinIntoString (", ") << "\n";
    }
    if (! program->problems.isEmpty())
        info << "PROBLEMS  " << program->problems.joinIntoString ("; ") << "\n";
    chosenInfo = info;
    instruments->update();
    repaint();
}

void LibraryBrowser::paint (juce::Graphics& g)
{
    g.fillAll (ParagonColours::background);
    for (const auto& area : { librariesArea, sectionsArea, instrumentsArea, detailsArea })
        DockStyle::drawPanel (g, area.toFloat());
    DockStyle::drawTitle (g, librariesArea.reduced (14, 0).withHeight (32), "LIBRARIES");
    DockStyle::drawTitle (g, sectionsArea.reduced (14, 0).withHeight (32), "SECTIONS");
    DockStyle::drawTitle (g, instrumentsArea.reduced (14, 0).withHeight (32), search.getText().isEmpty() ? "INSTRUMENTS" : "FOUND");
    DockStyle::drawTitle (g, detailsArea.reduced (14, 0).withHeight (32), "THE INSTRUMENT CHOSEN");
    auto text = detailsArea.reduced (16, 0).withTrimmedTop (40);
    if (chosen == juce::File())
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (13.5f));
        g.drawFittedText (folders.isEmpty() ? "ADD FOLDER... to add a library: a folder of SFZ instruments on your disk."
                                            : "Choose an instrument file on the left.", text.removeFromTop (60), juce::Justification::topLeft, 3);
    }
    else
    {
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawFittedText (chosen.getFileNameWithoutExtension(), text.removeFromTop (24), juce::Justification::centredLeft, 1, 0.7f);
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (ParagonColours::textDim);
        g.drawFittedText (chosenInfo, text.removeFromTop (100), juce::Justification::topLeft, 6, 0.8f);
    }
    if (! sectionFiles.isEmpty())
    {
        auto area = detailsArea.reduced (16, 0).withTrimmedTop (detailsArea.getHeight() - 132).withHeight (60);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("YOUR SECTION (" + juce::String (sectionFiles.size()) + ")", area.removeFromTop (18), juce::Justification::centredLeft);
        juce::StringArray names;
        for (const auto& f : sectionFiles)
            names.add (f.getFileNameWithoutExtension());
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (names.joinIntoString (" + "), area, juce::Justification::topLeft, 2, 0.8f);
    }
}

void LibraryBrowser::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (40).reduced (2, 4);
    close.setBounds (top.removeFromRight (90));
    top.removeFromRight (8);
    openFile.setBounds (top.removeFromRight (120));
    top.removeFromRight (8);
    search.setBounds (top.removeFromLeft (juce::jmin (420, top.getWidth())));
    area.removeFromTop (6);
    const auto w = area.getWidth();
    librariesArea = area.removeFromLeft (w * 18 / 100);
    area.removeFromLeft (8);
    sectionsArea = area.removeFromLeft (w * 15 / 100);
    area.removeFromLeft (8);
    instrumentsArea = area.removeFromLeft (w * 33 / 100);
    area.removeFromLeft (8);
    detailsArea = area;

    auto lib = librariesArea.reduced (8).withTrimmedTop (32);
    auto libButtons = lib.removeFromBottom (30);
    addFolder.setBounds (libButtons.removeFromLeft (libButtons.getWidth() * 3 / 5).reduced (2, 0));
    removeFolder.setBounds (libButtons.reduced (2, 0));
    lib.removeFromBottom (6);
    libraries->setBounds (lib);
    sections->setBounds (sectionsArea.reduced (8).withTrimmedTop (32));
    instruments->setBounds (instrumentsArea.reduced (8).withTrimmedTop (32));
    auto buttons = detailsArea.reduced (16, 12).removeFromBottom (32);
    load.setBounds (buttons.removeFromLeft (100));
    buttons.removeFromLeft (8);
    addToSection.setBounds (buttons.removeFromLeft (150));
    clearSection.setBounds (buttons.removeFromRight (80));
    buttons.removeFromRight (8);
    loadSection.setBounds (buttons.removeFromRight (140));
    loadSection.setVisible (! sectionFiles.isEmpty());
    clearSection.setVisible (! sectionFiles.isEmpty());
    load.setEnabled (true);
}
