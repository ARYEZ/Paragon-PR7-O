#include "RigBrowser.h"

#include "dsp/sampler/Section.h"

#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
    constexpr int sidebarWidth = 170;
    constexpr int detailsWidth = 300;

    void styleEditor (juce::TextEditor& editor)
    {
        editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1c1c20));
        editor.setColour (juce::TextEditor::outlineColourId, ParagonColours::panelEdge);
        editor.setColour (juce::TextEditor::focusedOutlineColourId, ParagonColours::accent);
        editor.setColour (juce::TextEditor::textColourId, ParagonColours::text);
        editor.setFont (juce::FontOptions (14.0f));
    }

    void styleLabel (juce::Label& label)
    {
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ParagonColours::textDim);
    }

    // One line describing a preset: its instrument and the sends it uses.
    juce::String describeBlocks (const Rig& rig)
    {
        const auto isOn = [&rig] (const juce::String& id)
        {
            const auto found = rig.parameters.find (id);
            return found != rig.parameters.end() && found->second > 0.5f;
        };
        juce::StringArray parts;
        parts.add (rig.instrument.isEmpty() ? juce::String ("no instrument")
                                            : sampler::section::describe (rig.instrument));
        if (isOn (ParamIDs::delayOn))
            parts.add ("delay");
        if (isOn (ParamIDs::reverbOn))
            parts.add ("reverb");
        return parts.joinIntoString (", ");
    }
}

RigBrowser::RigBrowser (ParagonX1AudioProcessor& p) : processor (p)
{
    const auto addRadio = [this] (juce::OwnedArray<juce::TextButton>& buttons, const juce::String& text, int group, std::function<void()> onClick)
    {
        auto* button = buttons.add (new juce::TextButton (text));
        button->setClickingTogglesState (true);
        button->setRadioGroupId (group);
        button->setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim);
        button->setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
        button->onClick = std::move (onClick);
        addAndMakeVisible (button);
    };

    addRadio (filterButtons, "All Presets", 1, [this] { filter = Filter::all; refreshList(); });
    addRadio (filterButtons, "Favourites", 1, [this] { filter = Filter::favourites; refreshList(); });
    addRadio (filterButtons, "My Presets", 1, [this] { filter = Filter::mine; refreshList(); });
    addRadio (filterButtons, "Factory Presets", 1, [this] { filter = Filter::factory; refreshList(); });
    addRadio (bankButtons, "All Banks", 2, [this] { bankFilter = -1; refreshList(); });
    for (int bank = 0; bank < Rig::numBanks; ++bank)
        addRadio (bankButtons, "Bank " + Rig::bankName (bank), 2, [this, bank] { bankFilter = bank; refreshList(); });
    filterButtons[0]->setToggleState (true, juce::dontSendNotification);
    bankButtons[0]->setToggleState (true, juce::dontSendNotification);

    styleEditor (search);
    search.setTextToShowWhenEmpty ("Search names and tags", ParagonColours::textDim);
    search.onTextChange = [this] { refreshList(); };
    addAndMakeVisible (search);

    list.setRowHeight (30);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0e0e10));
    list.setColour (juce::ListBox::outlineColourId, ParagonColours::panelEdge);
    list.setOutlineThickness (1);
    addAndMakeVisible (list);

    detailsTitle.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (detailsTitle);
    for (auto* label : { &bankLabel, &numberLabel, &tagsLabel })
    {
        styleLabel (*label);
        addAndMakeVisible (*label);
    }
    info.setFont (juce::FontOptions (12.0f));
    info.setColour (juce::Label::textColourId, ParagonColours::textDim);
    info.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (info);

    styleEditor (nameEditor);
    styleEditor (tagsEditor);
    nameEditor.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    addAndMakeVisible (nameEditor);
    addAndMakeVisible (tagsEditor);

    for (int bank = 0; bank < Rig::numBanks; ++bank)
        bankBox.addItem (Rig::bankName (bank), bank + 1);
    for (int number = 1; number <= Rig::maxNumber; ++number)
        numberBox.addItem (juce::String (number).paddedLeft ('0', 3), number);
    for (auto* box : { &bankBox, &numberBox })
    {
        box->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1c1c20));
        box->setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
        addAndMakeVisible (*box);
    }

    // Favourite is saved straight away (it works for factory rigs too).
    favouriteButton.onClick = [this]
    {
        if (const auto* entry = selectedEntry())
            processor.getRigLibrary().setFavourite (*entry, favouriteButton.getToggleState());
        refreshList();
    };
    addAndMakeVisible (favouriteButton);

    loadButton.onClick = [this] { if (const auto* entry = selectedEntry()) processor.loadRig (*entry); };
    saveDetailsButton.onClick = [this] { saveDetails(); };
    saveAsButton.onClick = [this] { promptSaveAs(); };
    deleteButton.onClick = [this] { confirmDelete(); };
    folderButton.onClick = [this]
    {
        const auto folder = processor.getRigLibrary().getFolder();
        folder.createDirectory();
        folder.revealToUser();
    };
    // 0.70.0: THE DEFAULT PRESET (pressed again on it: back to Init).
    defaultButton.onClick = [this]
    {
        if (const auto* entry = selectedEntry())
            processor.setDefaultRig (processor.isDefaultRig (*entry) ? nullptr : entry);
        showDetails();
        refreshList();
    };
    for (auto* button : { &loadButton, &saveDetailsButton, &saveAsButton, &deleteButton, &folderButton, &defaultButton })
        addAndMakeVisible (*button);

    processor.rigBroadcaster.addChangeListener (this);

    if (const auto* current = processor.findCurrentRigEntry())
        selectEntry (current);
    refreshList();
}

