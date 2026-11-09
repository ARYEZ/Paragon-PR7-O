#include "DockPanels.h"
#include "NoteNames.h"

#include "Parameters.h"
#include "ui/ParagonLookAndFeel.h"

namespace DockStyle
{
    void drawPanel (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e0b0f), 0.0f, area.getY(),
                                                 juce::Colour (0xff09070a), 0.0f, area.getBottom(), false));
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);
    }

    void drawTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
    {
        g.setColour (ParagonColours::text.withAlpha (0.9f));
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (title, area, juce::Justification::centredLeft, false);
    }
}

namespace
{
    void styleButton (juce::TextButton& button, bool primary)
    {
        button.setColour (juce::TextButton::buttonColourId, primary ? ParagonColours::accentDim.withAlpha (0.6f) : juce::Colour (0xff151217));
        button.setColour (juce::TextButton::textColourOffId, primary ? ParagonColours::accent : ParagonColours::text);
    }
}

//==============================================================================
SettingsPage::SettingsPage (ParagonX1AudioProcessor& p)
    : processor (p),
      channelChoice (p.getParameters(), ParamIDs::midiChannel, "MIDI CHANNEL",
                     "Which MIDI channel the instrument listens to (ANY: all of them). Not part of a preset."),
      namesChoice (p.getParameters(), ParamIDs::noteNames, "NOTE NAMES",
                   "How notes are named. C3: middle C (note 60) is C3, as in Cubase and Yamaha. C4: middle C is C4, as in Roland "
                   "and most other software. Only the names change, never which note plays what. (SFZ files name middle C c4 "
                   "whichever you choose.) Not part of a preset.")
{
    addAndMakeVisible (channelChoice);
    addAndMakeVisible (namesChoice);
    styleButton (openFolder, false);
    openFolder.onClick = [this]
    {
        processor.getRigLibrary().getFolder().createDirectory();
        processor.getRigLibrary().getFolder().startAsProcess();
    };

    for (auto* component : std::initializer_list<juce::Component*> { &openFolder })
        addAndMakeVisible (component);
    processor.rigBroadcaster.addChangeListener (this);

    // MIDI LINKS.
    for (auto* button : { &saveDefault, &exportLinks, &importLinks, &clearLinks })
    {
        styleButton (*button, button == &saveDefault);
        addAndMakeVisible (*button);
    }
    saveDefault.setTooltip ("A new PR7-O (in any project) starts with these links");
    exportLinks.setTooltip ("Save these links to a file, to back them up or use on another computer");
    importLinks.setTooltip ("Load links from a file saved with EXPORT");
    clearLinks.setTooltip ("Forget every link (undo isn't kept for links: EXPORT first if you might want them back)");
    saveDefault.onClick = [this] { processor.saveMidiLinksAsDefault(); };
    clearLinks.onClick = [this] { processor.setMidiLinks ({}); rebuildLinks(); };
    exportLinks.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Export MIDI links", processor.getRigLibrary().getFolder().getChildFile ("MIDI Links.xml"), "*.xml");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& c) { if (c.getResult() != juce::File()) processor.exportMidiLinks (c.getResult().withFileExtension ("xml")); });
    };
    importLinks.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Import MIDI links", processor.getRigLibrary().getFolder(), "*.xml");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& c) { if (c.getResult().existsAsFile()) { processor.importMidiLinks (c.getResult()); rebuildLinks(); } });
    };
    linkView.setViewedComponent (&linkList, false);
    linkView.setScrollBarsShown (true, false);
    addAndMakeVisible (linkView);
    rebuildLinks();
    startTimerHz (4);
}

void SettingsPage::LinkRow::resized()
{
    auto area = getLocalBounds();
    remove->setBounds (area.removeFromRight (28).reduced (2));
    text.setBounds (area);
}

void SettingsPage::visibilityChanged()
{
    if (isVisible() && ! (processor.getMidiLinks() == shownLinks))
        rebuildLinks();
}

void SettingsPage::timerCallback()
{
    if (! (processor.getMidiLinks() == shownLinks))
        rebuildLinks();
}

void SettingsPage::rebuildLinks()
{
    shownLinks = processor.getMidiLinks();
    linkRows.clear();
    for (const auto& link : shownLinks.links)
    {
        auto row = std::make_unique<LinkRow>();
        const auto* parameter = processor.getParameters().getParameter (link.parameterId);
        row->text.setText ("CC " + juce::String (link.controller) + "   " + juce::String::fromUTF8 ("\xe2\x86\x92") + "   "
                           + (parameter != nullptr ? parameter->getName (60) : link.parameterId), juce::dontSendNotification);
        row->text.setColour (juce::Label::textColourId, ParagonColours::text);
        row->text.setFont (juce::FontOptions (13.0f));
        row->remove = std::make_unique<juce::TextButton> ("X");
        row->remove->setTooltip ("Remove this link");
        const auto id = link.parameterId;
        row->remove->onClick = [this, id]
        {
            auto links = processor.getMidiLinks();
            links.forget (id);
            processor.setMidiLinks (links);
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SettingsPage> (this)] { if (safe != nullptr) safe->rebuildLinks(); });
        };
        row->addAndMakeVisible (row->text);
        row->addAndMakeVisible (*row->remove);
        linkList.addAndMakeVisible (*row);
        linkRows.push_back (std::move (row));
    }
    resized();
    repaint();
}

