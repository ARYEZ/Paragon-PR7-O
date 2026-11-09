#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// PR7-D 0.38.0: copied from PR7-K 0.63.0 for THE DRUM MACHINE's face (MachineView), so it's PR7-K's pearl
// (one change: the pearl white sheen's lavender is a pearl pink here: the family's colours only).
// Ported from PR7-G 0.52.0 (unchanged but for the badge, below) for PR7-K's front panel (0.50.0), so the face is
// drawn in exactly PR7-G's pearl. PR7-K uses paintUnit and paintBadge; the rest is PR7-G's floor unit, kept as is.
//
// The floor unit from the mockup, drawn as one piece of pearl (white, or black: see Finish.h):
// a thick bevelled rim with the red-orange light spilling round it, the
// satin face plate with screws in its corners, recessed section panels
// (the slot row and the footswitch row), the panels behind the slot windows, the
// PARAGON PR7-G badge, and the bay on the right that the expression pedal sits
// in. Purely visual: the controls on it are separate components on top.
class PedalBody final : public juce::Component
{
public:
    PedalBody();

    // Positions in this component's coordinates, set by the layout code.
    struct Geometry
    {
        juce::Rectangle<float> face;                       // the main face plate
        juce::Rectangle<float> pedalBay;                   // the expression pedal's housing
        std::vector<juce::Rectangle<float>> sections;      // raised panels (slot row, footswitch row)
        std::vector<juce::Rectangle<float>> insets;        // recessed panels (behind each slot window)
        juce::Rectangle<float> badge;                      // PARAGON over the PR7-G mark
        juce::Path printedRed, printedInk;                 // lines printed on the face (the tuner bracket, SAVE—SAVE AS)
    };
    void setGeometry (Geometry);

    // Redraws it in the current finish (see Finish.h).
    void refinish();

    void paint (juce::Graphics&) override;
    void resized() override;

    // The unit itself, for drawing smaller units in the same pearl (the
    // FACEPLATE): the glow round it, the thick pearl rim and its front edge,
    // and the satin face plate sunk into it, in the current finish. The
    // main unit is drawn by these too, so the two always match.
    struct Shape
    {
        float chassisRadius = 28.0f;
        float faceRadius = 16.0f;
        float frontEdge = 8.0f;     // the unit's thickness, below the rim's top
        float glow = 1.0f;          // how far the light round it spreads (1: the main unit's)
    };
    static void paintUnit (juce::Graphics&, juce::Rectangle<float> chassis, juce::Rectangle<float> face,
                           const std::function<void (juce::Graphics&)>& drawSatin, Shape);
    // PARAGON over the PR7 mark, stamped into the face, with the caption printed in red under it (PR7-G's badge;
    // PR7-K: static, the caption passed in).
    static void paintBadge (juce::Graphics&, juce::Rectangle<float> area, const juce::String& caption);
    // A section panel set down into the face (the main unit's slot and footswitch rows).
    static void paintSection (juce::Graphics&, juce::Rectangle<float> section, const std::function<void (juce::Graphics&)>& drawSatin);

private:
    void makeSatinTexture();
    void drawSatin (juce::Graphics&) const;   // the pearl satin grain over the whole component (clip first)
    static void strokeTopHighlight (juce::Graphics&, juce::Rectangle<float> area, float radius, float thickness, float alpha);

    juce::Image satin;
    Geometry geometry;
};