RigBrowser::~RigBrowser()
{
    processor.rigBroadcaster.removeChangeListener (this);
}

void RigBrowser::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The library was rescanned (save, delete...) or another rig loaded.
    refreshList();
}

const RigLibrary::Entry* RigBrowser::selectedEntry() const
{
    auto& library = processor.getRigLibrary();
    if (selectedFile != juce::File())
        return library.find (selectedFile);
    if (selectedFactoryName.isNotEmpty())
        return library.findFactory (selectedFactoryName);
    return nullptr;
}

void RigBrowser::selectEntry (const RigLibrary::Entry* entry)
{
    selectedFile = entry != nullptr && ! entry->isFactory ? entry->file : juce::File();
    selectedFactoryName = entry != nullptr && entry->isFactory ? entry->rig.name : juce::String();
}

const RigLibrary::Entry* RigBrowser::visibleEntry (int row) const
{
    const auto& entries = processor.getRigLibrary().getEntries();
    if (! juce::isPositiveAndBelow (row, (int) visible.size()) || visible[(size_t) row] >= entries.size())
        return nullptr;
    return &entries[visible[(size_t) row]];
}

void RigBrowser::refreshList()
{
    const auto query = search.getText().trim();
    visible.clear();
    defaultFile = juce::File();
    defaultFactory.clear();
    if (const auto* chosen = processor.findDefaultRigEntry())
    {
        defaultFile = chosen->file;
        defaultFactory = chosen->isFactory ? chosen->rig.name : juce::String();
    }

    const auto& entries = processor.getRigLibrary().getEntries();
    for (size_t index = 0; index < entries.size(); ++index)
    {
        const auto& entry = entries[index];
        if (filter == Filter::favourites && ! entry.rig.favourite) continue;
        if (filter == Filter::mine && entry.isFactory) continue;
        if (filter == Filter::factory && ! entry.isFactory) continue;
        if (bankFilter >= 0 && entry.rig.bank != bankFilter) continue;

        if (query.isNotEmpty() && ! entry.rig.name.containsIgnoreCase (query)
            && ! entry.rig.tags.joinIntoString (" ").containsIgnoreCase (query))
            continue;

        visible.push_back (index);
    }

    list.updateContent();

    // Keep the selection on the same rig if it's still visible.
    const auto* selected = selectedEntry();
    int selectedRow = -1;
    for (int row = 0; row < (int) visible.size(); ++row)
        if (visibleEntry (row) == selected)
            selectedRow = row;
    if (selectedRow >= 0)
        list.selectRow (selectedRow, false, true);
    else
        list.deselectAllRows();

    list.repaint();
    showDetails();
}

