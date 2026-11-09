#include "Logo.h"

namespace Logo
{
    namespace
    {
        // The letters are designed 1 unit tall, upright, then slanted. Stems
        // are 0.24 wide and bars 0.2 tall: chunky, for a bold silhouette.
        constexpr float slant = 0.3f;     // how far the top leans right, per unit of height
        constexpr float spacing = 0.1f;
        constexpr float letterWidths[] = { 0.8f, 0.84f, 0.8f, 0.4f, 0.82f };   // P, R, 7, dash, O
        constexpr float totalWidth = 0.8f + 0.84f + 0.8f + 0.4f + 0.82f + 4.0f * spacing + slant;

        // The two speed cuts, as fractions of the height from the top.
        constexpr std::pair<float, float> cuts[] = { { 0.63f, 0.68f }, { 0.79f, 0.83f } };

        void addPolygon (juce::Path& path, std::initializer_list<juce::Point<float>> points, float x)
        {
            auto first = true;
            for (auto point : points)
            {
                point.x += x;
                if (first)
                    path.startNewSubPath (point);
                else
                    path.lineTo (point);
                first = false;
            }
            path.closeSubPath();
        }

        // P's body (R shares it): bevelled corners on the bowl, and its counter.
        void addBowl (juce::Path& path, float x)
        {
            addPolygon (path, { { 0.0f, 0.0f }, { 0.64f, 0.0f }, { 0.8f, 0.15f }, { 0.8f, 0.45f }, { 0.64f, 0.6f },
                                { 0.24f, 0.6f }, { 0.24f, 1.0f }, { 0.0f, 1.0f } }, x);
            addPolygon (path, { { 0.24f, 0.2f }, { 0.56f, 0.2f }, { 0.56f, 0.4f }, { 0.24f, 0.4f } }, x);
        }

        // PR7-O's O, built from the P's parts: the bowl's bevelled corners on all four corners, carried down the whole
        // height (0.24 sides, 0.2 bars), its counter a hole (the path fills even-odd).
        void addO (juce::Path& path, float x)
        {
            addPolygon (path, { { 0.18f, 0.0f }, { 0.64f, 0.0f }, { 0.82f, 0.18f }, { 0.82f, 0.82f }, { 0.64f, 1.0f },
                                { 0.18f, 1.0f }, { 0.0f, 0.82f }, { 0.0f, 0.18f } }, x);
            addPolygon (path, { { 0.24f, 0.2f }, { 0.58f, 0.2f }, { 0.58f, 0.8f }, { 0.24f, 0.8f } }, x);
        }

        // The letters, 1 unit tall, upright: [0] is P, R and "-O", [1] is the 7.
        std::pair<juce::Path, juce::Path> letterPaths()
        {
            juce::Path pr, seven;
            pr.setUsingNonZeroWinding (false);   // so the bowls' counters are holes

            auto x = 0.0f;
            addBowl (pr, x);
            x += letterWidths[0] + spacing;

            addBowl (pr, x);
            addPolygon (pr, { { 0.4f, 0.6f }, { 0.64f, 0.6f }, { 0.84f, 1.0f }, { 0.6f, 1.0f } }, x);   // R's leg
            x += letterWidths[1] + spacing;

            addPolygon (seven, { { 0.0f, 0.0f }, { 0.8f, 0.0f }, { 0.8f, 0.2f }, { 0.44f, 1.0f }, { 0.18f, 1.0f },
                                 { 0.5f, 0.2f }, { 0.0f, 0.2f } }, x);
            x += letterWidths[2] + spacing;

            // "-O" (the orchestra): a dash at mid height, clear of the speed cuts, and the O.
            addPolygon (pr, { { 0.0f, 0.42f }, { 0.4f, 0.42f }, { 0.4f, 0.6f }, { 0.0f, 0.6f } }, x);
            x += letterWidths[3] + spacing;

            addO (pr, x);

            // Lean them forward: the top moves right, the baseline stays put.
            const auto lean = juce::AffineTransform (1.0f, -slant, slant, 0.0f, 1.0f, 0.0f);
            pr.applyTransform (lean);
            seven.applyTransform (lean);
            return { pr, seven };
        }
    }

    float widthFor (float height) noexcept
    {
        return height * totalWidth;
    }

