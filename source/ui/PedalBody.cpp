#include "PedalBody.h"

#include "Finish.h"
#include "Logo.h"
#include "ParagonLookAndFeel.h"

namespace
{
    constexpr int textureScale = 2;
    constexpr float chassisRadius = 28.0f;
    constexpr float glowMargin = 10.0f;        // room round the unit for the light spilling from it...
    constexpr float glowMarginBelow = 5.0f;    // ...less underneath, where the dock is close
    constexpr float frontEdge = 8.0f;          // the unit's front edge (its thickness), below the rim's top
    constexpr float faceRadius = 16.0f;

}

PedalBody::PedalBody()
{
    // Everything here is static, so draw it once and reuse the picture.
    setBufferedToImage (true);
    setInterceptsMouseClicks (false, false);
}

void PedalBody::setGeometry (Geometry newGeometry)
{
    geometry = std::move (newGeometry);
    repaint();
}

void PedalBody::resized()
{
    makeSatinTexture();
}

void PedalBody::makeSatinTexture()
{
    // Pearl paint, not brushed metal (see Finish::makeSatinGrain). Twice
    // the component's size, so the grain stays fine on high-resolution screens.
    satin = Finish::makeSatinGrain (getWidth() * textureScale, getHeight() * textureScale, 1234);
}

void PedalBody::refinish()
{
    makeSatinTexture();
    repaint();
}

void PedalBody::paint (juce::Graphics& g)
{
    // Every colour has its pearl white and its pearl black version.
    const auto black = Finish::isBlack();
    using Finish::pick;

    auto chassis = getLocalBounds().toFloat().reduced (glowMargin).withTrimmedBottom (glowMarginBelow - glowMargin);
    paintUnit (g, chassis, geometry.face, [this] (juce::Graphics& gg) { drawSatin (gg); }, {});

    // 4. Section panels (the slot row and the footswitch row), set down into
    //    the face: the face's edge casts a shadow along their top, the
    //    panel's own lower lip catches the light, and their pearl is a
    //    touch deeper and more even than the face around them.
    for (const auto& section : geometry.sections)
        paintSection (g, section, [this] (juce::Graphics& gg) { drawSatin (gg); });

    // 5. Recessed panels behind the slot windows: engraved, with a dark
    //    groove and a light lower edge.
    for (const auto& inset : geometry.insets)
    {
        g.setColour (black ? juce::Colours::black.withAlpha (0.22f) : juce::Colour (0xff8a4a66).withAlpha (0.06f));
        g.fillRoundedRectangle (inset, 8.0f);
        g.setColour (black ? juce::Colours::black.withAlpha (0.85f) : juce::Colour (0xff2a1c24).withAlpha (0.5f));
        g.drawRoundedRectangle (inset, 8.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (black ? 0.15f : 0.9f));
        g.drawRoundedRectangle (inset.translated (0.0f, 1.0f).expanded (0.5f), 8.5f, 1.0f);
    }

    // 6. The expression pedal's bay: a deeper pearl housing, more shaded
    //    than the face because it curves away, split from it by a dark seam. The pedal component draws the pedal.
    if (const auto bay = geometry.pedalBay; ! bay.isEmpty())
    {
        juce::Path bayPath;
        bayPath.addRoundedRectangle (bay, 22.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 8, { -2, 3 }).drawForPath (g, bayPath);
        juce::ColourGradient housing (pick (0xfff2e6ec, 0xff1a1819), bay.getX(), 0.0f, pick (0xffc8b0bd, 0xff060606), bay.getRight(), 0.0f, false);
        housing.addColour (0.05, pick (0xffffffff, 0xff2e2b2d));
        housing.addColour (0.22, pick (0xffece0e6, 0xff151314));
        housing.addColour (0.7, pick (0xffdcc9d2, 0xff0e0d0e));
        g.setGradientFill (housing);
        g.fillPath (bayPath);
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (bayPath);
            drawSatin (g);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), 0.0f, bay.getY(),
                                                 black ? juce::Colours::black.withAlpha (0.35f) : juce::Colour (0xff4a2034).withAlpha (0.14f), 0.0f, bay.getBottom(), false));
        g.fillPath (bayPath);
        strokeTopHighlight (g, bay.reduced (1.5f), 20.0f, 1.0f, black ? 0.5f : 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.drawRoundedRectangle (bay, 22.0f, 1.0f);
    }

    g.setColour (black ? Finish::printedRed() : ParagonColours::glow.darker (0.1f));
    g.strokePath (geometry.printedRed, juce::PathStrokeType (1.2f));
    g.setColour (Finish::ink().withAlpha (0.8f));
    g.strokePath (geometry.printedInk, juce::PathStrokeType (1.2f));

    if (! geometry.badge.isEmpty())
        paintBadge (g, geometry.badge, "NEURAL MULTI-FX PROCESSOR");
}

