#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Line icons from the mockup, drawn as vectors so they stay sharp at any
// window size. Each is designed in a 0-1 square and stroked by drawIcon().
namespace Icons
{
    enum class Type { amp, cabinet, rig, comp, drive, modulation, delay, reverb, guitar, speaker,
                      // Phase 6: header and expression pedal
                      processor, presets, undo, redo, tap, tuner, volume, wah,
                      // Phase 7: header and dock
                      save, settings, star, user, factory, plus, chevronLeft, chevronRight, flame,
                      // Phase 8
                      eq,
                      // Version 0.10
                      filterPitch,
                      // Version 0.12
                      loFi,
                      // Version 0.24
                      glitch,
                      // Version 0.27
                      pitch,
                      // PR7-K 0.15: the arpeggiator
                      arpeggio,
                      // PR7-K 0.17: the rack (FX)
                      rack,
                      // PR7-K 0.21: the drums
                      drums,
                      // PR7-D 0.4.0: the grooves
                      grooves,
                      // PR7-D 0.20.0: the mixer
                      mixer,
                      // PR7-O 0.1.0: the instrument (a violin)
                      orchestra };

    juce::Path get (Type type);

    // Draws `type` fitted into `area`, with an optional soft glow behind it.
    void draw (juce::Graphics&, Type type, juce::Rectangle<float> area, juce::Colour colour,
               float strokeWidth = 1.6f, bool glow = false);
}

// A button with a line icon and optional text beside it, as on the header
// (SYNTH, PRESETS, undo, redo). When it's a toggle and on, it's drawn in the accent colour.
class IconButton final : public juce::Button
{
public:
    IconButton (const juce::String& name, Icons::Type icon, const juce::String& text = {},
                juce::Justification justification = juce::Justification::centred);

    // Draws a box round the button (on the header the tabs have none).
    void setOutlined (bool shouldBeOutlined) { outlined = shouldBeOutlined; repaint(); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icons::Type icon;
    juce::String text;
    juce::Justification justification;
    bool outlined = false;
};