    juce::Rectangle<float> drawMark (juce::Graphics& g, juce::Rectangle<float> area, juce::Justification justification, const Style& style)
    {
        const auto height = juce::jmin (area.getHeight(), area.getWidth() / totalWidth);
        const auto bounds = justification.appliedToRectangle (juce::Rectangle<float> (widthFor (height), height), area);

        static const auto letters = letterPaths();
        const auto place = juce::AffineTransform::scale (height).translated (bounds.getX(), bounds.getY());
        auto pr = letters.first, seven = letters.second;
        pr.applyTransform (place);
        seven.applyTransform (place);

        if (style.glow)
        {
            // A soft neon halo behind the 7, wider and fainter outwards.
            for (int ring = 3; ring >= 1; --ring)
            {
                g.setColour (style.seven.withAlpha (0.1f * (float) (4 - ring)));
                g.strokePath (seven, juce::PathStrokeType (height * 0.07f * (float) ring, juce::PathStrokeType::curved));
            }
        }

        juce::Graphics::ScopedSaveState saved (g);
        for (const auto& [top, bottom] : cuts)
            g.excludeClipRegion (juce::Rectangle<float> (bounds.getX() - height, bounds.getY() + height * top,
                                                         bounds.getWidth() + 2.0f * height, height * (bottom - top))
                                     .getSmallestIntegerContainer());

        if (! style.highlight.isTransparent())
        {
            const auto down = juce::AffineTransform::translation (0.0f, juce::jmax (1.0f, height * 0.04f));
            g.setColour (style.highlight);
            g.fillPath (pr, down);
            g.fillPath (seven, down);
        }

        g.setColour (style.letters);
        g.fillPath (pr);
        g.setColour (style.seven);
        g.fillPath (seven);

        if (! style.outline.isTransparent())
        {
            g.setColour (style.outline);
            const juce::PathStrokeType stroke (style.outlineWidth, juce::PathStrokeType::mitered);
            g.strokePath (pr, stroke);
            g.strokePath (seven, stroke);
        }
        return bounds;
    }

    void drawIcon (juce::Graphics& g, juce::Rectangle<float> area, const IconStyle& style)
    {
        const auto n = juce::jmin (area.getWidth(), area.getHeight());
        const auto box = area.withSizeKeepingCentre (n, n);
        const auto lean = juce::AffineTransform (1.0f, -slant, slant, 0.0f, 1.0f, 0.0f);

        // "7O": the 7 at 0, the O 0.9 letters on; 2.02 letter heights wide with the lean.
        const auto h = 0.40f * n, top = box.getY() + 0.15f * n;
        const auto left = box.getCentreX() - 2.02f * h * 0.5f;
        juce::Path seven, d;
        d.setUsingNonZeroWinding (false);
        addPolygon (seven, { { 0.0f, 0.0f }, { 0.8f, 0.0f }, { 0.8f, 0.2f }, { 0.44f, 1.0f }, { 0.18f, 1.0f },
                             { 0.5f, 0.2f }, { 0.0f, 0.2f } }, 0.0f);
        addO (d, 0.9f);
        const auto place = lean.scaled (h).translated (left, top);
        seven.applyTransform (place);
        d.applyTransform (place);
        {
            juce::Graphics::ScopedSaveState saved (g);
            for (const auto& [from, to] : cuts)
                g.excludeClipRegion (juce::Rectangle<float> (box.getX(), top + h * from, n, h * (to - from)).getSmallestIntegerContainer());
            g.setColour (style.letters);
            g.fillPath (d);
            {
                juce::Graphics::ScopedSaveState upper (g);
                g.reduceClipRegion (juce::Rectangle<float> (box.getX(), box.getY(), n, top + h * cuts[0].first - box.getY()).getSmallestIntegerContainer());
                g.setColour (style.sevenTop);
                g.fillPath (seven);
            }
            g.reduceClipRegion (juce::Rectangle<float> (box.getX(), top + h * cuts[0].first, n, box.getBottom() - (top + h * cuts[0].first)).getSmallestIntegerContainer());
            g.setColour (style.sevenLow);
            g.fillPath (seven);
        }

        // The line, the mark under it, the frame.
        const auto lineY = top + h + 0.075f * n;
        g.setColour (style.accent);
        g.fillRect (juce::Rectangle<float> (box.getX() + 0.2f * n, lineY - 0.006f * n, 0.6f * n, 0.012f * n));
        const auto markHeight = 0.085f * n;
        drawMark (g, juce::Rectangle<float> (box.getX(), lineY + 0.05f * n, n, markHeight), juce::Justification::centredTop,
                  Style { style.letters, style.accent });
        g.setColour (style.accent);
        g.drawRoundedRectangle (box.reduced (0.03f * n), 0.17f * n, 0.016f * n);
    }
}
