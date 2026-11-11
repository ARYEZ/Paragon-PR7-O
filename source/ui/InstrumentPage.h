#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/InstrumentRack.h"
#include "ui/OrchestraView.h"
#include "ui/StageView.h"
#include "ui/LibraryBrowser.h"
#include "ui/SynthControls.h"

// THE KEYBOARD (PR7-O 0.1.0): 88 keys under the pages, in the plugin's colours; the keys the instrument plays tinted,
// keys sounding lit (the DAW's notes too). Click to play. 0.2.0: KEYSWITCH keys marked in orange (the chosen one
// brighter); clicking one switches.
class InstrumentKeyboard final : public juce::MidiKeyboardComponent
{
public:
    InstrumentKeyboard (ParagonX1AudioProcessor&);
    static constexpr int lowestKey = 21, highestKey = 108;   // A0 to C8: a piano's, and every orchestral instrument's
    void layOut();   // key widths for its bounds (after setBounds)
    void paint (juce::Graphics&) override;

private:
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour noteFillColour) override;
    bool plays (int note) const noexcept { return shown != nullptr && shown->plays (note); }
    float tintOf (int note) const noexcept;   // 0.4.0: a section's parts shaded in turn
    bool switches (int note) const noexcept { return shown != nullptr && shown->isKeyswitch (note); }
    void markSwitch (juce::Graphics&, int note, juce::Rectangle<float> band);

    struct Poll final : juce::Timer
    {
        std::function<void()> callback;
        void timerCallback() override { callback(); }
    };
    ParagonX1AudioProcessor& processor;
    std::shared_ptr<const sampler::Program> shown;
    int shownOctave = 3, shownSwitch = -1;
    juce::String shownSource;
    std::array<int, 128> partOfKey {};   // a section's part playing each key (-1: none, or not a section)
    Poll poll;
};

// THE REGION MAP: the instrument's regions as boxes over keys (across) and velocity (up), the way SFZ editors draw
// them: where each recording plays. A box's colour by its velocity range (soft ones orange, loud ones red: the family's
// note colours); several round robins on one place drawn as one box with a count. The keys sounding now lit. 0.2.0: of
// an instrument with keyswitches, the articulation chosen (and the regions every articulation plays).
class RegionMap final : public juce::Component, private juce::Timer
{
public:
    explicit RegionMap (ParagonX1AudioProcessor&);
    void paint (juce::Graphics&) override;
    void setProgram (std::shared_ptr<const sampler::Program>);
    int getNumBoxes() const noexcept { return (int) boxes.size(); }   // tests

private:
    void timerCallback() override;
    void rebuild();
    int shownSwitch = -1;
    juce::Rectangle<float> boxFor (int loKey, int hiKey, int loVel, int hiVel) const;
    ParagonX1AudioProcessor& processor;
    std::shared_ptr<const sampler::Program> program;
    struct Box { int loKey, hiKey, loVel, hiVel, count; };
    std::vector<Box> boxes;
    std::array<bool, 128> lit {};
    int firstKey = 21, lastKey = 108;   // the keys shown: the keyboard's 88, wider if the instrument reaches past them
};

// An articulation's name as the page shows it: a keyswitch label that starts with its key's name in SFZ's naming
// ("C2 Sustain", middle C c4) has that name replaced by the plugin's (NOTE NAMES: the keyboard's and the DAW's).
juce::String articulationName (const sampler::Program::Articulation&);

