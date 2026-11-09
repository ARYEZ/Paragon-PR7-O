#include "InsertPages.h"

#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthDisplays.h"

namespace
{
    using Model = effects::Drive::Model;
    using Mode = effects::Glitch::Mode;

    juce::String driveKey (const effects::Drive::Settings& s)
    {
        return juce::String ((int) s.model) + ":" + juce::String (s.drive) + ":" + juce::String (s.tone) + ":" + juce::String (s.levelDb) + ":"
             + juce::String (s.attack) + ":" + juce::String (s.gate) + ":" + juce::String (s.high) + ":" + juce::String (s.low) + ":"
             + juce::String (s.middle) + ":" + juce::String (s.midFreq);
    }

    // A level per 5 ms window.
    std::vector<float> levels (const std::vector<float>& x, int window)
    {
        std::vector<float> result;
        for (size_t start = 0; start + (size_t) window <= x.size(); start += (size_t) window)
        {
            double sum = 0.0;
            for (size_t i = start; i < start + (size_t) window; ++i)
                sum += x[i] * x[i];
            result.push_back ((float) std::sqrt (sum / window));
        }
        return result;
    }
}

//==============================================================================
DrivePreview::DrivePreview (ParagonX1AudioProcessor& p, int b) : processor (p), bus (b)
{
    setTooltip ("What the DRIVE does at these settings: a copy of it run on a 120 Hz sine at half level. Left, one cycle in "
                "(dim) and out (bright); right, the harmonics it adds, in dB below the note. A real chord distorts with more "
                "going on (the notes mix), so this is a guide, not the whole story.");
    render();
    startTimerHz (5);
}

void DrivePreview::timerCallback()
{
    if (isShowing() && driveKey (processor.getInsertDriveSettings (bus)) != shownKey)
    {
        render();
        repaint();
    }
}

void DrivePreview::render()
{
    auto settings = processor.getInsertDriveSettings (bus);
    shownKey = driveKey (settings);
    settings.on = true;   // drawn as it plays when on
    auto drive = std::make_unique<effects::Drive>();
    constexpr int block = 512;
    drive->prepare (sampleRate, block);
    const auto period = (int) (sampleRate / hz);   // 400 samples
    const auto total = period * 24;                // 0.2 s: 12 cycles to settle the fade-in and filters, 12 measured
    std::vector<float> in ((size_t) total), out ((size_t) total);
    for (int i = 0; i < total; ++i)
        in[(size_t) i] = level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate);
    out = in;
    for (int start = 0; start < total; start += block)
    {
        float* channels[] = { out.data() + start };
        drive->process (juce::dsp::AudioBlock<float> (channels, 1, (size_t) juce::jmin (block, total - start)), settings);
    }
    cycleIn.assign (in.end() - period, in.end());
    cycleOut.assign (out.end() - period, out.end());

    // Harmonics over the last 12 cycles (a whole number: no leakage between them).
    const auto from = total - 12 * period;
    float fundamental = 0.0f;
    for (int h = 1; h <= (int) harmonicsDb.size(); ++h)
    {
        double re = 0.0, im = 0.0;
        for (int i = from; i < total; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * h * hz * i / sampleRate;
            re += out[(size_t) i] * std::cos (phase);
            im += out[(size_t) i] * std::sin (phase);
        }
        const auto magnitude = (float) (2.0 * std::sqrt (re * re + im * im) / (12 * period));
        if (h == 1)
            fundamental = juce::jmax (1.0e-9f, magnitude);
        harmonicsDb[(size_t) h - 1] = juce::Decibels::gainToDecibels (magnitude / fundamental, -120.0f);
    }
}

