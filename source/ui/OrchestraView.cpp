#include "OrchestraView.h"

#include "OrchestraImages.h"
#include "ui/ParagonLookAndFeel.h"

//==============================================================================
namespace orchestra
{
    Kind kindOf (const juce::String& name)
    {
        // From a file's or an instrument's name (VSCO 2 CE's: ViolinEns, SViolin, ViolaEns, CelloEns, Contrabass...).
        const auto n = name.toLowerCase();
        if (n.contains ("viola") || n.contains ("vla"))
            return Kind::viola;
        if (n.contains ("violin") || n.contains ("vln") || n.contains ("fiddle"))
            return Kind::violin;
        if (n.contains ("cello") || n.contains ("vc ") || n.contains ("vcl"))
            return Kind::cello;
        if (n.contains ("contrabass") || n.contains ("double bass") || n.contains ("doublebass") || n.contains ("ctbss")
            || n.contains ("string bass") || (n.contains ("bass") && ! n.contains ("bassoon") && ! n.contains ("clarinet")
                                              && ! n.contains ("trombone") && ! n.contains ("tuba") && ! n.contains ("drum")
                                              && ! n.contains ("flute")))
            return Kind::bass;
        return Kind::none;
    }

    std::array<int, 4> openStrings (Kind k)
    {
        switch (k)
        {
            case Kind::violin: return { 55, 62, 69, 76 };   // G3 D4 A4 E5
            case Kind::viola:  return { 48, 55, 62, 69 };   // C3 G3 D4 A4
            case Kind::cello:  return { 36, 43, 50, 57 };   // C2 G2 D3 A3
            case Kind::bass:   return { 28, 33, 38, 43 };   // E1 A1 D2 G2
            case Kind::none:   break;
        }
        return { -1, -1, -1, -1 };
    }

    juce::String nameOf (Kind k)
    {
        switch (k)
        {
            case Kind::violin: return "violin";
            case Kind::viola:  return "viola";
            case Kind::cello:  return "cello";
            case Kind::bass:   return "bass";
            case Kind::none:   break;
        }
        return {};
    }

    float alongString (int semitones, float nutX, float bridgeX, float edge)
    {
        // A stop's place, nut (0) to bridge (1): 1 - 2^(-semitones / 12). 0.5.1: at the common scale the cello's and
        // bass's bridge end is off the left edge; a note stopped past `edge` lights at it, on its string.
        auto along = 1.0f - std::pow (2.0f, -(float) semitones / 12.0f);
        if (nutX > edge && bridgeX < edge && std::abs (bridgeX - nutX) > 1.0f)
            along = juce::jmin (along, (nutX - edge) / (nutX - bridgeX));
        return along;
    }

    std::vector<Stop> stopsFor (Kind k, std::vector<int> notes)
    {
        std::vector<Stop> stops;
        if (k == Kind::none)
            return stops;
        const auto open = openStrings (k);
        std::sort (notes.begin(), notes.end(), std::greater<int>());
        std::array<bool, 4> used {};
        for (const auto note : notes)
        {
            Stop stop;
            stop.note = note;
            // The highest free open string at or under it; under the lowest string, the lowest (open).
            for (int s = 3; s >= 0 && stop.string < 0; --s)
                if (! used[(size_t) s] && open[(size_t) s] <= note)
                    stop.string = s;
            if (stop.string < 0 && note < open[0] && ! used[0])
                stop.string = 0;
            if (stop.string >= 0)
            {
                used[(size_t) stop.string] = true;
                stop.semitones = juce::jlimit (0, 28, note - open[(size_t) stop.string]);   // the fingerboard's end
            }
            stops.push_back (stop);
        }
        return stops;
    }
}

//==============================================================================
namespace
{
    // The strings' colours, low to high (as the render's, and PR7-I's strings: pink, rose, red, orange).
    const std::array<juce::Colour, 4> stringColours { juce::Colour (0xffff2d6e), juce::Colour (0xffff2b48),
                                                      juce::Colour (0xffff3a28), juce::Colour (0xffff6a24) };

