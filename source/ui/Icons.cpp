#include "Icons.h"

#include "ParagonLookAndFeel.h"

namespace Icons
{
    namespace
    {
        // A pulse line, like a heartbeat: used for compressor and reverb shapes.
        void addPulse (juce::Path& p, std::initializer_list<juce::Point<float>> points)
        {
            bool first = true;
            for (auto point : points)
            {
                if (first)
                    p.startNewSubPath (point);
                else
                    p.lineTo (point);
                first = false;
            }
        }
    }

    juce::Path get (Type type)
    {
        juce::Path p;

        switch (type)
        {
            case Type::amp:
                // Amp head: cabinet outline, grille slots, a row of knobs.
                p.addRoundedRectangle (0.05f, 0.22f, 0.9f, 0.56f, 0.06f);
                p.addRectangle (0.12f, 0.32f, 0.76f, 0.001f);
                for (int i = 0; i < 6; ++i)
                    p.addRectangle (0.16f + (float) i * 0.12f, 0.40f, 0.06f, 0.16f);
                for (int i = 0; i < 5; ++i)
                    p.addEllipse (0.2f + (float) i * 0.14f, 0.62f, 0.05f, 0.05f);
                break;

            case Type::cabinet:
                p.addRoundedRectangle (0.12f, 0.08f, 0.76f, 0.84f, 0.06f);
                p.addEllipse (0.24f, 0.2f, 0.52f, 0.52f);
                p.addEllipse (0.42f, 0.38f, 0.16f, 0.16f);
                p.addEllipse (0.46f, 0.78f, 0.08f, 0.08f);
                break;

            case Type::rig:
                // An isometric cube.
                p.startNewSubPath (0.5f, 0.08f);
                p.lineTo (0.88f, 0.28f);
                p.lineTo (0.88f, 0.72f);
                p.lineTo (0.5f, 0.92f);
                p.lineTo (0.12f, 0.72f);
                p.lineTo (0.12f, 0.28f);
                p.closeSubPath();
                p.startNewSubPath (0.12f, 0.28f);
                p.lineTo (0.5f, 0.48f);
                p.lineTo (0.88f, 0.28f);
                p.startNewSubPath (0.5f, 0.48f);
                p.lineTo (0.5f, 0.92f);
                break;

            case Type::comp:
                addPulse (p, { { 0.0f, 0.5f }, { 0.3f, 0.5f }, { 0.38f, 0.3f }, { 0.46f, 0.82f },
                               { 0.56f, 0.12f }, { 0.64f, 0.66f }, { 0.7f, 0.5f }, { 1.0f, 0.5f } });
                break;

            case Type::drive:
                // A stompbox: body, two knobs, footswitch.
                p.addRoundedRectangle (0.22f, 0.06f, 0.56f, 0.88f, 0.08f);
                p.addEllipse (0.32f, 0.16f, 0.12f, 0.12f);
                p.addEllipse (0.56f, 0.16f, 0.12f, 0.12f);
                p.addEllipse (0.4f, 0.6f, 0.2f, 0.2f);
                break;

            case Type::modulation:
                p.startNewSubPath (0.0f, 0.5f);
                for (int i = 1; i <= 40; ++i)
                {
                    const auto x = (float) i / 40.0f;
                    p.lineTo (x, 0.5f - 0.36f * std::sin (x * juce::MathConstants<float>::twoPi * 2.0f));
                }
                break;

            case Type::delay:
                // Repeats fading round a circle.
                for (int i = 0; i < 9; ++i)
                {
                    const auto angle = (float) i / 9.0f * juce::MathConstants<float>::twoPi * 0.85f;
                    const auto r = 0.05f + 0.06f * (float) (9 - i) / 9.0f;
                    p.addEllipse (0.5f + 0.38f * std::sin (angle) - r, 0.5f - 0.38f * std::cos (angle) - r, r * 2.0f, r * 2.0f);
                }
                break;

            case Type::reverb:
                addPulse (p, { { 0.0f, 0.5f }, { 0.2f, 0.5f }, { 0.26f, 0.05f }, { 0.32f, 0.95f }, { 0.4f, 0.15f },
                               { 0.48f, 0.85f }, { 0.56f, 0.28f }, { 0.64f, 0.72f }, { 0.72f, 0.4f }, { 0.8f, 0.6f },
                               { 0.86f, 0.5f }, { 1.0f, 0.5f } });
                break;

            case Type::guitar:
                // Body and neck of an electric guitar, angled.
                p.addEllipse (0.06f, 0.52f, 0.3f, 0.3f);
                p.addEllipse (0.2f, 0.42f, 0.24f, 0.24f);
                p.startNewSubPath (0.34f, 0.56f);
                p.lineTo (0.86f, 0.1f);
                p.addRectangle (0.8f, 0.04f, 0.14f, 0.1f);
                break;

            case Type::speaker:
                p.startNewSubPath (0.08f, 0.38f);
                p.lineTo (0.24f, 0.38f);
                p.lineTo (0.44f, 0.18f);
                p.lineTo (0.44f, 0.82f);
                p.lineTo (0.24f, 0.62f);
                p.lineTo (0.08f, 0.62f);
                p.closeSubPath();
                p.addCentredArc (0.44f, 0.5f, 0.18f, 0.18f, 0.0f, 0.5f, 2.64f, true);
                p.addCentredArc (0.44f, 0.5f, 0.32f, 0.32f, 0.0f, 0.55f, 2.6f, true);
                p.addCentredArc (0.44f, 0.5f, 0.46f, 0.46f, 0.0f, 0.6f, 2.55f, true);
                break;

            case Type::processor:
                // Three effect blocks joined by a cable, as on the mockup's PROCESSOR tab.
                p.addRoundedRectangle (0.04f, 0.1f, 0.3f, 0.3f, 0.06f);
                p.addRoundedRectangle (0.66f, 0.1f, 0.3f, 0.3f, 0.06f);
                p.addRoundedRectangle (0.35f, 0.6f, 0.3f, 0.3f, 0.06f);
                p.startNewSubPath (0.34f, 0.25f);
                p.lineTo (0.66f, 0.25f);
                p.startNewSubPath (0.19f, 0.4f);
                p.quadraticTo (0.19f, 0.75f, 0.35f, 0.75f);
                break;

            case Type::presets:
                // A 2 x 2 grid of rounded tiles: a library.
                for (int i = 0; i < 4; ++i)
                    p.addRoundedRectangle (0.08f + 0.46f * (float) (i % 2), 0.08f + 0.46f * (float) (i / 2), 0.38f, 0.38f, 0.1f);
                break;

            case Type::undo:
            case Type::redo:
            {
                // A curved arrow turning back on itself; redo is its mirror image.
                p.startNewSubPath (0.2f, 0.36f);
                p.lineTo (0.62f, 0.36f);
                p.cubicTo (0.98f, 0.36f, 0.98f, 0.86f, 0.62f, 0.86f);
                p.lineTo (0.36f, 0.86f);
                p.startNewSubPath (0.36f, 0.18f);
                p.lineTo (0.18f, 0.36f);
                p.lineTo (0.36f, 0.54f);
                if (type == Type::redo)
                    p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, 0.5f, 0.5f));
                break;
            }