void DrivePreview::paint (juce::Graphics& g)
{
    DisplayStyle::drawScreen (g, getLocalBounds().toFloat());
    auto area = getLocalBounds().toFloat().reduced (24.0f, 20.0f);
    auto bars = area.removeFromRight (area.getWidth() * 0.38f);
    area.removeFromRight (24.0f);

    // One cycle in and out, scaled together.
    auto peak = 0.05f;
    for (const auto* cycle : { &cycleIn, &cycleOut })
        for (auto v : *cycle)
            peak = juce::jmax (peak, std::abs (v));
    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    const auto path = [&] (const std::vector<float>& cycle)
    {
        juce::Path line;
        for (size_t i = 0; i < cycle.size(); ++i)
        {
            const auto x = area.getX() + area.getWidth() * (float) i / (float) std::max<size_t> (1, cycle.size() - 1);
            const auto y = area.getCentreY() - area.getHeight() * 0.45f * cycle[i] / peak;
            if (i == 0)
                line.startNewSubPath (x, y);
            else
                line.lineTo (x, y);
        }
        return line;
    };
    g.setColour (ParagonColours::textDim);
    g.strokePath (path (cycleIn), juce::PathStrokeType (1.4f));
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (area));
    g.strokePath (path (cycleOut), juce::PathStrokeType (2.4f));
    ParagonColours::setFill (g, ParagonColours::accent, getLocalBounds());
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    ParagonColours::setTextFill (g, ParagonColours::accent, ParagonColours::textSpan (g.getCurrentFont(), "120 Hz SINE, IN AND OUT", (area.reduced (6.0f, 2.0f)).toFloat(), juce::Justification::topLeft));
    g.drawText ("120 Hz SINE, IN AND OUT", area.reduced (6.0f, 2.0f), juce::Justification::topLeft, false);
    ParagonColours::setFill (g, ParagonColours::accent, getLocalBounds());

    // What the model costs on a bus (both sides), as measured here (InsertTests' report): PR7-G's circuits solved at 8x,
    // on a played saw chord and on loud noise. HALO, the whole fuzz circuit solved at once, works far harder on loud,
    // bright sound (its solver takes many more steps to settle), so it shows both.
    constexpr int percentOnBus[] = { 11, 4, 17, 33, 3, 11, 18 };   // Drive::Model's order
    constexpr int worstOnBus[] = { 14, 4, 17, 120, 3, 13, 19 };
    const auto model = juce::jlimit (0, (int) std::size (percentOnBus) - 1, (int) processor.getInsertDriveSettings (bus).model);
    const auto percent = percentOnBus[model], worst = worstOnBus[model];
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (worst >= 50 ? juce::Colour (0xffff4d4d) : ParagonColours::textDim);
    g.drawText ("CPU ON A BUS: ABOUT " + juce::String (percent) + "% OF A CORE"
                    + (worst >= 50 ? ", UP TO " + juce::String (worst) + "% ON LOUD, BRIGHT SOUND" : juce::String()),
                area.reduced (6.0f, 2.0f), juce::Justification::bottomLeft, false);

    // The harmonics: 0 dB (the note) at the top to -60 at the bottom.
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    auto labels = bars.removeFromBottom (14.0f);
    constexpr float floorDb = -60.0f;
    for (const auto db : { 0.0f, -20.0f, -40.0f })
    {
        const auto y = bars.getY() + bars.getHeight() * db / floorDb;
        g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
        g.drawHorizontalLine (juce::roundToInt (y), bars.getX(), bars.getRight());
        g.setColour (ParagonColours::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (bars.getX() - 26.0f, y - 7.0f, 22.0f, 14.0f), juce::Justification::centredRight, false);
    }
    const auto width = bars.getWidth() / (float) harmonicsDb.size();
    for (size_t h = 0; h < harmonicsDb.size(); ++h)
    {
        const auto fraction = juce::jlimit (0.0f, 1.0f, 1.0f - harmonicsDb[h] / floorDb);
        const auto bar = juce::Rectangle<float> (bars.getX() + width * (float) h + width * 0.18f, bars.getBottom() - bars.getHeight() * fraction,
                                                 width * 0.64f, bars.getHeight() * fraction);
        if (h == 0)
            g.setColour (ParagonColours::textDim);
        else
            g.setGradientFill (ParagonLookAndFeel::menuHighlight (bar.withHeight (juce::jmax (1.0f, bar.getHeight()))));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (ParagonColours::textDim);
        g.drawText (juce::String ((int) h + 1), labels.withX (bars.getX() + width * (float) h).withWidth (width), juce::Justification::centred, false);
    }
}

