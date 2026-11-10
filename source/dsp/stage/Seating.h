#pragma once

#include <juce_core/juce_core.h>

#include <vector>

// THE STAGE's SEATS (PR7-O 0.8.0, our own design; the owner: "Let's do the stage"). Where each part sits, in metres on
// our stage (resources/orchestra/stage3d.py): x across from the conductor's left (-8.6 to 8.6), y back from the
// stage's front (0.3 to 11), and how wide the players spread across. A part's default seat comes from its name, our
// own layout after the usual modern orchestra plan seen from the conductor: violins front left, violas centre right,
// cellos front right, basses behind them, woodwinds in the middle on the first riser, horns left behind them,
// trumpets in the middle at the back, trombones and tuba right, timpani and percussion on the top riser, harp and
// keyboards at the left edge.
namespace stage
{
    struct Seat
    {
        float x = 0.0f, y = 5.0f, width = 3.0f;
        bool operator== (const Seat& o) const { return x == o.x && y == o.y && width == o.width; }
    };

    inline constexpr float halfWidth = 8.6f, front = 0.3f, back = 11.0f;
    Seat clamp (Seat) noexcept;

    // A part's usual seat, from its name ("ViolinEns-KS", "TromboneSus"...).
    Seat seatFor (const juce::String& name);

    // The orchestra's places (drawn faintly on the stage as a guide): a name and a seat.
    struct Place { const char* name; Seat seat; };
    const std::vector<Place>& places();
}