// THE INSTRUMENT page: LOAD... (an SFZ file, or a single sample over the keyboard; or drop one on the page), what it
// holds (its regions, keys, velocity layers, round robins, memory, anything not read yet), the region map, and how it
// plays (LEVEL, PAN, TRANSPOSE, TUNE, RELEASE, POLYPHONY) and where it goes (SEND DELAY, SEND REVERB, INSERT BUS).
// 0.4.0: NEW SECTION... (several SFZ files, or drop several): a section split over the keys, its SPLITS listed, each
// split point moved a semitone with - / + (applied 0.3 s after the last click: one reload, one undo step).
// 0.5.0 (the owner: "build both ... for the knob and button menu let's use the rack system from PR7-I"): the knobs,
// menus and keys on a rack unit across the top (InstrumentRack: LOAD, SECTION, CLEAR under its screen; PLAYING and
// ROUTING pages); the picture panel shows THE ORCHESTRA VIEW (OrchestraView) for a string instrument or section, its
// region map one click away (INSTRUMENT / REGIONS); anything else shows the region map.
class InstrumentPage final : public juce::Component,
                             public juce::FileDragAndDropTarget,
                             private juce::ChangeListener,
                             private juce::Timer
{
public:
    explicit InstrumentPage (ParagonX1AudioProcessor&);
    ~InstrumentPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    static bool isInstrumentFile (const juce::File&);

    void refresh();   // from the processor's instrument now
    juce::TextButton& getLoadButton() noexcept { return rack.loadKey; }
    // 0.2.0: the instrument's ARTICULATIONS (its keyswitches): a button each, the chosen one lit; click to switch.
    int getNumArticulations() const noexcept { return (int) articulationButtons.size(); }
    juce::TextButton& getArticulationButton (int i) noexcept { return *articulationButtons[(size_t) i]; }
    juce::TextButton& getClearButton() noexcept { return rack.clearKey; }
    juce::TextButton& getNewSectionButton() noexcept { return rack.sectionKey; }
    // 0.5.0: the rack, the picture, and which the panel shows (the picture when there's one, unless REGIONS is chosen).
    InstrumentRack& getRack() noexcept { return rack; }
    OrchestraView& getOrchestraView() noexcept { return picture; }
    bool isShowingPicture() const noexcept { return picture.isVisible(); }
    void chooseRegions (bool regions);
    // 0.8.0: the panel's STAGE view (each part's seat on our stage), between INSTRUMENT and REGIONS.
    StageView& getStageView() noexcept { return stageView; }
    // 0.11.0: THE LIBRARY BROWSER, opened by LOAD over the page below the rack.
    LibraryBrowser& getLibraryBrowser() noexcept { return browser; }
    void showLibrary (bool show);
    bool isShowingLibrary() const noexcept { return browser.isVisible(); }
    bool isShowingStage() const noexcept { return stageView.isVisible(); }
    void chooseStage();
    // 0.4.0: a section's SPLITS: a row a part (its name and keys); moving the split under part `part` (1 on) by
    // `semitones` (each part keeps a key at least), applied after a moment (applyPendingSplits: now).
    int getNumSplitRows() const noexcept { return (int) splitRows.size(); }
    juce::String getSplitRowText (int part) const;
    bool moveSplit (int part, int semitones);
    void applyPendingSplits();
    juce::String getInfoText() const { return info.getText(); }   // tests

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void chooseFile();
    void chooseSection();

    ParagonX1AudioProcessor& processor;
    InstrumentRack rack;
    juce::TextEditor info;
    RegionMap map;
    OrchestraView picture;
    StageView stageView;
    LibraryBrowser browser;
    juce::TextButton showPicture { "INSTRUMENT" }, showStage { "STAGE" }, showRegions { "REGIONS" };
    bool regionsChosen = false, stageChosen = false;
    std::vector<std::unique_ptr<ParamControl>> playControls, sendControls, stageControls, ensembleControls;   // the rack's knobs
    std::unique_ptr<ParamControl> dynamicsMenu, insertMenu, roomMenu, doubleMenu;                             // the rack's menus
    juce::TextButton stageOnKey, resetSeatsKey;   // 0.8.0: the STAGE page's keys
    std::unique_ptr<juce::ParameterAttachment> stageOnAttachment;
    juce::TextButton autoSustainKey;
    std::unique_ptr<juce::ParameterAttachment> autoSustainAttachment;
    juce::TextButton legatoKey;   // 0.7.0
    std::unique_ptr<juce::ParameterAttachment> legatoAttachment;
    void showRackPage();
    void showView();       // the picture or the region map
    void updateScreen();   // the rack's screen, from the instrument now
    void paintOverChildren (juce::Graphics&) override;
    std::vector<std::unique_ptr<juce::TextButton>> articulationButtons;
    int shownSwitch = -1, shownOctave = -1;
    juce::Rectangle<int> articulationArea;
    void rebuildArticulations();
    struct SplitRow { juce::Label name, keys; juce::TextButton down { "-" }, up { "+" }; };
    std::vector<std::unique_ptr<SplitRow>> splitRows;
    std::optional<sampler::section::Section> editedSection;   // splits moved, not yet applied
    double editedAt = 0.0;
    juce::Rectangle<int> splitArea;
    void rebuildSplits();
    void showSplits();   // the rows' text, from the section shown (or the one being edited)
    void updateInfo();   // the text, for the articulation chosen
    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<const sampler::Program> shown;
    juce::String shownFile;
    bool shownLoading = false;
    juce::Rectangle<int> infoArea, mapArea;
};
