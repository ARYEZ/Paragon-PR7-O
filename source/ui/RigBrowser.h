#pragma once

#include "library/Downloads.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

// The PRESETS page: browse, load, file and organise rigs.
//
//   left:   filters (all / favourites / mine / factory) and banks A-D
//   middle: search box and the list of rigs (double-click to load)
//   right:  the selected rig's details: name, favourite, bank, number,
//           tags, what it uses, and the buttons to act on it
class RigBrowser final : public juce::Component,
                         private juce::ListBoxModel,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit RigBrowser (ParagonX1AudioProcessor&);
    ~RigBrowser() override;

    // Asks for a name, then saves the current sound as a new rig.
    // Also used by the header's SAVE button for factory or unsaved rigs.
    void promptSaveAs();

    // Shows only some rigs: `filter` 0 all, 1 favourites, 2 mine, 3 factory;
    // `bank` -1 for every bank or 0-3 for A-D. (The dock's RIG BROWSER uses this.)
    void showFilter (int filter, int bank);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Filter { all, favourites, mine, factory };

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;   // 0.13.0: a library downloading for the chosen preset

    void refreshList();
    void showDetails();
    const RigLibrary::Entry* selectedEntry() const;
    void selectEntry (const RigLibrary::Entry* entry);
    void saveDetails();
    void confirmDelete();

    ParagonX1AudioProcessor& processor;

    Filter filter = Filter::all;
    int bankFilter = -1;   // -1 = all banks
    // Positions in the library's (sorted) entries, not pointers: saving or
    // deleting a rig rebuilds that list, and a stale pointer would crash.
    std::vector<size_t> visible;
    const RigLibrary::Entry* visibleEntry (int row) const;

    // Which rig is selected, by identity (entries are rebuilt on every rescan).
    juce::File defaultFile;           // 0.70.0: THE DEFAULT PRESET (a user one's file, or a factory one's name)
    juce::String defaultFactory;
    juce::File selectedFile;
    juce::String selectedFactoryName;

    juce::OwnedArray<juce::TextButton> filterButtons, bankButtons;
    juce::TextEditor search;
    juce::ListBox list { "Rigs", this };

    juce::Label detailsTitle { {}, "SELECTED PRESET" }, info;
    juce::TextEditor nameEditor, tagsEditor;
    juce::ToggleButton favouriteButton { "FAVOURITE" };
    juce::ComboBox bankBox, numberBox;
    juce::Label bankLabel { {}, "BANK" }, numberLabel { {}, "NUMBER" }, tagsLabel { {}, "TAGS (comma separated)" };
    juce::TextButton loadButton { "LOAD" }, saveDetailsButton { "SAVE DETAILS" },
                     saveAsButton { "SAVE CURRENT SOUND AS NEW..." }, deleteButton { "DELETE" },
                     folderButton { "OPEN PRESETS FOLDER" },
                     defaultButton { "OPEN WITH THIS IN EVERY NEW PR7-O" };   // 0.70.0: THE DEFAULT PRESET
public:
    juce::TextButton& getDefaultButton() noexcept { return defaultButton; }   // tests
private:

    juce::Rectangle<int> sidebarArea, detailsArea, expansionsHeading;
    // 0.13.0: the library the chosen preset plays when it isn't downloaded (null: none needed), its GET button.
    const downloads::Library* neededLibrary = nullptr;
    juce::TextButton getButton;
public:
    juce::TextButton& getGetButton() noexcept { return getButton; }   // tests
    const downloads::Library* getNeededLibrary() const noexcept { return neededLibrary; }
    void selectForTest (const RigLibrary::Entry* e) { selectEntry (e); refreshList(); }
    juce::String getInfoText() const { return info.getText(); }
    juce::Rectangle<int> getExpansionsHeading() const noexcept { return expansionsHeading; }
private:
    std::unique_ptr<juce::AlertWindow> dialog;
};
