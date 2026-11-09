#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// What a control asks of the window it's in (PR7-K's stage 4f): MIDI learn. The main view answers; a control finds it
// among its parents.
class ControlHost
{
public:
    virtual ~ControlHost() = default;

    virtual int linkedController (const juce::String& parameterId) const = 0;   // -1: none
    virtual bool isLearning (const juce::String& parameterId) const = 0;
    virtual void learn (const juce::String& parameterId) = 0;                  // or stop learning, if it is
    virtual void forget (const juce::String& parameterId) = 0;
};

// The pages' controls (PR7-K's synth page's). One ParamControl shows one parameter, as
// whatever suits its type:
//   a number   -> a knob, its name above and its value below
//   a choice   -> a drop-down menu, its name above
//   an on/off  -> an LED switch, its name above
// It can be pointed at another parameter of the same kind (attach), which is
// how the DRUMS page works: one set of controls, re-pointed at the chosen
// drum's parameters. Right-click for MIDI learn.
class ParamControl final : public juce::Component
{
public:
    ParamControl (juce::AudioProcessorValueTreeState&, const juce::String& parameterId, const juce::String& label,
                  const juce::String& tooltip = {}, bool knobForChoices = false);   // (a menu's parameter as a knob: PR7-I 0.5.0's rack)
    ~ParamControl() override;

    void attach (const juce::String& parameterId);

    // Compact: no name above (for a strip beside a panel's title). A switch
    // shows its name beside its LED instead; a menu fills the space.
    void setCompact (bool);
    void setLabelText (const juce::String& text) { label.setText (text, juce::dontSendNotification); }   // its name above
    // PR7-I 0.4.0: its name under the value instead (the GUITAR page's knobs, as the reference's are labelled).
    void setLabelBelow (bool);
    // PR7-I 0.5.0: as on PR7-G's rack: the name stamped above, the rack's big knob, the value on a small orange screen.
    void setRack (bool);
    const juce::String& getParameterId() const noexcept { return parameterId; }

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    // The cards' height for a knob: name, knob, value.
    static constexpr int knobHeight = 86;

private:
    ControlHost* host() const { return findParentComponentOfClass<ControlHost>(); }
    void showMenu();

    juce::AudioProcessorValueTreeState& state;
    juce::String parameterId;
    bool compact = false, labelBelow = false, rack = false;
    juce::Rectangle<float> valueScreen() const;
    juce::Label label;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> box;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> boxAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
};

// A titled dark panel that lays its controls out in rows.
class SynthPanel : public juce::Component
{
public:
    explicit SynthPanel (const juce::String& title);

    void paint (juce::Graphics&) override;

    // Where the content goes, under the title, and the title's strip (for things beside it).
    juce::Rectangle<int> getContentArea() const;
    juce::Rectangle<int> getTitleArea() const { return getLocalBounds().reduced (14, 0).withHeight (32); }

    // Lays `controls` out side by side across `row`, each `width` wide (or
    // sharing the row, if 0), centred.
    static void layoutRow (juce::Rectangle<int> row, const std::vector<juce::Component*>& controls, int width = 0, int gap = 4);

protected:
    juce::String title;
};