    juce::var geometry()
    {
        static const auto parsed = juce::JSON::parse (juce::String::fromUTF8 (OrchestraImages::orchestra_json, OrchestraImages::orchestra_jsonSize));
        return parsed;
    }

    juce::Point<float> pointOf (const juce::var& v) { return { (float) v[0], (float) v[1] }; }

    // Every hue into the family's range once more as a picture loads (JPEG moves a few colours a few degrees past the
    // processing's), as PR7-D's kit view does: a colour outside 318 through 0 to 42 degrees goes to the nearer end (4 degrees inside it).
    juce::Image inFamily (juce::Image image)
    {
        if (image.isNull())
            return image;
        image = image.convertedToFormat (juce::Image::ARGB);
        juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
            {
                const auto c = pixels.getPixelColour (x, y);
                const auto hue = c.getHue() * 360.0f;
                // (to 4 degrees inside the range: 8-bit rounding moves a colour set exactly to its end a little past
                // it. Every pixel with any colour, the nearly grey too: drawn smaller, a nearly grey lilac pixel blended
                // into a bright pink one made a 317-degree pink on Windows' renderer)
                if (c.getSaturation() > 0.0f && hue > 38.0f && hue < 322.0f)
                    pixels.setPixelColour (x, y, c.withHue ((hue - 38.0f < 322.0f - hue ? 38.0f : 322.0f) / 360.0f));
            }
        return image;
    }
}

const OrchestraView::Picture& OrchestraView::single (orchestra::Kind k)
{
    static std::map<orchestra::Kind, Picture> pictures;
    auto& p = pictures[k];
    if (p.image.isNull() && k != orchestra::Kind::none)
    {
        const auto name = orchestra::nameOf (k);
        int size = 0;
        if (const auto* data = OrchestraImages::getNamedResource ((name + "_jpg").toRawUTF8(), size))
            p.image = inFamily (juce::ImageFileFormat::loadFrom (data, (size_t) size));
        const auto g = geometry()["single"][juce::Identifier (name)];
        Strings s;
        for (int i = 0; i < 4; ++i)
        {
            s.bridge[(size_t) i] = pointOf (g["strings"][i]["bridge"]);
            s.nut[(size_t) i] = pointOf (g["strings"][i]["nut"]);
        }
        const auto a = g["area"];
        s.area = juce::Rectangle<float>::leftTopRightBottom ((float) a[0], (float) a[1], (float) a[2], (float) a[3]);
        p.instruments[k] = s;
    }
    return p;
}

const OrchestraView::Picture& OrchestraView::section (bool lit)
{
    static Picture pictures[2];
    auto& p = pictures[lit ? 1 : 0];
    if (p.image.isNull())
    {
        int size = 0;
        if (const auto* data = OrchestraImages::getNamedResource (lit ? "sectionlit_jpg" : "section_jpg", size))
            p.image = inFamily (juce::ImageFileFormat::loadFrom (data, (size_t) size));
        for (const auto k : { orchestra::Kind::bass, orchestra::Kind::cello, orchestra::Kind::viola, orchestra::Kind::violin })
        {
            const auto g = geometry()["section"][juce::Identifier (orchestra::nameOf (k))];
            Strings s;
            for (int i = 0; i < 4; ++i)
            {
                s.bridge[(size_t) i] = pointOf (g["strings"][i]["bridge"]);
                s.nut[(size_t) i] = pointOf (g["strings"][i]["nut"]);
            }
            const auto a = g["area"];
            s.area = juce::Rectangle<float>::leftTopRightBottom ((float) a[0], (float) a[1], (float) a[2], (float) a[3]);
            p.instruments[k] = s;
            if (lit && p.image.isValid())
            {
                // Its area widened, the edges faded out over 70 px: the glow and its reflection on the floor melt into
                // the dim picture instead of stopping at a box.
                constexpr int feather = 70;
                const auto area = s.area.getSmallestIntegerContainer().expanded (feather).getIntersection (p.image.getBounds());
                juce::Image layer (juce::Image::ARGB, area.getWidth(), area.getHeight(), true);
                {
                    const juce::Image::BitmapData from (p.image, area.getX(), area.getY(), area.getWidth(), area.getHeight());
                    juce::Image::BitmapData to (layer, juce::Image::BitmapData::writeOnly);
                    for (int y = 0; y < area.getHeight(); ++y)
                        for (int x = 0; x < area.getWidth(); ++x)
                        {
                            const auto edge = juce::jmin (juce::jmin (x, area.getWidth() - 1 - x), juce::jmin (y, area.getHeight() - 1 - y));
                            const auto t = juce::jlimit (0.0f, 1.0f, (float) edge / (float) feather);
                            const auto alpha = t * t * (3.0f - 2.0f * t);
                            to.setPixelColour (x, y, from.getPixelColour (x, y).withMultipliedAlpha (alpha));
                        }
                }
                p.litAreas[k] = { layer, area };
            }
        }
    }
    return p;
}