//==============================================================================
GlitchPreview::GlitchPreview (ParagonX1AudioProcessor& p, int b) : processor (p), bus (b)
{
    setTooltip ("What GLITCH does at these settings: a copy of it (with its own fixed dice, so the same each time) on a plain riff, "
                "eighth-note plucks at the song's tempo for two bars. The level in (dim) and out (bright) over time; the beats marked.");
    render();
    startTimerHz (5);
}

void GlitchPreview::timerCallback()
{
    if (! isShowing())   // 0.30.0: a hidden page's timer does nothing
        return;
    const auto s = processor.getInsertGlitchSettings (bus);
    const auto key = juce::String ((int) s.mode) + ":" + juce::String ((int) s.sync) + ":" + juce::String (s.lengthMs) + ":" + juce::String (s.division)
                   + ":" + juce::String (s.density) + ":" + juce::String (s.pitch) + ":" + juce::String (s.chaos) + ":" + juce::String (s.mix)
                   + ":" + juce::String (processor.getHostBpm());
    if (isShowing() && key != shownKey)
    {
        render();
        shownKey = key;
        repaint();
    }
}

void GlitchPreview::render()
{
    constexpr double sampleRate = 48000.0;
    constexpr int block = 512;
    auto settings = processor.getInsertGlitchSettings (bus);
    settings.on = true;
    bpm = juce::jlimit (40.0, 300.0, processor.getHostBpm() > 0.0 ? processor.getHostBpm() : 120.0);
    seconds = 8.0 * 60.0 / bpm;   // two bars
    const auto total = (int) (seconds * sampleRate);
    std::vector<float> in ((size_t) total), out;
    const auto eighth = 30.0 / bpm * sampleRate;
    for (int i = 0; i < total; ++i)
    {
        const auto since = std::fmod ((double) i, eighth) / sampleRate;
        in[(size_t) i] = 0.5f * (float) (std::exp (-since / 0.09) * std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sampleRate));
    }
    out = in;
    auto glitch = std::make_unique<effects::Glitch>();
    glitch->prepare (sampleRate, block);
    for (int start = 0; start < total; start += block)
    {
        float* channels[] = { out.data() + start };
        glitch->process (juce::dsp::AudioBlock<float> (channels, 1, (size_t) juce::jmin (block, total - start)), settings, bpm,
                         (double) start / sampleRate * bpm / 60.0);   // as if the song were playing from its start
    }
    levelsIn = levels (in, 240);
    levelsOut = levels (out, 240);
}

