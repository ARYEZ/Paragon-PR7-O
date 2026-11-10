#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

// THE ORCHESTRA VIEW (PR7-O 0.5.0): the INSTRUMENT page's picture, in the family's "Tron" look (PR7-D's kit, PR7-I's
// guitar): our own string family modelled and rendered in Blender (resources/orchestra/strings3d.py), the owner's choice
// of both mock-ups ("build both"):
//   - ONE INSTRUMENT (a violin, viola, cello or double bass loaded): it lies across the panel, scroll to the right, its
//     low string on top; each note sounding is lit on its string where a player would stop it, the string glowing from
//     there to the bridge (as PR7-I's guitar lights its notes).
//   - A SECTION (0.4.0's split, with string parts): bass, cello, viola and violin standing side by side, low to high;
//     each one's lit picture fades in while its part plays, its notes lit on its strings.
// Which instrument a file is comes from its name (orchestra::kindOf); anything else (woodwinds, percussion: not
// modelled yet) has no picture, and the page shows its region map.
//
// 0.6.0, THE BRASS (resources/orchestra/brass3d.py; the owner: "Build it but make the metal a bit more visible"): our own
// trumpet, horn, trombone and tuba in the same look. One lying across (bell right): the valves a player presses for the
// note lit (the trombone: its slide positions 1-7 marked where the hand goes, the note's lit). A brass section (tuba,
// trombone, horn, trumpet): the tuba standing at the left, the trombone across the top, horn and trumpet under it, each
// lit while its part plays. A section of both families shows the family with more parts.
//
// The notes' strings (our own rule, a player's in low position): the highest open string at or under the note, each
// string one note (a chord's lower notes move down a string); under the lowest string: the lowest, open. Sounding
// pitch: the key plus TRANSPOSE.
namespace orchestra
{
    enum class Kind { none, violin, viola, cello, bass, trumpet, horn, trombone, tuba };
    Kind kindOf (const juce::String& name);
    bool isBrass (Kind);
    std::array<int, 4> openStrings (Kind);   // MIDI notes, low to high (-1 for the brass)
    juce::String nameOf (Kind);
    // Each note's string and how many semitones over it (-1: no string), for these notes (any order).
    struct Stop { int note = 0, string = -1, semitones = 0; };
    std::vector<Stop> stopsFor (Kind, std::vector<int> notes);
    // How far along its string (nut 0, bridge 1) a stop is drawn: where it's stopped, but never past `edge` (px) when
    // the bridge end is off the picture's left edge (0.5.1).
    float alongString (int semitones, float nutX, float bridgeX, float edge);

    // A brass note's fingering (our own rule from the usual charts, sounding pitch): the lowest of its harmonics at or
    // over the note, lowered by the valves (2: a semitone, 1: two, 1+2: three, 2+3: four, 1+3: five, 1+2+3: six) or the
    // trombone's slide (1st to 7th position). B-flat trumpet, F horn (its F side: the thumb valve stays up), B-flat
    // tenor trombone (no F trigger), 4-valve B-flat tuba (4 for five, 2+4 for six, down to 1+2+3+4 under its second
    // harmonic). The 7th and 11th harmonics are left out (out of tune). `playable` false: out of the instrument's reach.
    struct Fingering { bool playable = false; int valves = 0; int position = 0; };   // valves: bit 0 = valve 1
    Fingering fingeringFor (Kind, int note);
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
    static const juce::Image& getSectionPicture (bool lit, bool brass = false) { return section (lit, brass).image; }
    bool showsBrass() const noexcept { return brassSection || orchestra::isBrass (kind); }
    // The note a brass instrument shows (the highest held in its keys; -1: none), and its fingering (tests).
    int getBrassNote (orchestra::Kind) const;
    // A brass instrument's valves and the trombone's hand at 1st / 7th in its picture (px; tests).
    static std::vector<juce::Point<float>> getValves (orchestra::Kind k, bool inSection);
    static bool hasSlide (orchestra::Kind k, bool inSection);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    std::vector<int> soundingNotes (int low, int high) const;
    juce::Rectangle<float> pictureArea (const juce::Image&) const;   // where the picture goes (filling the view, centred)

    // An instrument's geometry in its picture (px): a string instrument's strings, a brass one's valves (the finger
    // buttons / rotary valves' faces, valve 1 first) and the trombone's hand at 1st and 7th position.
    struct Strings
    {
        std::array<juce::Point<float>, 4> bridge, nut;
        juce::Rectangle<float> area;
        std::vector<juce::Point<float>> valves;
        std::optional<std::pair<juce::Point<float>, juce::Point<float>>> slide;
    };
    struct Picture
    {
        juce::Image image;
        std::map<orchestra::Kind, Strings> instruments;
        // The lit section's: each instrument's lit area, its edges faded out (no seam where it meets the dim picture).
        std::map<orchestra::Kind, std::pair<juce::Image, juce::Rectangle<int>>> litAreas;
    };
    static const Picture& single (orchestra::Kind);
    static const Picture& section (bool lit, bool brass);

    void paintStops (juce::Graphics&, const Strings&, const std::vector<orchestra::Stop>&, juce::AffineTransform) const;
    void paintFingering (juce::Graphics&, const Strings&, orchestra::Kind, int note, juce::AffineTransform) const;

    ParagonX1AudioProcessor& processor;
    Mode mode = Mode::none;
    orchestra::Kind kind = orchestra::Kind::none;
    std::map<orchestra::Kind, std::pair<int, int>> partKeys;   // a section's pictured parts: their keys
    bool brassSection = false;                                 // a section shown as the brass
    std::map<orchestra::Kind, float> glow;
    std::array<bool, 128> shownHeld {};
    int shownTranspose = 0;
    juce::String shownSource;
    std::shared_ptr<const sampler::Program> shownProgram;
};