//==============================================================================
OrchestraView::OrchestraView (ParagonX1AudioProcessor& p) : processor (p)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
    refresh();
    startTimerHz (30);
}

void OrchestraView::refresh()
{
    shownProgram = processor.getProgram();
    shownSource = processor.getInstrumentFile();
    mode = Mode::none;
    kind = orchestra::Kind::none;
    partKeys.clear();
    if (const auto s = processor.getSection())
    {
        for (const auto& part : s->parts)
            if (const auto k = orchestra::kindOf (sampler::section::nameOf (part)); k != orchestra::Kind::none && partKeys.count (k) == 0)
                partKeys[k] = { part.low, part.high };
        if (! partKeys.empty())
            mode = Mode::section;
    }
    else if (shownProgram != nullptr || shownSource.isNotEmpty())
    {
        kind = orchestra::kindOf (shownProgram != nullptr ? shownProgram->name : sampler::section::describe (shownSource));
        if (kind != orchestra::Kind::none)
            mode = Mode::single;
    }
    repaint();
}

float OrchestraView::getGlow (orchestra::Kind k) const noexcept
{
    const auto it = glow.find (k);
    return it != glow.end() ? it->second : 0.0f;
}

std::vector<int> OrchestraView::soundingNotes (int low, int high) const
{
    // The keys held, as they sound (TRANSPOSE added).
    std::vector<int> notes;
    for (int k = juce::jmax (0, low); k <= juce::jmin (127, high); ++k)
        if (shownHeld[(size_t) k])
            notes.push_back (juce::jlimit (0, 127, k + shownTranspose));
    return notes;
}

std::vector<orchestra::Stop> OrchestraView::getStops (orchestra::Kind k) const
{
    if (mode == Mode::single && k == kind)
        return orchestra::stopsFor (k, soundingNotes (0, 127));
    if (mode == Mode::section)
        if (const auto it = partKeys.find (k); it != partKeys.end())
            return orchestra::stopsFor (k, soundingNotes (it->second.first, it->second.second));
    return {};
}

void OrchestraView::timerCallback()
{
    if (processor.getProgram() != shownProgram || processor.getInstrumentFile() != shownSource)
        refresh();
    std::array<bool, 128> held {};
    const auto* program = shownProgram.get();
    for (int k = 0; k < 128; ++k)
        held[(size_t) k] = processor.getSampler().isKeyHeld (k) && (program == nullptr || ! program->isKeyswitch (k));
    const auto transpose = juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::instTranspose)->load());
    auto changed = held != shownHeld || transpose != shownTranspose;
    shownHeld = held;
    shownTranspose = transpose;
    // A section's parts: lit at once while they play, fading over about a quarter second after.
    if (mode == Mode::section)
        for (const auto& [k, keys] : partKeys)
        {
            const auto playing = ! soundingNotes (keys.first, keys.second).empty();
            auto& g = glow[k];
            const auto target = playing ? 1.0f : juce::jmax (0.0f, g - 1.0f / 8.0f);
            if (std::abs (target - g) > 1.0e-3f)
            {
                g = target;
                changed = true;
            }
        }
    if (changed)
        repaint();
}