            case Type::tap:
                // A square with a dot: the mockup's TAP icon.
                p.addRoundedRectangle (0.14f, 0.14f, 0.72f, 0.72f, 0.12f);
                p.addEllipse (0.4f, 0.4f, 0.2f, 0.2f);
                break;

            case Type::tuner:
                // A tuning-meter arc with its needle.
                p.addCentredArc (0.5f, 0.78f, 0.44f, 0.44f, 0.0f, -1.2f, 1.2f, true);
                p.startNewSubPath (0.5f, 0.78f);
                p.lineTo (0.62f, 0.4f);
                p.addEllipse (0.45f, 0.73f, 0.1f, 0.1f);
                break;

            case Type::volume:
                // Rising bars, as on the mockup's VOLUME button.
                for (int i = 0; i < 4; ++i)
                {
                    const auto height = 0.22f + 0.2f * (float) i;
                    p.addRectangle (0.12f + 0.21f * (float) i, 0.88f - height, 0.1f, height);
                }
                break;

            case Type::wah:
                // A resonant peak sweeping across: a filter's response.
                p.startNewSubPath (0.0f, 0.72f);
                p.cubicTo (0.3f, 0.72f, 0.36f, 0.1f, 0.48f, 0.1f);
                p.cubicTo (0.6f, 0.1f, 0.62f, 0.86f, 1.0f, 0.86f);
                p.startNewSubPath (0.26f, 0.94f);
                p.lineTo (0.74f, 0.94f);
                p.startNewSubPath (0.62f, 0.88f);
                p.lineTo (0.74f, 0.94f);
                p.lineTo (0.62f, 1.0f);
                break;