int RigBrowser::getNumRows()
{
    return (int) visible.size();
}

void RigBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto* found = visibleEntry (row);
    if (found == nullptr)
        return;

    const auto& entry = *found;
    const auto current = processor.getCurrentRig();
    const auto isCurrent = entry.isFactory ? current.factoryName == entry.rig.name
                                           : current.file == entry.file;

    if (selected)
        g.fillAll (ParagonColours::accentDim.withAlpha (0.6f));

    auto area = juce::Rectangle<int> (width, height).reduced (10, 0);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (ParagonColours::textDim);
    g.drawText (entry.rig.getSlotName(), area.removeFromLeft (40), juce::Justification::centredLeft, false);

    ParagonColours::setFill (g, entry.rig.favourite ? ParagonColours::accent : ParagonColours::panelEdge, juce::Rectangle<int> (0, 0, width, height));
    ParagonColours::setTextFill (g, entry.rig.favourite ? ParagonColours::accent : ParagonColours::panelEdge, ParagonColours::textSpan (g.getCurrentFont(), entry.rig.favourite ? juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x86")), area.withLeft (area.getRight() - 20).toFloat(), juce::Justification::centred));
    g.drawText (entry.rig.favourite ? juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x86")),
                area.removeFromRight (20), juce::Justification::centred, false);
    ParagonColours::setFill (g, entry.rig.favourite ? ParagonColours::accent : ParagonColours::panelEdge, juce::Rectangle<int> (0, 0, width, height));

    if (entry.isFactory)
    {
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (ParagonColours::textDim);
        g.drawText ("FACTORY", area.removeFromRight (60), juce::Justification::centred, false);
    }
    if (entry.isFactory ? (defaultFactory.isNotEmpty() && defaultFactory == entry.rig.name) : (defaultFile != juce::File() && defaultFile == entry.file))
    {
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        auto tag = area.removeFromRight (60);
        ParagonColours::setFill (g, ParagonColours::accent, tag.toFloat().reduced (4.0f, (float) height * 0.28f));
        g.drawRoundedRectangle (tag.toFloat().reduced (4.0f, (float) height * 0.28f), 3.0f, 1.0f);
        g.setColour (ParagonColours::text);
        g.drawText ("DEFAULT", tag, juce::Justification::centred, false);
    }

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (ParagonColours::textDim);
    g.drawText (entry.rig.tags.joinIntoString ("  "), area.removeFromRight (area.getWidth() / 3), juce::Justification::centredRight, true);

    g.setFont (juce::FontOptions (14.0f, isCurrent ? juce::Font::bold : juce::Font::plain));
    ParagonColours::setFill (g, isCurrent ? ParagonColours::accent : ParagonColours::text, juce::Rectangle<int> (0, 0, width, height));
    ParagonColours::setTextFill (g, isCurrent ? ParagonColours::accent : ParagonColours::text, ParagonColours::textSpan (g.getCurrentFont(), entry.rig.name, (area).toFloat(), juce::Justification::centredLeft));
    g.drawText (entry.rig.name, area, juce::Justification::centredLeft, true);
    ParagonColours::setFill (g, isCurrent ? ParagonColours::accent : ParagonColours::text, juce::Rectangle<int> (0, 0, width, height));
}

void RigBrowser::selectedRowsChanged (int lastRowSelected)
{
    if (const auto* entry = visibleEntry (lastRowSelected))
        selectEntry (entry);
    showDetails();
}

void RigBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (const auto* entry = visibleEntry (row))
        processor.loadRig (*entry);
}

