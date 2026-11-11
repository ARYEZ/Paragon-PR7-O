#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// THE MIXER tab (PR7-O 0.15.0; the owner, on the rack-style mock-up: "OMG that Tron style mixer looks amazing!!! Build
// it in that style and give it its own mixer tab"). A strip for each part of the instrument, then the SECTION strip
// after the stage, each one of the rack's units standing up (its pearl black chassis and red light, LED screens, the
// rack's knobs and neon keys). Top to bottom: the part's name and keys; EFFECTS (four slots: a key switches each on or
// off, its screen shows the effect; click an empty one for the list, a loaded one to edit it); SENDS AND PAN (DELAY,
// REVERB, PAN); LEVEL (the fader beside its meter; double-click: 0 dB); MUTE, SOLO, PHASE; OUTPUT (where it goes). The
// effect's editor is a rack unit over the strips (its knobs, ON, OUTPUT, the type, CLOSE). See dsp/mixer/PartMixer.h.
class MixerPage final : public juce::Component,
                        private juce::Timer
{
public:
    explicit MixerPage (ParagonX1AudioProcessor&);
    ~MixerPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    class Strip;
    class EffectEditor;

    // Tests and snapshots.
    int getNumStrips() const noexcept { return (int) strips.size(); }   // the parts', then the section's
    Strip& getStrip (int i) noexcept { return *strips[(size_t) i]; }
    juce::String getStripName (int i) const;
    void chooseEffect (int strip, int slot, int type);   // as the slot's list does (0: remove)
    void openEditor (int strip, int slot);
    void closeEditor();
    bool isEditorOpen() const;
    EffectEditor& getEditor() noexcept { return *editor; }
    void refresh();   // the strips from the processor now (the timer does this when they change)
    void removeEffectForTest (int strip, int slot) { chooseEffect (strip, slot, 0); }
    enum class Key { mute, solo, phase };
    juce::TextButton& getKey (int strip, Key);
    juce::TextButton& getEffectKey (int strip, int slot);
    juce::Slider& getPanKnob (int strip);
    void moveFader (int strip, float db);   // as a drag let go there
    juce::Rectangle<int> getStripBounds (int strip) const;
    int getEditorKnobCount() const;
    juce::Slider& getEditorKnob (int i);

private:
    void timerCallback() override;
    void rebuild();
    friend class Strip;
    friend class EffectEditor;
    mixer::StripSettings settingsOf (int strip) const;
    void apply (int strip, const mixer::StripSettings&, bool final);
    void showSlotMenu (int strip, int slot);

    ParagonX1AudioProcessor& processor;
    juce::StringArray parts;   // the strips' part names, in order (the section's strip after them)
    std::vector<std::unique_ptr<Strip>> strips;
    std::unique_ptr<EffectEditor> editor;
    int shownVersion = -1;
    juce::Rectangle<int> stripsArea;
};
