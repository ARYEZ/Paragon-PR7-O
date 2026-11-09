#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Icons.h"
#include "ui/SynthControls.h"

// The panels' shared style, and the SETTINGS page.

// Dark rounded panel with a small grey title, shared by the panels.
namespace DockStyle
{
    void drawPanel (juce::Graphics&, juce::Rectangle<float> area);
    void drawTitle (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);
}

//==============================================================================
// SETTINGS: MIDI (the channel, note names), the sounds, where presets are kept, MIDI links, and what the plugin is built from.
class SettingsPage final : public juce::Component,
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    explicit SettingsPage (ParagonX1AudioProcessor&);
    ~SettingsPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    // MIDI LINKS (PR7-K's stage 4f): one row per link, each with a button to remove it. For tests: the rows shown.
    int getNumLinkRows() const noexcept { return (int) linkRows.size(); }
    juce::TextButton* getRemoveButton (int row) { return row < (int) linkRows.size() ? linkRows[(size_t) row]->remove.get() : nullptr; }

private:
    void timerCallback() override;
    void rebuildLinks();

    struct LinkRow final : public juce::Component
    {
        juce::Label text;
        std::unique_ptr<juce::TextButton> remove;
        void resized() override;
    };
    std::vector<std::unique_ptr<LinkRow>> linkRows;
    juce::Viewport linkView;
    juce::Component linkList;
    juce::TextButton saveDefault { "SAVE AS DEFAULT" }, exportLinks { "EXPORT..." }, importLinks { "IMPORT..." }, clearLinks { "CLEAR ALL" };
    std::unique_ptr<juce::FileChooser> chooser;
    MidiLinks shownLinks;
    juce::Rectangle<int> midiArea;

    ParagonX1AudioProcessor& processor;
    juce::TextButton openFolder { "OPEN PRESETS FOLDER" };
    juce::Rectangle<int> keysArea, soundsArea, rigsArea, aboutArea;
    ParamControl channelChoice, namesChoice;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};
