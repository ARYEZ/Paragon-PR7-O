#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// THE ORCHESTRA VIEW (PR7-O 0.5.0): the INSTRUMENT page's picture, in the family's "Tron" look (PR7-D's kit, PR7-I's
// guitar): our own string family modelled and rendered in Blender (resources/orchestra/strings3d.py), the owner's choice
// of both mock-ups ("build both"):
//   - ONE INSTRUMENT (a violin, viola, cello or double bass loaded): it lies across the panel, scroll to the right, its
//     low string on top; each note sounding is lit on its string where a player would stop it, the string glowing from
//     there to the bridge (as PR7-I's guitar lights its notes).
//   - A SECTION (0.4.0's split, with string parts): bass, cello, viola and violin standing side by side, low to high;
//     each one's lit picture fades in while its part plays, its notes lit on its strings.
// Which instrument a file is comes from its name (orchestra::kindOf); anything else (brass, woodwinds, percussion: not
// modelled yet) has no picture, and the page shows its region map.
//
// The notes' strings (our own rule, a player's in low position): the highest open string at or under the note, each
// string one note (a chord's lower notes move down a string); under the lowest string: the lowest, open. Sounding
// pitch: the key plus TRANSPOSE.
namespace orchestra
{
    enum class Kind { none, violin, viola, cello, bass };
    Kind kindOf (const juce::String& name);
    std::array<int, 4> openStrings (Kind);   // MIDI notes, low to high
    juce::String nameOf (Kind);
    // Each note's string and how many semitones over it (-1: no string), for these notes (any order).
    struct Stop { int note = 0, string = -1, semitones = 0; };
    std::vector<Stop> stopsFor (Kind, std::vector<int> notes);
}

class OrchestraView final : public juce::Component, private juce::Timer
{
public:
    explicit OrchestraView (ParagonX1AudioProcessor&);

    enum class Mode { none, single, section };
    Mode getMode() const noexcept { return mode; }
    orchestra::Kind getKind() const noexcept { return kind; }           // the single instrument shown
    void refresh();                                                    // from the processor's instrument now
    float getGlow (orchestra::Kind k) const noexcept;                  // a section's part: how lit (0 to 1)
    std::vector<orchestra::Stop> getStops (orchestra::Kind) const;     // the notes lit on its strings now (tests)
    // The pictures as loaded (tests): one instrument's, or the section's (dim or lit).
    static const juce::Image& getPicture (orchestra::Kind k) { return single (k).image; }
    static const juce::Image& getSectionPicture (bool lit) { return section (lit).image; }

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    std::vector<int> soundingNotes (int low, int high) const;
    juce::Rectangle<float> pictureArea (const juce::Image&) const;   // where the picture goes (filling the view, centred)

    struct Strings { std::array<juce::Point<float>, 4> bridge, nut; juce::Rectangle<float> area; };
    struct Picture
    {
        juce::Image image;
        std::map<orchestra::Kind, Strings> instruments;
        // The lit section's: each instrument's lit area, its edges faded out (no seam where it meets the dim picture).
        std::map<orchestra::Kind, std::pair<juce::Image, juce::Rectangle<int>>> litAreas;
    };
    static const Picture& single (orchestra::Kind);
    static const Picture& section (bool lit);

    void paintStops (juce::Graphics&, const Strings&, const std::vector<orchestra::Stop>&, juce::AffineTransform) const;

    ParagonX1AudioProcessor& processor;
    Mode mode = Mode::none;
    orchestra::Kind kind = orchestra::Kind::none;
    std::map<orchestra::Kind, std::pair<int, int>> partKeys;   // a section's string parts: their keys
    std::map<orchestra::Kind, float> glow;
    std::array<bool, 128> shownHeld {};
    int shownTranspose = 0;
    juce::String shownSource;
    std::shared_ptr<const sampler::Program> shownProgram;
};
