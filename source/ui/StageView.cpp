#include "StageView.h"

#include "OrchestraImages.h"
#include "library/Library.h"
#include "ui/OrchestraView.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    // The picture's map from stage metres to its pixels: a grid every 0.5 m (orchestra.json's "stage").
    struct Grid
    {
        float x0 = -9.0f, y0 = -2.0f, step = 0.5f;
        int columns = 0, rows = 0;
        std::vector<juce::Point<float>> points;
        juce::Point<float> podium;
        juce::Point<float> at (int i, int j) const { return points[(size_t) (juce::jlimit (0, rows - 1, j) * columns + juce::jlimit (0, columns - 1, i))]; }
    };
    const Grid& grid()
    {
        static const Grid g = []
        {
            Grid r;
            const auto s = orchestra::geometry()["stage"];
            r.x0 = (float) s["x0"];
            r.y0 = (float) s["y0"];
            r.step = (float) s["step"];
            r.columns = (int) s["columns"];
            r.rows = (int) s["rows"];
            if (const auto* list = s["points"].getArray())
                for (const auto& p : *list)
                    r.points.push_back ({ (float) p[0], (float) p[1] });
            r.podium = { (float) s["podium"][0], (float) s["podium"][1] };
            if ((int) r.points.size() != r.columns * r.rows)
                r.columns = r.rows = 0;
            return r;
        }();
        return g;
    }
    const juce::Colour pearl (0xfff4eef1);
    float depthOf (const stage::Seat& s) { return juce::jlimit (0.5f, 1.2f, 0.45f + 0.12f * s.width); }
}

const juce::Image& StageView::getPicture()
{
    static const juce::Image image = []
    {
        int size = 0;
        const auto* data = OrchestraImages::getNamedResource ("stage_jpg", size);
        return data != nullptr ? orchestra::inFamily (juce::ImageFileFormat::loadFrom (data, (size_t) size)) : juce::Image();
    }();
    return image;
}

StageView::StageView (ParagonX1AudioProcessor& p) : processor (p)
{
    setOpaque (true);
    refresh();
    startTimerHz (30);
}

void StageView::refresh()
{
    parts = processor.getStageParts();
    shownSource = processor.getInstrumentFile();
    shownSeats = processor.getSeatsVersion();
    partKeys.assign (parts.size(), { 0, 127 });
    if (const auto section = processor.getSection())
        for (size_t i = 0; i < parts.size(); ++i)
            for (const auto& part : section->parts)
                if (sampler::section::nameOf (part) == parts[i].name)
                    partKeys[i] = { part.low, part.high };
    glow.assign (parts.size(), 0.0f);
    repaint();
}

juce::Rectangle<float> StageView::pictureArea() const
{
    const auto& image = getPicture();
    if (image.isNull())
        return getLocalBounds().toFloat();
    const auto scale = juce::jmax ((float) getWidth() / (float) image.getWidth(), (float) getHeight() / (float) image.getHeight());
    const auto w = (float) image.getWidth() * scale, h = (float) image.getHeight() * scale;
    return { ((float) getWidth() - w) * 0.5f, ((float) getHeight() - h) * 0.5f, w, h };
}

juce::Point<float> StageView::toPicture (juce::Point<float> m) const
{
    // Bilinear between the grid's points.
    const auto& g = grid();
    if (g.columns < 2)
        return {};
    const auto fx = juce::jlimit (0.0f, (float) (g.columns - 1) - 1.0e-3f, (m.x - g.x0) / g.step);
    const auto fy = juce::jlimit (0.0f, (float) (g.rows - 1) - 1.0e-3f, (m.y - g.y0) / g.step);
    const auto i = (int) fx, j = (int) fy;
    const auto tx = fx - (float) i, ty = fy - (float) j;
    const auto top = g.at (i, j) + (g.at (i + 1, j) - g.at (i, j)) * tx;
    const auto bottom = g.at (i, j + 1) + (g.at (i + 1, j + 1) - g.at (i, j + 1)) * tx;
    return top + (bottom - top) * ty;
}

juce::Point<float> StageView::toView (juce::Point<float> metres) const
{
    const auto area = pictureArea();
    const auto& image = getPicture();
    const auto scale = image.isNull() ? 1.0f : area.getWidth() / (float) image.getWidth();
    return toPicture (metres) * scale + area.getPosition();
}