SettingsPage::~SettingsPage()
{
    stopTimer();
    processor.rigBroadcaster.removeChangeListener (this);
}

void SettingsPage::paint (juce::Graphics& g)
{
    const auto section = [&g] (juce::Rectangle<int> area, const juce::String& title, const juce::String& body)
    {
        DockStyle::drawPanel (g, area.toFloat());
        auto inner = area.reduced (24, 18);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (title, inner.removeFromTop (24), juce::Justification::centredLeft, false);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (body, inner.removeFromTop (64), juce::Justification::topLeft, 4, 1.0f);
    };

    section (keysArea, "MIDI", "The instrument plays your DAW's notes (and the keyboard under the pages) on the channel chosen. "
                               "The pitch wheel bends by each instrument's own range (two semitones unless it says), and the "
                               "sustain pedal (CC 64) holds notes.");
    section (soundsArea, "THE SOUNDS", "PR7-O ships no samples: it plays SFZ instruments from your computer. Open orchestral "
                                       "libraries come this way: VSCO 2 Community Edition (Versilian Studios), Sonatina Symphonic "
                                       "Orchestra, Virtual Playing Orchestra. Check each library's licence before you release music "
                                       "with it; PR7-O never reads locked formats (Kontakt, Opus, Spitfire's player...).");
    section (rigsArea, "PRESETS", "Your presets are files in this folder, so you can back them up or share them:\n"
                                  + processor.getRigLibrary().getFolder().getFullPathName());
    section (midiArea, "MIDI LINKS", shownLinks.isEmpty()
                 ? juce::String ("None yet. Right-click a knob and choose MIDI learn, then move a knob or fader on your controller.")
                 : juce::String ("Your controller's knobs and faders, and the knobs they turn. Kept with the project; "
                                 "SAVE AS DEFAULT for every new PR7-O."));
    section (aboutArea, "ABOUT",
             juce::String ("Paragon PR7-O v") + JucePlugin_VersionString
                 + ". The PR7 family's orchestra instrument: SFZ instruments from open libraries, played by our own sampler, "
                   "through the family's effects (grown from PR7-D; our own design and code). "
                   "Free and open source under the GNU AGPLv3.\n"
                   "Built with JUCE (AGPLv3) and the Inter typeface (SIL OFL).");
}

void SettingsPage::resized()
{
    auto area = getLocalBounds();
    // MIDI and THE SOUNDS down the left; PRESETS, MIDI LINKS and ABOUT down the right.
    auto left = area.removeFromLeft (area.getWidth() / 2 - 6);
    keysArea = left.removeFromTop (190);
    left.removeFromTop (12);
    soundsArea = left.removeFromTop (170);
    area.removeFromLeft (12);
    rigsArea = area.removeFromTop (170);
    area.removeFromTop (12);
    aboutArea = area.removeFromBottom (150);
    area.removeFromBottom (12);
    midiArea = area;
    {
        auto keys = keysArea.reduced (24, 18).withTrimmedTop (84);
        auto bar = keys.removeFromTop (ParamControl::knobHeight - 30);
        channelChoice.setBounds (bar.removeFromLeft (130));
        bar.removeFromLeft (14);
        namesChoice.setBounds (bar.removeFromLeft (130));
    }
    auto midi = midiArea.reduced (24, 18).withTrimmedTop (84);
    auto buttons = midi.removeFromBottom (30);
    for (auto* button : { &saveDefault, &exportLinks, &importLinks, &clearLinks })
    {
        button->setBounds (buttons.removeFromLeft (button == &saveDefault ? 150 : 100));
        buttons.removeFromLeft (8);
    }
    midi.removeFromBottom (8);
    linkView.setBounds (midi);
    linkView.setVisible (! linkRows.empty());   // (nothing to show: the text above says how to make one)
    linkList.setSize (juce::jmax (10, midi.getWidth() - linkView.getScrollBarThickness()), (int) linkRows.size() * 26);
    for (size_t i = 0; i < linkRows.size(); ++i)
        linkRows[i]->setBounds (0, (int) i * 26, linkList.getWidth(), 24);

    openFolder.setBounds (rigsArea.reduced (24, 18).withTrimmedTop (98).removeFromTop (32).withWidth (220));
}