juce::Rectangle<float> OrchestraView::pictureArea (const juce::Image& image) const
{
    // Filling the view, its middle kept: cropped at the edges, never stretched.
    if (image.isNull())
        return getLocalBounds().toFloat();
    const auto scale = juce::jmax ((float) getWidth() / (float) image.getWidth(), (float) getHeight() / (float) image.getHeight());
    const auto w = (float) image.getWidth() * scale, h = (float) image.getHeight() * scale;
    return { ((float) getWidth() - w) * 0.5f, ((float) getHeight() - h) * 0.5f, w, h };
}

void OrchestraView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff060407));
    if (mode == Mode::none)
        return;
    const auto& base = mode == Mode::single ? single (kind) : section (false);
    if (base.image.isNull())
        return;
    const auto area = pictureArea (base.image);
    const auto toView = juce::AffineTransform::scale (area.getWidth() / (float) base.image.getWidth())
                            .translated (area.getX(), area.getY());
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (base.image, area);

    if (mode == Mode::single)
    {
        paintStops (g, base.instruments.at (kind), getStops (kind), toView);
        return;
    }
    // A section: each part's lit picture over the dim one while it plays, then its notes.
    const auto& lit = section (true);
    for (const auto& [k, keys] : partKeys)
    {
        juce::ignoreUnused (keys);
        const auto amount = getGlow (k);
        const auto it = base.instruments.find (k);
        if (it == base.instruments.end())
            continue;
        if (const auto layer = lit.litAreas.find (k); amount > 0.0f && layer != lit.litAreas.end())
        {
            const auto target = layer->second.second.toFloat().transformedBy (toView);
            g.setOpacity (amount);
            g.drawImage (layer->second.first, target);
            g.setOpacity (1.0f);
        }
        paintStops (g, it->second, getStops (k), toView);
    }
}

void OrchestraView::paintStops (juce::Graphics& g, const Strings& strings, const std::vector<orchestra::Stop>& stops,
                                juce::AffineTransform toView) const
{
    // Each note: its string lit from where it's stopped to the bridge (the part that sounds), a dot where it's stopped.
    for (const auto& stop : stops)
    {
        if (stop.string < 0)
            continue;
        const auto s = (size_t) stop.string;
        const auto nut = strings.nut[s].transformedBy (toView), bridge = strings.bridge[s].transformedBy (toView);
        const auto at = nut + (bridge - nut) * orchestra::alongString (stop.semitones, nut.x, bridge.x, 14.0f);
        const auto colour = stringColours[s];
        const auto scale = std::sqrt (std::abs (toView.getDeterminant()));
        const juce::Line<float> sounding (at, bridge);
        for (const auto& [width, alpha] : { std::pair { 20.0f, 0.10f }, std::pair { 10.0f, 0.22f }, std::pair { 5.0f, 0.5f } })
        {
            g.setColour (colour.withAlpha (alpha));
            g.drawLine (sounding, width * scale);
        }
        g.setColour (colour.interpolatedWith (juce::Colour (0xfff4eef1), 0.6f));
        g.drawLine (sounding, 2.6f * scale);
        // The dot: a halo, the string's colour, a pearl core (never smaller than 6.5 px: a small window stays readable).
        const auto r = juce::jmax (6.5f, 10.0f * scale);
        g.setColour (colour.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (r * 4.0f, r * 4.0f).withCentre (at));
        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (r * 2.2f, r * 2.2f).withCentre (at));
        g.setColour (juce::Colour (0xfff4eef1));
        g.fillEllipse (juce::Rectangle<float> (r * 1.1f, r * 1.1f).withCentre (at));
    }
}
