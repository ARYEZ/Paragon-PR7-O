#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// PR7-O 0.5.0: the INSTRUMENT page's knobs and keys as one of the family's rack units across the top of the page (the
// owner: "for the knob and button menu let's use the rack system from PR7-I"), drawn exactly as PR7-I's GuitarRack
// (PR7-G's parts: the pearl black chassis with the red light round it, the PARAGON PR7-O badge, stamped lettering and
// printed red lines, the orange LED screen, the big knobs with their values on small screens, the small keys with neon
// stripes). Always pearl black, as PR7-I's: it sits on the dark instrument picture.
//
// Left to right: the badge and the PAGE keys (PLAYING, ROUTING); the screen (the instrument, where it's from, the
// articulation) over the INSTRUMENT keys (LOAD, SECTION, CLEAR); the page: PLAYING (LEVEL, PAN, TRANSPOSE, TUNE,
// RELEASE, POLYPHONY; the DYNAMICS menu and the AUTO SUSTAIN key) or ROUTING (SEND DELAY, SEND REVERB, the INSERT BUS
// menu). The window's OUTPUT stays where it is, beside the pages.
//
// Only the unit's face, its page keys and its instrument keys: the knobs and menus on it (the page's) are its
// children, laid out in the cells it gives, shown and hidden by the page as the PAGE changes (onPageChanged).
class InstrumentRack final : public juce::Component
{
public:
    InstrumentRack();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    struct Screen
    {
        juce::String caption, name, corner, line1, line2;
        bool operator== (const Screen&) const = default;
    };
    void setScreen (const Screen&);
    const Screen& getScreen() const noexcept { return screen; }

    enum class Page { playing, routing };
    void setPage (Page);
    Page getPage() const noexcept { return page; }
    std::function<void()> onPageChanged;
    juce::TextButton& getPageKey (Page p) noexcept { return p == Page::playing ? playingKey : routingKey; }

    // The INSTRUMENT keys under the screen.
    juce::TextButton loadKey, sectionKey, clearKey;

    // Where the page's controls go, in the unit's pixels.
    static constexpr int numKnobs = 7;
    juce::Rectangle<int> knobCell (int index) const;          // PLAYING: LEVEL ... POLYPHONY, GLIDE (0.7.0)
    juce::Rectangle<int> menuCell (int index) const;          // PLAYING: DYNAMICS (0); ROUTING: INSERT BUS (0)
    juce::Rectangle<int> pageKeyCell (int index) const;       // PLAYING: AUTO SUSTAIN (0), LEGATO (1; 0.7.0)
    juce::Rectangle<int> routingKnobCell (int index) const;   // ROUTING: SEND DELAY, SEND REVERB
    juce::Rectangle<int> screenArea() const;
    juce::Rectangle<int> instrumentKeyCell (int index) const; // LOAD, SECTION, CLEAR

    static constexpr int preferredHeight = 236;

private:
    juce::Rectangle<float> face() const;
    juce::Rectangle<float> content() const;
    juce::Rectangle<float> badgeColumn() const;
    juce::Rectangle<float> displayColumn() const;
    juce::Rectangle<float> pageArea() const;      // under the page's title
    juce::Rectangle<float> sideOfPage() const;    // the page's right-hand column (menu and key)
    juce::Rectangle<int> pageSwitchCell (int index) const;
    void paintScreen (juce::Graphics&);

    juce::Image satin;
    Screen screen;
    Page page = Page::playing;
    juce::TextButton playingKey, routingKey;
};