juce::Point<float> StageView::toStage (juce::Point<float> v) const
{
    // The grid's nearest point, then Newton steps on the map (its slope measured a centimetre each way).
    const auto& g = grid();
    juce::Point<float> best { 0.0f, 5.0f };
    auto bestDistance = std::numeric_limits<float>::max();
    for (int j = 0; j < g.rows; ++j)
        for (int i = 0; i < g.columns; ++i)
        {
            const juce::Point<float> m { g.x0 + g.step * (float) i, g.y0 + g.step * (float) j };
            if (const auto d = toView (m).getDistanceSquaredFrom (v); d < bestDistance)
            {
                bestDistance = d;
                best = m;
            }
        }
    for (int step = 0; step < 6; ++step)
    {
        const auto at = toView (best);
        const auto dx = (toView (best + juce::Point<float> (0.01f, 0.0f)) - at) / 0.01f;
        const auto dy = (toView (best + juce::Point<float> (0.0f, 0.01f)) - at) / 0.01f;
        const auto det = dx.x * dy.y - dy.x * dx.y;
        if (std::abs (det) < 1.0e-6f)
            break;
        const auto e = v - at;
        best += { (e.x * dy.y - dy.x * e.y) / det, (dx.x * e.y - e.x * dx.y) / det };
    }
    return best;
}

juce::Path StageView::zone (const stage::Seat& s) const
{
    // An ellipse on the stage's floor (its width across, a depth that grows a little with it), seen in perspective.
    juce::Path p;
    const auto rx = 0.5f * s.width, ry = depthOf (s);
    for (int k = 0; k <= 48; ++k)
    {
        const auto a = juce::MathConstants<float>::twoPi * (float) k / 48.0f;
        const auto v = toView ({ s.x + rx * std::cos (a), s.y + ry * std::sin (a) });
        if (k == 0)
            p.startNewSubPath (v);
        else
            p.lineTo (v);
    }
    p.closeSubPath();
    return p;
}

int StageView::partAt (juce::Point<float> v) const
{
    for (int i = (int) parts.size() - 1; i >= 0; --i)
        if (zone (parts[(size_t) i].seat).contains (v) || toView ({ parts[(size_t) i].seat.x, parts[(size_t) i].seat.y }).getDistanceFrom (v) < 18.0f)
            return i;
    return -1;
}

float StageView::lit (int part) const
{
    return part >= 0 && part < (int) glow.size() ? glow[(size_t) part] : 0.0f;
}

void StageView::timerCallback()
{
    if ((processor.getInstrumentFile() != shownSource || processor.getSeatsVersion() != shownSeats) && dragging < 0)
        refresh();   // another instrument, or the seats changed (undo, a preset, a project)
    // A part lit at once while any key in its range is held, fading over about a quarter second after.
    auto changed = false;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        auto playing = false;
        for (int k = partKeys[i].first; k <= partKeys[i].second && ! playing; ++k)
            playing = processor.getSampler().isKeyHeld (k);
        const auto target = playing ? 1.0f : juce::jmax (0.0f, glow[i] - 1.0f / 8.0f);
        if (std::abs (target - glow[i]) > 1.0e-3f)
        {
            glow[i] = target;
            changed = true;
        }
    }
    if (changed)
        repaint();
}