            case Type::save:
                // A floppy disk: body with a clipped corner, label and shutter.
                p.startNewSubPath (0.12f, 0.12f);
                p.lineTo (0.72f, 0.12f);
                p.lineTo (0.88f, 0.28f);
                p.lineTo (0.88f, 0.88f);
                p.lineTo (0.12f, 0.88f);
                p.closeSubPath();
                p.addRectangle (0.28f, 0.12f, 0.38f, 0.22f);
                p.addRoundedRectangle (0.26f, 0.54f, 0.48f, 0.34f, 0.04f);
                break;

            case Type::settings:
            {
                // A cog: eight teeth round a ring, with a hole in the middle.
                constexpr int teeth = 8;
                for (int i = 0; i < teeth * 2; ++i)
                {
                    const auto angle = (float) i / (float) (teeth * 2) * juce::MathConstants<float>::twoPi;
                    const auto radius = (i % 2 == 0) ? 0.46f : 0.34f;
                    const auto next = (float) (i + 1) / (float) (teeth * 2) * juce::MathConstants<float>::twoPi;
                    const auto a = juce::Point<float> (0.5f + radius * std::sin (angle), 0.5f - radius * std::cos (angle));
                    const auto b = juce::Point<float> (0.5f + radius * std::sin (next), 0.5f - radius * std::cos (next));
                    if (i == 0)
                        p.startNewSubPath (a);
                    else
                        p.lineTo (a);
                    p.lineTo (b);
                }
                p.closeSubPath();
                p.addEllipse (0.36f, 0.36f, 0.28f, 0.28f);
                break;
            }

            case Type::star:
                p.addStar ({ 0.5f, 0.54f }, 5, 0.2f, 0.46f);
                break;

            case Type::user:
                p.addEllipse (0.34f, 0.1f, 0.32f, 0.32f);
                p.startNewSubPath (0.14f, 0.9f);
                p.cubicTo (0.14f, 0.5f, 0.86f, 0.5f, 0.86f, 0.9f);
                p.closeSubPath();
                break;

            case Type::factory:
                // A building with a saw-tooth roof.
                p.startNewSubPath (0.1f, 0.9f);
                p.lineTo (0.1f, 0.4f);
                p.lineTo (0.36f, 0.56f);
                p.lineTo (0.36f, 0.4f);
                p.lineTo (0.62f, 0.56f);
                p.lineTo (0.62f, 0.12f);
                p.lineTo (0.88f, 0.12f);
                p.lineTo (0.88f, 0.9f);
                p.closeSubPath();
                p.addRectangle (0.24f, 0.7f, 0.12f, 0.1f);
                p.addRectangle (0.5f, 0.7f, 0.12f, 0.1f);
                break;

            case Type::plus:
                p.startNewSubPath (0.5f, 0.15f);
                p.lineTo (0.5f, 0.85f);
                p.startNewSubPath (0.15f, 0.5f);
                p.lineTo (0.85f, 0.5f);
                break;

