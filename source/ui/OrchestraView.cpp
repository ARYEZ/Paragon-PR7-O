#include "OrchestraView.h"

#include "OrchestraImages.h"
#include "ui/ParagonLookAndFeel.h"

#include <tuple>

//==============================================================================
namespace orchestra
{
    Kind kindOf (const juce::String& name)
    {
        // From a file's or an instrument's name (VSCO 2 CE's: ViolinEns, SViolin, ViolaEns, CelloEns, Contrabass...;
        // 0.6.0, its brass: TrumpetSus, FHornSus, TromboneSus, Tuba-KS...).
        const auto n = name.toLowerCase();
        if (n.contains ("trombone") || n.contains ("tbn"))
            return Kind::trombone;
        if (n.contains ("tuba"))
            return Kind::tuba;
        if (n.contains ("trumpet") || n.contains ("tpt"))
            return Kind::trumpet;
        if (n.contains ("horn") && ! n.contains ("english") && ! n.contains ("basset") && ! n.contains ("flugel")
            && ! n.contains ("alphorn") && ! n.contains ("shorn"))
            return Kind::horn;
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
            case Kind::trumpet: case Kind::horn: case Kind::trombone: case Kind::tuba:
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
            case Kind::trumpet:  return "trumpet";
            case Kind::horn:     return "horn";
            case Kind::trombone: return "trombone";
            case Kind::tuba:     return "tuba";
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

    bool isBrass (Kind k)
    {
        return k == Kind::trumpet || k == Kind::horn || k == Kind::trombone || k == Kind::tuba;
    }

    Fingering fingeringFor (Kind k, int note)
    {
        Fingering f;
        if (! isBrass (k))
            return f;
        // The harmonics over the fundamental (semitones; the 7th, 11th, 13th and 14th left out: out of tune).
        static constexpr std::array<std::pair<int, int>, 12> harmonics { { { 1, 0 }, { 2, 12 }, { 3, 19 }, { 4, 24 }, { 5, 28 }, { 6, 31 },
                                                                          { 8, 36 }, { 9, 38 }, { 10, 40 }, { 12, 43 }, { 15, 47 }, { 16, 48 } } };
        // Each instrument: its fundamental, its lowest and highest harmonic used.
        const auto [fundamental, lowest, highest] = k == Kind::trumpet  ? std::tuple { 46, 2, 12 }    // B-flat2
                                                  : k == Kind::horn     ? std::tuple { 29, 2, 16 }    // F1
                                                  : k == Kind::trombone ? std::tuple { 34, 1, 10 }    // B-flat1 (pedals too)
                                                                        : std::tuple { 22, 1, 10 };   // B-flat0
        for (const auto& [n, offset] : harmonics)
        {
            const auto pitch = fundamental + offset;
            if (n < lowest || n > highest || pitch < note)
                continue;
            const auto lower = pitch - note;
            // How far it can come down: six semitones (the tuba: none on its pedal, eleven under its second harmonic
            // with the 4th valve).
            const auto reach = k == Kind::tuba ? (n == 1 ? 0 : n == 2 ? 11 : 6) : 6;
            if (lower > reach)
                return f;
            f.playable = true;
            if (k == Kind::trombone)
            {
                f.position = lower + 1;
                return f;
            }
            static constexpr std::array<int, 12> threeValves { 0b000, 0b010, 0b001, 0b011, 0b110, 0b101, 0b111, 0, 0, 0, 0, 0 };
            static constexpr std::array<int, 12> fourValves { 0b0000, 0b0010, 0b0001, 0b0011, 0b0110, 0b1000, 0b1010,
                                                              0b1001, 0b1011, 0b1110, 0b1101, 0b1111 };
            f.valves = (k == Kind::tuba ? fourValves : threeValves)[(size_t) lower];
            return f;
        }
        return f;
    }

    std::vector<Stop> stopsFor (Kind k, std::vector<int> notes)
    {
        std::vector<Stop> stops;
        if (k == Kind::none || isBrass (k))
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

    // An instrument's geometry from orchestra.json: its strings, or its valves and slide (0.6.0), and its area.
    template <typename Strings>
    Strings geometryOf (const juce::var& g)
    {
        Strings s;
        if (const auto* strings = g["strings"].getArray())
            for (int i = 0; i < 4 && i < strings->size(); ++i)
            {
                s.bridge[(size_t) i] = pointOf ((*strings)[i]["bridge"]);
                s.nut[(size_t) i] = pointOf ((*strings)[i]["nut"]);
            }
        if (const auto* valves = g["valves"].getArray())
            for (const auto& v : *valves)
                s.valves.push_back (pointOf (v));
        if (const auto* slide = g["slide"].getArray(); slide != nullptr && slide->size() == 2)
            s.slide = std::pair { pointOf ((*slide)[0]), pointOf ((*slide)[1]) };
        const auto a = g["area"];
        s.area = juce::Rectangle<float>::leftTopRightBottom ((float) a[0], (float) a[1], (float) a[2], (float) a[3]);
        return s;
    }

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
        p.instruments[k] = geometryOf<Strings> (geometry()["single"][juce::Identifier (name)]);
    }
    return p;
}

const OrchestraView::Picture& OrchestraView::section (bool lit, bool brass)
{
    static Picture pictures[4];
    auto& p = pictures[(lit ? 1 : 0) + (brass ? 2 : 0)];
    if (p.image.isNull())
    {
        using orchestra::Kind;
        int size = 0;
        const char* name = brass ? (lit ? "brasssectionlit_jpg" : "brasssection_jpg") : (lit ? "sectionlit_jpg" : "section_jpg");
        if (const auto* data = OrchestraImages::getNamedResource (name, size))
            p.image = inFamily (juce::ImageFileFormat::loadFrom (data, (size_t) size));
        const auto kinds = brass ? std::vector { Kind::tuba, Kind::trombone, Kind::horn, Kind::trumpet }
                                 : std::vector { Kind::bass, Kind::cello, Kind::viola, Kind::violin };
        for (const auto k : kinds)
        {
            const auto s = geometryOf<Strings> (geometry()[brass ? "brassSection" : "section"][juce::Identifier (orchestra::nameOf (k))]);
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
    brassSection = false;
    if (const auto s = processor.getSection())
    {
        // The family with more pictured parts (0.6.0: strings or brass; the strings when even); the other's parts play
        // unpictured.
        auto strings = 0, brass = 0;
        for (const auto& part : s->parts)
            if (const auto k = orchestra::kindOf (sampler::section::nameOf (part)); k != orchestra::Kind::none)
                ++(orchestra::isBrass (k) ? brass : strings);
        brassSection = brass > strings;
        for (const auto& part : s->parts)
            if (const auto k = orchestra::kindOf (sampler::section::nameOf (part));
                k != orchestra::Kind::none && orchestra::isBrass (k) == brassSection && partKeys.count (k) == 0)
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

std::vector<juce::Point<float>> OrchestraView::getValves (orchestra::Kind k, bool inSection)
{
    const auto& p = inSection ? section (false, true) : single (k);
    const auto it = p.instruments.find (k);
    return it != p.instruments.end() ? it->second.valves : std::vector<juce::Point<float>> {};
}

bool OrchestraView::hasSlide (orchestra::Kind k, bool inSection)
{
    const auto& p = inSection ? section (false, true) : single (k);
    const auto it = p.instruments.find (k);
    return it != p.instruments.end() && it->second.slide.has_value();
}

int OrchestraView::getBrassNote (orchestra::Kind k) const
{
    // One note at a time: the highest held in its keys.
    std::vector<int> notes;
    if (mode == Mode::single && k == kind)
        notes = soundingNotes (0, 127);
    else if (const auto it = partKeys.find (k); mode == Mode::section && it != partKeys.end())
        notes = soundingNotes (it->second.first, it->second.second);
    return notes.empty() ? -1 : *std::max_element (notes.begin(), notes.end());
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
    auto scale = juce::jmax ((float) getWidth() / (float) image.getWidth(), (float) getHeight() / (float) image.getHeight());
    auto w = (float) image.getWidth() * scale, h = (float) image.getHeight() * scale;
    auto x = ((float) getWidth() - w) * 0.5f;
    // 0.6.0: one trombone: its slide's run (1st to 7th position) kept in view, the picture moved sideways for it and,
    // where the view is too narrow even so, made a little smaller (a strip of the background above and below).
    if (mode == Mode::single && kind == orchestra::Kind::trombone)
        if (const auto& g = single (kind).instruments.at (kind); g.slide.has_value())
        {
            constexpr float margin = 90.0f;   // (picture px, each side: room for the marks' dots and numbers)
            const auto from = g.slide->first.x - margin, to = g.slide->second.x + margin;
            scale = juce::jmin (scale, (float) getWidth() / (to - from));
            w = (float) image.getWidth() * scale;
            h = (float) image.getHeight() * scale;
            x = (float) getWidth() * 0.5f - (from + to) * 0.5f * scale;
            x = w >= (float) getWidth() ? juce::jlimit ((float) getWidth() - w, 0.0f, x) : ((float) getWidth() - w) * 0.5f;
        }
    return { x, ((float) getHeight() - h) * 0.5f, w, h };
}

void OrchestraView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff060407));
    if (mode == Mode::none)
        return;
    const auto& base = mode == Mode::single ? single (kind) : section (false, brassSection);
    if (base.image.isNull())
        return;
    const auto area = pictureArea (base.image);
    const auto toView = juce::AffineTransform::scale (area.getWidth() / (float) base.image.getWidth())
                            .translated (area.getX(), area.getY());
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (base.image, area);

    if (mode == Mode::single)
    {
        if (orchestra::isBrass (kind))
            paintFingering (g, base.instruments.at (kind), kind, getBrassNote (kind), toView);
        else
            paintStops (g, base.instruments.at (kind), getStops (kind), toView);
        return;
    }
    // A section: each part's lit picture over the dim one while it plays, then its notes.
    const auto& lit = section (true, brassSection);
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
        if (orchestra::isBrass (k))
            paintFingering (g, it->second, k, getBrassNote (k), toView);
        else
            paintStops (g, it->second, getStops (k), toView);
    }
}

void OrchestraView::paintFingering (juce::Graphics& g, const Strings& geometry, orchestra::Kind k, int note,
                                    juce::AffineTransform toView) const
{
    // 0.6.0: a brass note's valves lit (a halo, orange, a pearl core, as the strings' dots), or the trombone's slide
    // positions marked 1-7 where the hand goes, the note's lit with the slide's run to it.
    const juce::Colour pink (0xffff2d6e), orange (0xffff6a24), pearl (0xfff4eef1);
    const auto scale = std::sqrt (std::abs (toView.getDeterminant()));
    const auto r = juce::jmax (6.5f, 10.0f * scale);
    const auto dot = [&] (juce::Point<float> at)
    {
        g.setColour (orange.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (r * 4.0f, r * 4.0f).withCentre (at));
        g.setColour (orange);
        g.fillEllipse (juce::Rectangle<float> (r * 2.2f, r * 2.2f).withCentre (at));
        g.setColour (pearl);
        g.fillEllipse (juce::Rectangle<float> (r * 1.1f, r * 1.1f).withCentre (at));
    };
    const auto fingering = note >= 0 ? orchestra::fingeringFor (k, note) : orchestra::Fingering {};
    if (geometry.slide.has_value())
    {
        const auto first = geometry.slide->first.transformedBy (toView), last = geometry.slide->second.transformedBy (toView);
        const auto at = [&] (int position) { return first + (last - first) * ((float) (position - 1) / 6.0f); };
        if (fingering.playable && fingering.position >= 1)
        {
            const juce::Line<float> run (first, at (fingering.position));
            for (const auto& [width, alpha] : { std::pair { 20.0f, 0.10f }, std::pair { 10.0f, 0.22f }, std::pair { 5.0f, 0.5f } })
            {
                g.setColour (pink.withAlpha (alpha));
                g.drawLine (run, width * scale);
            }
            g.setColour (pink.interpolatedWith (pearl, 0.6f));
            g.drawLine (run, 2.6f * scale);
        }
        g.setFont (juce::FontOptions (juce::jmax (9.0f, 19.0f * scale)));
        for (int p = 1; p <= 7; ++p)
        {
            const auto lit = fingering.playable && fingering.position == p;
            const auto c = at (p);
            if (lit)
                dot (c);
            else
            {
                const auto small = juce::jmax (2.5f, 4.0f * scale);
                g.setColour (pink.withAlpha (0.75f));
                g.fillEllipse (juce::Rectangle<float> (small * 2.0f, small * 2.0f).withCentre (c));
            }
            g.setColour (lit ? pearl : juce::Colour (0xff8a8a92));
            const auto below = juce::jmax (9.0f, 22.0f * scale);
            g.drawText (juce::String (p), juce::Rectangle<float> (40.0f * juce::jmax (0.5f, scale), below * 1.2f)
                                              .withCentre ({ c.x, c.y + below + below * 0.4f }),
                        juce::Justification::centred, false);
        }
        return;
    }
    if (! fingering.playable)
        return;
    for (size_t i = 0; i < geometry.valves.size() && i < 4; ++i)
        if ((fingering.valves >> i) & 1)
            dot (geometry.valves[i].transformedBy (toView));
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
