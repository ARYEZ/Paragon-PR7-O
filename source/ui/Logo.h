#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The PR7-O mark: heavy, wide, forward-slanted block letters with two "speed"
// cuts through their lower half, in the spirit of late-'90s floor
// processors. Drawn from vector outlines (not a font), so it's the same
// shape everywhere: the header and the unit's badge.
namespace Logo
{
    struct Style
    {
        juce::Colour letters;                              // P, R and "-O"
        juce::Colour seven;                                // the 7, usually the accent colour
        juce::Colour highlight = juce::Colours::transparentBlack;   // a light edge underneath, to look stamped into metal
        juce::Colour outline = juce::Colours::transparentBlack;     // an engraved outline
        float outlineWidth = 1.0f;
        bool glow = false;                                 // a soft neon glow behind the 7
    };

    // Width of the mark for a given letter height.
    float widthFor (float height) noexcept;

    // Draws the mark as large as fits in `area`, placed by `justification`.
    // Returns where it was drawn.
    juce::Rectangle<float> drawMark (juce::Graphics&, juce::Rectangle<float> area, juce::Justification justification, const Style&);

    // The PR7-O icon (PR7-D 0.57.0's, in the style of PR7-S's "7S" icon): a big slanted "7O" (the 7
    // `sevenTop` above its first speed cut, `sevenLow` below, the O `letters`), a line under it, the "PR7-O" mark
    // smaller under that (its 7 `accent`), all in a rounded frame (`accent`). Drawn in the square `area`.
    struct IconStyle
    {
        juce::Colour sevenTop, sevenLow, letters, accent;
    };
    void drawIcon (juce::Graphics&, juce::Rectangle<float> area, const IconStyle&);
}