void GlitchPreview::paint (juce::Graphics& g)
{
    DisplayStyle::drawScreen (g, getLocalBounds().toFloat());
    const auto area = getLocalBounds().toFloat().reduced (24.0f, 20.0f).withTrimmedBottom (14.0f);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    const auto beats = 8;
    for (int beat = 0; beat <= beats; ++beat)
    {
        const auto x = area.getX() + area.getWidth() * (float) beat / (float) beats;
        g.setColour (ParagonColours::panelEdge.withAlpha (beat % 4 == 0 ? 1.0f : 0.5f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        g.setColour (ParagonColours::textDim);
        if (beat < beats)
            g.drawText (juce::String (beat / 4 + 1) + "." + juce::String (beat % 4 + 1), juce::Rectangle<float> (x, area.getBottom() + 2.0f, 40.0f, 14.0f),
                        juce::Justification::centredLeft, false);
    }
    auto peak = 0.05f;
    for (const auto* list : { &levelsIn, &levelsOut })
        for (auto v : *list)
            peak = juce::jmax (peak, v);
    const auto outline = [&] (const std::vector<float>& list)
    {
        juce::Path shape;
        shape.startNewSubPath (area.getX(), area.getBottom());
        for (size_t i = 0; i < list.size(); ++i)
            shape.lineTo (area.getX() + area.getWidth() * (float) i / (float) std::max<size_t> (1, list.size() - 1),
                          area.getBottom() - area.getHeight() * 0.9f * list[i] / peak);
        shape.lineTo (area.getRight(), area.getBottom());
        shape.closeSubPath();
        return shape;
    };
    g.setColour (ParagonColours::textDim.withAlpha (0.35f));
    g.fillPath (outline (levelsIn));
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (area));
    g.setOpacity (0.75f);
    g.fillPath (outline (levelsOut));
    g.setOpacity (1.0f);
    ParagonColours::setFill (g, ParagonColours::accent, getLocalBounds());
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    ParagonColours::setTextFill (g, ParagonColours::accent, ParagonColours::textSpan (g.getCurrentFont(), "EIGHTH NOTES AT " + juce::String (bpm, 0) + " BPM, IN AND OUT", (area.reduced (6.0f, 2.0f)).toFloat(), juce::Justification::topLeft));
    g.drawText ("EIGHTH NOTES AT " + juce::String (bpm, 0) + " BPM, IN AND OUT", area.reduced (6.0f, 2.0f), juce::Justification::topLeft, false);
    ParagonColours::setFill (g, ParagonColours::accent, getLocalBounds());
}

//==============================================================================
InsertEffectPage::InsertEffectPage (ParagonX1AudioProcessor& p, Kind k, int b) : processor (p), kind (k), bus (b)
{
    auto& state = p.getParameters();
    const auto make = [&state, b] (auto& list, const char* what, const juce::String& label, const juce::String& tip)
    {
        list.push_back (std::make_unique<ParamControl> (state, ParamIDs::ins (b, what), label, tip));
    };
    if (kind == Kind::drive)
    {
        display = std::make_unique<DrivePreview> (p, b);
        make (top, "DriveOn", "ON", "DRIVE on or off.");
        make (top, "DriveModel", "MODEL", "BANSHEE a TS808-style overdrive (bass clean, mids pushed); RABID a RAT-style distortion; COSMONAUT a green "
                                          "Russian Big Muff-style fuzz; HALO a 1960s germanium fuzz; SKYLINE a modern take on the TS808 with ATTACK "
                                          "and a GATE; CHIMERA a Klon-style transparent overdrive; ABYSS a Metal Zone-style distortion with its EQ. "
                                          "Modelled on those circuits, not copies.");
        make (controls, "DriveGain", "DRIVE", "How hard it's driven (Cosmonaut: SUSTAIN; Halo: FUZZ; Chimera: GAIN; Abyss: DIST).");
        make (controls, "DriveTone", "TONE", "Darker to brighter (Rabid: its FILTER turned round; Chimera: TREBLE; Skyline: BRIGHT).");
        make (controls, "DriveAttack", "ATTACK", "Skyline's ATTACK: how much bass reaches the clipping, full (1) to tight (8). 7 is the TS808's.");
        make (controls, "DriveGate", "GATE", "Skyline's gate on its output: 0 off, up to fast and hard.");
        make (controls, "DriveHigh", "HIGH", "Abyss's HIGH.");
        make (controls, "DriveLow", "LOW", "Abyss's LOW.");
        make (controls, "DriveMiddle", "MIDDLE", "Abyss's MIDDLE, at MID FREQ.");
        make (controls, "DriveMidFreq", "MID FREQ", "Abyss's MID FREQ: where MIDDLE works.");
        make (controls, "DriveLevel", "LEVEL", "The level after it.");
    }
    else
    {
        display = std::make_unique<GlitchPreview> (p, b);
        make (top, "GlitchOn", "ON", "GLITCH on or off.");
        make (top, "GlitchMode", "MODE", "PITCH DELAY echoes climbing or falling by PITCH; SEQUENCE tempo-locked stutters (a slice repeated); INTERFERE "
                                         "a failing signal (pieces dropped, reversed or buzzing); TEXTURIZER a cloud of grains, smeared into a pad; "
                                         "KILLSWITCH a toggle switch flicked in time.");
        make (controls, "GlitchSync", "SYNC", "LENGTH in notes at the song's tempo (lit), or in milliseconds.");
        make (controls, "GlitchTime", "LENGTH", "Each step's or piece's length, in milliseconds.");
        make (controls, "GlitchDivision", "LENGTH", "Each step's or piece's length, in notes at the song's tempo.");
        make (controls, "GlitchDensity", "DENSITY", "How much goes on: how often it stutters, how much is broken up, how many grains, how much is cut.");
        make (controls, "GlitchPitch", "PITCH", "Repeats and grains shifted by this.");
        make (controls, "GlitchChaos", "CHAOS", "0: regular and predictable; up: lengths, pitches, directions and positions randomised.");
        make (controls, "GlitchMix", "MIX", "The glitched sound against the straight one.");
    }
    for (auto* list : { &top, &controls })
        for (auto& c : *list)
            addAndMakeVisible (*c);
    addAndMakeVisible (*display);
    back.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (back);
    startTimerHz (10);
}

bool InsertEffectPage::shows (const ParamControl& control) const
{
    const auto& id = control.getParameterId();
    const auto is = [&] (const char* what) { return id == ParamIDs::ins (bus, what); };
    if (kind == Kind::drive)
    {
        const auto model = processor.getInsertDriveSettings (bus).model;
        if (is ("DriveTone"))   return model != Model::halo && model != Model::abyss;
        if (is ("DriveAttack") || is ("DriveGate")) return model == Model::skyline;
        if (is ("DriveHigh") || is ("DriveLow") || is ("DriveMiddle") || is ("DriveMidFreq")) return model == Model::abyss;
        return true;
    }
    const auto synced = processor.getInsertGlitchSettings (bus).sync;
    if (is ("GlitchTime"))     return ! synced;
    if (is ("GlitchDivision")) return synced;
    return true;
}

void InsertEffectPage::timerCallback()
{
    if (! isShowing())   // 0.30.0: a hidden page's timer does nothing
        return;
    juce::String key;
    for (auto& c : controls)
        key << (shows (*c) ? "1" : "0");
    if (key != layoutKey)
    {
        layoutKey = key;
        resized();
    }
}

std::vector<ParamControl*> InsertEffectPage::getShownControls()
{
    std::vector<ParamControl*> result;
    for (auto* list : { &top, &controls })
        for (auto& c : *list)
            if (c->isVisible())
                result.push_back (c.get());
    return result;
}

void InsertEffectPage::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (juce::Rectangle<float> (24.0f, 18.0f, 120.0f, 24.0f)));
    g.drawText (kind == Kind::drive ? "DRIVE" : "GLITCH", juce::Rectangle<int> (24, 14, 160, 26), juce::Justification::centredLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.5f));
    const auto busName = ParamIDs::insertBusNames[bus + 1];
    g.drawText (kind == Kind::drive ? "Bus " + busName + ": first" : "Bus " + busName + ": after DRIVE", juce::Rectangle<int> (24, 38, 200, 16),
                juce::Justification::centredLeft, false);
}