void PedalBody::paintUnit (juce::Graphics& g, juce::Rectangle<float> chassis, juce::Rectangle<float> face,
                           const std::function<void (juce::Graphics&)>& drawSatin, Shape shape)
{
    // Every colour has its pearl white and its pearl black version.
    const auto black = Finish::isBlack();
    using Finish::pick;

    juce::Path chassisPath;
    chassisPath.addRoundedRectangle (chassis, shape.chassisRadius);

    // 1. Light spilling onto the dark background round the unit: soft and
    //    wide, strongest underneath, as in the mockup (no hard outline).
    juce::DropShadow (ParagonColours::glow.withAlpha (0.42f), juce::roundToInt (44.0f * shape.glow), { 0, juce::roundToInt (14.0f * shape.glow) }).drawForPath (g, chassisPath);
    juce::DropShadow (juce::Colour (0xffff3a5a).withAlpha (0.3f), juce::roundToInt (14.0f * shape.glow), { 0, juce::roundToInt (3.0f * shape.glow) }).drawForPath (g, chassisPath);

    // 2. The chassis: a thick pearl rim. Its front edge shows as a darker
    //    band along the bottom (the unit's thickness); the top surface of
    //    the rim runs from white where it faces the light to a soft rose
    //    shade lower down, enough range to read as a glossy curved finish
    //    without going grey.
    g.setGradientFill (juce::ColourGradient (pick (0xffb49ca8, 0xff141213), 0.0f, chassis.getBottom() - 30.0f,
                                             pick (0xff5e4652, 0xff020202), 0.0f, chassis.getBottom(), false));
    g.fillPath (chassisPath);

    const auto top = chassis.withTrimmedBottom (shape.frontEdge);
    juce::Path topPath;
    topPath.addRoundedRectangle (top, shape.chassisRadius);
    {
        juce::ColourGradient vertical (pick (0xffffffff, 0xff3a3638), 0.0f, top.getY(), pick (0xffc9b2be, 0xff080707), 0.0f, top.getBottom(), false);
        vertical.addColour (0.02, pick (0xfff3e9ee, 0xff1e1c1d));
        vertical.addColour (0.06, pick (0xffe2d3db, 0xff121112));
        vertical.addColour (0.45, pick (0xffeee3e9, 0xff181617));
        vertical.addColour (0.9, pick (0xffdccbd4, 0xff0d0c0d));
        g.setGradientFill (vertical);
        g.fillPath (topPath);

        // The rim's sides curve away: bright where they catch the light,
        // dark just inside.
        const auto bandShade = black ? juce::Colours::black.withAlpha (0.5f) : juce::Colour (0xff4a2a3a).withAlpha (0.3f);
        const auto bandShadeInner = black ? juce::Colours::black.withAlpha (0.26f) : juce::Colour (0xff4a2a3a).withAlpha (0.16f);
        const auto bandLight = black ? 0.55f : 1.0f;
        juce::ColourGradient banding (bandShade, top.getX(), 0.0f, bandShade, top.getRight(), 0.0f, false);
        banding.addColour (0.006, juce::Colours::white.withAlpha (0.9f * bandLight));
        banding.addColour (0.018, bandShadeInner);
        banding.addColour (0.5, juce::Colours::white.withAlpha (0.12f * bandLight));
        banding.addColour (0.982, bandShadeInner);
        banding.addColour (0.994, juce::Colours::white.withAlpha (0.85f * bandLight));
        g.setGradientFill (banding);
        g.fillPath (topPath);

        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (topPath);
        drawSatin (g);
    }

    // Crisp machined edges: a bright line along the top of the rim, the
    // reflected red along its bottom, and a dark outline all round.
    strokeTopHighlight (g, top.reduced (1.0f), shape.chassisRadius, 1.4f, black ? 0.75f : 1.0f);
    g.setColour (juce::Colour (0xffff8a8a).withAlpha (0.7f));
    g.drawHorizontalLine ((int) top.getBottom() - 1, top.getX() + shape.chassisRadius, top.getRight() - shape.chassisRadius);
    g.setColour (black ? juce::Colours::black.withAlpha (0.9f) : juce::Colour (0xff2a1420).withAlpha (0.7f));
    g.drawRoundedRectangle (chassis.reduced (0.5f), shape.chassisRadius, 1.0f);

    // 3. The face plate, sunk into the rim: bright pearl white with a satin finish.
    if (! face.isEmpty())
    {
        juce::Path facePath;
        facePath.addRoundedRectangle (face, shape.faceRadius);

        // The step down from the rim: a shadow just inside the rim, a dark
        // groove, and a bright lip where the rim's edge catches the light.
        juce::DropShadow (black ? juce::Colours::black.withAlpha (0.7f) : juce::Colour (0xff3a1a2a).withAlpha (0.4f), 8, { 0, -1 }).drawForPath (g, facePath);
        g.setColour (juce::Colours::white.withAlpha (black ? 0.2f : 0.75f));
        g.drawRoundedRectangle (face.expanded (2.5f).translated (0.0f, 1.5f), shape.faceRadius + 2.0f, 1.0f);

        // Pearl white, lit from above: nearly pure white at the top, a soft
        // band where it reflects the room, brighter again lower down. The
        // shading stays warm (rose, not grey) so it sits with the neon.
        // Black pearl is lit the same way, but stays deep black: only the
        // top catches a little light. Neutral to faintly warm, never blue,
        // so it reads as black next to the red neon rather than navy.
        juce::ColourGradient pearl (pick (0xfffffdfe, 0xff1a1819), face.getX(), face.getY(),
                                    pick (0xffefe3e9, 0xff070707), face.getX(), face.getBottom(), false);
        pearl.addColour (0.18, pick (0xfffaf4f7, 0xff121112));
        pearl.addColour (0.42, pick (0xfff1e7ec, 0xff0a0a0a));
        pearl.addColour (0.7, pick (0xfff8f1f4, 0xff0e0d0e));
        g.setGradientFill (pearl);
        g.fillPath (facePath);

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (facePath);
        drawSatin (g);

        // Pearlescence: a faint shift of hue across the plate, rose at the
        // left, peach in the middle, lilac at the right, the way pearl
        // paint changes colour with the angle you see it from. On black,
        // only a faint warm rose (cool tints made it read as navy).
        {
            juce::ColourGradient iridescence (black ? juce::Colour (0xffff6a9a).withAlpha (0.05f) : juce::Colour (0xffffc8dc).withAlpha (0.16f), face.getX(), face.getY(),
                                              black ? juce::Colour (0xffff4a6a).withAlpha (0.04f) : juce::Colour (0xffffe0ec).withAlpha (0.14f), face.getRight(), face.getBottom(), false);
            iridescence.addColour (0.5, black ? juce::Colour (0xffffb07a).withAlpha (0.025f) : juce::Colour (0xffffe0c8).withAlpha (0.12f));
            g.setGradientFill (iridescence);
            g.fillRect (face);
        }

        // A sharp diagonal sheen across the upper left, like a studio light
        // in a glossy finish: bright at its core, fading quickly either side.
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), face.getX(), face.getY(),
                                    juce::Colours::white.withAlpha (0.0f), face.getX() + face.getWidth() * 0.45f, face.getBottom(), false);
        const auto sheenStrength = black ? 0.22f : 1.0f;   // on black, a full-strength streak would read as grey paint
        sheen.addColour (0.3, juce::Colours::white.withAlpha (0.05f * sheenStrength));
        sheen.addColour (0.38, juce::Colours::white.withAlpha (0.45f * sheenStrength));
        sheen.addColour (0.46, juce::Colours::white.withAlpha (0.05f * sheenStrength));
        g.setGradientFill (sheen);
        g.fillRect (face);

        // Vignette: the plate falls off a little towards its edges and
        // corners, which is what makes it look lit rather than flat. Rose,
        // not black, and light, so the corners stay pearl rather than grey.
        const auto vignette = black ? juce::Colours::black.withAlpha (0.6f) : juce::Colour (0xff8a4a66).withAlpha (0.16f);
        g.setGradientFill (juce::ColourGradient (vignette.withAlpha (0.0f), face.getCentreX(), face.getY() + face.getHeight() * 0.4f,
                                                 vignette, face.getX() - face.getWidth() * 0.04f, face.getY(), true));
        g.fillRect (face);

        // The glow under the unit catches only the plate's bottom edge.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff9ab3).withAlpha (0.0f), 0.0f, face.getBottom() - 26.0f,
                                                 juce::Colour (0xffff9ab3).withAlpha (0.4f), 0.0f, face.getBottom(), false));
        g.fillRect (face.withTop (face.getBottom() - 26.0f));
    }

}