            case Type::chevronLeft:
            case Type::chevronRight:
                p.startNewSubPath (0.65f, 0.15f);
                p.lineTo (0.32f, 0.5f);
                p.lineTo (0.65f, 0.85f);
                if (type == Type::chevronRight)
                    p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, 0.5f, 0.5f));
                break;

            case Type::eq:
            {
                // Three faders at different heights: a graphic EQ.
                const float xs[] = { 0.2f, 0.5f, 0.8f }, knobs[] = { 0.62f, 0.3f, 0.5f };
                for (int i = 0; i < 3; ++i)
                {
                    p.startNewSubPath (xs[i], 0.08f);
                    p.lineTo (xs[i], 0.92f);
                    p.addRoundedRectangle (xs[i] - 0.1f, knobs[i] - 0.06f, 0.2f, 0.12f, 0.03f);
                }
                break;
            }

            case Type::filterPitch:
                // A resonant filter's peak, and an up / down arrow for pitch.
                p.startNewSubPath (0.04f, 0.72f);
                p.cubicTo (0.25f, 0.72f, 0.3f, 0.18f, 0.42f, 0.18f);
                p.cubicTo (0.52f, 0.18f, 0.55f, 0.85f, 0.66f, 0.92f);
                p.startNewSubPath (0.84f, 0.12f);
                p.lineTo (0.84f, 0.62f);
                p.startNewSubPath (0.74f, 0.24f);
                p.lineTo (0.84f, 0.12f);
                p.lineTo (0.94f, 0.24f);
                p.startNewSubPath (0.74f, 0.5f);
                p.lineTo (0.84f, 0.62f);
                p.lineTo (0.94f, 0.5f);
                break;

            case Type::loFi:
            {
                // An old transistor radio: the case, its round speaker grille and the antenna.
                p.addRoundedRectangle (0.06f, 0.34f, 0.88f, 0.58f, 0.08f);
                p.addEllipse (0.16f, 0.45f, 0.36f, 0.36f);
                p.startNewSubPath (0.62f, 0.52f);
                p.lineTo (0.84f, 0.52f);
                p.startNewSubPath (0.62f, 0.66f);
                p.lineTo (0.84f, 0.66f);
                p.startNewSubPath (0.62f, 0.8f);
                p.lineTo (0.84f, 0.8f);
                p.startNewSubPath (0.3f, 0.34f);
                p.lineTo (0.78f, 0.06f);
                break;
            }

            case Type::pitch:
            {
                // A tuning fork: two tines on a stem, ringing.
                p.startNewSubPath (0.38f, 0.06f);
                p.lineTo (0.38f, 0.48f);
                p.quadraticTo (0.38f, 0.62f, 0.5f, 0.62f);
                p.quadraticTo (0.62f, 0.62f, 0.62f, 0.48f);
                p.lineTo (0.62f, 0.06f);
                p.startNewSubPath (0.5f, 0.62f);
                p.lineTo (0.5f, 0.94f);
                p.startNewSubPath (0.2f, 0.16f);
                p.quadraticTo (0.12f, 0.27f, 0.2f, 0.38f);
                p.startNewSubPath (0.8f, 0.16f);
                p.quadraticTo (0.88f, 0.27f, 0.8f, 0.38f);
                break;
            }

            case Type::glitch:
            {
                // A waveform chopped up: a smooth start, then square stutter
                // steps that jump about, as GLITCH cuts the sound into pieces.
                p.startNewSubPath (0.04f, 0.5f);
                p.quadraticTo (0.12f, 0.22f, 0.2f, 0.5f);
                p.lineTo (0.28f, 0.5f);
                p.lineTo (0.28f, 0.2f);
                p.lineTo (0.4f, 0.2f);
                p.lineTo (0.4f, 0.74f);
                p.lineTo (0.48f, 0.74f);
                p.lineTo (0.48f, 0.36f);
                p.lineTo (0.58f, 0.36f);
                p.lineTo (0.58f, 0.86f);
                p.lineTo (0.66f, 0.86f);
                p.lineTo (0.66f, 0.12f);
                p.lineTo (0.74f, 0.12f);
                p.lineTo (0.74f, 0.6f);
                p.lineTo (0.84f, 0.6f);
                p.lineTo (0.84f, 0.42f);
                p.lineTo (0.96f, 0.42f);
                break;
            }

            case Type::arpeggio:
            {
                // Four notes climbing, as an arpeggio's blocks on a grid.
                for (int i = 0; i < 4; ++i)
                {
                    const auto x = 0.04f + 0.24f * (float) i, y = 0.72f - 0.2f * (float) i;
                    p.addRoundedRectangle (x, y, 0.19f, 0.16f, 0.03f);
                }
                break;
            }

            case Type::rack:
            {
                // Three rack units, one over another, each with a knob at its right.
                for (int i = 0; i < 3; ++i)
                {
                    const auto y = 0.08f + 0.3f * (float) i;
                    p.addRoundedRectangle (0.04f, y, 0.92f, 0.24f, 0.04f);
                    p.addEllipse (0.7f, y + 0.06f, 0.12f, 0.12f);
                }
                break;
            }

            case Type::mixer:
            {
                // Three fader slots, each with its cap at a different height.
                const float caps[] = { 0.62f, 0.3f, 0.5f };
                for (int i = 0; i < 3; ++i)
                {
                    const auto x = 0.2f + 0.3f * (float) i;
                    p.startNewSubPath (x, 0.06f);
                    p.lineTo (x, 0.94f);
                    p.addRoundedRectangle (x - 0.1f, caps[i] - 0.06f, 0.2f, 0.12f, 0.03f);
                }
                break;
            }

            case Type::drums:
            {
                // A drum from the side (its head an ellipse, its shell below) and two sticks crossed above it.
                p.addEllipse (0.12f, 0.42f, 0.76f, 0.2f);
                p.startNewSubPath (0.12f, 0.52f);
                p.lineTo (0.12f, 0.82f);
                p.quadraticTo (0.5f, 1.0f, 0.88f, 0.82f);
                p.lineTo (0.88f, 0.52f);
                p.startNewSubPath (0.2f, 0.06f);
                p.lineTo (0.62f, 0.44f);
                p.startNewSubPath (0.8f, 0.06f);
                p.lineTo (0.38f, 0.44f);
                break;
            }

            case Type::orchestra:
            {
                // A violin from the front: its scroll, the neck and strings down the body, the body's two bouts and
                // waist, and its two f-holes.
                p.addEllipse (0.455f, 0.0f, 0.09f, 0.09f);
                p.startNewSubPath (0.5f, 0.09f);
                p.lineTo (0.5f, 0.78f);
                p.startNewSubPath (0.5f, 0.3f);
                p.cubicTo (0.36f, 0.28f, 0.3f, 0.36f, 0.33f, 0.46f);
                p.cubicTo (0.36f, 0.52f, 0.36f, 0.56f, 0.31f, 0.62f);
                p.cubicTo (0.24f, 0.8f, 0.36f, 0.97f, 0.5f, 0.97f);
                p.cubicTo (0.64f, 0.97f, 0.76f, 0.8f, 0.69f, 0.62f);
                p.cubicTo (0.64f, 0.56f, 0.64f, 0.52f, 0.67f, 0.46f);
                p.cubicTo (0.7f, 0.36f, 0.64f, 0.28f, 0.5f, 0.3f);
                p.startNewSubPath (0.41f, 0.58f);
                p.quadraticTo (0.39f, 0.66f, 0.42f, 0.74f);
                p.startNewSubPath (0.59f, 0.58f);
                p.quadraticTo (0.61f, 0.66f, 0.58f, 0.74f);
                break;
            }

            case Type::grooves:
            {
                // A MIDI clip: its frame, and four notes in a rhythm across it.
                p.addRoundedRectangle (0.06f, 0.18f, 0.88f, 0.64f, 0.08f);
                for (const auto [x, y] : { std::pair { 0.2f, 0.66f }, std::pair { 0.4f, 0.4f }, std::pair { 0.58f, 0.66f }, std::pair { 0.76f, 0.34f } })
                {
                    p.startNewSubPath (x - 0.06f, y);
                    p.lineTo (x + 0.06f, y);
                }
                break;
            }

            case Type::flame:
                // The mockup's "All Rigs" icon.
                p.startNewSubPath (0.5f, 0.05f);
                p.cubicTo (0.95f, 0.4f, 0.85f, 0.95f, 0.5f, 0.95f);
                p.cubicTo (0.15f, 0.95f, 0.05f, 0.55f, 0.35f, 0.3f);
                p.cubicTo (0.35f, 0.5f, 0.45f, 0.55f, 0.5f, 0.55f);
                p.cubicTo (0.55f, 0.4f, 0.5f, 0.2f, 0.5f, 0.05f);
                p.closeSubPath();
                break;
        }

        return p;
    }

    void draw (juce::Graphics& g, Type type, juce::Rectangle<float> area, juce::Colour colour, float strokeWidth, bool glow)
    {
        const auto size = juce::jmin (area.getWidth(), area.getHeight());
        auto path = get (type);
        path.applyTransform (juce::AffineTransform::scale (size).translated (area.getCentreX() - size / 2.0f,
                                                                                  area.getCentreY() - size / 2.0f));

        if (glow)
        {
            ParagonColours::setFill (g, colour.withAlpha (0.25f), area);
            g.strokePath (path, juce::PathStrokeType (strokeWidth * 3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        ParagonColours::setFill (g, colour, area);
        g.strokePath (path, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
IconButton::IconButton (const juce::String& name, Icons::Type iconType, const juce::String& buttonText,
                        juce::Justification textJustification)
    : juce::Button (name), icon (iconType), text (buttonText), justification (textJustification)
{
    setTooltip (name);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = getToggleState();

    if (on || outlined || down)
    {
        ParagonColours::setFill (g, on ? ParagonColours::accentDim.withAlpha (0.55f) : juce::Colour (0xff110e12), getLocalBounds());
        g.fillRoundedRectangle (bounds, 4.0f);
        ParagonColours::setFill (g, on ? ParagonColours::accent.withAlpha (0.8f) : ParagonColours::panelEdge, getLocalBounds());
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }

    auto colour = on ? ParagonColours::accent : (highlighted ? ParagonColours::text : ParagonColours::text.withAlpha (0.85f));
    if (! isEnabled())
        colour = ParagonColours::textDim.withAlpha (0.45f);

    auto content = bounds.reduced (text.isEmpty() ? 4.0f : 8.0f, 0.0f);
    const auto iconSize = juce::jmin (18.0f, bounds.getHeight() * 0.5f);

    if (text.isEmpty())
    {
        Icons::draw (g, icon, content.withSizeKeepingCentre (iconSize, iconSize), colour, 1.6f, on);
        return;
    }

    const auto font = juce::FontOptions (juce::jmin (15.0f, bounds.getHeight() * 0.55f), on ? juce::Font::bold : juce::Font::plain);
    const auto textWidth = (float) juce::GlyphArrangement::getStringWidthInt (font, text);
    const auto gap = iconSize * 0.45f;

    // Centred: icon and text together in the middle. Otherwise from the left.
    auto row = justification == juce::Justification::centred
                 ? content.withSizeKeepingCentre (juce::jmin (content.getWidth(), iconSize + gap + textWidth), content.getHeight())
                 : content;
    Icons::draw (g, icon, row.removeFromLeft (iconSize).withSizeKeepingCentre (iconSize, iconSize), colour, 1.6f, on);
    row.removeFromLeft (gap);
    g.setFont (font);
    ParagonColours::setTextFill (g, colour, row.withWidth (juce::jmin (row.getWidth(), textWidth)));
    g.drawText (text, row, juce::Justification::centredLeft, true);
}