void InsertEffectPage::resized()
{
    auto area = getLocalBounds().reduced (14, 10);
    auto bar = area.removeFromTop (54);
    bar.removeFromLeft (190);
    back.setBounds (bar.removeFromRight (150).withSizeKeepingCentre (150, 30));
    bar.removeFromRight (20);
    top[0]->setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (10);
    top[1]->setBounds (bar.removeFromLeft (200));
    area.removeFromTop (8);
    auto controlRow = area.removeFromBottom (ParamControl::knobHeight);
    area.removeFromBottom (10);
    display->setBounds (area);
    std::vector<juce::Component*> row;
    for (auto& c : controls)
    {
        c->setVisible (shows (*c));
        if (c->isVisible())
            row.push_back (c.get());
    }
    SynthPanel::layoutRow (controlRow, row, 110);
}

//==============================================================================
InsertBusStrip::InsertBusStrip (ParagonX1AudioProcessor& p) : processor (p)
{
    for (int b = 0; b < ParamIDs::numInsertBuses; ++b)
    {
        auto& button = busButtons[(size_t) b];
        button.setButtonText ("BUS " + ParamIDs::insertBusNames[b + 1]);
        button.setClickingTogglesState (true);
        button.setRadioGroupId (972);
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0d0a0e));
        button.setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim.withAlpha (0.6f));
        button.setColour (juce::TextButton::textColourOffId, ParagonColours::textDim);
        button.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
        button.setTooltip ("Insert bus " + ParamIDs::insertBusNames[b + 1] + ": its DRIVE, GLITCH, MOD and LO-FI, on the instrument when it's routed to it");
        button.onClick = [this, b] { setBus (b); };
        addAndMakeVisible (button);
    }
    routeButton.setButtonText ("THE INSTRUMENT");
    routeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0d0a0e));
    routeButton.setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim.withAlpha (0.6f));
    routeButton.setColour (juce::TextButton::textColourOffId, ParagonColours::textDim);
    routeButton.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
    routeButton.setTooltip ("The instrument into this bus (lit), or click to route it here; click again for NONE (also INSERT BUS on the "
                            "INSTRUMENT page)");
    routeButton.onClick = [this]
    {
        auto* parameter = processor.getParameters().getParameter (ParamIDs::instInsert);
        const auto here = juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue())) == bus + 1;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (here ? 0.0f : (float) (bus + 1)));
        parameter->endChangeGesture();
        timerCallback();
    };
    addAndMakeVisible (routeButton);
    const char* names[] = { "SendDelay", "SendReverb" };
    const char* labels[] = { "TO DELAY", "TO REVERB" };
    for (int s = 0; s < 2; ++s)
    {
        sends[(size_t) s] = std::make_unique<ParamControl> (p.getParameters(), ParamIDs::ins (0, names[s]), labels[s],
                                                            juce::String ("How much of this bus goes to the ") + (s == 0 ? "DELAY" : "REVERB")
                                                                + ", after its effects (the instrument's own sends aren't used)");
        addAndMakeVisible (*sends[(size_t) s]);
    }
    busButtons[0].setToggleState (true, juce::dontSendNotification);
    timerCallback();
    startTimerHz (10);
}