void PedalBody::paintSection (juce::Graphics& g, juce::Rectangle<float> section, const std::function<void (juce::Graphics&)>& drawSatin)
{
    const auto black = Finish::isBlack();
    using Finish::pick;
    juce::Path panel;
    panel.addRoundedRectangle (section, 12.0f);
    g.setGradientFill (juce::ColourGradient (pick (0xffeee3e9, 0xff070707), 0.0f, section.getY(),
                                             pick (0xfff9f3f6, 0xff0f0e0f), 0.0f, section.getBottom(), false));
    g.fillPath (panel);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (panel);
        drawSatin (g);

        // Inner shadow along the top and down the sides, from the face's edge.
        for (int ring = 1; ring <= 5; ++ring)
        {
            g.setColour ((black ? juce::Colours::black : juce::Colour (0xff4a2034)).withAlpha ((0.11f - 0.019f * (float) ring) * (black ? 2.5f : 1.0f)));
            g.drawRoundedRectangle (section.translated (0.0f, (float) ring * 1.2f).reduced ((float) ring * 0.6f), 12.0f, 1.6f);
        }
    }
    // Dark crisp edge where the face drops away, bright lip at the bottom.
    g.setColour (black ? juce::Colours::black.withAlpha (0.85f) : juce::Colour (0xff3a2a32).withAlpha (0.55f));
    g.drawRoundedRectangle (section, 12.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (black ? 0.18f : 0.9f));
    {
        juce::Graphics::ScopedSaveState bottomOnly (g);
        g.reduceClipRegion (section.expanded (2.0f).withTrimmedTop (section.getHeight() - 6.0f).toNearestInt());
        g.drawRoundedRectangle (section.expanded (1.0f), 13.0f, 1.0f);
    }
}

