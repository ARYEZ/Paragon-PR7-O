#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/SynthControls.h"

// THE KEYBOARD (PR7-O 0.1.0): 88 keys under the pages, in the plugin's colours; the keys the instrument plays tinted,
// keys sounding lit (the DAW's notes too). Click to play.
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

    struct Poll final : juce::Timer
    {
        std::function<void()> callback;
        void timerCallback() override { callback(); }
    };
    ParagonX1AudioProcessor& processor;
    std::shared_ptr<const sampler::Program> shown;
    int shownOctave = 3;
    Poll poll;
};

// THE REGION MAP: the instrument's regions as boxes over keys (across) and velocity (up), the way SFZ editors draw
// them: where each recording plays. A box's colour by its velocity range (soft ones orange, loud ones red: the family's
// note colours); several round robins on one place drawn as one box with a count. The keys sounding now lit.
class RegionMap final : public juce::Component, private juce::Timer
{
public:
    explicit RegionMap (ParagonX1AudioProcessor&);
    void paint (juce::Graphics&) override;
    void setProgram (std::shared_ptr<const sampler::Program>);

private:
    void timerCallback() override;
    juce::Rectangle<float> boxFor (int loKey, int hiKey, int loVel, int hiVel) const;
    ParagonX1AudioProcessor& processor;
    std::shared_ptr<const sampler::Program> program;
    struct Box { int loKey, hiKey, loVel, hiVel, count; };
    std::vector<Box> boxes;
    std::array<bool, 128> lit {};
    int firstKey = 21, lastKey = 108;   // the keys shown: the keyboard's 88, wider if the instrument reaches past them
};

// THE INSTRUMENT page: LOAD... (an SFZ file, or a single sample over the keyboard; or drop one on the page), what it
// holds (its regions, keys, velocity layers, round robins, memory, anything not read yet), the region map, and how it
// plays (LEVEL, PAN, TRANSPOSE, TUNE, RELEASE, POLYPHONY) and where it goes (SEND DELAY, SEND REVERB, INSERT BUS).
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
    juce::TextButton& getLoadButton() noexcept { return loadButton; }
    juce::TextButton& getClearButton() noexcept { return clearButton; }
    juce::String getInfoText() const { return info.getText(); }   // tests

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void chooseFile();

    ParagonX1AudioProcessor& processor;
    juce::TextButton loadButton { "LOAD..." }, clearButton { "CLEAR" };
    juce::Label nameLabel, fileLabel;
    juce::TextEditor info;
    RegionMap map;
    std::vector<std::unique_ptr<ParamControl>> playControls, sendControls;
    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<const sampler::Program> shown;
    juce::String shownFile;
    bool shownLoading = false;
    juce::Rectangle<int> headArea, infoArea, mapArea, playArea, sendArea;
};