void InsertBusStrip::setBus (int b)
{
    bus = juce::jlimit (0, ParamIDs::numInsertBuses - 1, b);
    busButtons[(size_t) bus].setToggleState (true, juce::dontSendNotification);
    sends[0]->attach (ParamIDs::ins (bus, "SendDelay"));
    sends[1]->attach (ParamIDs::ins (bus, "SendReverb"));
    timerCallback();
    if (onBusChanged)
        onBusChanged (bus);
}

void InsertBusStrip::timerCallback()
{
    if (! isShowing())   // 0.30.0: a hidden page's timer does nothing
        return;
    const auto routed = juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::instInsert)->load()) == bus + 1;
    routeButton.setToggleState (routed, juce::dontSendNotification);
}

void InsertBusStrip::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("PLAYING INTO THIS BUS", routeButton.getBounds().translated (0, -20).withHeight (14).withWidth (200), juce::Justification::centredLeft, false);
}

void InsertBusStrip::resized()
{
    auto area = getLocalBounds().reduced (10, 4);
    for (auto& button : busButtons)
    {
        button.setBounds (area.removeFromLeft (58).withSizeKeepingCentre (58, 30));
        area.removeFromLeft (4);
    }
    area.removeFromLeft (14);
    auto right = area.removeFromRight (212);
    sends[0]->setBounds (right.removeFromLeft (100));
    right.removeFromLeft (6);
    sends[1]->setBounds (right.removeFromLeft (100));
    routeButton.setBounds (area.withSizeKeepingCentre (area.getWidth(), 30).translated (0, 8).removeFromLeft (180).withSizeKeepingCentre (176, 28));
}