void StageView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff060407));
    const auto& image = getPicture();
    if (image.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (image, pictureArea());
    }
    const auto stageOn = processor.getParameters().getRawParameterValue (ParamIDs::stageOn)->load() > 0.5f;

    // The orchestra's usual places, faintly (those no part loaded sits on).
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (const auto& place : stage::places())
    {
        auto taken = false;
        for (const auto& p : parts)
            taken = taken || p.seat.x == place.seat.x && p.seat.y == place.seat.y;
        if (taken)
            continue;
        const auto path = zone (place.seat);
        g.setColour (juce::Colour (0xffff3a48).withAlpha (0.22f));
        g.strokePath (path, juce::PathStrokeType (1.0f));
        const auto c = toView ({ place.seat.x, place.seat.y });
        const auto label = juce::Rectangle<float> (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), place.name) + 12.0f, 15.0f).withCentre (c);
        g.setColour (juce::Colour (0xff060407).withAlpha (0.75f));   // (a dark backing: readable over the risers' neon)
        g.fillRoundedRectangle (label, 4.0f);
        g.setColour (juce::Colour (0xff8a8a92).withAlpha (0.8f));
        g.drawText (place.name, label, juce::Justification::centred, false);
    }

    // The parts loaded: lit zones, brighter while they play, a pearl dot to drag them by, their names.
    for (int i = 0; i < (int) parts.size(); ++i)
    {
        const auto& part = parts[(size_t) i];
        const auto path = zone (part.seat);
        const auto bounds = path.getBounds();
        const auto amount = 0.35f + 0.5f * lit (i) + (i == hovering || i == dragging ? 0.15f : 0.0f);
        ParagonColours::setFill (g, ParagonColours::accent, bounds);
        g.setOpacity (juce::jlimit (0.0f, 1.0f, (stageOn ? 0.35f : 0.18f) * amount));
        g.fillPath (path);
        g.setOpacity (1.0f);
        ParagonColours::setFill (g, ParagonColours::accent, bounds);
        g.setOpacity (stageOn ? 0.9f : 0.5f);
        g.strokePath (path, juce::PathStrokeType (2.0f));
        g.setOpacity (1.0f);
        const auto c = toView ({ part.seat.x, part.seat.y });
        g.setColour (juce::Colour (0xffff3a48).withAlpha (0.3f));
        g.fillEllipse (juce::Rectangle<float> (26.0f, 26.0f).withCentre (c));
        g.setColour (pearl);
        g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (c));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        const auto nameArea = juce::Rectangle<float> (260.0f, 16.0f).withCentre ({ c.x, bounds.getY() - 10.0f });
        g.setColour (juce::Colour (0xff060407).withAlpha (0.7f));
        const auto label = library::partLabel (part.name).toUpperCase();   // (0.14.0: plain section names)
        g.fillRoundedRectangle (nameArea.withSizeKeepingCentre (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 14.0f, 17.0f), 4.0f);
        g.setColour (pearl);
        g.drawText (label, nameArea, juce::Justification::centred, false);
        // Its distances while it's pointed at or dragged.
        if (i == hovering || i == dragging)
        {
            g.setFont (juce::FontOptions (10.5f));
            const auto side = part.seat.x < -0.25f ? "L " + juce::String (-part.seat.x, 1) + " m"
                            : part.seat.x > 0.25f ? "R " + juce::String (part.seat.x, 1) + " m" : juce::String ("CENTRE");
            const auto text = side + "  " + juce::String (part.seat.y, 1) + " m BACK";
            const auto under = juce::Rectangle<float> (220.0f, 14.0f).withCentre ({ c.x, bounds.getBottom() + 10.0f });
            g.setColour (juce::Colour (0xff060407).withAlpha (0.8f));
            g.fillRoundedRectangle (under.withSizeKeepingCentre (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 12.0f, 15.0f), 4.0f);
            g.setColour (pearl.withAlpha (0.85f));
            g.drawText (text, under, juce::Justification::centred, false);
        }
    }

    // The mics: at the front, their distance written beside them.
    const auto area = pictureArea();
    const auto scale = image.isNull() ? 1.0f : area.getWidth() / (float) image.getWidth();
    const auto mic = grid().podium * scale + area.getPosition() + juce::Point<float> (0.0f, 34.0f);
    for (const auto dx : { -16.0f, 0.0f, 16.0f })
    {
        g.setColour (pearl);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (mic.translated (dx, dx == 0.0f ? -8.0f : 0.0f)));
    }
    g.setColour (pearl.withAlpha (0.8f));
    g.drawLine (mic.x - 16.0f, mic.y, mic.x + 16.0f, mic.y, 1.2f);
    g.drawLine (mic.x, mic.y, mic.x, mic.y - 8.0f, 1.2f);
    const auto metres = processor.getParameters().getRawParameterValue (ParamIDs::stageMic)->load();
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("MICS  " + juce::String (metres, metres < 10.0f ? 1 : 0) + " M IN FRONT", juce::Rectangle<float> (200.0f, 14.0f).withCentre (mic.translated (0.0f, 16.0f)),
                juce::Justification::centred, false);
    if (! stageOn)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("STAGE IS OFF: THE PARTS PLAY AS RECORDED (THE RACK'S STAGE PAGE)", getLocalBounds().toFloat().removeFromBottom (26.0f),
                    juce::Justification::centred, false);
    }
}

void StageView::mouseMove (const juce::MouseEvent& e)
{
    const auto over = partAt (e.position);
    if (over != hovering)
    {
        hovering = over;
        setMouseCursor (over >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void StageView::mouseDown (const juce::MouseEvent& e)
{
    dragging = partAt (e.position);
    if (dragging >= 0)
    {
        const auto& seat = parts[(size_t) dragging].seat;
        grabOffset = toView ({ seat.x, seat.y }) - e.position;
    }
}

void StageView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    auto& part = parts[(size_t) dragging];
    const auto m = toStage (e.position + grabOffset);
    part.seat = stage::clamp ({ m.x, m.y, part.seat.width });
    part.moved = true;
    processor.setSeat (part.name, part.seat);
    repaint();
}

void StageView::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        processor.seatMoveFinished();   // the preset modified; one undo step
    dragging = -1;
}