void RigBrowser::showDetails()
{
    const auto* entry = selectedEntry();
    const auto hasEntry = entry != nullptr;
    const auto editable = hasEntry && ! entry->isFactory;

    nameEditor.setText (hasEntry ? entry->rig.name : juce::String(), false);
    tagsEditor.setText (hasEntry ? entry->rig.tags.joinIntoString (", ") : juce::String(), false);
    bankBox.setSelectedId (hasEntry ? entry->rig.bank + 1 : 0, juce::dontSendNotification);
    numberBox.setSelectedId (hasEntry ? entry->rig.number : 0, juce::dontSendNotification);
    favouriteButton.setToggleState (hasEntry && entry->rig.favourite, juce::dontSendNotification);

    for (auto* component : std::initializer_list<juce::Component*> { &nameEditor, &tagsEditor, &bankBox, &numberBox,
                                                                      &saveDetailsButton, &deleteButton })
        component->setEnabled (editable);
    nameEditor.setReadOnly (! editable);
    tagsEditor.setReadOnly (! editable);
    favouriteButton.setEnabled (hasEntry);
    loadButton.setEnabled (hasEntry);
    defaultButton.setEnabled (hasEntry);
    defaultButton.setButtonText (hasEntry && processor.isDefaultRig (*entry) ? "THE DEFAULT (PRESS: BACK TO INIT)"
                                                                            : "OPEN WITH THIS IN EVERY NEW PR7-O");

    juce::String text;
    if (hasEntry)
    {
        const auto& rig = entry->rig;
        text << describeBlocks (rig);
        if (rig.notes.isNotEmpty())
            text << "\n\n" << rig.notes;
        if (entry->isFactory)
            text << "\n\nFactory presets can't be changed: load it, then save a copy.";
    }
    else
    {
        text = "Select a preset on the left.";
    }
    info.setText (text, juce::dontSendNotification);
}

void RigBrowser::saveDetails()
{
    const auto* entry = selectedEntry();
    if (entry == nullptr || entry->isFactory)
        return;

    Rig details;
    details.name = nameEditor.getText();
    details.bank = juce::jmax (0, bankBox.getSelectedId() - 1);
    details.number = juce::jmax (1, numberBox.getSelectedId());
    details.tags = juce::StringArray::fromTokens (tagsEditor.getText(), ",", {});
    details.tags.trim();
    details.tags.removeEmptyStrings();
    details.tags.removeDuplicates (true);

    processor.updateRigDetails (*entry, details);
    refreshList();
}

void RigBrowser::showFilter (int newFilter, int bank)
{
    const auto filterIndex = juce::jlimit (0, filterButtons.size() - 1, newFilter);
    const auto bankIndex = juce::jlimit (0, bankButtons.size() - 1, bank + 1);
    filterButtons[filterIndex]->setToggleState (true, juce::dontSendNotification);
    bankButtons[bankIndex]->setToggleState (true, juce::dontSendNotification);
    filter = (Filter) filterIndex;
    bankFilter = bank;
    refreshList();
}

void RigBrowser::promptSaveAs()
{
    // The dialog lives inside the plugin window: separate desktop windows
    // can end up behind the DAW in some hosts.
    dialog = std::make_unique<juce::AlertWindow> ("Save preset", "Save the current sound as a new preset called:",
                                                  juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("name", processor.getCurrentRig().name);
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    auto* parent = getParentComponent() != nullptr ? getParentComponent() : this;
    parent->addAndMakeVisible (*dialog);
    dialog->centreAroundComponent (parent, dialog->getWidth(), dialog->getHeight());
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis = juce::Component::SafePointer<RigBrowser> (this)] (int result)
    {
        if (safeThis == nullptr || safeThis->dialog == nullptr)
            return;

        const auto name = safeThis->dialog->getTextEditorContents ("name").trim();
        safeThis->dialog->setVisible (false);

        if (result == 1 && name.isNotEmpty() && safeThis->processor.saveCurrentRigAs (name))
            safeThis->selectEntry (safeThis->processor.findCurrentRigEntry());
        safeThis->refreshList();
    }));
}

