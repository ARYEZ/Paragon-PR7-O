#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/DockPanels.h"
#include "ui/FxPage.h"
#include "ui/Icons.h"
#include "ui/InstrumentPage.h"
#include "ui/LevelMeter.h"
#include "ui/MixerPage.h"
#include "ui/PaintCache.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/ProgressBox.h"
#include "ui/RigBrowser.h"

// A side strip: a title, a gain knob with its value underneath, and a level meter.
class GainColumn final : public juce::Component
{
public:
    GainColumn (const juce::String& title, juce::AudioProcessorValueTreeState& parameters,
                const juce::String& parameterId, LevelMeterSource& meterSource);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label titleLabel;
    juce::Slider knob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    LevelMeter meter;
};

// Everything in the plugin window, laid out on a fixed 1600-wide canvas (the editor scales it to the window, staying
// sharp: it's all vectors).
//
//   header:   logo | INSTRUMENT FX PRESETS SETTINGS | < preset > | undo redo save settings
//   pages:    the page shown                                         | OUTPUT
//   strip:    the KEYBOARD (SHOW / HIDE KEYBOARD at OUTPUT's foot; the window shorter without it)
class MainView final : public juce::Component,
                       public ControlHost,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit MainView (ParagonX1AudioProcessor&);
    ~MainView() override;

    static constexpr int width = 1600;
    static constexpr int height = 900;
    // SHOW / HIDE KEYBOARD (PR7-D 0.61.0's SHOW / HIDE MIDI TRACK, for the keyboard): hiding it takes its 148 canvas
    // pixels out, so the canvas, and the window, get shorter. Each window opens with it shown; not saved.
    static constexpr int stripHeight = 148;
    static constexpr int heightFor (bool tall) { return tall ? height : height - stripHeight; }
    std::function<void()> onHeightChanged;   // told after the view has its new height (the editor resizes the window)
    bool isKeyboardShown() const noexcept { return keyboardShown; }
    void setKeyboardShown (bool shown);

    enum class Page { instrument, fx, presets, settings, mixer };   // (0.15.0: MIXER, shown between INSTRUMENT and FX)
    void showPage (Page);
    Page getPage() const noexcept { return page; }

    // For snapshots and tests.
    InstrumentPage& getInstrumentPage() noexcept { return instrumentPage; }
    FxPage& getFxPage() noexcept { return fxPage; }
    MixerPage& getMixerPage() noexcept { return mixerPage; }
    SettingsPage& getSettingsPage() noexcept { return settingsPage; }
    InstrumentKeyboard& getKeyboard() noexcept { return keyboard; }
    ProgressBox& getProgressBox() noexcept { return progressBox; }
    void checkLongJobs (double nowMs);   // the timer's look at a load running (tests)

    // ControlHost: the knobs' MIDI learn.
    int linkedController (const juce::String& parameterId) const override;
    bool isLearning (const juce::String& parameterId) const override;
    void learn (const juce::String& parameterId) override;
    void forget (const juce::String& parameterId) override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void paintBackground (juce::Graphics&);
    PaintCache backgroundCache { true };
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshRig();
    void saveRig();
    void applyKeyboardView();

    ParagonX1AudioProcessor& audioProcessor;

    // Header
    IconButton instrumentTab { "The instrument: load an SFZ, how it plays", Icons::Type::orchestra, "INSTRUMENT" },
               mixerTab { "THE MIXER: a strip for each part (effects, sends, pan, level, mute, solo), then the section's", Icons::Type::mixer, "MIXER" },
               fxTab { "The effects: an insert bus, DELAY and REVERB sends, MOD / LO-FI and the MASTER rack", Icons::Type::rack, "FX" },
               presetsTab { "Browse, save and organise presets", Icons::Type::presets, "PRESETS" },
               settingsTab { "MIDI, the presets folder, MIDI links and more", Icons::Type::settings, "SETTINGS" };
    IconButton previousRig { "Previous preset", Icons::Type::chevronLeft }, nextRig { "Next preset", Icons::Type::chevronRight };
    juce::TextButton rigNameButton;
    IconButton undoButton { "Undo (Ctrl+Z)", Icons::Type::undo }, redoButton { "Redo (Ctrl+Shift+Z)", Icons::Type::redo },
               saveButton { "Save the preset (as a new preset if it's a factory one or not saved yet)", Icons::Type::save },
               settingsButton { "Settings", Icons::Type::settings };

    GainColumn outputColumn;
    InstrumentKeyboard keyboard;
    MidiLinks shownLinks;
    juce::String shownLearning;

    // The pages
    InstrumentPage instrumentPage;
    FxPage fxPage;
    MixerPage mixerPage;   // 0.15.0
    RigBrowser rigBrowser;
    SettingsPage settingsPage;
    juce::TextButton keyboardButton { "HIDE\nKEYBOARD" };
    bool keyboardShown = true;
    ProgressBox progressBox;   // over everything while a long load runs
    double loadingSince = -1.0;
    Page page = Page::instrument;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