void PedalBody::strokeTopHighlight (juce::Graphics& g, juce::Rectangle<float> area, float radius, float thickness, float alpha)
{
    // Light catching an edge from above: full strength along the top,
    // fading smoothly down the sides to nothing by about 40% of the way
    // down, rather than cut off at a hard line.
    juce::Path edge;
    edge.addRoundedRectangle (area, radius);
    juce::ColourGradient fade (juce::Colours::white.withAlpha (alpha), 0.0f, area.getY(),
                               juce::Colours::white.withAlpha (0.0f), 0.0f, area.getY() + area.getHeight() * 0.42f, false);
    fade.addColour (0.15, juce::Colours::white.withAlpha (alpha * 0.85f));
    g.setGradientFill (fade);
    g.strokePath (edge, juce::PathStrokeType (thickness));
}

void PedalBody::drawSatin (juce::Graphics& g) const
{
    g.drawImage (satin, getLocalBounds().toFloat());
}

void PedalBody::paintBadge (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& caption)
{
    // Heavy italic PARAGON, as "CYBER RPX" in the mockup, over the PR7 mark.
    const auto lineHeight = area.getHeight() * 0.38f;
    const auto font = juce::FontOptions (lineHeight * 1.12f, juce::Font::bold | juce::Font::italic);

    // A fine edge under the lettering makes it look stamped into the metal.
    const auto drawStamped = [&g] (const juce::String& text, juce::Rectangle<float> where, juce::Colour colour)
    {
        g.setColour (Finish::stampEdge());
        g.drawText (text, where.translated (0.0f, 1.2f), juce::Justification::centredLeft, false);
        g.setColour (colour);
        g.drawText (text, where, juce::Justification::centredLeft, false);
    };

    g.setFont (font);
    drawStamped ("PARAGON", area.removeFromTop (lineHeight), Finish::ink());

    // The PR7-G mark under it, as tall as the lettering's capitals, starting
    // where they do (the slant makes the P's foot sit under PARAGON's P).
    auto secondLine = area.removeFromTop (lineHeight);
    Logo::drawMark (g, secondLine.withSizeKeepingCentre (secondLine.getWidth(), lineHeight * 0.82f).withTrimmedLeft (lineHeight * 0.04f),
                   juce::Justification::centredLeft,
                   { Finish::ink(), ParagonColours::glow, Finish::stampEdge() });

    g.setFont (juce::FontOptions (lineHeight * 0.27f, juce::Font::bold));
    g.setColour (Finish::printedRed());
    g.drawText (caption, area.withTrimmedTop (4.0f).withHeight (lineHeight * 0.4f),
                juce::Justification::centredLeft, false);
}