void RigBrowser::confirmDelete()
{
    const auto* entry = selectedEntry();
    if (entry == nullptr || entry->isFactory)
        return;

    dialog = std::make_unique<juce::AlertWindow> ("Delete preset", "Move \"" + entry->rig.name + "\" to the bin?",
                                                  juce::MessageBoxIconType::WarningIcon);
    dialog->addButton ("Delete", 1);
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    auto* parent = getParentComponent() != nullptr ? getParentComponent() : this;
    parent->addAndMakeVisible (*dialog);
    dialog->centreAroundComponent (parent, dialog->getWidth(), dialog->getHeight());
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis = juce::Component::SafePointer<RigBrowser> (this)] (int result)
    {
        if (safeThis == nullptr || safeThis->dialog == nullptr)
            return;
        safeThis->dialog->setVisible (false);

        if (result == 1)
            if (const auto* toDelete = safeThis->selectedEntry())
                safeThis->processor.deleteRig (*toDelete);
        safeThis->refreshList();
    }));
}

void RigBrowser::paint (juce::Graphics& g)
{
    for (auto area : { sidebarArea, detailsArea })
    {
        g.setColour (ParagonColours::panel);
        g.fillRoundedRectangle (area.toFloat(), 8.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("PRESET BROWSER", sidebarArea.reduced (12, 8).withHeight (16), juce::Justification::centredLeft, false);
    if (bankButtons.size() > 0)
        g.drawText ("BANKS", bankButtons[0]->getBounds().translated (0, -22).withHeight (16), juce::Justification::centredLeft, false);
}

void RigBrowser::resized()
{
    auto area = getLocalBounds();

    sidebarArea = area.removeFromLeft (sidebarWidth);
    auto sidebar = sidebarArea.reduced (10, 8).withTrimmedTop (24);
    for (auto* button : filterButtons)
    {
        button->setBounds (sidebar.removeFromTop (28));
        sidebar.removeFromTop (4);
    }
    sidebar.removeFromTop (28);
    for (auto* button : bankButtons)
    {
        button->setBounds (sidebar.removeFromTop (28));
        sidebar.removeFromTop (4);
    }

    area.removeFromLeft (12);
    detailsArea = area.removeFromRight (detailsWidth);
    area.removeFromRight (12);

    search.setBounds (area.removeFromTop (30));
    area.removeFromTop (8);
    list.setBounds (area);

    auto details = detailsArea.reduced (14, 10);
    detailsTitle.setBounds (details.removeFromTop (20));
    nameEditor.setBounds (details.removeFromTop (34));
    details.removeFromTop (6);
    favouriteButton.setBounds (details.removeFromTop (24));
    details.removeFromTop (6);

    auto labels = details.removeFromTop (16);
    bankLabel.setBounds (labels.removeFromLeft (labels.getWidth() / 2));
    numberLabel.setBounds (labels);
    auto boxes = details.removeFromTop (28);
    bankBox.setBounds (boxes.removeFromLeft (boxes.getWidth() / 2).withTrimmedRight (6));
    numberBox.setBounds (boxes);
    details.removeFromTop (8);

    tagsLabel.setBounds (details.removeFromTop (16));
    tagsEditor.setBounds (details.removeFromTop (28));
    details.removeFromTop (10);

    folderButton.setBounds (details.removeFromBottom (26));
    details.removeFromBottom (6);
    saveAsButton.setBounds (details.removeFromBottom (30));
    details.removeFromBottom (6);
    defaultButton.setBounds (details.removeFromBottom (30));
    details.removeFromBottom (6);
    auto row = details.removeFromBottom (30);
    loadButton.setBounds (row.removeFromLeft (row.getWidth() / 3).withTrimmedRight (4));
    saveDetailsButton.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (4));
    deleteButton.setBounds (row);
    details.removeFromBottom (8);
    info.setBounds (details);
}
